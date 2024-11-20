#include "../Common/ShaderStructs.hlsli"
#include "../Common/Utils.hlsli"

cbuffer externalData : register(b0)
{
    uint ray_count;
}

Texture2D<uint4> restir_pdf_0: register(t0);
RWTexture2D<uint4> restir_pdf_1: register(u0);
RWTexture2D<float> restir_w_1: register(u1);

#define MAX_RAYS 16
#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 pixel = DTid.xy;
    static const float bias = 1.0f;
    
    float values[MAX_RAYS];
    Unpack16Bytes(restir_pdf_0[pixel], 5.0f, values);

    for (uint i = 0; i < MAX_RAYS; ++i) {
        values[i] += bias;
    }

    restir_pdf_1[pixel] = Pack16Bytes(values, 5.0f);
    float total_w = 0.0f;
    for (i = 0; i < MAX_RAYS; ++i) {
        total_w += values[i];
    }
    restir_w_1[pixel] = total_w;
}
