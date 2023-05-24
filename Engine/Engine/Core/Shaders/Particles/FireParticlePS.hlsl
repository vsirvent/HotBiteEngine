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
	int parallaxShadowEnable;
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
}

//////////////////////
// Fire Flame shader

// procedural noise from IQ
float2 hash(float2 p)
{
	p = float2(dot(p, float2(127.1, 311.7)),
		dot(p, float2(269.5, 183.3)));
	return -1.0 + 2.0 * frac(sin(p) * 43758.5453123);
}

float noise(in float2 p)
{
	const float K1 = 0.366025404; // (sqrt(3)-1)/2;
	const float K2 = 0.211324865; // (3-sqrt(3))/6;

	float2 i = floor(p + (p.x + p.y) * K1);

	float2 a = p - i + (i.x + i.y) * K2;
	float2 o = (a.x > a.y) ? float2(1.0, 0.0) : float2(0.0, 1.0);
	float2 b = a - o + K2;
	float2 c = a - 1.0 + 2.0 * K2;

	float3 h = max(0.5 - float3(dot(a, a), dot(b, b), dot(c, c)), 0.0);

	float3 n = h * h * h * h * float3(dot(a, hash(i + 0.0)), dot(b, hash(i + o)), dot(c, hash(i + 1.0)));

	return dot(n, float3(70, 70, 70));
}

float fbm(float2 uv)
{
	float f;
	float2x2 m = float2x2(1.6, 1.2, -1.2, 1.6);
	f = 0.5000 * noise(uv); uv = mul(uv, m);
	f += 0.2500 * noise(uv); uv = mul(uv, m);
	f += 0.1250 * noise(uv); uv = mul(uv, m);
	f += 0.0625 * noise(uv); uv = mul(uv, m);
	f = 0.5 + 0.5 * f;
	return f;
}

RenderTarget main(GSParticleOutput input)
{
	RenderTarget output;
	float4 lightColor = { 0.0f, 0.0f, 0.0f, 0.0f };
	float4 finalColor = material.diffuseColor;
	float border = 1.0f - pow(length(abs(input.uv * 2.0f - 1.0f)), 2.0f);
	float fade_in = saturate((1.0f - input.life) / 0.1f);
	float3 wpos = input.worldPos;
	float2 uv = float2(input.uv.y,1.0f - input.uv.x);
	float2 q = uv;
	float tfire = time * 0.6f;
	float strength = input.id % 2 + 2.0f;
	float T3 = max(4., 1.25 * strength) * tfire + input.id*2.2;
	q.x -= 0.5;
	q.y -= 0.25;
	float n = fbm(strength * q - float2(0, T3));
	float c = 1. - 16. * pow(max(0., length(q * float2(1.8 + q.y * 1.5, .75)) - n * max(0., q.y + .25)), 1.2);
	float c1 = n * c * (1.5 - pow(1.25 * (1.0f - uv.y), 4.));
	c1 = clamp(c1, 0., 1.);

	float3 col = float3(1.5 * c1, 1.5 * c1 * c1 * c1, c1 * c1 * c1 * c1 * c1 * c1);

	float a = c * (1. - pow(uv.y, 3.));
	finalColor.rgb = lerp(float3(0., 0., 0.), col, a);
	finalColor.a = border * input.life * fade_in * length(finalColor.rgb);
	output.light_map = saturate(lightColor);
	output.scene = saturate(finalColor);
	return output;
}