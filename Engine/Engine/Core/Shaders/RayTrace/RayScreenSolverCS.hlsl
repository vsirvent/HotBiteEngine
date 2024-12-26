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
    float divider;
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

RWTexture2D<float4> ray_inputs: register(u0);
RWTexture2D<float4> output : register(u1);
RWTexture2D<uint> tiles_output: register(u2);

Texture2D<float4> ray0: register(t1);
Texture2D<float4> ray1: register(t2);
Texture2D colorTexture: register(t3);
Texture2D lightTexture: register(t4);
Texture2D bloomTexture: register(t5);

Texture2D<float> hiz_textures[4];

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

    if (abs(denom) < 1e-6) {
        return FLT_MAX;
    }

    float t = dot(n, Q - P0) / denom;
    return (P0 + t * d);
}

float GetRayDepth(Plane rayPlane, float2 grid_pos) {
    float4 H = float4(grid_pos.x * 2.0f - 1.0f, (1.0f - grid_pos.y) * 2.0f - 1.0f, 0.0f, 1.0f);
    float4 D = mul(H, inv_projection);
    float4 eyepos = D / D.w;

    Line eyeLine;
    eyeLine.P0 = eyepos.xyz;
    eyeLine.d = eyepos.xyz;
    
    return length(IntersectLinePlane(eyeLine, rayPlane));
}

float2 GetNextGrid(float2 dir, float2 pixel) {
    float2 p0 = pixel;
    
    if (abs(dir.x) <= 0.00001f) {
        float2 currentGrid = floor(p0);
        float nextGrid = currentGrid.y + (dir.y > 0 ? 1 : -1);
        return float2(currentGrid.x, nextGrid);
    }
    else if (abs(dir.y) <= 0.00001f) {
        float2 currentGrid = floor(p0);
        float nextGrid = currentGrid.x + (dir.x > 0 ? 1 : -1);
        return float2(nextGrid, currentGrid.y);
    }
    else {        
        float2 currentGrid = floor(p0);
        float2 nextGrid;
        // Calculate initial intersection points with the grid lines
        nextGrid.x = currentGrid.x + (dir.x > 0 ? 1 : -1);
        nextGrid.y = currentGrid.y + (dir.y > 0 ? 1 : -1);

        // Calculate the step size to the next grid for x and y
        float2 tMax;
        tMax.x = (nextGrid.x - p0.x) / dir.x;
        tMax.y = (nextGrid.y - p0.y) / dir.y;
       
        if (tMax.x < tMax.y) {
            return float2(nextGrid.x, p0.y + (nextGrid.x - p0.x) * dir.y / dir.x);
        }
        else {
            return float2(p0.x + (nextGrid.y - p0.y) * dir.x / dir.y, nextGrid.y);
        }
    }
}

float GetHiZ(uint level, float2 pixel) {
    switch (level) {
    case 0: return hiz_textures[0][pixel];
    case 1: return hiz_textures[1][pixel];
    case 2: return hiz_textures[2][pixel];
    case 3: return hiz_textures[3][pixel];
    }
    return FLT_MAX;
}

float2 GetColor(Ray ray, float2 depth_dimensions, float2 output_dimensions, out uint hit) {
    
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

    float2 dir = normalize((pEndProj - pStartProj) * depth_dimensions);

    float2 screen_pixel = pixel0.xy * depth_dimensions;
    float3 color = float3(0.0f, 0.0f, 0.0f);
    int current_level = 1;
    float current_divider = pow(divider, current_level);

    float2 grid_pixel = screen_pixel / current_divider;
    float2 grid_pos = pixel0.xy;
    float2 prev_grid_pos;

    float grid_high_z = 0.0f;
    float ray_z = 0.0f;
        
    float n = 0.0f;

    float3 up = float3(0.0f, 1.0f, 0.0f);
    float3 normal = normalize(cross(up, ray.dir));
    Plane rayPlane;
    rayPlane.Q = ray.orig.xyz;
    rayPlane.n = normal;
    while (true) {
        prev_grid_pos = grid_pos;
        
        //Calculate ray z, grid_pos in NDC coords
        grid_pos = (grid_pixel * current_divider) / depth_dimensions;
        
        grid_high_z = GetHiZ(current_level, grid_pixel);
        ray_z = GetRayDepth(rayPlane, grid_pos);

        if (grid_high_z < ray_z) {
            current_level--;
            if (current_level < 0) {
                break;
            }
            current_divider = pow(divider, current_level);
            grid_pos = prev_grid_pos;
            //Calculate new pixel position in the new level from the previous valid grid       
            grid_pixel = grid_pos * depth_dimensions / current_divider;
        }
        else {
            grid_pixel = GetNextGrid(dir, grid_pixel);
        }
        
        if (grid_pixel.x < 0 || grid_pixel.x >= depth_dimensions.x / (current_divider) ||
            grid_pixel.y < 0 || grid_pixel.y >= depth_dimensions.y / (current_divider)) {
            hit = 0;
            return float2(-1, -1);
        }
    }
       
    hit = (abs(ray_z - grid_high_z) < 0.1f);
    return grid_pos;
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

    float2 ray_pixel = round(pixel * rayMapRatio);

    RaySource ray_source = fromColor(ray0[ray_pixel], ray1[ray_pixel]);

    float2 color_uv = float2(0.0f, 0.0f);
    uint hit = 0;
    Ray ray = GetRayInfoFromSourceWithNoDir(ray_source);

    //Get rays to be solved in the pixel
    float4 ray_input_dirs = ray_inputs[pixel];
    [branch]
    if (ray_input_dirs.x < 10e10) {
        if (dist2(ray_input_dirs.xy) > Epsilon) {
            ray.dir = GetCartesianCoordinates(ray_input_dirs.xy);
            ray.dir = normalize(mul(ray.dir, (float3x3)view));

            ray.orig = mul(ray.orig, view);
            ray.orig /= ray.orig.w;
            ray.orig.xyz += ray.dir * 0.05f;
            float reflex_ratio = (1.0f - ray_source.dispersion);
            
            color_uv = GetColor(ray, ray_map_dimensions, dimensions, hit);
            if (hit != 0) {
                ray_input_dirs.xy = float2(10e11, 10e11);
            }
        }
    }

    if (hit != 0) {
        [unroll]
        for (int x = -2; x <= 2; ++x) {
            [unroll]
            for (int y = -2; y <= 2; ++y) {
                int2 p = pixel / kernel_size + int2(x, y);
                tiles_output[p] = hit;
            }
        }
        color_uv *= ray_map_dimensions;
        float4 c = colorTexture[color_uv];
        float4 l = lightTexture[color_uv];
        float4 b = bloomTexture[color_uv];
        output[pixel] = float4(color_uv, 0.0f, 1.0f);// c* l + b;
        //output[pixel] = c * l + b;
        ray_inputs[pixel] = ray_input_dirs;
    }
}
    