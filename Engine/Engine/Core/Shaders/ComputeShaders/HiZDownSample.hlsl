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

//One level of the Hi-Z pyramid the screen space ray solvers march through: every
//texel of `output` is the NEAREST surface (the minimum world distance of depth_map)
//over the `ratio` x `ratio` block of `input` it covers. A ray in front of that
//distance cannot hit anything inside the block, which is what lets the march skip it.
//
//One pass, not the two separable ones this was first written as: a level is 5x5
//texels of the one below, so the single gather is 25 reads of a texture a fifth the
//size, and it needs no full-resolution temporary to carry the vertical half.
//
//The block starts AT the texel's first input pixel. The version this came from
//started one pixel late (it stepped before it read), so every cell described the
//pixels one to the right and one below its own - and a minimum over the wrong
//pixels is not a conservative bound, it is the thing that lets a ray skip over a
//surface that is really there.

#include "../Common/Defines.hlsli"

cbuffer externalData : register(b0)
{
    uint ratio;
}

Texture2D<float> input : register(t0);
RWTexture2D<float> output : register(u0);

#define NTHREADS 8

[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 input_dimensions;
    uint2 output_dimensions;
    input.GetDimensions(input_dimensions.x, input_dimensions.y);
    output.GetDimensions(output_dimensions.x, output_dimensions.y);
    if (DTid.x >= output_dimensions.x || DTid.y >= output_dimensions.y) {
        return;
    }

    //depth_map is cleared to FLT_MAX where nothing was drawn (the sky), and that is
    //also what an empty block keeps: nothing in it can be hit.
    float depth = FLT_MAX;
    uint2 first = DTid.xy * ratio;
    for (uint y = 0; y < ratio; ++y) {
        for (uint x = 0; x < ratio; ++x) {
            uint2 p = first + uint2(x, y);
            if (p.x < input_dimensions.x && p.y < input_dimensions.y) {
                float d = input[p];
                //A negative distance is not a surface.
                if (d >= 0.0f) {
                    depth = min(depth, d);
                }
            }
        }
    }
    output[DTid.xy] = depth;
}
