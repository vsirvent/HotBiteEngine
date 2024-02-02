#include "../Common/ShaderStructs.hlsli"

RWTexture2D<float4> input : register(u0);
RWTexture2D<float4> output : register(u1);
RWTexture2D<float> props : register(u2);

#define EPSILON 1e-6
#define VERTICAL 1
#define HORIZONTAL 2
#define KERNEL_SIZE 9
#define HALF_KERNEL 4
cbuffer externalData : register(b0)
{
    uint type;
}

void FillGaussianArray(out float array[KERNEL_SIZE], float dispersion)
{
    float sum = 0.0;
    int halfSize = KERNEL_SIZE / 2;
    float variance = 0.6f + dispersion * 4.0f;
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

float4 getColor(float2 pixel, float2 dir, float dispersion)
{

        float BlurWeights[KERNEL_SIZE];
        FillGaussianArray(BlurWeights, dispersion);
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
    uint2 dimensions;
    {
        uint w, h;
        input.GetDimensions(w, h);
        dimensions.x = w;
        dimensions.y = h;
    }

    float blockSizeX = (float)dimensions.x / (float)NTHREADS;
    float blockSizeY = (float)dimensions.y / (float)NTHREADS;
    float blockStartX = (float)DTid.x * blockSizeX;
    float blockStartY = (float)DTid.y * blockSizeY;

    uint2 npixels = { 0, 0 };
    uint2 dir = { 0, 0 };

    npixels = uint2(blockSizeX, blockSizeY);
    if (type == VERTICAL) {        
        dir = uint2(0, 1);
    }
    else if (type == HORIZONTAL) {
        dir = uint2(1, 0);
    }
    
    for (uint x = 0; x <= npixels.x; ++x)
    {
        for (uint y = 0; y <= npixels.y; ++y)
        {
            float2 pixel = float2(blockStartX + x, blockStartY + y);
            float dispersion = props[pixel];
            if (dispersion >= 0.0f) {
                output[pixel] = getColor(pixel, dir, dispersion);
            }
            else {
                output[pixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
            }
        }
    }
}
