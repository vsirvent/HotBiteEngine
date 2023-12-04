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
    matrix view_inverse;
    matrix prev_view;
};

Texture2D renderTexture;
Texture2D<float> depthTexture;

SamplerState basicSampler;

static const uint numSamples = 10;
float4 main(float4 pos: SV_POSITION) : SV_TARGET
{    
    float2 tpos = pos.xy;
    tpos.x /= screenW;
    tpos.y /= screenH;
    float4 finalColor;

    // Get the depth buffer value at this pixel. 
    float zOverW = depthTexture.Sample(basicSampler, tpos);
    if (zOverW != FLT_MAX) {
        // H is the viewport position at this pixel in the range -1 to 1.
        float4 H = float4(tpos.x * 2.0f - 1.0f, (1.0f - tpos.y) * 2.0f - 1.0f, zOverW, 1.0f);
        // Transform by the view-projection inverse.
        float4 D = mul(H, view_inverse);
        // Divide by w to get the world position.
        float4 worldPos = D / D.w;

        // Current viewport position
        float4 currentPos = H;
        // Use the world position, and transform by the previous view projection matrix.
        float4 previousPos = mul(worldPos, prev_view);
        // Convert to nonhomogeneous points [-1,1] by dividing by w. 
        previousPos /= previousPos.w;
        // Use this frame's position and last frame's to compute the pixel velocity.
        float2 velocity = (currentPos - previousPos).xy;

        velocity.x /= screenW;
        velocity.y /= screenH;

        // Get the initial color at this pixel.
        if (length(velocity) > 0.0f) {
            uint w = 0;
            float4 color = { 0.0f, 0.0f, 0.0f, 0.0f };
            float2 start_pos = tpos;// +(velocity * numSamples / 4);
            tpos = saturate(start_pos);
            for (uint i = numSamples; i > 0; --i) {
                // Sample the color buffer along the velocity vector.
                float4 currentColor = renderTexture.SampleLevel(basicSampler, tpos, 0);
                // Add the current color to our color sum. with a weight of the iteration
                color += (currentColor * (float)(i));
                w += (i);
                tpos -= velocity;
                if (tpos.x >= 1.0f || tpos.x <= 0.0f || tpos.y >= 1.0f || tpos.y <= 0.0f) {
                    break;
                }
            }
            finalColor = (color / (float)w);
        }
        else {
            finalColor = renderTexture.Sample(basicSampler, tpos);
        }
    }
    else {
        finalColor = renderTexture.Sample(basicSampler, tpos);
    }
    return finalColor;
}
