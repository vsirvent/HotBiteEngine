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
    float3 cameraPosition;
    float3 cameraDirection;
    uint enabled;
    int kernel_size;

    matrix prev_view_proj;
    
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
RWTexture2D<float4> bloom : register(u7);
RWTexture2D<float4> tiles_output : register(u4);

Texture2D<float4> ray0;
Texture2D<float4> ray1;

StructuredBuffer<BVHNode> objects: register(t2);
StructuredBuffer<BVHNode> objectBVH: register(t3);

ByteAddressBuffer vertexBuffer : register(t4);
ByteAddressBuffer indicesBuffer : register(t5);
Texture2D<float4> position_map : register(t6);
Texture2D<float4> motion_texture : register(t8);
Texture2D<float4> prev_position_map : register(t9);

Texture2D<float4> DiffuseTextures[MAX_OBJECTS];
Texture2D<float> DirShadowMapTexture[MAX_LIGHTS];
TextureCube<float> PointShadowMapTexture[MAX_LIGHTS];

//Packed array
static float2 lps[MAX_LIGHTS] = (float2[MAX_LIGHTS])LightPerspectiveValues;

#include "../Common/SimpleLight.hlsli"
#include "../Common/RayFunctions.hlsli"

#define max_distance 20.0f

float3 GenerateHemisphereRay(float3 dir, float3 tangent, float3 bitangent, float dispersion, float N, float NLevels, float rX)
{
    float index = rX * N * dispersion;

    // First point at the top (up direction)
    if (index < 1.0f) {
        return dir; // The first point is directly at the top
    }

    float cumulativePoints = 1;
    float level = 1;
    while (true) {
        float c = cumulativePoints + level * 2;
        if (c < index) {
            cumulativePoints = c;
        }
        else {
            break;
        }
        level++;
    };

    float pointsAtLevel = level * 2;  // Quadratic growth

    // Calculate local index within the current level
    float localIndex = index - cumulativePoints;

    float phi = level / NLevels * M_PI * (0.4 + rX);

    // Azimuthal angle (theta) based on number of points at this level
    float theta = (2.0f * M_PI + rX) * localIndex / pointsAtLevel; // Spread points evenly in azimuthal direction

    // Convert spherical coordinates to Cartesian coordinates
    float sinPhi = sin(phi);
    float cosPhi = cos(phi);
    float sinTheta = sin(theta);
    float cosTheta = cos(theta);

    // Local ray direction in spherical coordinates
    float3 localRay = float3(sinPhi * cosTheta, sinPhi * sinTheta, cosPhi);

    // Convert local ray to global coordinates (tangent space to world space)
    float3 globalRay = localRay.x * tangent + localRay.y * bitangent + localRay.z * dir;


    return normalize(dir + globalRay);
}

static float NCOUNT = 32.0f;
static uint N2 = 32;
static float N = N2 * NCOUNT;

struct RayTraceColor {
    float3 color[2];
    float dispersion[2];
    float3 bloom;
    bool hit;
};

bool GetColor(Ray origRay, float rX, float level, uint max_bounces, out RayTraceColor out_color, float dispersion, bool mix, bool refract)
{
    out_color.color[0] = float3(0.0f, 0.0f, 0.0f);
    out_color.color[1] = float3(0.0f, 0.0f, 0.0f);
    out_color.bloom = float3(0.0f, 0.0f, 0.0f);
    out_color.dispersion[0] = -1.0f;
    out_color.dispersion[1] = -1.0f;

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
        color *= o.opacity;

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

        color.rgb *= mat_color;

        float3 emission = material.emission * material.emission_color;
        color += emission;

        float material_dispersion = saturate(1.0f - material.specIntensity);
        att_dist += collision_dist * material_dispersion;
        if (refract) {
            att_dist = 1.0f;
        }
        float curr_att_dist = max(att_dist, 1.0f);
        if (ray.bounces == 0 || mix) {
            out_color.color[0] += color * ray.ratio / curr_att_dist;
            out_color.dispersion[0] = acc_dispersion;
            if (!refract) {
                out_color.bloom += emission / curr_att_dist;
            }
        }
        else {
            out_color.color[1] += color * ray.ratio / curr_att_dist;
            out_color.dispersion[1] = acc_dispersion;
        }
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
            float2 bloom_dimensions;
            {
                uint w, h;
                output0.GetDimensions(w, h);
                dimensions.x = w;
                dimensions.y = h;

                ray0.GetDimensions(w, h);
                ray_map_dimensions.x = w;
                ray_map_dimensions.y = h;

                bloom.GetDimensions(w, h);
                bloom_dimensions.x = w;
                bloom_dimensions.y = h;
            }
            float x = (float)DTid.x;
            float y = (float)DTid.y;

            float2 rayMapRatio = ray_map_dimensions / dimensions;
            float2 bloomRatio = bloom_dimensions / dimensions;

            float4 color_reflex = float4(0.0f, 0.0f, 0.0f, 1.0f);
            float4 color_reflex2 = float4(0.0f, 0.0f, 0.0f, 1.0f);
            float4 color_refrac = float4(0.0f, 0.0f, 0.0f, 1.0f);

            float2 pixel = float2(x, y);

            float2 ray_pixel = round(pixel * rayMapRatio);

            RaySource ray_source = fromColor(ray0[ray_pixel], ray1[ray_pixel]);
            [branch]
            if (ray_source.reflex <= Epsilon || dist2(ray_source.normal) <= Epsilon || ray_source.dispersion < 0.0f || ray_source.dispersion >= 1.0f)
            {
                output0[pixel] = color_reflex;
                output1[pixel] = color_refrac;
                return;
            }

            //Reflected ray
            float3 orig_pos = ray_source.orig.xyz;
            float camDist = length(orig_pos - cameraPosition);
            bool process = camDist < max_distance;

            float3 tangent;
            float3 bitangent;
            RayTraceColor rc;
            Ray ray = GetReflectedRayFromSource(ray_source);
    
        
            [branch]
            if (dist2(ray.dir) > Epsilon)
            {
                float3 normal = ray_source.normal;
                float3 orig_dir = ray.dir;
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

                rc.hit = false;
                [branch]
                if (DTid.z == 0) {
                    float3 seed = orig_pos * 100.0f;
                    float rX = rgba_tnoise(seed);
                    rX = pow(rX, 5.0f);
                    GetSpaceVectors(orig_dir, tangent, bitangent);
                    ray.dir = GenerateHemisphereRay(orig_dir, tangent, bitangent, ray_source.dispersion, N, level, rX);
                    ray.orig.xyz = orig_pos.xyz + ray.dir * 0.001f;
                    float dist = FLT_MAX;
                    GetColor(ray, rX, level, 0, rc, ray_source.dispersion, true, false);
                    color_reflex.rgb += rc.color[0] * ray_source.opacity;
                    color_reflex2.rgb += rc.color[1] * ray_source.opacity;
                    
                    float2 rc_disp = float2(rc.dispersion[0], rc.dispersion[1]);
                    float reflex_ratio = (1.0f - ray_source.dispersion);
                    output0[pixel] = color_reflex;
                    if (rc.hit) {
                        [unroll]
                        for (int x = -1; x <= 1; ++x) {
                            [unroll]
                            for (int y = -1; y <= 1; ++y) {
                                int2 p = pixel / kernel_size + int2(x, y);
                                tiles_output[p] = 1;
                            }
                        }
                    }
                }
                else {
                    //Refracted ray
                    if (ray_source.opacity < 1.0f && (enabled & REFRACT_ENABLED)) {
                        float3 seed = orig_pos * 100.0f;
                        float rX = rgba_tnoise(seed);
                        Ray ray = GetRefractedRayFromSource(ray_source);
                        if (dist2(ray.dir) > Epsilon)
                        {
                            GetColor(ray, rX, level, 0, rc, ray_source.dispersion, true, true);
                            color_refrac.rgb += rc.color[0] * (1.0f - ray_source.opacity);
                        }
                    }
                    output1[pixel] = color_refrac;
            
                    if (rc.hit) {
                        [unroll]
                        for (int x = -2; x <= 2; ++x) {
                            [unroll]
                            for (int y = -2; y <= 2; ++y) {
                                int2 p = pixel / kernel_size + int2(x, y);
                                tiles_output[p] = 1;
                            }
                        }
                    }
                }
            }
        }