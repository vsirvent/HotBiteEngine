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
#ifndef __RGBA_NOISE_HLSLI__
#define __RGBA_NOISE_HLSLI__

Texture2D rgbaNoise;

float rgba_hash(float n)
{
	return frac(cos(n) * 41415.92653);
}

float rgba_fnoise(in float3 x)
{
	float3 f = frac(x);
	float3 p = x - f;
	f = f * f * (3.0 - 2.0 * f);

	float n = p.x + p.y * 157.0 + 113.0 * p.z;
	return lerp(lerp(lerp(rgba_hash(n + 0.0), rgba_hash(n + 1.0), f.x),
		lerp(rgba_hash(n + 157.0), rgba_hash(n + 158.0), f.x), f.y),
		lerp(lerp(rgba_hash(n + 113.0), rgba_hash(n + 114.0), f.x),
			lerp(rgba_hash(n + 270.0), rgba_hash(n + 271.0), f.x), f.y), f.z);
}

float rgba_tnoise(float2 x)
{
	float2 f = frac(x);
	float2 p = x - f;
	float2 uv = (p.xy + f.xy);
	uv = fmod(uv, 256.0f) / 256.0f;
	float4 v = rgbaNoise.SampleLevel(basicSampler, uv, 0);
	return lerp(v.y, v.z, f.y);
}

float rgba_tnoise(float3 x)
{
#if 0
	return rgba_fnoise(x);
#else
	float3 f = frac(x);
	float3 p = x - f;
	float2 uv = ((p.xy + float2(37.0, 17.0) * p.z) + f.xy);
	uv = fmod(uv, 256.0f) / 256.0f;
	float4 v = rgbaNoise.SampleLevel(basicSampler, uv, 0);
	return lerp(v.y, v.z, f.z);
#endif
}

float3 rgba_tnoise3d(float3 x)
{
#if 0
	return rgba_fnoise(x);
#else
	float3 f = frac(x);
	float3 p = x - f;
	float2 uv = ((p.xy + float2(37.0, 17.0) * p.z) + f.xy);
	uv /= 256.0;
	float4 v = rgbaNoise.SampleLevel(basicSampler, uv, 0);
	return saturate(float3(lerp(v.y, v.z, v.x), lerp(v.x, v.y, v.z), lerp(v.x, v.z, v.y)));
#endif
}

#endif