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

#include <Common/Matrix.hlsli>
#include <Common/ShaderStructs.hlsli>
#include <Common/FastNoise.hlsli>
#include <Common/PixelCommon.hlsli>

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
	float parallaxScale;
	int meshNormalTextureEnable;
	int highTextureEnable;
	float3 cameraPosition;
	float3 cameraDirection;
	int screenW;
	int screenH;
	float4 LightPerspectiveValues[MAX_LIGHTS / 2];
	matrix DirPerspectiveMatrix[MAX_LIGHTS];
	matrix DirStaticPerspectiveMatrix[MAX_LIGHTS];
	matrix spot_view;
	float time;
	float cloud_density;

	uint multi_texture_count;
	float multi_parallax_scale;

	int disable_vol; 

	uint4 packed_multi_texture_operations[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_values[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_uv_scales[MAX_MULTI_TEXTURE / 4];
}

#include <Common/MultiTexture.hlsli>
#include <Common/PixelFunctions.hlsli>
#include <MainRender/MainRenderPS.hlsli>
#include "DemoCommons.hlsli"

Texture2D grassTexture[NGRASS];

RenderTargetRT main(GSOutput input)
{
	if (!any(input.tangent)) {
		//Depth-z check with depth texture
		float2 pos = input.position.xy;
		pos.x /= screenW;
		pos.y /= screenH;

		float depth = length(input.worldPos.xyz - cameraPosition) - 1.0f;
		float dz = depthTexture.SampleCmpLevelZero(PCFSampler, pos, depth);
		if (dz == 0.0f) { discard; }

		//grass pixel
		float3 color = { 0.0f, 0.0f, 0.0f };
		switch ((int)input.bitangent.x) {
		case 0: color = grassTexture[0].Sample(basicSampler, input.uv).rgb; break;
		case 1: color = grassTexture[1].Sample(basicSampler, input.uv).rgb; break;
		case 2: color = grassTexture[2].Sample(basicSampler, input.uv).rgb; break;
		}
		if (length(color) < 0.4f) {
			discard;
		}
		float4 wpos = input.worldPos;
		int i = 0;
		float4 finalColor = { 0.0f, 0.0f, 0.0f, 1.0f };
		float4 lightColor = { 0.0f, 0.0f, 0.0f, 0.0f };
		float3 normal = input.normal;
		// Calculate the ambient light
		finalColor.rgb += CalcAmbient(normal);
		// Calculate the directional light
		for (i = 0; i < dirLightsCount; ++i) {
			finalColor.rgb += CalcDirectional(normal, wpos, input.uv, material, dirLights[i], cloud_density, i, lightColor);
			if (dirLights[i].density > 0.0f) {
				lightColor.rgb += DirVolumetricLight(wpos, dirLights[i], i, time, cloud_density);
			}
		}
		
		// Calculate the point lights
		for (i = 0; i < pointLightsCount; ++i) {
			if (length(wpos - pointLights[i].Position) < pointLights[i].Range) {
				finalColor.rgb += CalcPoint(normal, wpos, input.uv, material, pointLights[i], i, lightColor);
				if (pointLights[i].density > 0.0f && disable_vol == 0) {
					lightColor.rgb += VolumetricLight(wpos, pointLights[i], i);
				}
			}
		}

		//Emission of point lights	
		matrix worldViewProj = mul(view, projection);
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

		finalColor.rgb *= color;
		RenderTargetRT output;
		output.light_map = lightColor;
		output.scene = finalColor;

		RaySource ray;
		ray.orig = wpos.xyz;
		ray.dispersion = 0.0f;
		ray.normal = normal;
		ray.density = 1.0f;

		output.rt_ray0_map = getColor0(ray);
		output.rt_ray1_map = getColor1(ray);

		return output;
	}
	else {
		return MainRenderPS(input);
	}
}
