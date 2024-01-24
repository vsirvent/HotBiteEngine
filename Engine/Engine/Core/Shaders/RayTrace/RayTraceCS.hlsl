#include "../Common/PixelCommon.hlsli"
#include "../Common/FastNoise.hlsli"
#include "../Common/QuickNoise.hlsli"
#include "../Common/NoiseSimplex.hlsli"
#include "../Common/RGBANoise.hlsli"
#include "../Common/ShaderStructs.hlsli"

#define MAX_OBJECTS 64
#define SAMPLES_PER_PIXEL 4
#define RAY_BOUNCES 3
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
	float3 orig;
	float3 dir;
	float t; //intersection
};

struct ObjectInfo
{
    matrix world;
	float3 aabb_min;
    uint objectOffset;
	float3 aabb_max;
    uint indexOffset;
};

cbuffer externalData : register(b0)
{
	matrix world;
	matrix view;
	matrix projection;
	AmbientLight ambientLight;
	DirLight dirLights[MAX_LIGHTS];
	PointLight pointLights[MAX_LIGHTS];
	int dirLightsCount;
	int pointLightsCount;
	MaterialColor material;

	int screenW;
	int screenH;

	float parallaxScale;
	int meshNormalTextureEnable;
	int highTextureEnable;

	float3 cameraPosition;
	float3 cameraDirection;
	
	float time;

	float4 LightPerspectiveValues[MAX_LIGHTS / 2];
	matrix DirPerspectiveMatrix[MAX_LIGHTS];
	matrix DirStaticPerspectiveMatrix[MAX_LIGHTS];

	uint multi_texture_count;
	float multi_parallax_scale;
	uint4 packed_multi_texture_operations[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_values[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_uv_scales[MAX_MULTI_TEXTURE / 4];

	uint nobjects;
	ObjectInfo objectInfos[MAX_OBJECTS];
}

StructuredBuffer<BVHNode> objects: register(t0);
StructuredBuffer<VertexShaderInput> vertexBuffer: register(t1);
StructuredBuffer<uint> indicesBuffer: register(t2);
RWTexture2D<float4> output : register(u0);

#include "../Common/PixelFunctions.hlsli"

bool is_leaf(BVHNode node)
{
	return (asuint(node.reg0) == 0);
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
	return asuint(node.reg1);
}

bool IntersectTri(Ray ray, uint vindex)
{
	VertexShaderInput v0 = vertexBuffer[indicesBuffer[vindex]];
	VertexShaderInput v1 = vertexBuffer[indicesBuffer[vindex + 1]];
	VertexShaderInput v2 = vertexBuffer[indicesBuffer[vindex + 2]];

	const float3 edge1 = v1.position - v0.position;
	const float3 edge2 = v2.position - v0.position;
	const float3 h = cross(ray.dir, edge2);
	const float a = dot(edge1, h);
	
	if (a > -0.0001f && a < 0.0001f) {
		return false; // ray parallel to triangle
	}
	
	const float f = 1 / a;
	const float3 s = ray.orig - v0.position;
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
	if (t > 0.0001f && t < ray.t) {
		ray.t = t;
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

	return tmax >= tmin && tmin < ray.t && tmax > 0;
}

bool IntersectAABB(Ray ray, BVHNode node)
{
	float3 bmin = aabb_min(node);
	float3 bmax = aabb_max(node);
	return IntersectAABB(ray, bmin, bmax);
}

void IntersectBVH(out Ray ray, StructuredBuffer<BVHNode> object, uint nodeIdx)
{
	BVHNode node = object[nodeIdx];
	if (!IntersectAABB(ray, node)) {
		return;
	}

	if (is_leaf(node))
	{
		IntersectTri(ray, index(node));
	}
	else
	{
		IntersectBVH(ray, object, left_child(node));
		IntersectBVH(ray, object, right_child(node));
	}
}

float Random(float2 uv)
{
	return frac(sin(dot(uv + time, float2(12.9898, 78.233))) * 43758.5453);
}

Ray GetRay(float2 uv)
{
	Ray ray;
	ray.orig = float3(cameraPosition);
	//ray.direction = float3(camera.upperLeftCorner - uv.x * camera.horizontalExtents - uv.y * camera.verticalExtents - camera.origin);

	return ray;
}

Ray GenerateRay(uint3 dispatchThreadID, float2 xy, uint2 dimensions)
{
	// PixelCoords are in the range of 0..1 with (0, 0) being the top left corner.
	float2 pixelCoords = (dispatchThreadID.xy) / dimensions.x;
	float2 randomVec = xy / dimensions.y;

	Ray ray = GetRay(pixelCoords + randomVec);
	return ray;
}

float4 GetColor(Ray ray)
{
	int rayBounces = RAY_BOUNCES;

	float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);

	float tMin = T_MIN;
	float tMax = T_MAX;
#if 0
	HitRecord hr;

	while (rayBounces >= 0)
	{
		int materialIndex = HitSceneObjects(ray, tMin, tMax, hr);
		if (materialIndex != -1)
		{
			Scatter(ray, hr, color, materialIndex);

			rayBounces--;
		}
		else
		{
			color *= BackgroundColor(ray.direction);
			return color;
		}
	}
#endif
	return color;
}

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	float4 result = float4(0.0f, 0.0f, 0.0f, 0.0f);
	uint w, h;
	uint2 dimensions;
	output.GetDimensions(w, h);
	dimensions.x = w;
	dimensions.y = h;

	uint blockSizeX = w / NTHREADS;
	uint blockSizeY = h / NTHREADS;

	// Calculate the starting pixel coordinates of the block
	uint blockStartX = DTid.x * blockSizeX;
	uint blockStartY = DTid.y * blockSizeY;

	// Process pixels within the block
	for (uint localY = 0; localY < blockSizeY; ++localY)
	{
		for (uint localX = 0; localX < blockSizeX; ++localX)
		{
			// Calculate the global pixel coordinates within the texture
			uint2 pixel = { blockStartX + localX, blockStartY + localY };
			// Make sure the pixel is within the bounds of the texture
			if (pixel.x < w && pixel.y < h)
			{
				for (int i = 0; i < SAMPLES_PER_PIXEL; ++i)
				{
					//float2 randomSeed = float2(Random(DTid.xx), Random(DTid.yy));
					//Ray ray = GenerateRay(DTid, randomSeed);
					//result += GetColor(ray);
				}
				result /= SAMPLES_PER_PIXEL;
				output[pixel] = result;
			}
		}
	}
}