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
}

complex ReadKernel(uint position, uint kernel) {
    complex c;
    float4 data = kernels[uint2(position / 2, kernel)];
    if ((position % 2) == 0) {
        c.real = data.r;
        c.img = data.g;
    }
    else {
        c.real = data.b;
        c.img = data.a;
    }
    return c;
}

complex GetKernelValue(int position, float kernel_size, float variance, float freq)
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
        kernels.GetDimensions(w, h);
        dimensions.x = w;
        dimensions.y = h;
    }

    uint kernel_unit = DTid.y;
    float fkernel_size = kernel_size;
    int half_kernel = kernel_size / 2;
    float dispersion = (float)kernel_unit / 100.0f;
    float max_variance = fkernel_size / 6.0f;
    float variance = clamp(dispersion, 0.1f, max_variance);

    float freq = max_variance / (variance * M_PI);

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
        kernels[uint2(ii, kernel_unit)] = data;
    }

    //Normalize
    float sum = 0.0f;
    for (uint x = 0; x < kernel_size; ++x)
    {
        complex n0 = ReadKernel(x, kernel_unit);
        for (uint y = 0; y < kernel_size; ++y)
        {
            
            complex n1 = ReadKernel(y, kernel_unit);
            complex c = MultComplex(n0, n1);
            sum += c.real + c.img;
        }
    }
    sum = sqrt(sum);
  
    for (uint p = 0; p <= half_kernel; ++p)
    {
        kernels[uint2(p, kernel_unit)] /= sum;
    }
}
