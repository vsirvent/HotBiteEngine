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

//The point lights of the frame, as the GPU sees them.
//
//Every point and spot light of the scene (up to MAX_TILED_LIGHTS) lives in ONE structured
//buffer of PackedLight, rebuilt each frame by RenderSystem::GatherLights. Nothing that
//shades a surface carries its own copy of them in a constant buffer any more - that was
//the 8 light cap, uploaded again for every draw call. A shader that shades a screen pixel
//reads them through the tile masks instead (Common/DeferredLighting.hlsli): one bit per
//light per 16x16 screen tile, filled by TileLightCullCS, so a pixel only visits the few
//lights whose range reaches its tile.
//
//The layout is mirrored by Core::PackedLight in Core/TiledLights.h, which packs it. They
//are 32 bytes and must stay that way - the structured buffer's stride is derived from
//this struct by the compiler and nothing checks the C++ side against it.

#ifndef __TILED_LIGHTS_HLSLI__
#define __TILED_LIGHTS_HLSLI__

#include "Defines.hlsli"

struct PackedLight
{
	float3 position;
	float  range;
	//RGB9E5 (shared exponent): 9 bit mantissas, so a light colour keeps ~0.3% of
	//precision however bright it is, and a 1000x intensity needs no extra field.
	uint   color;
	//The spot cone's axis, octahedral encoded in two signed 16 bit halves. Unused (0)
	//for a point light.
	uint   direction;
	//cos(outer half angle) in the low half, cos(inner) in the high one, both float16.
	//Unused for a point light.
	uint   cones;
	//bit 0 spot, bit 1 has a shadow map, bits 2..5 the slot of that shadow map in the
	//PointShadowMapTexture array (0..MAX_LIGHTS-1), bit 6 ray traced shadow.
	uint   flags;
};

//What a shader that cannot use the tile masks gets: the light, plus the two things only
//glows and fog read. Lives in a constant buffer, MAX_AUX_LIGHTS of them, so it takes no
//texture register (the ray tracers are at the 128 register limit).
struct AuxLight
{
	PackedLight light;
	//Volumetric density, the same field PointLight::Data::density has always carried.
	float density;
	//Radius of the visible glow in pixels (PointLight::Data::tilt_ratio).
	float tilt_ratio;
	float2 padding;
};

#define PACKED_LIGHT_SPOT 1u
#define PACKED_LIGHT_SHADOW 2u
#define PACKED_LIGHT_SLOT_SHIFT 2u
#define PACKED_LIGHT_SLOT_MASK 15u
//The shadow is a ray traced to the light in the lighting pass (Common/ShadowRays.hlsli).
//Exclusive with PACKED_LIGHT_SHADOW.
#define PACKED_LIGHT_RAYTRACED 64u

float3 UnpackLightColor(PackedLight l)
{
	const uint v = l.color;
	const float scale = exp2((float)(v >> 27) - 24.0f);
	return float3((float)(v & 0x1FFu), (float)((v >> 9) & 0x1FFu), (float)((v >> 18) & 0x1FFu)) * scale;
}

bool LightIsSpot(PackedLight l)
{
	return (l.flags & PACKED_LIGHT_SPOT) != 0u;
}

//-1 when the light casts no shadow (or lost its slot to a nearer one this frame).
int LightShadowSlot(PackedLight l)
{
	if ((l.flags & PACKED_LIGHT_SHADOW) == 0u) {
		return -1;
	}
	return (int)((l.flags >> PACKED_LIGHT_SLOT_SHIFT) & PACKED_LIGHT_SLOT_MASK);
}

bool LightIsRayTraced(PackedLight l)
{
	return (l.flags & PACKED_LIGHT_RAYTRACED) != 0u;
}

float3 UnpackLightDirection(PackedLight l)
{
	const float2 e = float2((float)((int)(l.direction << 16) >> 16),
	                        (float)((int)l.direction >> 16)) * (1.0f / 32767.0f);
	float3 n = float3(e.x, e.y, 1.0f - abs(e.x) - abs(e.y));
	const float t = saturate(-n.z);
	n.xy += float2(n.x >= 0.0f ? -t : t, n.y >= 0.0f ? -t : t);
	return normalize(n);
}

//Cone falloff of a spotlight: 1 inside the inner angle, 0 outside the outer one, a
//smoothstep between. `light_to_pos` need not be normalized. A point light returns 1, so
//every attenuation site multiplies this in unconditionally.
float PackedSpotFactor(PackedLight l, float3 light_to_pos)
{
	if (!LightIsSpot(l)) {
		return 1.0f;
	}
	const float cos_outer = f16tof32(l.cones & 0xFFFFu);
	const float cos_inner = f16tof32(l.cones >> 16);
	const float cd = dot(light_to_pos, UnpackLightDirection(l)) * rsqrt(max(dot(light_to_pos, light_to_pos), 1e-8f));
	return smoothstep(cos_outer, cos_inner, cd);
}

#endif
