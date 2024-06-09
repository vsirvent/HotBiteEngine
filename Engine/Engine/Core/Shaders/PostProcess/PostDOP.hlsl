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
#include "../Common/Utils.hlsli"

Texture2D renderTexture : register(t0);
Texture2D depthTexture: register(t1);

SamplerState basicSampler : register(s0);

cbuffer externalData : register(b0)
{
    int dopActive;    
    float focusZ;
    float amplitude;
    int type;
}

#define EPSILON 1e-6
#define VERTICAL 1
#define HORIZONTAL 2
#define KERNEL_SIZE 9
#define HALF_KERNEL KERNEL_SIZE/2

void FillGaussianArray(out float array[KERNEL_SIZE], float dispersion)
{
    float sum = 0.0;
    int halfSize = KERNEL_SIZE / 2;
    float variance = clamp(dispersion, 0.1f, 4.0f);
    int i;
    for (i = -HALF_KERNEL; i <= HALF_KERNEL; ++i)
    {
        int x = i;
        array[i + halfSize] = exp(-(x * x) / (2.0 * variance * variance)) / sqrt(2.0 * 3.14159265358979323846 * variance * variance);
        sum += array[i + halfSize];
    }

    // Normalize the array
    for (i = 0; i < KERNEL_SIZE; ++i)
    {
        array[i] /= sum;
    }
}

float4 getColor(float2 pixel, float w, float h, float2 dir, float dispersion)
{
    if (dispersion < 0.1f) {
        pixel.x /= w;
        pixel.y /= h;
        return renderTexture.Sample(basicSampler, pixel);
    }
    else {
        float BlurWeights[KERNEL_SIZE];
        FillGaussianArray(BlurWeights, dispersion);
        float4 color = float4(0.f, 0.f, 0.f, 1.f);
        float2 pos;
        for (int i = -HALF_KERNEL; i <= HALF_KERNEL; ++i) {
            pos = pixel + dir * i;
            color += renderTexture.Load(float3(pos, 0)) * BlurWeights[i + HALF_KERNEL];
        }
        return color;
    }
}


float4 main(float4 pos: SV_POSITION) : SV_TARGET
{
    uint2 dimensions;
    uint w, h;
    renderTexture.GetDimensions(w, h);
    dimensions.x = w;
    dimensions.y = h;

    float2 tpos = pos.xy / dimensions;
    float4 color = { 0.0f, 0.0f, 0.0f, 0.0f };
    float dispersion = 0.0f;
    uint2 dir = { 0, 0 };
    if (type == VERTICAL) {
        dir = uint2(0, 1);
    }
    else if (type == HORIZONTAL) {
        dir = uint2(1, 0);
    }

    if (false) {
        float z0 = depthTexture.Sample(basicSampler, tpos).r;
        dispersion = pow((focusZ - z0), 2.0f) * amplitude / 100.0f;
    }
    return getColor(pos.xy, w, h, dir, dispersion);
}
