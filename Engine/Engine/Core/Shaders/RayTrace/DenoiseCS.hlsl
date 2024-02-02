RWTexture2D<float4> input : register(u0);
RWTexture2D<float4> output : register(u1);

#define EPSILON 1e-6
#define VERTICAL 1
#define HORIZONTAL 2
#define KERNEL_SIZE 9
#define DENSITY 1.0f
cbuffer externalData : register(b0)
{
    uint type;
}

float4 getColor(float2 pixel, float2 dir)
{
    float4 color = float4(0.f, 0.f, 0.f, 1.f);
    float2 tpos;
    for (int i = -5; i <= 5; ++i) {
        tpos = pixel + dir * i;
        color += input[tpos];
    }
    color /= (10.0f * DENSITY);
    float4 color_orig = input[pixel];
    float diff = (length(color.xyz) - length(color_orig.xyz));
    if (diff > 0) {
        return color_orig * (1.0 - diff) + color * diff;
    }
    else {
        return color_orig;
    }
}

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 dimensions;
    {
        uint w, h;
        input.GetDimensions(w, h);
        dimensions.x = w;
        dimensions.y = h;
    }

    float blockSizeX = (float)dimensions.x / (float)NTHREADS;
    float blockSizeY = (float)dimensions.y / (float)NTHREADS;
    float blockStartX = (float)DTid.x * blockSizeX;
    float blockStartY = (float)DTid.y * blockSizeY;

    uint2 npixels = { 0, 0 };
    uint2 dir = { 0, 0 };

    npixels = uint2(blockSizeX, blockSizeY);
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
            float2 pixel = float2(blockStartX + x, blockStartY + y);
            output[pixel] = getColor(pixel, dir);
        }
    }
}
