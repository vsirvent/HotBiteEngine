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
	float3 cameraPosition;
	float3 cameraDirection;
	int screenW;
	int screenH;
	float4 LightPerspectiveValues[MAX_LIGHTS / 2];
	matrix DirPerspectiveMatrix[MAX_LIGHTS];
	matrix DirStaticPerspectiveMatrix[MAX_LIGHTS];
	matrix spot_view;
	float time;
}

Texture2D diffuseTexture;

RenderTarget main(GSParticleOutput input)
{
	RenderTarget output;
	float4 lightColor = { 0.0f, 0.0f, 0.0f, 0.0f };
	float alpha = pow(abs(input.life), 3.2f);
	float4 finalColor = material.ambientColor * alpha + material.diffuseColor * (1.0f - alpha);
	float border = 1.0f - pow(length(abs(input.uv * 2.0f - 1.0f)), 4.0f);
	float fade_in = saturate((1.0f - input.life) / 0.1f);
	// Apply textures
	if (material.flags & DIFFUSSE_MAP_ENABLED_FLAG) {
		float3 text_color = diffuseTexture.Sample(basicSampler, input.uv).rgb;
		float alpha_value = length(material.alphaColor - text_color);
		if (material.flags & ALPHA_ENABLED_FLAG) {
			if (alpha_value > 1.0f) {
				finalColor.rgb *= text_color;
				finalColor.a = alpha_value*(1.0f - border);
			}
			else {
				discard;
			}
		}
		else {
			finalColor.rgb *= text_color;
		}
	}
	finalColor.a = border*input.life * fade_in;

	output.light_map = saturate(lightColor);
	output.scene = saturate(finalColor);
	return output;
}