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

//What a particle billboard takes from the lights: the ambient colour and the colour of the
//directional lights, with no normal (a billboard has none worth speaking of) and no shadow
//lookup. The lit particle shaders used to call CalcAmbient/CalcDirectionalWithoutNormal, which
//were the material shaders' own copy of the lighting; with the lighting gone from them this is
//all a particle asks for, and it has no cost in registers or in lights - the point lights it
//once looped over were switched off (`#if 0`) in every one of these shaders.
//
//The includer supplies ambientLight.

#ifndef __PARTICLE_LIGHT_HLSLI__
#define __PARTICLE_LIGHT_HLSLI__

float3 ParticleAmbient()
{
	return ambientLight.AmbientDown + ambientLight.AmbientUp;
}

float3 ParticleDirectional(DirLight light)
{
	return light.Color.rgb * light.intensity;
}

#endif
