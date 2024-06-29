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
cbuffer externalData
{
    int screenW;
    int screenH;
    matrix view_proj;
    matrix prev_view_proj;
};

Texture2D renderTexture;
Texture2D positionTexture;
Texture2D prevPositionTexture;
SamplerState basicSampler;

static const uint MAX_STEPS = 50;

float4 main(float4 pos: SV_POSITION) : SV_TARGET
{    
    float2 tpos = pos.xy;
    float2 dimension = float2(screenW, screenH);
    tpos /= dimension;

    float4 inputColor = renderTexture[pos.xy];
    float4 output_color = inputColor;

    // Get the depth buffer value at this pixel. 
    float4 p1World = positionTexture.Sample(basicSampler, tpos);
    if (length(p1World) > 0) {

        float4 p0World = prevPositionTexture.Sample(basicSampler, tpos);
        float4 p0 = mul(p0World, prev_view_proj);
        float4 p1 = mul(p1World, view_proj);

        p0 /= abs(p0.w);
        p1 /= abs(p1.w);

        p0.y *= -1.0f;
        p1.y *= -1.0f;

        float2 dir = p1.xy - p0.xy;
        dir *= dimension * 0.5f;

        float fsteps = length(dir);


        float step_size = length(dir / fsteps);

        float n = 1.0f;
        float t = 1.0f;
        int i = 0;
        int real_steps = 0;

        while (n < fsteps && n < MAX_STEPS) {
            float a = (fsteps - n) / fsteps;
            t += a;
            n += step_size;
            ++real_steps;
        }

        n = step_size;
        dir /= fsteps;
        float ratio = (float)fsteps / real_steps;
        float4 inputColor = renderTexture[pos.xy];
        output_color = output_color / t;
        float2 p = pos - dir * ratio * real_steps / 5.0f;
        for (i = 0; i < real_steps ; ++i) {
            p += dir * ratio;
            float a = (fsteps - n) / (fsteps * t);
            output_color += renderTexture[p] * a;
            n += step_size;
        }
    }
    return output_color;
}
