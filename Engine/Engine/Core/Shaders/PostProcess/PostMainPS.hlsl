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

#include "../Common/Utils.hlsli"

Texture2D renderTexture : register(t0);
Texture2D depthTexture: register(t3);
Texture2D lightTexture: register(t4);
Texture2D rtTexture : register(t5);
SamplerState basicSampler : register(s0);

cbuffer externalData : register(b0)
{
	int screenW;
	int screenH;
    int dopActive;
    float nearFactor;
    float farFactor;
    matrix projection_inverse;
}

static const float BlurWeights[13] =
{
    0.002216,
    0.008764,
    0.026995,
    0.064759,
    0.120985,
    0.176033,
    0.199471,
    0.176033,
    0.120985,
    0.064759,
    0.026995,
    0.008764,
    0.002216,
};

float4 blur(Texture2D t, float2 pos, int level) {
#if 1
    float2 tpos = pos;
    tpos.x /= screenW;
    tpos.y /= screenH;
    return t.SampleLevel(basicSampler, tpos, level);
#else
    float4 color = float4(0.f, 0.f, 0.f, 1.f);
    float2 tpos;
    for (int dx = -6; dx <= 6; ++dx) {
        for (int dy = -6; dy <= 6; ++dy) {
            tpos.x = pos.x + dx;
            tpos.y = pos.y + dy;
            tpos.x /= screenW;
            tpos.y /= screenH;
            tpos = saturate(tpos);
            color += t.SampleLevel(basicSampler, tpos, level) * BlurWeights[dx + 6] * BlurWeights[dy + 6];
        }
    }
    return saturate(color);
#endif
}

float4 main(float4 pos: SV_POSITION) : SV_TARGET
{
    float2 tpos = pos.xy;
    tpos.x /= screenW;
    tpos.y /= screenH;
    float4 color = getSmoothPixel(basicSampler, renderTexture, tpos, screenW, screenH);
    //float4 color = renderTexture.Sample(basicSampler, tpos);
    if (dopActive) {
        float z0 = depthTexture.Sample(basicSampler, tpos).r;
        // H is the viewport position at this pixel in the range -1 to 1.
        float4 H = float4(tpos.x * 2.0f - 1.0f, (1.0f - tpos.y) * 2.0f - 1.0f, z0, 1.0f);
        // Transform by the projection inverse.
        float4 D = mul(H, projection_inverse);
        // Divide by w to get the world position.
        float4 worldPos = D / D.w;
        float z = worldPos.z;
        float far = 0.0f; 
        if (farFactor > 0.0f) {
            far = (z - farFactor) / farFactor;
        }
        float near = 0.0f;
        if (nearFactor > 0.0f) {
            near = (nearFactor - z) / nearFactor;
        }
        float depth = max(far, near);

        if (depth > 0.0f) {
            int d = (int)(depth*3.9); //number of mip levels
            float4 color2 = blur(renderTexture, pos.xy, d);
            float w = saturate(depth);
            color = lerp(color, color2, w);
        }
    }
    //Blur volumetric light 
    float4 l = lightTexture.Sample(basicSampler, tpos);
    float a = length(l.rgb);
    /*
    if (length(color) < 1.0f) {
        float gamma = 1.1f;
        float saturation = 1.2f;

        // convert from RGB to HSL
        float3 hsl = RGBtoHSL(saturate(color.rgb));
        // increase the saturation
        hsl.y *= saturation;
        // convert back from HSL to RGB
        color.rgb = saturate(HSLtoRGB(hsl));
        color.rgb = pow(abs(color.rgb), 1.0 / gamma);
    }
    */
    float4 rt = rtTexture.Sample(basicSampler, tpos);
    color = color*0.5f + 0.5*rt;
    //color += l;// (color* (1.0f - a) + l);
    return color;
}
