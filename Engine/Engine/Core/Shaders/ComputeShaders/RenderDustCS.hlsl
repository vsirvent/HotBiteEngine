
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
#include "../Common/ShaderStructs.hlsli"
#include "../Common/Utils.hlsli"
#include "../Common/PixelCommon.hlsli"

RWTexture2D<float4> output : register(u0);
RWTexture2D<float4> dustTexture : register(u1);
RWTexture2D<float> depthTextureUAV : register(u2);
Texture2D<uint> vol_data: register(t0);

cbuffer externalData : register(b0)
{
    int screenW;
    int screenH;
    float time;
    float focusZ;
    float amplitude;
    AmbientLight ambientLight;
    DirLight dirLights[MAX_LIGHTS];
    PointLight pointLights[MAX_LIGHTS];
    float3 cameraPosition;
    float3 cameraDirection;

    matrix view;
    matrix inverse_view;
    matrix projection;

    float4 LightPerspectiveValues[MAX_LIGHTS / 2];
    matrix DirPerspectiveMatrix[MAX_LIGHTS];

    uint dirLightsCount;
    uint pointLightsCount;

}

Texture2D<float> DirShadowMapTexture[MAX_LIGHTS];
TextureCube<float> PointShadowMapTexture[MAX_LIGHTS];

//Packed array
static float2 lps[MAX_LIGHTS] = (float2[MAX_LIGHTS])LightPerspectiveValues;

#include "../Common/SimpleLight.hlsli"
#include "../Common/RGBANoise.hlsli"

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 dimensions;
    {
        uint w, h;
        output.GetDimensions(w, h);
        dimensions.x = w;
        dimensions.y = h;
    }

    float2 input_pixel = float2(DTid.x, DTid.y);

    float4 wpos = dustTexture[input_pixel];

    float t = time * 0.1f;
    float3 p = float3(input_pixel.x + t, input_pixel.y + t, 1.0f);
    float noise = saturate(rgba_tnoise(p)) * 0.2f;
    wpos.w = wpos.w * 0.8f + noise;
    dustTexture[input_pixel] = wpos;
    float illumination = wpos.w;
    wpos.w = 1.0f;

    //Convert to view space
    float4 viewPos = mul(wpos, view);  // Transform to view space
    float4 viewPos2 = viewPos;  // Transform to view space the particle size
    viewPos2.x += 0.02f;

    float4 projPos = mul(viewPos, projection); // Transform to clip space
    projPos /= abs(projPos.w);

    float4 projPos2 = mul(viewPos2, projection);
    projPos2 /= abs(projPos2.w);



    //if we are in front of camera
    if (projPos.z >= 0.0f)
    {
        if (projPos.x < -1.1f || projPos.x > 1.1f) {
            //loop particle to the other side of the camera
            viewPos.x = -viewPos.x;
            wpos = mul(viewPos, inverse_view);
            wpos /= abs(wpos.w);
            wpos.w = dustTexture[input_pixel].w;
            dustTexture[input_pixel] = wpos;
        }
        if (abs(projPos.x) <= 1.0f && abs(projPos.y) <= 1.0f) {
            float2 screenPos;
            screenPos.x = (projPos.x * 0.5f + 0.5f) * dimensions.x;
            screenPos.y = (1.0f - (projPos.y * 0.5f + 0.5f)) * dimensions.y; // Invert y-axis for screen coordinates

            float2 screenPos2;
            screenPos2.x = (projPos2.x * 0.5f + 0.5f) * dimensions.x;
            screenPos2.y = (1.0f - (projPos2.y * 0.5f + 0.5f)) * dimensions.y;
            if (screenPos.x >= 0 && screenPos.x < dimensions.x && screenPos.y >= 0 && screenPos.y < dimensions.y)
            {
                float depth = depthTextureUAV[screenPos / 2];
                float dist_to_cam = length(wpos.xyz - cameraPosition);
                if (depth > dist_to_cam)
                {

                    uint i = 0;
                    float3 normal = { 0.0f, 1.0f, 0.0f };
                    float3 color = float3(0.0f, 0.0f, 0.0f);

                    // Calculate the directional lights
                    [unroll]
                    for (i = 0; i < dirLightsCount; ++i) {
                        color += CalcDirectional(normal, wpos.xyz, dirLights[i], i);
                    }

                    // Calculate the point lights
                    [unroll]
                    for (i = 0; i < pointLightsCount; ++i) {
                        if (length(wpos.xyz - pointLights[i].Position) < pointLights[i].Range) {
                            color += CalcPoint(normal, wpos.xyz, pointLights[i], i);
                        }
                    }
#define MAX_GLOBAL_ILLUMINATION 0.4f
                    float global = (float)vol_data[uint2(0, 0)] / (float)(dimensions.x * dimensions.y * 1000);
                    float att = 1.0f;
                    global *= global;
                    global *= global;
                    att = -global + (1.0f + MAX_GLOBAL_ILLUMINATION);

                    float2 distanceScreen = abs(screenPos - screenPos2);
                    float halfDistance = floor(distanceScreen.x);
#if 1
                    depthTextureUAV[screenPos / 2] = max(depth,dist_to_cam);
                    for (int x = -halfDistance; x <= halfDistance; ++x) {
                        for (int y = -halfDistance; y <= halfDistance; ++y) {
                            float dist_att = saturate(distanceScreen.x);
                            float w0 = saturate(1.0f - abs(x) / halfDistance);
                            float w1 = saturate(1.0f - abs(y) / halfDistance);
                            float total_att =  att * illumination * w0 * w1;

                            //Only write if we have a visible particle and attenuation is not too high that it will not be visible
                            if (total_att > 0.0001f)
                            {
                                float2 output_pixel = screenPos + float2(x, y);
                                output[output_pixel] = float4(color * total_att, 1.0f);
                            }
                        }
                    }
#else
                    float dist_att = saturate(distanceScreen.x);
                    float total_att = att * illumination * focus_gain;

                    //Only write if we have a visible particle and attenuation is not too high that it will not be visible
                    if (total_att > 0.0001f)
                    {
                        float2 output_pixel = screenPos;
                        output[output_pixel] = float4(color * total_att, 1.0f);
                    }
#endif
                }
            }
        }
    }
}
