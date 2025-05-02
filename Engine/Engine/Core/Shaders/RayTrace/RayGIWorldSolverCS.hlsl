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

cbuffer objectData : register(b1)
{
    uint nobjects;
    ObjectInfo objectInfos[MAX_OBJECTS];
    MaterialColor objectMaterials[MAX_OBJECTS];
}

RWTexture2D<float4> output : register(u0);
RWTexture2D<uint> tiles_output: register(u2);
RWTexture2D<uint4> restir_pdf_1: register(u3);

Texture2D<uint4> restir_pdf_0: register(t7);
Texture2D<float4> ray0;
Texture2D<float4> ray1;
Texture2D<uint4> ray_inputs: register(t0);

Texture2D<uint> restir_pdf_mask: register(t6);
Texture2D<float> restir_w_0: register(t11);

StructuredBuffer<BVHNode> objects: register(t2);
StructuredBuffer<BVHNode> objectBVH: register(t3);

Texture2D<float> DirShadowMapTexture[MAX_LIGHTS];
TextureCube<float> PointShadowMapTexture[MAX_LIGHTS];

//Packed array
static float2 lps[MAX_LIGHTS] = (float2[MAX_LIGHTS])LightPerspectiveValues;

#include "../Common/SimpleLight.hlsli"
#include "../Common/RayFunctions.hlsli"
#include "RayWorldSolver.hlsli"

#define NTHREADS 11

[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
#if GI_WORLD
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
    float4 color_refrac = float4(0.0f, 0.0f, 0.0f, 1.0f);
    uint2 hits = uint2(0, 0);
    Ray ray = GetRayInfoFromSourceWithNoDir(ray_source);
    float3 orig = ray.orig.xyz;

    //Get rays to be solved in the pixel
    RayTraceColor rc;
    float2 ray_input[4];
    Unpack4Float2FromI16(ray_inputs[pixel], MAX_RAY_POLAR_DIR, ray_input);
    float reflex_ratio = (1.0f - ray_source.dispersion);
    
    float pdf_cache[MAX_RAYS];
    UnpackRays(restir_pdf_1[pixel], RAY_W_SCALE, pdf_cache);
    uint i = 0;

    float wis[MAX_RAYS];
    uint mask = restir_pdf_mask[pixel];
    for (i = 0; i < 4; ++i) {
        uint wi = (mask & 0xF000) >> 12;
        wis[i] = wi;
        mask <<= 4;
    }

    float n = 0.0f;
    float4 final_color = float4(0.0f, 0.0f, 0.0f, 1.0f);
    for (i = 0; i < 4; ++i) {
        uint wi = wis[i];
        if (wi == 0xF) continue;

        Ray ray = GetRayInfoFromSourceWithNoDir(ray_source);
        ray.dir = GetCartesianCoordinates(ray_input[i]);
        ray.orig.xyz += ray.dir * 0.05f;
        if (abs(ray_input[i].x) < MAX_RAY_POLAR_DIR && dist2(ray_input[i]) > Epsilon) {
            ray.orig.xyz += ray.dir * 0.5f;
            if (GetColor(ray, 0, rc, ray_source.dispersion, false)) {
                hits.x = rc.hit != 0;
                if (i < 3) {
                    final_color.rgb += rc.color.rgb;
                    n += pdf_cache[wi];
                }
            }
            pdf_cache[wi] = RAY_W_BIAS + length(rc.color.rgb);
        }
    }
    float w_ratio = 1.0f;
    if (n > Epsilon) {
        final_color = (final_color * ray_source.opacity);
        w_ratio = restir_w_0[pixel] / n;
#if GI_SCREEN
        float4 gi_screen_color = output[pixel];
        final_color = ((final_color * w_ratio) / 3);
        gi_screen_color.rgb += final_color.rgb;
        output[pixel] = gi_screen_color;
#endif
    }
#if !GI_SCREEN
    output[pixel] = ((final_color * w_ratio) / 3);
#endif
    restir_pdf_1[pixel] = PackRays(pdf_cache, RAY_W_SCALE);

    uint hit = (hits.y & 0x01) << 1 | (hits.x & 0x01);
    if (hit != 0) {
        for (int x = -2; x <= 2; ++x) {
            for (int y = -2; y <= 2; ++y) {
                int2 p = pixel / kernel_size + int2(x, y);
                tiles_output[p] = tiles_output[p] | hit;
            }
        }
    }
#endif
}
    