#include "../Common/ShaderStructs.hlsli"

RWTexture2D<float4> input : register(u0);
RWTexture2D<float4> output : register(u1);

#define EPSILON 1e-6
#define VERTICAL 1
#define HORIZONTAL 2
#define KERNEL_SIZE 19
#define HALF_KERNEL KERNEL_SIZE/2
cbuffer externalData : register(b0)
{
    uint type;
    float variance;
}

void FillGaussianArray(out float array[KERNEL_SIZE])
{
    float sum = 0.0;
    int halfSize = KERNEL_SIZE / 2;
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

float4 getColor(float2 pixel, float2 dir)
{

        float BlurWeights[KERNEL_SIZE];
        FillGaussianArray(BlurWeights);
        float4 color = float4(0.f, 0.f, 0.f, 1.f);
        float2 tpos;
        for (int i = -HALF_KERNEL; i <= HALF_KERNEL; ++i) {
            tpos = pixel + dir * i;
            color += input[tpos] * BlurWeights[i + HALF_KERNEL];
        }
        return saturate(color);
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
    
    float2 pixel = float2(DTid.x, DTid.y);
    output[pixel] = getColor(pixel, dir);
}
