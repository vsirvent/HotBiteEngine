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

//#define PACK_RAYS_8
#define RAY_W_SCALE 1.0f
#define RAY_W_BIAS 0.00001f

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

#define MAX_RAY_INPUTS 4
#define MAX_RAY_INPUTS_POS MAX_RAY_INPUTS / 2
static const float MAX_RAY_POLAR_DIR = 2.0f * PI; //Maximum polar angle 2.0 * PI

struct InputRays {
    float4 dir2[MAX_RAY_INPUTS_POS];
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
    float distance;
    
    float3 v1;
    uint object; 
    
    float3 v2;
    float  u;

    uint3 vindex;
    float  v;    
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