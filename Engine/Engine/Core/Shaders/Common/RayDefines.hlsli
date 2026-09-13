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

#ifndef RAY_DEFINES_
#define RAY_DEFINES_

#define MAX_OBJECTS 100
#define MAX_STACK_SIZE 20
//Depth budget for the top-level (object) hierarchy, which holds at most
//MAX_OBJECTS leaves and so needs far less than a mesh's triangle BVH. Only used
//when USE_OBH is on, which it is not - see the note in GIRayTraceCS.hlsl.
#define MAX_VOLUME_STACK_SIZE 16

//Ceiling on how many mesh-BVH nodes a single candidate object's traversal may
//visit (GIRayTraceCS.hlsl and RayTraceCS.hlsl both descend with this pattern).
//MAX_STACK_SIZE only bounds how deep the explicit stack gets, not how many
//nodes a traversal touches in total - a ray whose max_distance-based prune
//(`go_left`/`go_right`) keeps re-admitting siblings can walk a long, shallow
//chain across most of a large mesh's tree without the stack ever growing past
//a handful of entries. Found by bisection: on a 326K-triangle mesh (no LOD)
//with a small-scaled, awkwardly-positioned static object, a single ray's
//descent ran long enough to trip the GPU driver's hang timeout
//(DXGI_ERROR_DEVICE_HUNG) and take the device down - a hard cap here (verified
//to clear the hang at 2000, given headroom here) bounds worst-case cost to a
//small constant instead of the size of the mesh, at the price of that one ray
//occasionally missing a hit beyond the cap, exactly the tradeoff already made
//for a stack overflow a few lines below.
#define MAX_NODE_VISITS 8192

//#define PACK_RAYS_8
#define RAY_W_SCALE 1.0f
#define RAY_W_BIAS 0.0001f

#ifdef PACK_RAYS_8
#define MAX_RAYS 8
#define PackRays Pack8Bytes
#define UnpackRays Unpack8Bytes
#else 
#define MAX_RAYS 16
#define PackRays Pack16Bytes
#define UnpackRays Unpack16Bytes
#endif

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

//What a hit is, in the fewest registers that still describe it: which triangle
//(vertex byte offsets, which the caller reloads positions/normals/UVs from),
//where in it, and how far. The three vertex positions used to live here too -
//see the note on IntersectTri.
struct IntersectionResult
{
    uint3 vindex;
    float distance;

    float u;
    float v;
    uint object;
    float padding;
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

#endif