#include "../Common/ShaderStructs.hlsli"
#include "../Common/PixelCommon.hlsli"

#define MAX_OBJECTS 64
#define MAX_STACK_SIZE 64

#define SAMPLES_PER_PIXEL 4
static const float T_MIN = 0.0001f;
static const float T_MAX = 1000.0f;

struct BVHNode
{
	//--
	float4 reg0; // aabb_min + left_child + right_child;
	//--
	float4 reg1; // aabb_max + index;
};

struct Ray {
	float4 orig;
	float3 dir;
    float density;
    uint bounces;
    float ratio;
	float t; //intersection distance    
};

struct RayObject {
    float4 orig;
    float3 dir;
    float t;
};

struct IntersectionResult
{
    float3 v0;
    float3 v1;
    float3 v2;

    float distance;
    float2 uv;

    uint3 vindex;
    uint object;
};

struct ObjectInfo
{
    matrix world;
    matrix inv_world;

	float3 aabb_min;
    uint objectOffset;

	float3 aabb_max;
    uint vertexOffset;

    float3 position;
    uint indexOffset;

    float density;
    float opacity;
    float padding0;
    float padding1;
};

cbuffer externalData : register(b0)
{
    float time;
    float3 cameraPosition;
    float3 cameraDirection;
    //Lights
    AmbientLight ambientLight;
	DirLight dirLights[MAX_LIGHTS];
	PointLight pointLights[MAX_LIGHTS];
	uint dirLightsCount;
	uint pointLightsCount;

	float4 LightPerspectiveValues[MAX_LIGHTS / 2];
	matrix DirPerspectiveMatrix[MAX_LIGHTS];
}

cbuffer objectData : register(b1)
{
    BVHNode objectBVH[MAX_OBJECTS * 2 - 1];
    uint nobjects;
    ObjectInfo objectInfos[MAX_OBJECTS];
    MaterialColor objectMaterials[MAX_OBJECTS];
}

StructuredBuffer<BVHNode> objects: register(t2);
ByteAddressBuffer vertexBuffer : register(t3);
ByteAddressBuffer indicesBuffer : register(t4);

RWTexture2D<float4> output0 : register(u0);
RWTexture2D<float4> output1 : register(u1);

RWTexture2D<float> props : register(u3);
RWTexture2D<float4> ray0 : register(u4);
RWTexture2D<float4> ray1 : register(u5);
RWTexture2D<float4> bloom : register(u6);

Texture2D<float4> DiffuseTextures[MAX_OBJECTS];
Texture2D<float> DirShadowMapTexture[MAX_LIGHTS];
TextureCube<float> PointShadowMapTexture[MAX_LIGHTS];

//Packed array
static float2 lps[MAX_LIGHTS] = (float2[MAX_LIGHTS])LightPerspectiveValues;

bool is_leaf(BVHNode node)
{
	return (asuint(node.reg0.w) == 0);
}

float3 aabb_min(BVHNode node)
{
	return float3(node.reg0.x, node.reg0.y, node.reg0.z);
}

float3 aabb_max(BVHNode node)
{
	return float3(node.reg1.x, node.reg1.y, node.reg1.z);
}

uint left_child(BVHNode node)
{
	return (asuint(node.reg0.w) & 0xffff0000) >> 16;
}

uint right_child(BVHNode node)
{
	return (asuint(node.reg0.w) & 0xffff);
}

uint index(BVHNode node)
{
	return asuint(node.reg1.w);
}

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

float3 CalcDirectional(float3 normal, float3 position, MaterialColor material, DirLight light, int index)
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

float3 CalcPoint(float3 normal, float3 position, MaterialColor material, PointLight light, int index)
{
    float3 finalColor = { 0.f, 0.f, 0.f };
    float3 lposition = light.Position;
    float lum = (light.Color.r + light.Color.g + light.Color.b) / 3.0f;
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
        float shadow = PointShadowPCFFAST(position - light.Position, light, index);
        finalColor *= shadow;
    }
    finalColor *= Attn;
    return finalColor;
}

bool IntersectTri(RayObject ray, uint indexOffset, uint vertexOffset, out IntersectionResult result)
{
    const uint indexByteOffset = indexOffset * 4;
    const uint vertexSize = 96;

    uint3 vindex = (vertexOffset + indicesBuffer.Load3(indexByteOffset)) * vertexSize;
    float3 v0 = asfloat(vertexBuffer.Load3(vindex.x));
    float3 v1 = asfloat(vertexBuffer.Load3(vindex.y));
    float3 v2 = asfloat(vertexBuffer.Load3(vindex.z));

    const float3 edge1 = v1 - v0;
    const float3 edge2 = v2 - v0;
    const float3 h = cross(ray.dir, edge2);
    const float a = dot(edge1, h);

    // Check if the ray is parallel to the triangle
    if (abs(a) < Epsilon)
    {
        return false;
    }

    const float f = 1.0f / a;
    const float3 s = ray.orig.xyz - v0;
    const float u = f * dot(s, h);

    if (u < 0 || u > 1) {
        return false;
    }

    const float3 q = cross(s, edge1);
    const float v = f * dot(ray.dir, q);

    if (v < 0 || u + v > 1) {
        return false;
    }

    const float t = f * dot(edge2, q);

    // Check if the intersection is within the valid range along the ray
    if (t > 0)
    {
        result.v0 = v0;
        result.v1 = v1;
        result.v2 = v2;
        result.vindex = vindex;
        result.object = 0;
        result.distance = t;
        result.uv = float2(u, v);
        return true;
    }

    return false;
}

bool IntersectAABB(Ray ray, float3 bmin, float3 bmax)
{
    float tx1 = (bmin.x - ray.orig.x) / ray.dir.x;
    float tx2 = (bmax.x - ray.orig.x) / ray.dir.x;
    float tmin = min(tx1, tx2);
    float tmax = max(tx1, tx2);

    float ty1 = (bmin.y - ray.orig.y) / ray.dir.y;
    float ty2 = (bmax.y - ray.orig.y) / ray.dir.y;
    tmin = max(tmin, min(ty1, ty2));
    tmax = min(tmax, max(ty1, ty2));

    float tz1 = (bmin.z - ray.orig.z) / ray.dir.z;
    float tz2 = (bmax.z - ray.orig.z) / ray.dir.z;
    tmin = max(tmin, min(tz1, tz2));
    tmax = min(tmax, max(tz1, tz2));

    return tmax >= tmin && tmax > 0 && ray.t > tmax;
}

bool IntersectAABB(RayObject ray, float3 bmin, float3 bmax)
{
	float tx1 = (bmin.x - ray.orig.x) / ray.dir.x;
	float tx2 = (bmax.x - ray.orig.x) / ray.dir.x;
	float tmin = min(tx1, tx2);
	float tmax = max(tx1, tx2);

	float ty1 = (bmin.y - ray.orig.y) / ray.dir.y;
	float ty2 = (bmax.y - ray.orig.y) / ray.dir.y;
	tmin = max(tmin, min(ty1, ty2));
	tmax = min(tmax, max(ty1, ty2));

	float tz1 = (bmin.z - ray.orig.z) / ray.dir.z;
	float tz2 = (bmax.z - ray.orig.z) / ray.dir.z;
	tmin = max(tmin, min(tz1, tz2));
	tmax = min(tmax, max(tz1, tz2));

	return tmax >= tmin && tmax > 0 && ray.t > tmax;
}

bool IntersectAABB(Ray ray, BVHNode node)
{
    float3 bmin = aabb_min(node);
    float3 bmax = aabb_max(node);
    return IntersectAABB(ray, bmin, bmax);
}

bool IntersectAABB(RayObject ray, BVHNode node)
{
	float3 bmin = aabb_min(node);
	float3 bmax = aabb_max(node);
	return IntersectAABB(ray, bmin, bmax);
}

float3 GetDiffuseColor(uint object, float2 uv)
{
    switch (object)
    { 
    case 0: return DiffuseTextures[0].SampleLevel(basicSampler, uv, 0).xyz;
    case 1: return DiffuseTextures[1].SampleLevel(basicSampler, uv, 0).xyz;
    case 2: return DiffuseTextures[2].SampleLevel(basicSampler, uv, 0).xyz;
    case 3: return DiffuseTextures[3].SampleLevel(basicSampler, uv, 0).xyz;
    case 4: return DiffuseTextures[4].SampleLevel(basicSampler, uv, 0).xyz;
    case 5: return DiffuseTextures[5].SampleLevel(basicSampler, uv, 0).xyz;
    case 6: return DiffuseTextures[6].SampleLevel(basicSampler, uv, 0).xyz;
    case 7: return DiffuseTextures[7].SampleLevel(basicSampler, uv, 0).xyz;
    case 8: return DiffuseTextures[8].SampleLevel(basicSampler, uv, 0).xyz;
    case 9: return DiffuseTextures[9].SampleLevel(basicSampler, uv, 0).xyz;
    case 10: return DiffuseTextures[10].SampleLevel(basicSampler, uv, 0).xyz;
    case 11: return DiffuseTextures[11].SampleLevel(basicSampler, uv, 0).xyz;
    case 12: return DiffuseTextures[12].SampleLevel(basicSampler, uv, 0).xyz;
    case 13: return DiffuseTextures[13].SampleLevel(basicSampler, uv, 0).xyz;
    case 14: return DiffuseTextures[14].SampleLevel(basicSampler, uv, 0).xyz;
    case 15: return DiffuseTextures[15].SampleLevel(basicSampler, uv, 0).xyz;
    case 16: return DiffuseTextures[16].SampleLevel(basicSampler, uv, 0).xyz;
    case 17: return DiffuseTextures[17].SampleLevel(basicSampler, uv, 0).xyz;
    case 18: return DiffuseTextures[18].SampleLevel(basicSampler, uv, 0).xyz;
    case 19: return DiffuseTextures[19].SampleLevel(basicSampler, uv, 0).xyz;
    case 20: return DiffuseTextures[20].SampleLevel(basicSampler, uv, 0).xyz;
    case 21: return DiffuseTextures[21].SampleLevel(basicSampler, uv, 0).xyz;
    case 22: return DiffuseTextures[22].SampleLevel(basicSampler, uv, 0).xyz;
    case 23: return DiffuseTextures[23].SampleLevel(basicSampler, uv, 0).xyz;
    case 24: return DiffuseTextures[24].SampleLevel(basicSampler, uv, 0).xyz;
    case 25: return DiffuseTextures[25].SampleLevel(basicSampler, uv, 0).xyz;
    case 26: return DiffuseTextures[26].SampleLevel(basicSampler, uv, 0).xyz;
    case 27: return DiffuseTextures[27].SampleLevel(basicSampler, uv, 0).xyz;
    case 28: return DiffuseTextures[28].SampleLevel(basicSampler, uv, 0).xyz;
    case 29: return DiffuseTextures[29].SampleLevel(basicSampler, uv, 0).xyz;
    case 30: return DiffuseTextures[30].SampleLevel(basicSampler, uv, 0).xyz;
    case 31: return DiffuseTextures[31].SampleLevel(basicSampler, uv, 0).xyz;
    case 32: return DiffuseTextures[32].SampleLevel(basicSampler, uv, 0).xyz;
    case 33: return DiffuseTextures[33].SampleLevel(basicSampler, uv, 0).xyz;
    case 34: return DiffuseTextures[34].SampleLevel(basicSampler, uv, 0).xyz;
    case 35: return DiffuseTextures[35].SampleLevel(basicSampler, uv, 0).xyz;
    case 36: return DiffuseTextures[36].SampleLevel(basicSampler, uv, 0).xyz;
    case 37: return DiffuseTextures[37].SampleLevel(basicSampler, uv, 0).xyz;
    case 38: return DiffuseTextures[38].SampleLevel(basicSampler, uv, 0).xyz;
    case 39: return DiffuseTextures[39].SampleLevel(basicSampler, uv, 0).xyz;
    case 40: return DiffuseTextures[40].SampleLevel(basicSampler, uv, 0).xyz;
    case 41: return DiffuseTextures[41].SampleLevel(basicSampler, uv, 0).xyz;
    case 42: return DiffuseTextures[42].SampleLevel(basicSampler, uv, 0).xyz;
    case 43: return DiffuseTextures[43].SampleLevel(basicSampler, uv, 0).xyz;
    case 44: return DiffuseTextures[44].SampleLevel(basicSampler, uv, 0).xyz;
    case 45: return DiffuseTextures[45].SampleLevel(basicSampler, uv, 0).xyz;
    case 46: return DiffuseTextures[46].SampleLevel(basicSampler, uv, 0).xyz;
    case 47: return DiffuseTextures[47].SampleLevel(basicSampler, uv, 0).xyz;
    case 48: return DiffuseTextures[48].SampleLevel(basicSampler, uv, 0).xyz;
    case 49: return DiffuseTextures[49].SampleLevel(basicSampler, uv, 0).xyz;
    case 50: return DiffuseTextures[50].SampleLevel(basicSampler, uv, 0).xyz;
    case 51: return DiffuseTextures[51].SampleLevel(basicSampler, uv, 0).xyz;
    case 52: return DiffuseTextures[52].SampleLevel(basicSampler, uv, 0).xyz;
    case 53: return DiffuseTextures[53].SampleLevel(basicSampler, uv, 0).xyz;
    case 54: return DiffuseTextures[54].SampleLevel(basicSampler, uv, 0).xyz;
    case 55: return DiffuseTextures[55].SampleLevel(basicSampler, uv, 0).xyz;
    case 56: return DiffuseTextures[56].SampleLevel(basicSampler, uv, 0).xyz;
    case 57: return DiffuseTextures[57].SampleLevel(basicSampler, uv, 0).xyz;
    case 58: return DiffuseTextures[58].SampleLevel(basicSampler, uv, 0).xyz;
    case 59: return DiffuseTextures[59].SampleLevel(basicSampler, uv, 0).xyz;
    case 60: return DiffuseTextures[60].SampleLevel(basicSampler, uv, 0).xyz;
    case 61: return DiffuseTextures[61].SampleLevel(basicSampler, uv, 0).xyz;
    case 62: return DiffuseTextures[62].SampleLevel(basicSampler, uv, 0).xyz;
    case 63: return DiffuseTextures[63].SampleLevel(basicSampler, uv, 0).xyz;
    default:
        return float3(0.0f, 0.0f, 0.0f);
    }
}

Ray GetReflectedRayFromSource(RaySource source)
{
    Ray ray;
    ray.orig = float4(source.orig, 1.0f);
    float3 in_dir = normalize(source.orig - cameraPosition);
    float3 out_dir = reflect(in_dir, source.normal);
    ray.dir = normalize(out_dir);
    ray.orig.xyz += ray.dir * 0.01f;
    ray.density = source.density;
    ray.bounces = 1;
    ray.ratio = 1.0f;
    ray.t = FLT_MAX;

    return ray;
}

Ray GetRefractedRayFromSource(RaySource source)
{
    Ray ray;
    ray.orig = float4(source.orig, 1.0f);
    float3 in_dir = normalize(source.orig - cameraPosition);
    float3 out_dir = refract(in_dir, source.normal, 1.0 / source.density);
    ray.dir = normalize(out_dir);
    ray.orig.xyz += ray.dir * 0.01f;
    ray.density = source.density;
    ray.ratio = 1.0f - source.opacity;
    ray.bounces = 0;
    ray.t = FLT_MAX;

    return ray;
}

Ray GetReflectedRayFromRay(Ray source, float3 normal, float ratio)
{
    Ray ray;
    ray.orig = source.orig;
    float3 out_dir = reflect(source.dir, normal);
    ray.dir = normalize(out_dir);
    ray.orig.xyz += ray.dir * 0.01f;
    ray.density = source.density;
    ray.bounces = source.bounces + 1;
    ray.ratio = ratio;
    ray.t = FLT_MAX;

    return ray;
}

Ray GetRefractedRayFromRay(Ray source, float in_density, float out_density, float3 normal, float ratio)
{
    Ray ray;
    ray.orig = source.orig;
    float3 out_dir = refract(source.dir, normal, in_density / out_density);
    ray.dir = normalize(out_dir);
    ray.orig.xyz += ray.dir * 0.1f;
    ray.density = out_density;
    ray.bounces = source.bounces + 1;
    ray.ratio = ratio;
    ray.t = FLT_MAX;

    return ray;
}

#if 0
float3 GetColor(Ray origRay, out float3 bloom)
{
	
    static const float max_distance = 500.0f;
    
    uint volumeStack[MAX_STACK_SIZE];
    uint stack[MAX_STACK_SIZE];
    bool collide = false;
    IntersectionResult result;

    Ray ray = origRay;
    float3 finalColor = float3(0.0f, 0.0f, 0.0f);
    bloom = float3(0.0f, 0.0f, 0.0f);

    bool end = false;
    while (!end)
    {
        result.distance = FLT_MAX;
        end = true;
        collide = false;

        int volumeStackSize = 0;
        volumeStack[volumeStackSize++] = 0;
        while (volumeStackSize > 0 && volumeStackSize < MAX_STACK_SIZE)
        {
            uint currentVolume = volumeStack[--volumeStackSize];

            BVHNode volumeNode = objectBVH[currentVolume];
            if (is_leaf(volumeNode))
            {
                int objectIndex = index(volumeNode);
                ObjectInfo o = objectInfos[objectIndex];

                float objectExtent = 4.0f * length(o.aabb_max - o.aabb_min);
                float distanceToObject = length(o.position - cameraPosition) - objectExtent;
                if (distanceToObject < max_distance && distanceToObject < result.distance)
                {
                    RayObject oray;
                    IntersectionResult object_result;
                    object_result.distance = FLT_MAX;

                    // Transform the ray direction from world space to object space
                    oray.orig = mul(ray.orig, o.inv_world);
                    oray.orig /= oray.orig.w;
                    oray.dir = normalize(mul(ray.dir, (float3x3) o.inv_world));
                    oray.t = FLT_MAX;

                    int stackSize = 0;
                    stack[stackSize++] = 0;

                    while (stackSize > 0 && stackSize < MAX_STACK_SIZE)
                    {
                        uint current = stack[--stackSize];

                        BVHNode node = objects[o.objectOffset + current];
                        if (is_leaf(node))
                        {
                            float t;
                            uint idx = index(node);
                            idx += o.indexOffset;

                            IntersectionResult tmp_result;
                            tmp_result.distance = FLT_MAX;
                            if (IntersectTri(oray, idx, o.vertexOffset, tmp_result))
                            {
                                tmp_result.distance /= determinant(o.world);
                                if (tmp_result.distance < result.distance && tmp_result.distance < object_result.distance)
                                {
                                    object_result.v0 = tmp_result.v0;
                                    object_result.v1 = tmp_result.v1;
                                    object_result.v2 = tmp_result.v2;
                                    object_result.vindex = tmp_result.vindex;
                                    object_result.distance = tmp_result.distance;
                                    object_result.uv = tmp_result.uv;
                                    object_result.object = objectIndex;
                                }
                            }
                        }
                        else if (IntersectAABB(oray, node))
                        {
                            stack[stackSize++] = left_child(node);
                            stack[stackSize++] = right_child(node);
                        }
                    }

                    if (object_result.distance > Epsilon && object_result.distance < result.distance)
                    {
                        collide = true;
                        ray.t = object_result.distance;
                        result.v0 = object_result.v0;
                        result.v1 = object_result.v1;
                        result.v2 = object_result.v2;
                        result.vindex = object_result.vindex;
                        result.distance = object_result.distance;
                        result.uv = object_result.uv;
                        result.object = objectIndex;
                    }
                }
            }
            else if (IntersectAABB(ray, volumeNode))
            {
                volumeStack[volumeStackSize++] = left_child(volumeNode);
                volumeStack[volumeStackSize++] = right_child(volumeNode);
            }
        }

        //At this point we have the ray collision distance and a collision result
        if (collide) {
            // Calculate space position
            ObjectInfo o = objectInfos[result.object];
            float3 normal0 = asfloat(vertexBuffer.Load3(result.vindex.x + 12));
            float3 normal1 = asfloat(vertexBuffer.Load3(result.vindex.y + 12));
            float3 normal2 = asfloat(vertexBuffer.Load3(result.vindex.z + 12));
            float3 opos = (1.0f - result.uv.x - result.uv.y) * result.v0 + result.uv.x * result.v1 + result.uv.y * result.v2;
            float3 normal = (1.0f - result.uv.x - result.uv.y) * normal0 + result.uv.x * normal1 + result.uv.y * normal2;

            normal = normalize(mul(normal, (float3x3)o.world));
            float4 pos = mul(float4(opos, 1.0f), o.world);
            pos /= pos.w;
            MaterialColor material = objectMaterials[result.object];

            float3 color = float3(0.0f, 0.0f, 0.0f);

            color += CalcAmbient(normal);

            uint i;
            for (i = 0; i < dirLightsCount; ++i) {
                color += CalcDirectional(normal, pos.xyz, material, dirLights[i], i);
            }

            // Calculate the point lights
            for (i = 0; i < pointLightsCount; ++i) {
                if (length(pos.xyz - pointLights[i].Position) < pointLights[i].Range) {
                    color += CalcPoint(normal, pos.xyz, material, pointLights[i], i);
                }
            }

            //Calculate material color
            if (material.flags & DIFFUSSE_MAP_ENABLED_FLAG) {
                float2 uv0 = asfloat(vertexBuffer.Load2(result.vindex.x + 24));
                float2 uv1 = asfloat(vertexBuffer.Load2(result.vindex.y + 24));
                float2 uv2 = asfloat(vertexBuffer.Load2(result.vindex.z + 24));
                float2 uv = uv0 * (1.0f - result.uv.x - result.uv.y) + uv1 * result.uv.x + uv2 * result.uv.y;
                color *= GetDiffuseColor(result.object, uv);
            }
            else {
                color *= material.diffuseColor.rgb;
            }

            float4 emission = material.emission * float4(material.emission_color, 1.0f);
            bloom += emission;

            finalColor += color * ray.ratio * o.opacity;
            ray.orig = pos;

            //If not opaque surface, generate a refraction ray
            if (ray.bounces < 5 && o.opacity < 1.0f) {
                if (ray.bounces % 2 == 0) {
                    normal = -normal;
                }
                ray = GetRefractedRayFromRay(ray, ray.density,
                    ray.density != 1.0 ? 1.0f : o.density,
                    normal, ray.ratio * (1.0f - o.opacity));
                end = false;
            }
        }
        else {
            //Color background

        }
    }
    return finalColor;
}
#else
float3 GetColor(Ray origRay, out float3 bloom)
{

    static const float max_distance = 500.0f;

    uint stack[MAX_STACK_SIZE];
    bool collide = false;
    IntersectionResult result;

    Ray ray = origRay;
    float3 finalColor = float3(0.0f, 0.0f, 0.0f);
    bloom = float3(0.0f, 0.0f, 0.0f);

    bool end = false;
    while (!end)
    {
        result.distance = FLT_MAX;
        end = true;
        collide = false;
        for (uint i = 0; i < nobjects; ++i)
        {
            ObjectInfo o = objectInfos[i];
            float objectExtent = 4.0f * length(o.aabb_max - o.aabb_min);
            float distanceToObject = length(o.position - cameraPosition) - objectExtent;

            if (distanceToObject < max_distance && distanceToObject < result.distance && IntersectAABB(ray, o.aabb_min, o.aabb_max))
            {
                RayObject oray;
                IntersectionResult object_result;
                object_result.distance = FLT_MAX;

                // Transform the ray direction from world space to object space
                oray.orig = mul(ray.orig, o.inv_world);
                oray.orig /= oray.orig.w;
                oray.dir = normalize(mul(ray.dir, (float3x3) o.inv_world));
                oray.t = FLT_MAX;

                int stackSize = 0;
                stack[stackSize++] = 0;

                while (stackSize > 0 && stackSize < MAX_STACK_SIZE)
                {
                    uint current = stack[--stackSize];

                    BVHNode node = objects[o.objectOffset + current];
                    if (is_leaf(node))
                    {
                        float t;
                        uint idx = index(node);
                        idx += o.indexOffset;

                        IntersectionResult tmp_result;
                        tmp_result.distance = FLT_MAX;
                        if (IntersectTri(oray, idx, o.vertexOffset, tmp_result))
                        {
                            tmp_result.distance /= determinant(o.world);
                            if (tmp_result.distance < result.distance && tmp_result.distance < object_result.distance)
                            {
                                object_result.v0 = tmp_result.v0;
                                object_result.v1 = tmp_result.v1;
                                object_result.v2 = tmp_result.v2;
                                object_result.vindex = tmp_result.vindex;
                                object_result.distance = tmp_result.distance;
                                object_result.uv = tmp_result.uv;
                                object_result.object = i;
                            }
                        }
                    }
                    else if (IntersectAABB(oray, node))
                    {
                        stack[stackSize++] = left_child(node);
                        stack[stackSize++] = right_child(node);
                    }
                }

                if (object_result.distance > Epsilon && object_result.distance < result.distance)
                {
                    collide = true;
                    ray.t = object_result.distance;
                    result.v0 = object_result.v0;
                    result.v1 = object_result.v1;
                    result.v2 = object_result.v2;
                    result.vindex = object_result.vindex;
                    result.distance = object_result.distance;
                    result.uv = object_result.uv;
                    result.object = i;
                }
            }
        }

        //At this point we have the ray collision distance and a collision result
        if (collide) {
            // Calculate space position
            ObjectInfo o = objectInfos[result.object];
            float3 normal0 = asfloat(vertexBuffer.Load3(result.vindex.x + 12));
            float3 normal1 = asfloat(vertexBuffer.Load3(result.vindex.y + 12));
            float3 normal2 = asfloat(vertexBuffer.Load3(result.vindex.z + 12));
            float3 opos = (1.0f - result.uv.x - result.uv.y) * result.v0 + result.uv.x * result.v1 + result.uv.y * result.v2;
            float3 normal = (1.0f - result.uv.x - result.uv.y) * normal0 + result.uv.x * normal1 + result.uv.y * normal2;

            normal = normalize(mul(normal, (float3x3)o.world));
            float4 pos = mul(float4(opos, 1.0f), o.world);
            pos /= pos.w;
            MaterialColor material = objectMaterials[result.object];

            float3 color = float3(0.0f, 0.0f, 0.0f);

            color += CalcAmbient(normal);

            for (i = 0; i < dirLightsCount; ++i) {
                color += CalcDirectional(normal, pos.xyz, material, dirLights[i], i);
            }

            // Calculate the point lights
            for (i = 0; i < pointLightsCount; ++i) {
                if (length(pos.xyz - pointLights[i].Position) < pointLights[i].Range) {
                    color += CalcPoint(normal, pos.xyz, material, pointLights[i], i);
                }
            }

            //Calculate material color
            if (material.flags & DIFFUSSE_MAP_ENABLED_FLAG) {
                float2 uv0 = asfloat(vertexBuffer.Load2(result.vindex.x + 24));
                float2 uv1 = asfloat(vertexBuffer.Load2(result.vindex.y + 24));
                float2 uv2 = asfloat(vertexBuffer.Load2(result.vindex.z + 24));
                float2 uv = uv0 * (1.0f - result.uv.x - result.uv.y) + uv1 * result.uv.x + uv2 * result.uv.y;
                color *= GetDiffuseColor(result.object, uv);
            }
            else {
                color *= material.diffuseColor.rgb;
            }

            float4 emission = material.emission * float4(material.emission_color, 1.0f);
            bloom += emission;

            finalColor += color * ray.ratio * o.opacity;
            ray.orig = pos;

            //If not opaque surface, generate a refraction ray
            if (ray.bounces < 5 && o.opacity < 1.0f) {
                if (ray.bounces % 2 == 0) {
                    normal = -normal;
                }
                ray = GetRefractedRayFromRay(ray, ray.density,
                    ray.density != 1.0 ? 1.0f : o.density,
                    normal, ray.ratio * (1.0f - o.opacity));
                end = false;
            }
        }
        else {
            //Color background

        }
    }
    return finalColor;
}
#endif
#define NTHREADS 32
#define DENSITY 1.0f

[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 dimensions;
    uint2 ray_map_dimensions;
    {
        uint w, h;
        output0.GetDimensions(w, h);
        dimensions.x = w;
        dimensions.y = h;

        ray0.GetDimensions(w, h);
        ray_map_dimensions.x = w;
        ray_map_dimensions.y = h;
    }

    float x = (float)DTid.x;
    float y = (float)DTid.y;

    uint2 rayMapRatio = ray_map_dimensions / dimensions;

    float4 color0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 color1 = float4(0.0f, 0.0f, 0.0f, 0.0f);

    uint2 pixel;
    uint2 ray_pixel;
    pixel = float2(x, y);
    ray_pixel = float2(x * rayMapRatio.x, y * rayMapRatio.y);

    RaySource ray_source = fromColor(ray0[ray_pixel], ray1[ray_pixel]);
    props[pixel] = ray_source.dispersion;
    if (ray_source.dispersion < Epsilon || ray_source.dispersion == 1.0f) {
        return;
    }
    
    if (length(ray_source.normal) < Epsilon)
    {
        return;
    }

    float3 bloomColor = { 0.0f, 0.0f, 0.0f };
    //Reflected ray
    if (ray_source.opacity > Epsilon) {
        Ray ray = GetReflectedRayFromSource(ray_source);
        if (length(ray.dir) > Epsilon)
        {
            color0 = float4(GetColor(ray, bloomColor), 1.0f);
        }
    }

    //Refracted ray
    if (ray_source.opacity < 1.0f) {
        Ray ray = GetRefractedRayFromSource(ray_source);
        if (length(ray.dir) > Epsilon)
        {
            color1 = float4(GetColor(ray, bloomColor), 1.0f) * (1.0f - ray_source.opacity);
        }
    }

    output0[pixel] = color0;
    output1[pixel] = color1;
    bloom[pixel] = float4(bloomColor, 1.0f);
}
