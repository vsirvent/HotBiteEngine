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

/*
Vertex stage of the Scene Editor's *model* preview - the template viewport in the
Templates panel (Tools/SceneEditor/ModelPreview.h).

It pairs with MaterialPreviewPS, which is already mesh-agnostic: the thumbnail
sphere and a template's real mesh differ only in how their interpolants are
produced, so the two previews shade identically and a material looks the same in
the swatch and on the model. What this shader adds over MaterialPreviewVS is
everything a real mesh needs and a unit sphere does not:

  - skinning, so an animated template previews in the pose the AnimationSystem
    would put it in rather than in its bind pose. Same four-influence blend and
    the same `joints`/`njoints` contract as MainRenderVS, because it is fed by the
    same Components::Mesh::Prepare.

  - a separate inverse world matrix for the frame vectors. MaterialPreviewVS can
    transform normals by `world` directly since its sphere is only ever uniformly
    scaled; a template's base transform is authored and routinely is not (a 0.025
    uniform scale is common, a squashed one is legal), and a non-uniform scale run
    through `world` bends normals away from the surface.

Deliberately absent, as in the rest of the preview pass: tessellation,
displacement and the velocity/previous-frame outputs. See MaterialPreviewPS.hlsl
for what the shading does and does not model.
*/

#include "../Common/ShaderStructs.hlsli"

cbuffer externalData : register(b0)
{
	matrix world;
	matrix worldInv;
	matrix view;
	matrix projection;
	matrix joints[MAX_JOINTS];
	uint njoints;
};

struct PreviewVertexToPixel
{
	float4 position   : SV_POSITION;
	float3 worldPos   : POSITION;
	float3 normal     : NORMAL;
	float2 uv         : TEXCOORD;
	float3 tangent    : POSITION1;
	float3 bitangent  : POSITION2;
};

PreviewVertexToPixel main(VertexShaderInput input)
{
	PreviewVertexToPixel output;

	float4 pos = float4(input.position, 1.0f);
	float3 normal = input.normal;
	float3 tangent = input.tangent;
	float3 bitangent = input.bitangent;

	//Linear blend skinning over the four influences the vertex format carries.
	//njoints guards the index because the joint array is sized for the largest
	//skeleton, not this one's.
	matrix skin = (matrix)0;
	int use_bones = 0;
	[unroll]
	for (int i = 0; i < 4; ++i) {
		if (input.bone_ids[i] >= 0 && input.weights[i] > 0.0f && (int)njoints > input.bone_ids[i]) {
			skin += joints[input.bone_ids[i]] * input.weights[i];
			use_bones = 1;
		}
	}
	if (use_bones) {
		pos = mul(pos, skin);
		//Skinning matrices are rigid up to a uniform scale, so the frame vectors
		//can go through the same matrix - it is only the *world* transform that
		//needs the inverse treatment below.
		normal = mul(normal, (float3x3)skin);
		tangent = mul(tangent, (float3x3)skin);
		bitangent = mul(bitangent, (float3x3)skin);
	}

	float4 world_pos = mul(pos, world);
	output.worldPos = world_pos.xyz;
	output.position = mul(mul(world_pos, view), projection);

	//worldInv is the transposed inverse of the world matrix, uploaded the way the
	//engine stores Transform::world_inv_matrix. Multiplying it on the *left* is
	//what makes this the inverse-transpose of a row-vector multiply, i.e. the
	//normal matrix.
	output.normal = normalize(mul(worldInv, float4(normal, 0.0f)).xyz);
	output.tangent = normalize(mul(worldInv, float4(tangent, 0.0f)).xyz);
	output.bitangent = normalize(mul(worldInv, float4(bitangent, 0.0f)).xyz);
	output.uv = input.uv;

	return output;
}
