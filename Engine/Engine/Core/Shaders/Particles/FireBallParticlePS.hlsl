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

#include "../Common/ShaderStructs.hlsli"
#include "../Common/PixelCommon.hlsli"
#include "../Common/FastNoise.hlsli"

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
	float sizeIncrementRatio;

	uint multi_texture_count;
	float multi_parallax_scale;

	uint4 packed_multi_texture_operations[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_values[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_uv_scales[MAX_MULTI_TEXTURE / 4];
}


#include "../Common/PixelFunctions.hlsli"

RenderTarget main(GSParticleOutput input)
{
	RenderTarget output;
	float4 lightColor = { 0.0f, 0.0f, 0.0f, 0.0f };
	float alpha = pow(input.life, 3.2f);
	float4 finalColor = material.ambientColor * alpha + material.diffuseColor * (1.0f - alpha);
	float border = 1.0f - pow(length(abs(input.uv * 2.0f - 1.0f)), 2.0f);
	float4 wpos = input.worldPos;
	float fade_in = saturate((1.0f - input.life) / 0.1f);
#if 1
	// Calculate the ambient light
	finalColor.rgb += CalcAmbient(float3(0.0f, 1.0f, 0.0f));
#endif
#if 1
	uint i = 0;
	// Calculate the directional light
	for (i = 0; i < dirLightsCount; ++i) {
		finalColor.rgb += 0.6f * CalcDirectionalWithoutNormal(wpos, material, dirLights[i], 0.0f, i, lightColor);
	}
#endif
#if 0
	// Apply point lights
	for (i = 0; i < pointLightsCount; ++i) {
		if (length(input.worldPos.xyz - pointLights[i].Position) < pointLights[i].Range) {
			finalColor.rgb += 0.6f * CalcPointWithoutNormal(wpos, material, pointLights[i], i, lightColor);
		}
	}
#endif
	// Apply textures
	if (material.flags & DIFFUSSE_MAP_ENABLED_FLAG) {
		int n = input.id % 4;
		float2 uv = input.uv;
		switch (n) {
		case 0: uv.x = 1.0f - uv.x; break;
		case 1: uv.y = 1.0f - uv.y; break;
		case 2: uv.x = 1.0f - uv.x; uv.y = 1.0f - uv.y; break;
		}
		float3 text_color = diffuseTexture.Sample(basicSampler, uv).rgb;
		float alpha_value = length(material.alphaColor - text_color);
		if (material.flags & ALPHA_ENABLED_FLAG) {
			if (alpha_value > 1.0f) {
				finalColor.rgb *= text_color;
			}
			else {
				discard;
			}
		}
		else {
			finalColor.rgb *= text_color;
		}
	}
	float t = time * 0.5f;
	wpos *= 1.1f - 0.1f * (1.0f - alpha);
	fnl_state state = fnlCreateState();
	state.noise_type = FNL_NOISE_OPENSIMPLEX2S;
	state.octaves = 1;
	state.frequency = 1.0f;
	float n0 = saturate(0.5f * fnlGetNoise3D(state, wpos.x - 0.5f * t, wpos.y - t, wpos.z + 0.5f * t) + 0.5f + 0.7f * alpha);

	finalColor.rgb *= n0;
	finalColor.a = border * input.life * n0 * fade_in;

	output.light_map = saturate(lightColor);
	output.scene = saturate(finalColor);
	return output;
}