
#include "../Common/ShaderStructs.hlsli"
#include "../Common/Utils.hlsli"

cbuffer externalData : register(b0)
{
    uint type;
    uint count;
    float3 cameraPosition;
    uint light_type;
    matrix view;
    matrix projection;
    uint debug;
}

Texture2D<float4> input : register(t0);
RWTexture2D<float4> output : register(u0);
Texture2D<float4> normals : register(t1);
Texture2D<float4> positions : register(t2);
Texture2D<float4> prev_output : register(t3);
Texture2D<float4> motion_texture : register(t4);
Texture2D<float4> prev_position_map: register(t5);
Texture2D<float4> dispersion: register(t6);

#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 pixel = float2(DTid.x, DTid.y);

    uint2 input_dimensions;
    uint2 normals_dimensions;
    {
        uint w = 0;
        uint h = 0;
        input.GetDimensions(w, h);
        input_dimensions.x = w;
        input_dimensions.y = h;

        normals.GetDimensions(w, h);
        normals_dimensions.x = w;
        normals_dimensions.y = h;
    }
    uint2 info_ratop = normals_dimensions / input_dimensions;

    float2 info_pixel = pixel * normalRatio;
    float3 p0_normal = normals[info_pixel].xyz;
    float3 p0_position = positions[info_pixel].xyz;
    RaySource ray_source = fromColor(positions[info_pixel], normals[info_pixel]);
    float4 c = float4(0.0f, 0.0f, 0.0f, 0.0f);

#define RATIO 8.0f

    uint2 pos_p00 = p00 * info_ratio;
	uint2 pos_p11 = p11 * info_ratio;
	uint2 pos_p01 = p01 * info_ratio;
	uint2 pos_p10 = p10 * info_ratio;
	uint2 in_pos = p0_position;

	float3 wp00 = positions[pos_p00].xyz;
	float3 wp11 = positions[pos_p11].xyz;
	float3 wp01 = positions[pos_p01].xyz;
	float3 wp10 = positions[pos_p10].xyz;
	float3 wpxx = p0_normal;

	// Calculate distances in 3D space between the sample point and the four surrounding points
	float d00 = dist2(wpxx - wp00);  // Distance to wp00
	float d11 = dist2(wpxx - wp11);  // Distance to wp11
	float d01 = dist2(wpxx - wp01);  // Distance to wp01
	float d10 = dist2(wpxx - wp10);  // Distance to wp10

	// Small epsilon to avoid division by zero
	float epsilon = 1e-4;

	// Calculate weights based on inverse distance (closer points have higher weight)
	float w00 = 1.0f / max(d00, epsilon);
	float w11 = 1.0f / max(d11, epsilon);
	float w01 = 1.0f / max(d01, epsilon);
	float w10 = 1.0f / max(d10, epsilon);

	float3 n00 = normals[pos_p00].xyz;
	float3 n11 = normals[pos_p11].xyz;
	float3 n01 = normals[pos_p01].xyz;
	float3 n10 = normals[pos_p10].xyz;
	float3 nxx = normals[in_pos].xyz;

	w00 *= pow(dot(nxx, n00), 10.0f);
	w11 *= pow(dot(nxx, n00), 10.0f);
	w01 *= pow(dot(nxx, n00), 10.0f);
	w10 *= pow(dot(nxx, n00), 10.0f);

	// Normalize weights
	float totalWeight = w00 + w11 + w01 + w10;

	w00 /= totalWeight;
	w11 /= totalWeight;
	w01 /= totalWeight;
	w10 /= totalWeight;

	return (input[p00] * w00 + input[p11] * w11 + input[p01] * w01 + input[p10] * w10);
}
