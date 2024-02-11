#include "../Common/ShaderStructs.hlsli"


globallycoherent RWTexture2D<float4> output : register(u1);
globallycoherent RWByteAddressBuffer outputLock : register(u2);

Texture2D<float4> input : register(t0);
Texture2D<float4> positionTexture : register(t1);
Texture2D<float4> prevPositionTexture : register(t2);

cbuffer externalData : register(b0)
{
    matrix view_proj;
    matrix prev_view_proj;
}

static const uint numSamples = 15;
static const float MOTION_FACTOR = 0.1f;
static const float MAX_VELOCITY_FACTOR = 2.0f;

void AddPixel(uint2 pixel, uint w, uint h, float4 color)
{
    uint addr = (pixel.y * w + pixel.x);
    bool keepWaiting = true;
    while (keepWaiting) {
        uint originalValue;
        outputLock.InterlockedCompareExchange(addr, 0, 1, originalValue);
        if (originalValue == 0) {
            DeviceMemoryBarrier();
            output[pixel] += color;
            outputLock.InterlockedExchange(addr, 0, originalValue);
            keepWaiting = false;
        }
    }
}

float KeepDecimals(float value, int nDecimalsToKeep)
{
    float power = pow(10, nDecimalsToKeep);
    return round(value * power) / power;
}

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 Gid: SV_GroupID, uint3 Tid: SV_GroupThreadID)
{
    //float2 pixel = float2(DTid.x, DTid.y);
    float2 pixel = float2(Gid.x * NTHREADS + Tid.x, Gid.y * NTHREADS + Tid.y);
    uint w, h;
    float2 dimension;
    output.GetDimensions(w, h);
    dimension.x = w;
    dimension.y = h;

    float4 inputColor = input[pixel];

    uint2 upixel = pixel;

    float4 p1World = positionTexture[pixel];
    float4 p0World = prevPositionTexture[pixel];

    float4 p0 = mul(p0World, prev_view_proj);
    float4 p1 = mul(p1World, view_proj);

    p0 /= p0.w;
    p1 /= p1.w;
    p0.y *= -1.0f;
    p1.y *= -1.0f;

    float2 dir = p1.xy - p0.xy;
    dir *= dimension;
        
    float dir_len = length(dir);

    float fsteps = dir_len;
    
    dir /= fsteps;

    float step_size = length(dir);
    float n = step_size;
    float t = 1.0f;

    while (n < fsteps) {
        float a = (fsteps - n) / fsteps;
        t += a;
        n += step_size;
    }

    AddPixel(upixel, w, h, inputColor / t);

    //Add the motion trace
    float2 p = pixel;
    n = step_size;
    while (n < fsteps) {
        upixel = round(p);
        if (upixel.x > 0 && upixel.x < w && upixel.y > 0 && upixel.y < h)
        {
            float a = (fsteps - n) / (fsteps * t);
            AddPixel(p, w, h, inputColor * a);
        }
        p += dir;
        n += step_size;
    }
}
