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

//The world-space radiance cache: a hash grid of irradiance, keyed by where a
//surface point is rather than by which pixel is looking at it.
//
//WHY IT EXISTS. The ReSTIR pass in GIRayTraceCS.hlsl keeps its convergence in two
//screen-space places - the per-pixel pdf (restir_pdf) and the denoised history
//(GIAverageCS pass 3). Both are keyed by pixel, so both die when the camera moves:
//the pdf is not even reprojected, it is blended toward flat in proportion to how
//far the pixel moved, because a pdf that describes a *different* surface point is
//worse than no pdf. Rotating the camera therefore restarts every pixel from a
//uniform distribution at the 1-2 rays a frame the pass can afford, and a newly
//disoccluded surface starts from nothing at all. This cache is where that
//convergence goes instead: it is attached to the surface, so it survives.
//
//WHAT IS STORED. Irradiance - the light *arriving* at a cell, not leaving it. That
//is the same quantity GIRayTraceCS already estimates per pixel and the same one the
//texture mixer multiplies by albedo (`color * (l + rt0 + rt2)`), so there is
//exactly one albedo multiply on every path through it. Getting that inconsistent
//between the two read sites is the classic way to end up with a scene that is
//subtly, unexplainably too bright.
//
//THE KEY is quantized position + a level derived from distance to the camera + a
//normal bucket. The level makes a cell roughly constant in screen space, so near
//geometry is cached finely and distant geometry cheaply - the same reasoning as the
//cascade fit and the LOD ratio. The normal bucket is what stops a floor and the
//ceiling below it sharing a cell, which is the classic hash-grid light leak.
//
//TWO BUFFERS, and the split is deliberate:
//  rcache       - the atomically updated part: key, this frame's accumulator, age.
//  rcache_value - the resolved value, written only by RadianceCacheResolveCS and
//                 read by everyone else, so a lookup is one 16-byte load and never
//                 contends with the deposits.
//
//A shader that only reads defines RC_READ_ONLY before including this; it then gets
//ByteAddressBuffer instead of RWByteAddressBuffer and none of the deposit code.

#ifndef _RADIANCE_CACHE_HLSLI
#define _RADIANCE_CACHE_HLSLI

#include "Utils.hlsli"

//Table size. Power of two: the slot is a mask, not a modulo. MUST equal
//RenderSystem::RADIANCE_CACHE_ENTRIES - the buffers are raw, so nothing checks it and
//a mismatch reads neighbouring cells as this one's colour.
//
//Sized from measurement, not from the "far more than a scene can fill" guess that was
//here at 524288. Cells are held at a constant screen footprint, so a single view needs
//about (screen pixels)/(cell pixels)^2 ~ 32k of them; what fills the table is not one
//view but RC_MAX_AGE frames of a *moving* one, at whatever level each surface was seen.
//Measured on sponza, orbiting the camera two full turns: 151k live cells, 29% of
//524288, with deposits already being dropped from 12.7% occupancy on. At 1M the same
//walk sits near 15%, which linear probing handles with room to spare.
//
//48 bytes an entry across the two buffers, so this is 50 MB. That is the real price of
//the feature and it is worth stating plainly.
#define RC_ENTRIES        1048576u
#define RC_MASK           (RC_ENTRIES - 1u)

//Bytes per record in each buffer. Spelled out because the buffers are raw - see
//the note on Core::RWByteBuffer for why they are not structured.
#define RC_STRIDE         32u
#define RC_VALUE_STRIDE   16u

//  rcache record:            rcache_value record:
//   +0  key_lo (0 free,       +0  irradiance r
//               1 tombstone)  +4  irradiance g
//   +4  key_hi                +8  irradiance b
//   +8  accum r               +12 confidence 0..1
//   +12 accum g
//   +16 accum b
//   +20 sample count
//   +24 last frame touched
//   +28 reserved
#define RC_OFF_KEY_LO     0u
#define RC_OFF_KEY_HI     4u
#define RC_OFF_ACCUM_R    8u
#define RC_OFF_ACCUM_G    12u
#define RC_OFF_ACCUM_B    16u
#define RC_OFF_COUNT      20u
#define RC_OFF_FRAME      24u

#define RC_KEY_FREE       0u
#define RC_KEY_TOMBSTONE  1u

//How many slots a key may probe before the deposit is dropped. Four was chosen for a
//table assumed to stay near-empty, and it does not survive a real load factor: on
//sponza, deposits started failing at 12.7% occupancy and reached 1% of all deposits by
//29%. Eight roughly squares the odds of a full run at a given load, and costs nothing
//at low occupancy because both loops exit on the first free or matching slot - the
//extra probes are only walked when the table is genuinely contended.
#define RC_PROBES         8u

//Fixed point for the atomic accumulator. Radiance is clamped to RC_MAX_RADIANCE
//first, so one sample contributes at most 16 * 4096 = 65536 and a cell would need
//65000 samples in one frame to overflow a uint - far past what any cell receives.
//The clamp doubles as the firefly guard on the way in.
#define RC_FIXED_SCALE    4096.0f
#define RC_MAX_RADIANCE   16.0f

//Cell size: RC_BASE_SIZE world units at the camera, doubling every time the distance
//doubles past 1/RC_LEVEL_SCALE. Away from the level steps this works out at roughly
//`RC_BASE_SIZE * RC_LEVEL_SCALE * 10 * distance` world units.
//
//These were 0.25 / 0.1, which put a cell at about 0.025 * distance - a block ~24
//full-resolution pixels across, held constant in screen space by design. That is far
//too coarse to be read per pixel: it is precisely the size of the cubes that showed up
//on sponza's columns, where a cell was as wide as the column it was shading. Sizing a
//cell for "constant screen footprint" is right, but the footprint has to be small
//enough that the jitter below dithers between neighbours at a scale the denoiser can
//absorb - a few pixels, not two dozen.
//
//At 0.125 / 0.05 a cell is about 6 full-resolution pixels. That is 16x more cells for
//a given surface; sponza goes from ~1.5k live to a few tens of thousands, against
//RC_ENTRIES of 524288, so there is ample room. Watch `dropped` in gi_cache_info if
//this is reduced further.
#define RC_BASE_SIZE      0.125f
#define RC_LEVEL_SCALE    0.05f
#define RC_MAX_LEVEL      12u

//A cell not deposited into for this many frames is freed. This is the memory the
//whole cache exists for: turning away from a wall and back must find the wall's
//answer still there, and at 60 fps this is about eight seconds of that. 64 frames
//(one second) was the first value here and is far too short - it expires a surface
//in about the time it takes to look away from it, which is exactly the case the
//screen-space path already handles badly.
//
//The cost of a longer memory is live cells, and this is the number that governs how
//many: everything seen in the window is retained. 512 was picked as "about eight
//seconds at 60 fps" - but sponza with ray tracing on runs at 20-30, where 512 frames is
//17-25 seconds of accumulation, and that is what filled the table. 256 is the same
//eight seconds at the frame rate this actually runs at.
#define RC_MAX_AGE        256u

//Convergence rate. A cold cell takes its first frame's mean outright (blend 1) and
//tightens toward RC_MIN_BLEND as confidence builds, which it does in 1/RC_CONF_STEP
//frames that receive samples. RC_MIN_BLEND is a floor rather than 0 on purpose: a
//fully converged cell must still be able to follow a moving light, and this bounds
//how long that takes to about 20 frames.
#define RC_CONF_STEP      0.05f
#define RC_MIN_BLEND      0.05f

//How much of a cell's cached irradiance a ray carries away from a hit. The cache is
//fed by rays that themselves read it, so this is a feedback loop: energy at a cell
//is direct + albedo * (what the cache already held), which converges only while the
//product stays under 1. Albedo alone does not guarantee that - a white material is
//~1, and the loop would then hold energy instead of losing it, brightening a corner
//frame after frame with nothing to stop it but the RC_MAX_RADIANCE clamp. Slightly
//under 1 makes the series converge for any material while costing a few percent of
//the light at the second bounce and less at every one after.
#define RC_BOUNCE_GAIN    0.9f

//How far the denoiser is allowed to lean on the cache at a primary pixel that still
//has usable screen-space history. Well under 1 on purpose: where the history is good
//the screen estimate is the better of the two - it carries contact detail at pixel
//resolution that a cell several centimetres across cannot represent - so the cache is
//a correction there, not a replacement. A pixel with *no* history ignores this and
//takes the cache outright, which is the case the cache exists for.
#define RC_PRIMARY_BLEND  0.6f

//Master switch for the primary-pixel fill, so the A/B that measures it can turn the
//whole thing off. RC_PRIMARY_BLEND is not that switch: a pixel with no history at all
//ignores it and takes the cache outright, which is precisely the case being measured.
#define RC_PRIMARY_ENABLE 1.0f

//Master switch for the lookup jitter, so the A/B that shows what it is worth can turn
//it off. 0 makes every lookup read the single cell the point falls in, which is what
//draws the cache's cells on screen as cubes.
#define RC_JITTER_ENABLE  1.0f

//Stats, one uint each. Written with a group reduction and a single atomic per
//group - a per-thread atomic on one address serializes the whole dispatch.
#define RC_STAT_LIVE      0u
#define RC_STAT_TOUCHED   4u
#define RC_STAT_EVICTED   8u
#define RC_STAT_DEPOSIT   12u
#define RC_STAT_DROPPED   16u
#define RC_STAT_HIT       20u
#define RC_STAT_MISS      24u
#define RC_STATS_BYTES    32u

#ifdef RC_READ_ONLY
ByteAddressBuffer   rcache;
ByteAddressBuffer   rcache_value;
#else
RWByteAddressBuffer rcache;
RWByteAddressBuffer rcache_value;
RWByteAddressBuffer rcache_stats;
#endif

//Which of the six axis-aligned directions the normal most points along. Six is
//enough to separate the surfaces that share a cell in practice (a floor from a
//ceiling, either side of a wall) without fragmenting the cache the way a finer
//bucketing would - every extra bucket is another cell to converge separately.
uint RCNormalBucket(float3 n)
{
    float3 a = abs(n);
    [branch]
    if (a.x >= a.y && a.x >= a.z) { return n.x >= 0.0f ? 0u : 1u; }
    [branch]
    if (a.y >= a.z)               { return n.y >= 0.0f ? 2u : 3u; }
    return n.z >= 0.0f ? 4u : 5u;
}

uint RCLevel(float3 pos, float3 camera_pos)
{
    float d = length(pos - camera_pos);
    return (uint)clamp(log2(max(d * RC_LEVEL_SCALE, 1.0f)), 0.0f, (float)RC_MAX_LEVEL);
}

float RCCellSize(uint level)
{
    return RC_BASE_SIZE * exp2((float)level);
}

//The key, the check word and the slot, all from one mix. key_lo is what the
//compare-exchange claims a slot with, so it must never collide with the two
//reserved values; key_hi is stored beside it and verifies that a slot holding the
//same key_lo really is this cell. The slot is taken from key_hi rather than key_lo
//so that two cells landing in the same slot are not also likely to share the word
//that distinguishes them.
//`jitter` displaces the point inside its own cell before quantizing, in cell units
//and expected in [-0.5, 0.5]. Zero for a deposit; random per pixel and per frame for
//a lookup - see RCLookupJittered for why that is not optional.
void RCKeyJittered(float3 pos, float3 normal, float3 camera_pos, float3 jitter,
                   out uint key_lo, out uint key_hi, out uint slot)
{
    uint level = RCLevel(pos, camera_pos);
    float size = RCCellSize(level);
    int3 cell = (int3)floor(pos / size + jitter);
    uint nb = RCNormalBucket(normal);

    uint h = (uint)cell.x * 73856093u ^ (uint)cell.y * 19349663u ^ (uint)cell.z * 83492791u;
    h = hash(h + level * 2654435761u + nb * 40503u);

    key_lo = h | 1u;
    //h of 0 or 1 would otherwise produce the tombstone marker as a real key.
    key_lo = (key_lo == RC_KEY_TOMBSTONE) ? 3u : key_lo;
    key_hi = hash(h ^ 0x9E3779B9u);
    slot = key_hi & RC_MASK;
}

void RCKey(float3 pos, float3 normal, float3 camera_pos,
           out uint key_lo, out uint key_hi, out uint slot)
{
    RCKeyJittered(pos, normal, camera_pos, 0.0f.xxx, key_lo, key_hi, slot);
}

//Read-only lookup. Returns the entry index or -1.
//
//The probe run stops at a free slot but steps over a tombstone: an evicted cell
//that simply zeroed its key would cut the chain and orphan every cell probed past
//it, which reads as a cache that mysteriously stops answering for one region.
int RCFindJittered(float3 pos, float3 normal, float3 camera_pos, float3 jitter)
{
    uint key_lo, key_hi, slot;
    RCKeyJittered(pos, normal, camera_pos, jitter, key_lo, key_hi, slot);

    [loop]
    for (uint p = 0u; p < RC_PROBES; ++p) {
        uint idx = (slot + p) & RC_MASK;
        uint2 k = rcache.Load2(idx * RC_STRIDE + RC_OFF_KEY_LO);
        [branch]
        if (k.x == key_lo && k.y == key_hi) { return (int)idx; }
        [branch]
        if (k.x == RC_KEY_FREE) { return -1; }
    }
    return -1;
}

//The value of a cell, or black with zero confidence when there is none. `w` is the
//confidence: 0 for a cell that has never resolved, 1 for one that has been fed for
//1/RC_CONF_STEP frames. Callers use it to decide how far to trust the cache against
//whatever else they have.
//
//THIS IS THE UNJITTERED FORM AND IT PRODUCES VISIBLE CUBES. A cell is constant
//across its whole volume, so reading one cell per pixel makes a piecewise-constant
//image - which is exactly what a voxel grid looks like on screen, and it is glaring
//while the camera moves because that is when the primary-pixel fill leans hardest on
//the cache. Use it only where the result is already being averaged over many samples;
//anything that puts a lookup on screen per pixel wants RCLookupJittered.
float4 RCLookup(float3 pos, float3 normal, float3 camera_pos)
{
    int idx = RCFindJittered(pos, normal, camera_pos, 0.0f.xxx);
    [branch]
    if (idx < 0) { return float4(0.0f, 0.0f, 0.0f, 0.0f); }
    return asfloat(rcache_value.Load4((uint)idx * RC_VALUE_STRIDE));
}

//The same lookup with the point displaced randomly inside its own cell first, which
//is what makes a hash grid usable on screen.
//
//Quantizing turns a continuous position into one constant per cell, so neighbouring
//pixels either agree exactly or jump - the cube edges. Displacing each pixel by up to
//half a cell before quantizing means a pixel near a boundary lands in either
//neighbour, with probability proportional to how close it is: the *average* over
//pixels is the trilinear interpolation of the surrounding cells, and the error left
//per pixel is noise instead of a step. Noise is what this renderer's denoiser and
//temporal accumulation are built to remove; a step is not, and no amount of blurring
//makes an edge stop reading as an edge.
//
//`seed` must vary per pixel AND per frame, or the dither is a fixed pattern that the
//temporal accumulation happily preserves.
float4 RCLookupJittered(float3 pos, float3 normal, float3 camera_pos, uint seed)
{
    float3 jitter = (float3(random(seed), random(seed + 0x9E3779B9u),
                            random(seed + 0x85EBCA6Bu)) - 0.5f) * RC_JITTER_ENABLE;
    int idx = RCFindJittered(pos, normal, camera_pos, jitter);
    [branch]
    if (idx < 0) { return float4(0.0f, 0.0f, 0.0f, 0.0f); }
    return asfloat(rcache_value.Load4((uint)idx * RC_VALUE_STRIDE));
}

#ifndef RC_READ_ONLY

//Insert if needed, then add one sample. Returns false when every probe slot was
//taken by another cell, which is the only way a sample is lost.
//
//The claim is a compare-exchange on key_lo followed by a plain store of key_hi, so
//there is a window in which another thread sees the key claimed but the check word
//still zero. That thread probes on and may create a duplicate of this cell
//elsewhere in the table; the duplicate converges to the same value and ages out on
//its own. Closing the window would need a lock per slot, which costs far more than
//the occasional duplicate.
bool RCDeposit(float3 pos, float3 normal, float3 camera_pos, float3 radiance, uint frame)
{
    uint key_lo, key_hi, slot;
    RCKey(pos, normal, camera_pos, key_lo, key_hi, slot);

    [loop]
    for (uint p = 0u; p < RC_PROBES; ++p) {
        uint idx = (slot + p) & RC_MASK;
        uint addr = idx * RC_STRIDE;
        uint old = 0u;

        rcache.InterlockedCompareExchange(addr + RC_OFF_KEY_LO, RC_KEY_FREE, key_lo, old);
        [branch]
        if (old == RC_KEY_FREE) {
            rcache.Store(addr + RC_OFF_KEY_HI, key_hi);
        }
        else if (old == RC_KEY_TOMBSTONE) {
            rcache.InterlockedCompareExchange(addr + RC_OFF_KEY_LO, RC_KEY_TOMBSTONE, key_lo, old);
            if (old != RC_KEY_TOMBSTONE && old != key_lo) { continue; }
            rcache.Store(addr + RC_OFF_KEY_HI, key_hi);
        }
        else if (old != key_lo || rcache.Load(addr + RC_OFF_KEY_HI) != key_hi) {
            continue;
        }

        float3 c = clamp(radiance, 0.0f, RC_MAX_RADIANCE) * RC_FIXED_SCALE;
        uint ignored;
        rcache.InterlockedAdd(addr + RC_OFF_ACCUM_R, (uint)c.r, ignored);
        rcache.InterlockedAdd(addr + RC_OFF_ACCUM_G, (uint)c.g, ignored);
        rcache.InterlockedAdd(addr + RC_OFF_ACCUM_B, (uint)c.b, ignored);
        rcache.InterlockedAdd(addr + RC_OFF_COUNT, 1u, ignored);
        rcache.Store(addr + RC_OFF_FRAME, frame);
        return true;
    }
    return false;
}

#endif //RC_READ_ONLY

#endif //_RADIANCE_CACHE_HLSLI
