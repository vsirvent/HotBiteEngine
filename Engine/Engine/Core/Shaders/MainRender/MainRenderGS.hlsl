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
	matrix view;
	matrix projection;
	float3 cameraPosition;
}

[maxvertexcount(3)]
void main(
	triangle DomainOutput input[3],
	inout TriangleStream< GSOutput > output
)
{
#if 0
	bool process = false;

	float4 p[3];
	
	uint i = 0;
	for (i = 0; i < 3; i++) {
		p[i] = mul(input[i].worldPos, mul(view, projection));
	}
	for (i = 0; i < 3; i++) {
		float2 pos;
		pos = p[i].xy;
		pos.x /= p[i].w;
		pos.y /= -p[i].w;
		pos.x = (pos.x + 1.0f) * 0.5f;
		pos.y = (pos.y + 1.0f) * 0.5f;
		if (pos.x > -screenW * 2.0f && pos.x < screenW * 2.0f && pos.y > -screenH * 2.0f && pos.y < screenH * 2.0f) {
			process = true;
			break;
		}
	}
#else
	bool process = true;
	float4 p[3];
	uint i = 0;
	for (i = 0; i < 3; i++) {
		p[i] = mul(input[i].worldPos, mul(view, projection));
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
		matrix worldViewProj = mul(view, projection);
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