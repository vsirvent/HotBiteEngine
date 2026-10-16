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

//Temporal accumulation of the light of the ray traced point lights.
//
//DeferredLightCS jitters every shadow ray's aim (Common/ShadowRays.hlsli), so one frame's
//shadow edge is noise; the light those lights add goes to its own buffer instead of the
//light map, and this pass averages it over the frames: last frame's accumulated light is
//found where this surface point was (prevPositionTexture, the point's position at the
//previous frame's transform, seen through the previous frame's camera), checked to be the
//same surface by its depth, and blended with this frame's. The result is written back as the
//new history and ADDED to the light map, which already holds everything else.
//
//A pixel whose history is missing or from another surface keeps this frame's value alone
//(noisy for a few frames) rather than a wrong one.

cbuffer externalData : register(b0)
{
	matrix view_proj;
	matrix prev_view_proj;
	int screenW;
	int screenH;
	//0 on the first frame, after a frame with no ray traced light, and after a resize.
	int history_valid;
	//Weight of this frame's value: 1 is no accumulation, 0.1 is about a ten frame average.
	float blend;
}

Texture2D<float4> positionTexture : register(t0);
Texture2D<float4> prevPositionTexture : register(t1);
Texture2D<float4> currentLight : register(t2);
//rgb: accumulated light, a: the view depth of the surface it was accumulated on.
Texture2D<float4> history : register(t3);
SamplerState basicSampler : register(s0);

RWTexture2D<float4> history_out : register(u0);
RWTexture2D<float4> light_out : register(u1);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
	if (dtid.x >= (uint)screenW || dtid.y >= (uint)screenH) {
		return;
	}
	const uint2 px = dtid.xy;
	const float4 wp = positionTexture[px];
	//Nothing was drawn here (the G-buffer is cleared to FLT_MAX).
	if (wp.x >= 1.0e30f) {
		history_out[px] = float4(0.0f, 0.0f, 0.0f, 0.0f);
		return;
	}
	const float3 cur = currentLight[px].rgb;
	const float depth = mul(float4(wp.xyz, 1.0f), view_proj).w;

	float3 result = cur;
	if (history_valid != 0) {
		const float4 pp = prevPositionTexture[px];
		if (pp.x < 1.0e30f && any(pp.xyz != 0.0f)) {
			const float4 c = mul(float4(pp.xyz, 1.0f), prev_view_proj);
			if (c.w > 0.0f) {
				const float2 ndc = c.xy / c.w;
				const float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
				if (all(uv >= 0.0f) && all(uv <= 1.0f)) {
					//The surface last frame's pixel showed has to be at the depth the point
					//was: anything else is a disocclusion, and its light is someone else's.
					const float4 h = history.SampleLevel(basicSampler, uv, 0);
					const float hist_depth = history.Load(int3((int2)(uv * float2(screenW, screenH)), 0)).a;
					if (abs(hist_depth - c.w) < 0.03f * c.w + 0.05f) {
						//History is clamped to what this frame's 3x3 neighbourhood holds: the noise
						//of a penumbra stays inside that range and is averaged away, while a
						//shadow that has just appeared or gone (a caster moved) is outside it and
						//snaps to the new value instead of fading in over 1/blend frames.
						float3 lo = cur;
						float3 hi = cur;
						//A pixel that traced only some of its lights this frame (DeferredLightCS, shadow_ray_max_lights; alpha 1) holds
						//that share scaled up, a value that is right only on average over the frames: the range of such a neighbourhood
						//says nothing about where the average is, and clamping to it takes energy away (a sparse, scaled up sample
						//is mostly below the average, so the range's top is often under it). Around those pixels the history is not clamped.
						float sampled = currentLight[px].a;
						[unroll]
						for (int dy = -1; dy <= 1; ++dy) {
							[unroll]
							for (int dx = -1; dx <= 1; ++dx) {
								const int2 q = clamp((int2)px + int2(dx, dy), int2(0, 0), int2(screenW - 1, screenH - 1));
								const float4 n4 = currentLight[q];
								lo = min(lo, n4.rgb);
								hi = max(hi, n4.rgb);
								sampled = max(sampled, n4.a);
							}
						}
						result = lerp((sampled > 0.0f) ? h.rgb : clamp(h.rgb, lo, hi), cur, blend);
					}
				}
			}
		}
	}
	history_out[px] = float4(result, depth);
	const float4 l = light_out[px];
	light_out[px] = float4(l.rgb + result, l.a);
}
