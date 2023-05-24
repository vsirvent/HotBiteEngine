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

#include "../../Common/ShaderStructs.hlsli"
#include "../../Common/Utils.hlsli"
#include "../../Common/Matrix.hlsli"
#include "../../Common/Quaternion.hlsli"
#include "../../Common/NoiseSimplex.hlsli"

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

[maxvertexcount(3)]
void main(
	triangle DomainOutput input[3],
	inout TriangleStream< GSOutput > output
)
{
	bool process = false;
	matrix worldViewProj = mul(view, projection);

	float4 wp[3];
	float4 p[3];
	//wl: water level (lava)
	float wl1[3];
	float wl0[3];
	uint i = 0;
	
	for (i = 0; i < 3; i++) {
		wp[i] = input[i].worldPos;
		float water_level = 0.5f*snoise(float2(time * 0.2f + wp[i].x * 0.1f, 
			                                   time * 0.2f + wp[i].z * 0.1f));
		wl1[i] = water_level;
		wl0[i] = input[i].worldPos.y;

		wp[i].y += water_level;
		p[i] = mul(wp[i], worldViewProj);
	}	
	for (i = 0; i < 3; i++) {
		float2 pos;
		pos = p[i].xy;
		pos.x /= p[i].w;
		pos.y /= -p[i].w;
		pos.x = (pos.x + 1.0f) * 2.5f;
		pos.y = (pos.y + 1.0f) * 2.5f;
		if (pos.x > -screenW*0.1f && pos.x < screenW && pos.y > -screenH * 0.1f && pos.y < screenH) {
			process = true;
			break;
		}
	}

	if (process) {
		// Edges of the triangle : postion delta
		float3 edge1 = wp[1].xyz - wp[0].xyz;
		float3 edge2 = wp[2].xyz - wp[0].xyz;

		// UV delta
		float2 deltaUV1 = input[1].uv - input[0].uv;
		float2 deltaUV2 = input[2].uv - input[0].uv;
		float dev = (deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x);
		float3 normal = { 0.f, 1.f, 0.f };
		float3 tangent = { 0.f, 0.f, 0.f };
		float3 bitangent = { 0.f, 0.f, 0.f };
		for (; i < 3; i++)
		{
			GSOutput element;
			element.uv = input[i].uv;
			element.mesh_uv = float2(wl1[i], wl0[i]);
			element.worldPos = wp[i];
			element.position = p[i];			
			element.normal = normal;
			element.tangent = tangent;
			element.bitangent = bitangent;
			output.Append(element);
		}
		output.RestartStrip();		
	}
}

