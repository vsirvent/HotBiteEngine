#include "../Common/ShaderStructs.hlsli"

RWTexture2D<float4> output : register(u1);
Texture2D<float4> positionTexture : register(t1);
Texture2D<float4> prevPositionTexture : register(t2);

cbuffer externalData : register(b0)
{
    matrix view_proj;
    matrix prev_view_proj;
}

float2 GetPixelDir(float2 pixel) {

    float4 p0World = prevPositionTexture[pixel];
    float4 p1World = positionTexture[pixel];

    if (length(p0World) > 0.0f && length(p1World) > 0.0f) {
        float4 p0 = mul(p0World, prev_view_proj);
        float4 p1 = mul(p1World, view_proj);

        p0 /= abs(p0.w);
        p1 /= abs(p1.w);

        p0.y *= -1.0f;
        p1.y *= -1.0f;
        return p1.xy - p0.xy;
    }
    else {
        return float2(0.0f, 0.0f);
    }
}

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 Gid: SV_GroupID, uint3 Tid: SV_GroupThreadID)
{
    float2 pixel = float2(DTid.x, DTid.y);
    output[pixel] = float4(GetPixelDir(pixel), 0.0f, 1.0f);
}
