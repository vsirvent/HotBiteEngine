#include "../Common/ShaderStructs.hlsli"
#include "../Common/Utils.hlsli"

cbuffer externalData : register(b0)
{
    float RATIO;
    matrix view;
    matrix projection;
    float3 cameraPosition;
    uint debug;
}

Texture2D<float4> input : register(t0);
RWTexture2D<float4> output : register(u0);
Texture2D<float4> positions : register(t2);
Texture2D<float4> normals : register(t3);
Texture2D<float4> prev_output : register(t4);
Texture2D<float2> motion_texture : register(t5);
Texture2D<float4> prev_position_map: register(t6);

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 pixel = float2(DTid.x, DTid.y);

    uint2 input_dimensions;
    uint2 info_dimensions;
    {
        uint w = 0;
        uint h = 0;
        uint w2 = 0;
        uint h2 = 0;
        input.GetDimensions(w, h);
        output.GetDimensions(w2, h2);
        input_dimensions.x = min(w, w2);
        input_dimensions.y = min(h, h2);

        positions.GetDimensions(w, h);
        info_dimensions.x = w;
        info_dimensions.y = h;
    }
    float2 infoRatio = info_dimensions / input_dimensions;

    float2 info_pixel = round(pixel * infoRatio);
    float3 p0_position = positions[info_pixel].xyz;
    float3 p0_normal = normals[info_pixel].xyz;

    if (false) {
        output[pixel] = input[pixel];
        return;
    }

#define KERNEL_SIZE 2
    float4 c0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float total_w = 0.0f;
    float w = 0.0f;
    float pixelMaxDist = 0.0f;
    float worldMaxDist = 0.0f;
    
    int x;
    int y;
    float2 middle_pixel = GetCloserPixel(pixel, RATIO);
    for (x = -KERNEL_SIZE; x <= KERNEL_SIZE; ++x) {
        for (y = -KERNEL_SIZE; y <= KERNEL_SIZE; ++y) {
            float2 input_p = GetCloserPixel(middle_pixel + float2(x, y) * RATIO, RATIO);
            float dist = dist2(input_p - pixel);
            pixelMaxDist = max(pixelMaxDist, dist);
    
            float2 p1_info_pixel = round(input_p * infoRatio);
            float3 p1_position = positions[p1_info_pixel].xyz;
            float world_dist = dist2(p1_position - p0_position);
            worldMaxDist = max(worldMaxDist, world_dist);
        }
    }

    for (x = -KERNEL_SIZE; x <= KERNEL_SIZE; ++x) {
        for (y = -KERNEL_SIZE; y <= KERNEL_SIZE; ++y) {
            float2 input_p = GetCloserPixel(middle_pixel + float2(x, y) * RATIO, RATIO);
            float dist = dist2(input_p - pixel);
            w = (1.0f - dist / pixelMaxDist);

            float2 p1_info_pixel = round(input_p * infoRatio);
            float3 p1_normal = normals[p1_info_pixel].xyz;
            float n = saturate(dot(p1_normal, p0_normal));
            
            float3 p1_position = positions[p1_info_pixel].xyz;
            float world_dist = dist2(p1_position - p0_position);
            float ww = pow(n, 20.0f / infoRatio)* (1.0f - world_dist / worldMaxDist);
            w *= ww;

            c0 += input[input_p] * w;
            total_w += w;
        }
    }
  
    if (total_w > Epsilon) {
        c0 /= total_w;
    }
    else {
        c0 *= 0.0f;
    }

#if 0
    float motion = 0.0f;
    matrix worldViewProj = mul(view, projection);
    float4 prev_pos = mul(prev_position_map[info_pixel], worldViewProj);
    prev_pos.x /= prev_pos.w;
    prev_pos.y /= -prev_pos.w;
    prev_pos.xy = (prev_pos.xy + 1.0f) * info_dimensions.xy / 2.0f;
    float m = length(motion_texture[prev_pos.xy].xy);
    if (m > motion) {
        motion = m;
    }
    float2 mvector = motion_texture[info_pixel].xy;
    if (mvector.x == -FLT_MAX) {
        motion = FLT_MAX;
    }
    else {
        m = length(motion_texture[info_pixel].xy);
        if (m > motion) {
            motion = m;
        }
    }

    float4 prev_color = prev_output[pixel];
    w = saturate(0.8f - motion * 50.0f);
    output[pixel] = prev_color * w + c0 * (1.0f - w);
#else
    output[pixel] = c0;
#endif
}