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

#include <Common/Matrix.hlsli>
#include <Common/ShaderStructs.hlsli>
#include <Common/NoiseSimplex.hlsli>

cbuffer externalData : register(b0)
{
	matrix world;
	float3 cameraPosition;
	float3 cameraDirection;
	bool tessEnabled;
	matrix joints[MAX_JOINTS];
	uint njoints;
	int tessType;
	float tessFactor;
};

VertexOutput main(VertexShaderInput input)
{
	VertexOutput output;
	
	float4 pos;
	float3 normal;
	
	pos = float4(input.position, 1.0f);
	normal = input.normal;
	
	output.position = pos;
	output.worldPos = mul(pos, world);
	output.normal = normalize(mul(normal, (float3x3)world));
	output.uv = input.uv;
	output.mesh_uv = input.mesh_uv;

	float factor = 1.0f;
	float depth = length(output.worldPos.xyz - cameraPosition);
	float silhouette = dot(cameraDirection, output.normal);
	float angle = acos(silhouette);
	if (depth < 0.0f || (angle < PI / 2.0f - 1.0f)) {
		//culling hidden normals
		factor = 0.0f;
	}
	
	const float n = pow(snoise(output.worldPos * 0.05f), 2.0f);
	factor = lerp(1.0f, tessFactor, n);
	if (factor < 2.0f) {
		factor = 1.0f;
	}
	output.tessFactor = factor;
	return output;
}
