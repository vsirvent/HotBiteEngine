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

//The material map: what the G-buffer pass knows about a surface that the lighting needs and
//the other targets do not carry. RGBA16F, cleared to zero each frame, written next to the
//other seven targets by MainRenderPS, WaterPS and LavaPS (SV_TARGET7) and by SplatRasterCS
//(material_out) and read by DeferredLightCS.
//
//    w     the shading model in the low 3 bits, GBUFFER_PASS2 on top of it for a surface
//          drawn by the second pass (see DeferredLightCS's pass filter)
//    x y z mean different things per model:
//
//    model                 x               y               z
//    STANDARD  (0)         spec intensity  bloom scale     parallax shadow of the first
//                                                          directional light (1 = none)
//    WATER     (1)         depth/clarity   shading normal x  shading normal z
//                          (transmission is exp(-x * WATER_EXTINCTION))
//    LAVA      (2)         light weight    -               -
//    SPLAT     (3)         spec intensity  bloom scale     the cloud's alpha at the pixel,
//                                                          which weights ALL of its light

#ifndef __GBUFFER_HLSLI__
#define __GBUFFER_HLSLI__

#define SHADING_STANDARD 0
#define SHADING_WATER 1
#define SHADING_LAVA 2
#define SHADING_SPLAT 3
#define GBUFFER_MODEL_MASK 7
#define GBUFFER_PASS2 8

//Per-channel absorption of water (kills red first, which is why deep clear water reads
//blue-green). WaterPS and DeferredLightCS must agree on this, so it lives here.
#define WATER_EXTINCTION float3(3.2f, 1.0f, 0.65f)

//The `w` of the material map for a surface of `model` drawn by geometry pass `render_pass`
//(1 or 2, what RenderSystem::DrawScene tells the shader).
float MaterialCode(int model, int render_pass)
{
	return (float)(model | ((render_pass == 2) ? GBUFFER_PASS2 : 0));
}

#endif
