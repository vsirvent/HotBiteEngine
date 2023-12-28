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
Texture2D meshNormalTexture;
Texture2D highTexture;

Texture2D<float> depthTexture;
Texture2D<float> DirShadowMapTexture[MAX_LIGHTS];
//Texture2D<float> DirStaticShadowMapTexture[MAX_LIGHTS];
TextureCube<float> PointShadowMapTexture[MAX_LIGHTS];

//Packed array
static float2 lps[MAX_LIGHTS] = (float2[MAX_LIGHTS])LightPerspectiveValues;

#define max_nsteps 20

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

float DirShadowPCF(float4 position, DirLight light, int index)
{
	float4 p = mul(position, DirPerspectiveMatrix[index]);
	p.x = (p.x + 1.0f) / 2.0f;
	p.y = 1.0f - ((p.y + 1.0f) / 2.0f);
	if (p.x < 0.0f || p.x > 1.0f || p.y < 0.0f || p.y > 1.0f) {
		return 0.5f;
	}
	//This code is commented as static shadows are disabled
	//float4 sp = mul(float4(position, 1.0f), DirStaticPerspectiveMatrix[index]);
	//sp.x = (sp.x + 1.0f) / 2.0f;
	//sp.y = 1.0f - ((sp.y + 1.0f) / 2.0f);
	float step = 0.0001f;
	float att1 = 0.0f;
	float count = 0.00001f;
	for (float x = -0.0003f; x < 0.0003f; x += step) {
		for (float y = -0.0003f; y < 0.0003f; y += step) {
			att1 += DirShadowMapTexture[index].SampleCmpLevelZero(PCFSampler, float2(p.x + x, p.y + y), p.z).r;
			count += 0.8f;
		}
	}
	att1 /= count;
	return saturate(att1);
	//float att2 = DirStaticShadowMapTexture[index].SampleCmpLevelZero(PCFSampler, sp.xy, p.z);
	//return saturate(att1*att2);
}

float DirShadowPCFFAST(float4 position, DirLight light, int index)
{
	float4 p = mul(position, DirPerspectiveMatrix[index]);
	p.x = (p.x + 1.0f) / 2.0f;
	p.y = 1.0f - ((p.y + 1.0f) / 2.0f);
	if (p.x < 0.0f || p.x > 1.0f || p.y < 0.0f || p.y > 1.0f) {
		return 0.5f;
	}
	float att1 = DirShadowMapTexture[index].SampleCmpLevelZero(PCFSampler, float2(p.x, p.y), p.z).r;
	return saturate(att1);
}

float PointShadowPCF(float3 ToPixel, PointLight light, int index)
{
	float3 ToPixelAbs = abs(ToPixel);
	float Z = max(ToPixelAbs.x, max(ToPixelAbs.y, ToPixelAbs.z));
	float d = (lps[index].x * Z + lps[index].y) / Z;
	float step = 0.02f;
	float att1 = 0.0f;
	float count = 0.00001f;
	for (float x = -0.06f; x < 0.06f; x += step) {
		for (float y = -0.06f; y < 0.06f; y += step) {
			att1 += PointShadowMapTexture[index].SampleCmpLevelZero(PCFSampler, float3(ToPixel.x + x, ToPixel.y + y, ToPixel.z), d);
			count += 1.0f;
		}
	}
	att1 /= count;
	return saturate(att1);
}

float PointShadowPCFFast(float3 ToPixel, PointLight light, int index)
{
	float3 ToPixelAbs = abs(ToPixel);
	float Z = max(ToPixelAbs.x, max(ToPixelAbs.y, ToPixelAbs.z));
	float d = (lps[index].x * Z + lps[index].y) / Z;
	float att1 = PointShadowMapTexture[index].SampleCmpLevelZero(PCFSampler, float3(ToPixel.x, ToPixel.y, ToPixel.z), d);
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
	float3 spec_intensity = material.specIntensity;
	if (material.flags & SPECULAR_MAP_ENABLED_FLAG) {
		spec_intensity *= specularTexture.Sample(basicSampler, uv).r;
	}
	else if (material.flags & ARM_MAP_ENABLED_FLAG) {
		spec_intensity *= armTexture.Sample(basicSampler, uv).g;
	}
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
		shadow = (DirShadowPCF(position, light, index));
		if (cloud_density > 0.0f) {
			shadow -= CloudPCF(position, light, cloud_density);
		}
		shadow = saturate(shadow);
	}
	float3 final = finalColor * shadow;
	bloom.rgb += bloomColor * shadow * material.bloom_scale;;
	return climit3(final);
}

float3 CalcDirectionalWithoutNormal(float4 position, MaterialColor material, DirLight light, float cloud_density, int index, inout float4 bloom)
{
	float3 color = light.Color.rgb * light.intensity;
	float3 finalColor = color;
	float3 bloomColor = { 0.f, 0.f, 0.f };

	float shadow = 1.0f;
	if (light.cast_shadow) {
		shadow = (DirShadowPCF(position, light, index));
		if (cloud_density > 0.0f) {
			shadow -= CloudPCF(position, light, cloud_density);
		}
		shadow = saturate(shadow);
	}
	float3 final = finalColor * shadow;
	bloom.rgb += bloomColor * shadow * material.bloom_scale;
	return final;
}

float3 DirVolumetricLight(float4 position, DirLight light, int index, float time, float cloud_density) {
	float3 lcolor = light.Color.rgb * light.intensity;

	float3 color = { 0.f, 0.f, 0.f };
	float step = 0.5f;
	float max_vol = 1.0f;
	float3 ToEye = cameraPosition.xyz - position.xyz;
	float3 camDir = cameraDirection.xyz;
	float angle_extra = saturate(pow(dot(normalize(camDir), normalize(light.DirToLight)), 3.0f)) * 2.0f;
	float density_step = light.density * step;	
	float shadow, fog, att;
	int apply_fog = light.flags & DIR_LIGHT_FLAG_FOG;
	float ToLight;
	float DistToLight;
	float LightRange;
	float3 step_color = (lcolor * density_step);	
	float3 step_color2 = step_color * angle_extra;
	float ToEyeDist = length(ToEye);
	int nsteps = ToEyeDist / step;
	float3 ToEyeRayUnit = ToEye / nsteps;
	float density_y = 1.0f;
	float max_h = 80.0f;
	if (light.range > 0.0f) {
		ToLight = light.position - position.xyz;
		ToLight *= -light.DirToLight;
		DistToLight = length(ToLight);
		LightRange = (light.range - DistToLight) / light.range;
		density_y = pow(saturate(LightRange), 2.0f);
	}
	if (density_y > 0.0f) {

		//Part of the last march
		float extra_step_w = ((ToEyeDist / step) - (float)nsteps);

		//We fine check shadow changes
		bool was_light = false;
		float3 ToEyeRayUnitMicroStep = ToEyeRayUnit / 10.0f;

		//End if maximum volume achieved
		float mag = 0.0f;
		float n = 1.0f;

		if (nsteps > max_nsteps) {
			int diff = nsteps - max_nsteps;
			if (diff > 50) {
				diff -= 50;
				if (diff < 500) {
					float diff2 = clamp(diff, 0, 100);
					if (apply_fog != 0 && max_h > position.y) {
						float h_att = saturate((max_h - position.y) / max_h);
						color += step_color2 * h_att * diff2 * 0.5f;
					}
				}
				position.xyz += ToEyeRayUnit * diff;
			}
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
				shadow = DirShadowPCFFAST(position, light, index);
				if (cloud_density > 0.0f) {
					shadow -= CloudPCF(position, light, cloud_density);
				}
				shadow = saturate(shadow);
			}
			att *= shadow;
			color += step_color * clamp(att * fog, 0.0f, 30.0f);
			color += step_color2 * att;
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

	float3 lposition = light.Position;
	float3 ToEye = cameraPosition.xyz - position;
	float ToEyeDist = length(ToEye);
	int nsteps = ToEyeDist / step;
	float3 ToEyeRayUnit = ToEye / nsteps;
	float extra_step_w = ((ToEyeDist / step) - (float)nsteps);
	bool was_light = false;
	float3 ToEyeRayUnitMicroStep = ToEyeRayUnit / 10.0f;
	float mag = 0.0f;
	if (nsteps > max_nsteps) {
		int diff = nsteps - max_nsteps;
		position += ToEyeRayUnit * diff;
		nsteps = max_nsteps;
	}
	for (int i = 0; i <= nsteps; ++i) {
		position += ToEyeRayUnit;
		float3 ToLight = position - lposition;
		float LightRange = (light.Range - length(ToLight)) / light.Range;
		float DistToLightNorm = saturate(LightRange);
		float shadow = 1.0f;
		if (light.cast_shadow) {
			shadow = PointShadowPCFFast(ToLight, light, index);
		}
#if 0
		bool is_light = (shadow > 0.0f);
		float att = saturate(pow(DistToLightNorm, 4.0f));
		if (i == nsteps) {
			att *= extra_step_w;
		}
		else {
			if (was_light && !is_light) {
				float3 current_pos = position;
				for (int i = 9; i >= 0; --i) {
					current_pos -= ToEyeRayUnitMicroStep;
					float3 testToLight = current_pos - lposition;
					float test = PointShadowPCFFast(testToLight, light, index);
					if (test > 0.9f) {
						float w = (float)i / 10.0f;
						att *= w;
						break;
					}
				}
			}
			else {
				att *= shadow;
			}
		}
		was_light = is_light;
#else
		float att = 1.0f;
		att *= shadow;
#endif		
		saturate(att);
		color += step_color * att;
		mag = max(color.r, max(color.g, color.b));
		if (mag > max_vol) {
			break;
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
		spec_intensity *= specularTexture.Sample(basicSampler, uv).r;
	}
	else if (material.flags & ARM_MAP_ENABLED_FLAG) {
		spec_intensity *= armTexture.Sample(basicSampler, uv).g;
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
		h = highTexture.SampleLevel(basicSampler, pos, 2).r;
	}
	return h;
}

float2 CalculateDepthUVBinary(float scale, float2 uv, float3 camera_pos,
	float3 fragment_pos, const float calculated_values[MAX_MULTI_TEXTURE], out float h, out float3 displacement) {

	float3 ToCam = camera_pos - fragment_pos;
	float dist = length(ToCam);
	float angle = dot(normalize(ToCam), float3(0.0f, 0.0f, 1.0f));
	float nsteps = 4.0f + 5.0f * saturate(1.0f - angle) + clamp(lerp(4.0f, 0.0f, dist / 50.0f), 0.0, 4.0);
	int insteps = (int)nsteps;
	float step_delta = 0.5f;

	displacement = float3(0.0f, 0.0f, 0.0f);
	ToCam.xy *= scale * angle;
	ToCam = normalize(ToCam);
	float3 curr_uvw = float3(uv, 0.0f) + ToCam * step_delta;
	int steps = 0;
	while (steps <= insteps) {
		steps++;
		h = GetHeight(curr_uvw.xy, calculated_values).r;
		if (h > curr_uvw.z) {
			curr_uvw += ToCam * step_delta;;
			displacement += ToCam * step_delta;;
		}
		else {
			curr_uvw -= ToCam * step_delta;
			displacement -= ToCam * step_delta;
		}
		step_delta *= 0.5f;
	}
	displacement.xy /= scale;
	return curr_uvw.xy;
}
#if 0
float2 CalculateDepthUVBinary(float scale, float2 uv, float3 camera_pos,
	float3 fragment_pos, float3 world_position, const float calculated_values[MAX_MULTI_TEXTURE], out float h, out float3 displacement) {

	float3 ToCam = camera_pos - fragment_pos;
	float dist = length(ToCam);
	float angle = dot(normalize(ToCam), float3(0.0f, 0.0f, 1.0f));
	ToCam.xy *= scale * angle;
	float nsteps = 4.0f + 5.0f * saturate(1.0f - angle) + lerp(4.0f, 0.0f, dist / 50.0f);
	displacement = float3(0.0f, 0.0f, 0.0f);
	int insteps = (int)nsteps;

	float step_delta = 0.5f;
	ToCam = normalize(ToCam);
	float3 curr_uvw = float3(uv, 0.0f) + ToCam * step_delta;
	int steps = 0;
	while (steps <= insteps) {
		steps++;
		h = GetHeight(curr_uvw.xy, world_position, calculated_values).r;
		if (h > curr_uvw.z) {
			curr_uvw += ToCam * step_delta;
			displacement += ToCam * step_delta;
		}
		else {
			curr_uvw -= ToCam * step_delta;
			displacement -= ToCam * step_delta;
		}
		step_delta *= 0.5f;
	}
	displacement.xy /= scale;
	return curr_uvw.xy;
}
#endif
