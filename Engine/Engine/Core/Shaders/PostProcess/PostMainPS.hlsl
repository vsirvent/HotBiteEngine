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
#include "../Common/QuickNoise.hlsli"

Texture2D renderTexture : register(t0);
Texture2D depthTexture: register(t3);
Texture2D lightTexture: register(t4);
Texture2D bloomTexture: register(t5);
Texture2D rtTexture0 : register(t6);
Texture2D rtTexture1 : register(t7);
Texture2D volLightTexture: register(t8);
Texture2D dustTexture: register(t9);
Texture2D lensFlareTexture: register(t10);
SamplerState basicSampler : register(s0);

cbuffer externalData : register(b0)
{
	int screenW;
	int screenH;
}

float4 main(float4 pos: SV_POSITION) : SV_TARGET
{
    float2 tpos = pos.xy;
    tpos.x /= screenW;
    tpos.y /= screenH;
#if 0
    float4 color = renderTexture.Sample(basicSampler, tpos);
    float4 l = lightTexture.Sample(basicSampler, tpos);
    float4 b = bloomTexture.Sample(basicSampler, tpos);    

    float4 rt0 = rtTexture0.Sample(basicSampler, tpos);
    float4 rt1 = rtTexture1.Sample(basicSampler, tpos);
    float4 vol = volLightTexture.Sample(basicSampler, tpos);
    float4 dust = dustTexture.Sample(basicSampler, tpos);
    float4 lens_flare = lensFlareTexture.Sample(basicSampler, tpos);
    color += (color + 2.0f * l) * rt0 + rt1 + b + dust + lens_flare + vol;
#endif
    return renderTexture.Sample(basicSampler, tpos);
}
