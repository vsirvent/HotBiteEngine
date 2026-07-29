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

#include "../../Common/Matrix.hlsli"
#include "../../Common/Quaternion.hlsli"
#include "../../Common/ShaderStructs.hlsli"
#include "../../Common/PixelCommon.hlsli"
#include "../../Common/FastNoise.hlsli"
#include "../../Common/Utils.hlsli"

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
	int materialFlags;
	float parallaxScale;
	int meshNormalTextureEnable;
	int highTextureEnable;
	float cloud_density;
	float3 cameraPosition;
	float3 cameraDirection;
	int screenW;
	int screenH;
	float4 LightPerspectiveValues[MAX_LIGHTS / 2];
	matrix DirPerspectiveMatrix[DIR_SHADOW_MATRIX_COUNT];
	matrix DirStaticPerspectiveMatrix[MAX_LIGHTS];
	matrix spot_view;
	float time;

	uint multi_texture_count;
	float multi_parallax_scale;

	uint4 packed_multi_texture_operations[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_values[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_uv_scales[MAX_MULTI_TEXTURE / 4];
	//Per layer, the orientation and altitude rules as (min, max, fade, enabled).
	//Declared here rather than in MultiTexture.hlsli because that file is an
	//include: every shader that pulls it in has to supply the constants it reads.
	float4 multi_texture_slope[MAX_MULTI_TEXTURE];
	float4 multi_texture_height[MAX_MULTI_TEXTURE];
	//Per layer, the mask image's own UV transform as (scale, scale, offset u,
	//offset v). Separate from the uv_scale that tiles the detail maps: a splat
	//map covers the surface once, the rock on it repeats tens of times.
	float4 multi_texture_mask_uv[MAX_MULTI_TEXTURE];
}

#include "../../Common/PixelFunctions.hlsli"
Texture2D renderTexture;
Texture2D prevLightTexture;

//How far light travels into this water before it is spent, in world units, taken from
//the material's `opacity`: 1.0 is a clear mountain lake you can see the bottom of,
//small values a murky pond. WaterPS never read `opacity` before, so giving it this
//meaning changes nothing that was authored earlier.
#define WATER_CLARITY_SCALE 45.0f
//Per-channel absorption at that distance. Water kills red first - which is the whole
//reason deep clear water reads blue-green over a sandy bottom instead of grey - so a
//single coefficient for all three channels would only dim the image toward black.
#define WATER_EXTINCTION float3(3.2f, 1.0f, 0.65f)
//Where nothing opaque sits behind the surface (the horizon past the far shore) the
//water is treated as this deep rather than as depth zero. Otherwise it turns
//*transparent* exactly where it should look deepest.
#define WATER_HORIZON_DEPTH 400.0f
//Width in world units of the shallow band that grows foam along a shore.
#define WATER_SHORE_WIDTH 2.2f

float3 CalcWaterDirectional(float3 normal, float3 position, float2 uv, DirLight light, int index, const float spec_intensity, inout float4 bloom)
{
	float3 color = light.Color.rgb * light.intensity;
	float3 finalColor = { 0.f, 0.f, 0.f };
	float3 bloomColor = { 0.f, 0.f, 0.f };
	float3 ToEye = cameraPosition.xyz - position.xyz;
	ToEye = normalize(ToEye);

	//Diffuse. This term did not exist: the surface was lit by a specular highlight and
	//nothing else, so every part of a lake the sun's reflection did not land on was
	//left completely unlit - which is what made the water read as tar rather than as
	//water with a colour.
	float NDotL = saturate(dot(normal, light.DirToLight));
	finalColor += color * NDotL;

	// Blinn specular
	float3 HalfWay = normalize(ToEye + light.DirToLight);
	float NDotH = saturate(dot(HalfWay, normal));
	//Fresnel (Schlick, water's F0 = 0.02): a lake is nearly matte looked straight down
	//into and a mirror at a grazing angle. Without it the sun's glint is equally strong
	//everywhere, which reads as a flat sheet however good the wave normals are.
	float NDotV = saturate(dot(normal, ToEye));
	float fresnel = 0.02f + 0.98f * pow(1.0f - NDotV, 5.0f);
	//Tinted by the light rather than white, so a low sun lays an orange glare on the
	//water instead of a white one.
	float3 spec_color = color * pow(NDotH, 500.0f) * spec_intensity * 15.0f * fresnel;
	finalColor += spec_color;
	bloomColor += spec_color;

	bloom.rgb += bloomColor;
	return finalColor;
}

float3 CalcWaterPoint(float3 normal, float3 position, float2 uv, PointLight light, int index, const float spec_intensity, inout float4 bloom)
{
	float3 finalColor = { 0.f, 0.f, 0.f };
	float3 bloomColor = { 0.f, 0.f, 0.f };
	float3 lposition = light.Position;
	float lum = (light.Color.r + light.Color.g + light.Color.b) / 3.0f;
	float3 ToLight = lposition - position;
	float3 ToEye = cameraPosition.xyz - position;
	float DistToLight = length(ToLight);
	// Phong diffuse
	ToLight /= DistToLight; // Normalize
	float NDotL = saturate(dot(ToLight, normal));
	float NDotL2 = saturate(dot(-ToLight, normal));
	finalColor = pow(light.Color * NDotL, 4.0f);
	// Blinn specular
	ToEye = normalize(ToEye);
	ToLight = normalize(ToLight);
	float3 HalfWay = normalize(ToEye + ToLight);
	float NDotH = saturate(dot(HalfWay, normal));
	float3 spec_color = { 0.f, 0.f, 0.f };
	spec_color += pow(NDotH, 400.0f) * spec_intensity;
	finalColor += spec_color;
	bloomColor.rgb += (saturate(NDotH - 0.9999f)) * 9999.0f * (spec_intensity);

	float LightRange = (light.Range - DistToLight) / light.Range;
	float DistToLightNorm = saturate(LightRange);
	float Attn = saturate(DistToLightNorm * DistToLightNorm);
	bloom.rgb += bloomColor;
	return finalColor;
}

RenderTargetRT main(GSOutput input)
{
	int i = 0;
	matrix worldViewProj = mul(view, projection);

	//depthTexture is still read below for what is *behind* the surface (the refracted
	//terrain, and whether the distorted sample is still behind it). The occlusion test
	//that used to be here is gone: the pass renders against the depth pre-pass buffer,
	//so the hardware rejects fragments the opaque scene covers before this shader runs.
	//See RenderSystem::DrawScene.
	float2 pos = input.position.xy;
	pos.x /= screenW;
	pos.y /= screenH;

	input.worldPos /= input.worldPos.w;
	float depth_test = length(input.worldPos.xyz - cameraPosition);

	RenderTargetRT output;

	float depth = length(input.worldPos.xyz - cameraPosition);
	
	float4 finalColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	float4 lightColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	
	float3 normal = input.normal;// { 0.0f, 1.0f, 0.0f };
	float spec_intensity = 1.0f;
	
	fnl_state state = fnlCreateState();
	state.noise_type = FNL_NOISE_OPENSIMPLEX2S;
	state.octaves = 2;
	state.fractal_type = FNL_FRACTAL_FBM;
	state.frequency = 0.6f;
	state.gain = 0.6;
	state.rotation_type_3d = FNL_ROTATION_IMPROVE_XY_PLANES;
	float t = time * 0.2f;
    float n0 = fnlGetNoise3D(state, input.worldPos.x + t, input.worldPos.y + t, input.worldPos.z + t);
	float n1 = fnlGetNoise3D(state, input.worldPos.z - t, input.worldPos.y - t, input.worldPos.x - t);
	//A second, much longer wave under the ripples. One frequency perturbing the normal
	//by a whole unit tilts it up to 45 degrees from pixel to pixel, which is why the
	//surface read as churning static and gave no sense of how big the lake was: real
	//water carries a slow swell with fine ripples riding on it, and it is the ratio
	//between the two that the eye reads as scale. The combined amplitude is also far
	//smaller now, so the surface still lies flat enough to reflect.
	state.frequency = 0.09f;
	float s0 = fnlGetNoise3D(state, input.worldPos.x + 0.3f * t, input.worldPos.y, input.worldPos.z + 0.3f * t);
	float s1 = fnlGetNoise3D(state, input.worldPos.z - 0.3f * t, input.worldPos.y, input.worldPos.x - 0.3f * t);
	float3 bump = float3(0.20f * n0 + 0.32f * s0, 0.0f, 0.20f * n1 + 0.32f * s1);
	normal = normalize(normal + bump);
	
	float2 pos2 = pos;
	pos2.x += n0 * 0.015f;
	pos2.y += n1 * 0.015f;
	pos2.xy = saturate(pos2.xy);

	//Check that the position of the terrain distorted is behind the water
	if (depthTexture.SampleCmpLevelZero(PCFSampler, pos2, depth_test) == 1.0f) {
		pos = pos2;
	}
	float dz = depthTexture.Sample(basicSampler, pos);

	float4 lumColor = float4(0.0f, 0.0f, 0.0f, 1.0f);
	// Calculate the ambient light
	lumColor.rgb += CalcAmbient(normal);

	//Apply directional light
	for (i = 0; i < dirLightsCount; ++i) {
		lumColor.rgb += CalcWaterDirectional(normal, input.worldPos.xyz, input.uv, dirLights[i], i, spec_intensity, lightColor).rgb;
	}

	// Apply point lights
	for (i = 0; i < pointLightsCount; ++i) {
		if (length(input.worldPos.xyz - pointLights[i].Position) < pointLights[i].Range) {
			lumColor.rgb += CalcWaterPoint(normal, input.worldPos.xyz, input.uv, pointLights[i], i, spec_intensity, lightColor).rgb;
		}
	}

	//The water's own colour. `diffuseColor` could never show before: it was multiplied
	//into a finalColor that was still zero at that point, so the only water colour in
	//the frame was a hard-coded (0,0,0.3) that the depth fade below drove to black -
	//which is why deep water rendered as tar. It is an authored material colour now.
	float3 water_color = material.diffuseColor.rgb;
	if (material.flags & DIFFUSSE_MAP_ENABLED_FLAG) {
		water_color *= diffuseTexture.Sample(basicSampler, input.uv).rgb;
	}

	//How much water the view ray passes through before it hits the bottom. Where
	//nothing opaque is behind the surface it is the far shore that has run out, not the
	//water, so that reads as deep rather than as a depth of zero - otherwise the lake
	//turns transparent exactly where it should look deepest.
	float water_depth = (dz > depth) ? (dz - depth) : WATER_HORIZON_DEPTH;

	//Beer-Lambert: the fraction of the bottom that survives the trip up through the
	//water. The old code faded it linearly to zero over 30 units instead, so anything
	//deeper than that was black no matter what was under it or what lit the surface.
	float clarity = max(0.5f, material.opacity * WATER_CLARITY_SCALE);
	float3 trans = exp(-water_depth * WATER_EXTINCTION / clarity);

	//Foam where the bottom rises to meet the surface, broken up by the same noise that
	//moves the waves so the shoreline is not a clean contour of the terrain under it.
	float shore = saturate(1.0f - water_depth / WATER_SHORE_WIDTH);
	float foam = saturate(shore * shore * (0.6f + 0.55f * n0));

	float3 bottom_color = renderTexture.Sample(basicSampler, pos).rgb;
	float3 bottom_light = prevLightTexture.Sample(basicSampler, pos).rgb;

	//Shallow water shows the bottom, deep water its own colour. The surface's own
	//lighting is weighted by (1 - trans), the light scattered back out of the column:
	//it grows over exactly the path length the transmission shrinks over, so a film of
	//water over sand still looks like wet sand while a lake looks like lit water.
	finalColor.rgb = lerp(water_color, bottom_color, trans) + foam;
	lumColor.rgb = lumColor.rgb * (1.0f - trans) + bottom_light * trans;

	output.light_map = lumColor;
	output.bloom_map = saturate(lightColor);
	output.scene = finalColor;
	RaySource ray;
	ray.orig = input.worldPos.xyz;
	ray.dispersion = 0.0f;
	ray.reflex = 1.0f;
	ray.normal = normalize(float3(0.0f, 1.0f, 0.0f) + 0.1f * normal);
	ray.density = material.density;
	ray.opacity = 1.0f;
	output.rt_ray0_map = getColor0(ray);
	output.rt_ray1_map = getColor1(ray);
	output.pos0_map = input.worldPos;
	output.pos1_map = input.worldPos;
	return output;
}


