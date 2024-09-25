#include "../Common/ShaderStructs.hlsli"
#include "../Common/Utils.hlsli"

cbuffer externalData : register(b0)
{
    uint type;
    uint count;
    float3 cameraPosition;
    uint light_type;
    matrix view;
    matrix projection;
    uint debug;
}

Texture2D<float4> input : register(t0);
RWTexture2D<float4> output : register(u0);
Texture2D<float4> normals : register(t1);
Texture2D<float4> positions : register(t2);
Texture2D<float4> prev_output : register(t3);
Texture2D<float4> motion_texture : register(t4);
Texture2D<float4> prev_position_map: register(t5);
Texture2D<float4> dispersion: register(t6);

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 pixel = float2(DTid.x, DTid.y);

    uint2 input_dimensions;
    uint2 normals_dimensions;
    {
        uint w = 0;
        uint h = 0;
        uint w2 = 0;
        uint h2 = 0;
        input.GetDimensions(w, h);
        output.GetDimensions(w2, h2);
        input_dimensions.x = min(w, w2);
        input_dimensions.y = min(h, h2);

        normals.GetDimensions(w, h);
        normals_dimensions.x = w;
        normals_dimensions.y = h;
    }
    uint2 normalRatio = normals_dimensions / input_dimensions;

    float2 info_pixel = pixel * normalRatio;
    float3 p0_normal = normals[info_pixel].xyz;
    float3 p0_position = positions[info_pixel].xyz;
    RaySource ray_source = fromColor(positions[info_pixel], normals[info_pixel]);
    float4 c = float4(0.0f, 0.0f, 0.0f, 0.0f);

#define HARD_MAX_KERNEL 32
    uint MAX_KERNEL = min(normals_dimensions.x / 32, HARD_MAX_KERNEL);
    float count = 0.0f;
    float2 dir = float2(0.0f, 0.0f);
    if (type == 1 || type == 3) {
        dir = float2(1.0f, 0.0f);
    }
    else if (type == 2 || type == 4) {
        dir = float2(0.0f, 1.0f);
    }

    float4 c0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float dist_att = 0.0f;
    float disp = -1.0f;
    uint min_dispersion = 0;

    if (dist2(ray_source.orig) <= Epsilon) {
        output[pixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    

    switch (light_type) {
    case 0: {
        //disp = sqrt(ray_source.dispersion);
        disp = dispersion[pixel].r;
        break;
    }
    case 1: {
        //disp = ray_source.dispersion;
        disp = dispersion[pixel].b;
        break;
    }
    }

    if (disp <= Epsilon) {
        output[pixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    uint min_k = normalRatio.x > 1 ? 1 : 0;
    int kernel = debug == 1 ? 0 : clamp(floor(max(MAX_KERNEL * disp, min_dispersion)), min_k, MAX_KERNEL);
    float motion = 0.0f;
    float camDist = dist2(cameraPosition - p0_position);
    for (int i = -kernel; i <= kernel; ++i) {
        float2 p = pixel + dir * i;
        if ((p.x < 0 || p.x >= input_dimensions.x) && (p.y < 0 || p.y >= input_dimensions.y)) {
            continue;
        }
        float2 p1_info_pixel = p * normalRatio;
        float3 p1_position = positions[p1_info_pixel].xyz;
        float3 p1_normal = normals[p1_info_pixel].xyz;
        float n = saturate(dot(p1_normal, p0_normal));
        float dist = max(dist2(p1_position - p0_position) / camDist, 0.1f);
        float w = pow(n, 20.0f / normalRatio) / dist;
        count += w;
        c0 += input[p] * w;
    }

    if (count > Epsilon) {
        c0 /= count;
    }
    else {
        c0 *= 0.0f;
    }
    if (type == 1) {
        output[pixel] = c0;
    }
    else {
        matrix worldViewProj = mul(view, projection);
        float4 prev_pos = mul(prev_position_map[info_pixel], worldViewProj);
        prev_pos.x /= prev_pos.w;
        prev_pos.y /= -prev_pos.w;
        prev_pos.xy = (prev_pos.xy + 1.0f) * normals_dimensions.xy / 2.0f;
        float m = length(motion_texture[prev_pos.xy].xyz);
        if (m > motion) {
            motion = m;
        }
        float3 mvector = motion_texture[info_pixel].xyz;
        if (mvector.x == -FLT_MAX) {
            motion = FLT_MAX;
        }
        else {
            m = length(motion_texture[info_pixel].xyz);
            if (m > motion) {
                motion = m;
            }
        }

        float4 prev_color = prev_output[pixel];
        float w = saturate(0.7f - motion * 100.0f);
        output[pixel] = prev_color * w + c0 * (1.0f - w);
    }
}