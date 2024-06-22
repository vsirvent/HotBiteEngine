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
#include "DoFBoke.hlsli"

cbuffer externalData : register(b0)
{
    uint kernel_size;
    uint ncomponents;
}

void ReadKernel(uint position, uint kernel, out complex c1, out complex c2) {
    float4 data = kernels[uint2(position, kernel)];
    c1.real = data.r;
    c1.img = data.g;
    c2.real = data.b;
    c2.img = data.a;
}

complex GetKernelValue(int position, float kernel_size, float variance, float freq, float component)
{
    float x = position;
    float x2 = x * x;
    float val = exp(-x2 / (2.0f * variance * variance));
    float pos = (freq * component * (x2)) / kernel_size;
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
    for (i = -half_kernel; i <= half_kernel; ++i)
    {
        complex c1 = GetKernelValue(i, fkernel_size, variance, freq, 0.5f);
        complex c2 = GetKernelValue(i, fkernel_size, variance, freq, 7.0f);
        kernels[uint2(i + half_kernel, kernel_unit)] = float4(c1.real, c1.img, c2.real, c2.img);
    }
#ifndef TEST
    //Normalize
    float sum1 = 0.0f;
    float sum2 = 0.0f;
    for (uint x = 0; x < kernel_size; ++x)
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
  
    for (uint p = 0; p < kernel_size; ++p)
    {
        float4 v = kernels[uint2(p, kernel_unit)];
        v.rg /= sum1;
        v.ba /= sum2;
        kernels[uint2(p, kernel_unit)] = v;
    }
#endif
}
