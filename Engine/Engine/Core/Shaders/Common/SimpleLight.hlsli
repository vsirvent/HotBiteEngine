
//Light at a ray hit.
//
//A ray hit has no screen tile - it can be anywhere in the world, behind the camera or off
//screen - so the reflection, refraction and indirect light tracers cannot use the tile masks
//DeferredLightCS reads. This is the one place in the engine, besides the deferred pass, that
//evaluates a light on a surface, and it deliberately does less than it: Lambert only, one
//shadow tap, the directional lights and the nearest few point lights (the aux lights,
//MAX_AUX_LIGHTS of them, read from a constant buffer because these shaders have no texture
//register to spare - see the note on Texture2DArray below). A hit that is out of reach of
//those is lit by the sun, ambient and the radiance cache, as it always was.
//
//The shadow of point light `slot` (PackedLight::flags, see LightShadowSlot). The slot is
//data, and a texture array cannot be indexed by data, so it is matched against each map by
//a literal index (see GatherPointShadow in LightShadows.hlsli).
float PointShadowPCFFAST(float3 ToPixel, int slot)
{
    float3 ToPixelAbs = abs(ToPixel);
    float Z = max(ToPixelAbs.x, max(ToPixelAbs.y, ToPixelAbs.z));
    float d = (lps[slot].x * Z + lps[slot].y) / Z;
    //This offset allows to avoid self shadow
    d -= 0.0001f;
    float att1 = 1.0f;
    [unroll]
    for (int s = 0; s < MAX_LIGHTS; ++s) {
        [branch]
        if (s == slot) {
            att1 = PointShadowMapTexture[s].SampleCmpLevelZero(PCFSampler, float3(ToPixel.x, ToPixel.y, ToPixel.z), d).r;
        }
    }
    return saturate(att1);
}

//A directional shadow map only covers a slice of the world: an XY footprint, and the
//near/far slab its depth encodes. Outside that the map holds nothing to test against,
//and the only sane answer is "lit". Sampling anyway does not fail quietly - the
//comparison sampler clamps to an edge texel in XY, and a p.z past the far plane
//compares greater than every stored depth - so both read as *occluded*, which is what
//a camera flying out of the map looked like: the world going dark. The static-caster
//variant has always returned lit here; the dynamic ones returned 0.5f, half-darkening
//everything beyond the footprint.
#ifndef __DIR_SHADOW_FOOTPRINT__
#define __DIR_SHADOW_FOOTPRINT__
bool OutsideShadowMap(float3 p)
{
    return p.x < 0.0f || p.x > 1.0f ||
           p.y < 0.0f || p.y > 1.0f ||
           p.z < 0.0f || p.z > 1.0f;
}

//See LightShadows.hlsli for what the cascade search is doing and why containment is
//the selection test. The two copies exist because these two headers are alternative
//lighting front ends, never included together.
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

//See LightShadows.hlsli - same colours, same floor, duplicated for the same reason
//the cascade search is.
static const float3 CASCADE_DEBUG_COLOR[MAX_SHADOW_CASCADES] = {
    float3(0.15f, 1.00f, 0.25f),
    float3(1.00f, 0.85f, 0.10f),
    float3(1.00f, 0.35f, 0.05f),
    float3(1.00f, 0.15f, 0.75f),
};
static const float3 CASCADE_DEBUG_OUTSIDE = float3(0.20f, 0.35f, 1.00f);
#define CASCADE_DEBUG_FLOOR 0.15f

float3 ApplyDirCascadeDebug(float3 lit, float4 position, DirLight light, int index)
{
    if (!(light.flags & DIR_LIGHT_FLAG_DEBUG_CASCADES)) {
        return lit;
    }
    float3 uvz = float3(0.0f, 0.0f, 0.0f);
    int cascade = SelectDirCascade(position, light, index, uvz);
    float3 tint = (cascade < 0) ? CASCADE_DEBUG_OUTSIDE : CASCADE_DEBUG_COLOR[cascade];
    return tint * (CASCADE_DEBUG_FLOOR + lit);
}
#endif

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

    //Two things were undoing the filtering here. round() snapped each tap back to 0 or
    //1, throwing away exactly the sub-texel blend that SampleCmpLevelZero exists to
    //give - so the average was of binary values again, stepping at texel boundaries.
    //And the divisor accumulated 0.8 per tap rather than 1, which made the mean 25% too
    //bright and then clamped, quietly eating the darker end of every penumbra.
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

float DirShadowPCFFAST(float4 position, DirLight light, int index)
{
    float3 p = float3(0.0f, 0.0f, 0.0f);
    int cascade = SelectDirCascade(position, light, index, p);
    if (cascade < 0) {
        return 1.0f;
    }
    float att1 = DirShadowMapTexture[index].SampleCmpLevelZero(PCFSampler, float3(p.x, p.y, cascade), p.z).r;
    return saturate(att1);
}

//Static casters render into their own map on a slow refresh cycle, sampled through the
//view matrix captured at that refresh. Returns 1.0f (lit) when the map holds no data
//for this pixel, since the result is min()'d with the dynamic attenuation.
//
//Opt-in: the includer must declare DirStaticShadowMapTexture[MAX_LIGHTS] +
//DirStaticPerspectiveMatrix[MAX_LIGHTS] and #define HAS_STATIC_DIR_SHADOWS before the
//include. It is not unconditional because the array costs MAX_LIGHTS texture registers,
//and shaders carrying DiffuseTextures[MAX_OBJECTS] (GIRayTraceCS, RayTraceCS) blow the
//128-register cs_5_0 limit with it (error X4565). Those keep static casters out of their
//shadow term; they are indirect/secondary passes where the dynamic map is enough.
#ifdef HAS_STATIC_DIR_SHADOWS
//One plain map per light, fitted to the widest cascade - see PixelFunctions.hlsli.
bool ProjectDirStatic(float4 position, DirLight light, int index, out float3 uvz)
{
    float4 p = mul(position, DirStaticPerspectiveMatrix[index]);
    uvz = float3((p.x + 1.0f) * 0.5f, 1.0f - ((p.y + 1.0f) * 0.5f), p.z);
    return !OutsideShadowMap(uvz);
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
    float att1 = DirStaticShadowMapTexture[index].SampleCmpLevelZero(PCFSampler, float2(p.x, p.y), p.z).r;
    return saturate(att1);
}

float DirShadowPCFFASTAll(float4 position, DirLight light, int index)
{
    return min(DirShadowPCFFAST(position, light, index),
               DirStaticShadowPCFFAST(position, light, index));
}

//See LightShadows.hlsli for what these colours mean.
static const float3 STATIC_DEBUG_OUTSIDE = float3(1.00f, 0.12f, 0.12f);
static const float3 STATIC_DEBUG_SHADOWED = float3(0.20f, 0.45f, 1.00f);
static const float3 STATIC_DEBUG_LIT = float3(0.25f, 1.00f, 0.45f);

float3 ApplyDirShadowDebug(float3 lit, float4 position, DirLight light, int index)
{
    if (!(light.flags & DIR_LIGHT_FLAG_DEBUG_STATIC)) {
        return ApplyDirCascadeDebug(lit, position, light, index);
    }
    float3 uvz = float3(0.0f, 0.0f, 0.0f);
    if (!(light.flags & DIR_LIGHT_FLAG_STATIC_SHADOW) ||
        !ProjectDirStatic(position, light, index, uvz)) {
        return STATIC_DEBUG_OUTSIDE * (CASCADE_DEBUG_FLOOR + lit);
    }
    float s = DirStaticShadowPCFFAST(position, light, index);
    return lerp(STATIC_DEBUG_SHADOWED, STATIC_DEBUG_LIT, saturate(s)) *
           (CASCADE_DEBUG_FLOOR + lit);
}
#else
float DirShadowPCFFASTAll(float4 position, DirLight light, int index)
{
    return DirShadowPCFFAST(position, light, index);
}

//No static map in this shader (see above), so the static debug view has nothing to
//show and falls back to the cascade one.
float3 ApplyDirShadowDebug(float3 lit, float4 position, DirLight light, int index)
{
    return ApplyDirCascadeDebug(lit, position, light, index);
}
#endif

float3 HitAmbient(float3 normal)
{
    // Convert from [-1, 1] to [0, 1]
    float up = normal.y * 0.5 + 0.5;
    // Calculate the ambient value
    float3 Ambient = ambientLight.AmbientDown + up * ambientLight.AmbientUp;
    // Apply the ambient value to the color
    return Ambient;
}

float3 HitDirectionalNoShadow(float3 normal, float3 position, DirLight light, int index)
{
    float3 color = light.Color.rgb * light.intensity;
    // Phong diffuse
    float NDotL = dot(light.DirToLight, normal);
    float3 finalColor = color * saturate(NDotL);
    return finalColor;
}

float3 HitDirectional(float3 normal, float3 position, DirLight light, int index)
{
    float3 color = light.Color.rgb * light.intensity;
    // Phong diffuse
    float NDotL = dot(light.DirToLight, normal);
    float3 finalColor = color * saturate(NDotL);
    if (light.cast_shadow) {
        float shadow = DirShadowPCFFASTAll(float4(position, 1.0f), light, index);
        finalColor *= shadow;
    }
    return ApplyDirShadowDebug(finalColor, float4(position, 1.0f), light, index);
}

float3 HitPoint(float3 normal, float3 position, PackedLight light)
{
    float3 finalColor = { 0.f, 0.f, 0.f };
    float3 lposition = light.position;
    float3 ToLight = lposition - position;
    float DistToLight = length(ToLight);
    // Phong diffuse
    ToLight /= max(DistToLight, 1e-5f); // Normalize
    float NDotL = saturate(dot(ToLight, normal));
    finalColor = UnpackLightColor(light) * NDotL;

    // Attenuation
    float LightRange = (light.range - DistToLight) / light.range;
    float DistToLightNorm = LightRange;
    float Attn = saturate(DistToLightNorm * DistToLightNorm);
    Attn *= PackedSpotFactor(light, position - lposition);
    int slot = LightShadowSlot(light);
    if (slot >= 0 && Attn > 0.0f) {
        float shadow = PointShadowPCFFAST(position - lposition, slot);
        finalColor *= shadow;
    }
    finalColor *= Attn;
    return finalColor;
}
