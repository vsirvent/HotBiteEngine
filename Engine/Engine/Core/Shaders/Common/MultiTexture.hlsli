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

#ifndef __MULTITEXTURE_HLSLI__
#define __MULTITEXTURE_HLSLI__

#include "Defines.hlsli"
#include "PixelCommon.hlsli"
#include "QuickNoise.hlsli"
#include "NoiseSimplex.hlsli"
#include "RGBANoise.hlsli"

//Mirrors the TEXT_* constants in Core/Material.h, which is where a layer's `op` is
//built. The two must be edited together: nothing validates the value on the way to
//the GPU, and a bit that means one thing on each side silently blends the wrong
//layer rather than failing.

//bits 0:2 defines the texture operation
#define MULTITEXT_OP_MASK 3
#define MULTITEXT_MIX 1
#define MULTITEXT_ADD 2
#define MULTITEXT_MULT 3

//bits 3:12 are reserved for the map texture enabled
#define MULTITEXT_DIFF (1 << 3)
#define MULTITEXT_NORM (1 << 4)
#define MULTITEXT_SPEC (1 << 5)
#define MULTITEXT_ARM  (1 << 6)
#define MULTITEXT_DISP (1 << 7)
#define MULTITEXT_AO   (1 << 8)
#define MULTITEXT_MASK (1 << 9)

//bits 12:18 reserved for texture flags
#define MULTITEXT_UV_NOISE (1 << 12)
#define MULTITEXT_MASK_NOISE (1 << 13)
//Narrow the layer to a range of dot(world normal, up) - the snow-on-flat-ground rule.
#define MULTITEXT_SLOPE (1 << 14)
//Narrow the layer to a range of world-space Y - the snow line, the waterline.
#define MULTITEXT_HEIGHT (1 << 15)
//Read the mask channel as 1 - value, so one image can drive two complementary layers.
#define MULTITEXT_MASK_INV (1 << 16)
//Which channel of the mask image the layer reads. Four layers off one RGBA splat map
//is what makes a single big mask over a terrain practical.
#define MULTITEXT_MASK_CHANNEL_SHIFT 17
#define MULTITEXT_MASK_CHANNEL_MASK (3 << MULTITEXT_MASK_CHANNEL_SHIFT)

Texture2D multi_diffuseTexture[MAX_MULTI_TEXTURE];
Texture2D multi_normalTexture[MAX_MULTI_TEXTURE];
Texture2D multi_specularTexture[MAX_MULTI_TEXTURE];
Texture2D multi_aoTexture[MAX_MULTI_TEXTURE];
Texture2D multi_armTexture[MAX_MULTI_TEXTURE];
Texture2D multi_highTexture[MAX_MULTI_TEXTURE];
//Four channels, not one: a mask image is a splat map, and MULTITEXT_MASK_CHANNEL_*
//says which of its channels a given layer reads.
Texture2D multi_maskTexture[MAX_MULTI_TEXTURE];

//Packed array
static const uint multi_texture_operations[MAX_MULTI_TEXTURE] = (const uint[MAX_MULTI_TEXTURE])packed_multi_texture_operations;
static const float multi_texture_values[MAX_MULTI_TEXTURE] = (const float[MAX_MULTI_TEXTURE])packed_multi_texture_values;
static const float multi_texture_uv_scales[MAX_MULTI_TEXTURE] = (const float[MAX_MULTI_TEXTURE])packed_multi_texture_uv_scales;

//How much of a range rule a value falls inside. `range` is (min, max, fade, enabled)
//as MultiMaterialData::Rebuild packs it: [min, max] is fully included (weight 1),
//falling off to 0 over `fade` units *outside* each edge. The fade sits entirely
//outside the range rather than straddling the edge (smoothstep(min-fade, min+fade,
//x), the more obvious-looking formula) on purpose - straddling puts the boundary
//itself at the fade's midpoint, which is 0.5 weight, not 1.0. That is the difference
//between "a flat surface with slope 1.0 and slope_max 1.0 shows the layer" and
//"...shows it at half strength" - the second is what a straddled fade gives every
//caller that (reasonably) sets max to the exact value they want fully included.
//The fade is clamped away from zero because smoothstep(a, a, x) has no gradient to
//give - a hard edge is what a zero fade should mean, and a one-texel-wide
//smoothstep is exactly that.
float multiRangeMask(float x, float4 range) {
	const float fade = max(range.z, 1e-5f);
	return smoothstep(range.x - fade, range.x, x) *
		(1.0f - smoothstep(range.y, range.y + fade, x));
}

//The per-layer weights this surface point gets: the authored value narrowed by the
//mask image, the noise, and the orientation/altitude rules. They all multiply, so a
//layer lands only where every rule it declares agrees.
//
//`world_normal` is the geometric normal *in world space* - the rules are about how
//the surface sits in the world, so a normal still in object space (which is what the
//domain shader has before it transforms it) would rotate the snow line with the model.
void getValues(out float output_values[MAX_MULTI_TEXTURE], SamplerState basicSampler, float2 uv, uint count, const uint op[MAX_MULTI_TEXTURE], const Texture2D mask[MAX_MULTI_TEXTURE], const float val[MAX_MULTI_TEXTURE], const float3 world_position, const float3 world_normal) {
	const float3 up = float3(0.0f, 1.0f, 0.0f);
	const float slope = dot(normalize(world_normal), up);
	for (uint i = 0; i < count; ++i) {
		//If texture is enabled
		if ((op[i] & MULTITEXT_OP_MASK) != 0) {
			float m = 1.0f;
			if ((op[i] & MULTITEXT_MASK) != 0) {
				//The mask has a UV transform of its own, independent of the uv_scale that
				//tiles the detail maps. A splat map covers the surface once while the rock
				//on it repeats tens of times, and the mesh's UVs are laid out for the
				//second - so sampling a mask at raw uv tiles it across the terrain. The
				//identity transform (1, 0, 0) is what every mask had before this existed.
				const float2 mask_uv = uv * multi_texture_mask_uv[i].xy + multi_texture_mask_uv[i].zw;
				const float4 texel = mask[i].SampleLevel(basicSampler, mask_uv, 0);
				const uint channel = (op[i] & MULTITEXT_MASK_CHANNEL_MASK) >> MULTITEXT_MASK_CHANNEL_SHIFT;
				float c = texel.r;
				if (channel == 1) { c = texel.g; }
				else if (channel == 2) { c = texel.b; }
				else if (channel == 3) { c = texel.a; }
				if ((op[i] & MULTITEXT_MASK_INV) != 0) { c = 1.0f - c; }
				m = c;
			}
			if ((op[i] & MULTITEXT_MASK_NOISE) != 0) {
				const float n = rgba_tnoise(world_position*0.1f + nrand(i));
				m *= n;
			}
			if ((op[i] & MULTITEXT_SLOPE) != 0) {
				m *= multiRangeMask(slope, multi_texture_slope[i]);
			}
			if ((op[i] & MULTITEXT_HEIGHT) != 0) {
				m *= multiRangeMask(world_position.y, multi_texture_height[i]);
			}
			output_values[i] = val[i] * m;
		}
		else {
			//A layer with no blend mode contributes nothing. Leaving its slot unwritten
			//let getMutliTextureValue read whatever was on the stack, which is a weight
			//out of uninitialized memory the moment a stack has a hole in it.
			output_values[i] = 0.0f;
		}
	}
}

//The "no layer supplies this map" default, shared by both getMutliTextureValue
//variants below. Anything not listed here defaults to {0,0,0}, which is correct for
//DIFF (black, nothing drawn) and DISP (zero height, no displacement) - but NORM is
//not sampled as a colour, it is a tangent-space direction decoded by `*2-1` in
//MainRenderPS.hlsli, and {0,0,0} decodes to (-1,-1,-1): a normal pointing away from
//every light, so a stack with no normal-map layer rendered every surface unlit
//instead of falling back to the geometric normal the way a plain material's missing
//NORMAL_MAP_ENABLED_FLAG does. {0.5,0.5,1.0} decodes to (0,0,1), "no perturbation".
float3 MultiTextureNotEnabledDefault(uint type) {
	float3 color = { 0.0f, 0.0f, 0.0f };
	switch (type) {
	case MULTITEXT_AO: {
		color.rgb = 1.0f;
	}break;
	case MULTITEXT_ARM: {
		color.r = 1.0f;
	}break;
	case MULTITEXT_NORM: {
		color = float3(0.5f, 0.5f, 1.0f);
	}break;
	}
	return color;
}

float3 getMutliTextureValue(SamplerState basicSampler, uint type, uint count, const uint op[MAX_MULTI_TEXTURE], const float val[MAX_MULTI_TEXTURE],
	const float uv_scales[MAX_MULTI_TEXTURE], float2 uv, const Texture2D text[MAX_MULTI_TEXTURE]) {
	float3 color = { 0.0f, 0.0f, 0.0f };
	uint i;
	bool enabled = false;

	for (i = 0; i < count; ++i) {
		//If texture is enabled
		if ((op[i] & type) != 0) {
			enabled = true;
			if (val[i] > 0.0f) {
				float v = val[i];
				float uv_scale = uv_scales[i];
				float3 tcolor = text[i].Sample(basicSampler, uv * uv_scale).rgb;
				switch (op[i] & MULTITEXT_OP_MASK) {
				case MULTITEXT_MIX: {
					color = (1.0f - v) * color + v * tcolor;
				}break;
				case MULTITEXT_ADD: {
					color += v * tcolor;
				}break;
				case MULTITEXT_MULT: {
					color *= v * tcolor;
				}break;
				}
			}
		}
	}

	if (!enabled) {
		color = MultiTextureNotEnabledDefault(type);
	}
	return color;
}

float3 getMutliTextureValueLevel(SamplerState basicSampler, uint level, uint type,
	uint count, const uint op[MAX_MULTI_TEXTURE], const float val[MAX_MULTI_TEXTURE],
	const float uv_scales[MAX_MULTI_TEXTURE], float2 uv, const Texture2D text[MAX_MULTI_TEXTURE]) {

	float3 color = { 0.0f, 0.0f, 0.0f };
	uint i;
	bool enabled = false;

	for (i = 0; i < count; ++i) {
		//If texture is enabled
		if ((op[i] & type) != 0) {
			enabled = true;
			if (val[i] > 0.0f) {
				float v = val[i];
				float uv_scale = uv_scales[i];
				float3 tcolor = text[i].SampleLevel(basicSampler, uv * uv_scale, level).rgb;
				switch (op[i] & MULTITEXT_OP_MASK) {
				case MULTITEXT_MIX: {
					color = (1.0f - v) * color + v * tcolor;
				}break;
				case MULTITEXT_ADD: {
					color += v * tcolor;
				}break;
				case MULTITEXT_MULT: {
					color *= v * tcolor;
				}break;
				}
			}
		}
	}

	if (!enabled) {
		color = MultiTextureNotEnabledDefault(type);
	}
	return color;
}

#endif