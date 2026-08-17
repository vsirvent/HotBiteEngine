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

#include "../Common/Utils.hlsli"
#include "../Common/RenderDebug.hlsli"
//For fromColor(): the ray source views decode the same packed pair the ray tracers
//read, through the same function, rather than reproducing the layout here.
#include "../Common/ShaderStructs.hlsli"

cbuffer externalData : register(b0)
{
    int rt_enabled;
    int frame_count;
    float time;
    uint debug;
    //Exposure for the debug views only. Most of the buffers below this mixes are
    //HDR and several (indirect light in particular) sit far below 1.0, so shown
    //raw they read as black and look broken. Ignored unless a buffer view is on.
    float debug_gain;
    //Only the two radiance cache views need this: a cell is keyed partly by its
    //distance to the camera, so a lookup cannot be done without it.
    float3 cameraPosition;
}

RWTexture2D<float4> output : register(u0);
Texture2D input: register(t2);
Texture2D depthTexture: register(t3);
Texture2D lightTexture: register(t4);
Texture2D bloomTexture: register(t5);
Texture2D rtTexture0 : register(t6);
Texture2D rtTexture1 : register(t7);
Texture2D rtTexture2 : register(t8);
Texture2D volLightTexture: register(t10);
Texture2D dustTexture: register(t11);
Texture2D lensFlareTexture: register(t12);
Texture2D positions: register(t13);
Texture2D normals: register(t14);
Texture2D emissionTexture: register(t15);
Texture2D<float2> motionTexture: register(t16);
SamplerState basicSampler : register(s0);

//Read-only: the mixer inspects the cache for the two debug views and never writes
//it. RC_READ_ONLY also drops RCDeposit, so the deposit path cannot be reached from
//a pass that has no business filling the cache.
#define RC_READ_ONLY
#include "../Common/RadianceCache.hlsli"

#include "../Common/RGBANoise.hlsli"

float4 Get3dInterpolatedColor(float2 uv, Texture2D text, float2 dimension, Texture2D positions, Texture2D normals, float2 pos_dimension) {

	float4 result;

	// Calculate the texture coordinates in the range [0, 1]
	float2 texCoords = uv * dimension;
	float2 pos_ratio = pos_dimension / dimension;

	// Calculate the integer coordinates of the four surrounding pixels
	int2 p00 = (int2)floor(texCoords);
	int2 p11 = int2(p00.x + 1, p00.y + 1);
	int2 p01 = int2(p00.x, p00.y + 1);
	int2 p10 = int2(p00.x + 1, p00.y);

	float epsilon = 10e-4;

	//Get 3d positions
	uint2 pos_p00 = round(p00 * pos_ratio);
	uint2 pos_p11 = round(p11 * pos_ratio);
	uint2 pos_p01 = round(p01 * pos_ratio);
	uint2 pos_p10 = round(p10 * pos_ratio);
	uint2 in_pos = round(texCoords * pos_ratio);

	float3 wp00 = positions[pos_p00].xyz;
	float3 wp11 = positions[pos_p11].xyz;
	float3 wp01 = positions[pos_p01].xyz;
	float3 wp10 = positions[pos_p10].xyz;
	float3 wpxx = positions[in_pos].xyz;

	[branch]
	if (wp00.x >= FLT_MAX || wp11.x >= FLT_MAX || wp01.x >= FLT_MAX || wp10.x >= FLT_MAX || wpxx.x >= FLT_MAX) {
		result = GetInterpolatedColor(uv, text, dimension);
	}
	else {
		float d00 = dist2(wpxx - wp00);  // Distance to wp00
		float d11 = dist2(wpxx - wp11);  // Distance to wp11
		float d01 = dist2(wpxx - wp01);  // Distance to wp01
		float d10 = dist2(wpxx - wp10);  // Distance to wp10

		float all_dist = d00 + d11 + d01 + d10;

		[branch]
		if (all_dist < epsilon) {
			result = GetInterpolatedColor(uv, text, dimension);
		}
		else {
			float w00 = 1.0f - d00 / all_dist;
			float w11 = 1.0f - d11 / all_dist;
			float w01 = 1.0f - d01 / all_dist;
			float w10 = 1.0f - d10 / all_dist;

#if 1
			// Calculate the fractional part of the coordinates
			float2 f = frac(texCoords);

			// Calculate the weights for bilinear interpolation
			w00 *= (1.0f - f.x) * (1.0f - f.y);
			w11 *= f.x * f.y;
			w01 *= (1.0f - f.x) * f.y;
			w10 *= f.x * (1.0f - f.y);
#endif
#if 0
			static const float DIST_K = 2.0f;
			w00 = pow(w00, DIST_K);
			w11 = pow(w11, DIST_K);
			w01 = pow(w01, DIST_K);
			w10 = pow(w10, DIST_K);
#endif
			float3 n00 = normals[pos_p00].xyz;
			float3 n11 = normals[pos_p11].xyz;
			float3 n01 = normals[pos_p01].xyz;
			float3 n10 = normals[pos_p10].xyz;
			float3 nxx = normals[in_pos].xyz;
#if 1
			static const float DOT_K = 2.0f;
			w00 *= pow(saturate(dot(nxx, n00)), DOT_K);
			w11 *= pow(saturate(dot(nxx, n11)), DOT_K);
			w01 *= pow(saturate(dot(nxx, n01)), DOT_K);
			w10 *= pow(saturate(dot(nxx, n10)), DOT_K);
#endif
			// Normalize weights
			float totalWeight = w00 + w11 + w01 + w10;

			[branch]
			if (totalWeight < epsilon) {
				result = GetInterpolatedColor(uv, text, dimension);
			}
			else {
				w00 /= totalWeight;
				w11 /= totalWeight;
				w01 /= totalWeight;
				w10 /= totalWeight;

				result = (text[p00] * w00 + text[p11] * w11 + text[p01] * w01 + text[p10] * w10);
			}
		}
	}

	return result;
}


float4 readColor(float2 pixel, texture2D text, uint w, uint h) {
    uint w2, h2;
    text.GetDimensions(w2, h2);
    if (w2 == w && h2 == h) {
		return text[round(pixel * float2(w2, h2))];
    }
    else {
#if 0
        float ratioW = ((float)w * 0.5f) / w2;
		float ratioH = ((float)h * 0.5f) / h2;
		uint n = 2;
		float x = 0;
		float y = 0;
		float4 c = 2 * Get3dInterpolatedColor(pixel, text, float2(w2, h2), positions, normals, float2(w, h));
		for (x = -ratioW; x < ratioW; x++) {
			for (y = -ratioH; y < ratioH; y++) {
				c += text.SampleLevel(basicSampler, pixel + float2(x * 0.5f / w2, y * 0.5f / h2), 0);
				//c += Get3dInterpolatedColor(pixel + float2(x / w, y / h), text, float2(w2, h2), positions, normals, float2(w, h));
				n++;
			}
		}
		c /= n;
		return c;
#else
		return Get3dInterpolatedColor(pixel, text, float2(w2, h2), positions, normals, float2(w, h));
#endif
    }
}

//Show one of the buffers this shader mixes, instead of the mix. Everything the
//frame is made of passes through here, which is why the switch lives in the mixer
//rather than in a pass of its own - no extra dispatch, no extra target, and what
//you see is exactly the bits the final frame was about to be built from.
//
//Only the selected texture is read: the branch is uniform across the dispatch
//(`debug` is a constant), so a debug frame does not pay for the readColor calls
//of the buffers it is not showing.
float4 DebugBufferColor(uint buffer_id, float2 tpos, float2 pixel, uint w, uint h)
{
    float3 c = float3(0.0f, 0.0f, 0.0f);
    switch (buffer_id) {
    case RT_DEBUG_BUFFER_SCENE:      c = input[pixel].rgb; break;
    case RT_DEBUG_BUFFER_LIGHT:      c = readColor(tpos, lightTexture, w, h).rgb; break;
    case RT_DEBUG_BUFFER_BLOOM:      c = readColor(tpos, bloomTexture, w, h).rgb; break;
    case RT_DEBUG_BUFFER_EMISSION:   c = readColor(tpos, emissionTexture, w, h).rgb; break;
    case RT_DEBUG_BUFFER_REFLECTION: c = readColor(tpos, rtTexture0, w, h).rgb; break;
    case RT_DEBUG_BUFFER_REFRACTION: c = readColor(tpos, rtTexture1, w, h).rgb; break;
    case RT_DEBUG_BUFFER_INDIRECT:   c = readColor(tpos, rtTexture2, w, h).rgb; break;
    case RT_DEBUG_BUFFER_VOLUMETRIC: c = readColor(tpos, volLightTexture, w, h).rgb; break;
    case RT_DEBUG_BUFFER_DUST:       c = readColor(tpos, dustTexture, w, h).rgb; break;
    case RT_DEBUG_BUFFER_LENS_FLARE: c = readColor(tpos, lensFlareTexture, w, h).rgb; break;
    //The three that are not colours are mapped rather than gained: a ramp or a
    //normal has nothing to expose, and scaling it would only destroy the mapping.
    case RT_DEBUG_BUFFER_DEPTH:      return float4(DebugDepthColor(depthTexture[pixel].r), 1.0f);
    case RT_DEBUG_BUFFER_POSITION:   return float4(DebugPositionColor(positions[pixel].xyz), 1.0f);
    case RT_DEBUG_BUFFER_NORMAL:     return float4(DebugNormalColor(normals[pixel].xyz), 1.0f);
    //Motion is the exception to "mapped buffers ignore the gain" - see DebugMotionColor.
    case RT_DEBUG_BUFFER_MOTION:     return float4(DebugMotionColor(motionTexture[pixel], debug_gain), 1.0f);
    //The world radiance cache, looked up at whatever surface this pixel sees. Unlike
    //every other view here this is not a texture being displayed - there is no
    //screen-space image of the cache to show - so it is resolved per pixel through
    //the same hash the ray tracer uses. Blue where the lookup found no cell, so that
    //"there is nothing here" is distinguishable from "there is a cell and it is
    //black", which is the distinction you are always trying to make.
    case RT_DEBUG_BUFFER_GI_CACHE: {
        float4 v = RCLookup(positions[pixel].xyz, normals[pixel].xyz, cameraPosition);
        if (v.w <= 0.0f) { return float4(0.0f, 0.0f, 0.6f, 1.0f); }
        return float4(saturate(v.rgb * debug_gain), 1.0f);
    }
    case RT_DEBUG_BUFFER_GI_CACHE_CONF: {
        float4 v = RCLookup(positions[pixel].xyz, normals[pixel].xyz, cameraPosition);
        return float4(DebugCacheConfidenceColor(v.w, v.w > 0.0f), 1.0f);
    }
    //The RaySource pair, decoded exactly as the ray tracers decode it. `positions` and
    //`normals` *are* rt_ray_sources0/1 - the same two targets, which is why no extra
    //binding was needed for these - so the xyz halves are already the POSITION and
    //NORMAL views above and these five show what is packed alongside them: the four
    //scalars that decide whether a pixel traces, and the mask that combines them.
    case RT_DEBUG_BUFFER_RAY_SOURCES: {
        RaySource ray = fromColor(positions[pixel], normals[pixel]);
        return float4(DebugRayMaskColor(dist2(ray.normal), ray.reflex, ray.dispersion,
                                        ray.opacity, Epsilon), 1.0f);
    }
    case RT_DEBUG_BUFFER_RAY_DISPERSION: {
        RaySource ray = fromColor(positions[pixel], normals[pixel]);
        return float4(DebugRayScalarColor(ray.dispersion, dist2(ray.normal) > Epsilon,
                                          debug_gain), 1.0f);
    }
    case RT_DEBUG_BUFFER_RAY_REFLEX: {
        RaySource ray = fromColor(positions[pixel], normals[pixel]);
        return float4(DebugRayScalarColor(ray.reflex, dist2(ray.normal) > Epsilon,
                                          debug_gain), 1.0f);
    }
    case RT_DEBUG_BUFFER_RAY_DENSITY: {
        RaySource ray = fromColor(positions[pixel], normals[pixel]);
        return float4(DebugRayScalarColor(ray.density, dist2(ray.normal) > Epsilon,
                                          debug_gain), 1.0f);
    }
    case RT_DEBUG_BUFFER_RAY_OPACITY: {
        RaySource ray = fromColor(positions[pixel], normals[pixel]);
        return float4(DebugRayScalarColor(ray.opacity, dist2(ray.normal) > Epsilon,
                                          debug_gain), 1.0f);
    }
    }
    return float4(saturate(c * debug_gain), 1.0f);
}

#define NTHREADS 8
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint w, h;
    output.GetDimensions(w, h);
    float2 pixel = float2(DTid.x, DTid.y);

    float2 tpos = pixel;
    tpos.x /= w;
    tpos.y /= h;

    [branch]
    if (DebugBuffer(debug) != RT_DEBUG_BUFFER_OFF) {
        output[pixel] = DebugBufferColor(DebugBuffer(debug), tpos, pixel, w, h);
        return;
    }

    float4 color = input[pixel];
    float4 l = readColor(tpos, lightTexture, w, h);
    float4 b = readColor(tpos, bloomTexture, w, h);
    float4 e = readColor(tpos, emissionTexture, w, h);
    float4 rt0 = readColor(tpos, rtTexture0, w, h);
    float4 rt1 = readColor(tpos, rtTexture1, w, h);
    float4 rt2 = readColor(tpos, rtTexture2, w, h);
    float4 vol = readColor(tpos, volLightTexture, w, h);
    float4 dust = readColor(tpos, dustTexture, w, h);
    float4 lens_flare = readColor(tpos, lensFlareTexture, w, h);
	    
    color = color * (l + rt0 + rt2) + rt1 + b + dust + lens_flare + vol;
    //color = color * (rt2) + rt1 + b + dust + lens_flare + vol;

#if 1
    output[pixel] = climit4(color);
#else
    float2 p = pixel * 0.3f + frac(time) * 1000.0f;
    float r = 1.0f + (0.1f * rgba_tnoise(p));
    output[pixel] = climit4(color * r);
#endif
}
