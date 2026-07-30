/*
The HotBite Game Engine

Copyright(c) 2023 Vicente Sirvent Orts

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

//Decoding for the `debug` value the render system pushes into TextureMixerCS,
//GIAverageCS and DenoiserCS.
//
//This file MUST stay in step with RenderSystem::eDebugBuffer and the
//RT_DEBUG_* constants in Systems/RenderSystem.h - the value crosses from C++
//to HLSL as a bare uint and nothing checks it. The editor's Render menu names
//the buffers in the same order.
//
//Layout: the low byte selects one buffer to show in place of the mixed frame,
//the bits above it are independent bypasses. They are deliberately separable -
//looking at the raw indirect light with its denoiser switched off is the whole
//point of having both.
#ifndef _RENDER_DEBUG_HLSLI
#define _RENDER_DEBUG_HLSLI

#define RT_DEBUG_BUFFER_MASK      0xFFu
#define RT_DEBUG_NO_GI_DENOISE    0x100u
#define RT_DEBUG_NO_RT_DENOISE    0x200u

#define RT_DEBUG_BUFFER_OFF          0u
#define RT_DEBUG_BUFFER_SCENE        1u
#define RT_DEBUG_BUFFER_LIGHT        2u
#define RT_DEBUG_BUFFER_BLOOM        3u
#define RT_DEBUG_BUFFER_EMISSION     4u
#define RT_DEBUG_BUFFER_REFLECTION   5u
#define RT_DEBUG_BUFFER_REFRACTION   6u
#define RT_DEBUG_BUFFER_INDIRECT     7u
#define RT_DEBUG_BUFFER_VOLUMETRIC   8u
#define RT_DEBUG_BUFFER_DUST         9u
#define RT_DEBUG_BUFFER_LENS_FLARE  10u
#define RT_DEBUG_BUFFER_DEPTH       11u
#define RT_DEBUG_BUFFER_POSITION    12u
#define RT_DEBUG_BUFFER_NORMAL      13u
#define RT_DEBUG_BUFFER_MOTION      14u
#define RT_DEBUG_BUFFER_GI_CACHE    15u
#define RT_DEBUG_BUFFER_GI_CACHE_CONF 16u

#define DebugBuffer(d)   ((d) & RT_DEBUG_BUFFER_MASK)
#define DebugFlag(d, f)  (((d) & (f)) != 0u)

//Two of the three buffers below are not colours, so they get a mapping rather
//than being dumped raw. Both are chosen to show *discontinuities*, which is what
//you are looking for when one of these is wrong.

//World position -> a 10-unit repeating ramp per axis. Absolute world coordinates
//have no useful display range, but the banding makes a seam or a stale reprojection
//obvious at a glance.
float3 DebugPositionColor(float3 world_pos)
{
    return frac(abs(world_pos) / 10.0f);
}

//World normal -> the usual [-1,1] to [0,1] remap.
float3 DebugNormalColor(float3 normal)
{
    return normal * 0.5f + 0.5f;
}

//Screen-space motion -> the usual velocity-buffer encoding: mid grey is a pixel that did
//not move, red/green deflect with +x/+y and cyan/magenta with -x/-y. Unlike the mappings
//above this one *is* scaled by debug_gain, because the useful range is enormous: a slow
//pan is a thousandth of NDC and a fast one is tenths, and there is no natural display
//range to pick. Gain 1 is calibrated so a full-screen sweep saturates.
//
//Pixels the motion pass rejected (nothing drawn there) are flagged in blue rather than
//left at grey, because "no motion vector" and "a zero motion vector" are the two states
//you are usually trying to tell apart.
float3 DebugMotionColor(float2 motion, float gain)
{
    if (motion.x < -1e30f) {
        return float3(0.0f, 0.0f, 0.6f);
    }
    return float3(saturate(0.5f + motion * gain * 25.0f), 0.5f);
}

//depth_map holds the world distance from the camera, cleared to FLT_MAX where
//nothing was drawn. An exponential falloff keeps the near field readable without
//needing to know the scene's scale, and the empty background stays black instead
//of saturating white.
float3 DebugDepthColor(float distance)
{
    if (distance >= FLT_MAX) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    return (1.0f - exp(-distance * 0.01f)).xxx;
}

//How converged a radiance cache cell is: black where there is no cell at all, then
//a cold-to-hot ramp from a cell resolved once to one that has been fed for
//1/RC_CONF_STEP frames. This is what the stage 2 blend reads, so a surface that
//stays blue is one the cache is not yet allowed to answer for - which is the
//difference between "the cache is not working" and "the cache has not got there
//yet". A ramp, so it ignores debug_gain.
float3 DebugCacheConfidenceColor(float confidence, bool present)
{
    if (!present) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    float c = saturate(confidence);
    return float3(c, 1.0f - abs(c * 2.0f - 1.0f), 1.0f - c);
}

#endif
