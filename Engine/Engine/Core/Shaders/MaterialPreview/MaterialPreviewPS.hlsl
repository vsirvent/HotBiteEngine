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

/*
Pixel stage of the Scene Editor's material thumbnail pass.

This is a *representative* shading of a material, not a preview of how the
material will actually look in the level. It models the maps and constants that
identify a material at a glance - albedo, normal, specular/ARM, AO, emission and
opacity - under a three-light studio rig, and deliberately models nothing
that depends on the scene: no shadows, no GI or ray-traced reflection, no
parallax or tessellation, no fog. Two materials that differ only in those
scene-dependent properties therefore produce identical thumbnails, which is the
accepted trade for a thumbnail that costs one small draw and never has to be
re-rendered when the level changes.

`material` is bound with the same MaterialColor layout the main render path uses
(PixelCommon.hlsli), so it stays in step with Core::MaterialProps for free.

The rig's three directions arrive as constants rather than being fixed here,
because they are anchored to the camera and the model viewport's camera orbits -
see PreviewPass::MakeLightRig, which is what both previews build them with. Their
*colours* stay fixed below: which light is the warm key and which is the cool fill
is the look of the rig, not something a caller chooses.
*/

#include "../Common/PixelCommon.hlsli"

cbuffer externalData : register(b0)
{
	MaterialColor material;
	float3 cameraPosition;
	float  padding;
	//Directions from the surface *towards* each light, in world space, normalized.
	float3 keyDir;
	float  keyPadding;
	float3 fillDir;
	float  fillPadding;
	float3 backDir;
	float  backPadding;
};

Texture2D diffuseTexture   : register(t0);
Texture2D normalTexture    : register(t1);
Texture2D specularTexture  : register(t2);
Texture2D aoTexture        : register(t3);
Texture2D armTexture       : register(t4);
Texture2D emissionTexture  : register(t5);
Texture2D opacityTexture   : register(t6);

struct PreviewVertexToPixel
{
	float4 position   : SV_POSITION;
	float3 worldPos   : POSITION;
	float3 normal     : NORMAL;
	float2 uv         : TEXCOORD;
	float3 tangent    : POSITION1;
	float3 bitangent  : POSITION2;
};

//A studio rig: a warm key, a cool fill that keeps the terminator readable, and a
//dim back light that separates the subject from the panel background. Where each
//one sits is the caller's (see the cbuffer above).
static const float3 KEY_COLOR  = float3(1.0f, 0.96f, 0.90f) * 1.10f;
static const float3 FILL_COLOR = float3(0.55f, 0.62f, 0.80f) * 0.40f;
static const float3 BACK_COLOR = float3(0.60f, 0.60f, 0.70f) * 0.25f;

float4 main(PreviewVertexToPixel input) : SV_TARGET
{
	float3 normal = normalize(input.normal);

	//Normal map, in the same tangent-space convention as the main render path.
	if (material.flags & NORMAL_MAP_ENABLED_FLAG) {
		float3 sampled = normalTexture.Sample(basicSampler, input.uv).rgb * 2.0f - 1.0f;
		float3x3 tbn = float3x3(normalize(input.tangent), normalize(input.bitangent), normal);
		normal = normalize(mul(sampled, tbn));
	}

	float4 albedo = material.diffuseColor;
	if (material.flags & DIFFUSSE_MAP_ENABLED_FLAG) {
		albedo *= diffuseTexture.Sample(basicSampler, input.uv);
	}

	//Specular intensity: an ARM map's blue channel (metalness) and green channel
	//(roughness) take priority when present, otherwise a dedicated specular map,
	//otherwise the material constant alone.
	float spec_intensity = material.specIntensity;
	float roughness = 0.5f;
	if (material.flags & ARM_MAP_ENABLED_FLAG) {
		float3 arm = armTexture.Sample(basicSampler, input.uv).rgb;
		roughness = arm.g;
		spec_intensity *= saturate(arm.b + 0.25f);
	}
	else if (material.flags & SPECULAR_MAP_ENABLED_FLAG) {
		spec_intensity *= specularTexture.Sample(basicSampler, input.uv).r;
	}

	float ao = 1.0f;
	if (material.flags & AO_MAP_ENABLED_FLAG) {
		ao = aoTexture.Sample(basicSampler, input.uv).r;
	}
	else if (material.flags & ARM_MAP_ENABLED_FLAG) {
		ao = armTexture.Sample(basicSampler, input.uv).r;
	}

	float3 view_dir = normalize(cameraPosition - input.worldPos);
	//Roughness drives the highlight tightness over a range that stays visible at
	//thumbnail size - a physically-derived exponent collapses to a single pixel.
	float shininess = lerp(120.0f, 8.0f, saturate(roughness));

	float3 lit = float3(0.0f, 0.0f, 0.0f);
	float3 light_dirs[3] = { keyDir, fillDir, backDir };
	float3 light_colors[3] = { KEY_COLOR, FILL_COLOR, BACK_COLOR };
	[unroll]
	for (int i = 0; i < 3; ++i) {
		float ndotl = saturate(dot(normal, light_dirs[i]));
		float3 half_vector = normalize(light_dirs[i] + view_dir);
		float specular = pow(saturate(dot(normal, half_vector)), shininess) * spec_intensity;
		lit += light_colors[i] * (albedo.rgb * ndotl + specular * ndotl);
	}

	//A little sky/ground ambient so surfaces facing away from every light still
	//read as material rather than as black silhouette.
	float3 ambient = lerp(float3(0.10f, 0.10f, 0.13f), float3(0.22f, 0.24f, 0.28f),
	                      normal.y * 0.5f + 0.5f);
	lit += albedo.rgb * ambient;
	lit *= ao;

	if (material.emission > 0.0f) {
		float3 emissive = material.emission_color;
		if (material.flags & EMISSION_MAP_ENABLED_FLAG) {
			emissive *= emissionTexture.Sample(basicSampler, input.uv).rgb;
		}
		//Emission is authored on an HDR scale that the main path resolves through
		//bloom and tone mapping; the thumbnail has neither, so compress it into
		//the visible range instead of blowing the sphere out to white.
		lit += emissive * saturate(material.emission / (material.emission + 1.0f));
	}

	float alpha = material.opacity * albedo.a;
	if (material.flags & OPACITY_MAP_ENABLED_FLAG) {
		alpha *= opacityTexture.Sample(basicSampler, input.uv).r;
	}

	return float4(saturate(lit), saturate(alpha));
}
