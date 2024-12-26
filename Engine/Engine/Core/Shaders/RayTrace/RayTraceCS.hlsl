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

#define REFLEX_ENABLED 1
#define REFRACT_ENABLED 2
#define USE_OBH 0

cbuffer externalData : register(b0)
{
    uint frame_count;
    uint enabled;
    uint divider;
    float time;
    float3 cameraPosition;
}

Texture2D<float4> ray0: register(t0);
Texture2D<float4> ray1: register(t1);
RWTexture2D<float4> ray_inputs: register(u0);

#include "../Common/Utils.hlsli"
#include "../Common/RayFunctions.hlsli"

static float NCOUNT = 32.0f;
static uint N2 = 32;
static float N = N2 * NCOUNT;
static float max_distance = 50.0f;
#define NTHREADS 32

[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    float2 ray_input_dimension;
    float2 ray_map_dimensions;
    {
        uint w, h;
        ray0.GetDimensions(w, h);
        ray_map_dimensions.x = w;
        ray_map_dimensions.y = h;
        ray_input_dimension.x = w / divider;
        ray_input_dimension.y = h / divider;
    }
    int i = 0;
    float x = (float)DTid.x;
    float y = (float)DTid.y;
    float2 pixel = float2(x, y);
    float2 ray_pixel = round(pixel * divider);
       
    RaySource ray_source = fromColor(ray0[ray_pixel], ray1[ray_pixel]);
    
    float3 orig_pos = ray_source.orig.xyz;
    bool process = length(orig_pos - cameraPosition) < max_distance;

    float3 normal;
    float3 tangent;
    float3 bitangent;
    int count = 0;

    float cumulativePoints = 0;
    float level = NCOUNT;

    float4 dirs = float4(10e11, 10e11, 10e11, 10e11);
    //Reflected ray
    if (ray_source.opacity > Epsilon && (enabled & REFLEX_ENABLED)) {
        Ray ray = GetReflectedRayFromSource(ray_source, cameraPosition);
        if (dist2(ray.dir) > Epsilon)
        {
            float3 seed = DTid;
            float rX = rgba_tnoise(seed) * N;
            rX = pow(rX, 2.0f);
            normal = ray.dir;
            GetSpaceVectors(normal, tangent, bitangent);
            dirs.xy = GetPolarCoordinates(ray.dir);
            //dirs.xy = GenerateHemisphereDispersedRay(normal, tangent, bitangent, ray_source.dispersion, N, level * 5.0f, rX);
        }
    }
    //Refracted ray
    if (ray_source.opacity < 0.99f && (enabled & REFRACT_ENABLED)) {
        Ray ray = GetRefractedRayFromSource(ray_source, cameraPosition);
        if (dist2(ray.dir) > Epsilon)
        {
            dirs.zw = GetPolarCoordinates(ray.dir);
        }
    }
    ray_inputs[pixel] = dirs;
}