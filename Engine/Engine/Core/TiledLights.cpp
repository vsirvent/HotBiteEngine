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

#include "TiledLights.h"
#include "DXCore.h"
#include <DirectXPackedVector.h>
#include <cmath>
#include <cstring>

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;

namespace {
	//Octahedral map of a unit vector to [-1, 1]^2 (Cigolle et al.): one projection onto
	//the octahedron |x|+|y|+|z| = 1, with the lower half folded outwards.
	void OctEncode(const float d[3], float& u, float& v) {
		const float l1 = std::fabs(d[0]) + std::fabs(d[1]) + std::fabs(d[2]);
		const float inv = l1 > 0.0f ? 1.0f / l1 : 0.0f;
		float x = d[0] * inv;
		float y = d[1] * inv;
		const float z = d[2] * inv;
		if (z < 0.0f) {
			const float ox = (1.0f - std::fabs(y)) * (x >= 0.0f ? 1.0f : -1.0f);
			const float oy = (1.0f - std::fabs(x)) * (y >= 0.0f ? 1.0f : -1.0f);
			x = ox;
			y = oy;
		}
		u = x;
		v = y;
	}

	uint32_t PackSnorm16(float f) {
		const float c = f < -1.0f ? -1.0f : (f > 1.0f ? 1.0f : f);
		const int32_t i = (int32_t)std::lround(c * 32767.0f);
		return (uint32_t)(i & 0xFFFF);
	}

	float UnpackSnorm16(uint32_t v) {
		return (float)(int16_t)(v & 0xFFFFu) / 32767.0f;
	}
}

PackedLight Core::PackLight(const float position[3], float range, const float color[3], bool is_spot,
	const float direction[3], float cos_inner, float cos_outer, int shadow_slot, bool ray_traced)
{
	PackedLight p = {};
	p.position[0] = position[0];
	p.position[1] = position[1];
	p.position[2] = position[2];
	p.range = range;

	//Negative colour has no meaning for a light and the shared exponent format cannot
	//hold it; DirectXMath clamps it to zero.
	DirectX::PackedVector::XMFLOAT3SE packed_color;
	DirectX::PackedVector::XMStoreFloat3SE(&packed_color,
		DirectX::XMVectorSet(color[0], color[1], color[2], 0.0f));
	p.color = packed_color.v;

	if (is_spot) {
		float u = 0.0f;
		float v = 0.0f;
		OctEncode(direction, u, v);
		p.direction = PackSnorm16(u) | (PackSnorm16(v) << 16);
		p.cones = (uint32_t)DirectX::PackedVector::XMConvertFloatToHalf(cos_outer) |
			((uint32_t)DirectX::PackedVector::XMConvertFloatToHalf(cos_inner) << 16);
		p.flags |= PACKED_LIGHT_SPOT;
	}
	if (shadow_slot >= 0) {
		p.flags |= PACKED_LIGHT_SHADOW | ((uint32_t)shadow_slot << PACKED_LIGHT_SLOT_SHIFT);
	}
	else if (ray_traced) {
		p.flags |= PACKED_LIGHT_RAYTRACED;
	}
	return p;
}

void Core::UnpackLightColor(const PackedLight& l, float out[3]) {
	const uint32_t v = l.color;
	const float scale = std::exp2((float)(v >> 27) - 24.0f);
	out[0] = (float)(v & 0x1FFu) * scale;
	out[1] = (float)((v >> 9) & 0x1FFu) * scale;
	out[2] = (float)((v >> 18) & 0x1FFu) * scale;
}

void Core::UnpackLightDirection(const PackedLight& l, float out[3]) {
	const float ex = UnpackSnorm16(l.direction);
	const float ey = UnpackSnorm16(l.direction >> 16);
	float x = ex;
	float y = ey;
	float z = 1.0f - std::fabs(ex) - std::fabs(ey);
	const float t = -z < 0.0f ? 0.0f : (-z > 1.0f ? 1.0f : -z);
	x += x >= 0.0f ? -t : t;
	y += y >= 0.0f ? -t : t;
	const float len = std::sqrt(x * x + y * y + z * z);
	const float inv = len > 0.0f ? 1.0f / len : 0.0f;
	out[0] = x * inv;
	out[1] = y * inv;
	out[2] = z * inv;
}

bool TiledLights::Init(int w, int h) {
	Release();
	ID3D11Device* device = DXCore::Get()->device;
	width = (uint32_t)w;
	height = (uint32_t)h;
	tiles_x = (width + TILE_SIZE - 1) / TILE_SIZE;
	tiles_y = (height + TILE_SIZE - 1) / TILE_SIZE;

	D3D11_BUFFER_DESC bd = {};
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = CAPACITY * sizeof(PackedLight);
	bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bd.StructureByteStride = sizeof(PackedLight);
	if (FAILED(device->CreateBuffer(&bd, nullptr, &lights_buffer))) {
		Release();
		return false;
	}
	D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
	sd.Format = DXGI_FORMAT_UNKNOWN;
	sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	sd.Buffer.FirstElement = 0;
	sd.Buffer.NumElements = CAPACITY;
	if (FAILED(device->CreateShaderResourceView(lights_buffer, &sd, &lights_srv))) {
		Release();
		return false;
	}
	if (FAILED(masks.Init(tiles_x * tiles_y * MASK_WORDS * 4u)) || FAILED(stats_buffer.Init(32, true))) {
		Release();
		return false;
	}
	masks.Clear(0);

	cull_shader = ShaderFactory::Get()->GetShader<SimpleComputeShader>("TileLightCullCS.cso");
	validate_shader = ShaderFactory::Get()->GetShader<SimpleComputeShader>("TileLightValidateCS.cso");
	if (cull_shader == nullptr || validate_shader == nullptr) {
		Release();
		return false;
	}
	return true;
}

void TiledLights::Release() {
	if (lights_srv != nullptr) { lights_srv->Release(); lights_srv = nullptr; }
	if (lights_buffer != nullptr) { lights_buffer->Release(); lights_buffer = nullptr; }
	masks.Release();
	stats_buffer.Release();
	cull_shader = nullptr;
	validate_shader = nullptr;
	light_count = 0;
	masks_cleared = false;
}

void TiledLights::Upload(const PackedLight* lights, uint32_t count) {
	light_count = count < CAPACITY ? count : CAPACITY;
	if (light_count == 0 || lights_buffer == nullptr) {
		return;
	}
	D3D11_BOX box = {};
	box.left = 0;
	box.right = light_count * (uint32_t)sizeof(PackedLight);
	box.top = 0;
	box.bottom = 1;
	box.front = 0;
	box.back = 1;
	DXCore::Get()->context->UpdateSubresource(lights_buffer, 0, &box, lights, 0, 0);
}

void TiledLights::Cull(const float4x4& view, const float4x4& projection_inverse,
	ID3D11ShaderResourceView* positions, bool collect_stats, bool culling)
{
	if (cull_shader == nullptr || positions == nullptr) {
		return;
	}
	ID3D11DeviceContext* context = DXCore::Get()->context;
	if (light_count == 0) {
		//No lights: every mask is empty. Writing that once is all there is to do, and
		//nothing is dispatched on the frames that follow - a scene with no point lights
		//pays nothing for the feature.
		if (!masks_cleared) {
			masks.Clear(0);
			masks_cleared = true;
		}
		if (collect_stats) {
			stats_buffer.Clear(0);
		}
		return;
	}
	masks_cleared = false;
	if (collect_stats) {
		stats_buffer.Clear(0);
	}
	cull_shader->SetMatrix4x4("view", view);
	cull_shader->SetMatrix4x4("projection_inverse", projection_inverse);
	cull_shader->SetInt("screenW", (int)width);
	cull_shader->SetInt("screenH", (int)height);
	cull_shader->SetInt("tiles_x", (int)tiles_x);
	cull_shader->SetInt("tiles_y", (int)tiles_y);
	cull_shader->SetInt("light_count", (int)light_count);
	cull_shader->SetInt("collect_stats", collect_stats ? 1 : 0);
	cull_shader->SetInt("disable_culling", culling ? 0 : 1);
	cull_shader->SetShaderResourceView("positionTexture", positions);
	cull_shader->SetShaderResourceView("lightsBuffer", lights_srv);
	cull_shader->SetUnorderedAccessView("tileMasks", masks.UAV());
	cull_shader->SetUnorderedAccessView("tileStats", stats_buffer.UAV());
	cull_shader->CopyAllBufferData();
	cull_shader->SetShader();
	context->Dispatch(tiles_x, tiles_y, 1);
	cull_shader->SetShaderResourceView("positionTexture", nullptr);
	cull_shader->SetShaderResourceView("lightsBuffer", nullptr);
	cull_shader->SetUnorderedAccessView("tileMasks", nullptr);
	cull_shader->SetUnorderedAccessView("tileStats", nullptr);
}

void TiledLights::ReadStats(TiledLightStats& stats) {
	stats.tiles_x = tiles_x;
	stats.tiles_y = tiles_y;
	uint32_t raw[8] = {};
	if (stats_buffer.Readback(raw, sizeof(raw))) {
		stats.tiles_lit = raw[0];
		stats.max_per_tile = raw[1];
		stats.total_bits = raw[2];
		stats.tiles_with_geometry = raw[3];
		stats.valid = true;
	}
}

TiledLightValidation TiledLights::Validate(ID3D11ShaderResourceView* positions) {
	TiledLightValidation result;
	if (validate_shader == nullptr || positions == nullptr || light_count == 0) {
		result.valid = light_count == 0;
		return result;
	}
	DXCore* dx = DXCore::Get();
	ID3D11DeviceContext* context = dx->context;
	RWByteBuffer counters;
	if (FAILED(counters.Init(32))) {
		return result;
	}
	counters.Clear(0);
	validate_shader->SetInt("screenW", (int)width);
	validate_shader->SetInt("screenH", (int)height);
	validate_shader->SetInt("tiles_x", (int)tiles_x);
	validate_shader->SetInt("light_count", (int)light_count);
	validate_shader->SetShaderResourceView("positionTexture", positions);
	validate_shader->SetShaderResourceView("lightsBuffer", lights_srv);
	validate_shader->SetShaderResourceView("tileMasks", masks.SRV());
	validate_shader->SetUnorderedAccessView("validateStats", counters.UAV());
	validate_shader->CopyAllBufferData();
	validate_shader->SetShader();
	context->Dispatch(tiles_x, tiles_y, 1);
	validate_shader->SetShaderResourceView("positionTexture", nullptr);
	validate_shader->SetShaderResourceView("lightsBuffer", nullptr);
	validate_shader->SetShaderResourceView("tileMasks", nullptr);
	validate_shader->SetUnorderedAccessView("validateStats", nullptr);

	D3D11_BUFFER_DESC sbd = {};
	sbd.Usage = D3D11_USAGE_STAGING;
	sbd.ByteWidth = 32;
	sbd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ID3D11Buffer* staging = nullptr;
	if (SUCCEEDED(dx->device->CreateBuffer(&sbd, nullptr, &staging))) {
		ID3D11Buffer* source = nullptr;
		counters.UAV()->GetResource(reinterpret_cast<ID3D11Resource**>(&source));
		context->CopyResource(staging, source);
		source->Release();
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		//Blocks until the dispatch above has finished: a test asked for the answer now.
		if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
			const uint32_t* raw = (const uint32_t*)mapped.pData;
			result.reached = raw[4];
			result.missed = raw[5];
			result.pixels = raw[6];
			result.valid = true;
			context->Unmap(staging, 0);
		}
		staging->Release();
	}
	return result;
}
