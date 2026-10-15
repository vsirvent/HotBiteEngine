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

// Temporal anti-aliasing over the whole frame, in place of FXAA.
//
// The geometry of every frame is drawn through a projection nudged by a different sub-pixel
// offset, so over a few frames each pixel is sampled at several places. This blends the
// current frame with the previous result, found where the motion vectors say the surface was.
// The motion vectors are built from the camera's own unjittered matrices, so the offset is
// not part of what they measure.
//
// What keeps the history honest:
//  - it is fetched with a Catmull-Rom filter, which blurs less than a bilinear tap does when
//    the sub-pixel motion would otherwise resample it every frame;
//  - it is clipped to the colour box of the current frame's 3x3 neighbourhood (variance
//    clipping, in YCoCg), so what the surface no longer looks like - a disocclusion, a
//    shadow that moved - cannot ghost for longer than the frame it takes the box to catch up;
//  - the weight of the current frame rises with the speed of the pixel.
// The noise of the ray traced shadows and the indirect light is inside that box, so it is
// averaged over the history instead of clipped away.
//
// Colour is HDR here (the mix is not tone mapped yet), so everything that compares or blends
// does it on c / (1 + luma(c)), which a single bright pixel cannot dominate; the result is
// taken back the same way.

cbuffer externalData : register(b0)
{
    // 0: the history holds nothing worth blending with; the result is the current frame.
    int history_valid;
}

Texture2D<float4> input : register(t0);
Texture2D<float4> history : register(t1);
Texture2D<float2> motionTexture : register(t2);
SamplerState linearSampler : register(s0);

RWTexture2D<float4> output : register(u0);
RWTexture2D<float4> history_out : register(u1);

// How wide, in standard deviations, the colour box the history is clipped to is.
#define CLIP_GAMMA 1.25f
// The weight of the current frame at rest, and at SPEED_FULL pixels of motion a frame.
#define BLEND_STILL 0.1f
#define BLEND_MOVING 0.3f
#define SPEED_FULL 8.0f
// Past this a motion vector is the "no surface here" marker MotionCS writes (-FLT_MAX).
#define MOTION_INVALID 1e20f
#define MAX_HDR 60000.0f

float Luma(float3 c)
{
    return dot(c, float3(0.299f, 0.587f, 0.114f));
}

float3 Warp(float3 c)
{
    return c / (1.0f + Luma(c));
}

float3 Unwarp(float3 c)
{
    return c / max(1.0f - Luma(c), 1.0f / 65536.0f);
}

float3 ToYCoCg(float3 c)
{
    return float3(
        0.25f * c.r + 0.5f * c.g + 0.25f * c.b,
        0.5f * c.r - 0.5f * c.b,
        -0.25f * c.r + 0.5f * c.g - 0.25f * c.b);
}

float3 FromYCoCg(float3 c)
{
    return float3(
        c.x + c.y - c.z,
        c.x + c.z,
        c.x - c.y - c.z);
}

// A frame the ray tracers wrote a NaN or an infinity into must not get into the history, where it
// would stay (the history is blended with itself).
float3 Sanitize(float3 c)
{
    return (all(isfinite(c))) ? clamp(c, 0.0f, MAX_HDR) : float3(0.0f, 0.0f, 0.0f);
}

float3 LoadColor(int2 p, int2 size)
{
    return Sanitize(input[clamp(p, int2(0, 0), size - 1)].rgb);
}

// Catmull-Rom through five bilinear taps (the corners of the 4x4 footprint are left out, their
// weight is too small to matter).
float3 SampleHistoryCatmullRom(float2 uv, float2 size)
{
    float2 position = uv * size;
    float2 center = floor(position - 0.5f) + 0.5f;
    float2 f = position - center;
    float2 f2 = f * f;
    float2 f3 = f2 * f;

    float2 w0 = -0.5f * f3 + f2 - 0.5f * f;
    float2 w1 = 1.5f * f3 - 2.5f * f2 + 1.0f;
    float2 w2 = -1.5f * f3 + 2.0f * f2 + 0.5f * f;
    float2 w3 = 0.5f * f3 - 0.5f * f2;

    float2 w12 = w1 + w2;
    float2 tc12 = (center + w2 / w12) / size;
    float2 tc0 = (center - 1.0f) / size;
    float2 tc3 = (center + 2.0f) / size;

    float2 lo = 0.5f / size;
    float2 hi = 1.0f - lo;
    tc12 = clamp(tc12, lo, hi);
    tc0 = clamp(tc0, lo, hi);
    tc3 = clamp(tc3, lo, hi);

    float3 result =
        history.SampleLevel(linearSampler, float2(tc12.x, tc0.y), 0).rgb * (w12.x * w0.y) +
        history.SampleLevel(linearSampler, float2(tc0.x, tc12.y), 0).rgb * (w0.x * w12.y) +
        history.SampleLevel(linearSampler, tc12, 0).rgb * (w12.x * w12.y) +
        history.SampleLevel(linearSampler, float2(tc3.x, tc12.y), 0).rgb * (w3.x * w12.y) +
        history.SampleLevel(linearSampler, float2(tc12.x, tc3.y), 0).rgb * (w12.x * w3.y);
    float weight = (w12.x * w0.y) + (w0.x * w12.y) + (w12.x * w12.y) + (w3.x * w12.y) + (w12.x * w3.y);
    return Sanitize(result / max(weight, 1e-5f));
}

// The point of the segment from the box's centre to `p` that is on the box's surface (p itself when it is inside).
float3 ClipToBox(float3 boxCenter, float3 boxExtents, float3 p)
{
    float3 v = p - boxCenter;
    float3 unit = abs(v / max(boxExtents, 1e-5f));
    float scale = max(unit.x, max(unit.y, unit.z));
    return scale > 1.0f ? boxCenter + v / scale : p;
}

#define NTHREADS 8
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint w, h;
    output.GetDimensions(w, h);
    if (DTid.x >= w || DTid.y >= h) {
        return;
    }
    int2 size = int2(w, h);
    int2 pixel = int2(DTid.xy);

    float3 current = LoadColor(pixel, size);
    if (history_valid == 0) {
        output[DTid.xy] = float4(current, 1.0f);
        history_out[DTid.xy] = float4(current, 1.0f);
        return;
    }

    // The colour box of the 3x3 around the pixel, as a mean and a spread in YCoCg.
    float3 m1 = float3(0.0f, 0.0f, 0.0f);
    float3 m2 = float3(0.0f, 0.0f, 0.0f);
    float3 boxMin = float3(1e30f, 1e30f, 1e30f);
    float3 boxMax = float3(-1e30f, -1e30f, -1e30f);
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            float3 c = ToYCoCg(Warp(LoadColor(pixel + int2(x, y), size)));
            m1 += c;
            m2 += c * c;
            boxMin = min(boxMin, c);
            boxMax = max(boxMax, c);
        }
    }
    float3 mean = m1 / 9.0f;
    float3 sigma = sqrt(max(m2 / 9.0f - mean * mean, 0.0f));
    // Never wider than the neighbourhood itself.
    float3 lo = max(mean - CLIP_GAMMA * sigma, boxMin);
    float3 hi = min(mean + CLIP_GAMMA * sigma, boxMax);
    float3 boxCenter = 0.5f * (lo + hi);
    float3 boxExtents = 0.5f * (hi - lo);

    // Where the surface was a frame ago. The vectors are NDC deltas with y already flipped
    // (see MotionCS), so half of one is the same move in texture coordinates. A pixel with no
    // surface in it has no vector: it keeps its place, and the clip does the rest.
    float2 motion = motionTexture[pixel];
    bool moved = abs(motion.x) < MOTION_INVALID && abs(motion.y) < MOTION_INVALID;
    if (!moved) {
        motion = float2(0.0f, 0.0f);
    }
    float2 uv = (float2(pixel) + 0.5f) / float2(size);
    float2 previousUv = uv - 0.5f * motion;
    bool onScreen = all(previousUv >= 0.0f) && all(previousUv <= 1.0f);

    float3 result = current;
    if (onScreen) {
        float3 hist = ToYCoCg(Warp(SampleHistoryCatmullRom(previousUv, float2(size))));
        hist = ClipToBox(boxCenter, boxExtents, hist);

        float speed = length(0.5f * motion * float2(size));
        float blend = lerp(BLEND_STILL, BLEND_MOVING, saturate(speed / SPEED_FULL));
        float3 mixed = lerp(hist, ToYCoCg(Warp(current)), blend);
        result = Sanitize(Unwarp(FromYCoCg(mixed)));
    }
    output[DTid.xy] = float4(result, 1.0f);
    history_out[DTid.xy] = float4(result, 1.0f);
}
