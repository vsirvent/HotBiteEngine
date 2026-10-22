/*
The HotBite Game Engine

Copyright(c) 2023 Vicente Sirvent Orts

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

//Ground truth ambient occlusion (GTAO, Jimenez et al. 2016), traced against the Hi-Z depth pyramid.
//
//For a pixel, a slice is the plane through the view vector and a direction on the screen. In that plane the
//surface hides everything below its tangent, and whatever stands around it hides more: the two horizons found
//by walking the depth buffer out on both sides of the pixel (the "rays" of this pass), measured as angles from
//the view vector. The visibility of the slice is the integral of the cosine weighted arc between the horizons,
//clipped to the hemisphere of the normal, which is closed form (Jimenez's iarc).
//
//The walk reads the Hi-Z pyramid (RenderSystem::high_z_maps, the nearest surface of 5^n x 5^n blocks of the depth
//map) and not the depth map: a step further from the pixel reads a coarser level, so a long reach costs the same
//handful of reads as a short one. The pyramid keeps the NEAREST surface of its block, so a coarse read can only
//hide more than the pixels in it do, never less; the steps that far from the pixel are weighted down by distance
//anyway. The steps next to the pixel (HIZ_LEVEL0_PIXELS) are the exception: they read the G-buffer's own position
//of the pixel they land on, not a point made of the depth and the middle of the pixel. The frame is rendered with
//a sub-pixel jitter (the temporal anti-aliasing), so the depth of a pixel belongs to a place up to half a pixel
//off its middle, and at one or two pixels from the surface being shaded that is as big as the heights the horizon
//is made of: it tilted every flat surface's horizon the same way and darkened open ground.
//
//The rays are split over the frames the way the ray traced shadows split their lights (DeferredLightCS.hlsl,
//shadow_ray_max_lights): a pixel traces `slices` of its slices a frame (one by default), the direction of which
//is picked by the pixel's place in a 4x4 pattern and moved on by the golden ratio with every frame, so the frames
//take turns at every direction. The jitter of the steps moves with the frame too. What one frame returns is one
//noisy estimate of the visibility; GtaoAccumulateCS (ShadowAccumulateCS.hlsl with GTAO_ACCUMULATE) blurs the
//pattern away and averages the frames, exactly as it does the light of the ray traced lights, and takes the share
//of the ambient light that the visibility hides out of the light map.
//
//Everything is in WORLD space: the depth map holds the distance from the camera, and so does the pyramid, so a
//sample's point is the camera plus the pixel's eye ray times that distance.
//
//The normal is the SURFACE's, rebuilt from where the neighbouring pixels are (GeometricNormal), and not the
//G-buffer's: that one is the shading normal, with the detail of the normal maps in it, which the depth buffer
//knows nothing about - a snow field whose normals lean every way against a depth buffer that is smooth would read
//as half buried in itself. The horizons come from the depth, so the normal they are measured against has to as well.

#include "../Common/Defines.hlsli"
#include "../Common/GBuffer.hlsli"
#include "../RayTrace/RayScreenSolver.hlsli"

cbuffer externalData : register(b0)
{
	//Pixel to eye ray: the unjittered camera's projection and view (row vector times matrix, as every matrix of the engine).
	matrix inverse_projection;
	matrix inverse_view;
	float3 cameraPosition;
	int screenW;
	int screenH;
	float hiz_ratio;
	//The reach of the occlusion in world units, and the share of it (at its far end) over which a sample's weight fades to nothing.
	float radius;
	float falloff;
	//How many pixels a world unit spans at distance 1 from the camera (half the height of the screen times the projection's
	//cotangent), and the most pixels the radius may reach on the screen (the nearer a surface, the more it would).
	float pixels_per_unit;
	float max_radius_px;
	//Slices traced this frame, steps on each side of a slice, the frame number (it moves the directions and the jitter on),
	//the exponent that bunches the steps towards the pixel and the one the finished visibility is raised to.
	int slices;
	int steps;
	uint frame;
	float step_power;
	float final_power;
}

Texture2D<float4> positionTexture : register(t0);
//rt_ray_sources1: the world normal in xyz.
Texture2D<float4> normalTexture : register(t1);
Texture2D<float4> materialTexture : register(t2);
Texture2D<float> ssHiZ[HIZ_TEXTURES] : register(t3);

//rgb: this frame's visibility (1 is open); a: 1 where a surface was traced (GtaoAccumulateCS does not clamp its history there).
RWTexture2D<float4> gtao_out : register(u0);

//The surface's normal at a pixel from the positions of its neighbours (a plane through the pixel and the nearer neighbour on each
//axis, so an edge of the depth does not tilt it), turned to face the camera. False when a neighbour holds no surface.
bool GeometricNormal(int2 px, float3 P, float3 v, out float3 N)
{
	N = float3(0.0f, 1.0f, 0.0f);
	const int2 last = int2(screenW - 1, screenH - 1);
	const float4 l = positionTexture[clamp(px + int2(-1, 0), int2(0, 0), last)];
	const float4 r = positionTexture[clamp(px + int2(1, 0), int2(0, 0), last)];
	const float4 u = positionTexture[clamp(px + int2(0, -1), int2(0, 0), last)];
	const float4 d = positionTexture[clamp(px + int2(0, 1), int2(0, 0), last)];
	if (l.x >= 1.0e30f || r.x >= 1.0e30f || u.x >= 1.0e30f || d.x >= 1.0e30f) {
		return false;
	}
	//Both point the way the pixel index grows (right, down).
	const float3 dx = (length(r.xyz - P) < length(P - l.xyz)) ? (r.xyz - P) : (P - l.xyz);
	const float3 dy = (length(d.xyz - P) < length(P - u.xyz)) ? (d.xyz - P) : (P - u.xyz);
	float3 n = cross(dy, dx);
	const float len = length(n);
	if (len < 1.0e-8f) {
		return false;
	}
	n /= len;
	N = (dot(n, v) < 0.0f) ? -n : n;
	return true;
}

static const float bayer4x4[16] = { 0.0f, 8.0f, 2.0f, 10.0f, 12.0f, 4.0f, 14.0f, 6.0f, 3.0f, 11.0f, 1.0f, 9.0f, 15.0f, 7.0f, 13.0f, 5.0f };

uint GtaoHash(uint x)
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

//The unit vector from the camera through a point of the screen, in pixels.
float3 EyeDir(float2 pixel)
{
	const float2 ndc = float2(pixel.x / (float)screenW * 2.0f - 1.0f, 1.0f - pixel.y / (float)screenH * 2.0f);
	const float4 v = mul(float4(ndc, 1.0f, 1.0f), inverse_projection);
	float3 d = v.xyz / v.w;
	//Looking down +z in view space: whatever side of the lens the unprojected point fell on, the ray is the one in front.
	if (d.z < 0.0f) {
		d = -d;
	}
	return normalize(mul(float4(d, 0.0f), inverse_view).xyz);
}

//How far from the pixel, in pixels, a step still reads the G-buffer's positions and not the pyramid.
#define HIZ_LEVEL0_PIXELS 16.0f

//The pyramid level a step this many pixels from the pixel reads: a block of the level is about a sixth of the distance
//across, so the sample stays a small footprint whatever the distance.
int HizLevelFor(float distance_px)
{
	return distance_px < HIZ_LEVEL0_PIXELS ? 0 : (distance_px < 80.0f ? 1 : (distance_px < 400.0f ? 2 : 3));
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
	if (dtid.x >= (uint)screenW || dtid.y >= (uint)screenH) {
		return;
	}
	const uint2 px = dtid.xy;
	const float4 wp = positionTexture[px];
	//Nothing was drawn here (the G-buffer is cleared to FLT_MAX).
	if (wp.x >= 1.0e30f) {
		gtao_out[px] = float4(1.0f, 1.0f, 1.0f, 0.0f);
		return;
	}
	//Only ordinary surfaces take ambient occlusion (water and lava light themselves, a splat cloud has no surface).
	const uint model = ((uint)(materialTexture[px].w + 0.5f)) & GBUFFER_MODEL_MASK;
	if (model != SHADING_STANDARD) {
		gtao_out[px] = float4(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}

	const float3 P = wp.xyz;
	const float3 to_camera = cameraPosition - P;
	const float distance_to_camera = max(length(to_camera), 1.0e-3f);
	const float3 v = to_camera / distance_to_camera;
	float3 N;
	if (!GeometricNormal((int2)px, P, v, N)) {
		//At a silhouette or the edge of the screen: the shading normal is all there is.
		N = normalTexture[px].xyz;
		const float normal_length = length(N);
		if (normal_length < 1.0e-4f) {
			gtao_out[px] = float4(1.0f, 1.0f, 1.0f, 1.0f);
			return;
		}
		N /= normal_length;
	}

	//The reach on the screen. Clamped near the camera, where it would cover the screen: the reach in the world shrinks with it.
	const float radius_px = min(radius * pixels_per_unit / distance_to_camera, max_radius_px);
	if (radius_px < 2.0f) {
		gtao_out[px] = float4(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}
	const float world_radius = radius_px * distance_to_camera / pixels_per_unit;
	const float falloff_range = max(falloff, 0.01f) * world_radius;

	//Which direction the pixel looks in this frame (its place in the 4x4 pattern, then the golden ratio a frame) and how its steps are
	//jittered (a hash of the pixel and the frame).
	const float pattern = (bayer4x4[(px.x & 3u) + 4u * (px.y & 3u)] + 0.5f) * (1.0f / 16.0f);
	const float slice_phase = frac(pattern + (float)(frame % 64u) * 0.6180339887f);
	const float step_noise = (float)(GtaoHash(px.x + GtaoHash(px.y + GtaoHash(frame))) >> 8) * (1.0f / 16777216.0f);

	const float2 centre = (float2)px + 0.5f;
	float visibility = 0.0f;
	[loop]
	for (int s = 0; s < slices; ++s) {
		const float phi = PI * frac(slice_phase + (float)s / (float)slices);
		const float2 omega = float2(cos(phi), sin(phi));

		//Where the direction goes in the world, at the pixel's own distance: the slice is the plane of this and the view vector.
		const float3 Pd = cameraPosition + EyeDir(centre + omega * 2.0f) * distance_to_camera;
		float3 along = Pd - P;
		along -= v * dot(along, v);
		const float along_length = length(along);
		if (along_length < 1.0e-6f) {
			visibility += 1.0f;
			continue;
		}
		const float3 ortho = along / along_length;
		const float3 axis = normalize(cross(ortho, v));

		//The normal in the slice: its length is the share of the hemisphere's weight that falls in the slice, and n is its angle from
		//the view vector, signed towards `ortho`.
		const float3 projected = N - axis * dot(N, axis);
		const float projected_length = length(projected);
		const float cos_n = saturate(dot(projected, v) / max(projected_length, 1.0e-5f));
		const float n = ((dot(ortho, projected) >= 0.0f) ? 1.0f : -1.0f) * acos(cos_n);
		const float sin_n = sin(n);

		//Cosine of the horizon on each side, from the surface's own tangent outwards (the lowest the horizon can be).
		float low[2] = { -sin_n, sin_n };
		float horizon[2] = { low[0], low[1] };

		[loop]
		for (int i = 0; i < steps; ++i) {
			float t = ((float)i + step_noise) / (float)steps;
			t = pow(t, step_power);
			const float step_px = max(t * radius_px, 1.5f);
			const int level = HizLevelFor(step_px);
			const float cell_size = ScreenCellSize(level, hiz_ratio);
			[unroll]
			for (int side = 0; side < 2; ++side) {
				const float2 sample_pos = centre + ((side == 0) ? omega : -omega) * step_px;
				if (sample_pos.x < 0.0f || sample_pos.y < 0.0f || sample_pos.x >= (float)screenW || sample_pos.y >= (float)screenH) {
					continue;
				}
				float3 S;
				if (level == 0) {
					const float4 sp = positionTexture[(uint2)sample_pos];
					//The sky.
					if (sp.x >= 1.0e30f) {
						continue;
					}
					S = sp.xyz;
				}
				else {
					const float2 cell = floor(sample_pos / cell_size);
					const float z = ScreenHiZ(ssHiZ, level, cell);
					//The sky, or a block with nothing drawn in it.
					if (z >= 1.0e30f) {
						continue;
					}
					S = cameraPosition + EyeDir((cell + 0.5f) * cell_size) * z;
				}
				const float3 delta = S - P;
				const float len = length(delta);
				if (len < 1.0e-4f) {
					continue;
				}
				//A sample at the edge of the reach counts for nothing: it falls back to the lowest horizon, so the occlusion fades
				//instead of cutting off.
				const float weight = saturate((world_radius - len) / falloff_range);
				const float shc = lerp(low[side], dot(delta / len, v), weight);
				horizon[side] = max(horizon[side], shc);
			}
		}

		//Horizon angles from the view vector (negative on the `-ortho` side), kept inside the normal's hemisphere.
		float h0 = -acos(clamp(horizon[1], -1.0f, 1.0f));
		float h1 = acos(clamp(horizon[0], -1.0f, 1.0f));
		h0 = n + clamp(h0 - n, -PI * 0.5f, PI * 0.5f);
		h1 = n + clamp(h1 - n, -PI * 0.5f, PI * 0.5f);
		const float arc0 = (cos_n + 2.0f * h0 * sin_n - cos(2.0f * h0 - n)) * 0.25f;
		const float arc1 = (cos_n + 2.0f * h1 * sin_n - cos(2.0f * h1 - n)) * 0.25f;
		visibility += projected_length * (arc0 + arc1);
	}
	visibility = saturate(visibility / (float)max(slices, 1));
	//Never fully black: a crease with nothing lit in it is not a hole.
	visibility = pow(max(visibility, 0.03f), final_power);
	gtao_out[px] = float4(visibility, visibility, visibility, 1.0f);
}
