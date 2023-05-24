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

cbuffer externalData : register(b0)
{
	matrix world;
	matrix view;
	matrix projection;
	matrix joints[MAX_JOINTS];
	uint njoints;
};

DepthVertexToPixel main(VertexShaderInput input)
{
	DepthVertexToPixel output;
	int use_bones = 0;
	matrix m;
	for (int i = 0; i < 4; ++i) {
		if (input.bone_ids[i] >= 0 && input.weights[i] > 0.0f && (int)njoints > input.bone_ids[i]) {
			m += joints[input.bone_ids[i]] * input.weights[i];
			use_bones = 1;
		}
	}
	float4 pos;
	if (use_bones) {
		pos = mul(float4(input.position, 1.0f), m);
	}
	else {
		pos = float4(input.position, 1.0f);
	}

	matrix worldViewProj = mul(mul(world, view), projection);
	output.worldPos = mul(pos, world);
	output.position = mul(pos, worldViewProj);
	return output;
}
