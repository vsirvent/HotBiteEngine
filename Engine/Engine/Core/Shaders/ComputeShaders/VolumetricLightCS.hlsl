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

Texture2D<float4> worldTexture: register(t0);
RWTexture2D<float4> output : register(u0);
RWTexture2D<uint> vol_data: register(u2);

cbuffer externalData : register(b0)
{
    int screenW;
    int screenH;
    float time;
    AmbientLight ambientLight;
    DirLight dirLights[MAX_LIGHTS];
    PointLight pointLights[MAX_LIGHTS];
    float3 cameraPosition;
    float3 cameraDirection;
    float cloud_density;
    matrix view_inverse;
    matrix projection_inverse;
    float4 LightPerspectiveValues[MAX_LIGHTS / 2];
    matrix DirPerspectiveMatrix[MAX_LIGHTS];
    matrix DirStaticPerspectiveMatrix[MAX_LIGHTS];

    int dirLightsCount;
    int pointLightsCount;

    uint multi_texture_count;
    float multi_parallax_scale;

    uint4 packed_multi_texture_operations[MAX_MULTI_TEXTURE / 4];
    float4 packed_multi_texture_values[MAX_MULTI_TEXTURE / 4];
    float4 packed_multi_texture_uv_scales[MAX_MULTI_TEXTURE / 4];

    int disable_vol;
}

#include "../Common/PixelFunctions.hlsli"

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 input_dimensions;
    uint2 output_dimensions;
    {
        uint w, h;
        worldTexture.GetDimensions(w, h);
        input_dimensions.x = w;
        input_dimensions.y = h;
        output.GetDimensions(w, h);
        output_dimensions.x = w;
        output_dimensions.y = h;
    }

    uint2 input_ratio = input_dimensions / output_dimensions;

    float2 output_pixel = float2(DTid.x, DTid.y);
    float2 input_pixel = output_pixel * input_ratio;
    float4 lightColor = { 0.0f, 0.0f, 0.0f, 1.0f };
    float4 wpos = worldTexture[input_pixel];
    if (length(wpos) <= Epsilon) {
        float2 tpos = input_pixel.xy;
        tpos.x /= screenW;
        tpos.y /= screenH;

        float4 H = float4(tpos.x * 2.0f - 1.0f, (1.0f - tpos.y) * 2.0f - 1.0f, 0.0f, 1.0f);
        float4 D = mul(H, projection_inverse);
        float4 eyepos = D / D.w;
        float3 worldPos = mul(eyepos, view_inverse).xyz;
        float3 dir = normalize(worldPos - cameraPosition);
        wpos.xyz = cameraPosition + dir * 100.0f;
        wpos.w = 1.0f;
    }
    int i = 0;
    // Calculate the directional light
    for (i = 0; i < dirLightsCount; ++i) {
        if (dirLights[i].density > 0.0f && disable_vol == 0) {
            lightColor.rgb += DirVolumetricLight(wpos, dirLights[i], i, time, cloud_density);
        }
    }

    // Calculate the point lights
    for (i = 0; i < pointLightsCount; ++i) {
        if (pointLights[i].density > 0.0f && disable_vol == 0) {
            lightColor.rgb += VolumetricLight(wpos.xyz, pointLights[i], i);
        }
    }
    float illumitation = length(lightColor) * 1000.0f;
    InterlockedAdd(vol_data[uint2(0, 0)], (uint)(illumitation));
    output[output_pixel] = lightColor;
}
