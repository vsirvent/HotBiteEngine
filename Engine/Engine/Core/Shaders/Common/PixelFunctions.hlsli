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

#ifndef __PIXEL_FUNCTIONS_INCLUDED__
#define __PIXEL_FUNCTIONS_INCLUDED__


#include "PixelCommon.hlsli"
#include "FastNoise.hlsli"
#include "QuickNoise.hlsli"
#include "NoiseSimplex.hlsli"
#include "RGBANoise.hlsli"
#include "MultiTexture.hlsli"

Texture2D diffuseTexture;
Texture2D normalTexture;
Texture2D specularTexture;
Texture2D aoTexture;
Texture2D armTexture;
Texture2D emissionTexture;
Texture2D opacityTexture;
Texture2D meshNormalTexture;
Texture2D highTexture;

Texture2D<float> depthTexture;
Texture2DArray<float> DirShadowMapTexture[MAX_LIGHTS];
Texture2D<float> DirStaticShadowMapTexture[MAX_LIGHTS];
TextureCube<float> PointShadowMapTexture[MAX_LIGHTS];

//Packed array
static float2 lps[MAX_LIGHTS] = (float2[MAX_LIGHTS])LightPerspectiveValues;

#define max_nsteps 100

float CloudPCF(float4 position, DirLight light, float cloud_density)
{
	float3 tolight = normalize(light.DirToLight);
    float3 sky_pos = position.xyz;
	sky_pos += tolight * ((1000.0f - position.y) / tolight.y);
	float f0 = 0.001f;
	float f1 = 0.005f;
	float f2 = 0.01f;
	float f3 = 0.03f;
	float w0 = 0.4f;
	float w1 = 0.3f;
	float w2 = 0.2f;
	float w3 = 0.1f;
	float t0 = time * 0.05f;
	float t1 = time * 0.1f;
	float t2 = time * 0.2f;
	float t3 = time * 0.3f;
		// Apply textures
	float3 p0 = sky_pos.xyz * f0;
	float3 p1 = sky_pos.xyz * f1;
	float3 p2 = sky_pos.xyz * f2;
	float3 p3 = sky_pos.xyz * f3;
	float n = rgba_fnoise(float3(p0.x - t0, p0.y - t0 / 5.0f, p0.z + t0 / 5.0f)) * w0;
	n += rgba_tnoise(float3(p1.x - t1, p1.y - t1 / 5.0f, p1.z + t1 / 5.0f)) * w1;
	n += rgba_tnoise(float3(p2.x - t2, p2.y - t2 / 5.0f, p2.z + t2 / 5.0f)) * w2;
	n += rgba_tnoise(float3(p3.x - t3, p3.y - t3 / 5.0f, p3.z + t3 / 5.0f)) * w3;
	n = saturate(n * 3.0f - 1.5f + (cloud_density - 1.0f));
	return n;
}

//A directional shadow map only covers a slice of the world: an XY footprint, and the
//near/far slab its depth encodes. Outside that the map holds nothing to test against,
//and the only sane answer is "lit". Sampling anyway does not fail quietly - the
//comparison sampler clamps to an edge texel in XY, and a p.z past the far plane
//compares greater than every stored depth - so both read as *occluded*, which is what
//a camera flying out of the map looked like: the world going dark. The static-caster
//variants have always returned lit here; the dynamic ones returned 0.5f, half-darkening
//everything beyond the footprint, and the slow path did not check at all.
#ifndef __DIR_SHADOW_FOOTPRINT__
#define __DIR_SHADOW_FOOTPRINT__
bool OutsideShadowMap(float3 p)
{
	return p.x < 0.0f || p.x > 1.0f ||
	       p.y < 0.0f || p.y > 1.0f ||
	       p.z < 0.0f || p.z > 1.0f;
}

//Which cascade of light `index` covers this world position, and where in that slice
//it lands. Cascades are fitted innermost first, so the first one that contains the
//point is also the highest-density one that does - which is the whole objective, and
//it needs no split distances uploaded alongside: containment in the map footprint is
//the same test, evaluated against the matrix that was actually rendered with.
//
//Returns -1 when no cascade covers the point, which is the ordinary case beyond the
//shadow distance and must read as lit, never as shadowed. Slices at or past
//cascade_count are never touched: their matrices are zero and every point would
//"land" dead centre in a map holding nothing.
int SelectDirCascade(float4 position, DirLight light, int index, out float3 uvz)
{
	uvz = float3(0.0f, 0.0f, 0.0f);
	for (int c = 0; c < light.cascade_count; ++c) {
		float4 p = mul(position, DirPerspectiveMatrix[index * MAX_SHADOW_CASCADES + c]);
		float3 t = float3((p.x + 1.0f) * 0.5f, 1.0f - ((p.y + 1.0f) * 0.5f), p.z);
		if (!OutsideShadowMap(t)) {
			uvz = t;
			return c;
		}
	}
	return -1;
}

//Static casters have one plain map per light, not a cascade set - it is fitted to the
//widest cascade, so a single lookup covers everything the cascades do. There is
//nothing to select between; this returns false when the point is off the map, which is
//the same "no data, read as lit" case a cascade miss is.
bool ProjectDirStatic(float4 position, DirLight light, int index, out float3 uvz)
{
	float4 p = mul(position, DirStaticPerspectiveMatrix[index]);
	uvz = float3((p.x + 1.0f) * 0.5f, 1.0f - ((p.y + 1.0f) * 0.5f), p.z);
	return !OutsideShadowMap(uvz);
}

//Debug view: recolour the light's contribution by which cascade shaded this pixel.
//This is the visualization that actually answers where the cascade boundaries fall,
//because it is evaluated per pixel through the same selection the shadow lookup used -
//a wireframe of the cascade volumes cannot show it, since those volumes are always
//wrapped around the viewer.
//
//Saturated hues, because the tint recolours only the *directional* term: a scene with
//strong ambient, fog or point lights dilutes it towards white in the final pixel, and
//a pastel band washes out to nothing exactly where you most need to read it. Mirrored
//by CASCADE_COLOR in Tools/SceneEditor/ShadowDebug.cpp, which draws the legend - a
//legend that disagrees with the tint is worse than no legend.
static const float3 CASCADE_DEBUG_COLOR[MAX_SHADOW_CASCADES] = {
	float3(0.15f, 1.00f, 0.25f), //0 - nearest, tightest
	float3(1.00f, 0.85f, 0.10f),
	float3(1.00f, 0.35f, 0.05f),
	float3(1.00f, 0.15f, 0.75f), //3 - farthest, coarsest
};
//Past the last cascade there is no shadow data and the pixel is left lit. Shown as a
//cold blue so "outside the shadow distance" reads as its own state rather than being
//mistaken for the last cascade.
static const float3 CASCADE_DEBUG_OUTSIDE = float3(0.20f, 0.35f, 1.00f);
//Added on top of the shaded result so the cascade stays legible where the light
//contributes nothing. Shadowed ground and faces turned away are precisely where the
//cascade needs reading - a pure multiply leaves them black, which is the one place a
//cascade debug view must not go dark.
#define CASCADE_DEBUG_FLOOR 0.15f

float3 ApplyDirCascadeDebug(float3 lit, float4 position, DirLight light, int index)
{
	if (!(light.flags & DIR_LIGHT_FLAG_DEBUG_CASCADES)) {
		return lit;
	}
	float3 uvz = float3(0.0f, 0.0f, 0.0f);
	int cascade = SelectDirCascade(position, light, index, uvz);
	float3 tint = (cascade < 0) ? CASCADE_DEBUG_OUTSIDE : CASCADE_DEBUG_COLOR[cascade];
	//Keeps the shading - shadow edges and normals still read - while forcing the hue.
	return tint * (CASCADE_DEBUG_FLOOR + lit);
}
#endif

//Percentage-closer filtering, and the one detail that decides whether a shadow edge
//looks smooth or looks like a staircase: *which* comparison instruction is used.
//
//GatherCmp is the "compare four values at once" instruction, and this code used to be
//built on it - 121 of them, stepping half a texel over a 5-texel box. That is exactly
//what produced the stepping. Gather returns the four *raw* comparison results and
//deliberately bypasses the sampler's filter, so no matter how many of them are averaged
//the shadow term can still only change at texel boundaries. Those boundaries are
//straight lines in shadow-map space, which is what a staircase on screen is.
//
//SampleCmpLevelZero is the one that fixes it. With the COMPARISON_MIN_MAG_MIP_LINEAR
//sampler the engine already creates (DXCore.cpp), the hardware compares the 2x2
//neighbourhood and bilinearly *blends* the four results, so the term varies
//continuously *within* a texel and the edge stops snapping to the grid. A square grid
//of those one texel apart is a real PCF kernel: 5x5 = 25 taps here, which looks
//considerably smoother than the 121 gathers it replaces and costs about a fifth as
//much.
//
//Kernel size is DIR_PCF_RADIUS in Defines.hlsli.
float DirShadowPCF(float4 position, DirLight light, int index)
{
	float3 p = float3(0.0f, 0.0f, 0.0f);
	int cascade = SelectDirCascade(position, light, index, p);
	if (cascade < 0) {
		return 1.0f;
	}
	float w;
	float h;
	float elements;
	DirShadowMapTexture[index].GetDimensions(w, h, elements);
	float2 texel = 1.0f / float2(w, h);

	float att1 = 0.0f;
	[unroll]
	for (int y = -DIR_PCF_RADIUS; y <= DIR_PCF_RADIUS; ++y) {
		[unroll]
		for (int x = -DIR_PCF_RADIUS; x <= DIR_PCF_RADIUS; ++x) {
			float2 o = float2(x, y) * texel;
			att1 += DirShadowMapTexture[index].SampleCmpLevelZero(
				PCFSampler, float3(p.xy + o, cascade), p.z).r;
		}
	}
	return saturate(att1 * DIR_PCF_WEIGHT);
}

//Static casters are rendered into their own map on a slow refresh cycle, under the
//light's view matrix as it stood at that refresh - hence DirStaticPerspectiveMatrix
//rather than DirPerspectiveMatrix. Outside the map footprint we return 1.0f (lit):
//this result is min()'d with the dynamic one, so "no data here" must not darken.
float DirStaticShadowPCF(float4 position, DirLight light, int index)
{
	if (!(light.flags & DIR_LIGHT_FLAG_STATIC_SHADOW)) {
		return 1.0f;
	}
	float3 p = float3(0.0f, 0.0f, 0.0f);
	if (!ProjectDirStatic(position, light, index, p)) {
		return 1.0f;
	}
	float w;
	float h;
	DirStaticShadowMapTexture[index].GetDimensions(w, h);
	float2 texel = 1.0f / float2(w, h);

	float att1 = 0.0f;
	[unroll]
	for (int y = -DIR_PCF_RADIUS; y <= DIR_PCF_RADIUS; ++y) {
		[unroll]
		for (int x = -DIR_PCF_RADIUS; x <= DIR_PCF_RADIUS; ++x) {
			float2 o = float2(x, y) * texel;
			att1 += DirStaticShadowMapTexture[index].SampleCmpLevelZero(
				PCFSampler, p.xy + o, p.z).r;
		}
	}
	return saturate(att1 * DIR_PCF_WEIGHT);
}

//Combined attenuation of both caster sets. Shadowing is occlusion, so the darker of
//the two wins.
float DirShadowPCFAll(float4 position, DirLight light, int index)
{
	return min(DirShadowPCF(position, light, index),
	           DirStaticShadowPCF(position, light, index));
}

//Debug view for the static caster map: what it reaches, and what it shadows.
//
//The static map is one plain map fitted to the widest cascade and re-rendered only
//rarely, so the question it raises is not "which slice" but "does it still cover the
//view at all" - walk far enough and its footprint is behind you, at which point every
//piece of scenery silently stops casting. Red is exactly that state, and seeing where
//the red begins *is* seeing the map's extent.
static const float3 STATIC_DEBUG_OUTSIDE = float3(1.00f, 0.12f, 0.12f); //not covered
static const float3 STATIC_DEBUG_SHADOWED = float3(0.20f, 0.45f, 1.00f); //static caster occludes
static const float3 STATIC_DEBUG_LIT = float3(0.25f, 1.00f, 0.45f);      //covered, unoccluded

float3 ApplyDirStaticDebug(float3 lit, float4 position, DirLight light, int index)
{
	if (!(light.flags & DIR_LIGHT_FLAG_DEBUG_STATIC)) {
		return lit;
	}
	float3 uvz = float3(0.0f, 0.0f, 0.0f);
	if (!(light.flags & DIR_LIGHT_FLAG_STATIC_SHADOW) ||
		!ProjectDirStatic(position, light, index, uvz)) {
		return STATIC_DEBUG_OUTSIDE * (CASCADE_DEBUG_FLOOR + lit);
	}
	float s = DirStaticShadowPCF(position, light, index);
	float3 tint = lerp(STATIC_DEBUG_SHADOWED, STATIC_DEBUG_LIT, saturate(s));
	return tint * (CASCADE_DEBUG_FLOOR + lit);
}

//The one place the two debug views are combined, so callers apply a single call and
//neither view has to know about the other.
float3 ApplyDirShadowDebug(float3 lit, float4 position, DirLight light, int index)
{
	if (light.flags & DIR_LIGHT_FLAG_DEBUG_STATIC) {
		return ApplyDirStaticDebug(lit, position, light, index);
	}
	return ApplyDirCascadeDebug(lit, position, light, index);
}

float DirShadowPCFFAST(float4 position, DirLight light, int index)
{
	float3 p = float3(0.0f, 0.0f, 0.0f);
	int cascade = SelectDirCascade(position, light, index, p);
	if (cascade < 0) {
		return 1.0f;
	}
	//Single tap, but SampleCmpLevelZero rather than GatherCmp: same one instruction,
	//and the hardware blends its 2x2 comparison instead of box-averaging it, so even
	//the cheap path gets a sub-texel gradient rather than a hard texel edge.
	return saturate(DirShadowMapTexture[index].SampleCmpLevelZero(
		PCFSampler, float3(p.xy, cascade), p.z).r);
}

float DirStaticShadowPCFFAST(float4 position, DirLight light, int index)
{
	if (!(light.flags & DIR_LIGHT_FLAG_STATIC_SHADOW)) {
		return 1.0f;
	}
	float3 p = float3(0.0f, 0.0f, 0.0f);
	if (!ProjectDirStatic(position, light, index, p)) {
		return 1.0f;
	}
	return saturate(DirStaticShadowMapTexture[index].SampleCmpLevelZero(
		PCFSampler, p.xy, p.z).r);
}

float DirShadowPCFFASTAll(float4 position, DirLight light, int index)
{
	return min(DirShadowPCFFAST(position, light, index),
	           DirStaticShadowPCFFAST(position, light, index));
}

float PointShadowPCF(float3 ToPixel, PointLight light, int index)
{
	float3 ToPixelAbs = abs(ToPixel);
	float Z = max(ToPixelAbs.x, max(ToPixelAbs.y, ToPixelAbs.z));
	float d = ((lps[index].x * Z + lps[index].y) / Z);
	//This offset allows to avoid self shadow
	float step = 0.02f;
	float att1 = 0.0f;
	float count = 0.00001f;
	d -= 0.0001f;
	for (float x = -0.06f; x < 0.06f; x += step) {
		for (float y = -0.06f; y < 0.06f; y += step) {
			att1 += round(PointShadowMapTexture[index].SampleCmpLevelZero(PCFSampler, float3(ToPixel.x + x, ToPixel.y + y, ToPixel.z), d).r);
			count += 1.0f;
		}
	}
	att1 /= count;
    float4 val = PointShadowMapTexture[index].GatherCmp(PCFSampler, float3(ToPixel.x, ToPixel.y, ToPixel.z), d);
    att1 = dot(val, float4(0.25, 0.25, 0.25, 0.25));
	return saturate(att1);
}

float PointShadowPCFFast(float3 ToPixel, PointLight light, int index)
{
	float3 ToPixelAbs = abs(ToPixel);
	float Z = max(ToPixelAbs.x, max(ToPixelAbs.y, ToPixelAbs.z));
	float d = (lps[index].x * Z + lps[index].y) / Z;
	//This offset allows to avoid self shadow
	d -= 0.0001f;
	float att1 = round(PointShadowMapTexture[index].SampleCmpLevelZero(PCFSampler, float3(ToPixel.x, ToPixel.y, ToPixel.z), d).r);
	return saturate(att1);
}

float3 CalcAmbient(float3 normal)
{
	// Convert from [-1, 1] to [0, 1]
	float up = normal.y * 0.5 + 0.5;
	// Calculate the ambient value
	float3 Ambient = ambientLight.AmbientDown + up * ambientLight.AmbientUp;
	// Apply the ambient value to the color
	return Ambient;
}

float3 CalcDirectional(float3 normal, float4 position, float2 uv, MaterialColor material, DirLight light, float cloud_density, int index, inout float4 bloom)
{
	float3 spec_intensity = material.specIntensity;
	if (material.flags & SPECULAR_MAP_ENABLED_FLAG) {
		spec_intensity *= PF_SAMPLE(specularTexture, basicSampler, uv).r;
	}
	else if (material.flags & ARM_MAP_ENABLED_FLAG) {
		spec_intensity *= PF_SAMPLE(armTexture, basicSampler, uv).g;
	}
	float3 color = light.Color.rgb * light.intensity;
	// Phong diffuse
	float NDotL = dot(light.DirToLight, normal);
	float3 finalColor = color * saturate(NDotL);
	float3 bloomColor = { 0.f, 0.f, 0.f };
	//Back reflex
	float NDotL2 = dot(-light.DirToLight, normal);
	finalColor += color * saturate(NDotL2) * 0.2f;

	// Blinn specular
#if 1
	
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
#endif
	float shadow = 1.0f;
	if (light.cast_shadow) {
		shadow = (DirShadowPCFAll(position, light, index));
		if (cloud_density > 0.0f) {
			shadow -= CloudPCF(position, light, cloud_density);
		}
		shadow = saturate(shadow);
	}
	float3 final = ApplyDirShadowDebug(finalColor * shadow, position, light, index);
	bloom.rgb += bloomColor * shadow * material.bloom_scale;;
	return final;
}

float3 CalcDirectionalWithoutNormal(float4 position, MaterialColor material, DirLight light, float cloud_density, int index, inout float4 bloom)
{
	float3 color = light.Color.rgb * light.intensity;
	float3 finalColor = color;
	float3 bloomColor = { 0.f, 0.f, 0.f };

	float shadow = 1.0f;
	if (light.cast_shadow) {
		shadow = (DirShadowPCFAll(position, light, index));
		if (cloud_density > 0.0f) {
			shadow -= CloudPCF(position, light, cloud_density);
		}
		shadow = saturate(shadow);
	}
	float3 final = ApplyDirShadowDebug(finalColor * shadow, position, light, index);
	bloom.rgb += bloomColor * shadow * material.bloom_scale;
	return final;
}

float3 DirVolumetricLight(float4 position, DirLight light, int index, float time, float cloud_density) {

	float3 lcolor = light.Color.rgb * light.intensity;
	float3 color = { 0.f, 0.f, 0.f };

	float step = 0.5f;
	float max_vol = 1.0f;

	float3 ToEye = cameraPosition.xyz - position.xyz;
	float ToEyeDist = length(ToEye);
	int nsteps = ToEyeDist / step;
	float3 ToEyeRayUnit = ToEye / nsteps;

	float3 camDir = cameraDirection.xyz;
	float angle_extra = saturate(pow(dot(normalize(camDir), normalize(light.DirToLight)), 3.0f)) * 2.0f;
	float density_step = light.density * step;
	float shadow, fog, att;
	int apply_fog = light.flags & DIR_LIGHT_FLAG_FOG;
	float3 step_color = (lcolor * density_step);	
	float3 step_color2 = step_color;
	
	//fog height
	float max_h = 80.0f;

	float3 ToLight = { 0.0f, 0.0f, 0.0f };
	float DistToLight = 0.0f;
	float LightRange = 0.0f;
	float density_y = 1.0f;
	if (light.range > 0.0f) {
		ToLight = light.position - position.xyz;
		ToLight *= -light.DirToLight;
		DistToLight = length(ToLight);
		LightRange = (light.range - DistToLight) / light.range;
		density_y = pow(saturate(LightRange), 2.0f);
	}
	if (density_y > 0.0f) {

		//End if maximum volume achieved
		float mag = 0.0f;
		float n = 1.0f;

		if (nsteps > max_nsteps) {
			int diff = nsteps - max_nsteps;
			position.xyz += ToEyeRayUnit * diff;
			nsteps = max_nsteps;
		}
		
		for (int i = 0; i < nsteps; ++i) {			
			shadow = 1.0f;
			fog = 1.0f;
			att = 1.0f;
			position.xyz += ToEyeRayUnit;
			if (apply_fog && max_h < position.y) {
				if (ToEyeRayUnit.y < 0.0f) {
					continue;
				}
				else {
					break;
				}
			}
			
			if (light.range > 0.0f) {
				float3 ToLight = light.position - position.xyz;
				ToLight *= -light.DirToLight;
				float DistToLight = length(ToLight);				
				float LightRange = (light.range - DistToLight) / light.range;
				att *= pow(saturate(LightRange), 2.0f);
				if (att == 0.0f) {
					break;
				}
			}
			if (apply_fog != 0) {
				float3 p = float3(position.x + time*0.5, position.y, position.z + time * 0.3f);
				float3 p2 = float3(position.x + time, position.y, position.z + time * 0.8f);
				float n = rgba_tnoise(p) * 0.5f +
					      rgba_tnoise(p*2.0f) * 0.3f +
						  rgba_tnoise(p2*4.0f) * 0.2f;
				n *= pow(saturate((max_h - position.y) / max_h), 5.0f);
				fog = clamp(30.0f * (n - 0.2f), -1.0f, 30.0f);
			}
			if (light.cast_shadow) {
				shadow = DirShadowPCFFASTAll(position, light, index);
				if (cloud_density > 0.0f) {
					shadow -= CloudPCF(position, light, cloud_density);
				}
				shadow = saturate(shadow);
			}
			att *= shadow;
			color += step_color * clamp(att * fog, 0.0f, 30.0f);
			mag = max(color.r, max(color.g, color.b));
			if (mag > max_vol) {
				break;
			}
		}
	}

	return color;
}

float3 VolumetricLight(float3 position, PointLight light, int index) {
	float3 color = { 0.f, 0.f, 0.f };
	float3 step_color = light.Color.rgb;
	float step = 0.5f;
	float max_vol = 0.3f;
	step_color.rgb *= (light.density * step);
	float3 intersection_position;

	if (line_sphere_intersection(position, cameraPosition.xyz, light.Position, light.Range, intersection_position)) {
		position = intersection_position;
		float3 lposition = light.Position;
		float3 ToEye = cameraPosition.xyz - position;
		float ToEyeDist = length(ToEye);
		int nsteps = ToEyeDist / step;
		float3 ToEyeRayUnit = ToEye / nsteps;
		float extra_step_w = ((ToEyeDist / step) - (float)nsteps);
		float mag = 0.0f;
		
		if (nsteps > max_nsteps) {
			int diff = nsteps - max_nsteps;
			position += ToEyeRayUnit * diff;
			nsteps = max_nsteps;
		}
		for (int i = 0; i <= nsteps; ++i) {
			position += ToEyeRayUnit;
			float3 ToLight = position - lposition;
			float ToLightDist = length(ToLight);
			if (ToLightDist > light.Range) {
				break;
			}
			float LightRange = (light.Range - ToLightDist) / light.Range;
			float DistToLightNorm = saturate(LightRange * LightRange);
			float shadow = 1.0f;
			if (light.cast_shadow) {
				shadow = PointShadowPCFFast(ToLight, light, index);
			}
			float att = DistToLightNorm;
			att *= shadow;
			saturate(att);
			color += step_color * att;
			mag = max(color.r, max(color.g, color.b));
			if (mag > max_vol) {
				break;
			}
		}
	}
	return color;
}

float3 CalcPoint(float3 normal, float3 position, float2 uv, MaterialColor material, PointLight light, int index, inout float4 bloom)
{
	float3 finalColor = { 0.f, 0.f, 0.f };
	float3 bloomColor = { 0.f, 0.f, 0.f };
	float3 lposition = light.Position;
	float lum = (light.Color.r + light.Color.g + light.Color.b) / 3.0f;
	float3 ToLight = lposition - position;
	float3 ToEye = cameraPosition.xyz - position;
	float DistToLight = length(ToLight);
	// Phong diffuse
	ToLight /= DistToLight; // Normalize
	float NDotL = saturate(dot(ToLight, normal));
	float NDotL2 = saturate(dot(-ToLight, normal));
	finalColor = light.Color * NDotL;
	//Back reflex
	finalColor += light.Color.rgb * saturate(NDotL2) * 0.2f;

	// Blinn specular
#if 1
	float3 spec_intensity = material.specIntensity;
	if (material.flags & SPECULAR_MAP_ENABLED_FLAG) {
		spec_intensity *= PF_SAMPLE(specularTexture, basicSampler, uv).r;
	}
	else if (material.flags & ARM_MAP_ENABLED_FLAG) {
		spec_intensity *= PF_SAMPLE(armTexture, basicSampler, uv).g;
	}
	ToEye = normalize(ToEye);
	ToLight = normalize(ToLight);
	float3 HalfWay = normalize(ToEye + ToLight);
	float NDotH = saturate(dot(HalfWay, normal));
	float3 spec_color = { 0.f, 0.f, 0.f };
	spec_color += pow(NDotH, 200.0f) * spec_intensity * 0.5f;
	spec_color += light.Color * pow(NDotH, 100.0f) * spec_intensity;
	spec_color += light.Color * pow(NDotH, 2.0f) * spec_intensity * 0.1f;
	finalColor += spec_color;
	bloomColor += spec_color;
#endif
	// Attenuation
	float LightRange = (light.Range - DistToLight) / light.Range;
	float DistToLightNorm = saturate(LightRange);
	float Attn = saturate(DistToLightNorm * DistToLightNorm);

	float shadow = 1.0f;
	if (light.cast_shadow) {
		shadow = PointShadowPCF(position - light.Position, light, index);
	}
	finalColor *= Attn * shadow;
	bloomColor *= Attn * shadow;
	bloom.rgb += bloomColor * material.bloom_scale;
	return finalColor;
}

float3 CalcPointWithoutNormal(float3 position, MaterialColor material, PointLight light, int index, inout float4 bloom)
{
	float3 finalColor = { 0.f, 0.f, 0.f };
	float3 bloomColor = { 0.f, 0.f, 0.f };
	float3 lposition = light.Position;
	float lum = (light.Color.r + light.Color.g + light.Color.b) / 3.0f;
	float3 ToLight = lposition - position;
	float3 ToEye = cameraPosition.xyz - position;
	float DistToLight = length(ToLight);
	// Phong diffuse
	ToLight /= DistToLight; // Normalize
	finalColor = light.Color;

	// Attenuation
	float LightRange = (light.Range - DistToLight) / light.Range;
	float DistToLightNorm = saturate(LightRange);
	float Attn = saturate(DistToLightNorm * DistToLightNorm);

	float shadow = 1.0f;
	if (light.cast_shadow) {
		shadow = PointShadowPCF(position - light.Position, light, index);
	}
	finalColor *= Attn * shadow;
	bloomColor *= Attn * shadow;
	bloom.rgb += bloomColor * material.bloom_scale;
	return finalColor;
}

float3 EmitPoint(float3 position, matrix worldViewProj, PointLight light)
{
	float3 lposition = mul(float4(light.Position, 1.0f), worldViewProj).xyz;
	lposition.x /= lposition.z;
	lposition.y /= -lposition.z;
	lposition.x = (lposition.x + 1.0f) * screenW / 2.0f;
	lposition.y = (lposition.y + 1.0f) * screenH / 2.0f;
	float2 ToLight = lposition.xy - position.xy;
	float DistToLight = length(ToLight);
	//Light emission
	float DistLightToPixel = 1.0 - saturate(DistToLight * 0.01f);
	float DistLightToPixel2 = 1.0 - saturate(DistToLight * 0.1f);
	float3 finalColor = light.Color * pow(DistLightToPixel, 10.0f);
	finalColor.rgb += pow(DistLightToPixel2, 10.0f);

	return finalColor;
}

float GetHeight(float2 pos, const float calculated_values[MAX_MULTI_TEXTURE]) {
	float h = 0.0f;
	if (multi_texture_count > 0) {
		h = getMutliTextureValueLevel(basicSampler, 2, MULTITEXT_DISP, multi_texture_count, multi_texture_operations,
			calculated_values, multi_texture_uv_scales, pos, multi_highTexture).r;
	}
	else {
		h = highTexture.SampleLevel(basicSampler, pos, 0).r;
	}
	return h;
}

float2 CalculateDepthUVBinary(float scale, float base_steps, float angle_steps, float2 uv, float3 camera_pos,
	float3 fragment_pos, const float calculated_values[MAX_MULTI_TEXTURE], out float h, out float3 displacement) {

	float3 ToCam = camera_pos - fragment_pos;
	float dist = length(ToCam);
	float angle = dot(normalize(ToCam), float3(0.0f, 0.0f, 1.0f));
	float nsteps = base_steps + angle_steps * saturate(1.0f - angle) + clamp(lerp(base_steps, 0.0f, dist / 50.0f), 0.0, base_steps);
	int insteps = (int)nsteps;
	
	displacement = float3(0.0f, 0.0f, 0.0f);
	ToCam.xy *= scale * angle;
	ToCam = normalize(ToCam);
	ToCam.z = abs(ToCam.z);
	float3 ToCam2 = ToCam / ToCam.z;
	float3 curr_uvw = float3(uv, 0.0f) + ToCam2;
	int steps = 0;
	bool binary_enabled = false;
	float step_delta = 0.03f;
	int max_linear_steps = 20;
	int linear_steps = 0;
	while (steps <= insteps) {		
		h = GetHeight(curr_uvw.xy, calculated_values).r;
		if (abs(h - curr_uvw.z) < 0.005f)
		{
			break;
		}
		if (linear_steps++ > max_linear_steps) {
			binary_enabled = true;
		}
		if (h >= curr_uvw.z) {
			curr_uvw += ToCam * step_delta;
			displacement += ToCam * step_delta;
			binary_enabled = true;
		}
		else {
			curr_uvw -= ToCam * step_delta;
			displacement -= ToCam * step_delta;
		}
		if (binary_enabled) {
			steps++;
			step_delta *= 0.5f;
		}
	}
	displacement.xy /= scale;
	return curr_uvw.xy;
}

#endif