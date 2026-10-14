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

//The one place the engine shades a surface with light.
//
//The G-buffer pass (MainRenderPS, WaterPS, LavaPS, SplatRasterCS) no longer evaluates a
//single light: it writes position, normal, emission and the MATERIAL MAP (GBuffer.hlsli), and
//DeferredLightCS shades the result with these functions, over the lights the tile masks
//say can reach the pixel. That is what removed the 8 light constant buffer from every
//material shader.
//
//The includer supplies as constants: cameraPosition, ambientLight, `time`, and what
//LightShadows.hlsli asks for - and what ShadowRays.hlsli asks for, which it includes
//before this file: ShadePoint traces a ray for the lights flagged PACKED_LIGHT_RAYTRACED.

#ifndef __DEFERRED_LIGHTING_HLSLI__
#define __DEFERRED_LIGHTING_HLSLI__

#include "TiledLights.hlsli"
#include "GBuffer.hlsli"
#include "LightShadows.hlsli"

float3 ShadeAmbient(float3 normal)
{
	// Convert from [-1, 1] to [0, 1]
	float up = normal.y * 0.5 + 0.5;
	return ambientLight.AmbientDown + up * ambientLight.AmbientUp;
}

float3 ShadeDirectional(float3 normal, float4 position, float spec_intensity, float bloom_scale,
                        DirLight light, float cloud_density, int index, inout float3 bloom)
{
	float3 color = light.Color.rgb * light.intensity;
	// Phong diffuse
	float NDotL = dot(light.DirToLight, normal);
	float3 finalColor = color * saturate(NDotL);
	float3 bloomColor = { 0.f, 0.f, 0.f };
	//Back reflex
	float NDotL2 = dot(-light.DirToLight, normal);
	finalColor += color * saturate(NDotL2) * 0.2f;

	// Blinn specular
	float3 ToEye = cameraPosition.xyz - position.xyz;
	ToEye = normalize(ToEye);
	float3 HalfWay = normalize(ToEye + light.DirToLight);
	float NDotH = saturate(dot(HalfWay, normal));
	float3 spec_color = { 0.f, 0.f, 0.f };
	spec_color += pow(NDotH, 200.0f) * spec_intensity * 0.5f;
	spec_color += color * pow(NDotH, 100.0f) * spec_intensity;
	spec_color += color * pow(NDotH, 2.0f) * spec_intensity * 0.1f;
	finalColor += spec_color;
	bloomColor += spec_color;

	float shadow = 1.0f;
	//A ray traced sun traces only where its caster scene reaches (shadow_ray_sun_distance round
	//the camera, the cascades' own reach); beyond it the ground is lit, as it is past the last
	//cascade.
	const bool ray_traced = !light.cast_shadow && (light.flags & DIR_LIGHT_FLAG_RAYTRACED) != 0 &&
		length(position.xyz - cameraPosition) < shadow_ray_sun_distance;
	if (light.cast_shadow || ray_traced) {
		if (ray_traced) {
			const float side = (NDotL >= 0.0f) ? 1.0f : -1.0f;
			shadow = ShadowRayOccludedDirectional(position.xyz + normal * (side * SR_SURFACE_BIAS), light.DirToLight) ? 0.0f : 1.0f;
		}
		else {
			shadow = (DirShadowPCFAll(position, light, index));
		}
		if (cloud_density > 0.0f) {
			shadow -= CloudPCF(position, light, cloud_density);
		}
		shadow = saturate(shadow);
	}
	float3 final = ApplyDirShadowDebug(finalColor * shadow, position, light, index);
	bloom += bloomColor * shadow * bloom_scale;
	return final;
}

//How much of a point light's range is left at `position`: 1 at the light, 0 at the
//edge of the range or outside the cone. The tile masks are conservative (a light is in
//a tile's mask if its range reaches the tile's depth slab, not each pixel), so this is
//also the per-pixel reject.
float PointAttenuation(PackedLight light, float3 position, out float3 ToLight, out float DistToLight)
{
	ToLight = light.position - position;
	DistToLight = length(ToLight);
	float LightRange = (light.range - DistToLight) / light.range;
	float DistToLightNorm = saturate(LightRange);
	float Attn = saturate(DistToLightNorm * DistToLightNorm);
	Attn *= PackedSpotFactor(light, position - light.position);
	return Attn;
}

float3 ShadePoint(float3 normal, float3 position, float spec_intensity, float bloom_scale,
                  PackedLight light, inout float3 bloom)
{
	float3 ToLight;
	float DistToLight;
	float Attn = PointAttenuation(light, position, ToLight, DistToLight);
	if (Attn <= 0.0f) {
		return float3(0.0f, 0.0f, 0.0f);
	}
	float3 lcolor = UnpackLightColor(light);
	float3 ToEye = cameraPosition - position;
	// Phong diffuse
	ToLight /= max(DistToLight, 1e-5f); // Normalize
	float NDotL = saturate(dot(ToLight, normal));
	float NDotL2 = saturate(dot(-ToLight, normal));
	float3 finalColor = lcolor * NDotL;
	//Back reflex
	finalColor += lcolor * NDotL2 * 0.2f;

	// Blinn specular
	ToEye = normalize(ToEye);
	float3 HalfWay = normalize(ToEye + ToLight);
	float NDotH = saturate(dot(HalfWay, normal));
	float3 spec_color = { 0.f, 0.f, 0.f };
	spec_color += pow(NDotH, 200.0f) * spec_intensity * 0.5f;
	spec_color += lcolor * pow(NDotH, 100.0f) * spec_intensity;
	spec_color += lcolor * pow(NDotH, 2.0f) * spec_intensity * 0.1f;
	finalColor += spec_color;
	float3 bloomColor = spec_color;

	float shadow = 1.0f;
	int slot = LightShadowSlot(light);
	if (slot >= 0) {
		shadow = PointShadowPCF(position - light.position, slot);
	}
	else if (LightIsRayTraced(light)) {
		//No shadow map: one ray from the surface to the light. It starts on the side of the
		//surface the light is on, so a face turned away from it is blocked by its own
		//object rather than lit through it.
		const float side = (dot(ToLight, normal) >= 0.0f) ? 1.0f : -1.0f;
		shadow = ShadowRayOccluded(position + normal * (side * SR_SURFACE_BIAS), light.position) ? 0.0f : 1.0f;
	}
	finalColor *= Attn * shadow;
	bloomColor *= Attn * shadow;
	bloom += bloomColor * bloom_scale;
	return finalColor;
}

//---- water ---------------------------------------------------------------------------
//The surface's own light only: what the bottom shows through is added by WaterPS.

float3 ShadeWaterDirectional(float3 normal, float3 position, DirLight light, inout float3 bloom)
{
	const float spec_intensity = 1.0f;
	float3 color = light.Color.rgb * light.intensity;
	float3 finalColor = { 0.f, 0.f, 0.f };
	float3 ToEye = normalize(cameraPosition.xyz - position.xyz);

	//Diffuse. This term did not exist: the surface was lit by a specular highlight and
	//nothing else, so every part of a lake the sun's reflection did not land on was
	//left completely unlit - which is what made the water read as tar rather than as
	//water with a colour.
	float NDotL = saturate(dot(normal, light.DirToLight));
	finalColor += color * NDotL;

	// Blinn specular
	float3 HalfWay = normalize(ToEye + light.DirToLight);
	float NDotH = saturate(dot(HalfWay, normal));
	//Fresnel (Schlick, water's F0 = 0.02): a lake is nearly matte looked straight down
	//into and a mirror at a grazing angle. Without it the sun's glint is equally strong
	//everywhere, which reads as a flat sheet however good the wave normals are.
	float NDotV = saturate(dot(normal, ToEye));
	float fresnel = 0.02f + 0.98f * pow(1.0f - NDotV, 5.0f);
	//Tinted by the light rather than white, so a low sun lays an orange glare on the
	//water instead of a white one.
	float3 spec_color = color * pow(NDotH, 500.0f) * spec_intensity * 15.0f * fresnel;
	finalColor += spec_color;
	bloom += spec_color;
	return finalColor;
}

float3 ShadeWaterPoint(float3 normal, float3 position, PackedLight light, inout float3 bloom)
{
	const float spec_intensity = 1.0f;
	float3 lcolor = UnpackLightColor(light);
	float3 ToLight = light.position - position;
	float3 ToEye = cameraPosition - position;
	float DistToLight = length(ToLight);
	// Phong diffuse
	ToLight /= max(DistToLight, 1e-5f); // Normalize
	float NDotL = saturate(dot(ToLight, normal));
	float3 finalColor = pow(lcolor * NDotL, 4.0f);
	// Blinn specular
	ToEye = normalize(ToEye);
	float3 HalfWay = normalize(ToEye + ToLight);
	float NDotH = saturate(dot(HalfWay, normal));
	float3 bloomColor = { 0.f, 0.f, 0.f };
	finalColor += pow(NDotH, 400.0f) * spec_intensity;
	bloomColor.rgb += (saturate(NDotH - 0.9999f)) * 9999.0f * (spec_intensity);
	//The range attenuation is not applied to the water's colour (never was); only the
	//cone is, so a spotlight does not light the water outside its beam.
	float spot = PackedSpotFactor(light, position - light.position);
	bloom += bloomColor * spot;
	return finalColor * spot;
}

//---- lava ----------------------------------------------------------------------------

float3 ShadeLavaDirectional(float3 normal, float4 position, DirLight light, int index)
{
	float3 color = light.Color.rgb * light.intensity;
	// Phong diffuse
	float NDotL = dot(light.DirToLight, normal);
	float3 finalColor = pow(color * saturate(NDotL), 4.0f);
	float shadow = saturate(DirShadowPCFAll(position, light, index));
	return finalColor * shadow;
}

float3 ShadeLavaPoint(float3 normal, float3 position, PackedLight light)
{
	float3 ToLight;
	float DistToLight;
	float Attn = PointAttenuation(light, position, ToLight, DistToLight);
	if (Attn <= 0.0f) {
		return float3(0.0f, 0.0f, 0.0f);
	}
	float3 lcolor = UnpackLightColor(light);
	ToLight /= max(DistToLight, 1e-5f); // Normalize
	float NDotL = saturate(dot(ToLight, normal));
	float3 finalColor = pow(lcolor * NDotL, 4.0f);
	float shadow = 1.0f;
	int slot = LightShadowSlot(light);
	if (slot >= 0) {
		shadow = PointShadowPCF(position - light.position, slot);
	}
	return finalColor * Attn * shadow;
}

#endif
