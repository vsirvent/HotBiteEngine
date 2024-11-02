#include "../Common/ShaderStructs.hlsli"

globallycoherent RWTexture2D<float4> output : register(u1);
globallycoherent RWByteAddressBuffer outputLock : register(u2);

Texture2D<float4> input : register(t0);
Texture2D<float2> motionTexture : register(t1);

cbuffer externalData : register(b0)
{
    matrix view_proj;
    matrix prev_view_proj;
    int enabled;
}

float2 GetPixelDir(float2 pixel) {
    return motionTexture[pixel].xy;
}

#define MAX_STEPS 30
#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 Gid: SV_GroupID, uint3 Tid: SV_GroupThreadID)
{
    float2 pixel = float2(DTid.x, DTid.y);
    
    uint w, h;
    float2 in_dimension;
    float2 out_dimension;

    input.GetDimensions(w, h);
    in_dimension.x = w;
    in_dimension.y = h;

    output.GetDimensions(out_dimension.x, out_dimension.y);
    float2 output_ratio = out_dimension / in_dimension;
    float2 input_ratio = 1.0f / output_ratio;
    float2 input_pixel = pixel * input_ratio;
    float4 inputColor = input[input_pixel];
        
    float2 dir = GetPixelDir(input_pixel);
    float orig_speed = length(dir);
    float2 norm_dir = normalize(dir);
    dir *= in_dimension.x;

    float2 max_speed_f2 = in_dimension * 0.05f;
    float max_speed = min(max_speed_f2.x, max_speed_f2.y);
    float speed = length(dir);

    if (speed > length(max_speed)) {
         dir = normalize(dir) * max_speed;
    }

    float fsteps = length(dir);
    
    if (enabled && fsteps >= 1.0f) {
        float step_size = length(dir / fsteps);

        float n = 1.0f;
        float t = 1.0f;
        int i = 0;
        int real_steps = 0;

        while (n < fsteps) { // && n < MAX_STEPS) {
            float a = (fsteps - n) / fsteps;
            t += a;
            n += step_size;
            ++real_steps;
        }

        n = step_size;
        dir /= fsteps;
        float ratio = (float)fsteps / real_steps;
        float2 p = pixel - dir * ratio * real_steps / 4.0f;
        float4 last_color = inputColor;

        float4 output_color = inputColor / t;
        if (real_steps > 0) {
            for (i = 0; i < real_steps; ++i) {
                p += dir * ratio;
                float2 p2 = p * input_ratio;
                float a = (fsteps - n) / (fsteps * t);
                float2 pdir = GetPixelDir(p2);
                float2 npdir = normalize(pdir);
                float angle = saturate(dot(norm_dir, npdir));
                
                last_color = input[p2] * angle + last_color * (1.0f - angle);
                output_color += last_color * a;
                n += step_size;
            }
        }
        output[pixel] = output_color;
    }
    else {
        output[pixel] = inputColor;
    }
}
