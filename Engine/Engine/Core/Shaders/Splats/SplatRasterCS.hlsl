// Rasterizes the binned Gaussians, lights them with the scene's own lights and
// shadows, and writes the result into the same targets MainRenderPS writes.
//
// One thread group per 16x16 tile, one thread per pixel. Two passes over the tile's
// splat list and no sort between them, which is the design decision this whole
// component rests on:
//
// A 3DGS renderer composites baked radiance back to front, and that needs an
// ordering. This one is producing *material* for a G-buffer instead - an albedo, a
// normal, a depth - and a G-buffer holds one surface per pixel however it is
// filled. So the question is not "what is the correct alpha composite" but "which
// surface is in front and what is it made of", and that is order-independent:
//   pass 1  the nearest depth at which coverage exists
//   pass 2  the alpha-weighted average of everything within a slab behind it
// Both are commutative, so the tile list can be in any order and there is nothing
// to sort. The cost is that you cannot see *through* a splat cloud - which the
// G-buffer could not represent anyway.

#define HB_COMPUTE_LIGHTING 1

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
	float splat_pad0;
}

#include "../Common/MultiTexture.hlsli"
#include "../Common/PixelFunctions.hlsli"

StructuredBuffer<SplatView> splat_views : register(t20);
Buffer<uint> tile_counts : register(t21);
Buffer<uint> tile_lists  : register(t22);

RWTexture2D<float4> scene_out : register(u0);
RWTexture2D<float4> light_out : register(u1);
RWTexture2D<float>  depth_out : register(u2);

// Cooperative batch. Each thread fetches one splat of the batch and every thread
// then reads all 256 out of groupshared, which turns 256 scattered loads per splat
// into one. 256 * 60 bytes = 15 KB of the 32 KB budget.
groupshared SplatView g_batch[256];

[numthreads(SPLAT_TILE_SIZE, SPLAT_TILE_SIZE, 1)]
void main(uint3 gid : SV_GroupID, uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
	uint tile = gid.y * tiles_x + gid.x;
	uint count = min(tile_counts[tile], SPLAT_MAX_PER_TILE);
	if (count == 0) {
		return;
	}

	int2 px = int2(dtid.xy);
	bool on_screen = (px.x < screenW && px.y < screenH);
	float2 pixel_centre = float2(px) + 0.5f;

	// --- pass 1: the nearest depth at which this pixel has coverage --------------
	float nearest = 1e30f;
	uint batches = (count + 255) / 256;
	uint b, i;

	for (b = 0; b < batches; ++b) {
		uint load = b * 256 + gi;
		if (load < count) {
			g_batch[gi] = splat_views[tile_lists[tile * SPLAT_MAX_PER_TILE + load]];
		}
		GroupMemoryBarrierWithGroupSync();

		uint n = min(256u, count - b * 256);
		if (on_screen) {
			for (i = 0; i < n; ++i) {
				SplatView s = g_batch[i];
				float2 d = pixel_centre - s.screen_xy;
				if (dot(d, d) > s.radius * s.radius) {
					continue;
				}
				if (s.alpha * SplatWeight(s.conic, d) >= SPLAT_MIN_ALPHA) {
					nearest = min(nearest, s.view_depth);
				}
			}
		}
		GroupMemoryBarrierWithGroupSync();
	}

	// No early-out here, however tempting. Every thread has to keep reaching the
	// barriers in pass 2 - a group where some threads have returned and others are
	// waiting on GroupMemoryBarrierWithGroupSync is a hang, and fxc rejects it
	// outright (X4026: "thread sync operation must be in non-varying flow control").
	// So the *work* is gated on `contributes` and the control flow is not.
	bool contributes = on_screen && (nearest < 1e30f);

	// --- pass 2: weighted average of the slab behind it --------------------------
	float3 acc_albedo = 0.0f;
	float3 acc_normal = 0.0f;
	float acc_spec = 0.0f;
	float acc_w = 0.0f;
	// The depth the G-buffer gets: where accumulated coverage crosses
	// surface_alpha, not simply `nearest`. `nearest` is the very first splat with
	// any coverage at all, which on a soft edge is a nearly transparent one sitting
	// in front of the real surface.
	float surface_depth = nearest;
	float coverage = 0.0f;

	for (b = 0; b < batches; ++b) {
		uint load = b * 256 + gi;
		if (load < count) {
			g_batch[gi] = splat_views[tile_lists[tile * SPLAT_MAX_PER_TILE + load]];
		}
		GroupMemoryBarrierWithGroupSync();

		uint n = min(256u, count - b * 256);
		for (i = 0; contributes && i < n; ++i) {
			SplatView s = g_batch[i];
			if (s.view_depth > nearest + SPLAT_DEPTH_SLAB) {
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
			acc_albedo += s.albedo * w;
			acc_normal += s.normal * w;
			acc_spec += s.spec * w;
			acc_w += w;
			coverage += w;
			if (coverage < surface_alpha) {
				surface_depth = s.view_depth;
			}
		}
		GroupMemoryBarrierWithGroupSync();
	}

	// Past the last barrier, so returning here is safe.
	if (!contributes || acc_w < SPLAT_MIN_ALPHA) {
		return;
	}

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
	m.flags = 0;   // no maps of any kind: everything is per splat

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
	// Only where the cloud actually covers the pixel: a partial edge should leave
	// the geometry behind it owning the depth.
	if (alpha > surface_alpha) {
		depth_out[px] = min(depth_out[px], surface_depth);
	}
}
