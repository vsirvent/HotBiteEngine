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
#include "RayScreenSolver.hlsli"

cbuffer externalData : register(b0)
{
    uint frame_count;
    float time;
    uint type;
    float hiz_ratio;
    uint divider;
    int kernel_size;
    float3 cameraPosition;
    matrix view;
    matrix projection;
    matrix inv_projection;
}

#include "../Common/RayFunctions.hlsli"

RWTexture2D<uint4> ray_inputs: register(u0);
RWTexture2D<float4> output : register(u1);
RWTexture2D<uint> tiles_output: register(u2);
RWTexture2D<uint4> restir_pdf_1: register(u3);

Texture2D<float4> ray0: register(t1);
Texture2D<float4> ray1: register(t2);
Texture2D colorTexture: register(t3);
Texture2D lightTexture: register(t4);
Texture2D bloomTexture: register(t5);
Texture2D<uint> restir_pdf_mask: register(t6);
Texture2D<uint4> restir_pdf_0: register(t7);

Texture2D<float> hiz_textures[HIZ_TEXTURES];

#define NTHREADS 11
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

    float2 rayMapRatio = divider;

    float2 pixel = float2(x, y);

    float2 ray_pixel = round(pixel * rayMapRatio);

    RaySource ray_source = fromColor(ray0[ray_pixel], ray1[ray_pixel]);

    float2 color_uv = float2(0.0f, 0.0f);
    float z_diff = FLT_MAX;

    float hit_distance;
    //Get rays to be solved in the pixel
    float2 ray_input[4];
    Unpack4Float2FromI16(ray_inputs[pixel], MAX_RAY_POLAR_DIR, ray_input);

    float3 final_color = float3(0.0f, 0.0f, 0.0f);
    float n = 0.0f;
    float ratio = 0.0f;
    ratio = ray_source.dispersion;
    
#if 1
    uint i = 0;
    float pdf_cache[MAX_RAYS];
    UnpackRays(restir_pdf_0[pixel], RAY_W_SCALE, pdf_cache);

    float wis[MAX_RAYS];
    int wis_size = 0;
    uint mask = restir_pdf_mask[pixel];
    for (i = 0; i < 4; ++i) {
        uint wi = (mask & 0xF000) >> 12;
        wis[wis_size++] = wi;
        mask <<= 4;
    }

    for (i = 0; i < 4; ++i) {
        uint wi = wis[i];
        if (wi == 0xF) continue;

        z_diff = FLT_MAX;
        Ray ray = GetRayInfoFromSourceWithNoDir(ray_source);
        if (abs(ray_input[i].x) < MAX_RAY_POLAR_DIR && dist2(ray_input[i]) > Epsilon) {
            ray.dir = GetCartesianCoordinates(ray_input[i]);
            ray.dir = normalize(mul(ray.dir, (float3x3)view));
            ray.orig = mul(ray.orig, view);
            ray.orig /= ray.orig.w;
            ray.orig.xyz += ray.dir * 0.5f;
            float reflex_ratio = (1.0f - ray_source.dispersion);
            color_uv = GetColor(ray, projection, inv_projection, hiz_textures, hiz_ratio, ray_map_dimensions, dimensions, z_diff, hit_distance);

            float max_diff = GetMaxDiff(hiz_textures[0], color_uv, ray_map_dimensions);
            if (z_diff < max_diff && ValidUVCoord(color_uv)) {
#if 1
                float4 c = GetInterpolatedColor(color_uv, colorTexture, ray_map_dimensions);
                float4 l = GetInterpolatedColor(color_uv, lightTexture, ray_map_dimensions);
                float4 b = GetInterpolatedColor(color_uv, bloomTexture, ray_map_dimensions);
#else
                color_uv *= ray_map_dimensions;
                float4 c = colorTexture[color_uv];
                float4 l = lightTexture[color_uv];
                float4 b = bloomTexture[color_uv];
#endif
                float diff_ratio = (z_diff / 0.05f);
                ray_input[i] = float2(FLT_MAX, FLT_MAX);
                float3 color = ((c * l + b) * ray_source.opacity);

                //Do not paint scan ray
                pdf_cache[wi] = RAY_W_BIAS + length(color);
                if (i < 3) {
                    final_color += color;
                    n++;
                }
            }
            else {
                pdf_cache[wi] = RAY_W_BIAS;

            }

            if (hit_distance > 2.0f || i >= 3) {
                ray_input[i] = float2(FLT_MAX, FLT_MAX);
            }
        }
    }
    if (n > 0) {
        final_color = (final_color * ray_source.opacity / n);
    }
    restir_pdf_1[pixel] = PackRays(pdf_cache, RAY_W_SCALE);
    output[pixel] = lerp(output[pixel], float4(final_color, 1.0f), 0.3f);
    //output[pixel] = float4(final_color.r, 0.0f, 1.0f, 1.0f);
#endif
    
    if (n > 0) {
        [unroll]
        for (int x = -2; x <= 2; ++x) {
            [unroll]
            for (int y = -2; y <= 2; ++y) {
                int2 p = pixel / kernel_size + int2(x, y);
                tiles_output[p] = 1;
            }
        }
    }
    ray_inputs[pixel] = Pack4Float2ToI16(ray_input, MAX_RAY_POLAR_DIR);

}
