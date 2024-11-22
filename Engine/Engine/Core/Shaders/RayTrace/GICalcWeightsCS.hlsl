#include "../Common/ShaderStructs.hlsli"
#include "../Common/RayDefines.hlsli"
#include "../Common/Utils.hlsli"

cbuffer externalData : register(b0)
{
    uint frame_count;
    uint ray_count;
}

Texture2D<uint4> restir_pdf_0: register(t0);
RWTexture2D<uint4> restir_pdf_1: register(u0);
RWTexture2D<float> restir_w_1: register(u1);

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 pixel = DTid.xy;
    float bias = 1.0f;
    
    float values[MAX_RAYS];
    UnpackRays(restir_pdf_0[pixel], RAY_W_SCALE, values);

    restir_pdf_1[pixel] = restir_pdf_0[pixel];
    float total_w = 0.0f;
    for (uint i = 0; i < MAX_RAYS; ++i) {
        total_w += values[i];
    }
    restir_w_1[pixel] = total_w;
}
