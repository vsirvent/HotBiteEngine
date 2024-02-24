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

static const uint numSamples = 15;
static const float MOTION_FACTOR = 0.1f;
static const float MAX_VELOCITY_FACTOR = 2.0f;

float4 main(float4 pos: SV_POSITION) : SV_TARGET
{    
    float2 tpos = pos.xy;
    tpos.x /= screenW;
    tpos.y /= screenH;
    float4 finalColor;

    // Get the depth buffer value at this pixel. 
    float4 p1World = positionTexture.Sample(basicSampler, tpos);
    if (length(p1World) > 0) {
        
        float4 p0World = prevPositionTexture.Sample(basicSampler, tpos);
        float4 p0 = mul(p0World, prev_view_proj);
        float4 p1 = mul(p1World, view_proj);
        
        p0 /= p0.w;
        p1 /= p1.w;

        // Use this frame's position and last frame's to compute the pixel velocity.
        float2 velocity = (p1.xy - p0.xy)*MOTION_FACTOR;
#if 0
        float dist = length(tpos - 0.5f)*2.0f;
        velocity *= dist*dist;
#endif
        float maxVelocity = MAX_VELOCITY_FACTOR / min(screenW, screenH); // Max velocity per pixel
        float magnitude = length(velocity); // Calculate magnitude of velocity vector

        // If magnitude exceeds max velocity, scale down the velocity vector
        if (magnitude > maxVelocity)
        {
            float scale = maxVelocity / magnitude;
            velocity *= scale;
        }

        velocity.y *= -1.0f;
        // Get the initial color at this pixel.
        if (length(velocity) > 0.0f) {
            uint w = 0;
            float4 color = { 0.0f, 0.0f, 0.0f, 0.0f };
            float2 start_pos = tpos - velocity * numSamples;
            for (uint i = 1; i < numSamples; ++i) {
                if (tpos.x >= 1.0f || tpos.x <= 0.0f || tpos.y >= 1.0f || tpos.y <= 0.0f) {
                    continue;
                }
                // Sample the color buffer along the velocity vector.
                float4 currentColor = renderTexture.SampleLevel(basicSampler, tpos, 0);
                // Add the current color to our color sum. with a weight of the iteration
                color += (currentColor * (float)(i));
                w += i;
                tpos += velocity;
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
