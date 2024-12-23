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

RWTexture2D<float4> output0 : register(u0);
RWTexture2D<float4> output1 : register(u1);
RWTexture2D<float4> tiles_output : register(u2);

Texture2D<float4> ray0;
Texture2D<float4> ray1;

StructuredBuffer<InputRays> ray_inputs: register(t0);
StructuredBuffer<BVHNode> objects: register(t2);
StructuredBuffer<BVHNode> objectBVH: register(t3);

Texture2D<float> DirShadowMapTexture[MAX_LIGHTS];
TextureCube<float> PointShadowMapTexture[MAX_LIGHTS];

//Packed array
static float2 lps[MAX_LIGHTS] = (float2[MAX_LIGHTS])LightPerspectiveValues;

#include "../Common/SimpleLight.hlsli"
#include "../Common/RayFunctions.hlsli"

#define max_distance 20.0f

struct RayTraceColor {
    float3 color;
    bool hit;
};

bool GetColor(Ray origRay, uint max_bounces, out RayTraceColor out_color, float dispersion, bool refract)
{
    out_color.color = float3(0.0f, 0.0f, 0.0f);
    out_color.hit = false;

    float last_dispersion = dispersion;
    float acc_dispersion = dispersion;
    float collision_dist = 0.0f;
    float att_dist = 0.0f;
#if USE_OBH   
    uint volumeStack[MAX_STACK_SIZE];
#endif
    uint stack[MAX_STACK_SIZE];
    RayObject oray;
    IntersectionResult object_result;

    bool collide = false;
    IntersectionResult result;
    Ray ray = origRay;
    bool end = false;

    while (!end)
    {
        result.distance = FLT_MAX;
        end = true;
        collide = false;
#if USE_OBH
        uint volumeStackSize = 0;
        volumeStack[volumeStackSize++] = 0;
        uint i = 0;
        while (volumeStackSize > 0 && volumeStackSize < MAX_STACK_SIZE)
        {
#else
        for (uint i = 0; i < nobjects; ++i)
        {
            uint objectIndex = i;
#endif

#if USE_OBH
            uint currentVolume = volumeStack[--volumeStackSize];

            BVHNode volumeNode = objectBVH[currentVolume];
            [branch]
            if (is_leaf(volumeNode))
            {
                uint objectIndex = index(volumeNode);
                ObjectInfo o = objectInfos[objectIndex];

                if (IntersectAABB(ray, o.aabb_min, o.aabb_max))
                {
#else
            ObjectInfo o = objectInfos[i];
            float objectExtent = length(o.aabb_max - o.aabb_min);
            float distanceToObject = length(o.position - origRay.orig.xyz) - objectExtent;
            [branch]
            if (distanceToObject < max_distance && distanceToObject < result.distance && IntersectAABB(ray, o.aabb_min, o.aabb_max))
            {
#endif
                object_result.distance = FLT_MAX;

                // Transform the ray direction from world space to object space
                oray.orig = mul(ray.orig, o.inv_world);
                oray.orig /= oray.orig.w;
                oray.dir = normalize(mul(ray.dir, (float3x3) o.inv_world));
                oray.t = FLT_MAX;

                uint stackSize = 0;
                stack[stackSize++] = 0;

                while (stackSize > 0 && stackSize < MAX_STACK_SIZE)
                {
                    uint current = stack[--stackSize];

                    BVHNode node = objects[o.objectOffset + current];
                    if (is_leaf(node))
                    {
                        float t;
                        uint idx = index(node);
                        idx += o.indexOffset;

                        IntersectionResult tmp_result;
                        tmp_result.distance = FLT_MAX;
                        if (IntersectTri(oray, idx, o.vertexOffset, tmp_result))
                        {
                            if (tmp_result.distance < object_result.distance) {
                                object_result.v0 = tmp_result.v0;
                                object_result.v1 = tmp_result.v1;
                                object_result.v2 = tmp_result.v2;
                                object_result.vindex = tmp_result.vindex;
                                object_result.distance = tmp_result.distance;
                                object_result.u = tmp_result.u;
                                object_result.v = tmp_result.v;
                                object_result.object = objectIndex;
                            }
                        }
                    }
                    else if (IntersectAABB(oray, node))
                    {
                        uint left_node_index = left_child(node);
                        uint right_node_index = right_child(node);

                        BVHNode left_node = objects[o.objectOffset + left_node_index];
                        BVHNode right_node = objects[o.objectOffset + right_node_index];

                        float left_dist = node_distance(left_node, oray.orig.xyz);
                        float right_dist = node_distance(right_node, oray.orig.xyz);

                        if (left_dist < right_dist) {
                            if (right_dist < object_result.distance && right_dist < max_distance) {
                                stack[stackSize++] = right_node_index;
                            }
                            if (left_dist < object_result.distance && left_dist < max_distance) {
                                stack[stackSize++] = left_node_index;
                            }
                        }
                        else {
                            if (left_dist < object_result.distance && left_dist < max_distance) {
                                stack[stackSize++] = left_node_index;
                            }
                            if (right_dist < object_result.distance && right_dist < max_distance) {
                                stack[stackSize++] = right_node_index;
                            }
                        }
                    }
                }

                if (object_result.distance < FLT_MAX) {
                    float3 opos = (1.0f - object_result.u - object_result.v) * object_result.v0 + object_result.u * object_result.v1 + object_result.v * object_result.v2;
                    float4 pos = mul(float4(opos, 1.0f), o.world);
                    float distance = length(pos - ray.orig);
                    if (distance < result.distance)
                    {
                        collide = true;
                        collision_dist = length(pos - ray.orig);
                        ray.t = distance;
                        result.v0 = object_result.v0;
                        result.v1 = object_result.v1;
                        result.v2 = object_result.v2;
                        result.vindex = object_result.vindex;
                        result.distance = distance;
                        result.u = object_result.u;
                        result.v = object_result.v;
                        result.object = objectIndex;
                    }
                }
            }
#if USE_OBH
                }
            else
                [branch]
                if (IntersectAABB(ray, volumeNode)) {

                    uint left_node_index = left_child(volumeNode);
                    uint right_node_index = right_child(volumeNode);

                    BVHNode left_node = objectBVH[left_node_index];
                    BVHNode right_node = objectBVH[right_node_index];

                    float left_dist = node_distance(left_node, ray.orig.xyz);
                    float right_dist = node_distance(right_node, ray.orig.xyz);

                    if (left_dist < right_dist) {
                        if (right_dist < result.distance && right_dist < max_distance) {
                            volumeStack[volumeStackSize++] = right_node_index;
                        }
                        if (left_dist < result.distance && left_dist < max_distance) {
                            volumeStack[volumeStackSize++] = left_node_index;
                        }
                    }
                    else {
                        if (left_dist < result.distance && left_dist < max_distance) {
                            volumeStack[volumeStackSize++] = left_node_index;
                        }
                        if (right_dist < result.distance && right_dist < max_distance) {
                            volumeStack[volumeStackSize++] = right_node_index;
                        }
                    }
                }
                ++i;
#endif
            }

    //At this point we have the ray collision distance and a collision result
    if (collide) {
        // Calculate space position
        ObjectInfo o = objectInfos[result.object];
        float3 normal0 = asfloat(vertexBuffer.Load3(result.vindex.x + 12));
        float3 normal1 = asfloat(vertexBuffer.Load3(result.vindex.y + 12));
        float3 normal2 = asfloat(vertexBuffer.Load3(result.vindex.z + 12));
        float3 opos = (1.0f - result.u - result.v) * result.v0 + result.u * result.v1 + result.v * result.v2;
        float3 normal = (1.0f - result.u - result.v) * normal0 + result.u * normal1 + result.v * normal2;
        normal = normalize(mul(normal, (float3x3)o.world));
        float4 pos = mul(float4(opos, 1.0f), o.world);
        pos /= pos.w;
        MaterialColor material = objectMaterials[result.object];

        float3 color = float3(0.0f, 0.0f, 0.0f);

        color += CalcAmbient(normal) * 0.5f;


        uint i = 0;
        for (i = 0; i < dirLightsCount; ++i) {
            color += CalcDirectional(normal, pos.xyz, dirLights[i], i);
        }

        // Calculate the point lights
        for (i = 0; i < pointLightsCount; ++i) {
            if (length(pos.xyz - pointLights[i].Position) < pointLights[i].Range) {
                color += CalcPoint(normal, pos.xyz, pointLights[i], i);
            }
        }

        bool use_mat_texture = material.flags & DIFFUSSE_MAP_ENABLED_FLAG;
        float3 mat_color = material.diffuseColor.rgb * !use_mat_texture;

        float2 uv0 = asfloat(vertexBuffer.Load2(result.vindex.x + 24));
        float2 uv1 = asfloat(vertexBuffer.Load2(result.vindex.y + 24));
        float2 uv2 = asfloat(vertexBuffer.Load2(result.vindex.z + 24));
        float2 uv = uv0 * (1.0f - result.u - result.v) + uv1 * result.u + uv2 * result.v;

        mat_color += GetDiffuseColor(result.object, uv) * use_mat_texture;

        color.rgb *= mat_color * o.opacity;

        float3 emission = material.emission * material.emission_color;
        color += emission;

        float material_dispersion = saturate(1.0f - material.specIntensity);
        att_dist += collision_dist * material_dispersion;
        if (refract) {
            att_dist = 1.0f;
        }
        float curr_att_dist = max(att_dist, 1.0f);
        out_color.color += color * ray.ratio / curr_att_dist;
        acc_dispersion += material_dispersion;
        last_dispersion = material_dispersion;
        ray.orig = pos;
        out_color.hit = true;

        //If not opaque surface, generate a refraction ray
        if (ray.bounces < 5 && o.opacity < 1.0f) {
            if (ray.bounces % 2 == 0) {
                normal = -normal;
            }
            ray = GetRefractedRayFromRay(ray, ray.density,
                ray.density != 1.0 ? 1.0f : o.density,
                normal, ray.ratio * (1.0f - o.opacity));
            end = false;
        }
    }
    else {
        //Color background

    }
        }
return out_color.hit;
        }

#define DENSITY 1.0f
#define NTHREADS 32

[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    float2 dimensions;
    float2 ray_map_dimensions;
    {
        uint w, h;
        output0.GetDimensions(w, h);
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
    uint ray_input_pos = (pixel.y * dimensions.x + pixel.x);

    RaySource ray_source = fromColor(ray0[ray_pixel], ray1[ray_pixel]);
    
    float4 color_reflex = float4(0.0f, 0.0f, 0.0f, 1.0f);
    float4 color_refrac = float4(0.0f, 0.0f, 0.0f, 1.0f);
    bool hit = false;
    //Ray ray = GetRayInfoFromSourceWithNoDir(ray_source);
    Ray ray = GetReflectedRayFromSource(ray_source, cameraPosition);
    float3 orig = ray.orig;
    //Get rays to be solved in the pixel
    [unroll]
    for (int i = 0; i < MAX_RAY_INPUTS; ++i) {
        RayTraceColor rc;
        float2 ray_input_dir = ray_inputs[ray_input_pos].dir[i];
        [branch]
        if (ray_input_dir.x > 10e10) {
            continue;
        }

        if (dist2(ray_input_dir) <= Epsilon) {
            color_reflex.rgb = float3(1.0f, 0.0f, 0.0f);
            break;
        }
        bool is_refract = abs(ray_input_dir.x) > 50.0f;

        ray_input_dir -= float2(100.0f, 100.0f) * is_refract;

        //We have a ray
        ray.dir = GetCartesianCoordinates(ray_input_dir);
        ray.orig.xyz = orig + ray.dir * 0.01f;

        GetColor(ray, 0, rc, ray_source.dispersion, is_refract);
        
        float reflex_ratio = (1.0f - ray_source.dispersion);
        color_reflex.rgb += rc.color * ray_source.opacity * (is_refract == 0);
        color_refrac.rgb += rc.color * (1.0f - ray_source.opacity) * is_refract;
        hit = hit || rc.hit;
    }
    if (hit) {
        [unroll]
        for (int x = -2; x <= 2; ++x) {
            [unroll]
            for (int y = -2; y <= 2; ++y) {
                int2 p = pixel / kernel_size + int2(x, y);
                tiles_output[p] = 1;
            }
        }
    }
    output0[pixel] = color_reflex;
    output1[pixel] = color_refrac;
}
    