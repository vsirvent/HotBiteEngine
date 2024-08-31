#include "../Common/ShaderStructs.hlsli"

RWTexture2D<float4> input : register(u0);
RWTexture2D<float4> output : register(u1);
Texture2D<uint> vol_data: register(t0);

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
    float v = 5.0f;
    for (i = -HALF_KERNEL; i <= HALF_KERNEL; ++i)
    {
        int x = i;
        array[i + halfSize] = exp(-(x * x) / (2.0f * v * v)) / sqrt(2.0f * 3.14159265358979323846f * v * v);
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
        float4 color = float4(0.f, 0.f, 0.f, 0.f);
        float2 tpos;
        for (int i = -HALF_KERNEL; i <= HALF_KERNEL; ++i) {
            tpos = pixel + dir * i;
            color += input[tpos] * BlurWeights[i + HALF_KERNEL];
        }
        return color;
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
    
    uint2 dimensions;
    uint w, h;
    input.GetDimensions(w, h);
    float2 pixel = float2(DTid.x, DTid.y);
    float global = (float)vol_data[uint2(0, 0)] / (float)(w * h * 1000);
    //Linear function for global atenuation y = −x + (1 + (MAX_GLOBAL_ILLUMINATION)
#define MAX_GLOBAL_ILLUMINATION 0.8f
    float att = 1.0f;
    global *= global;
    global *= global;
    att = -global + (1.0f + MAX_GLOBAL_ILLUMINATION);
    float4 color = getColor(pixel, dir);
    if (type == HORIZONTAL) {
        color *= att;
    }
    output[pixel] = max(color, 0);
}
