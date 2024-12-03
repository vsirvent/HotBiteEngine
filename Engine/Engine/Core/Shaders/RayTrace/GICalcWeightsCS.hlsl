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

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 pixel = DTid.xy;
   
    float values[MAX_RAYS];
    UnpackRays(restir_pdf_0[pixel], RAY_W_SCALE, values);

     
    float total_w = 0.0f;
    for (uint i = 0; i < MAX_RAYS; ++i) {
        total_w += values[i];
    }
    for (i = 0; i < MAX_RAYS; ++i) {
        values[i] /= total_w;
    }
    restir_pdf_1[pixel] = RAY_W_SCALE;
}
