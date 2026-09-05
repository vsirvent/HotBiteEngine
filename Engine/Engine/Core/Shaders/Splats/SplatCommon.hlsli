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
// depth bucket for free.
//
// WHICH IS TO SAY THE BINNING *IS* A PARALLEL COUNTING SORT BY DEPTH, and this constant
// is its precision - the width of the single radix digit it sorts on. That is the whole
// reason there is no separate sort pass: histogram, prefix scan, scatter is a counting
// sort already, and making the slice finer is a matter of widening the digit rather
// than of bolting a comparison sort onto the end.
//
// The bucket is the cloud's own quantized depth (QuantizeSplatDepth, over the near/far
// window RenderSystem fits to the cloud's bounding sphere each frame) with no per-tile
// window or step size on top: every tile's entries are sorted against the same global
// near/far span, so nothing is ever clamped or piled into a bucket it does not belong
// in. An earlier version anchored each tile's bands to its own nearest splat instead
// (tile_depth, still kept for SplatCompactCS's occupancy test - see its header), which
// needed a second constant, the number of quantization steps one band spans, to turn
// the per-tile delta back into a bucket index. That bought nothing: the span was always
// set to the cloud's whole range (the only value that does not clamp entries into the
// last bucket - see the paragraph below on why a narrower one was reverted), so the
// per-tile offset it computed was mathematically a shift of the same global bucket
// index, never a finer one. Binning directly on the global quantized depth is the same
// result with one fewer buffer read, one fewer cbuffer field, and no per-tile state to
// keep in step with it.
//
// 1024 IS NOT AN ARBITRARY INCREASE - IT IS THE POINT WHERE THE SORT BECOMES EXACT.
// SPLAT_DEPTH_BUCKETS is 1024 and QuantizeSplatDepth's output span is SPLAT_MAX_DEPTH_STEP
// (1023), so the bucket index equals the quantized depth for every value it can take: one
// bucket per quantization step, two entries sharing a bucket have *identical* stored
// depth, so no ordering exists between them to get wrong, and no comparison sort could
// separate them either.
//
// Narrowing that window to put finer buckets where a cloud's surface actually lives was
// tried and reverted, for the same reason a per-tile window was: everything past a
// narrow window piles into the final bucket as an unordered mass, and the rasterizer's
// walk is only sound where the ordering is real. Measured at a 0.1x window it put the
// tile-edge gradient ratio back to 1.68x and returned 0.17% flicker to a frame that was
// otherwise bit-identical.
//
// It follows that going finer means more depth BITS, not more buckets - and that this
// number and SPLAT_DEPTH_BITS have to move together or the 1:1 property quietly breaks
// back into an approximation. It was 128 (a 7-bit digit) while the rasterizer's
// accumulation was commutative and only needed buckets for an early-out; transmittance
// compositing is order-dependent, so the digit had to widen to the full 10.
//
// The cost is the histogram, which is tiles * this: 7.1 MB at 1080p and 57 MB at the
// 2560x1377 the editor runs at, cleared once per cloud per frame. SplatScanCS is one
// thread per bucket, so this also pins that dispatch at the 1024-thread D3D11 group
// cap - it cannot go higher without restructuring that scan.
#define SPLAT_DEPTH_BUCKETS 1024

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
// The quantized depth still exists, but only as the currency the binning passes and the
// rasterizer's co-location test share. It is a *priority*, never a value, so 10 bits is
// ample. RenderSystem fits the range to the cloud's own bounding sphere each frame
// rather than to the camera's clip planes, which at 0.01/1000 would put an entire
// cloud inside one step. It IS the bucket index directly - see the note on
// SPLAT_DEPTH_BUCKETS above - so nothing derives a bucket from it beyond this function.
#define SPLAT_DEPTH_BITS 10
#define SPLAT_MAX_DEPTH_STEP ((1u << SPLAT_DEPTH_BITS) - 1u)
// tile_depth is cleared to this, so the first splat to touch a tile wins the
// InterlockedMin and a tile nothing touches keeps a depth no splat can be within. Used
// only for SplatCompactCS's occupancy test now - see its header.
#define SPLAT_NO_DEPTH 0xFFFFFFFFu

// Every pass that needs a splat's priority - SplatPreprocessCS's per-tile minimum,
// SplatBinCS's bucket index, SplatRasterCS's co-location test - quantizes the same
// view_depth over the same near/far window and must agree exactly, so the arithmetic
// lives here rather than three times.
uint QuantizeSplatDepth(float view_depth, float quant_min, float quant_range)
{
	float d01 = (view_depth - quant_min) / max(1e-6f, quant_range);
	return (uint)(saturate(d01) * (float)SPLAT_MAX_DEPTH_STEP + 0.5f);
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

// Ceiling on a splat's per-pixel alpha in the compositing walk. Not a stylistic clamp:
// SplatView::alpha carries Components::SplatCloud::opacity_scale, which is not bounded
// above, so `a` can exceed 1 - and then (1 - a) is NEGATIVE, which does not brighten a
// pixel, it flips the sign of everything composited behind it and drives transmittance
// away from zero instead of toward it. Just under 1 rather than 1 so a single splat can
// never take transmittance to exactly zero, which would make every later weight
// identically 0 and hide an ordering bug behind a degenerate case.
#define SPLAT_MAX_ALPHA 0.999f

// Where the front-to-back walk stops: once this little of the background still shows
// through, nothing further back can move an accumulator by more than this fraction of
// what is already there. The direct analogue of SPLAT_MIN_ALPHA, one splat versus the
// whole remaining slice, and the reason the pass costs what the visible surface costs
// rather than what the tile holds.
#define SPLAT_MIN_T (1.0f / 255.0f)

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
//
// "BENIGN" DOES NOT EXTEND TO ZERO, and that was measured rather than assumed - see
// the header of SplatRasterCS for the numbers. Removing the tail entirely and stopping
// the walk at the crossing band is a tempting simplification (it deletes a cbuffer
// field, two float comparisons and three variables) and it is wrong: the tail is what
// carries acc_w past 1, where saturate() flattens it. Without it a pixel's alpha is
// the sum over bands 0..k and therefore a STEP FUNCTION of which band the crossing
// landed in - concentric alpha rings across the object, and a cloud at half opacity.

// Evaluates the 2D Gaussian at `d` pixels from the splat centre.
float SplatWeight(float3 conic, float2 d)
{
	float power = -0.5f * (conic.x * d.x * d.x + conic.z * d.y * d.y) - conic.y * d.x * d.y;
	// power is <= 0 by construction for a positive-definite conic; the clamp is
	// against a degenerate one, where exp() of a positive number would explode.
	return exp(min(0.0f, power));
}

#endif
