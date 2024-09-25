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

#include <Common/ShaderStructs.hlsli>
#include <Common/Utils.hlsli>
#include <Common/Matrix.hlsli>
#include <Common/Quaternion.hlsli>
#include <Common/NoiseSimplex.hlsli>
#include "GameCommons.hlsli"

cbuffer externalData : register(b0)
{
	int screenW;
	int screenH;
	matrix world;
	matrix inv_world;
	matrix view;
	matrix projection;
	float3 cameraPosition;
	float time;
}

void CreateGrass(float3 pos, matrix worldViewProj, inout TriangleStream< GSOutput > output) {

	float3 pos0 = float3(0.0f, 0.0f, 0.0f);
	float3 pos1 = float3(0.0f, 1.0f, 0.0f);
	float3 pos2 = float3(0.0f, 2.0f, 0.0f);
	float3 pos3 = float3(2.0f, 0.0f, 0.0f);
	float3 pos4 = float3(2.0f, 1.0f, 0.0f);
	float3 pos5 = float3(2.0f, 2.0f, 0.0f);
	float2 uv5 = { 0.0f, 0.0f };
	float2 uv4 = { 0.0f, 0.5f };
	float2 uv3 = { 0.0f, 1.0f };
	float2 uv2 = { 1.0f, 0.0f };
	float2 uv1 = { 1.0f, 0.5f };
	float2 uv0 = { 1.0f, 1.0f };
	float3 p[12] = { pos0, pos3, pos4, pos0, pos4, pos1,
				   pos1, pos4, pos5, pos1, pos5, pos2 };

	float2 v[12] = { uv0, uv3, uv4, uv0, uv4, uv1,
				   uv1, uv4, uv5, uv1, uv5, uv2 };
	float r0 = rand2d(pos.xy);
	float r1 = rand2d(pos.xz);
	float r2 = rand2d(pos.yz);
	float angle = r0*PI;
	float wind_angle = pow(snoise(float2(time * 0.02f + (pos.x + pos.z)*0.01f, time * 0.02f + (pos.y + pos.z) * 0.01f)), 4.0f);
	pos1.y *= (1.0f - r1 * 0.8f);
	pos2.y *= (1.0f - r2 * 0.8f);
	pos4.y *= (1.0f - r1 * 0.8f);
	pos5.y *= (1.0f - r2 * 0.8f);
	float dx = lerp(0.0f, 0.1f, r1);
	float dy = lerp(0.0f, 0.1f, r2);
	float4 q = rotate_angle_axis(angle, float3(dx, 1.0f, dy));
	float4 wind_q = rotate_angle_axis(wind_angle, float3(1.0f, 0.0f, 0.0f));
	float4 wind_q2 = rotate_angle_axis(wind_angle*2.0f, float3(1.0f, 0.0f, 0.0f));
	for (uint n = 0; n < 4; ++n) {
		for (uint x = 0; x < 3; ++x) {
			uint elem = n*3 + x;
			GSOutput element;
			element.uv = v[elem];
			element.mesh_uv = float2(0, 0);
			if (elem == 8 || elem == 10 || elem == 11) {
				element.worldPos = float4(pos + rotate_vector(rotate_vector(p[elem], q), wind_q2), 1.0f);
			}
			else {
				element.worldPos = float4(pos + rotate_vector(rotate_vector(p[elem], q), wind_q), 1.0f);
			}
			element.objectPos = element.worldPos;
			element.position = mul(element.worldPos, worldViewProj);
			element.normal = float3(0.0f, 0.0f, 1.0f);
			element.tangent = float3(0.0f, 0.0f, 0.0f);
			element.bitangent = float3(randRange(0.0f, 2.1f, pos.x + pos.z), 0.0f, 0.0f);
			output.Append(element);
		}
		output.RestartStrip();
	}
	
}

[maxvertexcount(39)]
void main(
	triangle TerrainDomainOutput input[3],
	inout TriangleStream< GSOutput > output
)
{
	matrix worldViewProj = mul(view, projection);
#if 0
	bool process = false;
	uint i = 0;
	float4 p[3];
	for (i = 0; i < 3; i++) {
		p[i] = mul(input[i].worldPos, worldViewProj);
	}
	for (i = 0; i < 3; i++) {
		float2 pos;
		pos = p[i].xy;
		pos.x /= p[i].w;
		pos.y /= -p[i].w;
		pos.x = (pos.x + 1.0f) * 0.5f;
		pos.y = (pos.y + 1.0f) * 0.5f;
		if (pos.x > -0.2f && pos.x < 1.2f && pos.y > -0.2f && pos.y < 1.2f) {
			process = true;
			break;
		}		
	}
#else
	bool process = true;
	float4 p[3];
	uint i = 0;
	for (i = 0; i < 3; i++) {
		p[i] = mul(input[i].worldPos, worldViewProj);
	}
#endif
	if (process) {

		// Edges of the triangle : postion delta
		float3 edge1 = input[1].position.xyz - input[0].position.xyz;
		float3 edge2 = input[2].position.xyz - input[0].position.xyz;

		// UV delta
		float2 deltaUV1 = input[1].uv - input[0].uv;
		float2 deltaUV2 = input[2].uv - input[0].uv;
		float dev = (deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x);

		float3 tangent = { 0.f, 0.f, 0.f };
		float3 bitangent = { 0.f, 0.f, 0.f };
		if (dev != 0.0f) {
			float r = 1.0f / dev;
			tangent.x = r * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
			tangent.y = r * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
			tangent.z = r * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

			bitangent.x = r * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
			bitangent.y = r * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
			bitangent.z = r * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
		}
		uint i = 0;
		for (; i < 3; i++)
		{
			GSOutput element;
			element.uv = input[i].uv;
			element.mesh_uv = input[i].mesh_uv;
			element.worldPos = input[i].worldPos;
			element.objectPos = input[i].position;
			element.position = p[i];
			element.normal = normalize(input[i].normal);
			element.tangent = mul(tangent, (float3x3)world);
			element.bitangent = mul(bitangent, (float3x3)world);
			output.Append(element);
		}
		output.RestartStrip();
		if (input[0].tess_factor > 2.5f) {
			float3 d0 = (input[0].worldPos - input[1].worldPos).xyz / 3.0f;
			float3 d1 = (input[0].worldPos - input[2].worldPos).xyz / 3.0f;

			float3 p = input[0].worldPos.xyz;
			p += d0;
			CreateGrass(p, worldViewProj, output);
			p += d1;
			CreateGrass(p, worldViewProj, output);
			p -= d0;
			CreateGrass(p, worldViewProj, output);
		}
	}
}
