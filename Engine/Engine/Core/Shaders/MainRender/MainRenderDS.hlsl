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
	matrix world;
	int highTextureEnable;
	float displacementScale;

	uint multi_texture_count;

	uint4 packed_multi_texture_operations[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_values[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_uv_scales[MAX_MULTI_TEXTURE / 4];
	//Per layer, the orientation and altitude rules as (min, max, fade, enabled).
	//Declared here rather than in MultiTexture.hlsli because that file is an
	//include: every shader that pulls it in has to supply the constants it reads.
	float4 multi_texture_slope[MAX_MULTI_TEXTURE];
	float4 multi_texture_height[MAX_MULTI_TEXTURE];
	//Per layer, the mask image's own UV transform as (scale, scale, offset u,
	//offset v). Separate from the uv_scale that tiles the detail maps: a splat
	//map covers the surface once, the rock on it repeats tens of times.
	float4 multi_texture_mask_uv[MAX_MULTI_TEXTURE];
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
	float4 vertexPrevPosition = domain.x * patch[0].prevPos + domain.y * patch[1].prevPos + domain.z * patch[2].prevPos;
	float3 vertexNormal = domain.x * patch[0].normal + domain.y * patch[1].normal + domain.z * patch[2].normal;
	float2 vertexUV = domain.x * patch[0].uv + domain.y * patch[1].uv + domain.z * patch[2].uv;
	float2 vertexMeshUV = domain.x * patch[0].mesh_uv + domain.y * patch[1].mesh_uv + domain.z * patch[2].mesh_uv;
	output.position = vertexPosition;
	output.worldPos = vertexWPosition;
	output.prevPos = vertexPrevPosition;
	output.mesh_uv = vertexMeshUV;
	output.uv = vertexUV;
	output.normal = normalize(vertexNormal);

	//
	// REMINDER: We need to update the depth tests to use updated vertex 
	//

	float disp = 0.0f;
	if (input.InsideTessFactor > 1.0f && displacementScale > 0.0f) {
		float h = 0.0f;
		if (multi_texture_count > 0) {
			float calculated_values[MAX_MULTI_TEXTURE];
			//output.normal is still in object space here (it is transformed a few lines
			//below), and the layer rules are world-space ones - passing it as-is would
			//tilt every slope rule with the model's own rotation.
			const float3 world_normal = normalize(mul(output.normal, (float3x3)world));
			getValues(calculated_values, basicSampler, output.uv, multi_texture_count, multi_texture_operations, multi_maskTexture, multi_texture_values, output.worldPos.xyz, world_normal);
			h = getMutliTextureValueLevel(basicSampler, 1, MULTITEXT_DISP, multi_texture_count, multi_texture_operations,
				calculated_values, multi_texture_uv_scales, output.uv, multi_highTexture).r;
		}
		else if (highTextureEnable) {
			h = highTexture.SampleLevel(basicSampler, output.uv, 1).r;
		}
		disp = displacementScale * h;
	}

	output.position.xyz += disp * output.normal;
	//The displacement is fixed in the surface's own texture space, so it has to move the
	//previous position by the same amount along the same (object-space) normal. Offsetting
	//only the current one would read as the whole displaced surface sliding every frame.
	output.prevPos.xyz += disp * output.normal;
	output.normal = normalize(mul(output.normal, (float3x3)world));
	output.worldPos.xyz += disp * output.normal;

	return output;
}
