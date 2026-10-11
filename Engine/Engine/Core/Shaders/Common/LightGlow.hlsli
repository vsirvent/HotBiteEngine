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

//The visible glow of a point light: a hot core and a halo around the light's screen
//position, added by the shaders that draw what is behind the light (the sky, lava). It is a
//post effect on the image of the lamp, not lighting, so it has nothing to do with the tile
//masks; it takes the aux lights because only the lamps near the camera are worth a glow.
//
//The includer supplies screenW and screenH.

#ifndef __LIGHT_GLOW_HLSLI__
#define __LIGHT_GLOW_HLSLI__

#include "TiledLights.hlsli"

float3 EmitPoint(float3 position, matrix worldViewProj, AuxLight aux)
{
	float3 lposition = mul(float4(aux.light.position, 1.0f), worldViewProj).xyz;
	lposition.x /= lposition.z;
	lposition.y /= -lposition.z;
	lposition.x = (lposition.x + 1.0f) * screenW / 2.0f;
	lposition.y = (lposition.y + 1.0f) * screenH / 2.0f;
	float2 ToLight = lposition.xy - position.xy;
	float DistToLight = length(ToLight);
	//The light's glow radius, in pixels: `tilt_ratio` is the core radius and the halo is
	//ten times that. 10 gives the 10 px core and 100 px halo this used to hard-code, so
	//levels that never touched the value look as they always did. Zero or less draws no
	//glow at all (a lamp you only want to light with, not to see).
	if (aux.tilt_ratio <= 0.0f) {
		return float3(0.0f, 0.0f, 0.0f);
	}
	//Light emission
	float DistLightToPixel = 1.0 - saturate(DistToLight / (aux.tilt_ratio * 10.0f));
	float DistLightToPixel2 = 1.0 - saturate(DistToLight / aux.tilt_ratio);
	float3 finalColor = UnpackLightColor(aux.light) * pow(DistLightToPixel, 10.0f);
	finalColor.rgb += pow(DistLightToPixel2, 10.0f);

	return finalColor;
}

#endif
