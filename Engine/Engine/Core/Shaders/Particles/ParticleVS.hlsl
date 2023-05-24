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

#include "../Common/Matrix.hlsli"
#include "../Common/ShaderStructs.hlsli"

cbuffer externalData : register(b0)
{
	matrix world;
	float3 cameraPosition;
	float3 cameraDirection;
	float3 offset;
	matrix joints[MAX_JOINTS];
	uint njoints;
#define PARTICLES_FLAG_LOCAL 1 << 0
	uint flags;
};

VertexParticleOutput main(VertexParticleInput input)
{
	VertexParticleOutput output;
	int use_bones = 0;
	matrix m;
	if (flags & PARTICLES_FLAG_LOCAL) {
		for (int i = 0; i < 4; ++i) {
			if (input.bone_ids[i] >= 0 && input.weights[i] > 0.0f && (int)njoints > input.bone_ids[i]) {
				m += joints[input.bone_ids[i]] * input.weights[i];
				use_bones = 1;
			}
		}
	}
	float4 pos;
	float3 normal;
	if (use_bones) {
		pos = mul(float4(input.position, 1.0f), m);
		normal = mul(input.normal, (float3x3)m);
	}
	else {
		pos = float4(input.position, 1.0f);
		normal = input.normal;
	}
	if (flags & PARTICLES_FLAG_LOCAL) {
		pos = mul(pos, world);
		pos.xyz += offset;
	}
	normal = input.normal;
	
	output.position = pos;
	output.worldPos = pos;
	output.normal = normal;
	output.life = input.life;
	output.size = input.size;
	output.id = input.id;
	return output;
}
