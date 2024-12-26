/*
The HotBite Game Engine

Copyright(c) 2024 Vicente Sirvent Orts

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "..\Common\Defines.hlsli"
cbuffer externalData : register(b0)
{
    uint ratio;
    uint type;
}

Texture2D<float> input : register(t0);
RWTexture2D<float> output : register(u0);

#define NTHREADS 32
#define VERTICAL 1
#define HORIZONTAL 2

[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 input_dimensions = { 0, 0 };
    input.GetDimensions(input_dimensions.x, input_dimensions.y);
    
    float2 in_pixel;
    float2 out_pixel = float2(DTid.x, DTid.y);
    float depth = FLT_MAX;
    uint2 dir = { 0, 0 };
    int kernel_size = ratio / 2;
    
    if (type == VERTICAL) {
        dir = uint2(0, 1);        
        in_pixel = float2(DTid.x, DTid.y * ratio);
    }
    else if (type == HORIZONTAL) {
        dir = uint2(1, 0);
        in_pixel = float2(DTid.x * ratio, DTid.y);
    }

    uint2 tpos = in_pixel;
    for (int i = -kernel_size; i <= kernel_size; ++i) {
        tpos += dir;
        if (tpos.x >= 0 && tpos.x < input_dimensions.x && tpos.y >= 0 && tpos.y < input_dimensions.y) {
            float tmp_depth = input[tpos];
            if (tmp_depth < FLT_MAX) {
                depth = min(depth, tmp_depth);
            }
        }
    }
    
    if (depth < 0.0f) {
        depth = FLT_MAX;
    }

    output[out_pixel] = depth;
}
