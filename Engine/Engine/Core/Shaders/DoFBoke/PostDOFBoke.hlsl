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

#include "../Common/Complex.hlsli"
#include "DoFBoke.hlsli"

Texture2D renderTexture;
Texture2D depthTexture;
Texture2D kernels;

SamplerState basicSampler : register(s0);

#define MAX_KERNEL_SIZE 128
#define MIN_DISPERSION 0.1f

cbuffer externalData : register(b0)
{
    int dopActive;    
    float focusZ;
    float amplitude;
    uint type;
    uint kernel_size;
}

float4 main(float4 pos: SV_POSITION) : SV_TARGET
{
    float dispersion = 0.0f;
    int half_kernel = kernel_size / 2;
    float max_variance = (float)kernel_size / 5.0f;

    if (dopActive) {
        uint2 dimensions;
        uint w, h;
        renderTexture.GetDimensions(w, h);
        dimensions.x = w;
        dimensions.y = h;
        float2 tpos = pos.xy / dimensions;
        float z0 = depthTexture.Sample(basicSampler, tpos).r;
        dispersion = pow((focusZ - z0), 2.0f) * amplitude / 100.0f;
        dispersion = clamp(dispersion, 0.0f, max_variance);
    }
    uint position = dispersion * 100.0f - 10.0f;
    complex_color ccolor;
    InitComplexColor(ccolor);
    float2 pixel = pos.xy;

    if (type == VERTICAL) {
        float2 p = pixel;
        if (dispersion < MIN_DISPERSION) {
#ifndef TEST
            float4 color = renderTexture[p];
#else
            float c = (p.x % kernel_size == 0 && p.y % kernel_size == 0) ? 0.5f : 0.0f;
            float4 color = float4(c, c, c, 1.0f);
#endif
            ColorToComplexColor(color, ccolor);
        }
        else {
            float2 dir = float2(1.0f, 0.0f);
            complex_color in_color;
            for (int i = -half_kernel; i <= half_kernel; ++i) {
                p = pixel + dir * (float)i;
#ifndef TEST
                float4 color = renderTexture[p];
#else
                float c = (p.x % kernel_size == 0 && p.y % kernel_size == 0) ? 0.5f : 0.0f;
                float4 color = float4(c, c, c, 1.0f);
#endif
                ColorToComplexColor(color, in_color);
                float4 data = kernels.Load(float3(uint2(i + half_kernel, position), 0));
                complex kvalue;
                kvalue.real = data.r;
                kvalue.img = data.g;
                AccMultComplexColor(in_color, kvalue, ccolor);
            }
        }
        //Vertical return packed color including real + img values
        return PackColors(GetReal(ccolor), GetImg(ccolor));
    }
    else {
        int2 p = pixel;
        complex_color ccolor_orig;
        PackedComplexColorToComplexColor(renderTexture[p], ccolor_orig);
        if (dispersion < MIN_DISPERSION) {
            PackedComplexColorToComplexColor(renderTexture[p], ccolor);
        } else {
            float2 dir = float2(0, 1.0f);
            complex_color in_color;
            for (int i = -half_kernel; i <= half_kernel; ++i) {
                p = pixel + dir * i;
                PackedComplexColorToComplexColor(renderTexture[p], in_color);
                float4 data = kernels.Load(float3(uint2(i + half_kernel, position), 0));
                complex kvalue;
                kvalue.real = data.r;
                kvalue.img = data.g;
                AccMultComplexColor(in_color, kvalue, ccolor);
            }
        }
        return climit4(ComplexColorToColor(ccolor, (uint)((pixel.x + half_kernel) / kernel_size) % 3));
    }
}
