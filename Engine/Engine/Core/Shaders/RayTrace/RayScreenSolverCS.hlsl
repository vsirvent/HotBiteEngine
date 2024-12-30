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
    uint type;
    float hiz_ratio;
    uint divider;
    int kernel_size;
    float3 cameraPosition;
    matrix view;
    matrix projection;
    matrix inv_projection;

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

#define HIZ_TEXTURES 5
#define TYPE_DI 1
#define TYPE_GI 2

Texture2D<float> hiz_textures[HIZ_TEXTURES];

struct Line {
    float3 P0; // Point on the line
    float3 d;  // Direction vector of the line
};

struct Plane {
    float3 Q; // Point on the plane
    float3 n; // Normal vector of the plane
};

float3 IntersectLinePlane(Line l, Plane p) {
    float3 P0 = l.P0;
    float3 d = l.d;
    float3 Q = p.Q;
    float3 n = p.n;

    float denom = dot(n, d);
    denom = max(abs(denom), 1e-6) * sign(denom);
    float t = dot(n, Q - P0) / denom;
    return (P0 + t * d);
}

float GetRayDepth(Plane rayPlane, float2 grid_pos, out float3 intersection_point) {
    float4 H = float4(grid_pos.x * 2.0f - 1.0f, (1.0f - grid_pos.y) * 2.0f - 1.0f, 0.0f, 1.0f);
    float4 D = mul(H, inv_projection);
    float4 eyepos = D / D.w;

    Line eyeLine;
    eyeLine.P0 = eyepos.xyz;
    eyeLine.d = eyepos.xyz;
    intersection_point = IntersectLinePlane(eyeLine, rayPlane);
    return length(intersection_point);
}

float2 GetNextGrid(float2 dir, float2 pixel) {
    float2 p0 = pixel;
    float2 invDir = 1.0f / dir;
    float2 currentGrid = floor(p0);
    float2 nextGrid;
    // Calculate initial intersection points with the grid lines
    nextGrid.x = currentGrid.x + sign(dir.x);
    nextGrid.y = currentGrid.y + sign(dir.y);;

    // Calculate the step size to the next grid for x and y
    float2 tMax;
    tMax.x = (nextGrid.x - p0.x) * invDir.x;
    tMax.y = (nextGrid.y - p0.y) * invDir.y;

    if (tMax.x < tMax.y) {
        return float2(nextGrid.x, p0.y + (nextGrid.x - p0.x) * dir.y / dir.x);
    }
    else {
        return float2(p0.x + (nextGrid.y - p0.y) * dir.x / dir.y, nextGrid.y);
    }
}

float GetHiZ(uint level, float2 pixel) {
    switch (level) {
    case 0: return hiz_textures[0][pixel];
    case 1: return hiz_textures[1][pixel];
    case 2: return hiz_textures[2][pixel];
    case 3: return hiz_textures[3][pixel];
    case 4: return hiz_textures[4][pixel];
    }
    return FLT_MAX;
}

float2 GetColor(Ray ray, float2 depth_dimensions, float2 output_dimensions, out float z_diff, out float hit_distance) {

    float4 pixel0 = mul(ray.orig, projection);
    pixel0 /= pixel0.w;
    pixel0.y *= -1.0f;
    pixel0.xy = (pixel0.xy + 1.0f) * 0.5f;
    float2 pStartProj = pixel0.xy;
    
    float4 p1 = ray.orig;
    p1.xyz += ray.dir;

    float4 pixel1 = mul(p1, projection);
    pixel1 /= pixel1.w;
    pixel1.y *= -1.0f;
    pixel1.xy = (pixel1.xy + 1.0f) * 0.5f;
    float2 pEndProj = pixel1.xy;
 
    float2 dir = normalize((pixel1.xy - pStartProj) * depth_dimensions);

  
    float2 screen_pixel = pixel0.xy * depth_dimensions;
    float3 color = float3(0.0f, 0.0f, 0.0f);
    int current_level = HIZ_TEXTURES - 1;
    float current_divider = pow(hiz_ratio, current_level);

    float2 grid_pixel = screen_pixel / current_divider;
    float2 grid_pos = pixel0.xy;
    float2 last_valid_grid_pos = grid_pos;

    float grid_high_z = 0.0f;

    float3 intersection_point;

    float ray_z = 0.0f;

    float n = 0.0f;

    float3 up = float3(0.0f, 1.0f, 0.0f);
    float3 normal = normalize(cross(up, ray.dir));
    float2 grid_size = current_divider / depth_dimensions;

    Plane rayPlane;
    rayPlane.Q = ray.orig.xyz;
    rayPlane.n = normal;

    while (current_level >= 0) {
        //Calculate ray z, grid_pos in NDC coords

        grid_pos = grid_pixel * grid_size;
        if (!ValidUVCoord(grid_pos)) {
            z_diff = FLT_MAX;
            hit_distance = FLT_MAX;
            return float2(-1, -1);
        }
        grid_high_z = GetHiZ(current_level, grid_pixel);
        ray_z = GetRayDepth(rayPlane, grid_pos, intersection_point);
       if (grid_high_z <= ray_z) {
            current_level--;
            current_divider = pow(hiz_ratio, current_level);
            grid_pos = last_valid_grid_pos;
            grid_size = current_divider / depth_dimensions;
            grid_pixel = (grid_pos * depth_dimensions) / current_divider;
        }
        else {
            last_valid_grid_pos = grid_pos;
            grid_pixel = GetNextGrid(dir, grid_pixel);
        }

    }

    z_diff = abs(ray_z - grid_high_z);
    hit_distance = length(intersection_point - ray.orig.xyz);
    
    return last_valid_grid_pos;
}

//Max diff depends on the distance gap between adyacent pixels and distance to camera
float GetMaxDiff(float2 uv, float2 dimension) {
    float2 texCoords = uv * dimension;

    int2 p00 = (int2)floor(texCoords);
    int2 p11 = (int2)ceil(texCoords);
    int2 p01 = int2(p00.x, p11.y);
    int2 p10 = int2(p11.x, p00.y);

    p00 += int2(-1, -1);
    p11 += int2(1, 1);
    p01 += int2(-1, 1);
    p10 += int2(1, -1);

    float z00 = (hiz_textures[0][p00]);
    float z11 = (hiz_textures[0][p11]);
    float z01 = (hiz_textures[0][p10]);
    float z10 = (hiz_textures[0][p01]);

    float d00_11 = abs(z00 - z11);
    float d00_10 = abs(z00 - z10);
    float d00_01 = abs(z00 - z01);
    float d11_01 = abs(z11 - z01);
    float d11_10 = abs(z11 - z10);
    float d01_10 = abs(z01 - z10);

    float diff = min(max(d00_11, max(d00_10, max(d00_01, max(d11_01, max(d11_10, d01_10))))), 0.299f);
    return saturate(0.3f - diff);
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
    if (type == TYPE_DI) {
        ratio = (1.0f - ray_source.dispersion);
    }
    else {
        ratio = ray_source.dispersion;
    }

    switch(type)
    {
        //Direct Illumination
        case 1: {
            [unroll]
            //Only work with 1 ray for DI
            float reflex_ratio = (1.0f - ray_source.dispersion);
            for (int r = 0; r < 1; ++r) {
                z_diff = FLT_MAX;
                Ray ray = GetRayInfoFromSourceWithNoDir(ray_source);
                if (abs(ray_input[r].x) < MAX_RAY_POLAR_DIR && dist2(ray_input[r]) > Epsilon) {
                    ray.dir = GetCartesianCoordinates(ray_input[r]);
                    ray.dir = normalize(mul(ray.dir, (float3x3)view));
                    ray.orig = mul(ray.orig, view);
                    ray.orig /= ray.orig.w;
                    ray.orig.xyz += ray.dir * 0.5f;
                    float reflex_ratio = (1.0f - ray_source.dispersion);
                    color_uv = GetColor(ray, ray_map_dimensions, dimensions, z_diff, hit_distance);

                    float max_diff = GetMaxDiff(color_uv, ray_map_dimensions);
                    if (z_diff < max_diff && ValidUVCoord(color_uv)) {
                        float4 c = GetInterpolatedColor(color_uv, colorTexture, ray_map_dimensions);
                        float4 l = GetInterpolatedColor(color_uv, lightTexture, ray_map_dimensions);
                        float4 b = GetInterpolatedColor(color_uv, bloomTexture, ray_map_dimensions);
                        ray_input[r] = float2(FLT_MAX, FLT_MAX);
                        final_color += ((c * l) * reflex_ratio * ray_source.opacity);
                        n++;
                    }
                
                }
            }
            output[pixel] = float4(sqrt(final_color), 1.0f);
            break;
        }
        //Global Illumination
        case 2: {
#if 1
            uint i = 0;
            float pdf_cache[MAX_RAYS];
            UnpackRays(restir_pdf_0[pixel], RAY_W_SCALE, pdf_cache);

            float wis[MAX_RAYS];
            int wis_size = 0;
            uint mask = restir_pdf_mask[pixel];
            for (i = 0; i < 4; ++i) {
                uint wi = (mask & 0xF000) >> 12;
                if (wi < 0xFF) {
                    wis[wis_size++] = wi;
                }
                else {
                    break;
                }
                mask <<= 4;
            }
            
            for (i = 0; i < wis_size; ++i) {
                z_diff = FLT_MAX;
                Ray ray = GetRayInfoFromSourceWithNoDir(ray_source);
                if (abs(ray_input[i].x) < MAX_RAY_POLAR_DIR && dist2(ray_input[i]) > Epsilon) {
                    ray.dir = GetCartesianCoordinates(ray_input[i]);
                    ray.dir = normalize(mul(ray.dir, (float3x3)view));
                    ray.orig = mul(ray.orig, view);
                    ray.orig /= ray.orig.w;
                    ray.orig.xyz += ray.dir * 0.5f;
                    float reflex_ratio = (1.0f - ray_source.dispersion);
                    color_uv = GetColor(ray, ray_map_dimensions, dimensions, z_diff, hit_distance);
                    uint wi = wis[i];
                    float max_diff = GetMaxDiff(color_uv, ray_map_dimensions);
                    if (z_diff < max_diff && ValidUVCoord(color_uv)) {
                        float4 c = GetInterpolatedColor(color_uv, colorTexture, ray_map_dimensions);
                        float4 l = GetInterpolatedColor(color_uv, lightTexture, ray_map_dimensions);
                        float4 b = GetInterpolatedColor(color_uv, bloomTexture, ray_map_dimensions);
                      
                        float diff_ratio = (z_diff / 0.05f);
                        ray_input[i] = float2(FLT_MAX, FLT_MAX);
                        float3 color = ((c * l) * ray_source.opacity) + b;
                        
                        pdf_cache[wi] = RAY_W_BIAS + length(color);
                        final_color += color;
                        n++;
                    }
                    else {
                        pdf_cache[wi] = RAY_W_BIAS;
                    }

                    if (hit_distance > 5.0f) {
                        ray_input[i] = float2(FLT_MAX, FLT_MAX);
                    }
                }
            }

            n = max(n, 1);
            final_color = (final_color  / n);
            restir_pdf_1[pixel] = PackRays(pdf_cache, RAY_W_SCALE);
            output[pixel] = float4(final_color, 1.0f);
#endif
            break;
        }
    }

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
