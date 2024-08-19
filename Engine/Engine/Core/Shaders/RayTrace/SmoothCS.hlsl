#include "../Common/ShaderStructs.hlsli"
#include "../Common/Utils.hlsli"

RWTexture2D<float4> input : register(u0);
RWTexture2D<float4> output : register(u1);
RWTexture2D<float> props : register(u2);
Texture2D<float> depth : register(t0);

#define EPSILON 1e-6
#define VERTICAL 1
#define HORIZONTAL 2
#define KERNEL_SIZE 3
#define HALF_KERNEL KERNEL_SIZE/2

cbuffer externalData : register(b0)
{
    uint type;
    uint use_depth;
}

void FillGaussianArray(out float array[KERNEL_SIZE], float dispersion)
{
    float sum = 0.0;
    int halfSize = KERNEL_SIZE / 2;
    float variance = 1.0f + dispersion * (float)HALF_KERNEL;
    int i;
    for (i = -HALF_KERNEL; i <= HALF_KERNEL; ++i)
    {
        int x = i;
        array[i + halfSize] = exp(-(x * x) / (2.0 * variance * variance)) / sqrt(2.0 * 3.14159265358979323846 * variance * variance);
        sum += array[i + halfSize];
    }

    // Normalize the array
    for (i = 0; i < KERNEL_SIZE; ++i)
    {
        array[i] /= sum;
    }
}

float4 getColor(float2 pixel, float2 dir, float dispersion, float depthRatio)
{
    if (use_depth == 0 || dispersion < Epsilon) {
        return input[pixel];
    }
    else {
        float BlurWeights[KERNEL_SIZE];
        FillGaussianArray(BlurWeights, dispersion);
        float4 color = float4(0.f, 0.f, 0.f, 1.f);
        float2 tpos;

        float pixelDepth = depth[pixel * depthRatio];
        float4 baseColor = input[pixel];
        for (int i = -HALF_KERNEL; i <= HALF_KERNEL; ++i) {
            tpos = pixel + dir * i;
            float d = depth[tpos * depthRatio];
            float4 c;
            c = lerp(input[tpos], baseColor, saturate(abs(d - pixelDepth) * 16.0f));
            color += c * BlurWeights[i + HALF_KERNEL];
        }
        return color;
    }
}

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 dir = { 0, 0 };
    if (type == VERTICAL) {
        dir = uint2(0, 1);
    }
    else if (type == HORIZONTAL) {
        dir = uint2(1, 0);
    }

    uint2 input_dimensions;
    uint2 depth_dimensions;
    {
        uint w, h;
        input.GetDimensions(w, h);
        input_dimensions.x = w;
        input_dimensions.y = h;

        depth.GetDimensions(w, h);
        depth_dimensions.x = w;
        depth_dimensions.y = h;
    }

    uint2 depthRatio = depth_dimensions / input_dimensions;

    float2 pixel = float2(DTid.x, DTid.y);
    float dispersion = props[pixel];
    if (dispersion >= 0.0f) {
        output[pixel] = getColor(pixel, dir, dispersion, depthRatio);
    }
    else {
        output[pixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
}
