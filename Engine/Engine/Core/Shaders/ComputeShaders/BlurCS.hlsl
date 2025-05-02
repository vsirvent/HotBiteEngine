#include "../Common/ShaderStructs.hlsli"

RWTexture2D<float4> input : register(u0);
RWTexture2D<float4> output : register(u1);
Texture2D<uint> vol_data: register(t0);

#define EPSILON 1e-6
#define VERTICAL 1
#define HORIZONTAL 2
#define KERNEL_SIZE 1
#define HALF_KERNEL KERNEL_SIZE/2
cbuffer externalData : register(b0)
{
    uint type;
    float variance;
}

static const float BlurWeights32[33] =
{
    0.0216771440357829, 0.023030174945629842, 0.024372268162712405, 0.0256920167211696, 0.026977641631473703, 0.028217160189400764, 0.02939856710486361, 0.030510024758829798, 0.03154005846550627, 0.032477752297221024, 0.033312940837257485, 0.03403639217321265, 0.034639977537105585, 0.03511682323976621, 0.035461440931519074, 0.035669832738636206, 0.03573956845982569, 0.035669832738636206, 0.035461440931519074, 0.03511682323976621, 0.034639977537105585, 0.03403639217321265, 0.033312940837257485, 0.032477752297221024, 0.03154005846550627, 0.030510024758829798, 0.02939856710486361, 0.028217160189400764, 0.026977641631473703, 0.0256920167211696, 0.024372268162712405, 0.023030174945629842, 0.0216771440357829
};

float4 blur32(RWTexture2D<float4> t, float2 pos, int type) {
    float4 color = float4(0.f, 0.f, 0.f, 1.f);
    if (type == HORIZONTAL) {
        for (int dx = -16; dx <= 16; ++dx) {
            color += t.Load(float3(pos.x + dx, pos.y, 0)) * BlurWeights32[dx + 16];
        }
    }
    else {
        for (int dy = -16; dy <= 16; ++dy) {
            color += t.Load(float3(pos.x, pos.y + dy, 0)) * BlurWeights32[dy + 16];
        }
    }
    return color;
}


#define NTHREADS 32

[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 dimensions;
    uint w, h;
    input.GetDimensions(w, h);
    float2 pixel = float2(DTid.x, DTid.y);
    float global = (float)vol_data[uint2(0, 0)] / (float)(w * h * 1000);
    //Linear function for global atenuation y = −x + (1 + (MAX_GLOBAL_ILLUMINATION)
#define MAX_GLOBAL_ILLUMINATION 0.4f
    float att = 1.0f;
    global *= global;
    global *= global;
    att = -global + (1.0f + MAX_GLOBAL_ILLUMINATION);
    //float4 color = blur32(input, pixel, type);
    float4 color = input.Load(float3(pixel.x, pixel.y, 0));
    if (type == HORIZONTAL) {
        //color *= att;
    }
    output[pixel] = max(color, 0);
}
