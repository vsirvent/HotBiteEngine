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
#include "../../Common/GBuffer.hlsli"

//Lava lights nothing itself either: the sun and the point lights that fall on it are added
//by DeferredLightCS (ShadeLavaDirectional/ShadeLavaPoint, the LAVA model of
//Common/GBuffer.hlsli), weighted by the same 0.6 * shore fade this pass puts on its own
//colour. What is left here is the lava's own glow, and the visible halo of the nearest
//point lamps behind it (the aux lights).
cbuffer externalData : register(b0)
{
	matrix world;
	matrix view;
	matrix projection;
	AuxLight auxLights[MAX_AUX_LIGHTS];
	MaterialColor material;
	int auxLightCount;
	int materialFlags;
	float parallaxScale;
	int meshNormalTextureEnable;
	int highTextureEnable;
	float3 cameraPosition;
	float3 cameraDirection;
	int screenW;
	int screenH;
	float time;
	//1 for the first geometry pass, 2 for the second (see RenderSystem::DrawScene).
	int renderPass;

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
#include "../../Common/LightGlow.hlsli"
Texture2D renderTexture;

RenderTargetRT main(GSOutput input)
{
	int i = 0;
	matrix worldViewProj = mul(view, projection);

	//depthTexture is still read below for what is *behind* the surface (the refracted
	//terrain). The occlusion test that used to be here is gone: the pass renders
	//against the depth pre-pass buffer, so the hardware rejects fragments the opaque
	//scene covers before this shader runs. See RenderSystem::DrawScene.
	float2 pos = input.position.xy;
	pos.x /= screenW;
	pos.y /= screenH;

	input.worldPos /= input.worldPos.w;
	float depth_test = length(input.worldPos.xyz - cameraPosition);
	//Not an occlusion test - this is the fraction of the 2x2 neighbourhood in which the
	//lava is in front of the terrain, which fades the surface out along the shore where
	//it meets the ground. It is applied to the emitted colour at the end.
	float dz_pcf = depthTexture.SampleCmpLevelZero(PCFSampler, pos, depth_test);

	RenderTargetRT output;

	float depth = length(input.worldPos.xyz - cameraPosition);
	float dz = depthTexture.Sample(basicSampler, pos);
	
	float4 finalColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	float4 lightColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	
	float3 normal = { 0.0f, 1.0f, 0.0f };
	float spec_intensity = 0.4f;
	fnl_state state = fnlCreateState();
	state.noise_type = FNL_NOISE_CELLULAR;
	state.octaves = 2;
	state.lacunarity = 3.0f;
	state.weighted_strength = 1.5f;
	state.fractal_type = FNL_FRACTAL_PINGPONG;
	state.frequency = 0.3f;
	state.gain = 0.6f;
	state.rotation_type_3d = FNL_ROTATION_IMPROVE_XY_PLANES;
	float t = time*0.5f;
    float n0 = fnlGetNoise3D(state, input.worldPos.x, input.worldPos.y + 2.0f*t, input.worldPos.z);
	state.frequency = 0.1f;
	float n1 = fnlGetNoise3D(state, 2.0f*input.worldPos.z, input.worldPos.y - 2.0f*t, 2.0f * input.worldPos.x);
	float3 bump = float3(n0, 0, n1);
	normal = normalize(normal + bump*0.5);
	state.noise_type = FNL_NOISE_OPENSIMPLEX2S;
	state.octaves = 1;
	state.frequency = 0.05f;
	state.fractal_type = FNL_FRACTAL_NONE;
	float n3 = 0.6f + 0.4f * fnlGetNoise3D(state, input.worldPos.z + 4.0f*t, input.worldPos.y + 5.0f * t, input.worldPos.x + 4.0f * t);
	
	// Apply textures
	float dist_to_terrain = 0.0f;
	float dist_to_terrain2 = 0.0f;
	if (dz > depth) {
		dist_to_terrain = saturate(1.0f - (dz - depth) / 3.0f);
		dist_to_terrain2 = saturate( 1.0f - (dz - depth) / 30.0f);
	}
	float3 terrain_color = float3(0.5f, 0.2f, 0.0f) * (1.0f - dist_to_terrain) * (0.5f * n0 + 0.5f);
	float3 terrain_color2 = float3(0.6f, 0.2f, 0.0f) * (1.0f - dist_to_terrain) * (0.5f * n1 + 0.5f);
	finalColor.rgb += n3*3.0f*(terrain_color * dist_to_terrain2 + terrain_color2);
	float m = max(finalColor.r, finalColor.g);
	m -= 1.0f;
	finalColor.gb += m;

	//Emission of point lights	
	float3 p2 = mul(input.worldPos, view).xyz;
	for (i = 0; i < auxLightCount; ++i) {
		float3 p1 = mul(float4(auxLights[i].light.position, 1.0f), view).xyz;
		//if we are in front of camera
		if (p1.z > 0) {
			//if pixel is behind light
			if (p1.z < p2.z) {
				finalColor.rgb += EmitPoint(input.position.xyz, worldViewProj, auxLights[i]);
			}
		}
	};

	output.scene = finalColor * 0.6f * dz_pcf;
	lightColor += output.scene;
	output.light_map = saturate(lightColor);
	output.bloom_map = lightColor;

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
	//The lava model: the deferred pass adds the sun and the point lights (lit towards (0,1,0))
	//scaled by the weight in x, the same 0.6 * shore fade the colour above carries.
	output.material_map = float4(0.6f * dz_pcf, 0.0f, 0.0f, MaterialCode(SHADING_LAVA, renderPass));
	return output;
}


