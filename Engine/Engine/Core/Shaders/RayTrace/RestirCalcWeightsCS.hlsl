#include "../Common/ShaderStructs.hlsli"
#include "../Common/Utils.hlsli"

cbuffer externalData : register(b0)
{
    uint ray_count;
}

Buffer<float4> restir_pdf_0: register(t0);

RWBuffer<float4> restir_pdf_1: register(u0);
RWTexture2D<float> restir_w_1: register(u1);

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 dims;

    uint2 pixel = DTid.xy;
    restir_w_1.GetDimensions(dims.x, dims.y);
    static const float count = ray_count / 4;
    static const float bias = 1.0f;
    float pdf_offset = (pixel.x + pixel.y * dims.x) * count;
    float total_w = 0.0f;
    
    for (uint i = 0; i < count; ++i) {
        uint pos = pdf_offset + i;
        float4 val = bias + restir_pdf_0[pos]; // lerp(bias + restir_pdf_0[pos], restir_pdf_1[pos], 0.2f);
        restir_pdf_1[pos] = val;
        total_w += val.x + val.y + val.z + val.w;
    }

    restir_w_1[pixel] = total_w;
}
