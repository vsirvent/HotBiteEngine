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
	float3 cameraPosition;
	float3 cameraDirection;
	int screenW;
	int screenH;
	float4 LightPerspectiveValues[MAX_LIGHTS / 2];
	matrix DirPerspectiveMatrix[MAX_LIGHTS];
	matrix spot_view;
	float time;
	float cloud_density;

	uint multi_texture_count;
	float multi_parallax_scale;

	uint4 packed_multi_texture_operations[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_values[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_uv_scales[MAX_MULTI_TEXTURE / 4];
}

#include "../../Common/PixelFunctions.hlsli"
Texture2D renderTexture;

float3 CalcLavaDirectional(float3 normal, float4 position, float2 uv, DirLight light, int index, const float spec_intensity, inout float4 bloom)
{
	float3 color = light.Color.rgb * light.intensity;
	// Phong diffuse
	float NDotL = dot(light.DirToLight, normal);
	float3 finalColor = pow(color * saturate(NDotL), 4.0f);
	float3 bloomColor = { 0.f, 0.f, 0.f };
	// Blinn specular
	float3 ToEye = cameraPosition.xyz - position.xyz;
	ToEye = normalize(ToEye);
	float3 HalfWay = normalize(ToEye + light.DirToLight);
	float NDotH = saturate(dot(HalfWay, normal));
	float3 spec_color = { 0.f, 0.f, 0.f };
	
	float shadow = 1.0f;
	shadow = (DirShadowPCF(position, light, index));
	shadow = saturate(shadow);
	
	return finalColor * shadow;
}


float3 CalcLavaPoint(float3 normal, float3 position, float2 uv, PointLight light, int index, const float spec_intensity, inout float4 bloom)
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
	
	float LightRange = (light.Range - DistToLight) / light.Range;
	float DistToLightNorm = saturate(LightRange);
	float shadow = 1.0f;
	float Attn = saturate(DistToLightNorm * DistToLightNorm);
	bloom.rgb += bloomColor;
	if (light.cast_shadow) {
		shadow = PointShadowPCF(position - light.Position, light, index);
	}
	finalColor *= Attn * shadow;
	bloomColor *= Attn * shadow;
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
	float dz = depthTexture.Sample(basicSampler, pos);
	
	float4 finalColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	float4 lightColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	
	float3 normal = { 0.0f, 1.0f, 0.0f };
	float spec_intensity = 0.4f;
#if 1
	//Apply directional light
	for (i = 0; i < dirLightsCount; ++i) {
		finalColor.rgb += CalcLavaDirectional(normal, input.worldPos, input.uv, dirLights[i], i, spec_intensity, lightColor).rgb;
	}

	// Apply point lights
	for (i = 0; i < pointLightsCount; ++i) {
		if (length(input.worldPos.xyz - pointLights[i].Position) < pointLights[i].Range) {
			finalColor.rgb += CalcLavaPoint(normal, input.worldPos.xyz, input.uv, pointLights[i], i, spec_intensity, lightColor).rgb;
		}
	}
#endif
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

	output.scene = finalColor * 0.6f * dz_pcf;
	lightColor += output.scene;
	output.light_map = saturate(lightColor);

	RaySource ray;
	ray.orig = input.worldPos.xyz;
	ray.dispersion = 0.0f;
	ray.reflex = 1.0f;
	ray.normal = normalize(float3(0.0f, 1.0f, 0.0f) + 0.1f * normal);
	ray.density = material.density;
	ray.opacity = 1.0f;

	output.rt_ray0_map = getColor0(ray);
	output.rt_ray1_map = getColor1(ray);
	return output;
}


