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

//#define TEST

RWTexture2D<float4> output : register(u0);
Texture2D input: register(t0);
Texture2D depthTexture: register(t1);
Texture2D normalTexture: register(t2);

static const float BORDER_DIFF = 1.0f;
static const float NORMAL_DIFF = 0.5f;

float BorderValue(float2 pixel) {
    float2 dp = pixel / 2;
    float z0 = depthTexture[dp].r;
    float3 n0 = normalTexture[pixel].xyz;
    float border = 0.0f;
    //return 0.0f;
    int l = 2;
    for (int x = -l; x < l; ++x) {
        for (int y = -l; y < l; ++y) {
            float2 delta = float2(x, y);
            float z = depthTexture[dp + delta].r;
            float diff = abs(z - z0);
            border = max(border, saturate(diff / BORDER_DIFF - BORDER_DIFF));
            
            float3 n = normalTexture[pixel + delta].xyz;
            float angle = acos(dot(n0, n));
            if (length(n) > 0.0f && angle > NORMAL_DIFF) {
                border = max(border, saturate(angle / NORMAL_DIFF - 1.0f));
            }
        }
    }
    return border;
}

float4 SmoothColor(float2 pixel) {
    float i = 0.0f;
    float w[5] = { 0.1f, 0.25f, 0.3f, 0.25f, 0.1f };
    float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float l = 2;
    for (int x = -l; x <= l; ++x) {
        for (int y = -l; y <= l; ++y) {
            int2 p = (int2)pixel + int2(x, y);
            color += input[p] * w[x + l] * w[y + l];
            i += 1.0f;
        }
    }
#if 0//def TEST
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
#else
    return color;
#endif
}

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint w, h;
    output.GetDimensions(w, h);
    float2 imageRes = { w, h };
   
    float2 pixel = float2(DTid.x, DTid.y);
    
    float4 color = input[pixel];
    float border = BorderValue(pixel);
    if (border > 0.0f) {
        color = color * (1.0f - border) + SmoothColor(pixel) * border;
    }

    output[pixel] = color;
}
