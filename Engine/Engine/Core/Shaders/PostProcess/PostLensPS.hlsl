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

//Physical camera artifacts, in the order a real one produces them: the lens
//disperses colour towards the edges of the frame and falls off in brightness away
//from the optical axis, then the film or sensor behind it adds grain.
//
//Three independent 0..1 amounts, each 0 = off and 1 = the strongest setting that
//still reads as a camera rather than a filter. Everything here is a handful of ALU
//plus at most three texture taps, and the taps collapse to one when aberration is
//off, so the whole stage costs about as much as the copy it replaces.

cbuffer externalData : register(b0)
{
    int screenW;
    int screenH;
    float time;
    float aberration;
    float grain;
    float vignette;
}

Texture2D renderTexture : register(t0);
SamplerState basicSampler : register(s0);

//Radial offset between the red and blue channels at the very corner of the frame,
//in UV. Deliberately small: past about half a percent of the frame it stops looking
//like glass and starts looking like a broken 3D TV.
#define MAX_ABERRATION 0.03f
//Peak amplitude of the grain, on a 0..1 colour scale.
#define MAX_GRAIN 0.14f
//How dark the corners get at vignette = 1.
#define MAX_VIGNETTE 0.8f
//Fraction of the way to the corner at which the vignette starts closing in. Real
//lenses hold roughly the middle third of the frame at full brightness.
#define VIGNETTE_START 0.3f

//Cheap per-pixel white noise. Two frac() and a dot - no texture fetch, which is the
//whole point of generating grain rather than sampling a noise map.
float Hash(float2 p)
{
    p = frac(p * float2(443.897f, 441.423f));
    p += dot(p, p + 19.19f);
    return frac(p.x * p.y);
}

float4 main(float4 pos: SV_POSITION) : SV_TARGET
{
    float2 uv = pos.xy / float2(screenW, screenH);
    //Offset from the optical axis. Both artifacts below are radial, so this one
    //vector drives them.
    float2 axis = uv - 0.5f;
    float r2 = dot(axis, axis);

    float4 color;
    if (aberration > 0.0f)
    {
        //Lateral chromatic aberration: the lens focuses short wavelengths slightly
        //closer to the axis than long ones, so red lands outside its true position
        //and blue inside, by an amount that grows with the square of the distance
        //from the axis and vanishes at the center. Green is the reference and stays
        //put, which is also why this never softens the middle of the frame.
        float2 offset = axis * (r2 * aberration * MAX_ABERRATION);
        color = renderTexture.SampleLevel(basicSampler, uv, 0);
        color.r = renderTexture.SampleLevel(basicSampler, uv + offset, 0).r;
        color.b = renderTexture.SampleLevel(basicSampler, uv - offset, 0).b;
    }
    else
    {
        color = renderTexture.SampleLevel(basicSampler, uv, 0);
    }

    if (vignette > 0.0f)
    {
        //Brightness falloff towards the corners. Distance is normalised so that 1 is
        //the corner regardless of aspect ratio, and smoothstep keeps the transition
        //free of the hard ring a linear falloff leaves.
        float d = length(axis) * 1.41421356f;
        color.rgb *= 1.0f - vignette * MAX_VIGNETTE * smoothstep(VIGNETTE_START, 1.0f, d);
    }

    if (grain > 0.0f)
    {
        //Monochrome grain, resampled every frame so it shimmers like running film
        //rather than sitting on the image as a fixed pattern. frac() on the elapsed
        //time keeps the hash input small enough to stay well conditioned after the
        //application has been running for hours.
        float n = Hash(pos.xy + frac(time) * 137.0f) - 0.5f;
        //Film grain lives in the emulsion, so it is most visible where the image is
        //dark and washes out as the exposure approaches white. Weighting by
        //luminance this way is the difference between film grain and video noise.
        float luma = dot(color.rgb, float3(0.299f, 0.587f, 0.114f));
        color.rgb += n * grain * MAX_GRAIN * saturate(1.0f - luma);
    }

    //Grain can push a near-black pixel below zero; the render target is float, so
    //nothing else would catch it before it turns into a black speckle downstream.
    color.rgb = max(color.rgb, 0.0f);
    return color;
}
