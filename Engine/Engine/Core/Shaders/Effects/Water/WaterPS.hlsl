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

float3 CalcWaterDirectional(float3 normal, float3 position, float2 uv, DirLight light, int index, const float spec_intensity, inout float4 bloom)
{
	float3 color = light.Color.rgb * light.intensity;
	// Phong diffuse
	float NDotL = dot(light.DirToLight, normal);
	float3 finalColor = pow(color * saturate(NDotL), 4.0f) * 0.5f;
	float3 bloomColor = { 0.f, 0.f, 0.f };
	// Blinn specular
	float3 ToEye = cameraPosition.xyz - position;
	ToEye = normalize(ToEye);
	float3 HalfWay = normalize(ToEye + light.DirToLight);
	float NDotH = saturate(dot(HalfWay, normal));
	float3 spec_color = { 0.f, 0.f, 0.f };
	spec_color += pow(NDotH, 400.0f) * spec_intensity;
	finalColor += spec_color;
	bloomColor += (saturate(NDotH - 0.9999f)) * 9999.0f * (spec_intensity);
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
	
	float3 normal = { 0.0f, 1.0f, 0.0f };
	float spec_intensity = 0.4f;
	
	fnl_state state = fnlCreateState();
	state.noise_type = FNL_NOISE_OPENSIMPLEX2S;
	state.octaves = 2;
	state.fractal_type = FNL_FRACTAL_FBM;
	state.frequency = 0.5;
	state.gain = 0.6;
	state.rotation_type_3d = FNL_ROTATION_IMPROVE_XY_PLANES;
	float t = time * 0.2f;
    float n0 = fnlGetNoise3D(state, input.worldPos.x + t, input.worldPos.y + t, input.worldPos.z + t);
	float n1 = fnlGetNoise3D(state, input.worldPos.z - t, input.worldPos.y - t, input.worldPos.x - t);
	float3 bump = float3(n0, 0, n1);
	normal = normalize(normal + bump*0.5);
	
	float2 pos2 = pos;
	pos2.x += n0 * 0.015f;
	pos2.y += n1 * 0.015f;
	pos2.xy = saturate(pos2.xy);

	//Check that the position of the terrain distorted is behind the water
	if (depthTexture.SampleCmpLevelZero(PCFSampler, pos2, depth_test) == 1.0f) {
		pos = pos2;
	}
	float dz = depthTexture.Sample(basicSampler, pos);
	//Apply directional light
	for (i = 0; i < dirLightsCount; ++i) {
		finalColor.rgb += CalcWaterDirectional(normal, input.worldPos.xyz, input.uv, dirLights[i], i, spec_intensity, lightColor).rgb;
		if (dirLights[i].density > 0.0f) {
			lightColor.rgb += DirVolumetricLight(input.worldPos, dirLights[i], i, time, cloud_density);
		}
	}

	// Apply point lights
	for (i = 0; i < pointLightsCount; ++i) {
		if (length(input.worldPos.xyz - pointLights[i].Position) < pointLights[i].Range) {
			finalColor.rgb += CalcWaterPoint(normal, input.worldPos.xyz, input.uv, pointLights[i], i, spec_intensity, lightColor).rgb;
			if (pointLights[i].density > 0.0f) {
				lightColor.rgb += VolumetricLight(input.worldPos, pointLights[i], i);
			}
		}
	}
	
	// Apply textures
	float dist_to_terrain = 0.0f;
	float dist_to_terrain2 = 0.0f;
	if (dz > depth) {
		dist_to_terrain = saturate(1.0f - (dz - depth) / 15.0f);
		dist_to_terrain2 = saturate( 1.0f - (dz - depth) / 30.0f);
	}
	float3 terrain_color = 0.5f*(renderTexture.Sample(basicSampler, pos).rgb * dist_to_terrain + float3(0.0f, 0.0f, 0.1f) * (1.0f - dist_to_terrain))* dist_to_terrain2;	
	finalColor.rgb += terrain_color;
	
	//Emission of point lights	
	float3 p2 = mul(input.worldPos, view).xyz;
	for (i = 0; i < pointLightsCount; ++i) {
		float3 p1 = mul(float4(pointLights[i].Position, 1.0f), view).xyz;
		//if we are in front of camera
		if (p1.z > 0) {
			//if pixel is behind light
			if (p1.z < p2.z) {
				finalColor.rgb += EmitPoint(input.position.xyz, worldViewProj, pointLights[i]);
			}
		}
	};

	output.light_map = saturate(lightColor);
	output.bloom_map = saturate(lightColor);
	output.scene = saturate(finalColor);
	RaySource ray;
	ray.orig = input.worldPos.xyz;
	ray.dispersion = 0.0f;
	ray.normal = normalize(float3(0.0f, 1.0f, 0.0f) + 0.1f * normal);
	ray.density = 1.0f;
	ray.opacity = 1.0f;
	output.rt_ray0_map = getColor0(ray);
	output.rt_ray1_map = getColor1(ray);

	return output;
}


