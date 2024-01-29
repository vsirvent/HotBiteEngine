#include "../Common/PixelCommon.hlsli"
#include "../Common/FastNoise.hlsli"
#include "../Common/QuickNoise.hlsli"
#include "../Common/NoiseSimplex.hlsli"
#include "../Common/RGBANoise.hlsli"
#include "../Common/ShaderStructs.hlsli"

#define MAX_OBJECTS 64
#define MAX_STACK_SIZE 64

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
	float t; //intersection distance
    uint index; //first vertex index of triange
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
    matrix invView;

    float time;
    float3 cameraPosition;
    float3 cameraDirection;

    uint nobjects;
    ObjectInfo objectInfos[MAX_OBJECTS];

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

	float4 LightPerspectiveValues[MAX_LIGHTS / 2];
	matrix DirPerspectiveMatrix[MAX_LIGHTS];
	matrix DirStaticPerspectiveMatrix[MAX_LIGHTS];

	uint multi_texture_count;
	float multi_parallax_scale;
	uint4 packed_multi_texture_operations[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_values[MAX_MULTI_TEXTURE / 4];
	float4 packed_multi_texture_uv_scales[MAX_MULTI_TEXTURE / 4];

}

StructuredBuffer<BVHNode> objects: register(t2);
StructuredBuffer<VertexShaderInput> vertexBuffer: register(t3);
Buffer<uint> indicesBuffer : register(t4);

RWTexture2D<float4> output : register(u0);

#include "../Common/PixelFunctions.hlsli"

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

bool IntersectTri(Ray ray, out float distance, out uint index, uint vindex)
{
    uint i0 = indicesBuffer[vindex];
    uint i1 = indicesBuffer[vindex + 1];
    uint i2 = indicesBuffer[vindex + 2];
    
    VertexShaderInput v0 = vertexBuffer[i0];
    VertexShaderInput v1 = vertexBuffer[i1];
    VertexShaderInput v2 = vertexBuffer[i2];

    const float3 edge1 = v1.position - v0.position;
    const float3 edge2 = v2.position - v0.position;
    const float3 h = cross(ray.dir, edge2);
    const float a = dot(edge1, h);

    const float epsilon = 1e-6; // Use a small epsilon for comparisons

    // Check if the ray is parallel to the triangle
    if (abs(a) < epsilon)
    {
        return false;
    }

    const float f = 1.0f / a;
    const float3 s = ray.orig - v0.position;
    const float u = f * dot(s, h);

    // Check if the intersection point is inside the triangle
    if (u < 0 || u > 1)
    {
        return false;
    }

    const float3 q = cross(s, edge1);
    const float v = f * dot(ray.dir, q);

    // Check if the intersection point is inside the triangle
    if (v < 0 || u + v > 1)
    {
        return false;
    }

    const float t = f * dot(edge2, q);

    // Check if the intersection is within the valid range along the ray
    if (t > 1e-6 && t < ray.t)
    {
        distance = t;
        index = vindex;
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

float Random(float2 uv)
{
	return frac(sin(dot(uv + time, float2(12.9898, 78.233))) * 43758.5453);
}

Ray GetRay(float2 pixel, int w, int h)
{
	Ray ray;

    float normalizedX = (2.0f * pixel.x) / w - 1.0f;
    float normalizedY = 1.0f - (2.0f * pixel.y) / h;

	ray.orig = float3(cameraPosition);
    float3 pos0 = mul(float4(normalizedX, normalizedY, 0.0f, 1.0f), invView).xyz;
    float3 pos1 = mul(float4(normalizedX, normalizedY, 1.0f, 1.0f), invView).xyz;
    ray.dir = normalize(pos1 - pos0);
    ray.t = FLT_MAX;
    ray.index = 0;
	return ray;
}

float4 GetColor(Ray ray)
{
	int rayBounces = RAY_BOUNCES;

	float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);

	float tMin = T_MIN;
	float tMax = T_MAX;

    for (int i = 0; i < nobjects; ++i)
    {
        ObjectInfo o = objectInfos[i];
        if (IntersectAABB(ray, o.aabb_min, o.aabb_max))
        {
            Ray oray;
            oray.orig = mul(float4(ray.orig, 1.0f), o.world);
            oray.dir = mul(float4(ray.dir, 1.0f), o.world);
            oray.t = FLT_MAX;
			
            // Stack for BVH traversal
            uint stack[MAX_STACK_SIZE];
            int stackSize = 0;
            stack[stackSize++] = 0;

            while (stackSize > 0 && stackSize < MAX_STACK_SIZE)
            {
                uint current = stack[--stackSize];

                BVHNode node = objects[current + o.objectOffset];

                
                if (is_leaf(node))
                {
					float t;
                    uint d;
                    if (IntersectTri(oray, t, d, o.indexOffset + index(node)))
                    {
                        oray.t = t;
                        oray.index = d;
                        break;
                    }
                }
                else
                {
                    if (IntersectAABB(oray, node))
                    {
                        stack[stackSize++] = left_child(node);
                        stack[stackSize++] = right_child(node);
                    }
                }
            }

			if (oray.t < T_MAX)
            {
                return float4(1.0f, 1.0f, 1.0f, 1.0f);
            }
        }
    }
    return float4(0.5f, 0.5f, 0.5f, 1.0f);
}

#define NTHREADS 32
#define DENSITY 0.8f

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

    uint2 npixels = { DENSITY * blockSizeX, DENSITY * blockSizeY };
	
    for (int x = 0; x <= npixels.x; ++x)
    {
        for (int y = 0; y <= npixels.y; ++y)
        {
            uint2 pixel = { blockStartX + (float) blockSizeX * random((blockStartX + 1) * x * 10 + y), blockStartY + (float) blockSizeY * random((blockStartY + 1) * y * 10 + x) };
            // Calculate the global pixel coordinates within the texture
			Ray ray = GetRay(pixel, w, h);
			result = GetColor(ray);
            output[pixel] = result;
        }
    }
}