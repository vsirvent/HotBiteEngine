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
    uint type;
}

//#define TEST

static const float EPSILON = 1e-6f;
static const uint VERTICAL = 1;
static const uint HORIZONTAL = 2;
static const int KERNEL_SIZE = 63;
static const int HALF_KERNEL = KERNEL_SIZE / 2;
static const float MAX_VARIANCE = (float)KERNEL_SIZE / 6;
static const float M_PI = 3.14159265358979323846f; /* pi */

struct complex {
    float real;
    float img;
};

struct complex_color {
    complex r;
    complex g;
    complex b;
};

complex MultComplex(const complex n0, const complex n1) {
    complex ret;
    ret.real = n0.real * n1.real - n0.img * n1.img;
    ret.img = n0.real * n1.img + n0.img * n1.real;
    return ret;
}

void AccMultComplex(const complex n0, const complex n1, in out complex val) {
    val.real += n0.real * n1.real - n0.img * n1.img;
    val.img += n0.real * n1.img + n0.img * n1.real;
}

void AccMultComplexColor(const complex_color in_color, const complex value, in out complex_color ccolor) {
    AccMultComplex(in_color.r, value, ccolor.r);
    AccMultComplex(in_color.g, value, ccolor.g);
    AccMultComplex(in_color.b, value, ccolor.b);
}

void PackedComplexColorToComplexColor(float4 packed_color, in out complex_color ccolor) {
    float4 c1;
    float4 c2;
    UnpackColors(packed_color, c1, c2);
    ccolor.r.real = c1.r;
    ccolor.g.real = c1.g;
    ccolor.b.real = c1.b;
    ccolor.r.img = c2.r;
    ccolor.g.img = c2.g;
    ccolor.b.img = c2.b;
}

void ColorToComplexColor(float4 color, out complex_color ccolor) {
    ccolor.r.real = color.r;
    ccolor.g.real = color.g;
    ccolor.b.real = color.b;
    ccolor.r.img = 0.0f;
    ccolor.g.img = 0.0f;
    ccolor.b.img = 0.0f;
}

float Energy(const complex value) {
    return (value.real * value.real + value.img * value.img);
}
float Length(const complex value) {
    return sqrt(Energy(value));
}

float ComplexToReal(const complex in_value, uint type) {
#if 0 //def TEST
    if (type == 0)
       return in_value.real;
    if (type == 1)
       return in_value.img;
    return 0.0f;
#else
    return in_value.real + in_value.img;
#endif
}

float4 ComplexColorToColor(const complex_color ccolor, uint type) {
    return float4(ComplexToReal(ccolor.r, type), ComplexToReal(ccolor.g, type), ComplexToReal(ccolor.b, type), 1.0f);
}

void InitComplexColor(out complex_color ccolor) {
    ccolor.r.real = 0.0f;
    ccolor.g.real = 0.0f;
    ccolor.b.real = 0.0f;
    ccolor.r.img = 0.0f;
    ccolor.g.img = 0.0f;
    ccolor.b.img = 0.0f;
}

float4 GetReal(const complex_color ccolor) {
    return float4(ccolor.r.real, ccolor.g.real, ccolor.b.real, 1.0f);
}

float4 GetImg(const complex_color ccolor) {
    return float4(ccolor.r.img, ccolor.g.img, ccolor.b.img, 1.0f);
}


void FillKernel(out complex array[KERNEL_SIZE], float dispersion)
{
    float variance = clamp(dispersion, 0.1f, MAX_VARIANCE);
    float freq = MAX_VARIANCE / (variance * 2.0f);
    float energy = 0.0f;
    int i;
    for (i = -HALF_KERNEL; i <= HALF_KERNEL; ++i)
    {
        float x = i;
        float val = exp(-(x * x) / (2.0f * variance * variance)) / sqrt(2.0f * M_PI * variance * variance);
        float pos = freq * (x * x) / (float)KERNEL_SIZE;
        array[i + HALF_KERNEL].real = val * cos(pos);
        array[i + HALF_KERNEL].img = val * sin(pos);

        complex c = array[i + HALF_KERNEL];
        energy += Energy(c);
    }
#if 0 //ndef TEST
    for (uint x = 0; x < KERNEL_SIZE; ++x)
    {
        for (uint y = 0; y < KERNEL_SIZE; ++y)
        {
            complex c = MultComplex(array[x], array[y]);
            sum += c.real + c.img;
        }
    }
    sum = sqrt(sum);
    for (i = 0; i < KERNEL_SIZE; ++i)
    {
        array[i].real /= sum;
        array[i].img /= sum;
    }
#else
    energy = sqrt(energy);
    for (i = 0; i < KERNEL_SIZE; ++i)
    {
        array[i].real /= energy;
        array[i].img /= energy;
    }
#endif
}

float4 main(float4 pos: SV_POSITION) : SV_TARGET
{
    float dispersion = 0.0f;
    uint2 dir = { 0, 0 };
    if (type == VERTICAL) {
        dir = uint2(0, 1);
    }
    else if (type == HORIZONTAL) {
        dir = uint2(1, 0);
    }

    if (dopActive) {
        uint2 dimensions;
        uint w, h;
        renderTexture.GetDimensions(w, h);
        dimensions.x = w;
        dimensions.y = h;
        float2 tpos = pos.xy / dimensions;

        float z0 = depthTexture.Sample(basicSampler, tpos).r;
        dispersion = pow((focusZ - z0), 2.0f) * amplitude / 100.0f;
    }

    complex_color ccolor;
    InitComplexColor(ccolor);
    float2 pixel = pos.xy;

    if (type == VERTICAL) {
        uint2 p = pixel;
        if (dispersion < 0.1f) {
#ifndef TEST
            float4 color = renderTexture.Load(float3(p, 0));
#else
            float c = (p.x % KERNEL_SIZE == 0 && p.y % KERNEL_SIZE == 0) ? 0.5f : 0.0f;
            float4 color = float4(c, c, c, 1.0f);
#endif
            ColorToComplexColor(color, ccolor);
        }
        else {
            float2 dir = float2(1, 0);
            complex kernel[KERNEL_SIZE];
            FillKernel(kernel, dispersion);
            complex_color in_color;

            for (int i = -HALF_KERNEL; i <= HALF_KERNEL; ++i) {
                p = pixel + dir * i;
#ifndef TEST
                float4 color = renderTexture.Load(float3(p, 0));
#else
                float c = (p.x % KERNEL_SIZE == 0 && p.y % KERNEL_SIZE == 0) ? 0.5f : 0.0f;
                float4 color = float4(c, c, c, 1.0f);
#endif
                ColorToComplexColor(color, in_color);
                AccMultComplexColor(in_color, kernel[i + HALF_KERNEL], ccolor);
            }
        }
        //Vertical return packed color including real + img values
        return PackColors(GetReal(ccolor), GetImg(ccolor));
    }
    else {
        uint2 p = pixel;
        if (dispersion < 0.1f) {
            PackedComplexColorToComplexColor(renderTexture.Load(float3(p, 0)), ccolor);
        } else {
            float2 dir = float2(0, 1);
            complex kernel[KERNEL_SIZE];
            FillKernel(kernel, dispersion);
            complex_color in_color;
            for (int i = -HALF_KERNEL; i <= HALF_KERNEL; ++i) {
                p = pixel + dir * i;
                PackedComplexColorToComplexColor(renderTexture.Load(float3(p, 0)), in_color);
                AccMultComplexColor(in_color, kernel[i + HALF_KERNEL], ccolor);
            }
        }
        return ComplexColorToColor(ccolor, (uint)(pixel.x / KERNEL_SIZE) % 2);
    }
}
