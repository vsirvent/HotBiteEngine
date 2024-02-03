RWTexture2D<float4> ray_acc : register(u0);
RWTexture2D<float4> ray_new0 : register(u1);
RWTexture2D<float4> ray_new1 : register(u2);
RWTexture2D<float4> ray_new2 : register(u3);

#define EPSILON 1e-6
#define NTHREADS 32

[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 pixel = float2(DTid.x, DTid.y);
    float4 color = ray_acc[pixel];
    ray_acc[pixel] = 0.5f * ray_acc[pixel] + 0.5f * ray_new0[pixel] + 0.5f * ray_new1[pixel/2] + 0.5f * ray_new2[pixel/4];
}
