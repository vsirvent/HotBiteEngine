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
Vertex stage of the material thumbnail pass used by the Scene Editor's Materials
panel. It is deliberately separate from MainRenderVS: a thumbnail draws one
unskinned sphere with no tessellation, shadows or instancing, so it needs none of
the joint/displacement machinery and must not pay for it once per material per
redraw. See MaterialPreviewPS.hlsl for what the pair does and does not model.
*/

#include "../Common/ShaderStructs.hlsli"

cbuffer externalData : register(b0)
{
	matrix world;
	matrix view;
	matrix projection;
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
	matrix worldViewProj = mul(mul(world, view), projection);

	output.position = mul(pos, worldViewProj);
	output.worldPos = mul(pos, world).xyz;
	//The preview sphere is only ever uniformly scaled, so the world matrix can be
	//used directly on the frame vectors - no inverse-transpose needed.
	output.normal = normalize(mul(float4(input.normal, 0.0f), world).xyz);
	output.tangent = normalize(mul(float4(input.tangent, 0.0f), world).xyz);
	output.bitangent = normalize(mul(float4(input.bitangent, 0.0f), world).xyz);
	output.uv = input.uv;
	return output;
}
