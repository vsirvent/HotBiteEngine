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
    float3 cameraDirection;
}

Texture2D<float4> ray0: register(t0);
Texture2D<float4> ray1: register(t1);
RWStructuredBuffer<InputRays> ray_inputs: register(u0);

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
    float ray_input_pixel = y * ray_input_dimension.x + x;
    float2 ray_pixel = round(pixel * divider);

    uint ray_input_pos = 0;
    for (i = 0; i < MAX_RAY_INPUTS; ++i) {
        if (ray_inputs[ray_input_pixel].dir[i].x >= 10e10) {
            ray_input_pos = i;
            break;
        }
    }

    if (i == MAX_RAY_INPUTS) {
        return;
    }

    RaySource ray_source = fromColor(ray0[ray_pixel], ray1[ray_pixel]);
    [branch]
    if (ray_source.reflex <= Epsilon || dist2(ray_source.normal) <= Epsilon || ray_source.dispersion < 0.0f)
    {
        return;
    }

    float3 orig_pos = ray_source.orig.xyz;
    bool process = length(orig_pos - cameraPosition) < max_distance;

    Ray ray = GetReflectedRayFromSource(ray_source, cameraPosition);
    [branch]
    if (dist2(ray.dir) > Epsilon)
    {
        float3 normal;
        float3 tangent;
        float3 bitangent;
        int count = 0;

        float cumulativePoints = 0;
        float level = 1;
        while (true) {
            float c = cumulativePoints + level * 2;
            if (c < N) {
                cumulativePoints = c;
            }
            else {
                break;
            }
            level++;
        };

        [branch]
        if (DTid.z == 0) {
            float3 seed = orig_pos * 100.0f;
            float rX = rgba_tnoise(seed);
            rX = pow(rX, 5.0f);
            normal = ray.dir;
            GetSpaceVectors(normal, tangent, bitangent);
            float2 polar_dir = GenerateHemisphereDispersedRay(normal, tangent, bitangent, ray_source.dispersion, N, level, rX);
            ray_inputs[ray_input_pixel].dir[ray_input_pos++] = polar_dir;
        }
        else {
            //Refracted ray
            if (ray_source.opacity < 1.0f && (enabled & REFRACT_ENABLED)) {
                float3 seed = orig_pos * 100.0f;
                float rX = rgba_tnoise(seed);
                Ray ray = GetRefractedRayFromSource(ray_source, cameraPosition);
                if (dist2(ray.dir) > Epsilon)
                {
                    normal = ray.dir;
                    GetSpaceVectors(normal, tangent, bitangent);
                    float2 polar_dir = GetPolarCoordinates(ray.dir, normal, tangent, bitangent);
                    ray_inputs[ray_input_pixel].dir[ray_input_pos++] = polar_dir;
                }
            }
        }
    }
}