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

Texture2D<float4> color_texture: register(t0);
Texture2D<float4> ray0: register(t1);
Texture2D<float4> ray1: register(t2);
Texture2D<float> hiz_textures[4];

float2 GetScreenDir(Ray ray, float2 pixel) {
    //Advance ray in the 3d view space by a dir unit
    float4 p1 = ray.orig;
    p1.rgb += ray.dir;

    //Convert to screen
    float4 pixel1 = mul(p1, projection);
    pixel1.xy /= pixel1.w;
    pixel1.y *= -1.0f;
    pixel1.xy = (pixel1.xy + 1.0f) * 0.5f;

    //Obtain direction in screen space
    float2 dir = normalize(pixel1.xy - pixel);
    return dir;
}

float GetRayDepth(Ray ray, float2 grid_pos) {
    float4 H = float4(grid_pos.x * 2.0f - 1.0f, (1.0f - grid_pos.y) * 2.0f - 1.0f, 0.0f, 1.0f);
    float4 D = mul(H, inv_projection);
    float4 eyepos = D / D.w;
    
    float dir2 = normalize(eyepos).z;

    //Line 1: z = ray.dir.z * step + ray.orig.z
    //Line 2: z = dir2 * step + ray.orig.z
    float step = 1.0f / (dir2 - ray.dir.z);
    float z = ray.dir * step + ray.orig.z;
    return z;
}


float2 GetNextGrid(float2 invDir, float2 pixel) {

    float2 p0 = pixel;

    float2 currentGrid = floor(p0);
    float2 nextGrid;
    // Calculate initial intersection points with the grid lines
    nextGrid.x = currentGrid.x + sign(invDir.x);
    nextGrid.y = currentGrid.y + sign(invDir.y);

    // Calculate the step size to the next grid for x and y
    float2 tMax;
    tMax.x = (nextGrid.x - p0.x) * invDir.x;
    tMax.y = (nextGrid.y - p0.y) * invDir.y;

    if (tMax.x < tMax.y) {
        return float2(nextGrid.x, currentGrid.y);
    }
    else if (tMax.x > tMax.y) {
        return float2(currentGrid.x, nextGrid.y);
    }
    else {
        return nextGrid;
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

float3 GetColor(Ray ray, float2 pixel, float2 depth_dimensions, out uint hit) {
    float2 screen_pixel = pixel * depth_dimensions;
    float3 color = float3(0.0f, 0.0f, 0.0f);
    uint current_level = 3;
    float current_divider = pow(divider, current_level);
    float2 dir = GetScreenDir(ray, pixel);
    float2 invDir = 1.0 / dir;
    
    float2 grid_pixel = screen_pixel / current_divider;

    float grid_high_z = 0.0f;
    float ray_z = 0.0f;

    while (current_level > 0) {
        grid_pixel = GetNextGrid(invDir, grid_pixel);
        grid_high_z = GetHiZ(current_level, grid_pixel);
        
        //Calculate ray z
        float2 grid_pos = (grid_pixel * current_divider) / depth_dimensions;
        float ray_z = GetRayDepth(ray, grid_pos);

        if (grid_high_z < ray_z) {
            current_level--;
            if (current_level == 0) {
                break;
            }
            current_divider = pow(divider, current_level);
            //Calculate new pixel position in the new level
            float2 p0_scaled = screen_pixel / current_divider;
            float2 g00 = grid_pixel * divider;
            float2 g10 = float2(grid_pixel.x + 1, grid_pixel.y    ) * divider;
            float2 g01 = float2(grid_pixel.x    , grid_pixel.y + 1) * divider;
            float2 g11 = float2(grid_pixel.x + 1, grid_pixel.y + 1) * divider;

            float d00 = dist2(p0_scaled - g00);
            float d10 = dist2(p0_scaled - g10);
            float d01 = dist2(p0_scaled - g01);
            float d11 = dist2(p0_scaled - g11);

            float min_dist = min(d00, min(d10, min(d01, d11)));
            if (min_dist == d00) {
                grid_pixel = g00;
            }
            else if (min_dist == d10) {
                grid_pixel = g10;
            }
            else if (min_dist == d01) {
                grid_pixel = g01;
            }
            else if (min_dist == d11) {
                grid_pixel = g11;
            }
        }
        
        if (grid_pixel.x < 0 || grid_pixel.x >= depth_dimensions.x / (current_divider) ||
            grid_pixel.y < 0 || grid_pixel.y >= depth_dimensions.y / (current_divider)) {
            hit = 0;
            return color;
        }
    }

    hit = (abs(ray_z - grid_high_z) < 0.01f);
    if (hit > 0) {
        color = color_texture[grid_pixel].rgb;
    }
    return color;
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

    float4 color_reflex = float4(0.0f, 0.0f, 0.0f, 1.0f);
    uint hit = 0;
    Ray ray = GetRayInfoFromSourceWithNoDir(ray_source);

    //Get rays to be solved in the pixel
    float4 ray_input_dirs = ray_inputs[pixel];
    [branch]
    if (ray_input_dirs.x < 10e10) {
        if (dist2(ray_input_dirs.xy) <= Epsilon) {
            color_reflex.rgb = float3(1.0f, 0.0f, 0.0f);
        }
        else {
            ray.dir = GetCartesianCoordinates(ray_input_dirs.xy);
            //We work in view space
            ray.dir = normalize(mul(ray.dir, (float3x3)view));
            ray.orig.xyz = mul(ray.orig, view).xyz + ray.dir * 0.01f;
            
            float reflex_ratio = (1.0f - ray_source.dispersion);
            color_reflex.rgb += GetColor(ray, pixel/dimensions, ray_map_dimensions, hit);
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
        output[pixel] = color_reflex;
        ray_inputs[pixel] = ray_input_dirs;
    }
}
    