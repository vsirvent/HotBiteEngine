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

//Ray traced point light shadows: is there a direct line from a surface to a light?
//
//A light flagged PACKED_LIGHT_RAYTRACED holds no shadow map. DeferredLightCS asks
//ShadowRayOccluded() once per lit pixel instead: one any-hit ray from the surface to the
//light through the scene Core::ShadowRays uploaded this frame -
//
//  srTlas     the top level hierarchy, one leaf per object, over the objects' world boxes
//  srObjects  the objects: world / inverse world matrices and where their mesh lives
//  srMeshNodes, srVertices, srIndices   every mesh's own triangle hierarchy and geometry
//             (the buffers the reflection / indirect light tracers read)
//
//Any hit ends the trace, so there is no nearest-hit ordering and no hit shading: the cost
//is the walk down to the first triangle in the way, and for a lit pixel that is usually
//the whole walk, since most rays are not blocked.
//
//Includer: Defines.hlsli before this file, and the cbuffer constants `shadow_ray_objects`
//(0 turns every ray into "not occluded"), `shadow_ray_radius` (how far the ray's target
//wanders round the light, for soft edges; 0 is a hard shadow), `shadow_ray_sun_spread` (the
//same for a directional light, as a cone) and `shadow_ray_frame`.

#ifndef __SHADOW_RAYS_HLSLI__
#define __SHADOW_RAYS_HLSLI__

#include "RayDefines.hlsli"

StructuredBuffer<BVHNode> srMeshNodes : register(t5);
StructuredBuffer<BVHNode> srTlas : register(t6);
StructuredBuffer<ObjectInfo> srObjects : register(t7);
ByteAddressBuffer srVertices : register(t8);
ByteAddressBuffer srIndices : register(t9);

//Deep enough for 2048 objects split on the mean (a lopsided split makes a deeper tree than
//a balanced one); a ray that fills it drops the subtree it could not push, which can only
//lose a shadow, never invent one.
#define SR_TLAS_STACK 32
//Bytes of one vertex, position first (Core::Vertex).
#define SR_VERTEX_SIZE 96
//How far the ray stops short of the light and how far its origin leaves the surface, in
//world units: the first keeps a lamp's own bulb from shadowing what it lights, the second
//keeps the surface from shadowing itself.
#define SR_LIGHT_MARGIN 0.05f
#define SR_SURFACE_BIAS 0.04f
//How far a directional light's ray looks for a caster. RenderSystem gathers casters out to this far
//beyond the shadow distance; mirrored by SHADOW_RAY_SUN_LENGTH there.
#define SR_SUN_RAY_LENGTH 120.0f

//Where along the ray it first lies inside the box, or FLT_MAX if it never does within
//[0, max_t].
float SrBoxEntry(float3 orig, float3 inv_dir, float3 bmin, float3 bmax, float max_t)
{
	const float3 t0 = (bmin - orig) * inv_dir;
	const float3 t1 = (bmax - orig) * inv_dir;
	const float3 tsmall = min(t0, t1);
	const float3 tbig = max(t0, t1);
	const float tmin = max(max(tsmall.x, tsmall.y), max(tsmall.z, 0.0f));
	const float tmax = min(min(tbig.x, tbig.y), tbig.z);
	return (tmax >= tmin && tmin <= max_t) ? tmin : FLT_MAX;
}

bool SrIsLeaf(BVHNode n)
{
	return asuint(n.reg0.w) == 0u;
}

//Moeller-Trumbore against the triangle whose first index is `index_offset`. True when the
//ray (unit direction) crosses it between 0 and `max_t`.
bool SrTriangleHit(float3 orig, float3 dir, uint index_offset, uint vertex_offset, float max_t)
{
	const uint3 vindex = (vertex_offset + srIndices.Load3(index_offset * 4u)) * SR_VERTEX_SIZE;
	const float3 v0 = asfloat(srVertices.Load3(vindex.x));
	const float3 v1 = asfloat(srVertices.Load3(vindex.y));
	const float3 v2 = asfloat(srVertices.Load3(vindex.z));
	const float3 e1 = v1 - v0;
	const float3 e2 = v2 - v0;
	const float3 h = cross(dir, e2);
	const float a = dot(e1, h);
	//Both faces count: a shadow caster is solid whichever way its triangles face.
	if (abs(a) < 1e-9f) {
		return false;
	}
	const float f = 1.0f / a;
	const float3 s = orig - v0;
	const float u = f * dot(s, h);
	if (u < 0.0f || u > 1.0f) {
		return false;
	}
	const float3 q = cross(s, e1);
	const float v = f * dot(dir, q);
	if (v < 0.0f || u + v > 1.0f) {
		return false;
	}
	const float t = f * dot(e2, q);
	return t > 1e-5f && t < max_t;
}

//Any triangle of one object on the segment, in the object's own space.
bool SrObjectOccluded(ObjectInfo o, float3 world_from, float3 world_dir, float world_len)
{
	float4 oo = mul(float4(world_from, 1.0f), o.inv_world);
	const float3 orig = oo.xyz / oo.w;
	float3 dir = mul(world_dir, (float3x3)o.inv_world);
	//The ray is a unit vector in object space, so its parameter is object space distance;
	//the segment's end is world_len world units away, which is this many object units
	//(a transform is affine, so the ratio holds along the whole ray).
	const float scale = length(dir);
	if (scale < 1e-12f) {
		return false;
	}
	dir /= scale;
	const float max_t = world_len * scale;
	const float3 inv_dir = 1.0f / dir;

	uint stack[MAX_STACK_SIZE];
	uint stack_size = 0;
	uint node_index = 0;
	uint visits = 0;
	[loop]
	while (visits++ < MAX_NODE_VISITS) {
		const BVHNode node = srMeshNodes[o.objectOffset + node_index];
		bool descend = false;
		if (SrBoxEntry(orig, inv_dir, node.reg0.xyz, node.reg1.xyz, max_t) < FLT_MAX) {
			if (SrIsLeaf(node)) {
				if (SrTriangleHit(orig, dir, asuint(node.reg1.w) + o.indexOffset, o.vertexOffset, max_t)) {
					return true;
				}
			}
			else {
				const uint packed_children = asuint(node.reg0.w);
				const uint left = (packed_children & 0xffff0000u) >> 16;
				const uint right = packed_children & 0xffffu;
				if (stack_size < MAX_STACK_SIZE) {
					stack[stack_size++] = right;
				}
				node_index = left;
				descend = true;
			}
		}
		if (!descend) {
			if (stack_size == 0u) {
				break;
			}
			node_index = stack[--stack_size];
		}
	}
	return false;
}

//The pixel being shaded and its place in the frame sequence: the seed of the per ray jitter.
static uint2 sr_pixel = uint2(0, 0);

uint SrHash(uint x)
{
	x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
	return x;
}

//A point inside the unit ball, different for every pixel and every frame. One ray per pixel
//per frame sees the light from a slightly different place each time, so the edge of a shadow
//is a hard edge moving by a pixel or two: averaged over the frames (ShadowAccumulateCS) it is
//a soft one, as wide as the ball the point is scaled to.
float3 SrBallSample()
{
	//Each pixel starts at a random point and then steps along a low discrepancy sequence (R3,
	//Roberts) one frame at a time: consecutive frames cover the ball evenly, so the running
	//average converges in a handful of frames where independent random points would take
	//several times as many to look as smooth.
	const uint seed = SrHash(sr_pixel.x + SrHash(sr_pixel.y));
	const float3 start = float3((float)(SrHash(seed) & 0xFFFFFFu), (float)(SrHash(seed + 1u) & 0xFFFFFFu),
		(float)(SrHash(seed + 2u) & 0xFFFFFFu)) * (1.0f / 16777215.0f);
	const float3 u = frac(start + (float)(shadow_ray_frame & 0xFFFFu) * float3(0.8191725134f, 0.6710436067f, 0.5497004779f));
	//Uniform in the ball: a direction from two of the numbers, a radius from the cube root of the third.
	const float z = u.x * 2.0f - 1.0f;
	const float phi = u.y * 6.2831853f;
	const float r = sqrt(max(1.0f - z * z, 0.0f));
	return float3(r * cos(phi), r * sin(phi), z) * pow(u.z, 1.0f / 3.0f);
}

//Anything solid on the segment of length `len` from `from` along the unit vector `dir`.
bool SrTrace(float3 from, float3 dir, float len)
{
	if (shadow_ray_objects == 0u || len <= 1e-4f) {
		return false;
	}
	const float3 inv_dir = 1.0f / dir;

	uint stack[SR_TLAS_STACK];
	uint stack_size = 0;
	uint node_index = 0;
	uint visits = 0;
	[loop]
	while (visits++ < 4096u) {
		const BVHNode node = srTlas[node_index];
		bool descend = false;
		if (SrBoxEntry(from, inv_dir, node.reg0.xyz, node.reg1.xyz, len) < FLT_MAX) {
			if (SrIsLeaf(node)) {
				if (SrObjectOccluded(srObjects[asuint(node.reg1.w)], from, dir, len)) {
					return true;
				}
			}
			else {
				const uint packed_children = asuint(node.reg0.w);
				const uint left = (packed_children & 0xffff0000u) >> 16;
				const uint right = packed_children & 0xffffu;
				if (stack_size < SR_TLAS_STACK) {
					stack[stack_size++] = right;
				}
				node_index = left;
				descend = true;
			}
		}
		if (!descend) {
			if (stack_size == 0u) {
				break;
			}
			node_index = stack[--stack_size];
		}
	}
	return false;
}

//An includer that can also look for casters ON THE SCREEN defines SR_SCREEN_SHADOWS and
//provides, before this point,
//    bool ScreenShadowActive()                                   whether it is on this frame
//    bool ScreenShadowOccluded(float3 from, float3 dir, float len)
//and the screen is looked at first: a caster the depth buffer shows ends the ray without the
//walk of the hierarchy, and it is the only way a mesh the hierarchy does not hold - a skinned
//one, posed on the GPU, whose triangles in the BVH are its bind pose - can cast at all.
//Neither is the whole answer: the screen sees only what is on it (and the front of that), the
//hierarchy sees everything in it that is not skinned, so they are asked in that order and the
//first yes is the answer.

//True when something solid is on the straight line from `from` (a point on the lit side of
//a surface) to `to` (the light, jittered inside shadow_ray_radius).
bool ShadowRayOccluded(float3 from, float3 to)
{
#ifdef SR_SCREEN_SHADOWS
	const bool screen = ScreenShadowActive();
#else
	const bool screen = false;
#endif
	if (shadow_ray_objects == 0u && !screen) {
		return false;
	}
	to += SrBallSample() * shadow_ray_radius;
	float3 dir = to - from;
	const float len = length(dir) - SR_LIGHT_MARGIN;
	dir = normalize(dir);
#ifdef SR_SCREEN_SHADOWS
	if (screen && ScreenShadowOccluded(from, dir, len)) {
		return true;
	}
#endif
	return SrTrace(from, dir, len);
}

//The same for a directional light: `to_light` is the unit vector towards it, aimed inside a
//cone of tangent shadow_ray_sun_spread (the sun is not a point either), and the ray is
//SR_SUN_RAY_LENGTH long - what is further away than that is not in the scene.
bool ShadowRayOccludedDirectional(float3 from, float3 to_light)
{
#ifdef SR_SCREEN_SHADOWS
	const bool screen = ScreenShadowActive();
#else
	const bool screen = false;
#endif
	if (shadow_ray_objects == 0u && !screen) {
		return false;
	}
	const float3 dir = normalize(to_light + SrBallSample() * shadow_ray_sun_spread);
#ifdef SR_SCREEN_SHADOWS
	if (screen && ScreenShadowOccluded(from, dir, SR_SUN_RAY_LENGTH)) {
		return true;
	}
#endif
	return SrTrace(from, dir, SR_SUN_RAY_LENGTH);
}

#endif
