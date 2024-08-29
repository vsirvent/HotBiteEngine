
#include "../Common/ShaderStructs.hlsli"
#include "../Common/Utils.hlsli"

cbuffer externalData : register(b0)
{
    uint type;
}

Texture2D<float4> input : register(t0);
RWTexture2D<float4> output : register(u0);

static const float kw[21] = { 0.000514f,0.001478f,0.003800f,0.008744f,0.018005f,0.033174f,0.054694f,0.080692f,0.106529f,0.125850f,0.133039f,0.125850f,0.106529f,0.080692f,0.054694f,0.033174f,0.018005f,0.008744f,0.003800f,0.001478f,0.000514f };

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 input_dimensions;
    uint w = 0;
    uint h = 0;
    input.GetDimensions(w, h);
    input_dimensions.x = w;
    input_dimensions.y = h;

    float2 pixel = float2(DTid.x, DTid.y);

    float2 dir = float2(0.0f, 0.0f);
    if (type == 1 || type == 3) {
        dir = float2(1.0f, 0.0f);
    }
    else if (type == 2 || type == 4) {
        dir = float2(0.0f, 1.0f);
    }

    int kernel = type <= 1?0:10; 
    float max_disp = 0.0f;
    if (type <= 2) {
        for (int i = -kernel; i <= kernel; ++i) {
            float2 p = pixel + dir * i;
            if ((p.x < 0 || p.x >= input_dimensions.x) && (p.y < 0 || p.y >= input_dimensions.y)) {
                break;
            }
            float c = input[p].g;
            if (c > max_disp) {
                max_disp = c;
            }
        }
    }
    else {
        for (int i = -kernel; i <= kernel; ++i) {
            float2 p = pixel + dir * i;
            if ((p.x < 0 || p.x >= input_dimensions.x) && (p.y < 0 || p.y >= input_dimensions.y)) {
                break;
            }
            max_disp += input[p].g * kw[i + 10];
        }
    }
    float4 c0 = input[pixel];
    c0.g = max_disp;
    output[pixel] = c0;
}
