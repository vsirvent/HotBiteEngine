// Bins each projected splat into the tiles it covers. Runs TWICE per cloud, over the
// same splats, with `bin_pass` selecting what it does with each (splat, tile) pair it
// finds:
//
//   pass 0 (count)    InterlockedAdd into the (tile, bucket) histogram, writing
//                     nothing else. Afterwards SplatScanCS and SplatBaseCS turn that
//                     histogram into an offset for every bucket of every tile.
//   pass 1 (scatter)  InterlockedAdd into those offsets, which are now cursors, and
//                     write the splat index at the slot it hands back.
//
// One shader rather than two because the two passes must enumerate *exactly* the same
// pairs and file each into exactly the same bucket. The first pass's histogram is the
// second pass's allocation: a single pair counted in one and not the other, or filed
// under a different bucket, writes past its slice and into the neighbouring bucket's
// entries. Two shaders that merely look alike would drift the first time the tile
// range or the cull is touched.
//
// What this replaced, and why: a fixed per-tile capacity with the excess dropped. The
// dropped share differed per tile, so the surface lost a different fraction of itself
// on each side of every tile boundary - a 16x16 grid over the cloud, changing every
// frame. There is no capacity now; a tile takes exactly the room the histogram says it
// needs.

#include "SplatCommon.hlsli"

cbuffer externalData : register(b0)
{
	uint  splat_count;
	uint  tiles_x;
	uint  tiles_y;
	uint  bin_pass;            // 0 = count, 1 = scatter
	float depth_quant_min;
	float depth_quant_range;
	// The cloud's whole quantized depth range (SPLAT_MAX_DEPTH_STEP), so the bands this
	// pass files entries into span everything and nothing is clamped into the last one.
	// In quantization steps, so the comparison is against exactly what tile_depth holds.
	// SplatRasterCS MUST be handed the same value - it re-derives each entry's band from
	// this to know which bucket the entry was filed in.
	uint  bucket_span_steps;
	uint  entry_capacity;      // size of the pool, in entries
}

StructuredBuffer<SplatView> splat_views : register(t0);
Buffer<uint> tile_depth : register(t1);
// Where each tile's slice starts in the pool. Only read in the scatter pass - it does
// not exist yet during the count.
Buffer<uint> tile_base : register(t2);

// Count pass: the (tile, bucket) histogram. Scatter pass: the same array, now holding
// each bucket's offset *within its tile*, used as a cursor.
RWBuffer<uint> bucket_offsets : register(u0);
RWBuffer<uint> splat_entries : register(u1);
RWByteAddressBuffer splat_stats : register(u2);

[numthreads(256, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
	uint idx = tid.x;
	if (idx >= splat_count) {
		return;
	}

	SplatView v = splat_views[idx];
	// SplatPreprocessCS zeroes the radius of everything it rejected. The view buffer is
	// never cleared - it is one entry per splat, and clearing it would cost more than
	// the pass that fills it - so this is the only thing separating a live entry from
	// one left over from an earlier frame.
	if (v.radius <= 0.0f) {
		return;
	}

	uint q = QuantizeSplatDepth(v.view_depth, depth_quant_min, depth_quant_range);

	float2 lo = v.screen_xy - v.radius;
	float2 hi = v.screen_xy + v.radius;
	int tx0 = max(0, (int)(lo.x) / SPLAT_TILE_SIZE);
	int ty0 = max(0, (int)(lo.y) / SPLAT_TILE_SIZE);
	int tx1 = min((int)tiles_x - 1, (int)(hi.x) / SPLAT_TILE_SIZE);
	int ty1 = min((int)tiles_y - 1, (int)(hi.y) / SPLAT_TILE_SIZE);

	for (int ty = ty0; ty <= ty1; ++ty) {
		for (int tx = tx0; tx <= tx1; ++tx) {
			uint tile = ty * tiles_x + tx;

			// The tile's nearest splat, which is what the depth bands are measured from.
			// Nothing is rejected against it, and that is a deliberate reversal: an
			// earlier version culled everything more than a window behind it, and
			// because the window is anchored to a *per-tile* minimum, the threshold
			// stepped at every tile boundary and so did the set of splats that
			// survived. That drew the 16x16 grid this pass exists to not draw - the
			// same artifact the fixed per-tile capacity used to cause, from a different
			// direction. It also cost real coverage: removing it raised the pixels this
			// pass writes by about a fifth.
			//
			// Bucketing against the same value is safe where culling was not, because
			// a bucket only decides *where in the slice* an entry lands, and the
			// rasterizer's accumulation is order-independent. Entries past the last
			// band are clamped into it rather than dropped.
			uint near_q = tile_depth[tile];
			if (near_q == SPLAT_NO_DEPTH) {
				near_q = q;
			}

			uint slot = tile * SPLAT_DEPTH_BUCKETS +
						SplatDepthBucket(q, near_q, bucket_span_steps);
			uint at;
			InterlockedAdd(bucket_offsets[slot], 1, at);

			if (bin_pass != 0) {
				// `at` is now the offset within the tile, because the scan seeded this
				// cursor with the bucket's own start. The tile's own base turns it into
				// a pool index.
				uint dst = tile_base[tile] + at;
				if (dst < entry_capacity) {
					splat_entries[dst] = idx;
				}
				else {
					// The pool was sized from a previous frame's measurement and this
					// frame wants more (a cut to a closer camera, typically). Counted so
					// splat_info reports it and RenderSystem grows for the next frame,
					// rather than the cloud quietly losing its far side.
					uint ignored;
					splat_stats.InterlockedAdd(20, 1, ignored);
				}
			}
		}
	}
}
