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

#include "Defines.hlsli"

#ifndef __UTILS_HLSLI__
#define __UTILS_HLSLI__

static const float Epsilon = 1e-4;

// Divide the 2D-Dispatch_Grid into tiles of dimension [N, DipatchGridDim.y]
// “CTA” (Cooperative Thread Array) == Thread Group in DirectX terminology
uint2 ThreadGroupTilingX(
	const uint2 dipatchGridDim,		// Arguments of the Dispatch call (typically from a ConstantBuffer)
	const uint2 ctaDim,			// Already known in HLSL, eg:[numthreads(8, 8, 1)] -> uint2(8, 8)
	const uint maxTileWidth,		// User parameter (N). Recommended values: 8, 16 or 32.
	const uint2 groupThreadID,		// SV_GroupThreadID
	const uint2 groupId			// SV_GroupID
)
{
	// A perfect tile is one with dimensions = [maxTileWidth, dipatchGridDim.y]
	const uint Number_of_CTAs_in_a_perfect_tile = maxTileWidth * dipatchGridDim.y;

	// Possible number of perfect tiles
	const uint Number_of_perfect_tiles = dipatchGridDim.x / maxTileWidth;

	// Total number of CTAs present in the perfect tiles
	const uint Total_CTAs_in_all_perfect_tiles = Number_of_perfect_tiles * maxTileWidth * dipatchGridDim.y;
	const uint vThreadGroupIDFlattened = dipatchGridDim.x * groupId.y + groupId.x;

	// Tile_ID_of_current_CTA : current CTA to TILE-ID mapping.
	const uint Tile_ID_of_current_CTA = vThreadGroupIDFlattened / Number_of_CTAs_in_a_perfect_tile;
	const uint Local_CTA_ID_within_current_tile = vThreadGroupIDFlattened % Number_of_CTAs_in_a_perfect_tile;
	uint Local_CTA_ID_y_within_current_tile;
	uint Local_CTA_ID_x_within_current_tile;

	if (Total_CTAs_in_all_perfect_tiles <= vThreadGroupIDFlattened)
	{
		// Path taken only if the last tile has imperfect dimensions and CTAs from the last tile are launched. 
		uint X_dimension_of_last_tile = dipatchGridDim.x % maxTileWidth;
		X_dimension_of_last_tile = max(1, X_dimension_of_last_tile);
		Local_CTA_ID_y_within_current_tile = Local_CTA_ID_within_current_tile / X_dimension_of_last_tile;
		Local_CTA_ID_x_within_current_tile = Local_CTA_ID_within_current_tile % X_dimension_of_last_tile;
	}
	else
	{
		Local_CTA_ID_y_within_current_tile = Local_CTA_ID_within_current_tile / maxTileWidth;
		Local_CTA_ID_x_within_current_tile = Local_CTA_ID_within_current_tile % maxTileWidth;
	}

	const uint Swizzled_vThreadGroupIDFlattened =
		Tile_ID_of_current_CTA * maxTileWidth +
		Local_CTA_ID_y_within_current_tile * dipatchGridDim.x +
		Local_CTA_ID_x_within_current_tile;

	uint2 SwizzledvThreadGroupID;
	SwizzledvThreadGroupID.y = Swizzled_vThreadGroupIDFlattened / dipatchGridDim.x;
	SwizzledvThreadGroupID.x = Swizzled_vThreadGroupIDFlattened % dipatchGridDim.x;

	uint2 SwizzledvThreadID;
	SwizzledvThreadID.x = ctaDim.x * SwizzledvThreadGroupID.x + groupThreadID.x;
	SwizzledvThreadID.y = ctaDim.y * SwizzledvThreadGroupID.y + groupThreadID.y;

	return SwizzledvThreadID.xy;
}

float3 climit3(float3 color) {
	float m = max(color.r, max(color.g, color.b));
	if (m > 1.0f) {
		//Apply saturation to avoid color bleeding
		color /= m;
	}
	return color;
}

float4 climit4(float4 color) {
	float m = max(color.r, max(color.g, color.b));
	if (m > 1.0f) {
		//Apply saturation to avoid color bleeding
		color.rgb /= m;
	}
	return color;
}

float2 GetCloserPixel(float2 pixel, float ratio) {
	return round(round(pixel / ratio) * ratio);
}

float4 GetInterpolatedColor(float2 uv, Texture2D text, float2 dimension) {
	uv = saturate(uv);
	// Calculate the texture coordinates in the range [0, 1]
	float2 texCoords = uv * dimension;

	// Calculate the integer coordinates of the four surrounding pixels
	int2 p00 = (int2)floor(texCoords);
	int2 p11 = (int2)ceil(texCoords);
	int2 p01 = int2(p00.x, p11.y);
	int2 p10 = int2(p11.x, p00.y);

	// Calculate the fractional part of the coordinates
	float2 f = frac(texCoords);

	// Calculate the weights for bilinear interpolation
	float w00 = (1.0f - f.x) * (1.0f - f.y);
	float w11 = f.x * f.y;
	float w01 = (1.0f - f.x) * f.y;
	float w10 = f.x * (1.0f - f.y);

	// Perform the bilinear interpolation
	return (text[p00] * w00 +
		text[p11] * w11 +
		text[p01] * w01 +
		text[p10] * w10);
}

float4 GetInterpolatedColor(float2 uv, RWTexture2D<float4> text, float2 dimension) {
	uv = saturate(uv);
	// Calculate the texture coordinates in the range [0, 1]
	float2 texCoords = uv * dimension;

	// Calculate the integer coordinates of the four surrounding pixels
	int2 p00 = (int2)floor(texCoords);
	int2 p11 = (int2)ceil(texCoords);
	int2 p01 = int2(p00.x, p11.y);
	int2 p10 = int2(p11.x, p00.y);

	// Calculate the fractional part of the coordinates
	float2 f = frac(texCoords);

	// Calculate the weights for bilinear interpolation
	float w00 = (1.0f - f.x) * (1.0f - f.y);
	float w11 = f.x * f.y;
	float w01 = (1.0f - f.x) * f.y;
	float w10 = f.x * (1.0f - f.y);

	// Perform the bilinear interpolation
	return (text[p00] * w00 +
		text[p11] * w11 +
		text[p01] * w01 +
		text[p10] * w10);
}

float dist2(float3 p) {
	return p.x * p.x + p.y * p.y + p.z * p.z;
}

float dist2(float2 p) {
	return p.x * p.x + p.y * p.y;
}

bool IsInteger(float2 value) {
	return all(value == floor(value));
}


// Converts a 2D array index back to 3D coordinates
uint3 Get3DPointFrom2DArray(uint2 index, uint zdim) {
	uint x = index.x;
	uint yz = index.y;

	uint y = yz / zdim;
	uint z = yz % zdim;

	return uint3(x, y, z);
}

float3 GenerateDirection(int i, int N) {
	// Golden ratio to create evenly spaced points
	float phi = (1.0 + sqrt(5.0)) * 0.5;

	// Calculate the spherical coordinates
	float theta = 2.0 * M_PI * float(i) / phi;
	float z = 1.0 - 2.0 * float(i) / float(N - 1); // z ranges from 1 to -1
	float radius = sqrt(1.0 - z * z); // Radius for the xy-plane

	// Convert spherical coordinates to Cartesian
	float x = radius * cos(theta);
	float y = radius * sin(theta);

	return float3(x, y, z);
}

float2 GetPolarCoordinates(float3 dir) {
	float theta = atan2(dir.z, dir.x);
	float phi = acos(dir.y);
	return float2(phi, theta);
}

float3 GetCartesianCoordinates(float phi, float theta) {
	float sinPhi = sin(phi);
	float cosPhi = cos(phi);
	float sinTheta = sin(theta);
	float cosTheta = cos(theta);

	return float3(sinPhi * cosTheta, cosPhi, sinPhi * sinTheta);
}

float3 GetCartesianCoordinates(float2 coords) {
	return GetCartesianCoordinates(coords.x, coords.y);
}

float2 GenerateHemisphereDispersedRay(float3 dir, float3 tangent, float3 bitangent, float dispersion, float N, float NLevels, float rX)
{
	float index = (rX * dispersion) % N;

	//index = (frame_count) % N;
	float N_SQRT = sqrt(N);
	float cumulativePoints = 1.0f;
	float level = 1.0f;
	float c = 1.0f;
	float phi;
	while (c < index) {
		phi = (level * M_PI * 0.5f) / NLevels;
		c = cumulativePoints + N_SQRT * sin(phi);
		cumulativePoints = c;
		level++;
	};
	level--;

	phi = (level * M_PI * 0.5f) / NLevels;

	float pointsAtLevel = 1.0f + N_SQRT * sin(phi);

	float localIndex = index - cumulativePoints;
	float theta = (2.0f * M_PI) * localIndex / pointsAtLevel;

	float3 localRay = GetCartesianCoordinates(phi, theta);

	float3 globalRay = localRay.x * tangent + localRay.y * dir + localRay.z * bitangent;

	return GetPolarCoordinates(normalize(dir + globalRay));
}

bool ValidUVCoord(float2 uv_coord) {
	return (uv_coord.x >= 0.0f && uv_coord.x < 1.0f &&
		uv_coord.y >= 0.0f && uv_coord.y < 1.0f);
}

void GetSpaceVectors(in float3 dir, out float3 tangent, out float3 bitangent) {
	float3 up = abs(dir.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
	tangent = normalize(cross(up, dir));
	bitangent = cross(dir, tangent);
}

// Converts 3D coordinates to a 2D array index
uint2 Get2DPointFrom3DArray(uint3 index, uint zdim) {
	return uint2(index.x, index.y * zdim + index.z);
}

void GetPerpendicularPlane(in float3 dir, out float3 v0, out float3 v1) {
	if (dir.x != 0.0f && dir.y != 0.0f) {
		v0 = normalize(float3(-dir.y, dir.x, 0.0f));
	}
	else {
		v0 = float3(1.0f, 0.0f, 0.0f);
	}
	v1 = normalize(cross(dir, v0));
}

uint ToByte(float val, float range)
{
	val = 255.0f * clamp(val, 0.0f, range) / range;
	return (uint)val;
}

float FromByte(uint val, float range)
{
	float fval = (float)val;
	fval = range * clamp(fval, 0.0f, 255.0f) / 255.0f;
	return fval;
}

int ToI16(float val, float range)
{
	val = 32768.0f * clamp(val, -range, range) / range;
	return (int)val;
}

float FromI16(int val, float range)
{
	float fval = (float)val;
	fval = range * clamp(fval, -32768.0f, 32768.0f) / 32768.0f;
	return fval;
}

uint PackTwoInt16(int low, int high) {
	return ( ((low & 0xFFFF) << 16) | (high & 0xFFFF) );
}

void UnpackTwoInt16(uint packedValue, out int low, out int high) {
	int intPackedValue = asint(packedValue);
	low = (intPackedValue >> 16);
	high = (intPackedValue << 16) >> 16;
}

uint4 Pack4Float2ToI16(float2 values[4], float max_value)
{
	uint4 data;
	data.r = PackTwoInt16(ToI16(values[0].x, max_value), ToI16(values[0].y, max_value));
	data.g = PackTwoInt16(ToI16(values[1].x, max_value), ToI16(values[1].y, max_value));
	data.b = PackTwoInt16(ToI16(values[2].x, max_value), ToI16(values[2].y, max_value));
	data.a = PackTwoInt16(ToI16(values[3].x, max_value), ToI16(values[3].y, max_value));
	return data;
}

void Unpack4Float2FromI16(uint4 data, float max_value, out float2 values[4])
{
	int low;
	int high;
	UnpackTwoInt16(data.r, low, high);
	values[0].x = FromI16(low, max_value);
	values[0].y = FromI16(high, max_value);
	UnpackTwoInt16(data.g, low, high);
	values[1].x = FromI16(low, max_value);
	values[1].y = FromI16(high, max_value);
	UnpackTwoInt16(data.b, low, high);
	values[2].x = FromI16(low, max_value);
	values[2].y = FromI16(high, max_value);
	UnpackTwoInt16(data.a, low, high);
	values[3].x = FromI16(low, max_value);
	values[3].y = FromI16(high, max_value);
}

uint4 Pack8Bytes(float values[8], float max_value)
{
	uint4 data;
	data.r = ToByte(values[0], max_value) << 24 | ToByte(values[1], max_value) << 16 | ToByte(values[2], max_value) << 8 | ToByte(values[3], max_value);
	data.g = ToByte(values[4], max_value) << 24 | ToByte(values[5], max_value) << 16 | ToByte(values[6], max_value) << 8 | ToByte(values[7], max_value);
	data.b = 0;
	data.a = 0;
	return data;
}

uint4 Pack16Bytes(float values[16], float max_value)
{
	uint4 data;
	data.r = ToByte(values[0], max_value) << 24 | ToByte(values[1], max_value) << 16 | ToByte(values[2], max_value) << 8 | ToByte(values[3], max_value);
	data.g = ToByte(values[4], max_value) << 24 | ToByte(values[5], max_value) << 16 | ToByte(values[6], max_value) << 8 | ToByte(values[7], max_value);
	data.b = ToByte(values[8], max_value) << 24 | ToByte(values[9], max_value) << 16 | ToByte(values[10], max_value) << 8 | ToByte(values[11], max_value);
	data.a = ToByte(values[12], max_value) << 24 | ToByte(values[13], max_value) << 16 | ToByte(values[14], max_value) << 8 | ToByte(values[15], max_value);
	return data;
}

void Unpack8Bytes(uint4 data, float max_value, out float values[8])
{
	uint d0 = data.r;
	uint d1 = data.g;

	values[0] = FromByte((d0 >> 24) & 0xFF, max_value);
	values[1] = FromByte((d0 >> 16) & 0xFF, max_value);
	values[2] = FromByte((d0 >> 8) & 0xFF, max_value);
	values[3] = FromByte(d0 & 0xFF, max_value);
	values[4] = FromByte((d1 >> 24) & 0xFF, max_value);
	values[5] = FromByte((d1 >> 16) & 0xFF, max_value);
	values[6] = FromByte((d1 >> 8) & 0xFF, max_value);
	values[7] = FromByte(d1 & 0xFF, max_value);
}

void Unpack16Bytes(uint4 data, float max_value, out float values[16])
{
	uint d0 = data.r;
	uint d1 = data.g;
	uint d2 = data.b;
	uint d3 = data.a;

	values[0]  = FromByte((d0 >> 24) & 0xFF, max_value);
	values[1]  = FromByte((d0 >> 16) & 0xFF, max_value);
	values[2]  = FromByte((d0 >> 8)  & 0xFF, max_value);
	values[3]  = FromByte( d0        & 0xFF, max_value);
	values[4]  = FromByte((d1 >> 24) & 0xFF, max_value);
	values[5]  = FromByte((d1 >> 16) & 0xFF, max_value);
	values[6]  = FromByte((d1 >> 8)  & 0xFF, max_value);
	values[7]  = FromByte( d1        & 0xFF, max_value);
	values[8]  = FromByte((d2 >> 24) & 0xFF, max_value);
	values[9]  = FromByte((d2 >> 16) & 0xFF, max_value);
	values[10] = FromByte((d2 >> 8)  & 0xFF, max_value);
	values[11] = FromByte( d2        & 0xFF, max_value);
	values[12] = FromByte((d3 >> 24) & 0xFF, max_value);
	values[13] = FromByte((d3 >> 16) & 0xFF, max_value);
	values[14] = FromByte((d3 >> 8)  & 0xFF, max_value);
	values[15] = FromByte( d3        & 0xFF, max_value);
}

float4 Pack4Colors(float4 color1, float4 color2, float4 color3, float4 color4) {
	// Scale colors from 0-1 to 0-256 (8-bit maximum value allow saturation to 10 bits)d
	uint4 scaledColor1 = clamp(uint4((color1 + 0.5f) * 512.0f), 0, 1023);
	uint4 scaledColor2 = clamp(uint4((color2 + 0.5f) * 512.0f), 0, 1023);
	uint4 scaledColor3 = clamp(uint4((color3 + 0.5f) * 512.0f), 0, 1023);
	uint4 scaledColor4 = clamp(uint4((color4 + 0.5f) * 512.0f), 0, 1023);

	uint packed_color1 = scaledColor1.b << 21 | scaledColor1.g << 10 | scaledColor1.r;
	uint packed_color2 = scaledColor2.b << 21 | scaledColor2.g << 10 | scaledColor2.r;
	uint packed_color3 = scaledColor3.b << 21 | scaledColor3.g << 10 | scaledColor3.r;
	uint packed_color4 = scaledColor4.b << 21 | scaledColor4.g << 10 | scaledColor4.r;

	// Store the packed colors in a float4
	float4 packedColors;
	packedColors.x = asfloat(packed_color1);
	packedColors.y = asfloat(packed_color2);
	packedColors.z = asfloat(packed_color3);
	packedColors.w = asfloat(packed_color4);

	return packedColors;
}

void Unpack4Colors(float4 packedColors, out float4 color1, out float4 color2, out float4 color3, out float4 color4) {
	// Extract the packed colors
	uint packedColor1 = asuint(packedColors.x);
	uint packedColor2 = asuint(packedColors.y);
	uint packedColor3 = asuint(packedColors.z);
	uint packedColor4 = asuint(packedColors.w);

	// Extract the RGB components from the packed colors
	uint4 scaledColor1;
	scaledColor1.r = packedColor1 & 0x3FF;
	scaledColor1.g = (packedColor1 >> 10) & 0x7FF;
	scaledColor1.b = (packedColor1 >> 21) & 0x7FF;
	
	uint4 scaledColor2;
	scaledColor2.r = packedColor2 & 0x3FF;
	scaledColor2.g = (packedColor2 >> 10) & 0x7FF;
	scaledColor2.b = (packedColor2 >> 21) & 0x7FF;
	
	uint4 scaledColor3;
	scaledColor3.r = packedColor3 & 0x3FF;
	scaledColor3.g = (packedColor3 >> 10) & 0x7FF;
	scaledColor3.b = (packedColor3 >> 21) & 0x7FF;
	
	uint4 scaledColor4;
	scaledColor4.r = packedColor4 & 0x3FF;
	scaledColor4.g = (packedColor4 >> 10) & 0x7FF;
	scaledColor4.b = (packedColor4 >> 21) & 0x7FF;
	
	// Scale colors back from 0-65535.0 to 0-1
	color1 = float4(scaledColor1) * 0.001953125f;
	color2 = float4(scaledColor2) * 0.001953125f;
	color3 = float4(scaledColor3) * 0.001953125f;
	color4 = float4(scaledColor4) * 0.001953125f;

	color1.rgb -= 0.5f;
	color2.rgb -= 0.5f;
	color3.rgb -= 0.5f;
	color4.rgb -= 0.5f;

	color1.a = 1.0f;
	color2.a = 1.0f;
	color3.a = 1.0f;
	color4.a = 1.0f;
}

float4 PackColors(float4 color1, float4 color2) {
	// Scale colors from 0-1 to 0-1024.0 (10-bit maximum value)
	uint4 scaledColor1 = uint4((color1 + 0.5f) * 1024.0f);
	uint4 scaledColor2 = uint4((color2 + 0.5f) * 1024.0f);

	uint color1_low = scaledColor1.g << 16 | scaledColor1.r;
	uint color1_hi = scaledColor1.a << 16 | scaledColor1.b;

	uint color2_low = scaledColor2.g << 16 | scaledColor2.r;
	uint color2_hi = scaledColor2.a << 16 | scaledColor2.b;

	// Store the packed colors in a float4
	float4 packedColors;
	packedColors.x = asfloat(color1_low);
	packedColors.y = asfloat(color1_hi);
	packedColors.z = asfloat(color2_low);
	packedColors.w = asfloat(color2_hi);

	return packedColors;
}

void UnpackColors(float4 packedColors, out float4 color1, out float4 color2) {
	// Extract the packed colors
	uint packedColor1_low = asuint(packedColors.x);
	uint packedColor1_hi = asuint(packedColors.y);
	uint packedColor2_low = asuint(packedColors.z);
	uint packedColor2_hi = asuint(packedColors.w);

	// Extract the RGB components from the packed colors
	uint4 scaledColor1;
	scaledColor1.r = packedColor1_low & 0xFFFF;
	scaledColor1.g = (packedColor1_low >> 16) & 0xFFFF;	
	scaledColor1.b = packedColor1_hi & 0xFFFF;
	scaledColor1.a = (packedColor1_hi >> 16) & 0xFFFF;

	uint4 scaledColor2;
	scaledColor2.r = packedColor2_low & 0xFFFF;
	scaledColor2.g = (packedColor2_low >> 16) & 0xFFFF;
	scaledColor2.b = packedColor2_hi & 0xFFFF;
	scaledColor2.a = (packedColor2_hi >> 16) & 0xFFFF;

	// Scale colors back from 0-65535.0 to 0-1
	color1 = float4(scaledColor1) / 1024.0f;
	color2 = float4(scaledColor2) / 1024.0f;

	color1 -= 0.5f;
	color2 -= 0.5f;
}

float4 getSmoothPixel(SamplerState basicSampler, Texture2D t, float2 pos, float screenW, float screenH) {

	static const float getSmoothPixelWeights[3] =
	{
		0.1,
		0.3,
		0.1,
	};

	float4 color = float4(0.f, 0.f, 0.f, 1.f);
	float2 tpos;
	float pix_w = 1.0 / screenW;
	float pix_h = 1.0 / screenH;
	for (int dx = -1; dx <= 1; ++dx) {
		for (int dy = -1; dy <= 1; ++dy) {
			tpos.x = pos.x + dx * pix_w;
			tpos.y = pos.y + dy * pix_h;
			tpos = saturate(tpos);
			color += t.Sample(basicSampler, tpos) * (getSmoothPixelWeights[dx + 1] + getSmoothPixelWeights[dy + 1])/2.0f;
		}
	}
	return color;
}

bool is_clockwise(float3 v0, float3 v1, float3 v2) {
	// Determine the triangle order using the right hand rule
	float3 edge1 = v1 - v0;
	float3 edge2 = v2 - v0;
	float3 normal = cross(edge1, edge2);

	if (normal.z > 0)
	{
		// Triangle is counter-clockwise
		return false;
	}
	else
	{
		// Triangle is clockwise
		return true;
	}
}

// Hash function from H. Schechter & R. Bridson, goo.gl/RXiKaH
uint hash(uint s)
{
	s ^= 2747636419u;
	s *= 2654435769u;
	s ^= s >> 16;
	s *= 2654435769u;
	s ^= s >> 16;
	s *= 2654435769u;
	return s;
}

float randRange(float minVal, float maxVal, float seed)
{
	return lerp(minVal, maxVal, frac(sin(dot(float2(seed, seed * 1.2345), float2(12.9898, 78.233))) * 43758.5453));
}

float random(uint seed)
{
	return float(hash(seed)) / 4294967295.0; // 2^32-1
}

float rand2d(float2 uv)
{
	return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
}

float4 ViewToWorld(float3 view_position, matrix view_inverse) {
	float4 H = float4(view_position.x, view_position.y, view_position.z, 1.0f);
	float4 D = mul(H, view_inverse);
	float4 worldPos = D / D.w;
	return worldPos;
}

float distance_point_infinite_line(float2 p0, float2 line_p2, float2 line_p1) {
	float2 line_direction = line_p2 - line_p1;
	float2 point_direction = p0 - line_p1;
	return abs(point_direction.x * line_direction.y - point_direction.y * line_direction.x) / length(line_direction);
}

float ditance_point_line(float2 p0, float2 line_endpoint1, float2 line_endpoint0) {
	float2 line_direction = line_endpoint1 - line_endpoint0;
	float2 point_direction = p0 - line_endpoint0;

	float t = dot(point_direction, line_direction) / dot(line_direction, line_direction);

	float2 distance_floattor;

	if (t < 0.0)
		distance_floattor = point_direction;
	else if (t > 1.0)
		distance_floattor = p0 - line_endpoint1;
	else
		distance_floattor = point_direction - t * line_direction;

	return length(distance_floattor);

}


// Function to calculate intersection between line segment and sphere, returning the closer intersection point to P1
bool line_sphere_intersection(float3 p1, float3 p2, float3 center, float radius, out float3 intersectionPoint) {
	float3 d = p2 - p1;
	float3 f = p1 - center;

	if (length(f) < radius) {
		intersectionPoint = p1;
		return true;
	}

	float a = dot(d, d);
	float b = 2.0f * dot(f, d);
	float c = dot(f, f) - radius * radius;

	float discriminant = b * b - 4 * a * c;
	if (discriminant < 0) {
		// No intersection
		return false;
	}
	else if (abs(discriminant) <= Epsilon) {
		// Tangent case
		float t = -b / (2.0f * a);
		intersectionPoint = p1 + t * d;
		return true;
	}
	else {
		float t1 = (-b + sqrt(discriminant)) / (2.0f * a);
		float t2 = (-b - sqrt(discriminant)) / (2.0f * a);

		float3 intersectionPoint1 = p1 + t1 * d;
		float3 intersectionPoint2 = p1 + t2 * d;

		// Calculate distances from P1 to intersection points
		float dist1 = length(intersectionPoint1 - p1);
		float dist2 = length(intersectionPoint2 - p1);

		// Choose the closer intersection point
		if (dist1 < dist2) {
			intersectionPoint = intersectionPoint1;
		}
		else {
			intersectionPoint = intersectionPoint2;
		}
		return true;
	}
}


#if 0
float3 HUEtoRGB(in float H)
{
	float R = abs(H * 6 - 3) - 1;
	float G = 2 - abs(H * 6 - 2);
	float B = 2 - abs(H * 6 - 4);
	return saturate(float3(R, G, B));
}

float3 RGBtoHCV(in float3 RGB)
{
	// Based on work by Sam Hocevar and Emil Persson
	float4 P = (RGB.g < RGB.b) ? float4(RGB.bg, -1.0, 2.0 / 3.0) : float4(RGB.gb, 0.0, -1.0 / 3.0);
	float4 Q = (RGB.r < P.x) ? float4(P.xyw, RGB.r) : float4(RGB.r, P.yzx);
	float C = Q.x - min(Q.w, Q.y);
	float H = abs((Q.w - Q.y) / (6 * C + Epsilon) + Q.z);
	return float3(H, C, Q.x);
}

float3 HSVtoRGB(in float3 HSV)
{
	float3 RGB = HUEtoRGB(HSV.x);
	return ((RGB - 1) * HSV.y + 1) * HSV.z;
}

float3 HSLtoRGB(in float3 HSL)
{
	float3 RGB = HUEtoRGB(HSL.x);
	float C = (1 - abs(2 * HSL.z - 1)) * HSL.y;
	return (RGB - 0.5) * C + HSL.z;
}

// The weights of RGB contributions to luminance.
 // Should sum to unity.
float3 HCYwts = float3(0.299, 0.587, 0.114);

float3 HCYtoRGB(in float3 HCY)
{
	float3 RGB = HUEtoRGB(HCY.x);
	float Z = dot(RGB, HCYwts);
	if (HCY.z < Z)
	{
		HCY.y *= HCY.z / Z;
	}
	else if (Z < 1)
	{
		HCY.y *= (1 - HCY.z) / (1 - Z);
	}
	return (RGB - Z) * HCY.y + HCY.z;
}

static const float HCLgamma = 3;
static const float HCLy0 = 100;
static const float HCLmaxL = 0.530454533953517; // == exp(HCLgamma / HCLy0) - 0.5
static const float _PI = 3.1415926536;

float3 HCLtoRGB(in float3 HCL)
{
	float3 RGB = 0;
	if (HCL.z != 0)
	{
		float H = HCL.x;
		float C = HCL.y;
		float L = HCL.z * HCLmaxL;
		float Q = exp((1 - C / (2 * L)) * (HCLgamma / HCLy0));
		float U = (2 * L - C) / (2 * Q - 1);
		float V = C / Q;
		float A = (H + min(frac(2 * H) / 4, frac(-2 * H) / 8)) * _PI * 2;
		float T;
		H *= 6;
		if (H <= 0.999)
		{
			T = tan(A);
			RGB.r = 1;
			RGB.g = T / (1 + T);
		}
		else if (H <= 1.001)
		{
			RGB.r = 1;
			RGB.g = 1;
		}
		else if (H <= 2)
		{
			T = tan(A);
			RGB.r = (1 + T) / T;
			RGB.g = 1;
		}
		else if (H <= 3)
		{
			T = tan(A);
			RGB.g = 1;
			RGB.b = 1 + T;
		}
		else if (H <= 3.999)
		{
			T = tan(A);
			RGB.g = 1 / (1 + T);
			RGB.b = 1;
		}
		else if (H <= 4.001)
		{
			RGB.g = 0;
			RGB.b = 1;
		}
		else if (H <= 5)
		{
			T = tan(A);
			RGB.r = -1 / T;
			RGB.b = 1;
		}
		else
		{
			T = tan(A);
			RGB.r = 1;
			RGB.b = -T;
		}
		RGB = RGB * V + U;
	}
	return RGB;
}

float3 RGBtoHSV(in float3 RGB)
{
	float3 HCV = RGBtoHCV(RGB);
	float S = HCV.y / (HCV.z + Epsilon);
	return float3(HCV.x, S, HCV.z);
}

float3 RGBtoHSL(in float3 RGB)
{
	float3 HCV = RGBtoHCV(RGB);
	float L = HCV.z - HCV.y * 0.5;
	float S = HCV.y / (1 - abs(L * 2 - 1) + Epsilon);
	return float3(HCV.x, S, L);
}

float3 RGBtoHCY(in float3 RGB)
{
	// Corrected by David Schaeffer
	float3 HCV = RGBtoHCV(RGB);
	float Y = dot(RGB, HCYwts);
	float Z = dot(HUEtoRGB(HCV.x), HCYwts);
	if (Y < Z)
	{
		HCV.y *= Z / (Epsilon + Y);
	}
	else
	{
		HCV.y *= (1 - Z) / (Epsilon + 1 - Y);
	}
	return float3(HCV.x, HCV.y, Y);
}

float3 RGBtoHCL(in float3 RGB)
{
	float3 HCL;
	float H = 0;
	float U = min(RGB.r, min(RGB.g, RGB.b));
	float V = max(RGB.r, max(RGB.g, RGB.b));
	float Q = HCLgamma / HCLy0;
	HCL.y = V - U;
	if (HCL.y != 0)
	{
		H = atan2(RGB.g - RGB.b, RGB.r - RGB.g) / _PI;
		Q *= U / V;
	}
	Q = exp(Q);
	HCL.x = frac(H / 2 - min(frac(H), frac(-H)) / 6);
	HCL.y *= Q;
	HCL.z = lerp(-U, V, Q) / (HCLmaxL * 2);
	return HCL;
}
#endif
#endif