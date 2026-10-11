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

//Shadow lookups at full quality: cascaded directional maps (dynamic and static casters),
//point light cube maps, and the cloud shadow. These used to live in PixelFunctions.hlsli
//next to the lighting that called them, which every material shader included and which
//therefore dragged the light constant buffers into the G-buffer pass. They are here so
//that only what really shades with shadows - DeferredLightCS and the volumetric march -
//includes them.
//
//The includer supplies, as constants: DirPerspectiveMatrix, DirStaticPerspectiveMatrix,
//LightPerspectiveValues and `time`; and PixelCommon.hlsli (PCFSampler, basicSampler,
//DirLight) before this file.

#ifndef __LIGHT_SHADOWS_HLSLI__
#define __LIGHT_SHADOWS_HLSLI__

#include "RGBANoise.hlsli"

Texture2DArray<float> DirShadowMapTexture[MAX_LIGHTS];
Texture2D<float> DirStaticShadowMapTexture[MAX_LIGHTS];
TextureCube<float> PointShadowMapTexture[MAX_LIGHTS];

//Packed array
static float2 lps[MAX_LIGHTS] = (float2[MAX_LIGHTS])LightPerspectiveValues;

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

//Which cube map a light's shadow is read from is DATA - the slot comes out of the packed
//light, not out of a loop counter - and a texture array cannot be indexed by data: fxc
//either unrolls the loop that produced the index (and fails, the loop is over a mask) or
//refuses. So the slot is matched against each of the MAX_LIGHTS maps by a literal index,
//one branch per slot, of which exactly one is taken.
float4 GatherPointShadow(int slot, float3 ToPixel, float d)
{
	float4 result = float4(1.0f, 1.0f, 1.0f, 1.0f);
	[unroll]
	for (int s = 0; s < MAX_LIGHTS; ++s) {
		[branch]
		if (s == slot) {
			result = PointShadowMapTexture[s].GatherCmp(PCFSampler, ToPixel, d);
		}
	}
	return result;
}

float SamplePointShadow(int slot, float3 ToPixel, float d)
{
	float result = 1.0f;
	[unroll]
	for (int s = 0; s < MAX_LIGHTS; ++s) {
		[branch]
		if (s == slot) {
			result = PointShadowMapTexture[s].SampleCmpLevelZero(PCFSampler, ToPixel, d).r;
		}
	}
	return result;
}

//Point light cube map `slot`: a 2x2 comparison blend (GatherCmp, averaged) around the
//texel the pixel falls in. The 3x3 grid of SampleCmpLevelZero taps this used to run first
//was overwritten by the gather before it was ever returned, so it is gone.
float PointShadowPCF(float3 ToPixel, int slot)
{
	float3 ToPixelAbs = abs(ToPixel);
	float Z = max(ToPixelAbs.x, max(ToPixelAbs.y, ToPixelAbs.z));
	float d = ((lps[slot].x * Z + lps[slot].y) / Z);
	//This offset allows to avoid self shadow
	d -= 0.0001f;
	float4 val = GatherPointShadow(slot, ToPixel, d);
	return saturate(dot(val, float4(0.25, 0.25, 0.25, 0.25)));
}

float PointShadowPCFFast(float3 ToPixel, int slot)
{
	float3 ToPixelAbs = abs(ToPixel);
	float Z = max(ToPixelAbs.x, max(ToPixelAbs.y, ToPixelAbs.z));
	float d = (lps[slot].x * Z + lps[slot].y) / Z;
	//This offset allows to avoid self shadow
	d -= 0.0001f;
	float att1 = round(SamplePointShadow(slot, ToPixel, d));
	return saturate(att1);
}

#endif
