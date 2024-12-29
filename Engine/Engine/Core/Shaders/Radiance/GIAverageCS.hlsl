#include "../Common/ShaderStructs.hlsli"
#include "../Common/Utils.hlsli"

cbuffer externalData : register(b0)
{
    uint debug;
    uint type;
    matrix prev_view_proj;
    float3 cameraPosition;
    uint frame_count;
    int kernel_size;
}

Texture2D<float4> input : register(t0);
Texture2D<float4> orig_input : register(t1);
RWTexture2D<float4> output : register(u0);
Texture2D<float4> positions : register(t2);
Texture2D<float4> normals : register(t3);
Texture2D<float4> prev_output : register(t4);
Texture2D<float2> motion_texture : register(t5);
Texture2D<float4> prev_position_map: register(t6);
Texture2D<uint> tiles_output: register(t7);


float GetPosW(int pos, uint kernel) {
    return cos((M_PI * abs((float)pos)) / (2.0f * (float)kernel));
}

//#define DEBUG
#define MIN_W 0.1f

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{

    float2 pixel = float2(DTid.x, DTid.y);
#ifdef DEBUG
    if (debug == 1) { 
        output[pixel] = input[pixel];
        return;
    }
#endif
    float2 input_dimensions;
    float2 info_dimensions;
    {
        int w = 0;
        int h = 0;
        int w2 = 0;
        int h2 = 0;
        input.GetDimensions(w, h);
        output.GetDimensions(w2, h2);
        input_dimensions.x = min(w, w2);
        input_dimensions.y = min(h, h2);

        positions.GetDimensions(w, h);
        info_dimensions.x = w;
        info_dimensions.y = h;
    }
    float2 infoRatio = info_dimensions / input_dimensions;
    float2 rpixel = pixel * infoRatio;
    

    float2 info_pixel = round(rpixel);
    float3 p0_position = positions[info_pixel].xyz;
    float3 p0_normal = normals[info_pixel].xyz;

    float dist_to_cam = dist2(p0_position - cameraPosition);
    int x;
    int y;

    static const float NORMAL_RATIO = 10.0f;
    static const float sigma = 2.0f;
    
    float total_w = 0.0f;
    float ww = 1.0f;
    float4 c = float4(0.0f, 0.0f, 0.0f, 0.0f);

    int full_kernel = 2 * kernel_size + 1;

    [branch]
    if (tiles_output[pixel / full_kernel] != 0) {

        float input_mix = 0.0f;

        int k = kernel_size + kernel_size + full_kernel;
        float prev_w = input[pixel].a;

        [branch]
        if (prev_w < 0.0f) {
            return;
        }

        uint count = 0;

        [branch]
        switch (type)
        {

            //Execute pass 1
        case 1: {
            //Pass 1 horizontal convolution
            float2 dir = float2(1.0f, 0.0f);

            for (x = -k; x <= k; ++x) {
                int2 p = (pixel + x * dir);
                p.x += step(Epsilon, -p.x) * k;
                p.x += step(Epsilon, p.x - input_dimensions.x) * -k;

                float2 p1_info_pixel = round(p * infoRatio);
                ww = 1.0f;

                float3 p1_position = positions[p1_info_pixel].xyz;
                float world_dist = dist2(p1_position - p0_position) / (dist_to_cam);
                ww = exp(-world_dist / (2.0f * sigma * sigma));

                float3 p1_normal = normals[p1_info_pixel].xyz;
                float n = saturate(dot(p1_normal, p0_normal));
                ww *= pow(n, max(NORMAL_RATIO / infoRatio.x, 1.0f));
                ww *= GetPosW(x, k);
                ww = max(ww, MIN_W);

                c += input[p] * ww;
                total_w += ww;
                count++;
            }
        } break;

            //Execute pass 2
        case 2: {
            float2 dir = float2(0.0f, 1.0f);
            for (x = -k; x <= k; ++x) {
                int2 p = (pixel + x * dir);
                p.y += step(Epsilon, -p.y) * k;
                p.x += step(Epsilon, p.y - input_dimensions.y) * -k;

                float2 p1_info_pixel = round(p * infoRatio);
                ww = 1.0f;

                float3 p1_position = positions[p1_info_pixel].xyz;
                float world_dist = dist2(p1_position - p0_position) / (dist_to_cam);
                ww = exp(-world_dist / (2.0f * sigma * sigma));

                float3 p1_normal = normals[p1_info_pixel].xyz;
                float n = saturate(dot(p1_normal, p0_normal));
                ww *= pow(n, max(NORMAL_RATIO / infoRatio.x, 1.0f));
                ww *= GetPosW(x, k);
                ww = max(ww, MIN_W);
                c.rgb += input[p].rgb * ww;

                total_w += ww;
                count++;
            }
        } break;

            //Execute pass 3 
        case 3: {
            //Pass 2 convolution failed again, make a minimal 2D pass
            [branch]
            if (prev_w < 0.0f && dist_to_cam < 100.0f) {
                k = kernel_size + full_kernel;
                for (x = -k; x <= k; ++x) {
                    for (y = -k; y <= k; ++y) {
                        int2 p = pixel + int2(x, y);
                        if (p.x < 0) { p.x += k; }
                        if (p.y < 0) { p.y += k; }
                        if (p.x > input_dimensions.x) { p.x -= k; }
                        if (p.y > input_dimensions.y) { p.y -= k; }
                        float2 p1_info_pixel = round(p * infoRatio);
                        ww = 1.0f;

                        float3 p1_position = positions[p1_info_pixel].xyz;
                        float world_dist = dist2(p1_position - p0_position) / (dist_to_cam);
                        ww = exp(-world_dist / (2.0f * sigma * sigma));

                        float3 p1_normal = normals[p1_info_pixel].xyz;
                        float n = saturate(dot(p1_normal, p0_normal));
                        ww *= pow(n, max(NORMAL_RATIO / infoRatio.x, 1.0f));
                        ww *= GetPosW(x, k) * GetPosW(y, k);;

                        c.rgb += orig_input[p].rgb * ww;
                        total_w += ww;
                        count++;
                    }
                }
                input_mix = saturate(prev_w - 1.5f);
            }
            else {
                // Pass2 convolution finished already, nothing to do in this pixel, just copy it
                c = input[pixel];
                total_w = 1.0f;
                c.a = 0.0f;
            }
        } break;
        }

        c = c / max(total_w, 1.0f);

        total_w /= k;
        if (type == 1) {
            c.a = total_w;
        }
        else {
            c.a = prev_w + total_w;
        }

        c = lerp(c, input[pixel], input_mix);
    }
#if 0
    if (type < 3) {
        output[pixel] = c;
    }
    else {
        float4 prev_pos = mul(prev_position_map[info_pixel], prev_view_proj);
        prev_pos.x /= prev_pos.w;
        prev_pos.y /= -prev_pos.w;
        prev_pos.xy = (prev_pos.xy + 1.0f) * input_dimensions.xy / 2.0f;
        float w = 0.3f;
        float4 prev_color = prev_output[floor(prev_pos.xy)];
        output[pixel] = lerp(prev_color, c, w);
    }
#else
    output[pixel] = c;
#endif

}
