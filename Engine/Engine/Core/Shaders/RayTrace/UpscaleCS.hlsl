#include "../Common/ShaderStructs.hlsli"

RWTexture2D<float4> input : register(u0);
RWTexture2D<float4> output : register(u1);
RWTexture2D<float> props : register(u2);

#define EPSILON 1e-6
#define VERTICAL 1
#define HORIZONTAL 2

cbuffer externalData : register(b0)
{
    uint type;
}


#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 isize;
    uint2 osize;
    {
        uint w, h;
        input.GetDimensions(w, h);
        isize.x = w;
        osize.y = h;
    }

    float inBlockSizeX = (float)isize.x / (float)NTHREADS;
    float inBlockSizeY = (float)isize.y / (float)NTHREADS;
    float inBlockStartX = (float)DTid.x * inBlockSizeX;
    float inBlockStartY = (float)DTid.y * inBlockSizeY;

    float outBlockSizeX = (float)osize.x / (float)NTHREADS;
    float outBlockSizeY = (float)osize.y / (float)NTHREADS;
    float outBlockStartX = (float)DTid.x * outBlockSizeX;
    float outBlockStartY = (float)DTid.y * outBlockSizeY;

    float2 ratio = (float2)isize / (float2)osize;
    
    uint2 npixels = { 0, 0 };
    uint2 dir = { 0, 0 };

    npixels = uint2(outBlockSizeX, outBlockSizeY);
    if (type == VERTICAL) {        
        dir = uint2(0, 1);
    }
    else if (type == HORIZONTAL) {
        dir = uint2(1, 0);
    }
    
    for (uint x = 0; x <= npixels.x; ++x)
    {
        for (uint y = 0; y <= npixels.y; ++y)
        {
            float2 ipixel = float2(inBlockStartX + (float)x * ratio.x, inBlockStartY + (float)y * ratio.y);
            float2 opixel = float2(outBlockStartX + x, outBlockStartY + y);
            //output[opixel] = getColor(ipixel);
        }
    }
}
