#ifndef __SPLAT_COMMON_HLSLI__
#define __SPLAT_COMMON_HLSLI__

// Shared layout for the Gaussian splat passes.
//
// SplatVertex mirrors Core::SplatVertex field for field. It is read as a
// StructuredBuffer, so the stride must match sizeof(SplatVertex) == 80 exactly and
// NOTHING VALIDATES THAT - a field added on one side and not the other shifts every
// subsequent read and renders as plausible-but-wrong geometry rather than as an
// error. Edit the two together.
struct SplatVertex
{
	float3 position;
	float  opacity;
	float3 cov_diag;        // Sxx, Syy, Szz
	float  spec_intensity;
	float3 cov_offdiag;     // Sxy, Sxz, Syz
	float  pad0;
	float3 albedo;
	float  pad1;
	float3 normal;
	float  pad2;
};

// What the preprocess pass hands the rasterizer: a splat already projected to
// screen space.
//
// 60 bytes: fxc packs a structured buffer element tightly on 4-byte boundaries,
// with none of the cbuffer float4 rules, so this is 15 floats and nothing more.
// RenderSystem::SplatView mirrors it field for field and static_asserts that
// number - if a field is added here, add it there too and re-read the stride out
// of the disassembly (`fxc /dumpbin SplatRasterCS.cso` ->
// `dcl_resource_structured t20, <stride>`) rather than trusting sizeof() on
// either side.
struct SplatView
{
	float2 screen_xy;   // projected centre, pixels
	float3 conic;       // inverse 2D covariance, upper triangle (a, b, c)
	float  view_depth;  // distance from the camera, world units
	float3 albedo;
	float3 normal;      // world space, already flipped if the component asked
	float  alpha;       // centre opacity, after the component's opacity_scale
	float  spec;
	float  radius;      // 3 sigma screen radius, pixels
};

// 16x16 pixels. Matches the thread group so one group owns one tile, and is the
// size the reference implementation settled on: 8x8 quadruples the group count and
// the per-tile setup cost, 32x32 is the DX11 group cap and leaves no headroom.
#define SPLAT_TILE_SIZE 16

// There is no per-tile capacity. A tile's entries live in a contiguous slice of one
// global pool, at an offset a prefix sum hands it, so a tile takes exactly the room
// it needs and a dense one cannot crowd out a sparse one.
//
// What that replaced is worth recording, because the failure was invisible until
// somebody looked at a magnified screenshot. A fixed capacity is sized for the worst
// tile *times every tile* - covering the 24k-entry tile a 1.8M splat capture produces
// would have cost 780 MB - so in practice it was set far below what tiles wanted, and
// the excess was dropped. The dropped fraction differed per tile, so the surface lost
// a different share of itself on each side of every tile boundary: a 16x16 grid,
// flickering, on an image that otherwise looked like a plausible cloud.
//
// Depth buckets. A tile's slice is ordered front to back at bucket granularity, and
// that ordering is a *side effect of the layout* rather than a sort - the histogram is
// over (tile, bucket) instead of (tile), so the scatter drops each entry into its own
// depth band for free. It is what lets the rasterizer stop walking a tile early.
// Nothing depends on the order being exact: entries inside one bucket are in whatever
// order the atomics produced, and the rasterizer's accumulation is commutative.
//
// The bands are measured from each tile's own nearest splat and span a small multiple
// of SPLAT_DEPTH_SLAB (SPLAT_BUCKET_SPAN_SLACK in RenderSystem.h), not the cloud's
// whole depth. Spanning the cloud would drop every entry of a given tile into two or
// three bands and order nothing; spanning the slab puts the resolution exactly where
// the rasterizer's early-out needs it, and everything further back - which the
// per-pixel slab will reject anyway - is clamped into the last band.
#define SPLAT_DEPTH_BUCKETS 128

// Threads per group in the scan passes. SplatScanCS uses SPLAT_DEPTH_BUCKETS threads
// (one per bucket of one tile); this is SplatBaseCS, which scans the per-tile totals.
#define SPLAT_SCAN_GROUP 256

// Threads per group in SplatCompactCS, which is one thread per tile of the screen.
#define SPLAT_COMPACT_GROUP 256

// A pool entry is a bare splat index. Nothing is packed alongside it: depth ordering
// is carried by *where* the entry sits in its tile's slice, and the rasterizer reads
// the real float depth off the SplatView it loads anyway. The packed (depth, index)
// key the sort-based predecessor needed is gone with the sort.
//
// The quantized depth still exists, but only as the currency the near-depth pass and
// the binning passes compare in. It is a *priority*, never a value, so 10 bits is
// ample. RenderSystem fits the range to the cloud's own bounding sphere each frame
// rather than to the camera's clip planes, which at 0.01/1000 would put an entire
// cloud inside one step.
#define SPLAT_DEPTH_BITS 10
#define SPLAT_MAX_DEPTH_STEP ((1u << SPLAT_DEPTH_BITS) - 1u)
// tile_depth is cleared to this, so the first splat to touch a tile wins the
// InterlockedMin and a tile nothing touches keeps a depth no splat can be within.
#define SPLAT_NO_DEPTH 0xFFFFFFFFu

// Three passes quantize the same depth and they must agree exactly - the near-depth
// pass writes one of these into tile_depth, and the counting and scattering passes
// both compare against it - so the arithmetic lives here rather than three times.
uint QuantizeSplatDepth(float view_depth, float quant_min, float quant_range)
{
	float d01 = (view_depth - quant_min) / max(1e-6f, quant_range);
	return (uint)(saturate(d01) * (float)SPLAT_MAX_DEPTH_STEP + 0.5f);
}

// Which depth band of a tile an entry belongs in: where it sits inside the span
// measured from that tile's nearest splat. Nothing is ever rejected by this - entries
// past the last band clamp into it - because the span is anchored to a per-tile
// minimum, and anything that *discards* against a per-tile threshold steps at every
// tile boundary and draws the 16x16 grid this design exists to avoid.
//
// This MUST return the same answer in the counting pass and the scattering pass. They
// run over the same splats with the same inputs, and the histogram the first builds is
// the exact allocation the second writes into - a single entry disagreeing would write
// past a bucket's slice and into the next one's. That is why both passes are the same
// shader with a flag rather than two shaders that look alike.
uint SplatDepthBucket(uint q, uint near_q, uint bucket_span_steps)
{
	uint delta = (q > near_q) ? (q - near_q) : 0u;
	uint bucket = (delta * SPLAT_DEPTH_BUCKETS) / max(1u, bucket_span_steps);
	return min(bucket, (uint)(SPLAT_DEPTH_BUCKETS - 1));
}


// A splat's keep/drop draw for the screen-coverage LOD. PCG-style integer hash, and it
// takes the splat index and NOTHING ELSE - no frame counter, no camera. The decision has
// to be the same every frame or the cloud boils, and because it is a threshold on a
// fixed per-splat value, raising the keep probability only ever *adds* splats to the set
// already being drawn, so moving toward a cloud fades detail in rather than reshuffling
// it.
float SplatHash01(uint x)
{
	x = x * 747796405u + 2891336453u;
	uint w = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
	w = (w >> 22u) ^ w;
	return (float)w * (1.0f / 4294967296.0f);
}

// Below this the Gaussian contributes under ~1/255 and is skipped. Not an
// occlusion test - it is a coverage test, and it is what lets the accumulation
// loops terminate early on the great majority of splat/pixel pairs.
#define SPLAT_MIN_ALPHA (1.0f / 255.0f)

// How far behind the surface a splat may still contribute is NOT a constant here - it
// is splat_depth_slab, which RenderSystem hands over as a WORLD thickness
// (SPLAT_SLAB_WORLD, floored at one depth band). It used to be a fraction of the
// cloud's own depth extent, which is the wrong thing to measure: a surface is the same
// couple of centimetres thick whatever the size of the capture it was cut out of.
//
// The failure that used to make it dangerous to shrink is gone. When the slab was
// measured from the nearest splat with any coverage it decided which surface filled a
// pixel, so at an internal depth jump - a tentacle crossing in front of another - the
// near surface contributed only its grazing edge, the surface actually covering the
// pixel sat beyond the slab, and the pixel wrote nothing: the background showed through
// as a 1-2 px seam tracing every overlap, 332 of them in one view of a 1-unit capture.
// The surface is found by a coverage threshold now and the slab is applied only
// afterwards, so no value of it can stop a pixel finding a surface.
//
// What is left is a tail over the surface's own splats, so too small is now the benign
// end - a slightly noisier average - and too large is the one with a symptom: the next
// surface back is averaged in, the cloud reads as semi-transparent and the depth it
// writes sits behind the object. It costs time as well, and a tenth of the cloud's
// depth was enough to hang the driver on a 1.8M splat capture.

// Evaluates the 2D Gaussian at `d` pixels from the splat centre.
float SplatWeight(float3 conic, float2 d)
{
	float power = -0.5f * (conic.x * d.x * d.x + conic.z * d.y * d.y) - conic.y * d.x * d.y;
	// power is <= 0 by construction for a positive-definite conic; the clamp is
	// against a degenerate one, where exp() of a positive number would explode.
	return exp(min(0.0f, power));
}

#endif
