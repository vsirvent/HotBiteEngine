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

#include <Common/PixelCommon.hlsli>
#include <Common/ShaderStructs.hlsli>
#include "DemoCommons.hlsli"

[domain("tri")]
TerrainDomainOutput main(
	HS_CONSTANT_DATA_OUTPUT input,
	float3 domain : SV_DomainLocation,
	const OutputPatch<HullOutput, NUM_CONTROL_POINTS> patch)
{
	TerrainDomainOutput output;
	// Determine the position of the new vertex.
	float4 vertexPosition = domain.x * patch[0].position + domain.y * patch[1].position + domain.z * patch[2].position;
	float4 vertexWPosition = domain.x * patch[0].worldPos + domain.y * patch[1].worldPos + domain.z * patch[2].worldPos;
	float3 vertexNormal = domain.x * patch[0].normal + domain.y * patch[1].normal + domain.z * patch[2].normal;
	float2 vertexUV = domain.x * patch[0].uv + domain.y * patch[1].uv + domain.z * patch[2].uv;
	float2 vertexMeshUV = domain.x * patch[0].mesh_uv + domain.y * patch[1].mesh_uv + domain.z * patch[2].mesh_uv;
	output.position = vertexPosition;
	output.worldPos = vertexWPosition;
	output.mesh_uv = vertexMeshUV;
	output.uv = vertexUV;
	output.normal = vertexNormal;
	output.normal = vertexNormal;
	output.tess_factor = input.InsideTessFactor;
	return output;
}
