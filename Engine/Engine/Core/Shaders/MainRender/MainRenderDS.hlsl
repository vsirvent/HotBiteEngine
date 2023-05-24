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
#include "../Common/ShaderStructs.hlsli"

cbuffer externalData : register(b0)
{
	int highTextureEnable;
	float displacementScale;

	uint multi_texture_count;

	uint4 packed_multi_texture_operations[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_values[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_uv_scales[MAX_MULTI_TEXTURE / 4];
}

#include "../Common/MultiTexture.hlsli"

Texture2D highTexture;

[domain("tri")]
DomainOutput main(
	HS_CONSTANT_DATA_OUTPUT input,
	float3 domain : SV_DomainLocation,
	const OutputPatch<HullOutput, NUM_CONTROL_POINTS> patch)
{
	DomainOutput output;
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

	if (!(domain.x == 0.0f || domain.y == 0.0f || domain.z == 0.0f)) {
#if 0
		// Calculate deltas to project onto three tangent planes
		float4 vecProj0 = dot(patch[0].worldPos - output.worldPos, float4(patch[0].normal, 0.0f)) * float4(patch[0].normal, 0.0f);
		float4 vecProj1 = dot(patch[1].worldPos - output.worldPos, float4(patch[1].normal, 0.0f)) * float4(patch[1].normal, 0.0f);
		float4 vecProj2 = dot(patch[2].worldPos - output.worldPos, float4(patch[2].normal, 0.0f)) * float4(patch[2].normal, 0.0f);

		// Lerp between projection vectors
		float4 vecOffset = domain.x * vecProj0 + domain.y * vecProj1 + domain.z * vecProj2;
		vecOffset = 0.5*vecOffset;

		// Add a fraction of the offset vector to the lerped position
		output.worldPos += vecOffset;
#endif
	}
	//
	// REMINDER: We need to update the depth tests to use updated vertex 
	//
	if (input.InsideTessFactor > 1.0f && displacementScale > 0.0f) {
		float h = 0.0f;
		if (multi_texture_count > 0) {
			float calculated_values[MAX_MULTI_TEXTURE];
			getValues(calculated_values, basicSampler, output.uv, multi_texture_count, multi_texture_operations, multi_maskTexture, multi_texture_values, output.worldPos.xyz);
			h = getMutliTextureValueLevel(basicSampler, 1, MULTITEXT_DISP, multi_texture_count, multi_texture_operations,
				calculated_values, multi_texture_uv_scales, output.uv, multi_highTexture).r;
		}
		else if (highTextureEnable) {
			h = highTexture.SampleLevel(basicSampler, output.uv, 1).r;
		}
		h = 2.0f * h - 1.0f;		
		output.worldPos += float4((displacementScale * h * output.normal), 0.0f);
	}
	return output;
}
