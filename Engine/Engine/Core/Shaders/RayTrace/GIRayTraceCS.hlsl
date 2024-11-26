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

#define REFLEX_ENABLED 1
#define REFRACT_ENABLED 2
#define INDIRECT_ENABLED 4
#define USE_OBH 0
#define LEVEL_RATIO 3
//#define BOUNCES
//#define DISABLE_RESTIR

cbuffer externalData : register(b0)
{
    uint frame_count;
    float time;
    float3 cameraPosition;
    float3 cameraDirection;

    uint ray_count;
    int kernel_size;

    //Lights
    AmbientLight ambientLight;
    DirLight dirLights[MAX_LIGHTS];
    PointLight pointLights[MAX_LIGHTS];
    uint dirLightsCount;
    uint pointLightsCount;

    matrix view;
    matrix projection;

    float4 LightPerspectiveValues[MAX_LIGHTS / 2];
    matrix DirPerspectiveMatrix[MAX_LIGHTS];
}

cbuffer objectData : register(b1)
{
    uint nobjects;
    ObjectInfo objectInfos[MAX_OBJECTS];
    MaterialColor objectMaterials[MAX_OBJECTS];
}

RWTexture2D<float4> output;
Texture2D<float4> ray0;
Texture2D<float4> ray1;

StructuredBuffer<BVHNode> objects: register(t2);
StructuredBuffer<BVHNode> objectBVH: register(t3);

ByteAddressBuffer vertexBuffer : register(t4);
ByteAddressBuffer indicesBuffer : register(t5);
Texture2D<float4> position_map : register(t6);
Texture2D<float4> motion_texture : register(t8);
Texture2D<float4> prev_position_map: register(t9);
Texture2D<uint4> restir_pdf_0: register(t10);
Texture2D<float> restir_w_0: register(t11);
RWTexture2D<uint4> restir_pdf_1: register(u0);
RWTexture2D<uint> tiles_output: register(u1);

Texture2D<float4> DiffuseTextures[MAX_OBJECTS];
Texture2D<float> DirShadowMapTexture[MAX_LIGHTS];
TextureCube<float> PointShadowMapTexture[MAX_LIGHTS];



//Packed array
static float2 lps[MAX_LIGHTS] = (float2[MAX_LIGHTS])LightPerspectiveValues;

#include "../Common/SimpleLight.hlsli"
#include "../Common/RayFunctions.hlsli"

#define max_distance 10.0f

static const float inv_ray_count = 1.0f / (float)ray_count;
static const uint stride = kernel_size * ray_count;

static const uint max_x = ray_count * kernel_size;
static const uint max_y = stride * kernel_size;

static const uint N = ray_count * kernel_size * kernel_size;
static const float space_size = (float)N / (float)ray_count;
static const float ray_enery_unit = inv_ray_count;



uint GetRayIndex(float2 pixel, float pdf_cache[MAX_RAYS], Texture2D<float> w_data, float index) {
#ifdef DISABLE_RESTIR
    return index;
#endif
    float w = max(w_data[pixel], RAY_W_BIAS * ray_count);
    float tmp_w = 0.0f;
    index = index * w * inv_ray_count;
    
    for (uint i = 0; i < ray_count; i++) {
            tmp_w += pdf_cache[i];
            if (tmp_w > index) {
                break;
            }
    }
    return i;
}

float3 GenerateHemisphereRay(float3 dir, float3 tangent, float3 bitangent, float dispersion, float N, float NLevels, float rX)
{
    float index = (rX * dispersion) % N;

    //index = (frame_count / 5) % N;
    float cumulativePoints = 1.0f;
    float level = 1.0f;
    float c = 1.0f;
    while (c < index) {
        c = cumulativePoints + level * LEVEL_RATIO;
        cumulativePoints = c;
        level++;
    };
    level--;

    float pointsAtLevel = level * LEVEL_RATIO;

    // Calculate local index within the current level
    float localIndex = index - cumulativePoints;

    float phi = (level * M_PI) / NLevels;

    // Azimuthal angle (theta) based on number of points at this level
    float theta = (2.0f * M_PI) * localIndex / pointsAtLevel; // Spread points evenly in azimuthal direction
        

    // Convert spherical coordinates to Cartesian coordinates
    float sinPhi = sin(phi);
    float cosPhi = cos(phi);
    float sinTheta = sin(theta);
    float cosTheta = cos(theta);

    // Local ray direction in spherical coordinates
    float3 localRay = float3(sinPhi * cosTheta, cosPhi, sinPhi * sinTheta);

    // Convert local ray to global coordinates (tangent space to world space)
    float3 globalRay = localRay.x * tangent + localRay.y * dir + localRay.z * bitangent;


    return normalize(dir + globalRay);
}

struct RayTraceColor {
    float3 color;
    bool hit;
};

void GetColor(Ray origRay, float rX, float level, uint max_bounces, out RayTraceColor out_color, float dispersion, bool mix, bool refract)
{
    out_color.color = float3(0.0f, 0.0f, 0.0f);
    out_color.hit = false;

    float collision_dist = 0.0f;
#if USE_OBH   
    uint volumeStack[MAX_STACK_SIZE];
#endif
    uint stack[MAX_STACK_SIZE];
    RayObject oray;
    IntersectionResult object_result;

    bool collide = false;
    IntersectionResult result;
    Ray ray = origRay;
    float att_dist = 1.0f;
    bool end = false;
#ifdef BOUNCES
    while (!end) {
#endif
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

                float objectExtent = length(o.aabb_max - o.aabb_min);
                float distanceToObject = length(o.position - origRay.orig.xyz) - objectExtent;
                [branch]
                if (distanceToObject < max_distance && distanceToObject < result.distance && IntersectAABB(ray, o.aabb_min, o.aabb_max))
                {
#else
            ObjectInfo o = objectInfos[i];
            float objectExtent = length(o.aabb_max - o.aabb_min);
            float distanceToObject = length(o.position - origRay.orig.xyz) - objectExtent;
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
                    [branch]
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
                                object_result.uv = tmp_result.uv;
                                object_result.object = objectIndex;
                            }
                        }
                    }
                    else 
                    if (IntersectAABB(oray, node))
                    {
                        stack[stackSize++] = left_child(node);
                        stack[stackSize++] = right_child(node);
                    }
                }

                if (object_result.distance < FLT_MAX) {
                    float3 opos = (1.0f - object_result.uv.x - object_result.uv.y) * object_result.v0 + object_result.uv.x * object_result.v1 + object_result.uv.y * object_result.v2;
                    float4 pos = mul(float4(opos, 1.0f), o.world);
                    float distance = length(pos - ray.orig);
                    if (distance < result.distance)
                    {
                        collide = true;
                        collision_dist = distance;
                        ray.t = distance;
                        result.v0 = object_result.v0;
                        result.v1 = object_result.v1;
                        result.v2 = object_result.v2;
                        result.vindex = object_result.vindex;
                        result.distance = distance;
                        result.uv = object_result.uv;
                        result.object = objectIndex;
                    }
                }
            }
#if USE_OBH
                }
 else 
     [branch]
     if (IntersectAABB(ray, volumeNode)) {
     volumeStack[volumeStackSize++] = left_child(volumeNode);
     volumeStack[volumeStackSize++] = right_child(volumeNode);
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
        float3 opos = (1.0f - result.uv.x - result.uv.y) * result.v0 + result.uv.x * result.v1 + result.uv.y * result.v2;
        float3 normal = (1.0f - result.uv.x - result.uv.y) * normal0 + result.uv.x * normal1 + result.uv.y * normal2;
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
            //if (length(pos.xyz - pointLights[i].Position) < pointLights[i].Range) {
                color += CalcPoint(normal, pos.xyz, pointLights[i], i);
            //}
        }

        //Calculate material color
        if (material.flags & DIFFUSSE_MAP_ENABLED_FLAG) {
            float2 uv0 = asfloat(vertexBuffer.Load2(result.vindex.x + 24));
            float2 uv1 = asfloat(vertexBuffer.Load2(result.vindex.y + 24));
            float2 uv2 = asfloat(vertexBuffer.Load2(result.vindex.z + 24));
            float2 uv = uv0 * (1.0f - result.uv.x - result.uv.y) + uv1 * result.uv.x + uv2 * result.uv.y;
            color *= GetDiffuseColor(result.object, uv);
        }
        else {
            color *= material.diffuseColor.rgb;
        }

        float3 emission = material.emission * material.emission_color;
        color += emission;

        att_dist *= max(collision_dist, 1.0f);

        out_color.color += color * ray.ratio / att_dist;

        ray.orig = pos;
        out_color.hit = true;
#ifdef BOUNCES
        if (material.emission <= Epsilon && ray.bounces < max_bounces && o.opacity > 0.0f) {
            ray = GetReflectedRayFromRay(ray, normal, ray.ratio);
            float3 tangent;
            float3 bitangent;
            GetSpaceVectors(ray.dir, tangent, bitangent);
            ray.dir = GenerateHemisphereRay(ray.dir, tangent, bitangent, (1.0f - material.specIntensity), N, level, rX);
            end = false;
        }
    }
#endif
        }
    }

    bool IsLowEnergy(float pdf[MAX_RAYS], uint len) {
        
        float total_enery = 0.0f;

        for (int i = 0; i < len; ++i) {
            total_enery += pdf[i];
        }
        float threshold = len * RAY_W_BIAS;

        return (total_enery <= threshold);
    }

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

    float2 rpixel = pixel * rayMapRatio;
    //rpixel.x += frame_count % (rayMapRatio.x * 0.5f);
    //rpixel.y += frame_count % (rayMapRatio.y * 0.5f) / 2.0f;
    float2 ray_pixel = round(rpixel);

    RaySource ray_source = fromColor(ray0[ray_pixel], ray1[ray_pixel]);
    bool end = (ray_source.reflex <= Epsilon || dist2(ray_source.normal) <= Epsilon);

    float3 orig_pos = ray_source.orig.xyz;
    float toCamDistance = dist2(orig_pos - cameraPosition);
   
    float3 tangent;
    float3 bitangent;
    RayTraceColor rc;
    Ray ray = GetReflectedRayFromSource(ray_source);

    float pdf_cache[MAX_RAYS];
    UnpackRays(restir_pdf_0[pixel], RAY_W_SCALE, pdf_cache);
    uint i = 0;

    end = end || (dist2(ray.dir) <= Epsilon);
    [branch]
    if (end)
    {
        for (i = 0; i < ray_count; i++) {
            pdf_cache[i] = max(pdf_cache[i] - 0.01f, RAY_W_BIAS);
            restir_pdf_1[pixel] = PackRays(pdf_cache, RAY_W_SCALE);
        }
        output[pixel] = float4(0.0f, 0.0f, 0.0f, -1.0f);
        return;
    }

    matrix worldViewProj = mul(view, projection);
    float4 prev_pos = mul(prev_position_map[ray_pixel], worldViewProj);
    prev_pos.x /= prev_pos.w;
    prev_pos.y /= -prev_pos.w;
    prev_pos.xy = round((prev_pos.xy + 1.0f) * dimensions.xy / 2.0f);

    float3 normal = ray_source.normal;
    float3 orig_dir = ray.dir;

    float cumulativePoints = 1;
    float level = 1;
    uint c = 0;
    while (c < N) {
        c = cumulativePoints + level * LEVEL_RATIO;
        cumulativePoints = c;
        level++;
    };
                      
    GetSpaceVectors(normal, tangent, bitangent);

    float color_w = 0.0f;

    for (i = 0; i < ray_count; i++) {
        pdf_cache[i] = max(pdf_cache[i], RAY_W_BIAS);
    }
    
    float wis[MAX_RAYS];
    int wis_size = 0;
    uint last_wi = MAX_RAYS + 1;
    
    //Check if this is a low enery pixel
    uint low_energy = IsLowEnergy(pdf_cache, ray_count);
#ifdef DISABLE_RESTIR
    uint start = 0;
    uint step = 1;
#else
    float2 mvector = motion_texture[ray_pixel].xy;
    float motion = 0.0f;
    if (mvector.x > -FLT_MAX) {
        motion = dist2(mvector);
    }
    float motion_ratio = 1.0f / max(100.0f * sqrt(motion) * toCamDistance, 0.01f);
    uint start = (((pixel.x + pixel.y + frame_count)) % ray_count) * low_energy * motion_ratio;
    uint step = frame_count % 4 + ray_count/4 + (ray_count * motion_ratio) * low_energy;
#endif

    for (i = 0; i < ray_count; i += step) {
        uint index = (i + start) % ray_count;
        uint wi = GetRayIndex(prev_pos.xy, pdf_cache, restir_w_0, index);
        wis[wis_size] = wi;
        wis_size += (last_wi != wi);
        last_wi = wi;
    }
 
    float4 color_diffuse = float4(0.0f, 0.0f, 0.0f, 1.0f);
    float offset = (pixel.x % kernel_size) * ray_count + (pixel.y % kernel_size) * stride;
    float offset2 = space_size;
   
    bool hit = false;
    for (i = 0; i < wis_size; ++i) {

        uint wi = wis[i];
        float n = fmod(offset + (float)wi * offset2, N);

        ray.dir = GenerateHemisphereRay(normal, tangent, bitangent, 1.0f, N, level * 1.2f, n);
        ray.orig.xyz = orig_pos.xyz + ray.dir * 0.001f;
        float dist = FLT_MAX;
        GetColor(ray, n, level, 1, rc, ray_source.dispersion, true, false);
        color_diffuse.rgb += rc.color;
        last_wi = wi;
        hit = hit || rc.hit;

        float w = length(rc.color.rgb);

#ifdef DISABLE_RESTIR
        pdf_cache[wi] = 1.0f;
#else
        pdf_cache[wi] = RAY_W_BIAS + w;
#endif
    }

    wis_size = max(wis_size, 1);
    restir_pdf_1[pixel] = PackRays(pdf_cache, RAY_W_SCALE);
    color_diffuse  = color_diffuse / wis_size;
    
    color_diffuse = sqrt(color_diffuse);
    //color_diffuse.a = 1.0f - 2.0f * !low_energy;
    output[pixel] = color_diffuse;

    if (!low_energy) {
        tiles_output[floor((float2)pixel / (float)(kernel_size * 4))] = 1;
    }
    //float r = wis_size / ray_count;
    //output[pixel] = float4(wis_size, 0.0f, 0.0f, 1.0f);
}
