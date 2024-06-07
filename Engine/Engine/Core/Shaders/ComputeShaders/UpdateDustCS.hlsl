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
RWTexture2D<float4> dustTexture : register(u0);

cbuffer externalData : register(b0)
{
    float time;
    float3 range;
}

SamplerState basicSampler;

#include "../Common/Defines.hlsli"
#include "../Common/RGBANoise.hlsli"

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 dimensions;
    {
        uint w, h;
        dustTexture.GetDimensions(w, h);
        dimensions.x = w;
        dimensions.y = h;
    }

    float2 particle = float2(DTid.x, DTid.y);
    float t = time;
    float3 p = float3(particle.x + t, particle.y - t, particle.x + particle.y + t * 0.5f);
    float4 position = float4(rgba_tnoise(p) * range, 1.0f);
    dustTexture[particle] = position;
}
