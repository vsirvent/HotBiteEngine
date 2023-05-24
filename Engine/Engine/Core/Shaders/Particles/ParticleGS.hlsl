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
#include "../Common/Utils.hlsli"

cbuffer externalData : register(b0)
{
	int screenW;
	int screenH;
	matrix view;
	matrix projection;
	float3 cameraPosition;
	float sizeIncrementRatio;
}

[maxvertexcount(6)]
void main(
	point VertexParticleOutput input[1],
	inout TriangleStream< GSParticleOutput > output
)
{
		bool process = false;
		matrix worldViewProj = mul(view, projection);

		float4 p = mul(input[0].worldPos, worldViewProj);
		float2 pos;
		pos = p.xy;
		pos.x /= p.w;
		pos.y /= -p.w;
		pos.x = (pos.x + 1.0f) * 0.5f;
		pos.y = (pos.y + 1.0f) * 0.5f;
		if (pos.x > -screenW * 0.1f && pos.x < screenW && pos.y > -screenH * 0.1f && pos.y < screenH) {
			process = true;			
		}
		if (process) {
			GSParticleOutput element;
			element.life = input[0].life;
			element.size = input[0].size;
			element.id = input[0].id;
			element.center = input[0].worldPos;
			float a = element.size + sizeIncrementRatio * element.size * (1.0f - element.life);
			float3 normal = normalize(cameraPosition - input[0].position);
			float3 tangent;
			float3 bitangent;

			tangent = normalize(cross(normal, float3(0.0f, 1.0f, 0.0f)));
			bitangent = cross(normal, tangent);

			tangent = normalize(tangent - dot(tangent, normal) * normal);
			bitangent = normalize(bitangent - dot(bitangent, normal) * normal - dot(bitangent, tangent) * tangent);

			//..........
			float3 up = tangent * a;
			float3 dir = bitangent * a;
			float3 o = input[0].worldPos.xyz;

			float3 p1 = o + up + dir;
			float2 p1_uv = { 1.0f, 1.0f };
			float3 p2 = o + up - dir;
			float2 p2_uv = { 0.0f, 1.0f };
			float3 p3 = o - up + dir;
			float2 p3_uv = { 1.0f, 0.0f };
			float3 p4 = o - up - dir;
			float2 p4_uv = { 0.0f, 0.0f };
			//---- vertical bottom
			element.worldPos = float4(p4, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p4_uv;
			output.Append(element);
			element.worldPos = float4(p2, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p2_uv;
			output.Append(element);
			element.worldPos = float4(p3, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p3_uv;
			output.Append(element);
			output.RestartStrip();

			element.worldPos = float4(p1, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p1_uv;
			output.Append(element);
			element.worldPos = float4(p3, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p3_uv;
			output.Append(element);
			element.worldPos = float4(p2, 1.0f);
			element.uv = p2_uv;
			element.position = mul(element.worldPos, worldViewProj);
			output.Append(element);
			output.RestartStrip();
#if 0
			//This code is to create a star instead of a plane looking the camera, disabled 
			//..........
			float3 up = float3(0.0f, 1.0f, 0.0f) * a;
			float3 dir = float3(1.0f, 0.0f, 0.0f) * a;
			float3 o = input[0].worldPos.xyz;
			float2 o_uv = { 0.0f, 0.5f };
			float3 d = o + float3(0.0f, 0.0f, 1.0f) * a;
			float2 d_uv = { 1.0f, 0.5f };
			float3 p0 = o + dir;
			float2 p0_uv = { 0.0f, 1.0f };
			float3 p1 = d + dir;
			float2 p1_uv = { 1.0f, 1.0f };
			float3 p2 = d - dir;
			float2 p2_uv = { 1.0f, 0.0f };
			float3 p3 = o - dir;
			float2 p3_uv = { 0.0f, 0.0f };
			float3 p4 = o + up;
			float2 p4_uv = { 0.0f, 1.0f };
			float3 p5 = d + up;
			float2 p5_uv = { 1.0f, 1.0f };
			float3 p6 = d - up;
			float2 p6_uv = { 1.0f, 0.0f };
			float3 p7 = o - up;
			float2 p7_uv = { 0.0f, 0.0f };

			float3 p8 = (p3 + p4) / 2.0f;
			float2 p8_uv = { 0.0f, 1.0f };
			float3 p9 = (p4 + p0) / 2.0f;
			float2 p9_uv = { 1.0f, 1.0f };
			float3 p10 = (p3 + p7) / 2.0f;
			float2 p10_uv = { 0.0f, 0.0f };
			float3 p11 = (p0 + p7) / 2.0f;
			float2 p11_uv = { 0.0f, 1.0f };
			float3 p12 = (p2 + p5) / 2.0f;
			float2 p12_uv = { 0.0f, 1.0f };
			float3 p13 = (p1 + p5) / 2.0f;
			float2 p13_uv = { 1.0f, 1.0f };
			float3 p14 = (p2 + p6) / 2.0f;
			float2 p14_uv = { 0.0f, 0.0f };
			float3 p15 = (p1 + p6) / 2.0f;
			float2 p15_uv = { 1.0f, 0.0f };
			//---- vertical bottom
			element.worldPos = float4(o, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = o_uv;
			output.Append(element);
			element.worldPos = float4(d, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = d_uv;
			output.Append(element);
			element.worldPos = float4(p7, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p7_uv;
			output.Append(element);
			output.RestartStrip();

			element.worldPos = float4(d, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = d_uv;

			output.Append(element);
			element.worldPos = float4(p6, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p6_uv;
			output.Append(element);
			element.worldPos = float4(p7, 1.0f);
			element.uv = p7_uv;
			element.position = mul(element.worldPos, worldViewProj);
			output.Append(element);
			output.RestartStrip();

			//---- horizontal far
			element.worldPos = float4(o, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = o_uv;
			output.Append(element);
			element.worldPos = float4(d, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = d_uv;
			output.Append(element);
			element.worldPos = float4(p3, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p3_uv;
			output.Append(element);
			output.RestartStrip();

			element.worldPos = float4(d, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = d_uv;
			output.Append(element);
			element.worldPos = float4(p2, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p2_uv;
			output.Append(element);
			element.worldPos = float4(p3, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p3_uv;
			output.Append(element);
			output.RestartStrip();
			//---- horizontal near
			element.worldPos = float4(o, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = o_uv;
			output.Append(element);
			element.worldPos = float4(p0, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p0_uv;
			output.Append(element);
			element.worldPos = float4(p1, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p1_uv;
			output.Append(element);
			output.RestartStrip();

			element.worldPos = float4(d, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = d_uv;
			output.Append(element);
			element.worldPos = float4(o, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = o_uv;
			output.Append(element);
			element.worldPos = float4(p1, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p1_uv;
			output.Append(element);
			output.RestartStrip();

			//---- vertical top
			element.worldPos = float4(o, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = o_uv;
			output.Append(element);
			element.worldPos = float4(p4, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p4_uv;
			output.Append(element);
			element.worldPos = float4(p5, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p5_uv;
			output.Append(element);
			output.RestartStrip();

			element.worldPos = float4(d, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = d_uv;
			output.Append(element);
			element.worldPos = float4(o, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = o_uv;
			output.Append(element);
			element.worldPos = float4(p5, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p5_uv;
			output.Append(element);
			output.RestartStrip();

			//---- tap 1
			element.worldPos = float4(p8, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p8_uv;
			output.Append(element);
			element.worldPos = float4(p9, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p9_uv;
			output.Append(element);
			element.worldPos = float4(p10, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p10_uv;
			output.Append(element);
			output.RestartStrip();

			element.worldPos = float4(p11, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p11_uv;
			output.Append(element);
			element.worldPos = float4(p10, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p10_uv;
			output.Append(element);
			element.worldPos = float4(p9, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p9_uv;
			output.Append(element);
			output.RestartStrip();

			//---- tap 2
			element.worldPos = float4(p12, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p12_uv;
			output.Append(element);
			element.worldPos = float4(p13, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p13_uv;
			output.Append(element);
			element.worldPos = float4(p14, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p14_uv;
			output.Append(element);
			output.RestartStrip();

			element.worldPos = float4(p15, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p15_uv;
			output.Append(element);
			element.worldPos = float4(p14, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p14_uv;
			output.Append(element);
			element.worldPos = float4(p13, 1.0f);
			element.position = mul(element.worldPos, worldViewProj);
			element.uv = p13_uv;
			output.Append(element);
			output.RestartStrip();
#endif
		}
}