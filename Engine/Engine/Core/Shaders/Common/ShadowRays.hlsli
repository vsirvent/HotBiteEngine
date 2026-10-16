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
//light through the scene Core::ShadowRays uploaded for that light this frame (the objects its
//range reaches, which are all that can be between a surface it lights and itself) -
//
//  srScenes   where each scene's hierarchy and objects start (the light names its scene in the
//             bits of its flags from PACKED_LIGHT_SCENE_SHIFT up)
//  srTlas     the top level hierarchies, one leaf per object, over the objects' world boxes
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
//same for a directional light, as a cone), `shadow_ray_sun_scene` (the scene a directional
//light's rays go through, SR_NO_SCENE for none) and `shadow_ray_frame`.

#ifndef __SHADOW_RAYS_HLSLI__
#define __SHADOW_RAYS_HLSLI__

#include "RayDefines.hlsli"

//Two ways to answer "is something in the way": the software hierarchy above (the default), and, when the includer defines
//SR_RAYQUERY (shader model 6.5), an inline ray query against the acceleration structure the engine builds each frame
//(Core::AccelStructure) - the hardware walks it. The hardware path has no scenes of its own: one structure holds every caster
//any of the frame's lights can reach, and a ray is as long as the distance it asks about. A light's scene index only says
//whether there is something to trace (SR_NO_SCENE: nothing). Everything else below, the jitter and the margins, is shared.
#ifdef SR_RAYQUERY
RaytracingAccelerationStructure srAccel : register(t15);
#else
//Where the scenes of the frame start: x its first node in srTlas, y its first object in
//srObjects, z how many objects it has.
StructuredBuffer<uint4> srScenes : register(t14);
StructuredBuffer<BVHNode> srMeshNodes : register(t5);
StructuredBuffer<BVHNode> srTlas : register(t6);
StructuredBuffer<ObjectInfo> srObjects : register(t7);
ByteAddressBuffer srVertices : register(t8);
ByteAddressBuffer srIndices : register(t9);
#endif

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
//The scene index of a light that has none (its scene did not fit in the frame's budget).
#define SR_NO_SCENE 0xFFFFFFu

//Counters of what the rays did, compiled in only while SR_STATS is defined (DeferredLightCS adds them up
//over the frame into sr_counters): 0 rays that reached the world, 1 ended by the screen, 2 steps of the top
//level hierarchy, 3 objects entered, 4 steps of an object's hierarchy, 5 triangles tested, 6 ended by the
//world, 7 pixels that traced at least one ray.
#ifdef SR_STATS
static uint sr_stat[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
#define SR_COUNT(i) sr_stat[i]++
#else
#define SR_COUNT(i)
#endif

#ifndef SR_RAYQUERY
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
	SR_COUNT(5);
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
	SR_COUNT(3);
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

	//The nearer child first, and a child the ray misses is never visited (nor pushed): a ray that is
	//stopped by something stops at the first thing it meets instead of after the walk of the rest, and
	//most rays of a lit scene are stopped (the lamp of a building is inside the building).
	BVHNode node = srMeshNodes[o.objectOffset];
	if (SrBoxEntry(orig, inv_dir, node.reg0.xyz, node.reg1.xyz, max_t) == FLT_MAX) {
		return false;
	}
	uint stack[MAX_STACK_SIZE];
	uint stack_size = 0;
	uint visits = 0;
	[loop]
	while (visits++ < MAX_NODE_VISITS) {
		SR_COUNT(4);
		bool have = false;
		if (SrIsLeaf(node)) {
			if (SrTriangleHit(orig, dir, asuint(node.reg1.w) + o.indexOffset, o.vertexOffset, max_t)) {
				return true;
			}
		}
		else {
			const uint packed_children = asuint(node.reg0.w);
			const uint left = (packed_children & 0xffff0000u) >> 16;
			const uint right = packed_children & 0xffffu;
			const BVHNode ln = srMeshNodes[o.objectOffset + left];
			const BVHNode rn = srMeshNodes[o.objectOffset + right];
			const float tl = SrBoxEntry(orig, inv_dir, ln.reg0.xyz, ln.reg1.xyz, max_t);
			const float tr = SrBoxEntry(orig, inv_dir, rn.reg0.xyz, rn.reg1.xyz, max_t);
			if (tl < FLT_MAX && tr < FLT_MAX) {
				if (stack_size < MAX_STACK_SIZE) {
					stack[stack_size++] = (tl <= tr) ? right : left;
				}
				if (tl <= tr) { node = ln; } else { node = rn; }
				have = true;
			}
			else if (tl < FLT_MAX) {
				node = ln;
				have = true;
			}
			else if (tr < FLT_MAX) {
				node = rn;
				have = true;
			}
		}
		if (!have) {
			if (stack_size == 0u) {
				break;
			}
			node = srMeshNodes[o.objectOffset + stack[--stack_size]];
		}
	}
	return false;
}
#endif
//The pixel being shaded: the seed of the per ray jitter.
static uint2 sr_pixel = uint2(0, 0);

uint SrHash(uint x)
{
	x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
	return x;
}

//A point inside the unit ball, a function of the pixel alone: the same pixel aims at the same
//point every frame, so a still picture does not move. The pixels of a 4x4 tile take 16 different
//points of a low discrepancy sequence (R3, Roberts), the tiles repeat, and ShadowAccumulateCS
//blurs over exactly one tile's worth of pixels, so the edge of a shadow becomes a soft one as
//wide as the ball the point is scaled to without the frames having anything to do with it.
float3 SrBallSample()
{
	const uint tile_index = (sr_pixel.x & 3u) + 4u * (sr_pixel.y & 3u);
	const float3 u = frac(float3(0.5f, 0.5f, 0.5f) + (float)(tile_index + 1u) * float3(0.8191725134f, 0.6710436067f, 0.5497004779f));
	//Uniform in the ball: a direction from two of the numbers, a radius from the cube root of the third.
	const float z = u.x * 2.0f - 1.0f;
	const float phi = u.y * 6.2831853f;
	const float r = sqrt(max(1.0f - z * z, 0.0f));
	return float3(r * cos(phi), r * sin(phi), z) * pow(u.z, 1.0f / 3.0f);
}

#ifdef SR_RAYQUERY
//Anything solid on the segment of length `len` from `from` along the unit vector `dir`: the first triangle of the structure
//that the ray meets ends the query, which is all that is asked (no nearest hit, no shading). Both faces count, as in the
//software walk (no culling flag), and every triangle is opaque.
bool SrTrace(float3 from, float3 dir, float len, uint scene_index)
{
	if (shadow_ray_objects == 0u || scene_index == SR_NO_SCENE || len <= 1e-4f) {
		return false;
	}
	RayDesc ray;
	ray.Origin = from;
	ray.Direction = dir;
	ray.TMin = 0.0f;
	ray.TMax = len;
	RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
	query.TraceRayInline(srAccel, RAY_FLAG_NONE, 0xFF, ray);
	while (query.Proceed()) {
	}
	return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}
#else
//Anything solid on the segment of length `len` from `from` along the unit vector `dir`, among
//the objects of scene `scene_index`.
bool SrTrace(float3 from, float3 dir, float len, uint scene_index)
{
	if (shadow_ray_objects == 0u || scene_index == SR_NO_SCENE || len <= 1e-4f) {
		return false;
	}
	const uint4 scene = srScenes[scene_index];
	if (scene.z == 0u) {
		return false;
	}
	const float3 inv_dir = 1.0f / dir;

	//Same walk as an object's: the nearer child first, the ones the ray misses not at all. A leaf is an
	//object, whose own hierarchy is walked only when the ray is in its box.
	BVHNode node = srTlas[scene.x];
	if (SrBoxEntry(from, inv_dir, node.reg0.xyz, node.reg1.xyz, len) == FLT_MAX) {
		return false;
	}
	uint stack[SR_TLAS_STACK];
	uint stack_size = 0;
	uint visits = 0;
	[loop]
	while (visits++ < 4096u) {
		SR_COUNT(2);
		bool have = false;
		if (SrIsLeaf(node)) {
			if (SrObjectOccluded(srObjects[scene.y + asuint(node.reg1.w)], from, dir, len)) {
				SR_COUNT(6);
				return true;
			}
		}
		else {
			const uint packed_children = asuint(node.reg0.w);
			const uint left = (packed_children & 0xffff0000u) >> 16;
			const uint right = packed_children & 0xffffu;
			const BVHNode ln = srTlas[scene.x + left];
			const BVHNode rn = srTlas[scene.x + right];
			const float tl = SrBoxEntry(from, inv_dir, ln.reg0.xyz, ln.reg1.xyz, len);
			const float tr = SrBoxEntry(from, inv_dir, rn.reg0.xyz, rn.reg1.xyz, len);
			if (tl < FLT_MAX && tr < FLT_MAX) {
				//A ray that fills the stack drops the subtree it could not push, which can only lose a shadow.
				if (stack_size < SR_TLAS_STACK) {
					stack[stack_size++] = (tl <= tr) ? right : left;
				}
				if (tl <= tr) { node = ln; } else { node = rn; }
				have = true;
			}
			else if (tl < FLT_MAX) {
				node = ln;
				have = true;
			}
			else if (tr < FLT_MAX) {
				node = rn;
				have = true;
			}
		}
		if (!have) {
			if (stack_size == 0u) {
				break;
			}
			node = srTlas[scene.x + stack[--stack_size]];
		}
	}
	return false;
}
#endif
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
//a surface) to `to` (the light, jittered inside shadow_ray_radius), among the objects of the
//light's scene.
bool ShadowRayOccluded(float3 from, float3 to, uint scene)
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
		SR_COUNT(1);
		return true;
	}
#endif
	SR_COUNT(0);
	//From the light to the surface, not the other way: a lamp stands on or in a building, and what stops most rays is that
	//building, which the ray then meets first. Any hit anywhere on the segment is the same answer from either end.
	return SrTrace(to - dir * SR_LIGHT_MARGIN, -dir, len, scene);
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
	return SrTrace(from, dir, SR_SUN_RAY_LENGTH, shadow_ray_sun_scene);
}

#endif
