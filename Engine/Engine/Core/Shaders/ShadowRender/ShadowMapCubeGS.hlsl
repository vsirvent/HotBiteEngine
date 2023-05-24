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

struct GS_OUTPUT
{
	float4 Pos : SV_POSITION;
	uint RTIndex : SV_RenderTargetArrayIndex;
};

cbuffer cbuffercbShadowMapCubeGS : register(b0)
{
	float4x4 CubeViewProj0;
	float4x4 CubeViewProj1;
	float4x4 CubeViewProj2;
	float4x4 CubeViewProj3;
	float4x4 CubeViewProj4;
	float4x4 CubeViewProj5;
	int activeViews;
}

[maxvertexcount(18)]
void main(triangle float4 InPos[3] : SV_Position, inout
	TriangleStream<GS_OUTPUT> OutStream)
{
	GS_OUTPUT output0;
	GS_OUTPUT output1;
	GS_OUTPUT output2;
	GS_OUTPUT output3;
	GS_OUTPUT output4;
	GS_OUTPUT output5;
	int v;
	if (activeViews & 0x01) {
		output0.RTIndex = 0;
		for (v = 0; v < 3; v++)
		{
			output0.Pos = mul(InPos[v], CubeViewProj0);
			OutStream.Append(output0);
		}
		OutStream.RestartStrip();
	}
	if (activeViews & 0x02) {
		output1.RTIndex = 1;
		for (v = 0; v < 3; v++)
		{
			output1.Pos = mul(InPos[v], CubeViewProj1);
			OutStream.Append(output1);
		}
		OutStream.RestartStrip();
	}
	if (activeViews & 0x04) {
		output2.RTIndex = 2;
		for (v = 0; v < 3; v++)
		{
			output2.Pos = mul(InPos[v], CubeViewProj2);
			OutStream.Append(output2);
		}
		OutStream.RestartStrip();
	}
	if (activeViews & 0x08) {
		output3.RTIndex = 3;
		for (v = 0; v < 3; v++)
		{
			output3.Pos = mul(InPos[v], CubeViewProj3);
			OutStream.Append(output3);
		}
		OutStream.RestartStrip();
	}
	if (activeViews & 0x10) {
		output4.RTIndex = 4;
		for (v = 0; v < 3; v++)
		{
			output4.Pos = mul(InPos[v], CubeViewProj4);
			OutStream.Append(output4);
		}
		OutStream.RestartStrip();
	}
	if (activeViews & 0x20) {
		output5.RTIndex = 5;
		for (v = 0; v < 3; v++)
		{
			output5.Pos = mul(InPos[v], CubeViewProj5);
			OutStream.Append(output5);
		}
		OutStream.RestartStrip();
	}
}