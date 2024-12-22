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
	int screenW;
	int screenH;
	matrix world;
	matrix inv_world;
	matrix view_projection;
	float3 cameraPosition;
}

float4 frustumPlanes[6] = {
	float4(1, 0, 0, 1),
	float4(-1, 0, 0, 1),
	float4(0, 1, 0, 1),
	float4(0, -1, 0, 1),
	float4(0, 0, 1, 1),
	float4(0, 0, -1, 1)
};

[maxvertexcount(3)]
void main(
	triangle DomainOutput input[3],
	inout TriangleStream< GSOutput > output
)
{
#if 1
	bool process = false;

	float4 p[3];
	float2 dimensions = { screenW , screenH };

	uint i = 0;
	for (i = 0; i < 3; i++) {
		p[i] = mul(input[i].worldPos, view_projection);	
	}

	bool behind = true;
	for (i = 0; i < 3; i++) {
		if (p[i].z >= 0.0f) {
			behind = false;
			break;
		}
	}

	if (!behind) {
		for (i = 0; i < 3; i++) {
			float2 pos;
			pos = p[i].xy;
			pos.xy /= p[i].w;
			pos.y *= -1.0f;

			pos.xy = (pos.xy + 1.0f) * 0.5f;
			if (pos.x >= 0.0f && pos.x <= 1.0f && pos.y >= 0.0f && pos.y <= 1.0f) {
				process = true;
				break;
			}
		}
	}

	if (!process) {
		float3 pp[3];
		for (i = 0; i < 3; ++i) {
			pp[i] = p[i].xyz / p[i].w;
		}

		float3 normal = cross(pp[1].xyz - pp[0].xyz, pp[2].xyz - pp[0].xyz);
		float d = dot(normal, pp[0].xyz);
		float3 frustumCenter = float3(0.0f, 0.0f, 0.0f);

		for (int i = 0; i < 6; i++) {
			frustumCenter += float3(frustumPlanes[i].x, frustumPlanes[i].y, frustumPlanes[i].z) * frustumPlanes[i].w;
		}
		frustumCenter /= 6.0f;

		if (abs(dot(normal, frustumCenter) + d) < length(normal)) {
			process = true;
		}
	}
#else
	bool process = true;
	float4 p[3];
	uint i = 0;
	for (i = 0; i < 3; i++) {
		p[i] = mul(input[i].worldPos, view_projection);
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
		for (uint i = 0; i < 3; i++)
		{
			GSOutput element;
			element.uv = input[i].uv;
			element.mesh_uv = input[i].mesh_uv;
			element.worldPos = input[i].worldPos;
			element.position = p[i];
			element.objectPos = input[i].position;
			element.normal = normalize(input[i].normal);
			element.tangent = mul(tangent, (float3x3)world);
			element.bitangent = mul(bitangent, (float3x3)world);
			output.Append(element);
		}
	}
}