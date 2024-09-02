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

#include "../Common/ShaderStructs.hlsli"
#include "../Common/PixelCommon.hlsli"
#include "../Common/RayDefines.hlsli"
#include "../Common/RGBANoise.hlsli"
#include "RTDefines.hlsli"

cbuffer externalData : register(b0)
{
	int screenW;
	int screenH;
	matrix world;
	matrix inv_world;
	matrix view_proj;
	float3 cameraPosition;

	uint frame_count;
	float time;

	uint enabled;
	MaterialColor material;
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
	uint nobjects;
	ObjectInfo objectInfos[MAX_OBJECTS];
	MaterialColor objectMaterials[MAX_OBJECTS];
}

Texture2D<float4> ray0 : register(t0);
Texture2D<float4> ray1 : register(t1);

StructuredBuffer<BVHNode> objects: register(t2);
StructuredBuffer<BVHNode> objectBVH: register(t3);

ByteAddressBuffer vertexBuffer : register(t4);
ByteAddressBuffer indicesBuffer : register(t5);
Texture2D<float4> position_map : register(t6);
Texture2D<float4> motionTexture : register(t8);
Texture2D<float4> depth_map : register(t9);

Texture2D<float4> DiffuseTextures[MAX_OBJECTS];
Texture2D<float> DirShadowMapTexture[MAX_LIGHTS];
TextureCube<float> PointShadowMapTexture[MAX_LIGHTS];

//Packed array
static float2 lps[MAX_LIGHTS] = (float2[MAX_LIGHTS])LightPerspectiveValues;

#include "../Common/SimpleLight.hlsli"
#include "../Common/RayFunctions.hlsli"

struct RayTraceColor {
	float3 color[2];
	float dispersion[2];
	float3 bloom;
	bool hit;
};

static const float max_distance = 1000.0f;

static float NCOUNT = 32.0f;
static uint N2 = 32;
static float N = N2 * NCOUNT;

bool GetColor(Ray origRay, float rX, float level, uint max_bounces, out RayTraceColor out_color, float dispersion, bool mix, bool refract)
{
	out_color.color[0] = float3(0.0f, 0.0f, 0.0f);
	out_color.color[1] = float3(0.0f, 0.0f, 0.0f);
	out_color.bloom = float3(0.0f, 0.0f, 0.0f);
	out_color.dispersion[0] = -1.0f;
	out_color.dispersion[1] = -1.0f;

	float last_dispersion = dispersion;
	float acc_dispersion = dispersion;
	float collision_dist = 0.0f;
	float att_dist = 0.0f;
#if USE_OBH   
	uint volumeStack[MAX_STACK_SIZE];
#endif
	uint stack[MAX_STACK_SIZE];
	RayObject oray;
	IntersectionResult object_result;

	bool collide = false;
	IntersectionResult result;
	Ray ray = origRay;
	bool end = false;

	for (uint bounce = 0; !end; ++bounce)
	{
		result.distance = FLT_MAX;
		end = true;
		collide = false;
#if USE_OBH
		uint volumeStackSize = 0;
		volumeStack[volumeStackSize++] = 0;
		uint i = 0;
		while (volumeStackSize > 0 && volumeStackSize < MAX_STACK_SIZE)
		{
#else
		for (uint i = 0; i < nobjects; ++i)
		{
			uint objectIndex = i;
#endif

#if USE_OBH
			uint currentVolume = volumeStack[--volumeStackSize];

			BVHNode volumeNode = objectBVH[currentVolume];
			if (is_leaf(volumeNode))
			{
				uint objectIndex = index(volumeNode);
				ObjectInfo o = objectInfos[objectIndex];

				float objectExtent = length(o.aabb_max - o.aabb_min);
				float distanceToObject = length(o.position - origRay.orig) - objectExtent;

				if (distanceToObject < max_distance && distanceToObject < result.distance && IntersectAABB(ray, o.aabb_min, o.aabb_max))
				{
#else
			ObjectInfo o = objectInfos[i];
			float objectExtent = length(o.aabb_max - o.aabb_min);
			float distanceToObject = length(o.position - origRay.orig) - objectExtent;
			if (distanceToObject < max_distance && distanceToObject < result.distance && IntersectAABB(ray, o.aabb_min, o.aabb_max))
			{
#endif
				object_result.distance = FLT_MAX;

				// Transform the ray direction from world space to object space
				oray.orig = mul(ray.orig, o.inv_world);
				oray.orig /= oray.orig.w;
				oray.dir = normalize(mul(ray.dir, (float3x3) o.inv_world));
				oray.t = FLT_MAX;

				uint stackSize = 0;
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
							if (tmp_result.distance < object_result.distance) {
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

				if (object_result.distance < FLT_MAX) {
					float3 opos = (1.0f - object_result.uv.x - object_result.uv.y) * object_result.v0 + object_result.uv.x * object_result.v1 + object_result.uv.y * object_result.v2;
					float4 pos = mul(float4(opos, 1.0f), o.world);
					float distance = length(pos - ray.orig);
					if (distance < result.distance)
					{
						collide = true;
						collision_dist = length(pos - ray.orig);
						ray.t = distance;
						result.v0 = object_result.v0;
						result.v1 = object_result.v1;
						result.v2 = object_result.v2;
						result.vindex = object_result.vindex;
						result.distance = distance;
						result.uv = object_result.uv;
						result.object = objectIndex;
					}
				}
			}
#if USE_OBH
				}
			else if (IntersectAABB(ray, volumeNode)) {
				volumeStack[volumeStackSize++] = left_child(volumeNode);
				volumeStack[volumeStackSize++] = right_child(volumeNode);
			}
			++i;
#endif
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

			float4 emission = material.emission * float4(material.emission_color, 1.0f);
			float3 color = float3(0.0f, 0.0f, 0.0f);
			if (material.emission > 0.0f) {
				color = emission;
			}
			else {
				color += CalcAmbient(normal) * 0.5f;

				int i = 0;
				for (i = 0; i < dirLightsCount; ++i) {
					color += CalcDirectional(normal, pos.xyz, dirLights[i], i);
				}

				// Calculate the point lights
				for (i = 0; i < pointLightsCount; ++i) {
					if (length(pos.xyz - pointLights[i].Position) < pointLights[i].Range) {
						color += CalcPoint(normal, pos.xyz, pointLights[i], i);
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
			}
			float material_dispersion = saturate(1.0f - material.specIntensity);
			att_dist += collision_dist * material_dispersion;
			if (refract) {
				att_dist = 1.0f;
			}
			float curr_att_dist = max(att_dist, 1.0f);
			if (ray.bounces == 0 || mix) {
				out_color.color[0] += color * ray.ratio * o.opacity / curr_att_dist;
				out_color.dispersion[0] = acc_dispersion;
				if (!refract) {
					out_color.bloom += emission / curr_att_dist;
				}
			}
			else {
				out_color.color[1] += color * ray.ratio * o.opacity / curr_att_dist;
				out_color.dispersion[1] = acc_dispersion;
			}
			acc_dispersion += material_dispersion;
			last_dispersion = material_dispersion;
			ray.orig = pos;
			out_color.hit = true;
			//If not opaque surface, generate a refraction ray
			if (ray.bounces < max_bounces && o.opacity < 1.0f) {
				if (i % 2 == 0) {
					normal = -normal;
				}
				ray = GetRefractedRayFromRay(ray, ray.density,
					ray.density != 1.0 ? 1.0f : o.density,
					normal, ray.ratio * (1.0f - o.opacity));
				end = false;
			}
			else if (ray.bounces < max_bounces && o.opacity > 0.0f) {
				ray = GetReflectedRayFromRay(ray, normal, ray.ratio);
				float3 tangent;
				float3 bitangent;
				GetSpaceVectors(ray.dir, tangent, bitangent);
				ray.dir = GenerateHemisphereRay(ray.dir, tangent, bitangent, pow(1.0f - material.specIntensity, 3.0f), N, level, rX);
				end = false;
			}
		}
		else {
			//Color background

		} // End collision check
	}
	return out_color.hit;
}

[maxvertexcount(3)]
void main(
	triangle RTDomainOutput input[3],
	inout TriangleStream< RTGSOutput > output
)
{

#if 0
	bool process = false;
	float4 p[3];
	uint i = 0;
	for (i = 0; i < 3; i++) {
		p[i] = mul(input[i].worldPos, view_proj);
	}
	for (i = 0; i < 3; i++) {
		float2 pos;
		pos = p[i].xy;
		pos.x /= p[i].w;
		pos.y /= -p[i].w;
		pos.x = (pos.x + 1.0f) * 0.5f;
		pos.y = (pos.y + 1.0f) * 0.5f;
		if (pos.x >= 0.0f && pos.x <= screenW && pos.y >= 0.0f && pos.y <= screenH) {
			process = true;
			break;
		}
	}
#else
bool process = true;
float4 p[3];
uint i = 0;
for (i = 0; i < 3; i++) {
	p[i] = mul(input[i].worldPos, view_proj);
}
#endif
	if (process) {

		float cumulativePoints = 0;
		float level = 1;
		while (true) {
			float c = cumulativePoints + level * 2;
			if (c < N) {
				cumulativePoints = c;
			}
			else {
				break;
			}
			level++;
		};

		for (i = 0; i < 3; i++)
		{
			RTGSOutput element;
			float3 tangent;
			float3 bitangent;
			GetSpaceVectors(input[i].normal, tangent, bitangent);

			RaySource ray_source;
			ray_source.orig = input[i].worldPos;
			if ((material.flags & RAYTRACING_ENABLED)) {
				ray_source.dispersion = saturate(1.0f - material.specIntensity);
				ray_source.reflex = material.rt_reflex;
			}
			else {
				ray_source.dispersion = -1.0f;
				ray_source.reflex = 0.0f;
			}
			ray_source.normal = input[i].normal;
			ray_source.density = material.density;
			ray_source.opacity = material.opacity;

			RayTraceColor rc;
			Ray ray = GetReflectedRayFromSource(ray_source);
			uint count = 0;
			float4 color_diffuse = float4(0.0f, 0.0f, 0.0f, 1.0f);
			if (length(input[i].worldPos - cameraPosition) < 20.0f) {
				for (uint n = 0; n < 3; ++n) {
					float rX;
					float3 seed = float3(50.0f, 50.0f, 50.0f) + p[i] * (n + 1);
					rX = rgba_tnoise(seed);
					rX = pow(rX, 2.0f);// / (float)(i + 1));
					rX *= 0.5f;
					ray.dir = GenerateHemisphereRay(input[i].normal, tangent, bitangent, 1.0f, N, level, rX);
					ray.orig.xyz = ray_source.orig.xyz + ray.dir * 0.01f;
					float dist = FLT_MAX;
					if (GetColor(ray, rX, level, 1, rc, ray_source.dispersion, true, false)) {
						color_diffuse.rgb += rc.color[0];
						count++;
					}
				}
				if (count > 0) {
					color_diffuse.rgb /= count;
				}
			}
			element.color = color_diffuse;
			element.position = p[i];
			output.Append(element);
		}
	}
}