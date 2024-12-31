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

#define HIZ_TEXTURES 5

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

float GetRayDepth(in matrix inv_projection, Plane rayPlane, float2 grid_pos, out float3 intersection_point) {
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

float GetHiZ(Texture2D<float> hiz_textures[5], uint level, float2 pixel) {
    switch (level) {
    case 0: return hiz_textures[0][pixel];
    case 1: return hiz_textures[1][pixel];
    case 2: return hiz_textures[2][pixel];
    case 3: return hiz_textures[3][pixel];
    case 4: return hiz_textures[4][pixel];
    }
    return FLT_MAX;
}

float2 GetColor(Ray ray, in matrix projection, in matrix inv_projection, Texture2D<float> hiz_textures[HIZ_TEXTURES], float hiz_ratio, float2 depth_dimensions, float2 output_dimensions, out float z_diff, out float hit_distance) {

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
        grid_high_z = GetHiZ(hiz_textures, current_level, grid_pixel);
        ray_z = GetRayDepth(inv_projection, rayPlane, grid_pos, intersection_point);
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
float GetMaxDiff(in Texture2D<float> hiz_texture, float2 uv, float2 dimension) {
    float2 texCoords = uv * dimension;

    int2 p00 = (int2)floor(texCoords);
    int2 p11 = (int2)ceil(texCoords);
    int2 p01 = int2(p00.x, p11.y);
    int2 p10 = int2(p11.x, p00.y);

    p00 += int2(-1, -1);
    p11 += int2(1, 1);
    p01 += int2(-1, 1);
    p10 += int2(1, -1);

    float z00 = (hiz_texture[p00]);
    float z11 = (hiz_texture[p11]);
    float z01 = (hiz_texture[p10]);
    float z10 = (hiz_texture[p01]);

    float d00_11 = abs(z00 - z11);
    float d00_10 = abs(z00 - z10);
    float d00_01 = abs(z00 - z01);
    float d11_01 = abs(z11 - z01);
    float d11_10 = abs(z11 - z10);
    float d01_10 = abs(z01 - z10);

    float diff = min(max(d00_11, max(d00_10, max(d00_01, max(d11_01, max(d11_10, d01_10))))), 0.299f);
    return saturate(0.3f - diff);
}
