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
#include "../Common/Defines.hlsli"
#include "RTDefines.hlsli"

RT_HS_CONSTANT_DATA_OUTPUT CalcHSPatchConstants(
	InputPatch<RTVertexOutput, NUM_CONTROL_POINTS> patch,
	uint PatchID : SV_PrimitiveID)
{
	RT_HS_CONSTANT_DATA_OUTPUT Output;
	
	Output.EdgeTessFactor[0] = 5.0f;
	Output.EdgeTessFactor[1] = 5.0f;
	Output.EdgeTessFactor[2] = 5.0f;
	Output.InsideTessFactor = 5.0f;
	return Output;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("CalcHSPatchConstants")]
RTHullOutput main(
	InputPatch<RTVertexOutput, NUM_CONTROL_POINTS> ip,
	uint i : SV_OutputControlPointID,
	uint PatchID : SV_PrimitiveID )
{
	RTHullOutput Output;
	Output.worldPos = ip[i].worldPos;
	Output.position = ip[i].position;
	Output.normal = ip[i].normal;
	return Output;
}
