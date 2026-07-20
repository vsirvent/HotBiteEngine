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

//Depth of field autofocus.
//
//The focal distance is the scene depth at the center of the view: DepthPS writes
//length(worldPos - cameraPosition) into the depth map, so a texel of it already is
//the world distance from the camera to whatever the camera is aimed at - exactly
//what the DOF shaders want in focusZ, with no projection maths and no readback.
//
//Dispatched as a single thread writing a single texel, which the DOF pixel shader
//(and the dust / lens flare passes) sample instead of a CPU-supplied constant.

Texture2D<float> depthTexture;
RWTexture2D<float> focusOutput;

//Depth map is cleared to FLT_MAX, so anything this large is sky, not geometry.
#define NO_GEOMETRY 1e30f

cbuffer externalData : register(b0)
{
    //Per-frame blend toward the newly measured distance, 0..1. Focus is measured
    //from one texel of a scene that the camera whips across, so an unsmoothed
    //value snaps hard the instant a near object crosses the view center.
    float smoothFactor;
    //Focal distance used when nothing has ever been in view (opening frame aimed
    //at the sky), in world units.
    float defaultFocus;
    //Half-size of the square of depth texels averaged around the center. Averaging
    //rather than reading the single center texel keeps the focus from flickering
    //between subject and background along a silhouette edge that sits on the
    //crosshair.
    int radius;
    //Forces the measurement to be adopted as-is, skipping the smoothing (first
    //frame, or after a cut that makes the previous distance meaningless).
    int reset;
}

[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint w, h;
    depthTexture.GetDimensions(w, h);
    int2 center = int2(w / 2, h / 2);
    int2 last = int2((int)w - 1, (int)h - 1);

    float sum = 0.0f;
    float count = 0.0f;
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            float d = depthTexture[clamp(center + int2(x, y), int2(0, 0), last)];
            if (d > 0.0f && d < NO_GEOMETRY) {
                sum += d;
                count += 1.0f;
            }
        }
    }

    float prev = focusOutput[uint2(0, 0)];
    //Aimed at the sky: hold the last valid distance rather than racking the lens
    //out to infinity and back as the horizon passes through the view center.
    float measured = (count > 0.0f) ? (sum / count) : ((prev > 0.0f) ? prev : defaultFocus);
    focusOutput[uint2(0, 0)] = (reset != 0 || prev <= 0.0f) ? measured : lerp(prev, measured, smoothFactor);
}
