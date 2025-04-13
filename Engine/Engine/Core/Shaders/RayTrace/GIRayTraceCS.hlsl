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

//#define BOUNCES
//#define DISABLE_RESTIR

cbuffer externalData : register(b0)
{
    uint frame_count;
    float time;
    float3 cameraPosition;
    uint divider;
    matrix view_proj;
    int kernel_size;
}

Texture2D<float4> ray0;
Texture2D<float4> ray1;

Texture2D<float4> motion_texture : register(t8);
Texture2D<float4> prev_position_map: register(t9);
Texture2D<uint4> restir_pdf_0: register(t10);
Texture2D<float> restir_w_0: register(t11);
RWTexture2D<uint4> ray_inputs: register(u2);
RWTexture2D<uint> restir_pdf_mask: register(u3);

#include "../Common/RayFunctions.hlsli"

static const float inv_ray_count = 1.0f / (float)MAX_RAYS;
static const uint stride = kernel_size * MAX_RAYS;

static const uint N = MAX_RAYS * kernel_size * kernel_size;
static const float space_size = (float)N / (float)MAX_RAYS;


uint GetRayIndex(float pdf_cache[MAX_RAYS], float w, float index) {
#ifdef DISABLE_RESTIR
    return index;
#endif
    float tmp_w = 0.0f;
    index = (index + 1.0f) * w * inv_ray_count;
    
    for (uint i = 0; i < MAX_RAYS; i++) {
            tmp_w += pdf_cache[i];
            if (tmp_w > index) {
                break;
            }
    }
    return i;
}

#if 0
float2 GenerateHemisphereRay(float3 dir, float3 tangent, float3 bitangent, float dispersion, float N, float NLevels, float rX)
{
    float index = (rX * dispersion) % N;

    //index = (frame_count) % N;
    float N_SQRT = sqrt(N);
    float cumulativePoints = 1.0f;
    float level = 1.0f;
    float c = 1.0f;
    float phi;
    while (c < index) {
        phi = (level * M_PI * 0.5f) / NLevels;
        c = cumulativePoints + N_SQRT * sin(phi);
        cumulativePoints = c;
        level++;
    };
    level--;

    
    float pointsAtLevel = 1.0f + ceil(N_SQRT * sin(phi));
    phi = (level * M_PI) / NLevels;

    // Calculate local index within the current level
    float localIndex = index - cumulativePoints;
   
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

    return GetPolarCoordinates(normalize(dir + globalRay));
}
#else

#define LEVEL_RATIO 3
float2 GenerateHemisphereRay(float3 dir, float3 tangent, float3 bitangent, float dispersion, float N, float NLevels, float rX)
{
    float index = (rX * dispersion) % N;

    //index = (frame_count) % N;
    float cumulativePoints = 1.0f;
    float level = 1.0f;
    float c = 1.0f;
    while (c < index) {
        c = cumulativePoints + level * LEVEL_RATIO;
        cumulativePoints = c;
        level++;
    };
    level--;

    float pointsAtLevel = 1.0f + level * LEVEL_RATIO;

    // Calculate local index within the current level
    float localIndex = index - cumulativePoints;
    float phi = (level * M_PI * 0.8f) / NLevels;
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

    return GetPolarCoordinates(normalize(dir + globalRay));
}

#endif
bool IsLowEnergy(float pdf[MAX_RAYS], uint len) {
        
    float total_enery = 0.0f;

    for (uint i = 0; i < len; ++i) {
        total_enery += pdf[i];
    }
    float threshold = len * 0.01f;

    return (total_enery < threshold);
}

#define NTHREADS 32

[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    float2 dimensions;
    float2 ray_map_dimensions;
    {
        uint w, h;
        ray0.GetDimensions(w, h);
        ray_map_dimensions.x = w;
        ray_map_dimensions.y = h;
    }
    float x = (float)DTid.x;
    float y = (float)DTid.y;

    float rayMapRatio = divider;
    dimensions = ray_map_dimensions / divider;
    float2 pixel = float2(x, y);
    float2 rpixel = pixel * rayMapRatio;
    float2 ray_pixel = round(rpixel);

    RaySource ray_source = fromColor(ray0[ray_pixel], ray1[ray_pixel]);
    bool end = (ray_source.reflex <= Epsilon || dist2(ray_source.normal) <= Epsilon);

    float3 orig_pos = ray_source.orig.xyz;
    float toCamDistance = dist2(orig_pos - cameraPosition);
   
    float3 tangent;
    float3 bitangent;
    Ray ray = GetReflectedRayFromSource(ray_source, cameraPosition);

    float pdf_cache[MAX_RAYS];

    float4 prev_pos = mul(prev_position_map[ray_pixel], view_proj);
    prev_pos.x /= prev_pos.w;
    prev_pos.y /= -prev_pos.w;
    prev_pos.xy = (prev_pos.xy + 1.0f) * dimensions.xy * 0.5f;

    UnpackRays(restir_pdf_0[prev_pos.xy], RAY_W_SCALE, pdf_cache);
    uint i = 0;

    end = end || (dist2(ray.dir) <= Epsilon);
    [branch]
    if (end)
    {
        return;
    }
    
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
    
    float wis[MAX_RAYS];
    int wis_size = 0;

    uint last_wi = MAX_RAYS + 1;

    for (i = 0; i < MAX_RAYS; i++) {
        pdf_cache[i] = max(pdf_cache[i], RAY_W_BIAS);
    }

    uint restir_mask = 0;
    float w_pixel = max(restir_w_0[pixel], RAY_W_BIAS * MAX_RAYS);

    float unordered_wis[MAX_RAYS];
    for (i = 0; i < MAX_RAYS; ++i) {
        uint wi = GetRayIndex(pdf_cache, w_pixel, i);
        if (last_wi != wi) {
            unordered_wis[wis_size] = wi;
            wis_size++;
            last_wi = wi;
        }
    }

    for (i = 0; i < 3 && i < wis_size; ++i) {
        float max_wi = 0.0f;
        uint max_wi_pos = 0;
        uint max_unordered_wis_pos = 0;
        for (int j = 0; j < wis_size; ++j) {
            if (max_wi < pdf_cache[unordered_wis[j]]) {
                max_wi_pos = unordered_wis[j];
                max_wi = pdf_cache[max_wi_pos];
                max_unordered_wis_pos = j;
            }
        }

        wis[i] = max_wi_pos;
        unordered_wis[max_unordered_wis_pos] = 0.0f;
        restir_mask = (restir_mask << 4) | max_wi_pos;
    }

    for (i = wis_size; i < 3; ++i) {
        wis[i] = 0xF;
        restir_mask = (restir_mask << 4) | 0xF;
    }

    //Add scan line in ray 4
    uint scan_ray = frame_count% MAX_RAYS;
    bool found;
    do {
        found = false;
        for (i = 0; i < 3; ++i) {
            if (wis[i] == scan_ray) {
                scan_ray = (scan_ray + 1) % MAX_RAYS;
                found = true;
                break;
            }
        }

    } while (found);
    wis[3] = scan_ray;
    restir_mask = (restir_mask << 4) | scan_ray;

    float4 color_diffuse = float4(0.0f, 0.0f, 0.0f, 1.0f);
    float offset = ((pixel.x) % kernel_size) * MAX_RAYS + ((pixel.y) % kernel_size) * stride;
    float offset2 = space_size;
   
    float2 ray_input[4];
    for (i = 0; i < 4; ++i) {
        uint wi = wis[i];
        if (wi < 0xF) {
            float n = fmod(offset + (float)wi * offset2, N);
            ray_input[i] = GenerateHemisphereRay(normal, tangent, bitangent, 1.0f, N, level, n);
        }
        else {
            ray_input[i] = float2(MAX_RAY_POLAR_DIR, MAX_RAY_POLAR_DIR);
        }
    }
    uint4 data = Pack4Float2ToI16(ray_input, MAX_RAY_POLAR_DIR);
    ray_inputs[pixel] = data;
    restir_pdf_mask[pixel] = restir_mask;
}
