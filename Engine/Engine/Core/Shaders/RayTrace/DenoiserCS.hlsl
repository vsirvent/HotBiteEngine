
#include "../Common/ShaderStructs.hlsli"
#include "../Common/Utils.hlsli"

cbuffer externalData : register(b0)
{
    uint type;
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

float4 RoundColor(float4 color, float precision) {
    return round(color * precision) / precision;
}

// Estructura para almacenar un color y su frecuencia
struct ColorCount {
    float2 pixel;
    float4 rounded;
    uint count;
    uint next;
    
};

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
        input.GetDimensions(w, h);
        input_dimensions.x = w;
        input_dimensions.y = h;

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
    
    #define MAX_KERNEL 40
    float count = 0.0f;
    uint min_dispersion = 0;
    if (light_type == 0) {
        min_dispersion = 0;
    }
    else if (light_type == 1) {
        min_dispersion = MAX_KERNEL;
    }

    float2 dir = float2(0.0f, 0.0f);
    if (type == 1) {
        dir = float2(1.0f, 0.0f);
    }
    else if (type == 2) {
        dir = float2(0.0f, 1.0f);
    }

    float4 c0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float dist_att = 0.0f;
    int kernel = debug == 1 ? 0 : clamp(floor(max(MAX_KERNEL * pow(ray_source.dispersion, 2.0f), min_dispersion)), 0, MAX_KERNEL);
    float motion = 0.0f;
    float weights[2 * MAX_KERNEL + 1];

    for (int i = -kernel; i <= kernel; ++i) {
        float2 p = pixel + dir * i;
        p.x = clamp(p.x, 0.0f, input_dimensions.x - 1.0f);
        p.y = clamp(p.y, 0.0f, input_dimensions.y - 1.0f);
        float2 p1_info_pixel = p * normalRatio;

        float3 p1_position = positions[p1_info_pixel].xyz;
        float dist = clamp(dist2(p1_position - p0_position), 1.0f, 1000.0f);

        float3 p1_normal = normals[p1_info_pixel].xyz;
        float n = saturate(dot(p1_normal, p0_normal));
        float w = pow(n, 10.0f) / dist;

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
    else if (type == 2) {
        matrix worldViewProj = mul(view, projection);
        float4 prev_pos = mul(prev_position_map[info_pixel], worldViewProj);
        prev_pos.x /= prev_pos.w;
        prev_pos.y /= -prev_pos.w;
        prev_pos.x = (prev_pos.x + 1.0f) * normals_dimensions.x / 2.0f;
        prev_pos.y = (prev_pos.y + 1.0f) * normals_dimensions.y / 2.0f;
        float m = length(motion_texture[prev_pos.xy].xyz);
        if (m > motion) {
            motion = m;
        }
        float3 mvector = motion_texture[info_pixel].xyz;
        if (mvector.x < 0.0f) {
            motion = FLT_MAX;
        }
        else {
            m = length(motion_texture[info_pixel].xyz);
            if (m > motion) {
                motion = m;
            }
        }
        
        float w = saturate(0.7 - motion * 100.0f);
        float4 prev_color = prev_output[pixel];
        output[pixel] = prev_color* w + c0 * (1.0f - w);
    }
}
