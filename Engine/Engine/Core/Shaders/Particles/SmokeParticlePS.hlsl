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
#include "../Common/PixelCommon.hlsli"
#include "../Common/FastNoise.hlsli"
#include "../Common/RGBANoise.hlsli"

cbuffer externalData : register(b0)
{
	matrix world;
	matrix view;
	matrix projection;
	AmbientLight ambientLight;
	DirLight dirLights[MAX_LIGHTS];
	MaterialColor material;
	int dirLightsCount;
	float parallaxScale;
	int meshNormalTextureEnable;
	int highTextureEnable;
	float3 cameraPosition;
	float3 cameraDirection;
	int screenW;
	int screenH;
	matrix spot_view;
	float time;
	float sizeIncrementRatio;

	uint multi_texture_count;
	float multi_parallax_scale;

	uint4 packed_multi_texture_operations[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_values[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_uv_scales[MAX_MULTI_TEXTURE / 4];
	//Per layer, the orientation and altitude rules as (min, max, fade, enabled).
	//Declared here rather than in MultiTexture.hlsli because that file is an
	//include: every shader that pulls it in has to supply the constants it reads.
	float4 multi_texture_slope[MAX_MULTI_TEXTURE];
	float4 multi_texture_height[MAX_MULTI_TEXTURE];
	//Per layer, the mask image's own UV transform as (scale, scale, offset u,
	//offset v). Separate from the uv_scale that tiles the detail maps: a splat
	//map covers the surface once, the rock on it repeats tens of times.
	float4 multi_texture_mask_uv[MAX_MULTI_TEXTURE];
}


#include "../Common/PixelFunctions.hlsli"
#include "../Common/ParticleLight.hlsli"
//Value noise summed over four octaves, the domain warped once by the noise itself: the billows of a smoke rather than the grain of one
//noise. 0..1.
float SmokeFbm(float3 q)
{
	float3 warp = float3(rgba_fnoise(q * 1.7f + 11.3f), rgba_fnoise(q * 1.7f + 27.1f), rgba_fnoise(q * 1.7f + 5.9f)) - 0.5f;
	q += warp * 0.9f;
	float n = 0.0f;
	float amplitude = 0.5f;
	for (int octave = 0; octave < 4; ++octave) {
		n += rgba_fnoise(q) * amplitude;
		q = q * 2.03f + 17.17f;
		amplitude *= 0.5f;
	}
	return n / 0.9375f;
}

//A puff of smoke, drawn on a quad that faces the camera as if it were a ball of smoke: the density follows how thick a sphere is behind each
//pixel, so the edge is soft and round; the billows are noise in the puff's own frame (its id is its seed, and it turns slowly and churns
//as it ages), so they move with the puff instead of sliding through it; it is lit from above, darker where it is dense, and fades where
//it meets the scene behind it.
RenderTarget main(GSParticleOutput input)
{
	RenderTarget output;
	float4 lightColor = { 0.0f, 0.0f, 0.0f, 0.0f };
	float4 finalColor = material.diffuseColor;
	float fade_in = saturate((1.0f - input.life) / 0.15f);
	float age = 1.0f - input.life;
	//The smoke's colour lit by the sky: the ambient and the directional lights. Multiplied, not added: added, the light washed every
	//puff to the sky's own colour (near white by day, so the smoke vanished against the snow) whatever colour the material gave it.
	float3 light = ParticleAmbient();
	uint i = 0;
	for (i = 0; i < (uint)dirLightsCount; ++i) {
		light += 0.6f * ParticleDirectional(dirLights[i]);
	}

	//The quad as -1..1; up on the screen is -x (ParticleGS's corners).
	float2 p = input.uv * 2.0f - 1.0f;
	float r2 = dot(p, p);
	float thickness = sqrt(saturate(1.0f - r2));
	float up = -p.x;
	//Each puff turned by its own angle, turning slowly one way or the other.
	float seed = (float)input.id;
	float spin = seed * 2.39996f + time * (((input.id & 1u) != 0u) ? 0.12f : -0.12f);
	float2 pr = float2(p.x * cos(spin) - p.y * sin(spin), p.x * sin(spin) + p.y * cos(spin));
	float3 q = float3(pr * 2.2f, seed * 7.13f + age * 1.6f + time * 0.05f);
	float n = SmokeFbm(q);

	//The noise cuts the ball into billows with gaps between them; the middle of the ball is the densest.
	float density = saturate(thickness * 1.4f - 0.1f) * smoothstep(0.35f, 0.75f, n + thickness * 0.25f);

	//Soft where the puff meets the scene: it fades over a part of its own size as the surface behind comes close (depthTexture is the
	//scene's world distance from the camera, RenderSystem's depth_map).
	float scene = depthTexture.Load(int3((int2)input.position.xy, 0));
	float here = length(input.worldPos.xyz - cameraPosition);
	float soft = scene > 0.0f ? saturate((scene - here) / max(input.size, 0.1f)) : 1.0f;

	//Lit from above: the top of a puff takes the sky's light, its underside and its dense core are in their own shadow.
	float shade = lerp(0.6f, 1.15f, saturate(0.5f + 0.5f * up + 0.25f * (n - 0.5f)));
	shade *= lerp(1.0f, 0.7f, density);
	//An old puff has thinned and lightened.
	shade *= lerp(1.0f, 1.15f, age);
	finalColor.rgb *= light * shade;
	//Not opaque on its own: a plume is the puffs overlapping, so it is thick where they pile up and thin where one is alone.
	finalColor.a = density * saturate(input.life * 1.6f) * fade_in * soft * 0.65f;

	output.light_map = saturate(lightColor);
	output.scene = saturate(finalColor);
	return output;
}
