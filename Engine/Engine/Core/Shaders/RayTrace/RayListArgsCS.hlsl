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

//Turns how many rays each list holds into the group counts of the dispatches that
//trace them. The CPU never learns the counts - reading them back to size an ordinary
//Dispatch would stall the pipeline every frame - so the dispatches are indirect and
//this is what writes their arguments. See RayLists.hlsli.

#include "RayLists.hlsli"

//Three uints per list (x, y, z groups), in the order of the lists.
RWBuffer<uint> dispatch_args : register(u0);
RWByteAddressBuffer ray_counts : register(u1);

[numthreads(1, 1, 1)]
void main()
{
    [unroll]
    for (uint list = 0; list < 2; ++list) {
        uint count = ray_counts.Load(list * 4);
        dispatch_args[list * 3 + 0] = (count + RAY_LIST_GROUP - 1) / RAY_LIST_GROUP;
        dispatch_args[list * 3 + 1] = 1;
        dispatch_args[list * 3 + 2] = 1;
    }
}
