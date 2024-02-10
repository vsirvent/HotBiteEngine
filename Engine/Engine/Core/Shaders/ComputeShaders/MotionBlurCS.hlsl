#include "../Common/ShaderStructs.hlsli"

RWTexture2D<float4> input : register(u0);
RWTexture2D<float4> output : register(u1);
Texture2D<float4> positionTexture : register(t0);
Texture2D<float4> prevPositionTexture : register(t1);

cbuffer externalData : register(b0)
{
    matrix view_proj;
    matrix prev_view_proj;
}

static const uint numSamples = 15;
static const float MOTION_FACTOR = 0.1f;
static const float MAX_VELOCITY_FACTOR = 2.0f;

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 pixel = float2(DTid.x, DTid.y);
    
    float2 dimension;
    output.GetDimensions(dimension.x, dimension.y);

    float4 p1World = positionTexture[pixel];
    if (length(p1World) > 0) {

        float4 p0World = prevPositionTexture[pixel];
        float4 p0 = mul(p0World, prev_view_proj);
        float4 p1 = mul(p1World, view_proj);

        p0 /= p0.w;
        p1 /= p1.w;
        p0.y *= -1;
        p1.y *= -1;
        
        float2 dir = p0.xy - p1.xy;
        dir *= dimension;

        if (length(dir) > 1.0f) {
            uint steps = length(dir);
            float fsteps = (float)steps;

            float v = max(abs(dir.x), abs(dir.y));
            dir /= v;

            float2 outPixel = pixel;
            float total = 0.0f;

            float4 outColor = { 0.0f, 0.0f, 0.0f, 1.0f };
            for (uint i = 0; i < steps; ++i) {
                float a = (fsteps - (float)i) / fsteps;
                total += a;
                outColor += input[pixel];
                pixel -= dir;
            }
            outColor /= total;
            output[outPixel] = outColor;
        }
        else {
            output[pixel] += input[pixel];
        }
    }
    else {
        output[pixel] += input[pixel];
    }
}
