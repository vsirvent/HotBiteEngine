/*
Copyright (c) 2016  LukeRissacher

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, 
publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is 
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED 
TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE 
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef __QUICK_NOISE_HLSLI__
#define __QUICK_NOISE_HLSLI__

float nrand(float2 uv)
{
    return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
}

float2 GetGradient(float2 intPos, float t) {

    // Texture-based rand (a bit faster on my GPU)
    float rand = nrand(intPos);

    // Rotate gradient: random starting rotation, random rotation rate
    float angle = 6.283185 * rand + 4.0 * t * rand;
    return float2(cos(angle), sin(angle));
}


float Pseudo3dNoise(float3 pos) {
    float2 i = floor(pos.xy);
    float2 f = pos.xy - i;
    float2 blend = f * f * (3.0 - 2.0 * f);
    float noiseVal =
        lerp(
            lerp(
                dot(GetGradient(i + float2(0, 0), pos.z), f - float2(0, 0)),
                dot(GetGradient(i + float2(1, 0), pos.z), f - float2(1, 0)),
                blend.x),
            lerp(
                dot(GetGradient(i + float2(0, 1), pos.z), f - float2(0, 1)),
                dot(GetGradient(i + float2(1, 1), pos.z), f - float2(1, 1)),
                blend.x),
            blend.y
        );
    return (noiseVal * 1.4 + 1.0f)*0.5f; // normalize to about [-1..1]
}

static const float3x3 m = 
   { 0.00, 0.80, 0.60,
    -0.80, 0.36, -0.48,
    -0.60, -0.48, 0.64 };

float noise(float3 pos) {
    float3 q = pos;
    float f = 0.5000 * Pseudo3dNoise(q); q = mul(q, m) * 2.01f;
    f += 0.2500 * Pseudo3dNoise(q); q = mul(q, m) * 2.02;
    f += 0.1250 * Pseudo3dNoise(q); q = mul(q, m) * 2.03;
    f += 0.0625 * Pseudo3dNoise(q); q = mul(q, m) * 2.01;
    return f;
}

/* https://www.shadertoy.com/view/XsX3zB
 *
 * The MIT License
 * Copyright © 2013 Nikita Miropolskiy
 *
 * ( license has been changed from CCA-NC-SA 3.0 to MIT
 *
 *   but thanks for attributing your source code when deriving from this sample
 *   with a following link: https://www.shadertoy.com/view/XsX3zB )
 *
 * ~
 * ~ if you're looking for procedural noise implementation examples you might
 * ~ also want to look at the following shaders:
 * ~
 * ~ Noise Lab shader by candycat: https://www.shadertoy.com/view/4sc3z2
 * ~
 * ~ Noise shaders by iq:
 * ~     Value    Noise 2D, Derivatives: https://www.shadertoy.com/view/4dXBRH
 * ~     Gradient Noise 2D, Derivatives: https://www.shadertoy.com/view/XdXBRH
 * ~     Value    Noise 3D, Derivatives: https://www.shadertoy.com/view/XsXfRH
 * ~     Gradient Noise 3D, Derivatives: https://www.shadertoy.com/view/4dffRH
 * ~     Value    Noise 2D             : https://www.shadertoy.com/view/lsf3WH
 * ~     Value    Noise 3D             : https://www.shadertoy.com/view/4sfGzS
 * ~     Gradient Noise 2D             : https://www.shadertoy.com/view/XdXGW8
 * ~     Gradient Noise 3D             : https://www.shadertoy.com/view/Xsl3Dl
 * ~     Simplex  Noise 2D             : https://www.shadertoy.com/view/Msf3WH
 * ~     Voronoise: https://www.shadertoy.com/view/Xd23Dh
 * ~
 *
 */

 /* discontinuous pseudorandom uniformly distributed in [-0.5, +0.5]^3 */
float3 random3(float3 c) {
	float j = 4096.0 * sin(dot(c, float3(17.0, 59.4, 15.0)));
	float3 r;
	r.z = frac(512.0 * j);
	j *= .125;
	r.x = frac(512.0 * j);
	j *= .125;
	r.y = frac(512.0 * j);
	return r - 0.5;
}

/* skew constants for 3d simplex functions */
static const float F3 = 0.3333333;
static const float G3 = 0.1666667;

/* 3d simplex noise */
float simplex3d(float3 p) {
	/* 1. find current tetrahedron T and it's four vertices */
	/* s, s+i1, s+i2, s+1.0 - absolute skewed (integer) coordinates of T vertices */
	/* x, x1, x2, x3 - unskewed coordinates of p relative to each of T vertices*/

	/* calculate s and x */
	float3 s = floor(p + dot(p, float3(F3, F3, F3)));
	float3 x = p - s + dot(s, float3(G3, G3, G3));

	/* calculate i1 and i2 */
	float3 e = step(float3(0.0, 0.0, 0.0), x - x.yzx);
	float3 i1 = e * (1.0 - e.zxy);
	float3 i2 = 1.0 - e.zxy * (1.0 - e);

	/* x1, x2, x3 */
	float3 x1 = x - i1 + G3;
	float3 x2 = x - i2 + 2.0 * G3;
	float3 x3 = x - 1.0 + 3.0 * G3;

	/* 2. find four surflets and store them in d */
	float4 w, d;

	/* calculate surflet weights */
	w.x = dot(x, x);
	w.y = dot(x1, x1);
	w.z = dot(x2, x2);
	w.w = dot(x3, x3);

	/* w fades from 0.6 at the center of the surflet to 0.0 at the margin */
	w = max(0.6 - w, 0.0);

	/* calculate surflet components */
	d.x = dot(random3(s), x);
	d.y = dot(random3(s + i1), x1);
	d.z = dot(random3(s + i2), x2);
	d.w = dot(random3(s + 1.0), x3);

	/* multiply d by w^4 */
	w *= w;
	w *= w;
	d *= w;

	/* 3. return the sum of the four surflets */
	return dot(d, float4(52.0, 52.0, 52.0, 52.0));
}

/* const matrices for 3d rotation */
static const float3x3 rot1 = float3x3(-0.37, 0.36, 0.85, -0.14, -0.93, 0.34, 0.92, 0.01, 0.4);
static const float3x3 rot2 = float3x3(-0.55, -0.39, 0.74, 0.33, -0.91, -0.24, 0.77, 0.12, 0.63);
static const float3x3 rot3 = float3x3(-0.71, 0.52, -0.47, -0.08, -0.72, -0.68, -0.7, -0.45, 0.56);

/* directional artifacts can be reduced by rotating each octave */
float simplex3d_fractal(float3 m) {
	float f0 = 0.5333333 * simplex3d(m);
	float f1 = 0.2666667 * simplex3d(2.0 * m);
	float f2 = 0.1333333 * simplex3d(4.0 * m);
	//float f3 = 0.0666667 * simplex3d(8.0 * m);
	return f0 + f1 + f2;// +f3;
}

#endif