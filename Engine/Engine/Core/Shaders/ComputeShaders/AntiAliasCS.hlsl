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

// FXAA 3.11 (quality preset), after Timothy Lottes' algorithm.
// Runs on the mixed LDR image, so luma-based edge detection is valid here.

cbuffer externalData : register(b0)
{
    int enabled;
}

RWTexture2D<float4> output : register(u0);
Texture2D input : register(t0);
SamplerState basicSampler : register(s0);

// Trims processing of flat areas (absolute and relative-to-local-contrast).
#define EDGE_THRESHOLD_MIN 0.0312f
#define EDGE_THRESHOLD_MAX 0.125f
// Amount of sub-pixel aliasing removal.
#define SUBPIXEL_QUALITY 0.75f
// Edge end search: 12 steps with growing stride covers ~30 pixels.
#define ITERATIONS 12

static const float QUALITY[ITERATIONS] = {
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.5f, 2.0f, 2.0f, 2.0f, 2.0f, 4.0f, 8.0f
};

float RgbToLuma(float3 rgb)
{
    // sqrt approximates a perceptual curve since the input is linear.
    return sqrt(dot(rgb, float3(0.299f, 0.587f, 0.114f)));
}

float3 SampleColor(float2 uv, float2 rcpFrame)
{
    // basicSampler uses wrap addressing; clamp so border pixels never fetch the opposite edge.
    uv = clamp(uv, 0.5f * rcpFrame, 1.0f - 0.5f * rcpFrame);
    return input.SampleLevel(basicSampler, uv, 0).rgb;
}

float SampleLuma(float2 uv, float2 rcpFrame)
{
    return RgbToLuma(SampleColor(uv, rcpFrame));
}

float SampleLumaOff(float2 uv, int2 off, float2 rcpFrame)
{
    return SampleLuma(uv + off * rcpFrame, rcpFrame);
}

float3 Fxaa(float2 uv, float2 rcpFrame, float3 colorCenter)
{
    float lumaCenter = RgbToLuma(colorCenter);
    float lumaDown = SampleLumaOff(uv, int2(0, -1), rcpFrame);
    float lumaUp = SampleLumaOff(uv, int2(0, 1), rcpFrame);
    float lumaLeft = SampleLumaOff(uv, int2(-1, 0), rcpFrame);
    float lumaRight = SampleLumaOff(uv, int2(1, 0), rcpFrame);

    float lumaMin = min(lumaCenter, min(min(lumaDown, lumaUp), min(lumaLeft, lumaRight)));
    float lumaMax = max(lumaCenter, max(max(lumaDown, lumaUp), max(lumaLeft, lumaRight)));
    float lumaRange = lumaMax - lumaMin;

    // Flat area: keep the pixel untouched.
    float3 result = colorCenter;

    [branch]
    if (lumaRange >= max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD_MAX)) {
        float lumaDownLeft = SampleLumaOff(uv, int2(-1, -1), rcpFrame);
        float lumaUpRight = SampleLumaOff(uv, int2(1, 1), rcpFrame);
        float lumaUpLeft = SampleLumaOff(uv, int2(-1, 1), rcpFrame);
        float lumaDownRight = SampleLumaOff(uv, int2(1, -1), rcpFrame);

        float lumaDownUp = lumaDown + lumaUp;
        float lumaLeftRight = lumaLeft + lumaRight;

        float lumaLeftCorners = lumaDownLeft + lumaUpLeft;
        float lumaDownCorners = lumaDownLeft + lumaDownRight;
        float lumaRightCorners = lumaDownRight + lumaUpRight;
        float lumaUpCorners = lumaUpRight + lumaUpLeft;

        float edgeHorizontal = abs(-2.0f * lumaLeft + lumaLeftCorners)
                             + abs(-2.0f * lumaCenter + lumaDownUp) * 2.0f
                             + abs(-2.0f * lumaRight + lumaRightCorners);
        float edgeVertical = abs(-2.0f * lumaUp + lumaUpCorners)
                           + abs(-2.0f * lumaCenter + lumaLeftRight) * 2.0f
                           + abs(-2.0f * lumaDown + lumaDownCorners);

        bool isHorizontal = (edgeHorizontal >= edgeVertical);

        // Pick the edge side with the steepest gradient.
        float luma1 = isHorizontal ? lumaDown : lumaLeft;
        float luma2 = isHorizontal ? lumaUp : lumaRight;
        float gradient1 = luma1 - lumaCenter;
        float gradient2 = luma2 - lumaCenter;

        bool is1Steepest = abs(gradient1) >= abs(gradient2);
        float gradientScaled = 0.25f * max(abs(gradient1), abs(gradient2));

        float stepLength = isHorizontal ? rcpFrame.y : rcpFrame.x;
        float lumaLocalAverage;
        if (is1Steepest) {
            stepLength = -stepLength;
            lumaLocalAverage = 0.5f * (luma1 + lumaCenter);
        }
        else {
            lumaLocalAverage = 0.5f * (luma2 + lumaCenter);
        }

        // Start on the edge, half a pixel towards the steepest side.
        float2 currentUv = uv;
        if (isHorizontal) {
            currentUv.y += stepLength * 0.5f;
        }
        else {
            currentUv.x += stepLength * 0.5f;
        }

        // Walk along the edge in both directions until its luma end is found.
        float2 offset = isHorizontal ? float2(rcpFrame.x, 0.0f) : float2(0.0f, rcpFrame.y);
        float2 uv1 = currentUv - offset;
        float2 uv2 = currentUv + offset;

        float lumaEnd1 = SampleLuma(uv1, rcpFrame) - lumaLocalAverage;
        float lumaEnd2 = SampleLuma(uv2, rcpFrame) - lumaLocalAverage;

        bool reached1 = abs(lumaEnd1) >= gradientScaled;
        bool reached2 = abs(lumaEnd2) >= gradientScaled;
        bool reachedBoth = reached1 && reached2;

        if (!reached1) {
            uv1 -= offset;
        }
        if (!reached2) {
            uv2 += offset;
        }

        if (!reachedBoth) {
            for (int i = 2; i < ITERATIONS; i++) {
                if (!reached1) {
                    lumaEnd1 = SampleLuma(uv1, rcpFrame) - lumaLocalAverage;
                }
                if (!reached2) {
                    lumaEnd2 = SampleLuma(uv2, rcpFrame) - lumaLocalAverage;
                }
                reached1 = abs(lumaEnd1) >= gradientScaled;
                reached2 = abs(lumaEnd2) >= gradientScaled;
                reachedBoth = reached1 && reached2;
                if (!reached1) {
                    uv1 -= offset * QUALITY[i];
                }
                if (!reached2) {
                    uv2 += offset * QUALITY[i];
                }
                if (reachedBoth) {
                    break;
                }
            }
        }

        float distance1 = isHorizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
        float distance2 = isHorizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);

        bool isDirection1 = distance1 < distance2;
        float distanceFinal = min(distance1, distance2);
        float edgeThickness = distance1 + distance2;

        float pixelOffset = -distanceFinal / edgeThickness + 0.5f;

        // Only shift if the closer end confirms the edge crossing, to avoid overshooting.
        bool isLumaCenterSmaller = lumaCenter < lumaLocalAverage;
        bool correctVariation = ((isDirection1 ? lumaEnd1 : lumaEnd2) < 0.0f) != isLumaCenterSmaller;
        float finalOffset = correctVariation ? pixelOffset : 0.0f;

        // Sub-pixel aliasing removal for isolated details thinner than one pixel.
        float lumaAverage = (1.0f / 12.0f) * (2.0f * (lumaDownUp + lumaLeftRight) + lumaLeftCorners + lumaRightCorners);
        float subPixelOffset1 = saturate(abs(lumaAverage - lumaCenter) / lumaRange);
        float subPixelOffset2 = (-2.0f * subPixelOffset1 + 3.0f) * subPixelOffset1 * subPixelOffset1;
        float subPixelOffsetFinal = subPixelOffset2 * subPixelOffset2 * SUBPIXEL_QUALITY;

        finalOffset = max(finalOffset, subPixelOffsetFinal);

        float2 finalUv = uv;
        if (isHorizontal) {
            finalUv.y += finalOffset * stepLength;
        }
        else {
            finalUv.x += finalOffset * stepLength;
        }
        result = SampleColor(finalUv, rcpFrame);
    }
    return result;
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

    float2 rcpFrame = 1.0f / float2(w, h);
    float2 uv = (float2(DTid.xy) + 0.5f) * rcpFrame;

    float4 color = input[DTid.xy];
    if (enabled != 0) {
        color.rgb = Fxaa(uv, rcpFrame, color.rgb);
    }
    output[DTid.xy] = color;
}
