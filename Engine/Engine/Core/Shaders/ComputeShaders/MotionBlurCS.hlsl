#include "../Common/ShaderStructs.hlsli"


globallycoherent RWTexture2D<float4> output : register(u1);
RWByteAddressBuffer outputLock : register(u2);

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

void AddIntPixel(uint2 pixel, uint w, uint h, float4 color)
{
    uint addr = (pixel.y * w + pixel.x);
    bool keepWaiting = true;
    while (keepWaiting) {
        uint originalValue;
        outputLock.InterlockedCompareExchange(addr, 0, 1, originalValue);
        if (originalValue == 0) {
            output[pixel] += color;
            outputLock.InterlockedExchange(addr, 0, originalValue);
            keepWaiting = false;
        }
    }
}

void AddPixel(float2 p, uint w, uint h, float4 color)
{
    float2 pfloor = floor(p);
    float2 pceil = ceil(p);

    float2 w00 = { 1.0f - (p.x - pfloor.x), 1.0f - (p.y - pfloor.y) };
    float2 w01 = { 1.0f - (p.x - pfloor.x), (p.y - pfloor.y) };

    float2 w10 = { (p.x - pfloor.x), 1.0f - (p.y - pfloor.y) };
    float2 w11 = { (p.x - pfloor.x), (p.y - pfloor.y) };

    uint2 p00 = pfloor;
    uint2 p11 = pceil;
    uint2 p01 = { pfloor.x, pceil.y };
    uint2 p10 = { pceil.x, pfloor.y };

    AddIntPixel(p00, w, h, color * w00.x * w00.y);
    AddIntPixel(p01, w, h, color * w01.x * w01.y);
    AddIntPixel(p10, w, h, color * w10.x * w10.y);
    AddIntPixel(p11, w, h, color * w11.x * w11.y);
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
        
    float4 p1World = positionTexture[pixel];
    float4 p0World = prevPositionTexture[pixel];
    float4 dirWorld = p1World - p0World;

    float4 p0 = mul(p0World, prev_view_proj);
    float4 p1 = mul(p1World, view_proj);

    p0 /= p0.w;
    p1 /= p1.w;
    p0.y *= -1.0f;
    p1.y *= -1.0f;

    float2 dir = p1.xy - p0.xy;
    dir *= dimension*0.5f;
        
    float dir_len = min(length(dir), 100.0f);

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

    AddPixel(pixel, w, h, inputColor / t);
    AllMemoryBarrierWithGroupSync();
    //Add the motion trace
    float2 p = pixel;
    n = step_size;
    while (n < fsteps) {
        float a = (fsteps - n) / (fsteps * t);
        AddPixel(p, w, h, inputColor * a);        
        p += dir;
        n += step_size;
    }
}
