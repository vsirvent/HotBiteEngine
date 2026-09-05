// Rasterizes the binned Gaussians, lights them with the scene's own lights and
// shadows, and writes the result into the same targets MainRenderPS writes.
//
// One thread group per 16x16 tile, one thread per pixel, over the contiguous slice of
// the entry pool that belongs to this tile.
//
// A 3DGS renderer composites baked radiance back to front. This one is producing
// *material* for a G-buffer instead - an albedo, a normal, a depth - and a G-buffer
// holds one surface per pixel however it is filled, so the question is not "what is
// the correct alpha composite" but "which surface is in front and what is it made of".
//
// THE SLICE IS EXACTLY SORTED, and everything below depends on it. The binning is a
// parallel counting sort by quantized depth (histogram, prefix scan, scatter) and its
// radix is SPLAT_DEPTH_BUCKETS - at 1024 over a span of SPLAT_MAX_DEPTH_STEP the bucket
// index equals the quantized depth, so entries sharing a bucket share a *depth* and
// there is no order between them left to get wrong. See the note in SplatCommon.hlsli.
//
// That is what buys front-to-back alpha compositing. Walking the slice in order:
//
//   a = the splat's alpha at this pixel      (centre opacity x the 2D Gaussian)
//   w = a * T                                 the share of the pixel it actually wins
//   T *= (1 - a)                              what is still visible behind it
//
// and every accumulator is weighted by w rather than by a. The difference is occlusion
// *between splats*: a splat behind an already-opaque surface gets w near zero on its
// own, so nothing has to decide where "the surface" ends. `alpha` for the pixel is
// 1 - T, which telescopes to sum(w) exactly and is therefore in [0,1] by construction
// with no saturate() anywhere.
//
// WHAT IS ACCUMULATED IS STILL MATERIAL, NOT RADIANCE. A 3DGS renderer composites baked
// colour and is done; this one is filling a G-buffer, so it composites albedo, normal
// and spec and lets the engine light the result with the level's own lights, shadows and
// ray tracing. Compositing radiance here would take the cloud out of all of that.
//
// The early-out falls out of the same state: once T is under SPLAT_MIN_T nothing further
// back can move any accumulator by more than that, so the walk stops. This is the
// "stop at the opaque surface" the design is for, and unlike the band-boundary test it
// replaced it is exact rather than granular.
//
// WHAT THIS REPLACED, because the failure mode is instructive and cheap to recreate.
// The predecessor was deliberately order-INdependent: it summed alpha-weighted material
// over every entry up to the band where coverage crossed surface_alpha, plus a tail
// (`splat_depth_slab`) of about a band past it. That worked, but only because the tail
// carried the coverage sum past 1 where saturate() flattened it. Deleting the tail and
// stopping at the crossing band - which looks like a clean simplification and was built
// and measured - drew the object in concentric alpha rings at about half opacity:
// 31.95% of the frame changed at 1.6 units, mean |delta| 19.8/255. The cause was that
// alpha was a sum over bands 0..k and therefore a step function of *which band* the
// crossing landed in. Transmittance has no such threshold anywhere, which is why the
// rings cannot come back: nothing in this walk is band-granular any more, and the
// rasterizer no longer knows what a band is.
//
// It also means `surface_alpha` now does exactly one job - gating whether the cloud owns
// the pixel's G-buffer - where it used to also decide which splats formed the surface.
//
// The cost of the whole design is that you cannot see *through* a splat cloud - which
// the G-buffer could not represent anyway.

#define HB_COMPUTE_LIGHTING 1

// Diagnostic switch. 0 walks each tile's whole slice instead of stopping once the
// remaining transmittance can no longer matter. Flipping it is how to tell "the walk
// stops too soon" from "the binning is wrong"; it found the answer once already.
//
// It is NOT bit-identical, unlike the band-granular early-out it replaced, because
// continuing to composite keeps adding contributions of size T*a - so the difference is
// bounded by SPLAT_MIN_T rather than being zero, and the check is on the distribution
// rather than on the max. Measured on the 60k fixture at 2 units:
//
//   >= 1/255   13.85% of pixels     the tolerance itself
//   >= 3/255    0.83%
//   >= 8/255    0.0026%  (93 px)
//   >= 48/255   0.0003%  (11 px)
//
// The handful of large ones are terminator pixels, where `normal` is renormalized after
// the walk so a late layer of any weight can rotate it slightly, and N.L is near zero
// there. If that tail ever matters, SPLAT_MIN_T is the knob - it costs walk length
// roughly in proportion.
//
// The same run is what shows the early-out is worth having at all, via `entries_walked`
// in splat_info: with it off that counter is exactly total_binned * 256 (every thread
// reads every entry of its tile), and with it on the fixture walks 11.5% of that at 2
// units and 15.7% at 6.
#define SPLAT_EARLY_OUT 1

#include "SplatCommon.hlsli"
#include "../Common/ShaderStructs.hlsli"
#include "../Common/PixelCommon.hlsli"

cbuffer externalData : register(b0)
{
	matrix world;
	matrix view;
	matrix projection;
	AmbientLight ambientLight;
	DirLight dirLights[MAX_LIGHTS];
	PointLight pointLights[MAX_LIGHTS];
	MaterialColor material;
	int dirLightsCount;
	int pointLightsCount;
	float3 cameraPosition;
	float3 cameraDirection;
	int screenW;
	int screenH;
	float4 LightPerspectiveValues[MAX_LIGHTS / 2];
	matrix DirPerspectiveMatrix[DIR_SHADOW_MATRIX_COUNT];
	matrix DirStaticPerspectiveMatrix[MAX_LIGHTS];
	matrix spot_view;
	float time;
	float cloud_density;
	uint  multi_texture_count;
	float multi_parallax_scale;
	uint4 packed_multi_texture_operations[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_values[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_uv_scales[MAX_MULTI_TEXTURE / 4];
	float4 multi_texture_slope[MAX_MULTI_TEXTURE];
	float4 multi_texture_height[MAX_MULTI_TEXTURE];
	float4 multi_texture_mask_uv[MAX_MULTI_TEXTURE];
}

cbuffer splatData : register(b1)
{
	uint tiles_x;
	uint tiles_y;
	// How opaque the cloud has to be at a pixel before it owns that pixel's G-buffer.
	// Its ONLY job now: the walk composites front to back and lets transmittance decide
	// what a splat is worth, so nothing here declares where a surface begins or ends.
	float surface_alpha;
	// The depth quantization window the binning used. NOT to reconstruct a band - the
	// walk never asks where an entry sits in the depth range - but to recover each
	// entry's quantized depth, which is what says whether two adjacent entries are
	// co-located and must be composited as one layer. It has to be the same window
	// SplatBinCS was given or the grouping disagrees with the sort that produced it.
	float splat_quant_min;
	float splat_quant_range;
	float splat_pad0;
	// Takes a world position of this cloud to where that point was last frame:
	// inverse(world) * prev_world, in the same row-vector convention `world` uses.
	//
	// A matrix rather than a per-splat previous position, because every splat of a cloud
	// shares one transform - so the whole previous pose is one constant, and SplatView
	// stays 60 bytes instead of growing 12 more times 1.8M splats. It is exact: there is
	// no skinning or per-splat animation for this to approximate.
	matrix prev_world_from_world;
}
// The depth quantization window, the bucket span and tile_depth were all here to let
// this pass re-derive which band the binning filed an entry into. Nothing needs a band
// any more: the slice is exactly sorted, so the walk consumes it in order and never asks
// where an entry sits in the depth range. That also frees a texture register in a shader
// that reads eight resources.

#include "../Common/MultiTexture.hlsli"
#include "../Common/PixelFunctions.hlsli"

StructuredBuffer<SplatView> splat_views : register(t20);
// Where this tile's slice starts in the pool, and how many entries it holds. There is
// no per-tile capacity and no stride: a tile owns exactly [base, base + total).
Buffer<uint> tile_base  : register(t21);
Buffer<uint> tile_total : register(t22);
Buffer<uint> splat_entries : register(t23);
// The covered tiles, densely packed by SplatCompactCS. The dispatch is indirect over
// this list rather than over the screen's tile grid, so a group exists only where there
// is something to rasterize - see the note on the pixel coordinates in main().
Buffer<uint> tile_list : register(t25);

// The same targets MainRenderPS writes, bloom included. A cloud that filled only
// scene/light/depth was invisible to everything downstream that works off the G-buffer:
// no motion vector however fast it moved, no ray-traced reflection of it, and nothing
// for ReSTIR to gather indirect light from.
RWTexture2D<float4> scene_out : register(u0);
RWTexture2D<float4> light_out : register(u1);
RWTexture2D<float>  depth_out : register(u2);
// rt_ray0/rt_ray1 are the RaySource pair: world position and world normal, each with a
// pair of material scalars packed into w. ShaderStructs.hlsli's getColor0/getColor1 do
// the packing, and are used here rather than reproduced so the layout has one owner.
RWTexture2D<float4> rt_ray0_out : register(u4);
RWTexture2D<float4> rt_ray1_out : register(u5);
// World position now and last frame. MotionCS turns the pair into motion vectors.
RWTexture2D<float4> position_out : register(u6);
RWTexture2D<float4> prev_position_out : register(u7);
// Counters for `splat_info`: how many tiles this pass actually ran over, and how many
// pixels it wrote. Without them "the cloud is not on screen" and "the cloud is not
// being rasterized" look identical from outside, which cost a debugging round.
RWByteAddressBuffer splat_stats : register(u3);
// Emission behind the cloud. This is the ninth UAV and the reason the device asks for
// feature level 11_1 (11_0 caps a compute shader at eight) - see
// DXCore::SupportsExtendedUAVSlots. On an 11_0 device DrawSplats leaves it unbound,
// which is legal: the writes below are discarded and the bloom simply is not occluded,
// exactly as it was before this existed.
//
// A splat emits nothing of its own, so unlike scene_out and light_out there is nothing
// to lerp *towards* - the cloud can only take emission away, in the proportion it hides
// whatever is behind it.
RWTexture2D<float4> bloom_out : register(u8);

// Cooperative batch. Each thread fetches one splat of the batch and every thread
// then reads all 256 out of groupshared, which turns 256 scattered loads per splat
// into one. 256 * 60 bytes = 15 KB of the 32 KB budget.
groupshared SplatView g_batch[256];
// The quantized depth of each batch entry, computed ONCE by the thread that loads it
// rather than 256 times by every thread that reads it. It is what identifies a
// co-located group below - two entries are co-located exactly when this matches, since
// the binning's bucket index and this value are the same number. 1 KB on top of the
// batch's 15 KB.
groupshared uint g_q[256];
// Entries this group's threads actually examined, summed for `splat_info`. Reduced in
// groupshared first and pushed to the global counter once per GROUP: one atomic per
// thread would be ~1M of them a frame onto a single address, which is a measurable cost
// for a diagnostic. Without it the early-out is unobservable - a walk that stops at the
// opaque surface and one that reads the whole slice produce the same image, which is
// the point of it, so the only evidence it works at all is this number.
groupshared uint g_walked;

// --- the front-to-back walk ---------------------------------------------------------
// Two levels, and the second one is not an optimisation - it is what keeps the result
// from depending on atomic scheduling.
//
// The slice is sorted to one quantization step, so entries sharing a step are adjacent
// but in whatever order the binning atomics produced, and that order changes every
// frame. IT IS TEMPTING TO CALL THAT HARMLESS BECAUSE THEY ARE AT THE SAME DEPTH. It is
// not: alpha compositing is order-dependent even between co-located splats -
// c1*a1 + c2*a2*(1-a1) is not c2*a2 + c1*a1*(1-a2) unless the colours or the alphas
// agree - so a naive composite boils. Measured on the 60k fixture, frozen clock and
// fixed camera, consecutive frames: 0.002% of pixels differing became 0.42%, worst
// delta 21/255 became 100. That is the same magnitude as the flicker every earlier
// version of this pass was rewritten to remove (0.17%, 0.51%, 1.3%).
//
// So a run of equal-depth entries is gathered as ONE layer and composited once:
//
//   material   sum(c_i * a_i) / sum(a_i)     a mean, commutative
//   coverage   1 - prod(1 - a_i)             the layer's own alpha, commutative
//
// Both are symmetric in the entries, so the answer no longer depends on their order,
// and transmittance still advances exactly once per distinct depth. Which is the
// physically sensible reading anyway: things at the same distance do not occlude each
// other in a fixed sequence, they share the pixel.
struct SplatWalk
{
	// Composited so far, each term already weighted by its layer's a * T.
	float3 albedo;
	float3 normal;
	float  spec;
	float  depth;
	float  T;
	// The layer being gathered: everything seen so far at quantized depth `q`.
	float3 g_albedo;
	float3 g_normal;
	float  g_spec;
	float  g_depth;
	float  g_alpha;   // sum(a_i), the material normalizer
	float  g_trans;   // prod(1 - a_i), the layer's transmittance
	uint   q;
};

SplatWalk SplatWalkInit()
{
	SplatWalk k;
	k.albedo = 0.0f; k.normal = 0.0f; k.spec = 0.0f; k.depth = 0.0f;
	k.T = 1.0f;
	k.g_albedo = 0.0f; k.g_normal = 0.0f; k.g_spec = 0.0f; k.g_depth = 0.0f;
	k.g_alpha = 0.0f; k.g_trans = 1.0f;
	// A sentinel no quantized depth can take, so the first entry always opens a layer.
	k.q = 0xFFFFFFFFu;
	return k;
}

// Composites the gathered layer and starts an empty one. Safe to call on an empty
// layer, which is what makes it usable both on a depth change and once at the end.
void SplatWalkFlush(inout SplatWalk k)
{
	if (k.g_alpha > 0.0f) {
		float a = 1.0f - k.g_trans;
		float w = a * k.T;
		// One divide for the whole layer: the group sums are alpha-weighted, so
		// dividing by the alpha sum turns them into the layer's mean material, and w
		// then scales that by what the layer is worth against everything in front.
		//
		// The max() is not belt-and-braces. fxc FLATTENS this branch - it warned
		// X4008 "floating point division by zero" on the bare divide - so the divide
		// executes for an empty layer too, and on that path w is 0 and g_alpha is 0.
		// 0/0 is NaN, `g_albedo * NaN` is NaN, and a NaN reaching an accumulator does
		// not stay local: it survives every later add and the pixel is dead for the
		// rest of the walk. Guarding the value rather than the control flow is the
		// only form of this that is safe under flattening.
		float s = w / max(k.g_alpha, 1e-20f);
		k.albedo += k.g_albedo * s;
		k.normal += k.g_normal * s;
		k.spec += k.g_spec * s;
		k.depth += k.g_depth * s;
		k.T *= k.g_trans;
	}
	k.g_albedo = 0.0f; k.g_normal = 0.0f; k.g_spec = 0.0f; k.g_depth = 0.0f;
	k.g_alpha = 0.0f; k.g_trans = 1.0f;
}

void SplatWalkAdd(inout SplatWalk k, SplatView s, float a, uint q)
{
	if (q != k.q) {
		SplatWalkFlush(k);
		k.q = q;
	}
	k.g_albedo += s.albedo * a;
	k.g_normal += s.normal * a;
	k.g_spec += s.spec * a;
	k.g_depth += s.view_depth * a;
	k.g_alpha += a;
	k.g_trans *= (1.0f - a);
}

[numthreads(SPLAT_TILE_SIZE, SPLAT_TILE_SIZE, 1)]
void main(uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint gi : SV_GroupIndex)
{
	// The dispatch is a ONE-DIMENSIONAL indirect one over the compacted tile list, so
	// neither SV_GroupID.xy nor SV_DispatchThreadID carries a screen position any more -
	// the group index selects a tile out of the list and the pixel has to be rebuilt from
	// that tile's place in the grid. Reading the pixel off SV_DispatchThreadID as this
	// used to would put every group in the first row of tiles.
	uint tile = tile_list[gid.x];
	uint count = tile_total[tile];
	if (count == 0) {
		return;
	}
	uint base = tile_base[tile];
	if (gi == 0) {
		uint ignored;
		splat_stats.InterlockedAdd(24, 1, ignored);
	}

	int2 px = int2(tile % tiles_x, tile / tiles_x) * SPLAT_TILE_SIZE + int2(gtid.xy);
	bool on_screen = (px.x < screenW && px.y < screenH);
	float2 pixel_centre = float2(px) + 0.5f;

	uint batches = (count + 255) / 256;
	uint b, i;

	// --- one walk: composite the slice front to back ------------------------------
	// All the state lives in the struct: what has been composited, plus the co-located
	// layer currently being gathered. See the note where SplatWalk is declared for why
	// the second level exists.
	SplatWalk k = SplatWalkInit();

	// Off-screen threads start finished. They still have to reach every barrier - a
	// group where some threads have returned and others are waiting on
	// GroupMemoryBarrierWithGroupSync is a hang, and fxc rejects it outright (X4026:
	// "thread sync operation must be in non-varying flow control") - so the *work* is
	// gated on a flag and the control flow is not.
	bool done = !on_screen;
	uint walked = 0;
	if (gi == 0) {
		g_walked = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	for (b = 0; b < batches; ++b) {
		uint load = b * 256 + gi;
		if (load < count) {
			// A pool entry is a bare splat index; its depth ordering is carried by
			// where it sits in the slice, and its real depth comes off the SplatView.
			SplatView sv = splat_views[splat_entries[base + load]];
			g_batch[gi] = sv;
			// The same quantization SplatBinCS sorted on, so equal values here are
			// exactly the entries it filed into one bucket - which, the radix being a
			// full 10 bits, is one depth step. Recovered rather than stored: it costs a
			// madd and a convert once per entry against 4 bytes per splat on a buffer
			// that can hold 1.8M of them.
			g_q[gi] = QuantizeSplatDepth(sv.view_depth, splat_quant_min, splat_quant_range);
		}
		GroupMemoryBarrierWithGroupSync();

		uint n = min(256u, count - b * 256);
		for (i = 0; !done && i < n; ++i) {
			SplatView s = g_batch[i];
			++walked;

			float2 d = pixel_centre - s.screen_xy;
			if (dot(d, d) > s.radius * s.radius) {
				continue;
			}
			// The splat's alpha AT THIS PIXEL: its centre opacity shaped by the 2D
			// Gaussian. Clamped below 1 because `alpha` already carries the component's
			// opacity_scale, which is not bounded above - and an `a` over 1 would make
			// (1 - a) negative, which does not merely brighten a pixel, it flips the
			// sign of every contribution behind it.
			float a = min(s.alpha * SplatWeight(s.conic, d), SPLAT_MAX_ALPHA);
			if (a < SPLAT_MIN_ALPHA) {
				continue;
			}
			SplatWalkAdd(k, s, a, g_q[i]);
#if SPLAT_EARLY_OUT
			// The opaque surface has been reached: everything further back is behind it
			// and can move an accumulator by at most T, which is under a 255th of what
			// is already there. Tested on the composited T, so it only fires on a layer
			// boundary - a half-gathered layer has not advanced T yet, which is exactly
			// the granularity that keeps this from depending on intra-layer order.
			if (k.T < SPLAT_MIN_T) {
				done = true;
			}
#endif
		}
		GroupMemoryBarrierWithGroupSync();
	}
	// The last layer never met a depth change to close it.
	SplatWalkFlush(k);

	// Reduce the walk lengths before anything returns - every thread of the group has to
	// reach both of these, and the alpha test below lets threads out.
	uint ignored_walk;
	InterlockedAdd(g_walked, walked, ignored_walk);
	GroupMemoryBarrierWithGroupSync();
	if (gi == 0) {
		uint ignored;
		splat_stats.InterlockedAdd(12, g_walked, ignored);
	}

	// The pixel's coverage against whatever is behind the cloud. Each layer contributed
	// a * T and the product of the (1 - a) factors is T, so the weights sum to 1 - T
	// exactly - which makes this both the alpha and the right normalizer for the means
	// below. A silhouette edge simply ends up with a small one; nothing special is done
	// for it, and nothing here needs a saturate(), T only ever falling.
	float alpha = 1.0f - k.T;
	// Past the last barrier, so returning here is safe.
	if (!on_screen || alpha < SPLAT_MIN_ALPHA) {
		return;
	}
	float surface_depth = k.depth / alpha;

	// --- occlusion, PER PIXEL ------------------------------------------------------
	// depth_out is depth_map: the world distance to the opaque surface at this pixel,
	// written by the depth pre-pass and cleared to FLT_MAX, so a pixel with nothing
	// behind it passes and a cloud still draws against the background.
	//
	// This is the test that was missing, and it is not the same as either of the two
	// that were already here:
	//
	//  - SplatPreprocessCS rejects a splat whose CENTRE is more than a slab behind the
	//    opaque depth AT ITS CENTRE PIXEL. That is per splat, not per pixel, so it only
	//    catches splats that are wholly behind something. A splat straddling an
	//    occluder's silhouette has a visible centre and survives with all of its
	//    coverage, including the part that should be hidden.
	//  - The G-buffer block below tested depth, so position/normal/depth/ray sources
	//    were correct. Only scene_out and light_out were written unconditionally - so
	//    the cloud's COLOUR was composited over geometry standing in front of it while
	//    the depth buffer said, correctly, that the geometry was nearer. That is why
	//    the artifact shows in the lit image and not in the depth view.
	//
	// Taken here rather than at the writes so an occluded pixel also skips the lighting
	// loops below, which are the expensive part of this shader.
	if (surface_depth >= depth_out[px]) {
		return;
	}
	{
		uint ignored;
		splat_stats.InterlockedAdd(28, 1, ignored);
	}

	// Transmittance-weighted means. The normalizer is the same `alpha` computed above,
	// because the weights and the coverage are the same sum - no saturate() and no
	// second accumulator.
	float3 albedo = k.albedo / alpha;
	float3 normal = normalize(k.normal);
	float spec = k.spec / alpha;

	// --- light it, with the scene's own lights -----------------------------------
	// The whole reason this component stores material rather than baked radiance:
	// these are the same functions MainRenderPS calls, so a cloud takes the level's
	// sun, its point lights and its shadow cascades exactly as the geometry beside
	// it does.
	// Where this pixel's splat surface is in the world. Rebuilt from the pixel and
	// the depth rather than interpolated, since a compute thread has no vertex to
	// carry it - but it has to be the *world* point CalcDirectional's shadow
	// lookups and CalcPoint's range test expect, so both the field of view and the
	// camera's orientation have to come back in:
	//   - dividing the NDC by projection._11/._22 undoes the perspective scaling,
	//     giving a direction in view space (where +z is forward, and the frustum's
	//     shape is exactly what those two terms encode).
	//   - `view` maps world to view for a row vector, so its upper 3x3 rotates a
	//     world direction into view space; the inverse of a rotation is its
	//     transpose, which takes the ray back out to the world.
	// view_depth is a radial distance from the camera (SplatPreprocessCS stores
	// length(world_pos - cameraPosition)), so the direction is normalized before it
	// is scaled - not divided down to a z-depth.
	float2 ndc = float2((pixel_centre.x / screenW) * 2.0f - 1.0f,
						1.0f - (pixel_centre.y / screenH) * 2.0f);
	float3 view_dir = float3(ndc.x / projection._11, ndc.y / projection._22, 1.0f);
	float3 world_dir = normalize(mul(view_dir, transpose((float3x3)view)));
	float4 world_pos = float4(cameraPosition + world_dir * surface_depth, 1.0f);

	MaterialColor m = material;
	m.specIntensity = float3(spec, spec, spec);
	// Every texture-map bit cleared - everything is per splat - but the ray tracing bit
	// kept, because that is what decides whether the ray sources written below are ones
	// the tracers will follow rather than pixels they skip.
	m.flags = material.flags & RAYTRACING_ENABLED;

	float4 bloom = float4(0.0f, 0.0f, 0.0f, 0.0f);
	float3 lum = CalcAmbient(normal);
	int li;
	for (li = 0; li < dirLightsCount; ++li) {
		lum += CalcDirectional(normal, world_pos, float2(0.0f, 0.0f), m,
							   dirLights[li], cloud_density, li, bloom);
	}
	for (li = 0; li < pointLightsCount; ++li) {
		if (length(world_pos.xyz - pointLights[li].Position) < pointLights[li].Range) {
			lum += CalcPoint(normal, world_pos.xyz, float2(0.0f, 0.0f), m,
							 pointLights[li], li, bloom);
		}
	}

	// scene holds the albedo and light_map the lighting, because the mixer
	// multiplies the two (TextureMixerCS: color * (l + rt0 + rt2) + ...). Writing a
	// pre-multiplied colour into scene would have it lit a second time.
	scene_out[px] = float4(lerp(scene_out[px].rgb, albedo, alpha), 1.0f);
	light_out[px] = float4(lerp(light_out[px].rgb, lum, alpha), 1.0f);

	// Emission is the one channel a splat only ever subtracts from. bloom_map holds what
	// the geometry passes emitted at this pixel, and the mixer adds it back after a blur
	// (TextureMixerCS: ... + b + ...); with the cloud composited into scene but not into
	// bloom, a fire behind the cloud had its colour correctly hidden and its *glow* added
	// on top regardless - so it read as shining through the model.
	//
	// alpha is 1 - T, the share of the pixel the cloud won, so 1 - alpha is exactly the
	// transmittance still reaching the camera from behind it. Scaling by that is the same
	// compositing the two lerps above do, against an emission of zero. Unconditional
	// rather than gated on surface_alpha, because this is a blend and not a claim on the
	// pixel: a cloud edge covering a third of a pixel should dim the glow behind it by a
	// third, exactly as it dims the colour.
	bloom_out[px] = float4(bloom_out[px].rgb * (1.0f - alpha), 1.0f);

	// --- the G-buffer ------------------------------------------------------------
	// One surface per pixel, so unlike scene and light there is nothing to blend into:
	// either this cloud owns the pixel or the geometry behind it does. Being in front is
	// no longer part of this test because the occlusion early-out above already
	// established it - one thread owns one pixel and nothing between here and there
	// writes depth_out - so what is left is the coverage rule: a partial edge leaves the
	// surface behind it in place.
	if (alpha > surface_alpha) {
		depth_out[px] = surface_depth;

		// Where this point was last frame. For a cloud that is one matrix - no skinning
		// and no per-splat animation - so a cloud that has not moved writes the same
		// value into both maps and MotionCS reads zero motion from it, which is right.
		float4 prev_world_pos = mul(world_pos, prev_world_from_world);
		position_out[px] = float4(world_pos.xyz, 1.0f);
		prev_position_out[px] = float4(prev_world_pos.xyz / prev_world_pos.w, 1.0f);

		// The ray sources, packed exactly as MainRenderPS packs them. dispersion and
		// reflex follow the same rule it uses: a traced surface takes them from the
		// material, an untraced one is marked with dispersion 2 so the tracers pass
		// over the pixel instead of firing a ray at it.
		RaySource ray;
		ray.orig = world_pos.xyz;
		ray.normal = normal;
		if (m.flags & RAYTRACING_ENABLED) {
			ray.dispersion = saturate(1.0f - spec);
			ray.reflex = material.rt_reflex;
		}
		else {
			ray.dispersion = 2.0f;
			ray.reflex = 0.0f;
		}
		ray.density = material.density;
		ray.opacity = alpha;
		rt_ray0_out[px] = getColor0(ray);
		rt_ray1_out[px] = getColor1(ray);
	}
}
