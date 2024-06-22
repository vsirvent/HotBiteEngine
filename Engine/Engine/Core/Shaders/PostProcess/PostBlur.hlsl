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

cbuffer externalData
{
    int screenW;
    int screenH;
    int horizontal;
}

Texture2D renderTexture;
SamplerState basicSampler;

static const float BlurWeights32[33] =
{
0.0216771440357829, 0.023030174945629842, 0.024372268162712405, 0.0256920167211696, 0.026977641631473703, 0.028217160189400764, 0.02939856710486361, 0.030510024758829798, 0.03154005846550627, 0.032477752297221024, 0.033312940837257485, 0.03403639217321265, 0.034639977537105585, 0.03511682323976621, 0.035461440931519074, 0.035669832738636206, 0.03573956845982569, 0.035669832738636206, 0.035461440931519074, 0.03511682323976621, 0.034639977537105585, 0.03403639217321265, 0.033312940837257485, 0.032477752297221024, 0.03154005846550627, 0.030510024758829798, 0.02939856710486361, 0.028217160189400764, 0.026977641631473703, 0.0256920167211696, 0.024372268162712405, 0.023030174945629842, 0.0216771440357829
};

static const float BlurWeights16[17] =
{
    0.05435598138586304, 0.055971998316144515, 0.057411358131712296, 0.05865815060020956, 0.05969836664294216, 0.060520160177855754, 0.06111407261015653, 0.06147321402395617, 0.06159339622231991, 0.06147321402395617, 0.06111407261015653, 0.060520160177855754, 0.05969836664294216, 0.05865815060020956, 0.057411358131712296, 0.055971998316144515, 0.05435598138586304
};

float4 blur32(Texture2D t, float2 pos) {
    float4 color = float4(0.f, 0.f, 0.f, 1.f);
    if (horizontal) {
        for (int dx = -16; dx <= 16; ++dx) {
            color += t.Load(float3(pos.x + dx, pos.y, 0)) * BlurWeights32[dx + 16];
        }
    }
    else {
        for (int dy = -16; dy <= 16; ++dy) {
            color += t.Load(float3(pos.x, pos.y + dy, 0)) * BlurWeights32[dy + 16];
        }
    }
    return color;
}

float4 blur16(Texture2D t, float2 pos) {
    float4 color = float4(0.f, 0.f, 0.f, 1.f);
    if (horizontal) {
        for (int dx = -8; dx <= 8; ++dx) {
            color += t.Load(float3(pos.x + dx, pos.y, 0)) * BlurWeights16[dx + 8];
        }
    }
    else {
        for (int dy = -8; dy <= 8; ++dy) {
            color += t.Load(float3(pos.x, pos.y + dy, 0)) * BlurWeights16[dy + 8];
        }
    }
    return color;
}

float4 main(float4 pos: SV_POSITION) : SV_TARGET
{
    //return blur32(renderTexture, pos.xy);
    return blur16(renderTexture, pos.xy);
}
