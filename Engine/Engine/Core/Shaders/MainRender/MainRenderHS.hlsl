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

//The tessellation factor of one edge, from its two end points and nothing else.
//
//That is the whole point. The patch on the other side of this edge computes it from the
//same two vertices, and D3D only keeps a tessellated surface watertight when both sides
//agree on the factor - so anything that depends on the *triangle* (its area, which vertex
//comes first) opens a crack along every edge whose two triangles differ. The previous
//version scaled all three edges by "triangle height over edge 0-1": for the two triangles
//of a quad, edge 0-1 is a side in one and the diagonal in the other, so the halves of a
//cube face were tessellated at different densities and the diagonal between them was
//cut at two different rates (the black slashes along it).
//
//The vertex factors are averaged (VS gives each vertex its own: distance, silhouette). An
//edge whose end points both ask for none stays at exactly 1, whatever its length - the
//length scale below would otherwise tessellate an untessellated material's long edges,
//and a factor of 0 (culled) stays 0 so the patch is dropped.
float EdgeFactor(VertexOutput a, VertexOutput b)
{
	const float requested = 0.5f * (a.tessFactor + b.tessFactor);
	if (requested <= 1.0f) {
		return requested;
	}
	const float length_ws = length(a.worldPos.xyz - b.worldPos.xyz);
	const float length_scale = max(sqrt(length_ws) * 0.5f, 0.01f);
	return requested * length_scale;
}

HS_CONSTANT_DATA_OUTPUT CalcHSPatchConstants(
	InputPatch<VertexOutput, NUM_CONTROL_POINTS> patch,
	uint PatchID : SV_PrimitiveID)
{
	HS_CONSTANT_DATA_OUTPUT Output;
	//Edge i is the one opposite vertex i (D3D's tri domain).
	Output.EdgeTessFactor[0] = EdgeFactor(patch[1], patch[2]);
	Output.EdgeTessFactor[1] = EdgeFactor(patch[2], patch[0]);
	Output.EdgeTessFactor[2] = EdgeFactor(patch[0], patch[1]);
	const float most = max(Output.EdgeTessFactor[0], max(Output.EdgeTessFactor[1], Output.EdgeTessFactor[2]));
	//Nothing asked for tessellation (or the whole patch is culled): leave it alone, and do
	//not let the inside factor invent some. MainRenderDS gates displacement on it being
	//above 1, so a material that is not tessellated must come out at exactly 1.
	if (most <= 1.0f) {
		Output.InsideTessFactor = most;
	}
	else {
		//Symmetric in the three edges, so the two triangles of a quad - which have the same
		//three edge factors - get the same inside density.
		Output.InsideTessFactor = (Output.EdgeTessFactor[0] + Output.EdgeTessFactor[1] + Output.EdgeTessFactor[2]) * 0.5f;
	}
	return Output;
}

[domain("tri")]
[partitioning("pow2")]
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
	Output.prevPos = ip[i].prevPos;
	Output.normal = ip[i].normal;
	Output.uv = ip[i].uv;
	Output.mesh_uv = ip[i].mesh_uv;
	return Output;
}
