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

//The lists of rays the screen space solver leaves for the world tracer, and the
//counters beside them. One layout, three readers: RayReflexScreenSolverCS fills them,
//RayListArgsCS turns the counts into the group counts of the indirect dispatches, and
//RayTraceCS walks them. RenderSystem.h's RT_LIST_* / RT_COUNT_* MUST match.

#ifndef __RAY_LISTS_HLSLI__
#define __RAY_LISTS_HLSLI__

//Where a ray may be solved (RenderSystem::eRtSpace, as bits).
#define RT_SPACE_SCREEN 1
#define RT_SPACE_WORLD 2

//The lists, in the one buffer: list n starts at n * list_capacity.
#define RT_LIST_REFLECT 0
#define RT_LIST_REFRACT 1

//The counters, in the one raw buffer, in uints. The first two are how many entries each
//list holds (and so what the dispatches are sized from); the others are a readout.
#define RT_COUNT_REFLECT 0
#define RT_COUNT_REFRACT 1
#define RT_COUNT_SCREEN_SOLVED 2
#define RT_COUNT_REFLECTIVE 3

//A pixel is its two coordinates in 16 bits each: textures are never wider than that.
uint PackRayPixel(uint2 pixel)
{
    return (pixel.y << 16) | (pixel.x & 0xFFFF);
}

uint2 UnpackRayPixel(uint packed)
{
    return uint2(packed & 0xFFFF, packed >> 16);
}

//Threads of a world tracer group: a list is walked in groups of this many entries.
#define RAY_LIST_GROUP 64

#endif
