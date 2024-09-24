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

#include "../Common/Matrix.hlsli"
#include "../Common/ShaderStructs.hlsli"
#include "../Common/PixelCommon.hlsli"
#include "../Common/FastNoise.hlsli"
#include "../Common/RGBANoise.hlsli"

cbuffer externalData : register(b0)
{
	matrix world;
	matrix view;
	matrix projection;
	float3 back_color;
	AmbientLight ambientLight;
	DirLight dirLights[MAX_LIGHTS];
	PointLight pointLights[MAX_LIGHTS];
	MaterialColor material;
	int dirLightsCount;
	int pointLightsCount;
	int materialFlags;
	float3 cameraPosition;
	float3 cameraDirection;
	int screenW;
	int screenH;
	float4 LightPerspectiveValues[MAX_LIGHTS / 2];
	matrix DirPerspectiveMatrix[MAX_LIGHTS];
	float time;
	int is_space;
	float space_intensity;
	float cloud_density;
	matrix spot_view;
	uint cloud_test;
	float multi_texture_count;
	uint4 packed_multi_texture_operations[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_values[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_uv_scales[MAX_MULTI_TEXTURE / 4];
}

#include "../Common/MultiTexture.hlsli"
#include "../Common/PixelFunctions.hlsli"

float4 SkyEmitPoint(float3 position, matrix worldViewProj, float3 light_position, float3 color, float scale)
{
	float3 lposition = mul(float4(light_position, 1.0f), worldViewProj).xyz;
	lposition.x /= lposition.z;
	lposition.y /= -lposition.z;
	lposition.x = (lposition.x + 1.0f) * screenW / 2.0f;
	lposition.y = (lposition.y + 1.0f) * screenH / 2.0f;
	float2 ToLight = lposition.xy - position.xy;
	float DistToLight = length(ToLight);
	//Light emission
	float DistLightToPixel = 1.0 - saturate(DistToLight * 0.01f * scale);
	float DistLightToPixel2 = 1.0 - saturate(DistToLight * 0.1f * scale);
	float factor = pow(DistLightToPixel, 10.0f);
	float4 finalColor = float4(color * factor, 0.0f);
	finalColor.rgba += pow(DistLightToPixel2, 10.0f);

	return saturate(finalColor);
}

RenderTarget main(VertexToPixel input)
{
	int i = 0;
	float2 pos = input.position.xy;
	pos.x /= screenW;
	pos.y /= screenH;
	float depth = length(input.worldPos.xyz - cameraPosition);
	float dz = depthTexture.SampleCmpLevelZero(PCFSampler, pos, depth);
	if (dz == 0.0f) discard;
	matrix worldViewProj = mul(view, projection);

	float4 finalColor = { 0.0f, 0.0f, 0.0f, dirLights[0].intensity };
	float4 lightColor = { 1.0f, 1.0f, 1.0f, 1.0f };

	RenderTarget output;

	if (!is_space) {
		finalColor.rgb += back_color;
#if 1
		for (i = 0; i < dirLightsCount; ++i) {
			float3 pos = cameraPosition.xyz + dirLights[i].DirToLight * 1000000.0f;
			float3 p1 = mul(float4(pos, 1.0f), view).xyz;
			//if we are in front of camera
			if (p1.z > 0) {
				finalColor += SkyEmitPoint(input.position.xyz, worldViewProj,
					pos, dirLights[i].Color * dirLights[i].intensity, 0.1f);
			}
		}

		float4 cloud = { 0.0f, 0.0f, 0.0f, 0.0f };
		float4 finalColor_precloud = finalColor * 1.8f;

		if (cloud_density > 0.0f) {			
			if (cloud_test == 0) {
				float f0 = 0.001f;
				float f1 = 0.005f;
				float f2 = 0.01f;
				float f3 = 0.03f;
				float w0 = 0.4f;
				float w1 = 0.3f;
				float w2 = 0.2f;
				float w3 = 0.1f;
				float t0 = time * 0.05f;
				float t1 = time * 0.1f;
				float t2 = time * 0.2f;
				float t3 = time * 0.3f;
				float3 cloud_pos = input.worldPos.xyz;
				float nsteps = 20.0f;
				int isteps = (int)nsteps;
				float3 ToCam = cameraPosition.xyz - cloud_pos;
				float3 ToCamRayUnit = normalize(ToCam) * rgba_tnoise(cloud_pos.zxy * 0.001f + time * 0.01f) * 30.0f;
				for (int s = 0; s < isteps; ++s) {
					// Apply textures
					float3 p0 = cloud_pos.xyz * f0;
					float3 p1 = cloud_pos.xyz * f1;
					float3 p2 = cloud_pos.xyz * f2;
					float3 p3 = cloud_pos.xyz * f3;
					float n = rgba_fnoise(float3(p0.x - t0, p0.y - t0 / 5.0f, p0.z + t0 / 5.0f)) * w0;
					n += rgba_tnoise(float3(p1.x - t1, p1.y - t1 / 5.0f, p1.z + t1 / 5.0f)) * w1;
					n += rgba_tnoise(float3(p2.x - t2, p2.y - t2 / 5.0f, p2.z + t2 / 5.0f)) * w2;					
					n += rgba_tnoise(float3(p3.x - t3, p3.y - t3 / 5.0f, p3.z + t3 / 5.0f)) * w3;
					n = saturate(n * 3.0f - 1.5f + (cloud_density - 1.0f));
					float c =  n * (nsteps - (float)s)/nsteps;
					float4 text = { c, c, c, n };

					cloud = lerp(cloud, text, n);
					cloud_pos += ToCamRayUnit;
				}
			}
			else {
				uint x = abs(input.worldPos.x) / 100.0f;
				uint y = abs(input.worldPos.z) / 100.0f;
				uint c = (x % 2 == 0) && (y % 2 == 0);
				cloud = float4(c, c, c, c);
			}
			cloud *= 2.0f;
			//dense clouds are darker
			finalColor.rgb += cloud.rgb * cloud.a + finalColor.rgb * (1.0f - cloud.a);
		} 

		//Alpha blend cloud and background
		finalColor.a = max(dirLights[0].intensity, cloud.a);

		//Far fog
		float w = saturate(depth / 20000.0f);
		if (input.worldPos.y < 0.0f) {
			w = 1.0f;
		}
		finalColor = finalColor * (1.0f - w) + w * finalColor_precloud;		
#endif

	}
	else {
		finalColor.rgb += back_color;
		// Apply textures
		if ((material.flags & DIFFUSSE_MAP_ENABLED_FLAG) && space_intensity > 0.0f) {
			float2 sky_uv = input.uv;
			float4 text = diffuseTexture.SampleLevel(basicSampler, sky_uv, 0);
			finalColor += text * space_intensity * 0.8;
		}
	}
	
#if 1
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


	
#endif
	output.light_map = saturate(lightColor);
	output.scene = saturate(finalColor);
	return output;

}


