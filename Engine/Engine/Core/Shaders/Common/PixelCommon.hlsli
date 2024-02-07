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

#include "Defines.hlsli"
#include "QuickNoise.hlsli"
#include "NoiseSimplex.hlsli"
#include "Utils.hlsli"

SamplerState basicSampler;
SamplerComparisonState PCFSampler;

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
};

struct DirLight
{
#define DIR_LIGHT_FLAG_FOG 1
	float3 Color;
	float  intensity;
	float3 DirToLight;
	float density;
	uint cast_shadow;
	float3 position;
	float range;
	int flags;
	float2 padding;
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

struct MaterialColor
{
	float4 ambientColor;
	float4 diffuseColor;

	float specIntensity;
	float parallax_scale;
	float parallax_steps;
	float parallax_angle_steps;

	float parallax_shadow_scale;
	float bloom_scale;
	float opacity;
	float density;

	float3 alphaColor;	
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
	uint flags;
};

struct AmbientLight
{
	float3 AmbientDown;
	float  padding0;
	float3 AmbientUp;
	float  padding1;
};



#endif