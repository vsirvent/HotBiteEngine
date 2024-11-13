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

#include "../Common/Utils.hlsli"

cbuffer externalData : register(b0)
{
    int rt_enabled;
    int frame_count;
    float time;
    uint debug;
}

RWTexture2D<float4> output : register(u0);
Texture2D input: register(t2);
Texture2D depthTexture: register(t3);
Texture2D lightTexture: register(t4);
Texture2D bloomTexture: register(t5);
Texture2D rtTexture0 : register(t6);
Texture2D rtTexture1 : register(t7);
Texture2D rtTexture2 : register(t8);
Texture2D rtTexture3 : register(t9);
Texture2D volLightTexture: register(t10);
Texture2D dustTexture: register(t11);
Texture2D lensFlareTexture: register(t12);
Texture2D positions: register(t13);
Texture2D normals: register(t14);
Texture2D emissionTexture: register(t15);
SamplerState basicSampler : register(s0);

#include "../Common/RGBANoise.hlsli"

float4 Get3dInterpolatedColor(float2 uv, Texture2D text, float2 dimension, Texture2D positions, Texture2D normals, float2 pos_dimension) {


	// Calculate the texture coordinates in the range [0, 1]
	float2 texCoords = uv * dimension;
	float2 pos_ratio = pos_dimension / dimension;

	// Calculate the integer coordinates of the four surrounding pixels
	int2 p00 = (int2)floor(texCoords);
	int2 p11 = int2(p00.x + 1, p00.y + 1);
	int2 p01 = int2(p00.x, p00.y + 1);
	int2 p10 = int2(p00.x + 1, p00.y);

	float epsilon = 10e-4;

	//Get 3d positions
	uint2 pos_p00 = round(p00 * pos_ratio);
	uint2 pos_p11 = round(p11 * pos_ratio);
	uint2 pos_p01 = round(p01 * pos_ratio);
	uint2 pos_p10 = round(p10 * pos_ratio);
	uint2 in_pos = round(texCoords * pos_ratio);

	float3 wp00 = positions[pos_p00].xyz;
	float3 wp11 = positions[pos_p11].xyz;
	float3 wp01 = positions[pos_p01].xyz;
	float3 wp10 = positions[pos_p10].xyz;
	float3 wpxx = positions[in_pos].xyz;

	if (wp00.x >= FLT_MAX || wp11.x >= FLT_MAX || wp01.x >= FLT_MAX || wp10.x >= FLT_MAX || wpxx.x >= FLT_MAX) {
		return GetInterpolatedColor(uv, text, dimension);
	}
	
	float d00 = dist2(wpxx - wp00);  // Distance to wp00
	float d11 = dist2(wpxx - wp11);  // Distance to wp11
	float d01 = dist2(wpxx - wp01);  // Distance to wp01
	float d10 = dist2(wpxx - wp10);  // Distance to wp10

	float all_dist = d00 + d11 + d01 + d10;

	if (all_dist < epsilon) {
		return GetInterpolatedColor(uv, text, dimension);
	}

	float w00 = 1.0f - d00 / all_dist;
	float w11 = 1.0f - d11 / all_dist;
	float w01 = 1.0f - d01 / all_dist;
	float w10 = 1.0f - d10 / all_dist;

#if 1
	// Calculate the fractional part of the coordinates
	float2 f = frac(texCoords);

	// Calculate the weights for bilinear interpolation
	w00 *= (1.0f - f.x) * (1.0f - f.y);
	w11 *= f.x * f.y;
	w01 *= (1.0f - f.x) * f.y;
	w10 *= f.x * (1.0f - f.y);
#endif
#if 0
	static const float DIST_K = 2.0f;
	w00 = pow(w00, DIST_K);
	w11 = pow(w11, DIST_K);
	w01 = pow(w01, DIST_K);
	w10 = pow(w10, DIST_K);
#endif
	float3 n00 = normals[pos_p00].xyz;
	float3 n11 = normals[pos_p11].xyz;
	float3 n01 = normals[pos_p01].xyz;
	float3 n10 = normals[pos_p10].xyz;
	float3 nxx = normals[in_pos].xyz;
#if 1
	static const float DOT_K = 2.0f;
	w00 *= pow(saturate(dot(nxx, n00)), DOT_K);
	w11 *= pow(saturate(dot(nxx, n11)), DOT_K);
	w01 *= pow(saturate(dot(nxx, n01)), DOT_K);
	w10 *= pow(saturate(dot(nxx, n10)), DOT_K);
#endif
	// Normalize weights
	float totalWeight = w00 + w11 + w01 + w10;

	if (totalWeight < epsilon) {
		return GetInterpolatedColor(uv, text, dimension);
	}
	w00 /= totalWeight;
	w11 /= totalWeight;
	w01 /= totalWeight;
	w10 /= totalWeight;

	return (text[p00] * w00 + text[p11] * w11 + text[p01] * w01 + text[p10] * w10);
}


float4 readColor(float2 pixel, texture2D text, uint w, uint h) {
    uint w2, h2;
    text.GetDimensions(w2, h2);
    if (w2 == w && h2 == h) {
        return text.SampleLevel(basicSampler, pixel, 0);
    }
    else {
#if 1
        float ratioW = ((float)w * 0.5f) / w2;
		float ratioH = ((float)h * 0.5f) / h2;
		uint n = 2;
		float x = 0;
		float y = 0;
		float4 c = 2 * Get3dInterpolatedColor(pixel, text, float2(w2, h2), positions, normals, float2(w, h));
		for (x = -ratioW; x < ratioW; x++) {
			for (y = -ratioH; y < ratioH; y++) {
				c += text.SampleLevel(basicSampler, pixel + float2(x * 0.5f / w2, y * 0.5f / h2), 0);
				//c += Get3dInterpolatedColor(pixel + float2(x / w, y / h), text, float2(w2, h2), positions, normals, float2(w, h));
				n++;
			}
		}
		c /= n;
		return c;
#else
		return Get3dInterpolatedColor(pixel, text, float2(w2, h2), positions, normals, float2(w, h));
#endif
    }
}

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint w, h;
    output.GetDimensions(w, h);
    float2 pixel = float2(DTid.x, DTid.y);

    float2 tpos = pixel;
    tpos.x /= w;
    tpos.y /= h;

    float4 color = input[pixel];
    float4 l = readColor(tpos, lightTexture, w, h);
    float4 b = readColor(tpos, bloomTexture, w, h);
    float4 e = readColor(tpos, emissionTexture, w, h);
    float4 rt0 = readColor(tpos, rtTexture0, w, h);
    float4 rt1 = readColor(tpos, rtTexture1, w, h);
    float4 rt2 = readColor(tpos, rtTexture2, w, h);
    float4 rt3 = readColor(tpos, rtTexture3, w, h);
    float4 vol = readColor(tpos, volLightTexture, w, h);
    float4 dust = readColor(tpos, dustTexture, w, h);
    float4 lens_flare = readColor(tpos, lensFlareTexture, w, h);

    rt2 = pow(rt2, 0.5f);
    
	if (rt_enabled) {
        if (true) { //debug == 0) {
            color = color * (l + rt0 + rt2 + rt3) + rt1 + b + dust + lens_flare + vol;
        }
        else {
            color = rt0 + rt1 + rt2 + rt3;
        }
    }
    else {
        color = color * l + b + dust + lens_flare + vol;
    }
#if 1
    output[pixel] = climit4(color);
#else
    float2 p = pixel * 0.3f + frac(time) * 1000.0f;
    float r = 1.0f + (0.1f * rgba_tnoise(p));
    output[pixel] = climit4(color * r);
#endif
}
