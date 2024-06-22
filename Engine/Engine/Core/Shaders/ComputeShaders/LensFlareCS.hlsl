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
RWTexture2D<float> depthTextureUAV : register(u1);
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
    matrix projection;
    
    float4 LightPerspectiveValues[MAX_LIGHTS / 2];
    uint pointLightsCount;

    matrix DirPerspectiveMatrix[MAX_LIGHTS];
    uint dirLightsCount;
}

Texture2D<float> DirShadowMapTexture[MAX_LIGHTS];
TextureCube<float> PointShadowMapTexture[MAX_LIGHTS];

//Packed array
static float2 lps[MAX_LIGHTS] = (float2[MAX_LIGHTS])LightPerspectiveValues;

#include "../Common/SimpleLight.hlsli"
#include "../Common/RGBANoise.hlsli"

float4 EmitPoint(float2 pixel, float2 light_screen_pos, float2 dimenstion, PointLight light, float focus)
{
    float2 ToLight = light_screen_pos - pixel;
    float DistToLight = length(ToLight);
    float DistToCamera = length(light.Position - cameraPosition);

    //Light emission
    float DistLightToPixel = 1.0 - saturate(DistToLight * 0.01f);
    float DistLightToPixel2 = 1.0 - saturate(DistToLight * 0.02f);

    float2 delta = pixel - light_screen_pos;
    float angle = atan2(delta.x, delta.y);
    float angle_rot = angle + dot(light.Position, cameraPosition) * 0.0001f;;
    float angle1 = angle_rot * 12.0f + 1.55;
    float angle2 = angle_rot * 6.0f - 1.55;
    float angle3 = angle * 2.0f - 1.55;

    float3 color = 30.0f * light.Color * pow(DistLightToPixel, 100.0f);
    float3 color1 = color * 0.5f;
    float3 color2 = light.Color;

    float3 flare1 = saturate(color1 * sin(angle1));
    float3 flare2 = saturate(color * sin(angle2));
    float3 flare3 = saturate(color2 * pow(abs(sin(angle3)), 20.0f) * 1.0f);

    float dist_att = (dimenstion.x * 0.02f) / DistToLight;
    float3 att_dist_by_color = float3(dist_att, dist_att, dist_att * 1.5f);
    att_dist_by_color = saturate(att_dist_by_color);

    float att_height = saturate(dimenstion.y * 0.002f / abs(light_screen_pos.y - pixel.y));
    float3 att_height_by_color = float3(att_height, att_height, att_height * 1.2f);
    att_height_by_color = att_height_by_color;

    color += (flare1 + flare2) * att_dist_by_color * light.Range * 0.1f * focus + flare3 * att_dist_by_color * att_height_by_color * light.Range * 0.01f * saturate(focus + 0.2f);
    color += pow(DistLightToPixel2, 10.0f);
    return float4(color, 1.0f);
}

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 dimensions;
    float2 depth_dimensions;
    {
        uint w, h;
        output.GetDimensions(w, h);
        dimensions.x = w;
        dimensions.y = h;

        depthTextureUAV.GetDimensions(w, h);
        depth_dimensions.x = w;
        depth_dimensions.y = h;
    }

    float2 depth_ratio = depth_dimensions / dimensions;

    float2 pixel = float2(DTid.x, DTid.y);
    float2 depth_pixel = float2(pixel.x * depth_ratio.x, pixel.y * depth_ratio.y);

    //Calculate the lens flare effect for each available point light
    for (uint i = 0; i < pointLightsCount; ++i) {
        //if we are in front of camera and we are not behind an object
        float4 light_view_pos = mul(float4(pointLights[i].Position, 1.0f), view);
        light_view_pos /= abs(light_view_pos.w);

        if (light_view_pos.z > 0.0f) {
            
            float2 light_screen_pos;
            float4 lposition = mul(light_view_pos, projection);
            lposition /= abs(lposition.w);

            light_screen_pos.x = (lposition.x * 0.5f + 0.5f) * dimensions.x;
            light_screen_pos.y = (1.0f - (lposition.y * 0.5f + 0.5f)) * dimensions.y; // Invert y-axis for screen coordinates

            float dist_to_cam = length(pointLights[i].Position - cameraPosition);
            float visibility = 0.0f;
            float count = 0.0f;
            float z0 = 0.0f;
            for (int x = -2; x <= 2; ++x) {
                for (int y = -2; y <= 2; ++y) {
                    float depth = depthTextureUAV[(light_screen_pos + float2(x, y))* depth_ratio];
                    z0 += depth;
                    count += 1.0f;
                    if (depth > dist_to_cam) {
                        visibility += 1.0f;
                    }
                }
            }
            
            visibility /= count;
            z0 /= count;

            if (visibility > 0.0f &&
                light_screen_pos.x > 0.0 && light_screen_pos.x < dimensions.x &&
                light_screen_pos.y > 0.0 && light_screen_pos.y < dimensions.y) {
                
                float focus = 1.0f;
                if (focusZ > 0.0f) {
                    focus = 1.0f - saturate(pow((focusZ - z0), 2.0f) * amplitude * 0.001f) + 0.1f;
                }

                float4 color = EmitPoint(pixel, light_screen_pos, dimensions, pointLights[i], focus);

                output[pixel] += color * visibility;
            }
        }
    };
}
