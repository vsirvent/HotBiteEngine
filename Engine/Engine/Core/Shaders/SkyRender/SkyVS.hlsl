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
	int is_space;
};

VertexToPixel main(VertexShaderInput input)
{
	VertexToPixel output;
	float4 pos = float4(input.position, 1.0f);
	matrix worldViewProj;
	if (is_space) {
		output.worldPos = mul(pos, world);
		output.position = mul(pos, mul(mul(world, view), projection));
	}
	else {
		//Fix clouds y value to 1000
		if (pos.y > 0.0f) {
			pos.y = 1000.0f;
			//float3 dir = normalize(pos);
			//dir = dir / dir.y;
			//pos.xyz = dir * 1000.0f;			
		}
		output.worldPos = pos;
		output.position = mul(pos, mul(view, projection));
	}
	output.uv = input.uv;
	return output;
}
