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
#include "../Common/Defines.hlsli"
#include "../Common/ShaderStructs.hlsli"
#include "../Common/Utils.hlsli"
#include "../Common/PixelCommon.hlsli"

Texture2D renderTexture : register(t0);
Texture2D worldTexture: register(t2);


cbuffer externalData : register(b0)
{
    int screenW;
    int screenH;
    float time;
    AmbientLight ambientLight;
    DirLight dirLights[MAX_LIGHTS];
    PointLight pointLights[MAX_LIGHTS];
    float3 cameraPosition;
    float3 cameraDirection;
	float cloud_density;

    float4 LightPerspectiveValues[MAX_LIGHTS / 2];
    matrix DirPerspectiveMatrix[MAX_LIGHTS];
    matrix DirStaticPerspectiveMatrix[MAX_LIGHTS];

	int dirLightsCount;
	int pointLightsCount;

    uint multi_texture_count;
    float multi_parallax_scale;

    uint4 packed_multi_texture_operations[MAX_MULTI_TEXTURE / 4];
    float4 packed_multi_texture_values[MAX_MULTI_TEXTURE / 4];
    float4 packed_multi_texture_uv_scales[MAX_MULTI_TEXTURE / 4];

	int disable_vol;
}

#include "../Common/PixelFunctions.hlsli"

float4 main(float4 pos: SV_POSITION) : SV_TARGET
{
	int i = 0;
	float2 tpos = pos.xy;
	tpos.x /= screenW;
	tpos.y /= screenH;
	float4 lightColor = renderTexture.SampleLevel(basicSampler, tpos, 0);
	float4 wpos = worldTexture.SampleLevel(basicSampler, tpos, 0);
	if (length(wpos) < Epsilon) {
		wpos.xyz = cameraPosition * cameraDirection * 1000.0f;
	}
	// Calculate the directional light
	for (i = 0; i < dirLightsCount; ++i) {
		if (dirLights[i].density > 0.0f && disable_vol == 0) {
			lightColor.rgb += DirVolumetricLight(wpos, dirLights[i], i, time, cloud_density);
		}
	}

	// Calculate the point lights
	for (i = 0; i < pointLightsCount; ++i) {
		if (pointLights[i].density > 0.0f && disable_vol == 0) {
			lightColor.rgb += VolumetricLight(wpos, pointLights[i], i);
		}
	}
	return lightColor;
}