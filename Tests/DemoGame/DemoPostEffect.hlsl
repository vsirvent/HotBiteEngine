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

#include "DemoCommons.hlsli"

/** 
 * Every post - process shader can access a set of predefined variables :
   Texture2D renderTexture : register(t0);
   Texture2D depthTexture: register(t3);
   Texture2D lightTexture: register(t4);
   SamplerState basicSampler : register(s0);

    cbuffer externalData : register(b0)
    {
        int screenW;
        int screenH;
        int time;
    }
 
    (you can add extra variables by using PostProcess:SetVariable)
*/

cbuffer externalData : register(b0)
{
    int screenW;
    int screenH;
    float damageTime;
    float deadTime;
    float time;
}

Texture2D renderTexture : register(t0);
SamplerState basicSampler : register(s0);

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
}

float4 main(float4 pos: SV_POSITION) : SV_TARGET
{
    //This post-process shows a damage effect when the player receives damage,
    //also goes to grayscale when the player is dead.
    float2 tpos = pos.xy;
    tpos.x /= screenW;
    tpos.y /= screenH;
    float4 color = renderTexture.Sample(basicSampler, tpos);
    //Damage fades out 2 seconds
    float damageFactor = saturate((2.0f - (time - damageTime) / 2.0f) / 2.0f);
    //Dead gray fades in 5 seconds
    float deadFactor = 0.0f; 
    if (deadTime > 0.0f) {
        deadFactor = saturate(time - deadTime);
    }
    if (damageFactor > 0.0f) {
        float4 damageColor = color;
        float vignette = 1.0f - length(tpos - float2(0.5f, 0.5f))*1.5f;
        float a = sin(time*4.0f) * 0.2 + 0.4f;
        damageColor.rgb *= 1.0f * a + vignette * (1.0f - a);
        damageColor.rgb *= lerp(float3(1.0f, 1.0f, 1.0f), float3(1.0f, 0.0f, 0.0f), 1.0f - vignette);
        color = lerp(color, damageColor, damageFactor);
    }
    if (deadFactor > 0.0f) {
        //Blur image when dead
		float4 deadColor = blur(renderTexture, pos.xy, 3);
        float l = dot(deadColor.rgb, float3(0.299, 0.587, 0.114))*0.8f;
        float center_degraded = saturate(abs(tpos.y - 0.5f)*3.0f);
		deadColor.rgb = lerp(deadColor.rgb, center_degraded * float3(l, l, l), deadFactor);
		color = lerp(color, deadColor, deadFactor);
	}
	return color;
}