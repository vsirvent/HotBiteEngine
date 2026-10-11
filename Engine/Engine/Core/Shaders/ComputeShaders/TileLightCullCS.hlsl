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

//Decides, per 16x16 screen tile, which of the frame's point lights can light anything in
//it, and records the answer as a bitmask of MAX_TILED_LIGHTS bits (TILE_MASK_WORDS words).
//
//One thread group per tile, one thread per pixel of it:
//
//  1. every thread reads the G-buffer position of its pixel; the group reduces them to the
//     nearest and farthest view depth of any *surface* in the tile (a pixel with no
//     geometry - sky - contributes nothing);
//  2. every thread then takes MAX_TILED_LIGHTS / 256 = 4 of the lights and tests each
//     one's range sphere against the tile's frustum (four side planes through the eye)
//     and against that depth slab. A light that survives sets its bit in group shared
//     memory;
//  3. the group writes the mask out.
//
//The depth slab is what makes this more than a screen-space test: a light behind the
//wall the tile is looking at, or hanging in front of the tile's only surface at a
//distance its range does not cover, is dropped, however well it lines up on screen.
//The test is conservative - a light is only ever kept when its sphere touches the
//region, never dropped when it could shade a pixel - so the shading pass can trust the
//mask completely and needs no fallback.
//
//A tile with no geometry has an empty mask (zmin > zmax, nothing intersects the slab).

#include "../Common/Defines.hlsli"
#include "../Common/TiledLights.hlsli"

cbuffer cullData : register(b0)
{
	//World to view, uploaded like every other matrix here: used as mul(float4(p, 1), view).
	matrix view;
	matrix projection_inverse;
	int screenW;
	int screenH;
	int tiles_x;
	int tiles_y;
	int light_count;
	//Non-zero when the counters below should be filled in (RenderSystem clears them).
	int collect_stats;
	//Non-zero turns the culling off: every light is in the mask of every tile that shows a
	//surface. It is the brute-force reference - what shading with every light at every pixel
	//would cost, and what it would look like, which must be the same image - for the tests that
	//measure the culling (RenderSystem::SetTileCulling). Never on in a game.
	int disable_culling;
	float padding;
}

Texture2D<float4> positionTexture : register(t0);
StructuredBuffer<PackedLight> lightsBuffer : register(t1);
RWByteAddressBuffer tileMasks : register(u0);
//[0] tiles with at least one light, [1] the most lights in one tile, [2] the sum of all
//the tiles' lights, [3] tiles holding any geometry. See Core::TiledLightStats.
RWByteAddressBuffer tileStats : register(u1);

groupshared uint g_zmin;
groupshared uint g_zmax;
groupshared uint g_mask[TILE_MASK_WORDS];
groupshared uint g_count;

//The view-space direction through a point of the screen, given in NDC.
float3 ViewRay(float2 ndc)
{
	float4 v = mul(float4(ndc, 1.0f, 1.0f), projection_inverse);
	return v.xyz / v.w;
}

//A side plane of the tile's frustum: it passes through the eye (the view-space origin),
//so it is just a normal, and it faces the inside of the frustum - toward `inside`.
float3 SidePlane(float3 a, float3 b, float3 inside)
{
	float3 n = normalize(cross(a, b));
	return (dot(n, inside) < 0.0f) ? -n : n;
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint3 gid : SV_GroupID, uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
	if (gi == 0) {
		g_zmin = asuint(FLT_MAX);
		g_zmax = 0;
		g_count = 0;
	}
	if (gi < TILE_MASK_WORDS) {
		g_mask[gi] = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	//Depth range of the surfaces in the tile. A view depth is positive in front of the
	//camera, and positive floats order the same as their bit patterns, so the atomics
	//run on the bits.
	if (dtid.x < (uint)screenW && dtid.y < (uint)screenH) {
		float4 wp = positionTexture[dtid.xy];
		//Empty pixels are cleared to FLT_MAX (see RenderSystem::Clear).
		if (wp.x < 1.0e30f) {
			float vz = max(mul(float4(wp.xyz, 1.0f), view).z, 0.0f);
			InterlockedMin(g_zmin, asuint(vz));
			InterlockedMax(g_zmax, asuint(vz));
		}
	}
	GroupMemoryBarrierWithGroupSync();

	const float zmin = asfloat(g_zmin);
	const float zmax = asfloat(g_zmax);
	const bool has_geometry = zmin <= zmax;

	if (has_geometry) {
		//The tile's frustum. Its extent is clipped to the screen so a partial tile at the
		//right or bottom edge does not keep lights that only touch the part off screen.
		const float x0 = (float)(gid.x * TILE_SIZE);
		const float y0 = (float)(gid.y * TILE_SIZE);
		const float x1 = min(x0 + (float)TILE_SIZE, (float)screenW);
		const float y1 = min(y0 + (float)TILE_SIZE, (float)screenH);
		const float2 ndc0 = float2(x0 / screenW * 2.0f - 1.0f, 1.0f - y0 / screenH * 2.0f);
		const float2 ndc1 = float2(x1 / screenW * 2.0f - 1.0f, 1.0f - y1 / screenH * 2.0f);
		const float3 c00 = ViewRay(float2(ndc0.x, ndc0.y));
		const float3 c10 = ViewRay(float2(ndc1.x, ndc0.y));
		const float3 c01 = ViewRay(float2(ndc0.x, ndc1.y));
		const float3 c11 = ViewRay(float2(ndc1.x, ndc1.y));
		const float3 centre = ViewRay(0.5f * (ndc0 + ndc1));
		const float3 pl_top = SidePlane(c00, c10, centre);
		const float3 pl_right = SidePlane(c10, c11, centre);
		const float3 pl_bottom = SidePlane(c11, c01, centre);
		const float3 pl_left = SidePlane(c01, c00, centre);

		for (uint i = gi; i < (uint)light_count; i += TILE_SIZE * TILE_SIZE) {
			const PackedLight l = lightsBuffer[i];
			const float3 c = mul(float4(l.position, 1.0f), view).xyz;
			const float r = l.range;
			bool inside = (c.z + r >= zmin) && (c.z - r <= zmax);
			inside = inside && (dot(pl_top, c) >= -r);
			inside = inside && (dot(pl_right, c) >= -r);
			inside = inside && (dot(pl_bottom, c) >= -r);
			inside = inside && (dot(pl_left, c) >= -r);
			if (inside || disable_culling != 0) {
				InterlockedOr(g_mask[i >> 5], 1u << (i & 31u));
			}
		}
	}
	GroupMemoryBarrierWithGroupSync();

	const uint tile = gid.y * (uint)tiles_x + gid.x;
	if (gi < TILE_MASK_WORDS) {
		const uint word = g_mask[gi];
		tileMasks.Store((tile * TILE_MASK_WORDS + gi) * 4, word);
		if (collect_stats != 0) {
			InterlockedAdd(g_count, countbits(word));
		}
	}
	GroupMemoryBarrierWithGroupSync();
	if (collect_stats != 0 && gi == 0) {
		uint ignored;
		if (g_count > 0) {
			tileStats.InterlockedAdd(0, 1, ignored);
		}
		tileStats.InterlockedMax(4, g_count, ignored);
		tileStats.InterlockedAdd(8, g_count, ignored);
		if (has_geometry) {
			tileStats.InterlockedAdd(12, 1, ignored);
		}
	}
}
