/*
The HotBite Game Engine

Copyright(c) 2024 Vicente Sirvent Orts

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

//Reflections and refractions, step one of two: look at every pixel once, answer on
//the screen what the screen can answer, and LIST what is left for the world tracer.
//
//  - Every reflective pixel's reflection ray (the very one RayTraceCS would trace,
//    see ReflexRay.hlsli) is marched across the depth buffer. If it passes behind a
//    surface that is on the screen, what is drawn there IS the reflection - the lit
//    colour of that pixel - and the pixel is finished.
//  - Whatever the screen could not answer (a ray that leaves it, hides behind
//    something, or finds nothing on it) is appended to the reflection list, and so is
//    every pixel that refracts: those need the world, whatever the screen found.
//
//The lists are the point of the exercise. RayTraceCS runs over THEM, not over the
//screen: a thread per ray left, packed from the front of the list. Had it run over
//the screen and skipped the pixels solved here, a warp with one unsolved pixel among
//thirty-one solved ones would still have gone through the whole BVH walk for that one,
//and the screen pass would have saved nothing - the cost of a warp is that of its
//slowest lane. Packed, a warp is full of rays that all need tracing, and the rays the
//screen solved are not in anybody's warp.
//
//The list keeps the screen's locality: each 8x8 group of pixels appends its entries
//together, in pixel order, so the 32 entries of a warp are neighbouring pixels (two
//tiles at most) whose rays go the same way and walk the same part of the BVH - the
//property the screen-sized dispatch it replaces had by construction.
//
//It writes output0 and output1 for EVERY pixel of its dispatch (the solved colour or
//black), so the frame needs no clear; the world tracer overwrites the listed ones.

#include "../Common/ShaderStructs.hlsli"
#include "../Common/PixelCommon.hlsli"
#include "../Common/RayDefines.hlsli"
#include "../Common/RGBANoise.hlsli"
#include "../Common/GBuffer.hlsli"

#define REFLEX_ENABLED 1
#define REFRACT_ENABLED 2

cbuffer externalData : register(b0)
{
    float3 cameraPosition;
    uint enabled;
    int kernel_size;
    float hiz_ratio;
    //How far along a ray, in world units, the march looks.
    float max_distance;
    //Steps the march may take before it gives the ray up.
    int max_steps;
    //Which of the two solvers are on: bit 0 the screen, bit 1 the world (RT_SPACE_*).
    uint space;
    //Entries each list can hold: one per pixel of this dispatch.
    uint list_capacity;
    //Whether to count what was solved where (counts[RT_COUNT_*]); for a readout only.
    uint collect_stats;
    matrix view_proj;
}

#include "RayScreenSolver.hlsli"
#include "ReflexRay.hlsli"
#include "RayLists.hlsli"

RWTexture2D<float4> output0 : register(u0);
RWTexture2D<float4> output1 : register(u1);
//The lists, and the counters that say how full they are. See RayLists.hlsli.
RWBuffer<uint> ray_list : register(u2);
RWByteAddressBuffer ray_counts : register(u3);
//The denoiser skips tiles nobody wrote to; see RayTraceCS.
RWTexture2D<uint> tiles_output : register(u4);

//Level 0 is depth_map itself, then the min pyramid built from it by HiZDownSample.
Texture2D<float> hiz_textures[HIZ_TEXTURES];
Texture2D<float4> ray0;
Texture2D<float4> ray1;
//The frame as it is lit when this runs: albedo, light and bloom, which the mixer will
//later combine as albedo * light + bloom (plus the ray traced terms this is computing).
Texture2D<float4> colorTexture;
Texture2D<float4> lightTexture;
Texture2D<float4> bloomTexture;
//The G-buffer's material map, for how shiny the surface a ray lands on is.
Texture2D<float4> materialTexture;

#define NTHREADS 8
#define GROUP_SIZE (NTHREADS * NTHREADS)

//The group's running counts of the entries it appends to each list, scanned in place:
//after the scan, a thread's slot holds how many entries come before it, itself included.
groupshared uint scan_reflect[GROUP_SIZE];
groupshared uint scan_refract[GROUP_SIZE];
groupshared uint list_base[2];
groupshared uint group_stats[2];

[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint group_index : SV_GroupIndex)
{
    //Every thread reaches the barriers below, so nobody returns early: a thread past
    //the edge of the image just has nothing to do.
    uint2 output_size;
    output0.GetDimensions(output_size.x, output_size.y);
    bool inside = DTid.x < output_size.x && DTid.y < output_size.y;

    uint to_reflect = 0;
    uint to_refract = 0;
    uint solved = 0;
    uint reflective = 0;

    if (group_index < 2) {
        group_stats[group_index] = 0;
    }

    [branch]
    if (inside) {
        uint ray_w, ray_h;
        ray0.GetDimensions(ray_w, ray_h);
        float2 ray_map_dimensions = float2(ray_w, ray_h);

        float2 pixel = float2(DTid.xy);
        float2 ray_pixel = round(pixel * (ray_map_dimensions / float2(output_size)));

        RaySource ray_source = fromColor(ray0[ray_pixel], ray1[ray_pixel]);

        float4 result = float4(0.0f, 0.0f, 0.0f, 1.0f);

        [branch]
        if (PixelReflects(ray_source)) {
            float3 mirror = ReflexMirrorDirection(ray_source);
            [branch]
            if (dist2(mirror) > Epsilon) {
                bool reflections = (enabled & REFLEX_ENABLED) != 0;
                reflective = 1;

                [branch]
                if (reflections && (space & RT_SPACE_SCREEN)) {
                    float rX;
                    float3 dir_world = ReflexRayDirection(ray_source.orig, mirror, ray_source.dispersion, rX);

                    //A ray that does not leave the surface (a rough one spread under the
                    //horizon) has nothing in front of it on the screen: the world tracer
                    //can say where it goes, this cannot.
                    uint2 hit_pixel;
                    [branch]
                    if (dot(dir_world, ray_source.normal) > 0.01f) {
                        [branch]
                        if (ScreenTrace(ray_source.orig + dir_world * 0.01f, dir_world, cameraPosition, ray_map_dimensions, view_proj,
                                        hiz_textures, hiz_ratio, max_distance, max_steps,
                                        0.0f, -1.0f, 0.0f, true, ray0, ray1, hit_pixel)) {
                            float3 lit = colorTexture[hit_pixel].rgb * lightTexture[hit_pixel].rgb + bloomTexture[hit_pixel].rgb;

                            //RayTraceCS fades what it finds with the distance the ray went,
                            //by how rough the surface it landed on is (`att_dist`); the same
                            //law here, or a reflection would change brightness where the two
                            //solvers meet. The roughness is the landing surface's own, read
                            //from the G-buffer (spec intensity is its x for the models that
                            //have one).
                            float4 material = materialTexture[hit_pixel];
                            uint model = (uint)material.w & GBUFFER_MODEL_MASK;
                            float spec = (model == SHADING_STANDARD || model == SHADING_SPLAT) ? saturate(material.x) : 0.5f;
                            float travelled = length(ray0[hit_pixel].xyz - ray_source.orig);
                            float attenuation = max(travelled * saturate(1.0f - spec), 1.0f);

                            result.rgb = lit * ray_source.opacity / attenuation;
                            solved = 1;

                            [unroll]
                            for (int x = -1; x <= 1; ++x) {
                                [unroll]
                                for (int y = -1; y <= 1; ++y) {
                                    int2 p = int2(DTid.xy / (uint)kernel_size) + int2(x, y);
                                    tiles_output[p] = 1;
                                }
                            }
                        }
                    }
                }

                //What is left goes to the world tracer - if there is one.
                [branch]
                if (space & RT_SPACE_WORLD) {
                    to_reflect = (reflections && solved == 0) ? 1 : 0;
                    to_refract = ((enabled & REFRACT_ENABLED) && ray_source.opacity < 1.0f) ? 1 : 0;
                }
            }
        }
        output0[DTid.xy] = result;
        output1[DTid.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    //Where this thread's entries go: the exclusive prefix of the group's flags, so the
    //entries stay in pixel order.
    scan_reflect[group_index] = to_reflect;
    scan_refract[group_index] = to_refract;
    GroupMemoryBarrierWithGroupSync();
    [unroll]
    for (uint offset = 1; offset < GROUP_SIZE; offset <<= 1) {
        uint a = (group_index >= offset) ? scan_reflect[group_index - offset] : 0;
        uint b = (group_index >= offset) ? scan_refract[group_index - offset] : 0;
        GroupMemoryBarrierWithGroupSync();
        scan_reflect[group_index] += a;
        scan_refract[group_index] += b;
        GroupMemoryBarrierWithGroupSync();
    }
    uint rank_reflect = scan_reflect[group_index] - to_reflect;
    uint rank_refract = scan_refract[group_index] - to_refract;

    //One reservation per list per GROUP, not one atomic per ray: the last thread holds
    //both totals.
    if (group_index == GROUP_SIZE - 1) {
        uint total_reflect = scan_reflect[group_index];
        uint total_refract = scan_refract[group_index];
        uint base_reflect = 0;
        uint base_refract = 0;
        if (total_reflect > 0) {
            ray_counts.InterlockedAdd(RT_COUNT_REFLECT * 4, total_reflect, base_reflect);
        }
        if (total_refract > 0) {
            ray_counts.InterlockedAdd(RT_COUNT_REFRACT * 4, total_refract, base_refract);
        }
        list_base[0] = base_reflect;
        list_base[1] = base_refract;
    }

    if (collect_stats != 0) {
        if (solved != 0) {
            InterlockedAdd(group_stats[0], 1);
        }
        if (reflective != 0) {
            InterlockedAdd(group_stats[1], 1);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    uint entry = PackRayPixel(DTid.xy);
    if (to_reflect != 0) {
        ray_list[RT_LIST_REFLECT * list_capacity + list_base[0] + rank_reflect] = entry;
    }
    if (to_refract != 0) {
        ray_list[RT_LIST_REFRACT * list_capacity + list_base[1] + rank_refract] = entry;
    }

    if (collect_stats != 0 && group_index == 0) {
        uint ignored;
        if (group_stats[0] > 0) {
            ray_counts.InterlockedAdd(RT_COUNT_SCREEN_SOLVED * 4, group_stats[0], ignored);
        }
        if (group_stats[1] > 0) {
            ray_counts.InterlockedAdd(RT_COUNT_REFLECTIVE * 4, group_stats[1], ignored);
        }
    }
}
