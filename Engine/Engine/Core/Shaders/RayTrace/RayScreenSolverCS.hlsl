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
#include "../Common/ShaderStructs.hlsli"
#include "../Common/PixelCommon.hlsli"
#include "../Common/RayDefines.hlsli"
#include "../Common/RGBANoise.hlsli"

cbuffer externalData : register(b0)
{
    uint frame_count;
    float time;
    uint enabled;
    int kernel_size;
    float3 cameraPosition;

    //Lights
    AmbientLight ambientLight;
    DirLight dirLights[MAX_LIGHTS];
    PointLight pointLights[MAX_LIGHTS];
    uint dirLightsCount;
    uint pointLightsCount;

    float4 LightPerspectiveValues[MAX_LIGHTS / 2];
    matrix DirPerspectiveMatrix[MAX_LIGHTS];
}

Texture2D<float> DirShadowMapTexture[MAX_LIGHTS];
TextureCube<float> PointShadowMapTexture[MAX_LIGHTS];

//Packed array
static float2 lps[MAX_LIGHTS] = (float2[MAX_LIGHTS])LightPerspectiveValues;

#include "../Common/SimpleLight.hlsli"
#include "../Common/RayFunctions.hlsli"

RWTexture2D<float4> ray_inputs: register(u0);
RWTexture2D<float4> output : register(u1);
RWTexture2D<uint> tiles_output: register(u2);

Texture2D<float4> ray0;
Texture2D<float4> ray1;
Texture2D<float4> hiz_textures[4];

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    float2 dimensions;
    float2 ray_map_dimensions;
    {
        uint w, h;
        output.GetDimensions(w, h);
        dimensions.x = w;
        dimensions.y = h;

        ray0.GetDimensions(w, h);
        ray_map_dimensions.x = w;
        ray_map_dimensions.y = h;
    }
    float x = (float)DTid.x;
    float y = (float)DTid.y;

    float2 rayMapRatio = ray_map_dimensions / dimensions;

    float2 pixel = float2(x, y);


    float2 ray_pixel = round(pixel * rayMapRatio);

    RaySource ray_source = fromColor(ray0[ray_pixel], ray1[ray_pixel]);

    float4 color_reflex = float4(0.0f, 0.0f, 0.0f, 1.0f);
    uint hit = 0;
    Ray ray = GetRayInfoFromSourceWithNoDir(ray_source);
    float3 orig = ray.orig;

    //Get rays to be solved in the pixel
    float4 ray_input_dirs = ray_inputs[pixel];
    [branch]
    if (ray_input_dirs.x < 10e10) {
        if (dist2(ray_input_dirs.xy) <= Epsilon) {
            color_reflex.rgb = float3(1.0f, 0.0f, 0.0f);
        }
        else {
            ray.dir = GetCartesianCoordinates(ray_input_dirs.xy);
            ray.orig.xyz = orig + ray.dir * 0.01f;
            
            float reflex_ratio = (1.0f - ray_source.dispersion);
            //color_reflex.rgb += rc.color * ray_source.opacity;
            //hit = rc.hit != 0;
            if (hit != 0) {
                ray_input_dirs.xy = float2(10e11, 10e11);
            }
        }
    }

    if (hit != 0) {
        [unroll]
        for (int x = -2; x <= 2; ++x) {
            [unroll]
            for (int y = -2; y <= 2; ++y) {
                int2 p = pixel / kernel_size + int2(x, y);
                tiles_output[p] = hit;
            }
        }
        output[pixel] = color_reflex;
        ray_inputs[pixel] = ray_input_dirs;
    }
}
    