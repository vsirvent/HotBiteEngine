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
#include "RayDefines.hlsli"

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

    return tmax >= tmin && tmax > 0.0f;
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

    return tmax >= tmin && tmax > 0.0f;
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