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

//The reflection / refraction tracer (RayTraceCS.hlsl) and the indirect light tracer (GIRayTraceCS.hlsl) with the hardware
//answering which triangle a ray hits: an inline ray query against the acceleration structure the engine builds from the same
//hundred objects (Core::AccelStructure, the WORLD top level), in place of walking the software hierarchy object by object.
//Included by those two, after the cbuffers and RayFunctions.hlsli, when RT_RAYQUERY is defined (RayTraceHwCS.hlsl and
//GIRayTraceHwCS.hlsl are that define and an include, built as shader model 6.5).
//
//What stays the same is everything after the hit: the hit is handed back in the IntersectionResult the software walk makes (the
//vertex offsets of its triangle, where in it, how far, which object), so the shading - normal, UV, material, lights, the radiance
//cache - is the code it always was. An instance's ID is its place in the object list (objectInfos), which is what picks the material,
//the world matrix and the texture, as it did.
#ifndef RAY_QUERY_TRACE_
#define RAY_QUERY_TRACE_

#ifdef RT_RAYQUERY

//Registered by hand: the explicit registers of the two shaders leave this one free.
RaytracingAccelerationStructure rtAccel : register(t7);

//The software tracers look at an object only if it is within max_distance of the pixel (plus its own size): a ray traced for a pixel
//ignores whatever lies farther, which is what keeps a hundred objects affordable for them and what the picture was made with. The
//hardware could look at all of them, and does not: the instances are not opaque, so the ray query hands each triangle it finds to
//the shader (CANDIDATE_NON_OPAQUE_TRIANGLE) and this decides whether it counts. RT_HW_CULL 0 skips the question: every object counts,
//the nearest hit is the answer, the query never leaves the hardware (faster, and the picture gains the light of objects farther than
//max_distance).
#ifndef RT_HW_CULL
#define RT_HW_CULL 1
#endif

//As far as a ray looks for a hit: max_distance is not a limit along the ray (see RayTraceCS.hlsl), so this is only far enough for
//any scene (a ray with no end is not valid for the hardware).
#define RT_RAY_LENGTH 4096.0f

//The pixel the rays being traced are for (set by BuildCandidateList, once a pixel).
static float3 rt_cull_origin;

void BuildCandidateList(float3 orig)
{
    rt_cull_origin = orig;
}

bool IsCandidateObject(ObjectInfo o)
{
    float objectExtent = length(o.aabb_max - o.aabb_min);
    return length(o.position - rt_cull_origin) - objectExtent < max_distance;
}

//Whether the software walk would have looked at this triangle. It drops a node of the object's hierarchy once the node's bounding sphere
//(its box's centre and half diagonal) is farther than max_distance from the ray's origin, in the object's own space, and a leaf is a
//triangle: so a triangle counts when its own box is within reach, which is not a limit along the ray - a big wall is made of big
//triangles, and the sphere of one reaches far past the ray's end (gating the ray's length instead took 80% of the indirect light out of
//sponza). `origin` is the ray's, in the object's space.
bool TriangleInReach(ObjectInfo o, uint primitive, float3 origin)
{
    const uint3 vindex = (o.vertexOffset + indicesBuffer.Load3((o.indexOffset + primitive * 3) * 4)) * 96;
    const float3 v0 = asfloat(vertexBuffer.Load3(vindex.x));
    const float3 v1 = asfloat(vertexBuffer.Load3(vindex.y));
    const float3 v2 = asfloat(vertexBuffer.Load3(vindex.z));
    const float3 bmin = min(v0, min(v1, v2));
    const float3 bmax = max(v0, max(v1, v2));
    const float extent = length(bmax - bmin) * 0.5f;
    const float3 center = (bmin + bmax) * 0.5f;
    return max(length(center - origin) - extent, 0.0f) < max_distance;
}

//The nearest triangle of the objects that count along `ray`. `result` is the software walk's: which triangle (the byte offsets of its
//three vertices), where in it, how far from the origin, which object.
bool TraceNearest(Ray ray, out IntersectionResult result)
{
    result = (IntersectionResult)0;

    RayDesc rd;
    rd.Origin = ray.orig.xyz;
    rd.Direction = ray.dir;
    rd.TMin = 0.0f;
    rd.TMax = RT_RAY_LENGTH;

#if RT_HW_CULL
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(rtAccel, RAY_FLAG_NONE, 0xFF, rd);
    while (query.Proceed())
    {
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            const ObjectInfo candidate = objectInfos[query.CandidateInstanceID()];
            if (IsCandidateObject(candidate) && TriangleInReach(candidate, query.CandidatePrimitiveIndex(), query.CandidateObjectRayOrigin()))
            {
                query.CommitNonOpaqueTriangleHit();
            }
        }
    }
#else
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(rtAccel, RAY_FLAG_FORCE_OPAQUE, 0xFF, rd);
    while (query.Proceed())
    {
    }
#endif

    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    {
        return false;
    }

    const uint object = query.CommittedInstanceID();
    const ObjectInfo o = objectInfos[object];
    //The indices of a mesh count from its own first vertex; a vertex is 96 bytes.
    const uint3 vindex = (o.vertexOffset + indicesBuffer.Load3((o.indexOffset + query.CommittedPrimitiveIndex() * 3) * 4)) * 96;
    //The weights of the second and third vertices, as the software walk's u and v.
    const float2 bary = query.CommittedTriangleBarycentrics();

    result.vindex = vindex;
    result.distance = query.CommittedRayT();
    result.u = bary.x;
    result.v = bary.y;
    result.object = object;
    return true;
}

//The unbounded array of the objects' textures (the engine's bindless table: GfxContext::CSSetBindlessSRVs). The software shaders pick
//a texture with a switch over MAX_OBJECTS cases, which is what a shader model that cannot index a texture array leaves; this one can.
Texture2D<float4> DiffuseTextures[] : register(t0, space1);

float3 GetDiffuseColor(uint object, float2 uv)
{
    return DiffuseTextures[NonUniformResourceIndex(object)].SampleLevel(basicSampler, uv, 0).xyz;
}

#endif
#endif
