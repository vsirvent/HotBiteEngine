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
    
    matrix view;
    matrix projection;
    
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
}

#include "../Common/PixelFunctions.hlsli"

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 input_pixel = float2(DTid.x, DTid.y);
    float4 lightColor = { 0.0f, 0.0f, 0.0f, 1.0f };

    float4 wpos = dustTexture[input_pixel];

    //Convert to view space
    matrix worldViewProj = mul(view, projection);

    float4 viewPos = mul(wpos, worldViewProj);
    viewPos /= viewPos.w;
    
    //if we are in front of camera
    if (viewPos.z > 0.0f && abs(viewPos.x) < 0.5f && abs(viewPos.y) < 0.5f)
    {
        viewPos.x /= viewPos.z;
        viewPos.y /= -viewPos.z;
        viewPos.x = (viewPos.x + 1.0f) * screenW / 2.0f;
        viewPos.y = (viewPos.y + 1.0f) * screenH / 2.0f;
        
        float4 color = float4(1.0f, 1.0f, 1.0f, 1.0f);

        float illumitation = length(color) * 900.0f;
        //InterlockedAdd(vol_data[uint2(0, 0)], (uint) (illumitation));
        output[viewPos.xy] += color;
    }
}
