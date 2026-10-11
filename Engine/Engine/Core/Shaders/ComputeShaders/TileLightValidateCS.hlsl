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

//Checks the tile masks by brute force. For every pixel that shows a surface it tests every
//light's range sphere against the pixel's own world position, and where the light reaches
//the pixel it looks the light up in the pixel's tile mask. A light that reaches a pixel
//but is not in its tile's mask is a MISS: the shading pass would never see it, and the
//pixel would be darker than the light says. TileLightCullCS is conservative by design, so
//the expected number of misses is zero.
//
//Costs pixels x lights distance tests, which is why it runs only when asked
//(RenderSystem::ValidateTileLights), never as part of a frame.

#include "../Common/Defines.hlsli"
#include "../Common/TiledLights.hlsli"

cbuffer validateData : register(b0)
{
	int screenW;
	int screenH;
	int tiles_x;
	int light_count;
}

Texture2D<float4> positionTexture : register(t0);
StructuredBuffer<PackedLight> lightsBuffer : register(t1);
ByteAddressBuffer tileMasks : register(t2);
//[4] (pixel, light) pairs where the light reaches the pixel, [5] of those, how many were
//absent from the mask, [6] pixels with geometry.
RWByteAddressBuffer validateStats : register(u0);

groupshared uint g_reached;
groupshared uint g_missed;
groupshared uint g_pixels;

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint3 gid : SV_GroupID, uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
	if (gi == 0) {
		g_reached = 0;
		g_missed = 0;
		g_pixels = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	if (dtid.x < (uint)screenW && dtid.y < (uint)screenH) {
		const float4 wp = positionTexture[dtid.xy];
		if (wp.x < 1.0e30f) {
			const uint tile = gid.y * (uint)tiles_x + gid.x;
			uint reached = 0;
			uint missed = 0;
			for (int i = 0; i < light_count; ++i) {
				const PackedLight l = lightsBuffer[i];
				const float3 d = wp.xyz - l.position;
				//A hair inside the range: the mask was built in view space and this runs
				//in world space, so a pixel exactly on the sphere is a coin toss that says
				//nothing about the culling.
				const float r = l.range * 0.999f;
				if (dot(d, d) < r * r) {
					++reached;
					const uint word = tileMasks.Load((tile * TILE_MASK_WORDS + ((uint)i >> 5)) * 4);
					if ((word & (1u << ((uint)i & 31u))) == 0u) {
						++missed;
					}
				}
			}
			InterlockedAdd(g_reached, reached);
			InterlockedAdd(g_missed, missed);
			InterlockedAdd(g_pixels, 1);
		}
	}
	GroupMemoryBarrierWithGroupSync();
	if (gi == 0) {
		uint ignored;
		validateStats.InterlockedAdd(16, g_reached, ignored);
		validateStats.InterlockedAdd(20, g_missed, ignored);
		validateStats.InterlockedAdd(24, g_pixels, ignored);
	}
}
