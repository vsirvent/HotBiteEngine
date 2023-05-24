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

#include "GUICommon.hlsli"

Texture2D renderTexture : register(t0);
Texture2D background_texture : register(t1);
SamplerState basicSampler : register(s0);

#define WIDGET		     (1)
#define BACKGROUND_IMAGE (1 << 1)
#define BACKGROUND_ALPHA (1 << 2)
#define INVERT_UV_X (1 << 3)
#define INVERT_UV_Y (1 << 4)

cbuffer externalData : register(b0)
{
	unsigned int screenW;
	unsigned int screenH;
	unsigned int ui_flags;
	float alpha;
	float3 background_alpha_color;
}

float4 main(VertexOutput input) : SV_TARGET
{
	float texture_alpha = 1.0f;
	float4 color = { 0.0f, 0.0f, 0.0f, 0.0f };
	if (ui_flags & INVERT_UV_X) {
		input.uv.x = 1.0f - input.uv.x;
	}
	if (ui_flags & INVERT_UV_Y) {
		input.uv.y = 1.0f - input.uv.y;
	}
	if (ui_flags & WIDGET) {
		if (ui_flags & BACKGROUND_IMAGE) {
			color = background_texture.Sample(basicSampler, input.uv.xy);
			if (ui_flags & BACKGROUND_ALPHA) {
				texture_alpha = length(background_alpha_color.rgb - color.rgb);
				if (texture_alpha < 0.3f) {
					discard;
				}
			}			
		}
		else {
			color = input.color;
		}	
		color.a = min(alpha, color.a);
		if (color.a == 0.0f) {
			discard;
		}
	}
	else {
		float2 tpos = input.position.xy;
		tpos.x /= screenW;
		tpos.y /= screenH;
		color = renderTexture.Sample(basicSampler, tpos);
	}
	return color;
}
