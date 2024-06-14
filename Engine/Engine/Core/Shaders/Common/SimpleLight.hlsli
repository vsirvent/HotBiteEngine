float PointShadowPCFFAST(float3 ToPixel, PointLight light, int index)
{
    float3 ToPixelAbs = abs(ToPixel);
    float Z = max(ToPixelAbs.x, max(ToPixelAbs.y, ToPixelAbs.z));
    float d = (lps[index].x * Z + lps[index].y) / Z;
    //This offset allows to avoid self shadow
    d -= 0.01f;
    float att1 = PointShadowMapTexture[index].SampleCmpLevelZero(PCFSampler, float3(ToPixel.x, ToPixel.y, ToPixel.z), d).r;
    return saturate(att1);
}

float PointShadowPCF(float3 ToPixel, PointLight light, int index)
{
    float3 ToPixelAbs = abs(ToPixel);
    float Z = max(ToPixelAbs.x, max(ToPixelAbs.y, ToPixelAbs.z));
    float d = ((lps[index].x * Z + lps[index].y) / Z);
    //This offset allows to avoid self shadow
    d -= 0.01f;
    float step = 0.02f;
    float att1 = 0.0f;
    float count = 0.00001f;
    for (float x = -0.06f; x < 0.06f; x += step) {
        for (float y = -0.06f; y < 0.06f; y += step) {
            att1 += PointShadowMapTexture[index].SampleCmpLevelZero(PCFSampler, float3(ToPixel.x + x, ToPixel.y + y, ToPixel.z), d).r;
            count += 1.0f;
        }
    }
    att1 /= count;
    return saturate(att1);
}

float DirShadowPCF(float4 position, DirLight light, int index)
{
    float4 p = mul(position, DirPerspectiveMatrix[index]);
    p /= p.w;
    p.x = (p.x + 1.0f) / 2.0f;
    p.y = 1.0f - ((p.y + 1.0f) / 2.0f);
    if (p.x < 0.0f || p.x > 1.0f || p.y < 0.0f || p.y > 1.0f) {
        return 0.5f;
    }
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
}


float DirShadowPCFFAST(float4 position, DirLight light, int index)
{
    float4 p = mul(position, DirPerspectiveMatrix[index]);
    p /= p.w;
    p.x = (p.x + 1.0f) / 2.0f;
    p.y = 1.0f - ((p.y + 1.0f) / 2.0f);
    if (p.x < 0.0f || p.x > 1.0f || p.y < 0.0f || p.y > 1.0f) {
        return 0.5f;
    }
    float att1 = DirShadowMapTexture[index].SampleCmpLevelZero(PCFSampler, float2(p.x, p.y), p.z).r;
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

float3 CalcDirectional(float3 normal, float3 position, DirLight light, int index)
{
    float3 color = light.Color.rgb * light.intensity;
    // Phong diffuse
    float NDotL = dot(light.DirToLight, normal);
    float3 finalColor = color * saturate(NDotL);
    if (light.cast_shadow) {
        float shadow = DirShadowPCFFAST(float4(position, 1.0f), light, index);
        finalColor *= shadow;
    }
    return finalColor;
}

float3 CalcPoint(float3 normal, float3 position, PointLight light, int index)
{
    float3 finalColor = { 0.f, 0.f, 0.f };
    float3 lposition = light.Position;
    float3 ToLight = lposition - position;
    float3 ToEye = cameraPosition - position;
    float DistToLight = length(ToLight);
    // Phong diffuse
    ToLight /= DistToLight; // Normalize
    float NDotL = saturate(dot(ToLight, normal));
    finalColor = light.Color * NDotL;

    // Attenuation
    float LightRange = (light.Range - DistToLight) / light.Range;
    float DistToLightNorm = saturate(LightRange);
    float Attn = saturate(DistToLightNorm * DistToLightNorm);
    if (light.cast_shadow) {
        float shadow = PointShadowPCFFAST(light.Position - position, light, index);
        finalColor *= shadow;
    }
    finalColor *= Attn;
    return finalColor;
}