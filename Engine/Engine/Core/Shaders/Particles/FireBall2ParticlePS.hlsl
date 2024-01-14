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

#include "../Common/ShaderStructs.hlsli"
#include "../Common/PixelCommon.hlsli"
#include "../Common/RGBANoise.hlsli"

cbuffer externalData : register(b0)
{
    matrix world;
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
    matrix DirPerspectiveMatrix[MAX_LIGHTS];
    matrix DirStaticPerspectiveMatrix[MAX_LIGHTS];
    matrix spot_view;
    float time;
    float sizeIncrementRatio;
}

static const int ITERATIONS = 30;
static const float STEP = 0.02;



float fbm(float3 x)
{
    float r = 0.0;
    float w = 1.0, s = 1.0;
    for (int i = 0; i < 5; i++)
    {
        w *= 0.5;
        s *= 2.0;
        r += w * rgba_tnoise(s * x);
    }
    return r;
}

float sphere(float3 p, float r)
{
    return length(p) - r;
}

float4 scene(float3 pos, float3 dir, out float dist, float t)
{
    float rad1 = 0.6, rad2 = 0.6;
    float d = sphere(pos, rad1);
    
    d -= rad2 * fbm(float3(pos.x + t * 0.5f, pos.y + t * 0.5f, pos.z - t * 0.5f));
    dist = d;
    float4 color;
    float r = length(pos) / rad1;
    color.a = smoothstep(1., 0., clamp(d / .05, 0., 1.));
    color.a *= smoothstep(1., 0., clamp((r - 1.25) * 2., 0., 1.));
    color.rgb = 1.2 * lerp(float3(1.0, 0.95, 0.2), float3(1.0, 0.35, 0.), clamp((r - 1.1) * 3., 0., 1.));
    color.rgb = lerp(color.rgb, float3(0.95, 0.95, 1.0) * 0.25, clamp((r - 1.3) * 3., 0., 1.));
    return color;
}

float4 render(float3 eye, float3 dir, float t)
{    
    float4 color = { 0.0f, 0.0f, 0.0f, 0.0f };    
    float3 pos = eye;
    for (int i = 0; i < ITERATIONS; i++)
    {
        if (color.a > 0.99) break;
        float dist;
        float4 d = scene(pos, dir, dist, t);
        d.rgb *= d.a;
        color += d * (1.0 - color.a);
        pos += dir * max(STEP, dist * 0.5);
    }
    color.rgb += float3(0.1 * float3(1.0, 0.5, 0.1)) * (1.0 - color.a);
    return color;
}

RenderTarget main(GSParticleOutput input)
{
    RenderTarget output;
    float4 lightColor = { 0.0f, 0.0f, 0.0f, 0.0f };
    float4 finalColor = { 0.0f, 0.0f, 0.0f, 0.0f };
    float3 uv = 2.0f*(input.worldPos.xyz - input.center) / input.size;
    float3 eye = cameraPosition - input.center;
    float3 dir = normalize(uv - eye);
    float t = time + 2.5f*sin(input.id/10.0f);
    finalColor = render(eye, dir, t);

    float border = 1.0f - pow(length(abs(input.uv * 2.0f - 1.0f)), 2.0f);
    float fade_in = saturate((1.0f - input.life) / 0.1f);
    float fade_out = saturate(input.life * 10.0f);
    finalColor.a *= border * fade_out * fade_in;
    output.light_map = saturate(lightColor);
    output.scene = saturate(finalColor);
    return output;
}
