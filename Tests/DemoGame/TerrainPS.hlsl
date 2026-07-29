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

#include <Common/Matrix.hlsli>
#include <Common/ShaderStructs.hlsli>
#include <Common/FastNoise.hlsli>
#include <Common/PixelCommon.hlsli>

cbuffer externalData : register(b0)
{
	matrix world;
	matrix prevWorld;
	matrix view;
	matrix projection;
	AmbientLight ambientLight;
	DirLight dirLights[MAX_LIGHTS];
	PointLight pointLights[MAX_LIGHTS];
	MaterialColor material;
	int dirLightsCount;
	int pointLightsCount;
	float parallaxScale;
	int meshNormalTextureEnable;
	int highTextureEnable;
	float3 cameraPosition;
	float3 cameraDirection;
	int screenW;
	int screenH;
	float4 LightPerspectiveValues[MAX_LIGHTS / 2];
	//This block mirrors the engine's own lighting cbuffer (MainRender/MainRenderPS.hlsl)
	//field for field, because PixelFunctions.hlsli is included below and indexes into
	//it. Cascades made the dynamic array MAX_LIGHTS * MAX_SHADOW_CASCADES long; a game
	//shader left at MAX_LIGHTS here does not fail to compile, it silently shifts every
	//field after it and reads garbage matrices - which renders as a black surface.
	matrix DirPerspectiveMatrix[DIR_SHADOW_MATRIX_COUNT];
	//Static casters are one plain map per light, so this one stays MAX_LIGHTS long.
	matrix DirStaticPerspectiveMatrix[MAX_LIGHTS];
	matrix spot_view;
	float time;
	float cloud_density;

	uint multi_texture_count;
	float multi_parallax_scale;

	
	int disable_rt;

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

#include <Common/MultiTexture.hlsli>
#include <Common/PixelFunctions.hlsli>
#include <MainRender/MainRenderPS.hlsli>
#include "DemoCommons.hlsli"

Texture2D grassTexture[NGRASS];

RenderTargetRT main(GSOutput input)
{
	if (!any(input.tangent)) {
		//No occlusion test here: the terrain (and the grass the geometry shader grows
		//on it) is rendered on top of the depth pre-pass buffer, so occluded fragments
		//never reach this shader. See RenderSystem::DrawScene.
		//grass pixel
		float3 color = { 0.0f, 0.0f, 0.0f };
		switch ((int)input.bitangent.x) {
		case 0: color = grassTexture[0].Sample(basicSampler, input.uv).rgb; break;
		case 1: color = grassTexture[1].Sample(basicSampler, input.uv).rgb; break;
		case 2: color = grassTexture[2].Sample(basicSampler, input.uv).rgb; break;
		}
		if (length(color) < 0.4f) {
			discard;
		}
		float4 wpos = input.worldPos;
		int i = 0;
		float4 finalColor = { 0.0f, 0.0f, 0.0f, 1.0f };
		float4 lightColor = { 0.0f, 0.0f, 0.0f, 0.0f };
		float3 normal = input.normal;
		// Calculate the ambient light
		finalColor.rgb += CalcAmbient(normal);
		// Calculate the directional light
		for (i = 0; i < dirLightsCount; ++i) {
			finalColor.rgb += CalcDirectional(normal, wpos, input.uv, material, dirLights[i], cloud_density, i, lightColor);
		}
		
		// Calculate the point lights
		for (i = 0; i < pointLightsCount; ++i) {
			if (length(wpos - pointLights[i].Position) < pointLights[i].Range) {
				finalColor.rgb += CalcPoint(normal, wpos, input.uv, material, pointLights[i], i, lightColor);
			}
		}

		//Emission of point lights	
		matrix worldViewProj = mul(view, projection);
		float3 p2 = mul(input.worldPos, view).xyz;
		for (i = 0; i < pointLightsCount; ++i) {
			float3 p1 = mul(float4(pointLights[i].Position, 1.0f), view).xyz;
			//if we are in front of camera
			if (p1.z > 0) {
				//if pixel is behind light
				if (p1.z < p2.z) {
					finalColor.rgb += EmitPoint(input.position.xyz, worldViewProj, pointLights[i]);
				}
			}
		};

		finalColor.rgb *= color;
		RenderTargetRT output;
		output.light_map = lightColor;
		output.bloom_map = lightColor;
		output.scene = finalColor;

		RaySource ray;
		ray.orig = wpos.xyz;
		ray.dispersion = -1.0f;
		ray.normal = normal;
		ray.density = 1.0f;
		ray.opacity = 1.0f;
		ray.reflex = 1.0f;

		output.rt_ray0_map = getColor0(ray);
		output.rt_ray1_map = getColor1(ray);
		//Unlike the terrain branch below, this one builds its own RenderTargetRT, so it
		//has to fill *every* target. Leaving these two unassigned does not skip the
		//export - it exports whatever happened to be in the register, and MotionCS reads
		//that as this pixel's world position now and last frame, so the grass got a
		//motion vector out of uninitialized memory. Grass does not move, so its previous
		//position is its current one, same as WaterPS and LavaPS do.
		output.pos0_map = input.worldPos;
		output.pos1_map = input.worldPos;

		return output;
	}
	else {
		return MainRenderPS(input);
	}
}
