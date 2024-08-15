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
#include "../Common/QuickNoise.hlsli"
#include "../Common/RayDefines.hlsli"

cbuffer externalData : register(b0)
{
    int rt_enabled;
}

RWTexture2D<float4> output : register(u0);
Texture2D input: register(t2);
Texture2D depthTexture: register(t3);
Texture2D lightTexture: register(t4);
Texture2D bloomTexture: register(t5);
Texture2D rtTexture0 : register(t6);
Texture2D rtTexture1 : register(t7);
Texture2D volLightTexture: register(t8);
Texture2D dustTexture: register(t9);
Texture2D lensFlareTexture: register(t10);
Texture2D positions: register(t11);
Texture2D normals: register(t12);
SamplerState basicSampler : register(s0);

float4 readColor(float2 pixel, texture2D text, uint w, uint h) {
    uint w2, h2;
    text.GetDimensions(w2, h2);

    if (w2 == w && h2 == h) {
        return text.SampleLevel(basicSampler, pixel, 0);
    }
    else {
        //return GetInterpolatedColor(pixel, text, float2(w2, h2));
        return Get3dInterpolatedColor(pixel, text, float2(w2, h2), positions, normals, float2(w, h));
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
    float4 rt0 = readColor(tpos, rtTexture0, w, h);
    float4 rt1 = readColor(tpos, rtTexture1, w, h);
    float4 vol = readColor(tpos, volLightTexture, w, h);
    float4 dust = readColor(tpos, dustTexture, w, h);
    float4 lens_flare = readColor(tpos, lensFlareTexture, w, h);

    if (rt_enabled) {
        color += (color + 2.0f * l) * rt0 + rt1 + b + dust + lens_flare + vol;
        //color = rt0;
    }
    else {
        color += color * l * 0.2f + b + dust + lens_flare + vol;
    }
    output[pixel] = color;
}
