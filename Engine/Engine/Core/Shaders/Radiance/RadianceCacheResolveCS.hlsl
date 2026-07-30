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

//Turns one frame's deposits into the value everything else reads: mean of this
//frame's samples, blended into the running estimate at a rate that tightens as the
//cell converges, accumulator reset, and cells nobody has touched in RC_MAX_AGE
//frames freed.
//
//One thread per entry over the whole table, every frame. That sounds wasteful and
//is not: the table is 16 MB, so a pass that only reads the key word and exits costs
//about the memory bandwidth of one small texture. The alternative - having the
//trace append touched cells to a list and dispatching indirectly over it - saves
//that read and costs an append per deposit, which is an atomic on a single counter
//in the hottest loop in the frame. Measured, this is the cheaper side.

#include "../Common/Utils.hlsli"
#include "../Common/RadianceCache.hlsli"

cbuffer externalData : register(b0)
{
    uint frame_count;
    uint reset;
}

#define NTHREADS 64

//Group reduction for the counters. A per-thread InterlockedAdd on one address
//serializes the entire dispatch - 524288 threads queueing on four words - so each
//group reduces into shared memory and issues one atomic per counter.
groupshared uint g_live;
groupshared uint g_touched;
groupshared uint g_evicted;

[numthreads(NTHREADS, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID)
{
    [branch]
    if (GTid.x == 0) {
        g_live = 0u;
        g_touched = 0u;
        g_evicted = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    uint idx = DTid.x;
    [branch]
    if (idx < RC_ENTRIES) {
        uint addr = idx * RC_STRIDE;
        uint vaddr = idx * RC_VALUE_STRIDE;
        uint key_lo = rcache.Load(addr + RC_OFF_KEY_LO);

        //A level load clears the table outright rather than waiting for every cell
        //to age out: the old scene's geometry is gone, and its cells would otherwise
        //keep answering lookups for RC_MAX_AGE frames of the new one.
        [branch]
        if (reset != 0u) {
            rcache.Store4(addr + RC_OFF_KEY_LO, uint4(RC_KEY_FREE, 0u, 0u, 0u));
            rcache.Store4(addr + RC_OFF_ACCUM_B, uint4(0u, 0u, 0u, 0u));
            rcache_value.Store4(vaddr, uint4(0u, 0u, 0u, 0u));
        }
        else if (key_lo != RC_KEY_FREE && key_lo != RC_KEY_TOMBSTONE) {
            uint4 acc = rcache.Load4(addr + RC_OFF_ACCUM_R); //r,g,b,count
            uint last_frame = rcache.Load(addr + RC_OFF_FRAME);
            float4 value = asfloat(rcache_value.Load4(vaddr));

            [branch]
            if (acc.w > 0u) {
                float3 mean = float3(acc.x, acc.y, acc.z) /
                              (RC_FIXED_SCALE * (float)acc.w);

                //Cold cells take the new mean outright and converged ones barely
                //move, but never less than RC_MIN_BLEND: a cell that stopped
                //responding entirely could not follow a light being switched on.
                float blend = max(1.0f - value.w, RC_MIN_BLEND);
                value.rgb = lerp(value.rgb, mean, blend);
                value.w = min(value.w + RC_CONF_STEP, 1.0f);

                rcache.Store4(addr + RC_OFF_ACCUM_R, uint4(0u, 0u, 0u, 0u));
                rcache_value.Store4(vaddr, asuint(value));
            }

            [branch]
            if (frame_count - last_frame > RC_MAX_AGE) {
                //Tombstone rather than free: a probe run that hit a zero here would
                //stop, orphaning every cell that had probed past this slot.
                rcache.Store(addr + RC_OFF_KEY_LO, RC_KEY_TOMBSTONE);
                rcache_value.Store4(vaddr, uint4(0u, 0u, 0u, 0u));
                uint ignored;
                InterlockedAdd(g_evicted, 1u, ignored);
            }
            else {
                uint ignored;
                InterlockedAdd(g_live, 1u, ignored);
                [branch]
                if (acc.w > 0u) {
                    InterlockedAdd(g_touched, 1u, ignored);
                }
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();
    [branch]
    if (GTid.x == 0) {
        uint ignored;
        rcache_stats.InterlockedAdd(RC_STAT_LIVE, g_live, ignored);
        rcache_stats.InterlockedAdd(RC_STAT_TOUCHED, g_touched, ignored);
        rcache_stats.InterlockedAdd(RC_STAT_EVICTED, g_evicted, ignored);
    }
}
