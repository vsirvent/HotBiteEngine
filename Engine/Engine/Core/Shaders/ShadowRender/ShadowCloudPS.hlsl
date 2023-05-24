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

cbuffer externalData : register(b0) {
	float time;
	float cloud_density;
	uint cloud_test;
}

float main(DepthVertexToPixel input) : SV_TARGET
{
	float3 sky_pos = input.worldPos.xyz;

	if (cloud_test == 0) {
		float f0 = 0.001f;
		float f1 = 0.005f;
		float f2 = 0.01f;
		float f3 = 0.03f;
		float w0 = 0.4f;
		float w1 = 0.3f;
		float w2 = 0.2f;
		float w3 = 0.1f;
		float t0 = time * 0.1f;
		float t1 = time * 0.3f;
		float t2 = time * 0.7f;
		float t3 = time * 0.8f;
		float3 cloud_pos = input.worldPos.xyz;
		// Apply textures
		float3 p0 = cloud_pos.xyz * f0;
		float3 p1 = cloud_pos.xyz * f1;
		float3 p2 = cloud_pos.xyz * f2;
		float3 p3 = cloud_pos.xyz * f3;
		float n = rgba_fnoise(float3(p0.x - t0, p0.y - t0 / 5.0f, p0.z + t0 / 5.0f)) * w0;
		n += rgba_tnoise(float3(p1.x - t1, p1.y - t1 / 5.0f, p1.z + t1 / 5.0f)) * w1;
		n += rgba_tnoise(float3(p2.x - t2, p2.y - t2 / 5.0f, p2.z + t2 / 5.0f)) * w2;
		n += rgba_tnoise(float3(p3.x - t3, p3.y - t3 / 5.0f, p3.z + t3 / 5.0f)) * w3;
		n = saturate(n * 3.0f - 1.5f);
		float offset = 1.0f - cloud_density;
		return saturate(n - offset);
	}
	else {
		uint x = abs(sky_pos.x) / 100.0f;
		uint y = abs(sky_pos.z) / 100.0f;
		uint c = (x % 2 == 0) && (y % 2 == 0);
		return c;
	}
}