
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

RWTexture2D<float4> output : register(u0);
Texture2D<float4> normals : register(t1);
Texture2D<float4> positions : register(t2);
Texture2D<float4> prev_output : register(t3);
Texture2D<float4> motion_texture : register(t4);
Texture2D<float4> prev_position_map: register(t5);


#define NTHREADS 32
[numthreads(NTHREADS, NTHREADS, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	float2 pixel = float2(DTid.x, DTid.y);

	uint2 output_dimensions;
	uint2 normals_dimensions;
	{
		uint w = 0;
		uint h = 0;
		output.GetDimensions(w, h);
		output_dimensions.x = w;
		output_dimensions.y = h;

		normals.GetDimensions(w, h);
		normals_dimensions.x = w;
		normals_dimensions.y = h;
	}
	uint2 info_ratio = normals_dimensions / output_dimensions;

	float2 info_pixel = pixel * info_ratio;
	float3 p0_normal = normals[info_pixel].xyz;
	float3 p0_position = positions[info_pixel].xyz;
	RaySource ray_source = fromColor(positions[info_pixel], normals[info_pixel]);
	float4 c = float4(0.0f, 0.0f, 0.0f, 0.0f);

#define RATIO 8.0f
	float2 p00 = floor(pixel / RATIO) * RATIO;
	float2 p01 = p00 + float2(0.0f, RATIO);
	float2 p10 = p00 + float2(RATIO, 0.0f);
	float2 p11 = p00 + float2(RATIO, RATIO);

	float camDist = dist2(cameraPosition - p0_position);
	uint2 pos_p00 = p00 * info_ratio;
	uint2 pos_p11 = p11 * info_ratio;
	uint2 pos_p01 = p01 * info_ratio;
	uint2 pos_p10 = p10 * info_ratio;
	
	float3 wp00 = positions[pos_p00].xyz;
	float3 wp11 = positions[pos_p11].xyz;
	float3 wp01 = positions[pos_p01].xyz;
	float3 wp10 = positions[pos_p10].xyz;
	float3 wpxx = p0_position;

	// Calculate distances in 3D space between the sample point and the four surrounding points
	float d00 = dist2(wpxx - wp00) / camDist;  // Distance to wp00
	float d11 = dist2(wpxx - wp11) / camDist;  // Distance to wp11
	float d01 = dist2(wpxx - wp01) / camDist;  // Distance to wp01
	float d10 = dist2(wpxx - wp10) / camDist;  // Distance to wp10

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
	float3 nxx = p0_normal;

	//w00 *= pow(dot(nxx, n00), 10.0f);
	//w11 *= pow(dot(nxx, n00), 10.0f);
	//w01 *= pow(dot(nxx, n00), 10.0f);
	//w10 *= pow(dot(nxx, n00), 10.0f);

	// Normalize weights
	float totalWeight = w00 + w11 + w01 + w10;

	w00 /= totalWeight;
	w11 /= totalWeight;
	w01 /= totalWeight;
	w10 /= totalWeight;

	output[pixel] = prev_output[pixel] * 0.7f + 0.3f * (output[p00] * w00 + output[p11] * w11 + output[p01] * w01 + output[p10] * w10);
}
