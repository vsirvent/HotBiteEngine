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
#define INDIRECT_ENABLED 4
//Walk the top-level BVH over the objects (RenderSystem's `tbvh`, bound as
//objectBVH) instead of testing every object's box for every ray.
//
//OFF, and measured rather than assumed. The hierarchy is correct - the two
//variants render the same frame to within 1/255 - and at a wide viewpoint it is
//the faster structure: on Marbles' sponza from up the atrium this pass went
//2.20 -> 1.68 ms. But that is not where the cost is. `max_distance` is 10 units,
//and BuildCandidateList below applies it ONCE PER PIXEL, which in any real
//interior rejects almost every object for a few ALU ops; the hierarchy re-derives
//the same rejection ONCE PER RAY, and pays dependent 32-byte node loads and a
//second indexable stack to do it. Down at the player's viewpoint that costs
//26 -> 24 fps (a smaller volume stack recovers ~1 of it, no more).
//
//So the flat list plus the per-pixel cull is the better accelerator at
//MAX_OBJECTS = 100 with a 10-unit ray. Turn this on if either changes: many more
//objects, or a ray budget long enough that the cull stops rejecting.
#define USE_OBH 0
#define LEVEL_RATIO 3
//#define BOUNCES
//#define DISABLE_RESTIR

cbuffer externalData : register(b0)
{
    uint frame_count;
    float time;
    float3 cameraPosition;
    float3 cameraDirection;

    uint ray_count;
    int kernel_size;

    //Lights
    AmbientLight ambientLight;
    DirLight dirLights[MAX_LIGHTS];
    PointLight pointLights[MAX_LIGHTS];
    uint dirLightsCount;
    uint pointLightsCount;

    matrix view;
    matrix projection;

    float4 LightPerspectiveValues[MAX_LIGHTS / 2];
    matrix DirPerspectiveMatrix[DIR_SHADOW_MATRIX_COUNT];
}

cbuffer objectData : register(b1)
{
    uint nobjects;
    ObjectInfo objectInfos[MAX_OBJECTS];
    MaterialColor objectMaterials[MAX_OBJECTS];
}

RWTexture2D<float4> output;
Texture2D<float4> ray0;
Texture2D<float4> ray1;

StructuredBuffer<BVHNode> objects: register(t2);
StructuredBuffer<BVHNode> objectBVH: register(t3);

ByteAddressBuffer vertexBuffer : register(t4);
ByteAddressBuffer indicesBuffer : register(t5);
Texture2D<float4> position_map : register(t6);
Texture2D<float4> motion_texture : register(t8);
Texture2D<float4> prev_position_map: register(t9);
Texture2D<uint4> restir_pdf_0: register(t10);
Texture2D<float> restir_w_0: register(t11);
RWTexture2D<uint4> restir_pdf_1: register(u0);
RWTexture2D<uint> tiles_output: register(u1);

Texture2D<float4> DiffuseTextures[MAX_OBJECTS];
//No DirStaticShadowMapTexture here: DiffuseTextures[MAX_OBJECTS] leaves no room for
//another MAX_LIGHTS array within the 128 texture registers (X4565). See SimpleLight.hlsli.
Texture2DArray<float> DirShadowMapTexture[MAX_LIGHTS];
TextureCube<float> PointShadowMapTexture[MAX_LIGHTS];



//Packed array
static float2 lps[MAX_LIGHTS] = (float2[MAX_LIGHTS])LightPerspectiveValues;

#include "../Common/SimpleLight.hlsli"
#include "../Common/RayFunctions.hlsli"

#define max_distance 10.0f

static const float inv_ray_count = 1.0f / (float)ray_count;
static const uint stride = kernel_size * ray_count;

static const uint max_x = ray_count * kernel_size;
static const uint max_y = stride * kernel_size;

static const uint N = ray_count * kernel_size * kernel_size;
static const float space_size = (float)N / (float)ray_count;
static const float ray_enery_unit = inv_ray_count;



//Inverse-CDF pick of one ray out of the pdf cache. `stratum` is which of the
//ray_count equal slices of the CDF to draw from, `jitter` where inside that slice
//to land, and it must be random in [0,1): sampling the slice's *left edge* is what
//the estimator below cannot be paired with. At jitter 0 the target of stratum 0 is
//exactly 0, and the first pdf entry is always positive (RAY_W_BIAS), so stratum 0
//returns ray 0 unconditionally - whatever its probability. That ray then gets the
//weight 1/pdf[0] of a sample that was supposed to be drawn *with* probability
//pdf[0]/W, which for a cold entry is the 2/wis_size clamp, i.e. ~16x the share one
//direction out of ray_count is worth. Since the stratum set is picked from `start`,
//which is a function of the pixel, the over-bright pick lands on the same diagonal
//in every tile and sweeps across the scene as `start` cycles - the scanning band.
//With the jitter each ray is selected with probability pdf/W, which is exactly what
//the 1/pdf weighting assumes, and no ray is picked for free.
uint GetRayIndex(float pdf_cache[MAX_RAYS], float w, float stratum, float jitter) {
#ifdef DISABLE_RESTIR
    return stratum;
#endif
    float tmp_w = 0.0f;
    float target = (stratum + jitter) * w * inv_ray_count;

    for (uint i = 0; i < ray_count; i++) {
            tmp_w += pdf_cache[i];
            if (tmp_w > target) {
                break;
            }
    }
    //If the accumulated weights never reach the target, i ends at ray_count: clamp to a valid ray
    return min(i, ray_count - 1);
}

//Smallest level k >= 1 such that 1 + LEVEL_RATIO * k * (k + 1) / 2 >= index.
//Closed form of the previous linear search, which also returned 0 (division by zero
//in the caller) for index <= 1.
float HemisphereLevelFromIndex(float index)
{
    float m = 2.0f * (index - 1.0f) / (float)LEVEL_RATIO;
    float k = ceil((sqrt(1.0f + 4.0f * m) - 1.0f) * 0.5f);
    return max(k, 1.0f);
}

float3 GenerateHemisphereRay(float3 dir, float3 tangent, float3 bitangent, float dispersion, float N, float NLevels, float rX)
{
    float index = (rX * dispersion) % N;

    float level = HemisphereLevelFromIndex(index);
    float cumulativePoints = 1.0f + (float)LEVEL_RATIO * level * (level + 1.0f) * 0.5f;

    float pointsAtLevel = level * LEVEL_RATIO;

    // Calculate local index within the current level
    float localIndex = index - cumulativePoints;

    float phi = (level * M_PI * 0.5f) / NLevels;

    // Azimuthal angle (theta) based on number of points at this level
    float theta = (2.0f * M_PI) * localIndex / pointsAtLevel; // Spread points evenly in azimuthal direction
        

    // Convert spherical coordinates to Cartesian coordinates
    float sinPhi = sin(phi);
    float cosPhi = cos(phi);
    float sinTheta = sin(theta);
    float cosTheta = cos(theta);

    // Local ray direction in spherical coordinates
    float3 localRay = float3(sinPhi * cosTheta, cosPhi, sinPhi * sinTheta);

    // Convert local ray to global coordinates (tangent space to world space)
    float3 globalRay = localRay.x * tangent + localRay.y * dir + localRay.z * bitangent;


    return normalize(dir + globalRay);
}

struct RayTraceColor {
    float3 color;
    bool hit;
};

#if !USE_OBH
//Objects within max_distance of the pixel origin are the same for every ray of the
//pixel: cull the object list once per pixel instead of once per ray. A bitmask keeps
//the per-thread storage at 4 registers instead of a spilled index array.
//
//This is the mitigation for not having a top-level BVH, and goes away with it: the
//hierarchy rejects whole groups of objects per ray for the cost of one box test,
//where this still touches every object once per pixel.
#define CANDIDATE_WORDS ((MAX_OBJECTS + 31) / 32)
static uint candidate_mask[CANDIDATE_WORDS];

void BuildCandidateList(float3 orig)
{
    uint w = 0;
    for (w = 0; w < CANDIDATE_WORDS; ++w) {
        candidate_mask[w] = 0;
    }
    for (uint i = 0; i < nobjects; ++i) {
        ObjectInfo o = objectInfos[i];
        float objectExtent = length(o.aabb_max - o.aabb_min);
        float distanceToObject = length(o.position - orig) - objectExtent;
        if (distanceToObject < max_distance) {
            candidate_mask[i >> 5] |= 1u << (i & 31);
        }
    }
}

bool IsCandidate(uint i)
{
    return (candidate_mask[i >> 5] & (1u << (i & 31))) != 0;
}
#endif

void GetColor(Ray origRay, float rX, float level, uint max_bounces, out RayTraceColor out_color, float dispersion, bool mix, bool refract)
{
    out_color.color = float3(0.0f, 0.0f, 0.0f);
    out_color.hit = false;

    float collision_dist = 0.0f;
#if USE_OBH
    //Sized for the object hierarchy, not for a mesh's: MAX_OBJECTS is 100, so a
    //balanced tree is ~7 deep and two entries per level is ample. The mesh stack
    //below stays at MAX_STACK_SIZE. These arrays are indexable temporaries and
    //this shader is already at the cs_5_0 register limit, so the difference is
    //occupancy rather than memory.
    uint volumeStack[MAX_VOLUME_STACK_SIZE];
#endif
    uint stack[MAX_STACK_SIZE];
    RayObject oray;
    IntersectionResult object_result;

    bool collide = false;
    IntersectionResult result;
    Ray ray = origRay;
    bool end = false;
#ifdef BOUNCES
    while (!end) {
#endif
        result.distance = FLT_MAX;
        end = true;
        collide = false;
#if USE_OBH
        uint volumeStackSize = 0;
        volumeStack[volumeStackSize++] = 0;
        uint i = 0;
        while (volumeStackSize > 0 && volumeStackSize < MAX_VOLUME_STACK_SIZE)
        {
#else
        for (uint ci = 0; ci < nobjects; ++ci)
        {
            uint objectIndex = ci;
            if (!IsCandidate(ci)) {
                continue;
            }
#endif

#if USE_OBH
            uint currentVolume = volumeStack[--volumeStackSize];

            BVHNode volumeNode = objectBVH[currentVolume];
            [branch]
            if (is_leaf(volumeNode))
            {
                uint objectIndex = index(volumeNode);
                ObjectInfo o = objectInfos[objectIndex];

                float objectExtent = length(o.aabb_max - o.aabb_min);
                float distanceToObject = length(o.position - origRay.orig.xyz) - objectExtent;
                [branch]
                if (distanceToObject < max_distance && distanceToObject < result.distance && IntersectAABB(ray, o.aabb_min, o.aabb_max))
                {
#else
            ObjectInfo o = objectInfos[objectIndex];
            if (IntersectAABB(ray, o.aabb_min, o.aabb_max))
            {
#endif
                object_result.distance = FLT_MAX;

                // Transform the ray direction from world space to object space
                oray.orig = mul(ray.orig, o.inv_world);
                oray.orig /= oray.orig.w;
                oray.dir = normalize(mul(ray.dir, (float3x3) o.inv_world));
                oray.t = FLT_MAX;

                //Descend the tree with the child already in hand instead of
                //pushing both children and popping one: every internal node used
                //to be fetched three times over (once as each parent's child, to
                //order it, and once again when it came off the stack). Now a node
                //is fetched once, as a child, and the nearer one is stepped into
                //directly; only the far child goes on the stack.
                //
                //`limit` tightens as triangles are hit, which is what makes the
                //near-first order pay: the far subtree is usually rejected by a
                //hit found in the near one.
                float3 invDir = 1.0f / oray.dir;
                uint stackSize = 0;
                uint base = o.objectOffset;
                BVHNode node = objects[base];
                bool traverse = aabb_entry(oray.orig.xyz, invDir, node) < FLT_MAX;

                [loop]
                while (traverse)
                {
                    [branch]
                    if (is_leaf(node))
                    {
                        IntersectionResult tmp_result;
                        if (IntersectTri(oray, index(node) + o.indexOffset, o.vertexOffset,
                                         object_result.distance, tmp_result))
                        {
                            object_result = tmp_result;
                            object_result.object = objectIndex;
                        }
                    }
                    else
                    {
                        uint left_node_index = left_child(node);
                        uint right_node_index = right_child(node);
                        BVHNode left_node = objects[base + left_node_index];
                        BVHNode right_node = objects[base + right_node_index];

                        //Two gates per child, and they are different questions. The
                        //slab entry is "does the ray reach this box, and could it
                        //hold anything nearer than the best hit so far".
                        float left_t = aabb_entry(oray.orig.xyz, invDir, left_node);
                        float right_t = aabb_entry(oray.orig.xyz, invDir, right_node);
                        //And this is `max_distance`, which is NOT a limit along the
                        //ray: it is how far from the pixel the tracer looks at all,
                        //the same distance-from-origin test BuildCandidateList
                        //applies to whole objects. Gating the ray's *length* by it
                        //instead looks like an optimisation and is a behaviour
                        //change - the boxes of a big wall are entered 20+ units
                        //along a ray whose origin sits well inside max_distance of
                        //them, and rejecting those took 80% of the indirect light
                        //out of sponza.
                        float left_reach = node_distance(left_node, oray.orig.xyz);
                        float right_reach = node_distance(right_node, oray.orig.xyz);

                        bool go_left = left_t < object_result.distance && left_reach < max_distance;
                        bool go_right = right_t < object_result.distance && right_reach < max_distance;
                        [branch]
                        if (go_left && go_right)
                        {
                            //Near child now, far child later - and dropped rather
                            //than overflowing the stack, which loses one subtree
                            //where bailing out of the loop lost every pending one.
                            //Written as a branch and not a ternary: fxc will not
                            //select between two struct values (X3020).
                            [branch]
                            if (left_t <= right_t)
                            {
                                if (stackSize < MAX_STACK_SIZE) {
                                    stack[stackSize++] = right_node_index;
                                }
                                node = left_node;
                            }
                            else
                            {
                                if (stackSize < MAX_STACK_SIZE) {
                                    stack[stackSize++] = left_node_index;
                                }
                                node = right_node;
                            }
                            continue;
                        }
                        else if (go_left)
                        {
                            node = left_node;
                            continue;
                        }
                        else if (go_right)
                        {
                            node = right_node;
                            continue;
                        }
                    }

                    [branch]
                    if (stackSize == 0) {
                        break;
                    }
                    node = objects[base + stack[--stackSize]];
                }

                if (object_result.distance < FLT_MAX) {
                    float3 opos = bary_position(object_result);
                    float4 pos = mul(float4(opos, 1.0f), o.world);
                    float distance = length(pos - ray.orig);
                    if (distance < result.distance)
                    {
                        collide = true;
                        collision_dist = distance;
                        ray.t = distance;
                        result = object_result;
                        result.distance = distance;
                        result.object = objectIndex;
                    }
                }
            }
#if USE_OBH
                }
 else 
     [branch]
     if (IntersectAABB(ray, volumeNode)) {


         uint left_node_index = left_child(volumeNode);
         uint right_node_index = right_child(volumeNode);

         BVHNode left_node = objectBVH[left_node_index];
         BVHNode right_node = objectBVH[right_node_index];

         float left_dist = node_distance(left_node, ray.orig.xyz);
         float right_dist = node_distance(right_node, ray.orig.xyz);

         float dists[2] = { left_dist, right_dist };
         uint node_indices[2] = { left_node_index, right_node_index };

         if (dists[0] > dists[1]) {
             float temp_dist = dists[0];
             dists[0] = dists[1];
             dists[1] = temp_dist;

             uint temp_index = node_indices[0];
             node_indices[0] = node_indices[1];
             node_indices[1] = temp_index;
         }

         [unroll]
         for (int i = 0; i < 2; ++i) {
             if (dists[i] < result.distance && dists[i] < max_distance) {
                 volumeStack[volumeStackSize++] = node_indices[i];
             }
         }
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
        float3 opos = bary_position(result);
        float3 normal = (1.0f - result.u - result.v) * normal0 + result.u * normal1 + result.v * normal2;
        normal = normalize(mul(normal, (float3x3)o.world));
        float4 pos = mul(float4(opos, 1.0f), o.world);
        pos /= pos.w;
        MaterialColor material = objectMaterials[result.object];

        float3 color = float3(0.0f, 0.0f, 0.0f);

 
        uint i = 0;
        for (i = 0; i < dirLightsCount; ++i) {
            color += CalcDirectional(normal, pos.xyz, dirLights[i], i);
        }

        // Calculate the point lights
        for (i = 0; i < pointLightsCount; ++i) {
            if (length(pos.xyz - pointLights[i].Position) < pointLights[i].Range) {
                color += CalcPoint(normal, pos.xyz, pointLights[i], i);
            }
        }

        color *= o.opacity;


        bool use_mat_texture = material.flags & DIFFUSSE_MAP_ENABLED_FLAG;
        float3 mat_color = material.diffuseColor.rgb * !use_mat_texture;

        float2 uv0 = asfloat(vertexBuffer.Load2(result.vindex.x + 24));
        float2 uv1 = asfloat(vertexBuffer.Load2(result.vindex.y + 24));
        float2 uv2 = asfloat(vertexBuffer.Load2(result.vindex.z + 24));
        float2 uv = uv0 * (1.0f - result.u - result.v) + uv1 * result.u + uv2 * result.v;

        mat_color += GetDiffuseColor(result.object, uv) * use_mat_texture;

        color.rgb *= mat_color;

        float3 emission = material.emission * material.emission_color;
        color.rgb += emission;
        
        out_color.color += color * ray.ratio;

        ray.orig = pos;
        out_color.hit = true;
#ifdef BOUNCES
        if (material.emission <= Epsilon && ray.bounces < max_bounces && o.opacity > 0.0f) {
            ray = GetReflectedRayFromRay(ray, normal, ray.ratio);
            float3 tangent;
            float3 bitangent;
            GetSpaceVectors(ray.dir, tangent, bitangent);
            ray.dir = GenerateHemisphereRay(ray.dir, tangent, bitangent, (1.0f - material.specIntensity), N, level, rX);
            end = false;
        }
    }
#endif
    }
}

bool IsLowEnergy(float pdf[MAX_RAYS], uint len) {
        
    float total_enery = 0.0f;

    for (uint i = 0; i < len; ++i) {
        total_enery += pdf[i];
    }
    float threshold = len * RAY_W_BIAS;

    return (total_enery < threshold);
}

#define NTHREADS 8

[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    float2 dimensions;
    float2 ray_map_dimensions;
    {
        uint w, h;
        output.GetDimensions(w, h);
        dimensions.x = w;
        dimensions.y = h;

        ray0.GetDimensions(w, h);
        ray_map_dimensions.x = w;
        ray_map_dimensions.y = h;
    }
    float x = (float)DTid.x;
    float y = (float)DTid.y;

    float2 rayMapRatio = ray_map_dimensions / dimensions;
        
    float2 pixel = float2(x, y);
    float2 rpixel = pixel * rayMapRatio;
    float2 ray_pixel = round(rpixel);

    RaySource ray_source = fromColor(ray0[ray_pixel], ray1[ray_pixel]);
    bool end = (ray_source.reflex <= Epsilon || dist2(ray_source.normal) <= Epsilon);

    float3 orig_pos = ray_source.orig.xyz;
    float toCamDistance = dist2(orig_pos - cameraPosition);
   
    float3 tangent;
    float3 bitangent;
    RayTraceColor rc;
    Ray ray = GetReflectedRayFromSource(ray_source);

    float pdf_cache[MAX_RAYS];
    UnpackRays(restir_pdf_0[pixel], RAY_W_SCALE, pdf_cache);
    uint i = 0;

    end = end || (dist2(ray.dir) <= Epsilon);
    [branch]
    if (end)
    {
        for (i = 0; i < ray_count; i++) {
            pdf_cache[i] = max(pdf_cache[i] - 0.01f, RAY_W_BIAS);
        }
        restir_pdf_1[pixel] = PackRays(pdf_cache, RAY_W_SCALE);
        output[pixel] = float4(0.0f, 0.0f, 0.0f, -1.0f);
        return;
    }
    
    float3 normal = ray_source.normal;
    float3 orig_dir = ray.dir;

    float level = HemisphereLevelFromIndex((float)N) + 1.0f;

    GetSpaceVectors(normal, tangent, bitangent);

#if !USE_OBH
    BuildCandidateList(orig_pos);
#endif

    //Check if this is a low enery pixel. Must be done on the raw unpacked values:
    //after the bias floor below the total can never go under the threshold.
    uint low_energy = IsLowEnergy(pdf_cache, ray_count);

    float2 mvector = motion_texture[ray_pixel].xy;
    float motion = 0.0f;
    if (mvector.x > -FLT_MAX) {
        motion = dist2(mvector);
    }

    float w_pixel = max(restir_w_0[pixel], RAY_W_BIAS * ray_count);

    //The pdf cache is stored per screen pixel and is not reprojected, so any motion
    //makes it describe a different surface point. Blend it toward a flat distribution
    //proportionally to how many pixels the point moved: selection degrades to uniform
    //sampling instead of importance-amplifying stale directions (smearing artifacts).
    float pixels_moved = sqrt(motion) * dimensions.x * 0.5f;
    float stale = saturate(pixels_moved * 0.25f);
    float flat_pdf = w_pixel * inv_ray_count;
    for (i = 0; i < ray_count; i++) {
        pdf_cache[i] = lerp(max(pdf_cache[i], RAY_W_BIAS), flat_pdf, stale);
    }

    float wis[MAX_RAYS];
    uint wis_size = 0;
    uint last_wi = MAX_RAYS + 1;

    uint pixel_seed = hash((uint)pixel.x * 73856093u ^ (uint)pixel.y * 19349663u);
    uint jitter_seed = hash(pixel_seed + frame_count * 9781u);

#ifdef DISABLE_RESTIR
    uint start = 0;
    uint step = 1;
#else
    float motion_ratio = 1.0f / max(100.0f * sqrt(motion) * toCamDistance, 0.01f);
    //start cycles every frame for every pixel so all CDF strata get revisited over
    //time; otherwise rays in the lower half of the CDF keep a stale pdf forever.
    //The per-pixel term is hashed rather than pixel.x + pixel.y: a linear ramp gives
    //every pixel on a diagonal the same strata, so whatever variance a stratum
    //carries is drawn as a coherent line one pixel wide repeating every ray_count
    //pixels, and the +frame_count sweeps it across the scene. Hashed, neighbours are
    //uncorrelated and the same variance is left as noise, which is what the kernel
    //below and the denoiser are built to average away.
    uint start = (pixel_seed + frame_count) % ray_count;
    uint step = frame_count % 8 + ray_count / 2 + (uint)((ray_count * motion_ratio) * low_energy);
#endif
    for (i = 0; i < ray_count; i += step) {
        uint index = (i + start) % ray_count;
        uint wi = GetRayIndex(pdf_cache, w_pixel, index, random(jitter_seed + i * 26699u));
        wis[wis_size] = wi;
        wis_size += (last_wi != wi);
        last_wi = wi;
    }
 
    float4 color_diffuse = float4(0.0f, 0.0f, 0.0f, 1.0f);
    float offset = ((pixel.x) % kernel_size) * ray_count + ((pixel.y)% kernel_size) * stride;
    float offset2 = space_size;

    //Resampled-importance weight: rays are drawn with probability pdf/W, so the
    //estimate of the mean over the full ray set is (W / (ray_count * n)) * sum(f / pdf).
    //This keeps the pixel brightness equivalent to tracing all ray_count rays.
    float ris_w = w_pixel * inv_ray_count / (float)wis_size;

    bool hit = false;
    for (i = 0; i < wis_size; ++i) {

        uint wi = wis[i];
        float n = fmod(offset + (float)wi * offset2, N);

        ray.dir = GenerateHemisphereRay(normal, tangent, bitangent, 1.0f, N, level, n);
        ray.orig.xyz = orig_pos.xyz + ray.dir * 0.001f;
        float dist = FLT_MAX;
        GetColor(ray, n, level, 1, rc, ray_source.dispersion, true, false);
        last_wi = wi;
        hit = hit || rc.hit;

        float w = length(rc.color.rgb);

#ifdef DISABLE_RESTIR
        pdf_cache[wi] = 1.0f;
        color_diffuse.rgb += rc.color;
#else
        //A converged cache never needs a per-ray weight above 1/n (flat pdf case);
        //clamp at 2/n so a stale pdf cannot amplify a ray into a firefly.
        color_diffuse.rgb += rc.color * min(ris_w / pdf_cache[wi], 2.0f / (float)wis_size);
        pdf_cache[wi] = RAY_W_BIAS + w;
#endif
    }

    restir_pdf_1[pixel] = PackRays(pdf_cache, RAY_W_SCALE);
#ifdef DISABLE_RESTIR
    color_diffuse = color_diffuse / ray_count;
#endif

    color_diffuse.rgb = pow(color_diffuse.rgb, 0.5f);
    output[pixel] = color_diffuse;

    if (hit) {
        [unroll]
        for (int x = -2; x <= 2; ++x) {
            [unroll]
            for (int y = -2; y <= 2; ++y) {
                int2 p = (pixel / kernel_size) + int2(x, y);
                tiles_output[p] = 1;
            }
        }
    }
 
    //float r = wis_size / ray_count;
    //output[pixel] = float4(wis_size, 0.0f, 0.0f, 1.0f);
}
