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

#ifndef __SHADER_STRUCTS_HLSLI__
#define __SHADER_STRUCTS_HLSLI__

#include "Defines.hlsli"

struct RaySource
{
	float3 orig;
	float dispersion;
	float3 normal;
	float density;
	float opacity;
	float reflex;
};

RaySource fromColor(float4 color0, float4 color1)
{
	RaySource ray;
	ray.orig = color0.xyz;
	ray.normal = color1.xyz;

	uint w = asuint(color0.w);
	ray.dispersion = (float)(w >> 16) / 1000.0f;
	ray.reflex = (float)(w & 0xFFFF) / 1000.0f;
    w = asuint(color1.w);
	ray.density = (float)(w >> 16) / 1000.0f;
	ray.opacity = (float)(w & 0xFFFF) / 1000.0f;
	return ray;
}

float4 getColor0(RaySource ray)
{
	int d = (ray.dispersion * 1000.0f);
	int r = (ray.reflex * 1000.0f);
	float w = asfloat(d << 16 | (r & 0xFFFF));
	return float4(ray.orig, w);
}

float4 getColor1(RaySource ray)
{
	int d = (ray.density * 1000.0f);
	int o = (ray.opacity * 1000.0f);
	float w = asfloat(d << 16 | (o & 0xFFFF));
	return float4(ray.normal, w);
}

float4 getPropsColor(float dispersion) {
	return float4(dispersion, 0.0f, 0.0f, 0.0f);
}

float getDispersion(float4 color) {
	return color.r;
}

struct VolumetricLightData
{
	uint globalIllumination;
	float3 padding;
};

struct DepthVertexToPixel
{
	float4 position		: SV_POSITION;	// XYZW position (System Value Position)
	float4 worldPos		: POSITION;
};

struct VertexToPixel
{
	float4 position		: SV_POSITION;	// XYZW position (System Value Position)
	float4 worldPos		: POSITION;
	float2 uv			: TEXCOORD;
};

struct VertexShaderInput
{
	float3 position		: POSITION;
	float3 normal		: NORMAL;
	float2 uv			: TEXCOORD;
	float2 mesh_uv		: TEXCOORD1;
	float3 tangent      : POSITION1;
	float3 bitangent    : POSITION2;
	int    bone_ids[4]  : BLENDINDICES;
	float  weights[4]   : BLENDWEIGHT;
};

struct VertexOutput
{
	float4 position		: SV_POSITION;
	float4 worldPos		: POSITION;
	float3 normal		: NORMAL;
	float2 uv			: TEXCOORD;
	float2 mesh_uv		: TEXCOORD1;
	float  tessFactor   : TESSFACTOR;
};

struct HullOutput
{
	float4 position		: SV_POSITION;
	float4 worldPos		: POSITION;
	float3 normal		: NORMAL;
	float2 uv			: TEXCOORD;
	float2 mesh_uv		: TEXCOORD1;
};

struct DomainOutput
{
	float4 position		: SV_POSITION;	
	float4 worldPos		: POSITION;
	float3 normal		: NORMAL;
	float2 uv			: TEXCOORD;
	float2 mesh_uv		: TEXCOORD1;
};

struct GSOutput
{
	float4 position     : SV_POSITION;
	float4 worldPos     : POSITION0;
	float4 objectPos    : POSITION1;
	float3 normal		: NORMAL;
	float2 uv			: TEXCOORD;
	float2 mesh_uv		: TEXCOORD1;
	float3 tangent      : POSITION2;
	float3 bitangent    : POSITION3;
};

struct GSSimpleOutput
{
	float4 position     : SV_POSITION;
	float4 worldPos     : POSITION;
};

struct VertexParticleInput
{
	float3 position		: POSITION;
	float3 normal		: NORMAL;
	float  life : PSIZE0;
	float  size : PSIZE1;
	uint  id : PSIZE2;
	int    bone_ids[4]  : BLENDINDICES;
	float  weights[4]   : BLENDWEIGHT;
};

struct VertexParticleOutput
{
	float4 position		: SV_POSITION;
	float4 worldPos		: POSITION;
	float3 normal		: NORMAL;
	float  life : PSIZE0;
	float  size : PSIZE1;
	uint  id : PSIZE2;
};

struct GSParticleOutput
{
	float4 position     : SV_POSITION;
	float4 worldPos     : POSITION0;
	float2 uv			: TEXCOORD;
	float  life         : PSIZE0;
	float  size : PSIZE1;
	uint  id : PSIZE2;
	float3 center     : POSITION1;
};

struct HS_CONSTANT_DATA_OUTPUT
{
	float EdgeTessFactor[3]	: SV_TessFactor;
	float InsideTessFactor : SV_InsideTessFactor;
};

#endif