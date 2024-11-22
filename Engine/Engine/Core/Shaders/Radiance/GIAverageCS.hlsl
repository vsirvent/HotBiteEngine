#include "../Common/ShaderStructs.hlsli"
#include "../Common/Utils.hlsli"

cbuffer externalData : register(b0)
{
    uint debug;
    uint type;
    matrix view;
    matrix projection;
    float3 cameraPosition;
    uint frame_count;
    int kernel_size;
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

    if (debug == 1) { 
        output[pixel] = input[pixel];
        return;
    }

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
    float2 rpixel = pixel * infoRatio;
    //rpixel.x += frame_count % (infoRatio.x * 0.5f);
    //rpixel.y += frame_count % (infoRatio.y * 0.5f) / 2.0f;
    

    float2 info_pixel = round(rpixel);
    float3 p0_position = positions[info_pixel].xyz;
    float3 p0_normal = normals[info_pixel].xyz;

    float pixelMaxDist = 0.0f;
    float worldMaxDist = 0.0f;

    int x;
    int y;

    float sigma = 1.0f;
    float total_w = 0.0f;
    float ww = 1.0f;
    float4 c = float4(0.0f, 0.0f, 0.0f, 0.0f);

    int full_kernel = 2 * kernel_size + 1;
    int k = kernel_size + full_kernel * 2;

#if 1
    //Convolution type
    float2 dir = lerp(float2(1.0f, 0.0f), float2(0.0f, 1.0f), step(1.5, type));
    for (x = -k; x <= k; ++x) {
        int2 p = pixel + x * dir;

        float2 p1_info_pixel = round(p * infoRatio);
        ww = 1.0f;
        if (abs(x) > k) {
            float3 p1_position = positions[p1_info_pixel].xyz;
            if (p1_position.x == FLT_MAX) continue;
            float world_dist = dist2(p1_position - p0_position);
            ww = exp(-world_dist / (2.0f * sigma * sigma));
        }

        float3 p1_normal = normals[p1_info_pixel].xyz;
        float n = saturate(dot(p1_normal, p0_normal));
        ww *= pow(n, max(5.0f / infoRatio.x, 1.0f));

        c += input[p] * ww;
        total_w += ww;
    }
    static const float epsilon = 10e-4;
    c = c * step(epsilon, total_w);
    c = c / max(total_w, epsilon);

#if 1
    if (type == 1) {
        output[pixel] = c;
    }
    else {
        matrix worldViewProj = mul(view, projection);
        float4 prev_pos = mul(prev_position_map[info_pixel], worldViewProj);
        prev_pos.x /= prev_pos.w;
        prev_pos.y /= -prev_pos.w;
        prev_pos.xy = (prev_pos.xy + 1.0f) * info_dimensions.xy / 2.0f;
#if 0
        float motion = length(motion_texture[prev_pos.xy].xy);        
        float2 mvector = motion_texture[info_pixel].xy;
        if (mvector.x == -FLT_MAX) {
            motion = 0.0f;
        }
        else {
            motion = length(mvector);            
        }
        if (motion < 0.005f) {
            prev_pos.xy = pixel.xy;
        }
        else {
            prev_pos.xy /= infoRatio;
        }
        float w = saturate(0.8f - motion * 100.0f);
#else
        prev_pos.xy /= infoRatio;
        float w = 0.3f;
#endif
        float4 prev_color = prev_output[round(prev_pos.xy)];
        output[pixel] = prev_color * w + c * (1.0f - w);
    }
#else
    output[pixel] = c;
#endif

#else
    //Single pass type
    for (x = -k; x <= k; ++x) {
        for (y = -k; y <= k; ++y) {
            int2 p = pixel + int2(x, y);
            float2 p1_info_pixel = round(p * infoRatio);
            float3 p1_position = positions[p1_info_pixel].xyz;
            if (p1_position.x == FLT_MAX) continue;

            float world_dist = dist2(p1_position - p0_position);
            ww = exp(-world_dist / (2.0f * sigma * sigma));

            float3 p1_normal = normals[p1_info_pixel].xyz;
            float n = saturate(dot(p1_normal, p0_normal));
            ww *= pow(n, 5.0f / infoRatio.x);
            c += input[p] * ww;
            total_w += ww;
        }
    }
    static const float epsilon = 10e-4;
    c = c * step(epsilon, total_w);
    c = c / max(total_w, epsilon);

#if 1
    matrix worldViewProj = mul(view, projection);
    float4 prev_pos = mul(prev_position_map[info_pixel], worldViewProj);
    prev_pos.x /= prev_pos.w;
    prev_pos.y /= -prev_pos.w;
    prev_pos.xy = (prev_pos.xy + 1.0f) * info_dimensions.xy / 2.0f;
    prev_pos.xy /= infoRatio;
    float4 prev_color = prev_output[floor(prev_pos.xy)];
    float w = 0.5f;
    output[pixel] = prev_color * w + c * (1.0f - w);
#else
    output[pixel] = c;
#endif
#endif

}
