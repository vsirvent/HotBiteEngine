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
// EVERY RESULT HERE IS ORDER-INDEPENDENT, and that is deliberate rather than
// incidental. The slice arrives ordered front to back only at *bucket* granularity -
// SPLAT_DEPTH_BUCKETS bands across the cull window, with entries inside one band in
// whatever order the binning atomics produced - so anything that depended on the exact
// order would depend on atomic scheduling, which changes every frame. That is the bug
// this pass was rewritten to remove: `surface_depth` used to be "the entry at which a
// running coverage sum crossed surface_alpha", which moved frame to frame and dragged
// the reconstructed world position, the shadow lookup and depth_map with it.
//
// One walk, accumulating alpha-weighted sums - including sum(w * view_depth), so
// surface_depth is the weighted *mean* depth of what was gathered, a commutative
// quantity with no notion of "first".
//
// The bucket order is used for two things, and neither can get the sums wrong. One is
// an early-out: the walk stops once an entry is far enough past the tail that no later
// entry can be inside it, with one bucket width of slack because the ordering is
// coarse, so being approximate costs a few extra entries walked and never a wrong
// result. The other is the pair of decisions that need a front-to-back prefix - where
// the surface is, and where the pixel became opaque - and both are taken only at a
// *band boundary*, where the prefix is a sum over a fixed set and so is order-
// independent again.
//
// The cost of the whole design is that you cannot see *through* a splat cloud - which
// the G-buffer could not represent anyway.

#define HB_COMPUTE_LIGHTING 1

// Diagnostic switch. 0 walks each tile's whole slice instead of stopping early, which
// must produce an identical image - the early-out is bounded by slack that makes it
// conservative. Flipping this is how to tell "the walk stops too soon" from "the
// binning is wrong"; it found the answer once already.
//
// It covers the *distance* early-out only. The opaque one below is not under it,
// because that one changes which entries are gathered by design - a pixel that is
// already opaque must not keep averaging in the surface behind it - so putting it here
// would mean this switch no longer produced an identical image and stopped being able
// to answer the question it exists for.
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
	float surface_alpha;
	// World width of one depth bucket. The slice is ordered only to this granularity,
	// so both early-outs carry exactly this much slack: an entry can be out of order
	// with its neighbours by less than a bucket, never by more.
	float splat_bucket_width;
	// How far behind a pixel's nearest splat another may still contribute. Fitted to the
	// cloud's depth extent by RenderSystem - see the note in SplatCommon.hlsli for why a
	// world constant leaves seams at every internal depth jump.
	float splat_depth_slab;
	// Takes a world position of this cloud to where that point was last frame:
	// inverse(world) * prev_world, in the same row-vector convention `world` uses.
	//
	// A matrix rather than a per-splat previous position, because every splat of a cloud
	// shares one transform - so the whole previous pose is one constant, and SplatView
	// stays 60 bytes instead of growing 12 more times 1.8M splats. It is exact: there is
	// no skinning or per-splat animation for this to approximate.
	matrix prev_world_from_world;
	// The depth quantization window the binning used, so this pass can reconstruct the
	// band boundaries the entries were filed against.
	float splat_quant_min;
	float splat_quant_range;
	float splat_pad0;
	float splat_pad1;
}

#include "../Common/MultiTexture.hlsli"
#include "../Common/PixelFunctions.hlsli"

StructuredBuffer<SplatView> splat_views : register(t20);
// Where this tile's slice starts in the pool, and how many entries it holds. There is
// no per-tile capacity and no stride: a tile owns exactly [base, base + total).
Buffer<uint> tile_base  : register(t21);
Buffer<uint> tile_total : register(t22);
Buffer<uint> splat_entries : register(t23);
// The tile's nearest splat, quantized. The bands are measured from it, so it is what
// this pass needs to snap the surface depth to a band boundary.
Buffer<uint> tile_depth : register(t24);

// The same seven targets MainRenderPS writes, minus bloom (a splat has no emission).
// A cloud that filled only scene/light/depth was invisible to everything downstream
// that works off the G-buffer: no motion vector however fast it moved, no ray-traced
// reflection of it, and nothing for ReSTIR to gather indirect light from.
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

// Cooperative batch. Each thread fetches one splat of the batch and every thread
// then reads all 256 out of groupshared, which turns 256 scattered loads per splat
// into one. 256 * 60 bytes = 15 KB of the 32 KB budget.
groupshared SplatView g_batch[256];

[numthreads(SPLAT_TILE_SIZE, SPLAT_TILE_SIZE, 1)]
void main(uint3 gid : SV_GroupID, uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
	uint tile = gid.y * tiles_x + gid.x;
	uint count = tile_total[tile];
	if (count == 0) {
		return;
	}
	uint base = tile_base[tile];
	if (gi == 0) {
		uint ignored;
		splat_stats.InterlockedAdd(24, 1, ignored);
	}

	int2 px = int2(dtid.xy);
	bool on_screen = (px.x < screenW && px.y < screenH);
	float2 pixel_centre = float2(px) + 0.5f;

	uint batches = (count + 255) / 256;
	uint b, i;

	// --- one walk: find the surface and accumulate it -----------------------------
	// The surface is the depth at which coverage accumulated front to back first
	// reaches surface_alpha, and everything up to it plus a short tail is what gets
	// averaged.
	//
	// NOT the nearest splat with any coverage, which is what this used to take, and the
	// difference is the whole reason the pass stopped leaving seams. A fixed slab
	// measured from the nearest splat has two jobs pulling against each other: at an
	// internal depth jump - a tentacle crossing in front of another - the near geometry
	// contributes only its grazing edge, so the slab has to be wide enough to reach the
	// surface *behind* it or the pixel writes nothing and the background shows through.
	// But a slab that wide averages front and back together everywhere else and the
	// cloud goes milky. Measured on a 1-unit capture: 0.05 of its depth left 332 seam
	// pixels in one view, 0.30 cleared them but rendered the object semitransparent, and
	// 0.50 hung the driver. A coverage threshold has no such conflict - a grazing edge
	// simply never reaches it, so the walk carries on by itself until something does.
	//
	// THE CROSSING IS DECLARED ONLY AT A BAND BOUNDARY, and that is what makes an
	// order-dependent quantity safe here. A running total is a prefix, so it needs the
	// front-to-back order, and the slice has that only to band granularity - entries
	// inside one band are in whatever order the atomics produced, which changes every
	// frame. Taking the crossing splat's own depth let the surface wander inside a band
	// frame to frame, moving the accumulated set: 0.51% of the lit frame and 1.21% of
	// the normal buffer flickering on a frozen clock and a fixed camera.
	//
	// The band is immune to that. Coverage accumulated *before* a band is a sum over a
	// fixed set, so it is order-independent; whether the total crosses inside a given
	// band follows from that, so it is order-independent too. Only the position within
	// the band is not, and that is exactly what is discarded.
	//
	// One walk rather than two. The predecessor found the crossing in one pass and
	// re-walked to accumulate; once the answer became band-granular the two collapse,
	// because the set pass 2 wanted is "every band up to the crossing band, plus the
	// tail" - which is precisely what has already been accumulated by the time the
	// boundary is reached. Deferring the test to the boundary is what makes that exact
	// rather than approximate: if a mid-band prefix crosses inside band k then the full
	// band sum does too, and the coverage before band k is below the threshold either
	// way, so both formulations name the same band.
	float3 acc_albedo = 0.0f;
	float3 acc_normal = 0.0f;
	float acc_spec = 0.0f;
	float acc_w = 0.0f;
	float acc_depth = 0.0f;
	float surf = 0.0f;
	// No ceiling until the surface has been found; the walk is bounded by the slice.
	float limit = 1e30f;
	bool started = false;
	bool crossed = false;
	// The band being accumulated, and the first one that covered this pixel at all.
	float cur_band = -1.0f;
	float first_band = 0.0f;

	// Where this tile's bands start, in world units. tile_depth holds the quantized
	// depth of its nearest splat, which is the origin SplatBinCS measured bands from.
	const uint tile_near_q = tile_depth[tile];
	const float near_world = splat_quant_min +
		((float)tile_near_q / (float)SPLAT_MAX_DEPTH_STEP) * splat_quant_range;
	const float inv_band = 1.0f / max(1e-6f, splat_bucket_width);

	// Off-screen threads start finished. They still have to reach every barrier - a
	// group where some threads have returned and others are waiting on
	// GroupMemoryBarrierWithGroupSync is a hang, and fxc rejects it outright (X4026:
	// "thread sync operation must be in non-varying flow control") - so the *work* is
	// gated on a flag and the control flow is not.
	bool done = !on_screen;

	for (b = 0; b < batches; ++b) {
		uint load = b * 256 + gi;
		if (load < count) {
			// A pool entry is a bare splat index; its depth ordering is carried by
			// where it sits in the slice, and its real depth comes off the SplatView.
			g_batch[gi] = splat_views[splat_entries[base + load]];
		}
		GroupMemoryBarrierWithGroupSync();

		uint n = min(256u, count - b * 256);
		for (i = 0; !done && i < n; ++i) {
			SplatView s = g_batch[i];
			float band = floor((s.view_depth - near_world) * inv_band);

			// A band boundary: everything before this band is in, so this is the one
			// point at which the crossing may be declared - and the one point at which
			// the walk may stop for being opaque.
			if (band != cur_band) {
				if (!crossed && started && acc_w >= surface_alpha) {
					crossed = true;
					surf = near_world + (cur_band + 1.0f) * splat_bucket_width;
					limit = surf + splat_depth_slab;
				}
				// The pixel is already fully covered, so nothing behind it can be seen
				// through it - `alpha` below is saturate(acc_w), so at 1 the composite
				// replaces what is behind entirely, and every further entry would only
				// pull albedo, normal and depth toward a surface this one occludes. That
				// is the milky, too-deep look, and the slab alone cannot prevent it: a
				// tail wide enough to span a thick surface also reaches the next surface
				// wherever this one is thin.
				//
				// AT A BAND BOUNDARY, exactly like the crossing above and for the same
				// reason. Coverage accumulated before a band is a sum over a fixed set,
				// so both this test and the set it stops at are order-independent;
				// stopping the moment acc_w crossed *inside* a band would keep whichever
				// entries the binning atomics happened to put first, which changes every
				// frame - the flicker this pass was rewritten to remove.
				//
				// Deliberately not under SPLAT_EARLY_OUT: this is what the pass means by
				// a surface rather than an optimisation of it, so it holds in both
				// settings of that switch and leaves its "identical image" contract
				// intact.
				if (crossed && acc_w >= SPLAT_OPAQUE_ALPHA) {
					done = true;
				}
				cur_band = band;
				if (done) {
					continue;
				}
			}

#if SPLAT_EARLY_OUT
			// Past the tail, and the slice is ordered, so nothing further can be inside
			// it - with one band of slack, because the ordering is only that fine.
			if (s.view_depth > limit + splat_bucket_width) {
				done = true;
				continue;
			}
#endif
			// The exact test, applied whether or not the early-out let an extra band
			// through, so the slack costs entries walked and never a wrong sum.
			if (s.view_depth > limit) {
				continue;
			}
			float2 d = pixel_centre - s.screen_xy;
			if (dot(d, d) > s.radius * s.radius) {
				continue;
			}
			float w = s.alpha * SplatWeight(s.conic, d);
			if (w < SPLAT_MIN_ALPHA) {
				continue;
			}
			if (!started) {
				started = true;
				first_band = band;
			}
			acc_albedo += s.albedo * w;
			acc_normal += s.normal * w;
			acc_spec += s.spec * w;
			acc_depth += s.view_depth * w;
			acc_w += w;
		}
		GroupMemoryBarrierWithGroupSync();
	}

	bool contributes = on_screen && started;
	// A pixel whose coverage never reached the threshold - a silhouette edge with the
	// background genuinely behind it. It keeps whatever partial alpha it gathered, and
	// its surface is the first band that covered it.
	if (!crossed && started) {
		surf = near_world + (first_band + 1.0f) * splat_bucket_width;
	}
	// Past the last barrier, so returning here is safe.
	if (!contributes || acc_w < SPLAT_MIN_ALPHA) {
		return;
	}
	{
		uint ignored;
		splat_stats.InterlockedAdd(28, 1, ignored);
	}
	float surface_depth = acc_depth / acc_w;

	float3 albedo = acc_albedo / acc_w;
	float3 normal = normalize(acc_normal);
	float spec = acc_spec / acc_w;
	// Total coverage, not the weight sum: several overlapping splats can sum well
	// past one, and this is the pixel's opacity against what is behind it.
	float alpha = saturate(acc_w);

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

	// --- the G-buffer ------------------------------------------------------------
	// One surface per pixel, so unlike scene and light there is nothing to blend into:
	// either this cloud owns the pixel or the geometry behind it does. It owns it when
	// it covers the pixel at all (a partial edge leaves the surface behind it in place)
	// AND it is in front, which is the same depth test the write itself performs -
	// hoisted out of the old min() so that every G-buffer channel agrees with the depth
	// rather than each deciding separately.
	if (alpha > surface_alpha && surface_depth < depth_out[px]) {
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
