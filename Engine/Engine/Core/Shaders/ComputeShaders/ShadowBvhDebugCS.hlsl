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

//What the ray traced shadows are traced through, seen from the camera (the shadow_bvh and
//shadow_bvh_cost debug buffers).
//
//One camera ray per pixel walks the same buffers a shadow ray does (Common/ShadowRays.hlsli: the
//hierarchy of a scene, the objects, each mesh's own hierarchy) and finds the NEAREST triangle, so
//what is drawn is exactly the geometry a shadow ray can hit: the level of detail the rays were
//given, in the place the rays see it - not the model that is drawn.
//
//mode 1 (size)  the triangle's area in world units on a ramp, red tiny to blue large: the density
//               of the geometry. Triangle edges are darkened so the mesh can be seen.
//mode 2 (cost)  how many nodes of the two hierarchies the camera ray had to look at, black to
//               white through red: where a ray is expensive.

#include "../Common/Defines.hlsli"

cbuffer externalData : register(b0)
{
	//Pixel to world: row vector times this, as every matrix of the engine.
	matrix inv_view_proj;
	float3 cameraPosition;
	int screenW;
	int screenH;
	//The scene to draw (an index into srScenes), SR_NO_SCENE for none.
	uint debug_scene;
	int mode;
	//Nodes that are white in the cost view.
	float cost_scale;
	//What Common/ShadowRays.hlsli reads of the lighting pass's constants.
	uint shadow_ray_objects;
	float shadow_ray_radius;
	uint shadow_ray_frame;
	float shadow_ray_sun_spread;
	uint shadow_ray_sun_scene;
}

#include "../Common/ShadowRays.hlsli"

RWTexture2D<float4> picture : register(u0);

//The nearest hit so far: its triangle (in the object's space), the object's matrix, and what it took.
static float3 hit_p0, hit_p1, hit_p2;
static matrix hit_world;
static float2 hit_uv;
static bool hit_found = false;
static uint steps = 0;

//Moeller-Trumbore, nearest hit: shrinks max_t and keeps the triangle when it is nearer.
bool DbgTriangle(float3 orig, float3 dir, uint index_offset, uint vertex_offset, inout float max_t)
{
	const uint3 vindex = (vertex_offset + srIndices.Load3(index_offset * 4u)) * SR_VERTEX_SIZE;
	const float3 v0 = asfloat(srVertices.Load3(vindex.x));
	const float3 v1 = asfloat(srVertices.Load3(vindex.y));
	const float3 v2 = asfloat(srVertices.Load3(vindex.z));
	const float3 e1 = v1 - v0;
	const float3 e2 = v2 - v0;
	const float3 h = cross(dir, e2);
	const float a = dot(e1, h);
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
	if (t <= 1e-5f || t >= max_t) {
		return false;
	}
	max_t = t;
	hit_p0 = v0;
	hit_p1 = v1;
	hit_p2 = v2;
	hit_uv = float2(u, v);
	return true;
}

//Nearest hit in one object, in world distance along the camera ray.
void DbgObject(ObjectInfo o, float3 world_from, float3 world_dir, inout float best)
{
	float4 oo = mul(float4(world_from, 1.0f), o.inv_world);
	const float3 orig = oo.xyz / oo.w;
	float3 dir = mul(world_dir, (float3x3)o.inv_world);
	const float scale = length(dir);
	if (scale < 1e-12f) {
		return;
	}
	dir /= scale;
	float max_t = best * scale;
	const float3 inv_dir = 1.0f / dir;

	BVHNode node = srMeshNodes[o.objectOffset];
	if (SrBoxEntry(orig, inv_dir, node.reg0.xyz, node.reg1.xyz, max_t) == FLT_MAX) {
		return;
	}
	uint stack[MAX_STACK_SIZE];
	uint stack_size = 0;
	uint visits = 0;
	[loop]
	while (visits++ < MAX_NODE_VISITS) {
		++steps;
		bool have = false;
		if (SrIsLeaf(node)) {
			if (DbgTriangle(orig, dir, asuint(node.reg1.w) + o.indexOffset, o.vertexOffset, max_t)) {
				best = max_t / scale;
				hit_world = o.world;
				hit_found = true;
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
		// What was pushed was in the ray's way when it was, not necessarily now that a nearer hit is known.
		while (!have && stack_size > 0u) {
			node = srMeshNodes[o.objectOffset + stack[--stack_size]];
			have = SrBoxEntry(orig, inv_dir, node.reg0.xyz, node.reg1.xyz, max_t) < FLT_MAX;
		}
		if (!have) {
			break;
		}
	}
}

//Nearest hit among the objects of the scene.
void DbgTrace(float3 from, float3 dir, float len, uint scene_index)
{
	if (scene_index == SR_NO_SCENE) {
		return;
	}
	const uint4 scene = srScenes[scene_index];
	if (scene.z == 0u) {
		return;
	}
	const float3 inv_dir = 1.0f / dir;
	float best = len;
	BVHNode node = srTlas[scene.x];
	if (SrBoxEntry(from, inv_dir, node.reg0.xyz, node.reg1.xyz, best) == FLT_MAX) {
		return;
	}
	uint stack[SR_TLAS_STACK];
	uint stack_size = 0;
	uint visits = 0;
	[loop]
	while (visits++ < 4096u) {
		++steps;
		bool have = false;
		if (SrIsLeaf(node)) {
			DbgObject(srObjects[scene.y + asuint(node.reg1.w)], from, dir, best);
		}
		else {
			const uint packed_children = asuint(node.reg0.w);
			const uint left = (packed_children & 0xffff0000u) >> 16;
			const uint right = packed_children & 0xffffu;
			const BVHNode ln = srTlas[scene.x + left];
			const BVHNode rn = srTlas[scene.x + right];
			const float tl = SrBoxEntry(from, inv_dir, ln.reg0.xyz, ln.reg1.xyz, best);
			const float tr = SrBoxEntry(from, inv_dir, rn.reg0.xyz, rn.reg1.xyz, best);
			if (tl < FLT_MAX && tr < FLT_MAX) {
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
		while (!have && stack_size > 0u) {
			node = srTlas[scene.x + stack[--stack_size]];
			have = SrBoxEntry(from, inv_dir, node.reg0.xyz, node.reg1.xyz, best) < FLT_MAX;
		}
		if (!have) {
			break;
		}
	}
}

//Red, orange, yellow, green, cyan, blue, violet along x in [0, 1].
float3 Ramp(float x)
{
	x = saturate(x);
	const float3 c0 = float3(1.0f, 0.05f, 0.05f);
	const float3 c1 = float3(1.0f, 0.65f, 0.05f);
	const float3 c2 = float3(0.9f, 0.95f, 0.1f);
	const float3 c3 = float3(0.1f, 0.85f, 0.2f);
	const float3 c4 = float3(0.05f, 0.8f, 0.9f);
	const float3 c5 = float3(0.1f, 0.3f, 1.0f);
	const float3 c6 = float3(0.6f, 0.2f, 0.9f);
	const float f = x * 6.0f;
	if (f < 1.0f) return lerp(c0, c1, f);
	if (f < 2.0f) return lerp(c1, c2, f - 1.0f);
	if (f < 3.0f) return lerp(c2, c3, f - 2.0f);
	if (f < 4.0f) return lerp(c3, c4, f - 3.0f);
	if (f < 5.0f) return lerp(c4, c5, f - 4.0f);
	return lerp(c5, c6, f - 5.0f);
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
	if (dtid.x >= (uint)screenW || dtid.y >= (uint)screenH) {
		return;
	}
	const float2 ndc = float2(((float)dtid.x + 0.5f) / (float)screenW * 2.0f - 1.0f, 1.0f - ((float)dtid.y + 0.5f) / (float)screenH * 2.0f);
	//A point the pixel sees, far along its ray (either end of the depth range: the one farther from the eye).
	float4 a = mul(float4(ndc, 1.0f, 1.0f), inv_view_proj);
	float4 b = mul(float4(ndc, 0.0f, 1.0f), inv_view_proj);
	const float3 pa = a.xyz / a.w;
	const float3 pb = b.xyz / b.w;
	const float3 far_point = (length(pa - cameraPosition) > length(pb - cameraPosition)) ? pa : pb;
	const float3 dir = normalize(far_point - cameraPosition);

	DbgTrace(cameraPosition, dir, 100000.0f, debug_scene);

	//A faint ground so the picture reads as a scene and not as floating shapes.
	float3 color = float3(0.03f, 0.035f, 0.045f);
	if (mode == 2) {
		const float heat = saturate((float)steps / max(cost_scale, 1.0f));
		color = (steps == 0u) ? color : lerp(float3(heat, heat * 0.1f, 0.0f), float3(1.0f, 1.0f, 1.0f), saturate(heat * heat));
	}
	else if (hit_found) {
		const float3 w0 = mul(float4(hit_p0, 1.0f), hit_world).xyz;
		const float3 w1 = mul(float4(hit_p1, 1.0f), hit_world).xyz;
		const float3 w2 = mul(float4(hit_p2, 1.0f), hit_world).xyz;
		const float3 n = cross(w1 - w0, w2 - w0);
		const float area = 0.5f * length(n);
		//0.01 square units is red, 100 is blue.
		const float x = (log10(max(area, 1e-6f)) + 2.0f) / 4.0f;
		const float facing = abs(dot(normalize(n), dir));
		float3 c = Ramp(x) * (0.45f + 0.55f * facing);
		const float edge = min(min(hit_uv.x, hit_uv.y), 1.0f - hit_uv.x - hit_uv.y);
		if (edge < 0.04f) {
			c *= 0.25f;
		}
		color = c;
	}
	picture[dtid.xy] = float4(color, 1.0f);
}
