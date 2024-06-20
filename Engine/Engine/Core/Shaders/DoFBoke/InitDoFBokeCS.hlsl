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
RWTexture2D<float4> dofKernels : register(u0);

#include "../Common/Complex.hlsli"
#include "DoFBoke.hlsli"

cbuffer externalData : register(b0)
{
    uint kernel_size;
}

complex GetKernelValue(uint position, float kernel_size, float variance, float freq)
{
    float x = position;
    float val = exp(-(x * x) / (2.0f * variance * variance));
    float pos = freq * (x * x) / kernel_size;
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
        dofKernels.GetDimensions(w, h);
        dimensions.x = w;
        dimensions.y = h;
    }

    uint kernel_position = 0;
    uint kernel_unit = DTid.y;
    float fkernel_size = kernel_size;
    int half_kernel = kernel_size / 2;
    float dispersion = (float)kernel_unit / 100.0f;
    float max_variance = (float)kernel_size / 6.0f;
    float variance = clamp(dispersion, 0.1f, max_variance);

    float freq = max_variance / (variance * 2.0f);

    int i;
    uint ii = 0;
    for (i = -half_kernel; i <= half_kernel; i += 2, ++ii)
    {
        complex c = GetKernelValue(i, fkernel_size, variance, freq);
        float4 data;
        data.r = c.real;
        data.g = c.img;
        c = GetKernelValue(i + 1, fkernel_size, variance, freq);
        data.b = c.real;
        data.a = c.img;
        dofKernels[uint2(ii + half_kernel, kernel_unit)] = data;
    }

    //Normalize
    float sum = 0.0f;
    for (uint x = 0; x < kernel_size; x += 2)
    {
        for (uint y = 0; y < kernel_size; ++y)
        {
            complex n0;
            complex n1;
            float4 data = dofKernels[uint2(x, kernel_unit)];
            n0.real = data.r;
            n0.img = data.g;
            n1.real = data.b;
            n1.img = data.a;
            complex c = MultComplex(n0, n1);
            sum += c.real + c.img;
        }
    }
    sum = sqrt(sum);
  
    for (uint p = 0; p < kernel_size / 2; ++p)
    {
        dofKernels[uint2(p, kernel_unit)] /= sum;
    }
}
