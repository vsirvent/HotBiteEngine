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

RWTexture2D<float4> kernels : register(u0);

#include "../Common/Complex.hlsli"

static const float MIN_KERNEL_VALUE = 0.005f;

cbuffer externalData : register(b0)
{
    uint kernel_size;
    uint ncomponents;
}

void ReadKernel(uint position, uint kernel, out complex c1, out complex c2) {
    float4 data = kernels[uint2(position + 1, kernel)];
    c1.real = data.r;
    c1.img = data.g;
    c2.real = data.b;
    c2.img = data.a;
}

complex GetKernelValue(int position, float kernel_size, float variance, float freq)
{
    float x = position;
    float x2 = x * x;
    float val = exp(-x2 / (2.0f * variance * variance));
    float pos = (freq * (x2)) / kernel_size;
    complex c;
    c.real = val * cos(pos);
    c.img = val * sin(pos);
    return c;
}

#define NTHREADS 32
[numthreads(1, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 dimensions;
    {
        uint w, h;
        kernels.GetDimensions(w, h);
        dimensions.x = w;
        dimensions.y = h;
    }

    uint kernel_unit = DTid.y;
    float fkernel_size = kernel_size;
    int half_kernel = kernel_size / 2;
    float dispersion = (float)kernel_unit / 100.0f;
    float max_variance = fkernel_size / 5.0f;
    float variance = clamp(dispersion, 0.1f, max_variance);

    float freq = max_variance / (variance * M_PI);

    int i;
    bool o1 = false;
    bool o2 = false;
    float4 offsets = float4(0.0f,0.0f, 0.0f, 0.0f);
    for (i = -half_kernel; i <= half_kernel; ++i)
    {
        complex c1 = GetKernelValue(i, fkernel_size, variance, freq * 0.5f);
        complex c2 = GetKernelValue(i, fkernel_size, variance, freq * 1.2f);
        //Each kernel float4 stores 2 kernel values generated with different frequencies
        kernels[uint2(i + 1 + half_kernel, kernel_unit)] = float4(c1.real, c1.img, c2.real, c2.img);
        if (!o1 && Length(c1) > MIN_KERNEL_VALUE) {
            offsets = kernels[uint2(0, kernel_unit)];
            offsets.r = (float)(i + half_kernel);
            kernels[uint2(0, kernel_unit)] = offsets;
            o1 = true;
        }
        if (!o2 && Length(c2) > MIN_KERNEL_VALUE) {
            offsets = kernels[uint2(0, kernel_unit)];
            offsets.g = (float)(i + half_kernel);
            kernels[uint2(0, kernel_unit)] = offsets;
            o2 = true;
        }
    }
#ifndef TEST //Testing avoids normalization
    //Normalize
    float sum1 = 0.0f;
    float sum2 = 0.0f;
    uint o = min(offsets.r, offsets.g);
    for (uint x = o; x < kernel_size - o; ++x)
    {
        complex c0_0, c1_0;
        ReadKernel(x, kernel_unit, c0_0, c1_0);
        for (uint y = 0; y < kernel_size; ++y)
        {            
            complex c0_1, c1_1;
            ReadKernel(y, kernel_unit, c0_1, c1_1);

            complex r1 = MultComplex(c0_0, c0_1);
            complex r2 = MultComplex(c1_0, c1_1);
            
            sum1 += ComplexToReal(r1);
            sum2 += ComplexToReal(r2);
        }
    }
    sum1 = sqrt(sum1);
    sum2 = sqrt(sum2);
    
    for (uint p = o; p <= kernel_size - o; ++p)
    {
        float4 v = kernels[uint2(p + 1, kernel_unit)];
        v.rg /= sum1;
        v.ba /= sum2;
        kernels[uint2(p + 1, kernel_unit)] = v;
    }
#endif
}
