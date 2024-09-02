
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
	float RATIO;
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

	float2 center = round(pixel / RATIO) * RATIO;

	float totalW = 0.0f;

	static const float epsilon = 1e-4;
	
	float camDist = dist2(cameraPosition - p0_position);
	float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);
	[unroll]
	for (int x = -2; x <= 2; ++x) {
		[unroll]
		for (int y = -2; y <= 2; ++y) {
			float2 p = center + float2(x * RATIO, y * RATIO);
			uint2 pos_p = p * info_ratio;
			float dist = max(dist2(p0_position - positions[pos_p].xyz) * 0.1f/ camDist, epsilon);
			float n = max(pow(dot(p0_normal, normals[pos_p].xyz), 2.0f), epsilon);
			float w = 1.0f / dist;
			color += output[p] * w;
			totalW += w;
		}
	}

	if (totalW > epsilon) {
		output[pixel] = prev_output[pixel] * 0.5f + 0.5f * color / totalW;
	}
}
