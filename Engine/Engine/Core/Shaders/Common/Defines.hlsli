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

#ifndef __DEFINES_HLSLI__
#define __DEFINES_HLSLI__

#define MAX_MULTI_TEXTURE 8
//Directional lights, and the shadow-casting point lights (each one needs a cube map bound
//as a shader resource, so those are limited by registers, not by data). Point lights as
//such are not limited by this any more: see MAX_TILED_LIGHTS.
#define MAX_LIGHTS 8
//The point lights the tiled lighting pass knows about. Mirrors MAX_TILED_LIGHTS in
//Components/Lights.h. The per-tile mask has one bit per light, so this is
//TILE_MASK_WORDS 32-bit words per tile, and both numbers are baked into the layout of
//the mask buffer: change them together on both sides.
#define MAX_TILED_LIGHTS 1024
#define TILE_MASK_WORDS (MAX_TILED_LIGHTS / 32)
//Screen tile edge, in pixels. The culling dispatch is one 16x16 group per tile, so this
//is also the group size of TileLightCullCS and DeferredLightCS.
#define TILE_SIZE 16
//Point lights a shader that has no screen tile (ray hits, glows, the volumetric march)
//gets through a constant buffer: the nearest ones to the camera. Mirrors
//MAX_AUX_LIGHTS in Components/Lights.h.
#define MAX_AUX_LIGHTS 8
//Shadow cascade slices per directional light. Mirrors MAX_SHADOW_CASCADES in
//Components/Lights.h - the two size the same arrays from opposite sides and must
//match. Per-light cascade matrices are packed at this fixed stride, so the entry for
//cascade c of light i is always at [i * MAX_SHADOW_CASCADES + c] no matter how many
//slices that light actually uses.
#define MAX_SHADOW_CASCADES 4
#define DIR_SHADOW_MATRIX_COUNT (MAX_LIGHTS * MAX_SHADOW_CASCADES)

//Directional shadow PCF kernel: taps are one texel apart over a (2r+1) square, so 2 is
//5x5 = 25 taps. Radius is in whole texels rather than world units, which makes the blur
//a fixed number of texels and therefore scale with each cascade's texel size - a near
//cascade blurs a short distance, a far one a long distance. Shared by both lighting
//front ends (PixelFunctions.hlsli and SimpleLight.hlsli), which is why it lives here.
#define DIR_PCF_RADIUS 2
#define DIR_PCF_TAPS ((2 * DIR_PCF_RADIUS + 1) * (2 * DIR_PCF_RADIUS + 1))
#define DIR_PCF_WEIGHT (1.0f / (float)DIR_PCF_TAPS)
#define FLT_MAX 3.4e+38
#define FLT_MIN 1.1e-38 

#define EQUAL(x, y, delta) (abs(x - y) < delta)
#define MAX_JOINTS 256

#define NUM_CONTROL_POINTS 3
#ifndef PI
#define PI 3.14159265359f
#endif 
#define M_PI PI
#endif