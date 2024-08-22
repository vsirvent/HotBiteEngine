
#include "../Common/ShaderStructs.hlsli"
#include "../Common/Utils.hlsli"

cbuffer externalData : register(b0)
{
    uint type;
    float3 cameraPosition;
    uint light_type;
}

Texture2D<float4> input : register(t0);
RWTexture2D<float4> output : register(u0);
Texture2D<float4> normals : register(t1);
Texture2D<float4> positions : register(t2);
Texture2D<float4> prev_output : register(t3);
Texture2D<float4> motion_texture : register(t4);

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
        uint w, h;
        if (type == 1 || type == 3) {
            input.GetDimensions(w, h);
        }
        else if (type == 2 || type == 4) {
            output.GetDimensions(w, h);
        }
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
    
    float count = 0.0f;
    uint min_dispersion = 0;
    if (light_type == 0) {
        min_dispersion = 0;
    }
    else if (light_type == 1) {
        min_dispersion = 40;
    }
    float2 dir = float2(0.0f, 0.0f);
    if (type == 1) {
        dir = float2(0.0f, 1.0f);
    }
    else if (type == 2) {
        dir = float2(1.0f, 0.0f);
    }
    float distToCam = min(pow(dist2(cameraPosition - p0_position), 1.0f), 10.0f);
    
    float4 c0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float dist_att = 0.0f;
    int kernel = 40;// floor(max(2.0f * ray_source.dispersion, min_dispersion));
    for (int i = -kernel; i <= kernel; ++i) {
        float2 p = pixel + dir * i;
        info_pixel = p * normalRatio;
        float3 p1_normal = normals[info_pixel].xyz;
        float3 p1_position = positions[info_pixel].xyz;
        float4 color = input[p]; 
        float dist = clamp(length(p1_position - p0_position), 0.8f, 1000.0f);
        float distNormal = length(p1_normal - p0_normal) / pow(distToCam, 3.0f);
        distToCam = clamp(distToCam, 1.0f, 1000.0f);
        float n = saturate(dot(p1_normal, p0_normal));
        float w = pow(n, 5.0f);
            
        c0 += color * saturate(w / dist);
        count += w;
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
        float motion = length(motion_texture[info_pixel].xyz);
        float w = max(saturate(0.7 - motion * 100.0f), 0.1f);
        float4 prev_color = prev_output[pixel];
        output[pixel] = prev_color* w + c0 * (1.0f - w);
    }
}
