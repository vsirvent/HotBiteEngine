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

#ifndef __PIXEL_COMMON_HLSLI__
#define __PIXEL_COMMON_HLSLI__


SamplerState basicSampler;
SamplerComparisonState PCFSampler;

//A compute shader has no implicit derivatives, so Sample is illegal in one - and fxc
//rejects it at *compile* time, on a branch that can never execute, not merely when the
//branch is taken. SplatRasterCS includes the pixel-side lighting headers so a Gaussian
//is lit by exactly the same CalcDirectional/CalcPoint the pixel shaders use, rather than
//by a second copy of the lighting that would drift away from it, so every texture read
//on that path goes through this macro.
//
//Defining it here rather than next to the calls, because the callers are spread across
//PixelFunctions.hlsli and MultiTexture.hlsli and this is the header both of them already
//include first. Everything else in the lighting path is derivative-free already - the
//shadow lookups are SampleCmpLevelZero, which is legal in compute.
//
//The compute build takes the explicit mip-0 path. That is not a compromise for the splat
//pass: it reads no mipped material texture at all (a splat's colour is per splat and its
//specular intensity is a per-cloud constant), so the guarded branches are ones it never
//enters.
#ifdef HB_COMPUTE_LIGHTING
#define PF_SAMPLE(tex, samp, uv) tex.SampleLevel(samp, uv, 0)
#else
#define PF_SAMPLE(tex, samp, uv) tex.Sample(samp, uv)
#endif

#include "Defines.hlsli"
#include "QuickNoise.hlsli"
#include "NoiseSimplex.hlsli"
#include "Utils.hlsli"


struct RenderTarget
{
	float4 scene : SV_TARGET0;
	float4 light_map : SV_TARGET1;
};

struct RenderTargetRT
{
	float4 scene : SV_TARGET0;
	float4 light_map : SV_TARGET1;
	float4 bloom_map : SV_TARGET2;
	float4 rt_ray0_map : SV_TARGET3;
	float4 rt_ray1_map : SV_TARGET4;
	float4 pos0_map : SV_TARGET5;
	float4 pos1_map : SV_TARGET6;
};

struct DirLight
{
#define DIR_LIGHT_FLAG_FOG 1
#define DIR_LIGHT_FLAG_INVERSE 2
	//Mirrors DIR_LIGHT_FLAG_STATIC_SHADOW in Components/Lights.h: the static shadow
	//map for this light has been rendered and bound at least once.
#define DIR_LIGHT_FLAG_STATIC_SHADOW 4
	//Debug view: tint this light's contribution by which cascade shaded each pixel.
	//Mirrors DIR_LIGHT_FLAG_DEBUG_CASCADES in Components/Lights.h.
#define DIR_LIGHT_FLAG_DEBUG_CASCADES 8
	//Debug view: tint by the *static* caster map instead - what it covers and what it
	//shadows. Mirrors DIR_LIGHT_FLAG_DEBUG_STATIC in Components/Lights.h. Takes
	//precedence over the cascade tint if both are somehow set, since they recolour the
	//same term and their palettes would multiply into nonsense.
#define DIR_LIGHT_FLAG_DEBUG_STATIC 16
	float3 Color;
	float  intensity;
	float3 DirToLight;
	float density;
	uint cast_shadow;
	float3 position;
	float range;
	int flags;
	//Live cascade slices of this light, 0..MAX_SHADOW_CASCADES. Zero means nothing has
	//been rendered yet and no cascade may be sampled.
	int cascade_count;
	float padding;
};

struct PointLight
{
	float3 Position;
	float  Range;
	float3 Color;
	float  density;
	uint cast_shadow;
	float tilt_ratio;
	float2 padding;
};

//Mirrors Core::MaterialProps (Core/Material.h) field for field - it is memcpy'd into
//this cbuffer layout, so the two must be edited together.
struct MaterialColor
{
	float4 diffuseColor;

	float specIntensity;
	float parallax_scale;
	float parallax_steps;
	float parallax_angle_steps;

	float parallax_shadow_scale;
	float bloom_scale;
	float opacity;
	float density;

	float emission;
	float3 emission_color;

	float rt_reflex;
#define NORMAL_MAP_ENABLED_FLAG 1 << 0
#define PARALLAX_MAP_ENABLED_FLAG 1 << 1
#define DIFFUSSE_MAP_ENABLED_FLAG 1	<< 2
#define SPECULAR_MAP_ENABLED_FLAG 1	<< 3
#define AO_MAP_ENABLED_FLAG 1 << 4
#define ALPHA_ENABLED_FLAG 1 << 5
#define ARM_MAP_ENABLED_FLAG 1 << 6
#define EMISSION_MAP_ENABLED_FLAG 1 << 7
#define OPACITY_MAP_ENABLED_FLAG 1 << 8
#define BLEND_ENABLED_FLAG 1 << 10
#define PARALLAX_SHADOW_ENABLED_FLAG 1 << 11
#define RAYTRACING_ENABLED 1 << 12
#define WORLD_UV_ENABLED_FLAG 1 << 9
	uint flags;
	//World units per texture repeat when WORLD_UV_ENABLED_FLAG is set - see
	//Core::MaterialProps::world_uv_scale. These two replace what used to be a
	//plain float2 padding; world_uv_reserved stays unused.
	float world_uv_scale;
	float world_uv_reserved;
};

struct AmbientLight
{
	float3 AmbientDown;
	float  padding0;
	float3 AmbientUp;
	float  padding1;
};



#endif