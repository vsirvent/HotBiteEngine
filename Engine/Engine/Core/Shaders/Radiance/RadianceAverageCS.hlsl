#include "../Common/ShaderStructs.hlsli"
#include "../Common/Utils.hlsli"

cbuffer externalData : register(b0)
{
    uint debug;
    float RATIO;
    matrix view;
    matrix projection;
    float3 cameraPosition;
}

Texture2D<float4> input : register(t0);
RWTexture2D<float4> output : register(u0);
Texture2D<float4> positions : register(t2);
Texture2D<float4> prev_output : register(t3);
Texture2D<float2> motion_texture : register(t4);
Texture2D<float4> prev_position_map: register(t5);

#define KERNEL_SIZE 5

//static const float kw[2 * KERNEL_SIZE + 1] = { 0.1f, 0.3f, 0.3f, 0.3f, 0.1f };

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 pixel = round(float2(DTid.x, DTid.y) * RATIO);

    if (false) {
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
    float2 info_pixel = round(pixel * infoRatio);


    float4 c = float4(0.0f, 0.0f, 0.0f, 0.0f);
    for (int x = -KERNEL_SIZE; x <= KERNEL_SIZE; ++x) {
        for (int y = -KERNEL_SIZE; y <= KERNEL_SIZE; ++y) {
            int2 p = pixel + int2(x, y) * RATIO;
            c += input[p];// *kw[x + KERNEL_SIZE] * kw[y + KERNEL_SIZE];
        }
    }
    c /= 2 * KERNEL_SIZE * KERNEL_SIZE;
#if 1
    float4 prev_color = prev_output[pixel];
    float w = 0.8f; // saturate(0.8f - motion * 50.0f);
    output[pixel] = prev_color * w + c * (1.0f - w);
#else
    output[pixel] = c;;
#endif
}