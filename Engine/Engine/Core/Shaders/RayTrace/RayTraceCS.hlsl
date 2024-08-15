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

#define REFLEX_ENABLED 1
#define REFRACT_ENABLED 2

cbuffer externalData : register(b0)
{
    uint frame_count;
    float time;
    float3 cameraPosition;
    float3 cameraDirection;

    uint enabled;
    uint nobjects;
    ObjectInfo objectInfos[MAX_OBJECTS];
    MaterialColor objectMaterials[MAX_OBJECTS];

    //Lights
    AmbientLight ambientLight;
	DirLight dirLights[MAX_LIGHTS];
	PointLight pointLights[MAX_LIGHTS];
	uint dirLightsCount;
	uint pointLightsCount;

	float4 LightPerspectiveValues[MAX_LIGHTS / 2];
	matrix DirPerspectiveMatrix[MAX_LIGHTS];
}


RWTexture2D<float4> output0 : register(u0);
RWTexture2D<float4> output1 : register(u1);
RWTexture2D<float4> prev_output0 : register(u2);
RWTexture2D<float4> prev_output1 : register(u3);

RWTexture2D<float> props : register(u4);
RWTexture2D<float4> ray0 : register(u5);
RWTexture2D<float4> ray1 : register(u6);
RWTexture2D<float4> bloom : register(u7);

StructuredBuffer<BVHNode> objects: register(t2);
StructuredBuffer<BVHNode> objectBVH: register(t3);

ByteAddressBuffer vertexBuffer : register(t4);
ByteAddressBuffer indicesBuffer : register(t5);
Texture2D<float4> position_map : register(t6);
Texture2D<float4> prev_position_map : register(t7);
Texture2D<float4> motionTexture : register(t8);
Texture2D<float4> depth_map : register(t9);
Texture2D<float4> prev_depth_map : register(t10);

Texture2D<float4> DiffuseTextures[MAX_OBJECTS];
Texture2D<float> DirShadowMapTexture[MAX_LIGHTS];
TextureCube<float> PointShadowMapTexture[MAX_LIGHTS];

//Packed array
static float2 lps[MAX_LIGHTS] = (float2[MAX_LIGHTS])LightPerspectiveValues;

#include "../Common/SimpleLight.hlsli"
#include "../Common/RayFunctions.hlsli"

float3 GetColor(Ray origRay, out float3 bloom, out float ray_distance)
{

    static const float max_distance = 500.0f;
    ray_distance = max_distance;
    uint stack[MAX_STACK_SIZE];
    bool collide = false;
    IntersectionResult result;

    Ray ray = origRay;
    float3 finalColor = float3(0.0f, 0.0f, 0.0f);
    bloom = float3(0.0f, 0.0f, 0.0f);

    bool end = false;
    while (!end)
    {
        result.distance = FLT_MAX;
        end = true;
        collide = false;
        for (uint i = 0; i < nobjects; ++i)
        {
            ObjectInfo o = objectInfos[i];
            float objectExtent = length(o.aabb_max - o.aabb_min);
            float distanceToObject = length(o.position - origRay.orig) - objectExtent;

            if (distanceToObject < max_distance && distanceToObject < result.distance && IntersectAABB(ray, o.aabb_min, o.aabb_max))
            {
                RayObject oray;
                IntersectionResult object_result;
                object_result.distance = FLT_MAX;

                // Transform the ray direction from world space to object space
                oray.orig = mul(ray.orig, o.inv_world);
                oray.orig /= oray.orig.w;
                oray.dir = normalize(mul(ray.dir, (float3x3) o.inv_world));
                oray.t = FLT_MAX;

                int stackSize = 0;
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
                               object_result.object = i;
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
                    float distance = length(pos - origRay.orig);
                    if (distance < result.distance)
                    {
                        collide = true;
                        ray.t = distance;
                        result.v0 = object_result.v0;
                        result.v1 = object_result.v1;
                        result.v2 = object_result.v2;
                        result.vindex = object_result.vindex;
                        result.distance = distance;
                        result.uv = object_result.uv;
                        result.object = i;
                    }
                }
            }
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
            ray_distance = result.distance;
            normal = normalize(mul(normal, (float3x3)o.world));
            float4 pos = mul(float4(opos, 1.0f), o.world);
            pos /= pos.w;
            MaterialColor material = objectMaterials[result.object];

            float3 color = float3(0.0f, 0.0f, 0.0f);

            color += CalcAmbient(normal);

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

            float4 emission = material.emission * float4(material.emission_color, 1.0f);
            bloom += emission;

            finalColor += color * ray.ratio * o.opacity;
            ray.orig = pos;

            //If not opaque surface, generate a refraction ray
            if (ray.bounces < 3 && o.opacity < 1.0f) {
                if (ray.bounces % 2 == 0) {
                    normal = -normal;
                }
                ray = GetRefractedRayFromRay(ray, ray.density,
                    ray.density != 1.0 ? 1.0f : o.density,
                    normal, ray.ratio * (1.0f - o.opacity));
                end = false;
            }
        }
        else {
            //Color background

        }
    }
   
    return finalColor;
}


#define DENSITY 1.0f
#define DIVIDER 2
#define DIVIDER2 4
#define NTHREADS 32

[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 dimensions;
    float2 ray_map_dimensions;
    float2 bloom_dimensions;
    {
        uint w, h;
        output0.GetDimensions(w, h);
        dimensions.x = w;
        dimensions.y = h;

        ray0.GetDimensions(w, h);
        ray_map_dimensions.x = w;
        ray_map_dimensions.y = h;

        bloom.GetDimensions(w, h);
        bloom_dimensions.x = w;
        bloom_dimensions.y = h;
    }

    uint f = frame_count % (DIVIDER2);

    float x = (float)DTid.x;
    float y = (float)DTid.y;

    float2 rayMapRatio = ray_map_dimensions / dimensions;
    float2 bloomRatio = bloom_dimensions / dimensions;

    float4 color0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 color1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float3 bloomColor = { 0.0f, 0.0f, 0.0f };

    float2 pixel = float2(x, y);
    float2 ray_pixel = pixel * rayMapRatio;

    RaySource ray_source = fromColor(ray0[ray_pixel], ray1[ray_pixel]);

    props[pixel] = props[pixel]*0.5f + ray_source.dispersion*0.5f;
    if (ray_source.dispersion >= 1.0f || ray_source.reflex < Epsilon || length(ray_source.normal) < Epsilon)
    {
        output0[pixel] = color0;
        output1[pixel] = color1;
        return;
    }

    //Reflected ray
    if (ray_source.opacity > Epsilon && (enabled & REFLEX_ENABLED)) {
        Ray ray = GetReflectedRayFromSource(ray_source);
        if (length(ray.dir) > Epsilon)
        {
            Ray ray_orig = ray;
            int count = 1;
            float distance = FLT_MAX;
            float distance_att = max(distance * ray_source.dispersion * 0.3f, 1.0f);
            color0 = float4(GetColor(ray, bloomColor, distance), 1.0f);
            if (ray_source.dispersion > 0.0f) {
                float3 v0, v1;
                GetPerpendicularPlane(ray_orig.dir, v0, v1);
                float3 dirs[3][2] = { {v1, v0 - v1, -v0 - v1 },
                                        {-v1, -v0 + v1, v0 + v1 } };

                float loops = (int)(ray_source.dispersion * 5.0f);
                float delta = 0.0f;
                float dist;
                for (int l = 0; l <= loops && delta < distance * 0.005f; ++l) {
                    int ld = l % 2;
                    delta = ray_source.dispersion * 0.01f * (l + 1.0f);
                    for (int i = 0; i < 3; ++i) {
                        float3 rdir = ray_orig.dir + dirs[i][ld] * delta;
                        Ray ray2 = ray_orig;
                        ray2.dir = rdir;
                        float4 c = float4(GetColor(ray2, bloomColor, dist), 1.0f);
                        float dist_att = max(dist * ray_source.dispersion * 0.3f, 1.0f);
                        color0 += c / dist_att;
                        count++;
                    }
                }
                color0 /= count;
            }
        }
    }

    //Refracted ray
    if (ray_source.opacity < 1.0f && (enabled & REFRACT_ENABLED)) {
        Ray ray = GetRefractedRayFromSource(ray_source);
        if (length(ray.dir) > Epsilon)
        {
            float dummy;
            color1 = float4(GetColor(ray, bloomColor, dummy), 1.0f) * (1.0f - ray_source.opacity);
        }
    }

    output0[pixel] = color0 * ray_source.reflex;
    output1[pixel] = color1;

    for (uint bx = 0; bx < bloomRatio.x; ++bx) {
        for (uint by = 0; by < bloomRatio.y; ++by) {
            uint2 bpixel = { pixel.x * bloomRatio.x + bx, pixel.y * bloomRatio.y + by };
            bloom[bpixel] += float4(bloomColor * 0.8f, 1.0f);
        }
    }
}
