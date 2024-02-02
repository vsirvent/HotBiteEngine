RWTexture2D<float4> ray_acc : register(u0);
RWTexture2D<float4> ray_new : register(u1);

#define EPSILON 1e-6
#define NTHREADS 32

[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 dimensions;
    {
        uint w, h;
        ray_acc.GetDimensions(w, h);
        dimensions.x = w;
        dimensions.y = h;
    }

    float blockSizeX = (float)dimensions.x / (float)NTHREADS;
    float blockSizeY = (float)dimensions.y / (float)NTHREADS;
    float blockStartX = (float)DTid.x * blockSizeX;
    float blockStartY = (float)DTid.y * blockSizeY;

    uint2 npixels = { blockSizeX,  blockSizeY };
    
    for (uint x = 0; x < npixels.x; ++x)
    {
        for (uint y = 0; y <= npixels.y; ++y)
        {
            float2 pixel = float2(blockStartX + x, blockStartY + y);
            float4 color = ray_acc[pixel];
            ray_acc[pixel] = 0.5f * ray_new[pixel] + 0.5f * ray_acc[pixel];
        }
    }
}
