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

Texture2D renderTexture;
Texture2D depthTexture;
Texture2D kernels;

SamplerState basicSampler : register(s0);

#define MAX_KERNEL_SIZE 128
#define MIN_DISPERSION 0.1f
#define GHOST
static const uint VERTICAL = 1;
static const uint HORIZONTAL = 2;
static const int MAX_DISTANCE = 500;

cbuffer externalData : register(b0)
{
    int dopActive;    
    float focusZ;
    float amplitude;
    uint type;
    uint kernel_size;
}

void GetOfsset(in Texture2D k, uint position, out float o0, out float o1)
{
    float4 data = k[uint2(0, position)];
    o0 = data.r;
    o1 = data.g;
};

//Unpack kernel values
void GetKernelValue(in Texture2D k, uint i, uint position, out complex c0, out complex c1)
{
    float4 data = k[uint2(i + 1, position)];
    c0.real = data.r;
    c0.img = data.g;
    c1.real = data.b;
    c1.img = data.a;
};

//Get the row value in the kernel based on the variance
uint GetPosition(float2 pixel, float2 ratio, uint max_positions)
{
    uint position = 0;
    if (dopActive) {
        float z0 = depthTexture[pixel * ratio].r;
        float dispersion = abs(focusZ - z0) * amplitude * 0.1f;
        position = clamp(dispersion * 100.0f - 10.0f, 0, max_positions - 1);
    }
    return position;
}

float4 main(float4 pos: SV_POSITION) : SV_TARGET
{
    int half_kernel = kernel_size / 2;
    float max_variance = (float)kernel_size / 5.0f;
    float2 pixel = pos.xy;
    float w0, h0, w1, h1;
    renderTexture.GetDimensions(w0, h0);
    depthTexture.GetDimensions(w1, h1);
    float2 ratio = float2(w1 / w0, h1 / h0);
    uint max_positions;
    uint dummy;
    kernels.GetDimensions(dummy, max_positions);
    complex_color ccolor, ccolor2;
    InitComplexColor(ccolor);
    InitComplexColor(ccolor2);
    int near_focus = max_positions / 20;
    if (type == VERTICAL) {

        float2 p = pixel;
        int position = GetPosition(p, ratio, max_positions);
        if (position == 0) {
#ifndef TEST
            float4 color = renderTexture[p];
#else
            float c = ((uint)p.x % kernel_size == 0 && (uint)p.y % kernel_size == 0) ? 0.5f : 0.00051f;
            float4 color = float4(c, c, c, 1.0f);
#endif
            ColorToComplexColor(color, ccolor);
            ccolor2 = ccolor;
        }
        else {
            float2 dir = float2(1.0f, 0.0f);
            complex_color in_color;
            complex w0 = InitComplex(1.0f, 0.0f);
            complex w1 = InitComplex(1.0f, 0.0f);

            bool normalize = false;
            float o1, o2;
            GetOfsset(kernels, position, o1, o2);
            float offset = min(o1, o2);
            int amp = half_kernel - offset;
            for (int i = -amp; i <= amp; ++i) {
                p = pixel + dir * (float)i;
#ifndef TEST
                float4 color = renderTexture[p];
#else
                float c = ((uint)p.x % kernel_size == 0 && (uint)p.y % kernel_size == 0) ? 0.5f : 0.00051f;
                float4 color = float4(c, c, c, 1.0f);
#endif
                complex k0, k1;
                GetKernelValue(kernels, i + half_kernel, position, k0, k1);
#ifdef GHOST
                //Ghosting is very noticiable when there is a big gap of depth distance and object is near focus
                //in that case, we avoid adding that pixel and compensate later to add extra weight
                int tmp_pos = GetPosition(p, ratio, max_positions);
#else
                int tmp_pos = position;
#endif
                if (tmp_pos < near_focus && abs(position - tmp_pos) > MAX_DISTANCE) {
                    normalize = true;
                    w0 = AddComplex(w0, k0);
                    w1 = AddComplex(w1, k1);
                }
                else {
                    ColorToComplexColor(color, in_color);
                    AccMultComplexColor(in_color, k0, ccolor);
                    AccMultComplexColor(in_color, k1, ccolor2);
                }
            }

            // Normalize by the sum of the included weights
            if (normalize) {
                w0 = PowComplex(w0);
                w1 = PowComplex(w1);
                ccolor = ScaleComplexColor(ccolor, Length(w0));
                ccolor2 = ScaleComplexColor(ccolor2, Length(w1));
            }
        }
        //Vertical return 2 complex colors packed in a single float4
        return Pack4Colors(GetReal(ccolor), GetImg(ccolor), GetReal(ccolor2), GetImg(ccolor2));
    }
    else {
        float2 p = pixel;
        int position = GetPosition(p, ratio, max_positions);
        if (position == 0) {
            PackedComplex2ColorToComplexColor(renderTexture[p], ccolor, ccolor2);
        } else {
            float2 dir = float2(0, 1.0f);
            complex_color in_color, in_color2;
            complex w0 = InitComplex(1.0f, 0.0f);
            complex w1 = InitComplex(1.0f, 0.0f);
            bool normalize = false;
            float o1, o2;
            GetOfsset(kernels, position, o1, o2);
            float offset = min(o1, o2);
            int amp = half_kernel - offset;
            for (int i = -amp; i <= amp; ++i) {
                p = pixel + dir * i;
                complex k0, k1;
                GetKernelValue(kernels, i + half_kernel, position, k0, k1);
#ifdef GHOST
                //Ghosting is very noticiable when there is a big gap of depth distance and object is near focus
                //in that case, we avoid adding that pixel and compensate later to add extra weight
                int tmp_pos = GetPosition(p, ratio, max_positions);
#else
                int tmp_pos = position;
#endif
                //Ghosting is very noticiable when there is a big gap of depth distance and object is near focus
                //in that case, we avoid adding that pixel and compensate later to add extra weight
                if (tmp_pos < near_focus && abs(position - tmp_pos) > MAX_DISTANCE) {
                    normalize = true;
                    w0 = AddComplex(w0, k0);
                    w1 = AddComplex(w1, k1);
                }
                else {
                    PackedComplex2ColorToComplexColor(renderTexture[p], in_color, in_color2);
                    AccMultComplexColor(in_color, k0, ccolor);
                    AccMultComplexColor(in_color2, k1, ccolor2);
                }
            }

            // Normalize by the sum of the included weights
            if (normalize) {
                w0 = PowComplex(w0);
                w1 = PowComplex(w1);
                ccolor = ScaleComplexColor(ccolor, Length(w0));
                ccolor2 = ScaleComplexColor(ccolor2, Length(w1));
            }
        }
#ifdef TEST //Diferent rendering circles combinations to check how they mix
        int type = ((p.x + half_kernel) / kernel_size) % 4;
        switch (type) {
        case 0: return float4(climit4((ComplexColorToColor(ccolor) + ComplexColorToColor(ccolor2)) * 0.5f).r, 0.0f, 0.0f, 1.0f);
        case 1: return float4(0.0f, climit4((ComplexColorToColor(ccolor))).g, 0.0f, 1.0f);
        case 2: return float4(0.0f, 0.0f, climit4((ComplexColorToColor(ccolor2))).b, 1.0f);
        case 3: return float4(climit4((ComplexColorToColor(ccolor) - 0.5f*ComplexColorToColor(ccolor2)) * 0.5f).rg, 0.0f, 1.0f);
        }
#else
        float4 c1 = ComplexColorToColor(ccolor);
        float4 c2 = ComplexColorToColor(ccolor2);
        return climit4((ComplexColorToColor(ccolor) - 0.3f * ComplexColorToColor(ccolor2)) * 1.6f);
#endif
    }
}
