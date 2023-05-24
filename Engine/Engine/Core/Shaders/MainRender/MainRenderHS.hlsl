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

#include "../Common/ShaderStructs.hlsli"

HS_CONSTANT_DATA_OUTPUT CalcHSPatchConstants(
	InputPatch<VertexOutput, NUM_CONTROL_POINTS> patch,
	uint PatchID : SV_PrimitiveID)
{
	HS_CONSTANT_DATA_OUTPUT Output;
	if (patch[0].tessFactor != 1.0f) {
		float4 d1 = (patch[1].worldPos - patch[0].worldPos);
		float4 d2 = (patch[2].worldPos - patch[0].worldPos);
		float area = length(cross(d1.xyz, d2.xyz)) / length(d1);
		float area_unit = 0.5f;
		float area_factor = max(sqrt(area) * area_unit, 0.01f);
		Output.EdgeTessFactor[0] = 0.5f * (patch[1].tessFactor + patch[2].tessFactor) * area_factor;
		Output.EdgeTessFactor[1] = 0.5f * (patch[2].tessFactor + patch[0].tessFactor) * area_factor;
		Output.EdgeTessFactor[2] = 0.5f * (patch[0].tessFactor + patch[1].tessFactor) * area_factor;
		float inside_factor = (Output.EdgeTessFactor[0] + Output.EdgeTessFactor[1] + Output.EdgeTessFactor[2]) * 0.5f;
		Output.InsideTessFactor = inside_factor;
	}
	else {
		Output.EdgeTessFactor[0] = 1.0f;
		Output.EdgeTessFactor[1] = 1.0f;
		Output.EdgeTessFactor[2] = 1.0f;
		Output.InsideTessFactor = 1.0f;
	}
	return Output;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("CalcHSPatchConstants")]
HullOutput main(
	InputPatch<VertexOutput, NUM_CONTROL_POINTS> ip,
	uint i : SV_OutputControlPointID,
	uint PatchID : SV_PrimitiveID )
{
	HullOutput Output;
	Output.worldPos = ip[i].worldPos;
	Output.position = ip[i].position;
	Output.normal = ip[i].normal;
	Output.uv = ip[i].uv;
	Output.mesh_uv = ip[i].mesh_uv;
	return Output;
}
