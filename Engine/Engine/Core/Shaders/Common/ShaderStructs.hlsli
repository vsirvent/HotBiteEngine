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
};

RaySource fromColor(float4 color0, float4 color1)
{
	RaySource ray;
	ray.orig = color0.xyz;
	ray.dispersion = color0.w;
	ray.normal = color1.xyz;
	ray.density = color1.w;
	return ray;
}

float4 getColor0(RaySource ray)
{
	return float4(ray.orig, ray.dispersion);
}

float4 getColor1(RaySource ray)
{
	return float4(ray.normal, ray.density);
}

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
	float4 worldPos     : POSITION;
	float3 normal		: NORMAL;
	float2 uv			: TEXCOORD;
	float2 mesh_uv		: TEXCOORD1;
	float3 tangent      : POSITION1;
	float3 bitangent    : POSITION2;
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