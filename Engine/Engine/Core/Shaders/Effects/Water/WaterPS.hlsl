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
	matrix DirPerspectiveMatrix[MAX_LIGHTS];
	matrix spot_view;
	float time;

	uint multi_texture_count;
	float multi_parallax_scale;

	uint4 packed_multi_texture_operations[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_values[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_uv_scales[MAX_MULTI_TEXTURE / 4];
}

#include "../../Common/PixelFunctions.hlsli"
Texture2D renderTexture;
Texture2D prevLightTexture;

float3 CalcWaterDirectional(float3 normal, float3 position, float2 uv, DirLight light, int index, const float spec_intensity, inout float4 bloom)
{
	float3 color = light.Color.rgb * light.intensity;
	float3 finalColor = { 0.f, 0.f, 0.f };
	float3 bloomColor = { 0.f, 0.f, 0.f };
	// Blinn specular
	float3 ToEye = cameraPosition.xyz - position.xyz;
	ToEye = normalize(ToEye);
	float3 HalfWay = normalize(ToEye + light.DirToLight);
	float NDotH = saturate(dot(HalfWay, normal));
	float3 spec_color = { 0.f, 0.f, 0.f };
	spec_color += pow(NDotH, 500.0f) * spec_intensity * 15.0f;
//	spec_color += color * pow(NDotH, 100.0f) * spec_intensity;
//	spec_color += color * pow(NDotH, 2.0f) * spec_intensity * 0.1f;
	finalColor += spec_color;
	bloomColor += spec_color;

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

	//Depth-z check with depth texture
	float2 pos = input.position.xy;
	pos.x /= screenW;
	pos.y /= screenH;

	
	input.worldPos /= input.worldPos.w;
	float depth_test = length(input.worldPos.xyz - cameraPosition);
	float dz_pcf = depthTexture.SampleCmpLevelZero(PCFSampler, pos, depth_test);
	if (dz_pcf == 0.0f) { discard; }

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
	float3 bump = float3(n0, 0, n1);
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

	// Apply textures
	if (material.flags & DIFFUSSE_MAP_ENABLED_FLAG || multi_texture_count > 0) {
		float3 text_color = diffuseTexture.Sample(basicSampler, input.uv).rgb;
		finalColor.rgb *= text_color;
	}
	else {
		finalColor *= float4(0.9f, 0.9f, 1.0f, 1.0f) * 0.3f;
	}

	// Apply textures
	float dist_to_terrain = 0.0f;
	float dist_to_terrain2 = 0.0f;
	if (dz > depth) {
		dist_to_terrain = saturate(1.0f - (dz - depth) / 60.0f);
		dist_to_terrain2 = saturate( 1.0f - (dz - depth) / 30.0f);
	}
	float3 terrain_color = (renderTexture.Sample(basicSampler, pos).rgb + float3(0.0f, 0.0f, 0.3f) * (1.0f - dist_to_terrain))* dist_to_terrain2;	
	float3 prev_light = (prevLightTexture.Sample(basicSampler, pos).rgb + float3(0.0f, 0.0f, 0.3f) * (1.0f - dist_to_terrain)) * dist_to_terrain2;
	finalColor.rgb += terrain_color;
	lumColor.rgb += prev_light;

	output.light_map = lumColor;
	output.bloom_map = lightColor;
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


