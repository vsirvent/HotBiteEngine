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

#include "Lights.h"
#include <algorithm>
#include <DirectXMath.h>

using namespace DirectX;

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Core;
using namespace HotBite::Engine::Components;

AmbientLight::AmbientLight() :Light(LightType::Ambient) {
}

AmbientLight::AmbientLight(const float3& color_down, const float3& color_up) :
	Light(LightType::Ambient) {
	data.colorDown = color_down;
	data.colorUp = color_up;
}

struct AmbientLight::Data& AmbientLight::GetData() {
	return data;
}

DirectionalLight::DirectionalLight() :Light(LightType::Directional) {
}

HRESULT DirectionalLight::Init(const float3& c, const float3& dir,
	bool cast_shadow, int shadow_resolution_divisor,
	float volume_density) {
	HRESULT hr = S_OK;
	XMVECTOR v = { dir.x, dir.y, dir.z };
	init = true;
	this->shadow_resolution_divisor = shadow_resolution_divisor;
	XMStoreFloat3(&this->data.direction, XMVector3Normalize(v));
	this->data.density = volume_density / 1000.0f;
	this->data.color = c;
	this->data.cast_shadow = cast_shadow;
	
	if (cast_shadow) {
		int w = (int)((float)texture_resolution_ratio * DXCore::Get()->GetWidth() * shadow_resolution_divisor);
		do {
			texture.Init(w, w);
			hr = static_texture.Init(w, w);
			if (FAILED(hr)) {
				texture_resolution_ratio /= 2;
				w = (int)((float)texture_resolution_ratio * DXCore::Get()->GetWidth() * shadow_resolution_divisor);
			}
		} while (FAILED(hr) && texture_resolution_ratio > 0);
		if (FAILED(hr)) {
			printf("DirectionalLight::Init: Warning, can't create shadow texture, cast_shadow disabled\n");
			cast_shadow = false;
		}
		else {
			shadow_vp.TopLeftX = 0;
			shadow_vp.TopLeftY = 0;
			shadow_vp.Width = (float)w;
			shadow_vp.Height = (float)w;
			shadow_vp.MinDepth = 0.0f;
			shadow_vp.MaxDepth = 1.0f;
		}
	}
	return hr;
}

ID3D11ShaderResourceView* DirectionalLight::StaticDepthResource() {
	return static_texture.SRV();
}

ID3D11DepthStencilView* DirectionalLight::StaticDepthView() {
	return static_texture.Depth();
}

ID3D11ShaderResourceView* DirectionalLight::DepthResource() {
	return texture.SRV();
}

ID3D11DepthStencilView* DirectionalLight::DepthView() {
	return texture.Depth();
};

bool DirectionalLight::CastShadow() const {
	return data.cast_shadow;
}

DirectionalLight::Data& DirectionalLight::GetData() {
	return data;
}

const D3D11_VIEWPORT& DirectionalLight::GetShadowViewPort() const {
	return shadow_vp;
}

HRESULT DirectionalLight::Release() {
	texture.Release();
	static_texture.Release();
	init = false;
	return S_OK;
}

void DirectionalLight::RefreshStaticViewMatrix() {
	static_viewMatrix = viewMatrix;
	//The static map is about to be rendered under this matrix, so from here on it is
	//safe for shaders to sample it.
	data.flags |= DIR_LIGHT_FLAG_STATIC_SHADOW;
}

const float4x4* DirectionalLight::GetViewMatrix() const {
	return &viewMatrix;
}

const float4x4* DirectionalLight::GetStaticViewMatrix() const {
	return &static_viewMatrix;
}

const float4x4& DirectionalLight::GetLightPerspectiveValues() const {
	return lightPerspectiveValues;
};

const float4x4& DirectionalLight::GetProjectionMatrix() const {
	return projectionMatrix;
}

const float4x4& DirectionalLight::GetSpotMatrix() const {
	return spotMatrix;
}

void DirectionalLight::SetSpotMatrix(const matrix& m) {
	XMStoreFloat4x4(&spotMatrix, m);
}

PointLight::PointLight() :Light(LightType::Point) {
}

HRESULT PointLight::Init(const float3& color,
	float range, bool cast_shadow,
	int shadow_resolution_divisor, float volume_density) {
	HRESULT hr = S_OK;
	this->data.position = { 0.0f, 0.0f, 0.0f };
	this->data.range = range;
	this->data.density = volume_density / 1000.0f;
	this->data.color = color;
	this->data.cast_shadow = cast_shadow;
	this->dirty = true;
	this->init = true;
	this->shadow_resolution_divisor = shadow_resolution_divisor;
	if (cast_shadow) {
		int w = DXCore::Get()->GetWidth() / shadow_resolution_divisor;

		if (FAILED(texture.Init(w, w))) {
			return hr;
		}
		shadow_vp.TopLeftX = 0;
		shadow_vp.TopLeftY = 0;
		shadow_vp.Width = (float)w;
		shadow_vp.Height = (float)w;
		shadow_vp.MinDepth = 0.0f;
		shadow_vp.MaxDepth = 1.0f;
	}
	return hr;
}

ID3D11ShaderResourceView* PointLight::DepthResource() {
	return texture.SRV();
}

ID3D11DepthStencilView* PointLight::DepthView() {
	return texture.Depth();
};

bool PointLight::CastShadow() const {
	return this->data.cast_shadow;
}

const D3D11_VIEWPORT& PointLight::GetShadowViewPort() const {
	return shadow_vp;
}

HRESULT PointLight::Release() {
	texture.Release();
	init = false;
	return S_OK;
}

PointLight::Data& PointLight::GetData() {
	return data;
}

const float4x4* PointLight::GetViewMatrix() const {
	return &viewMatrix[0];
}

const float4x4& PointLight::GetLightPerspectiveValues() const {
	return lightPerspectiveValues;
};

const float4x4& PointLight::GetProjectionMatrix() const {
	return projectionMatrix;
}

// -----------------------------------------------------------------------------
// Serialization. See ECS/Serialization.h for the contract.
//
// The shadow map is a GPU resource sized at Init time, so a light that has not
// been initialized yet is Init'd from the incoming JSON rather than having its
// Data poked directly - otherwise a light added in the editor would have colour
// and range but no depth texture, and would silently never cast a shadow. An
// already-initialized light takes the cheap path and just updates Data.
//
// `density` round-trips as the value a level author writes: Init divides it by
// 1000 on the way in, so ToJson multiplies it back out.
// -----------------------------------------------------------------------------

using nlohmann::json;
using namespace HotBite::Engine::ECS::JsonUtil;

json AmbientLight::ToJson(const ECS::SerializeContext& ctx) const {
	json j;
	j["color_down"] = FromFloat3(data.colorDown);
	j["color_up"] = FromFloat3(data.colorUp);
	return j;
}

void AmbientLight::FromJson(const json& j, const ECS::SerializeContext& ctx) {
	ToFloat3(j, "color_down", data.colorDown);
	ToFloat3(j, "color_up", data.colorUp);
}

json DirectionalLight::ToJson(const ECS::SerializeContext& ctx) const {
	json j;
	j["color"] = FromFloat3(data.color);
	j["direction"] = FromFloat3(data.direction);
	j["position"] = FromFloat3(data.position);
	j["intensity"] = data.intensity;
	j["range"] = data.range;
	j["density"] = data.density * 1000.0f;
	j["cast_shadow"] = (data.cast_shadow != 0);
	j["resolution"] = shadow_resolution_divisor;
	j["fog"] = (data.flags & DIR_LIGHT_FLAG_FOG) != 0;
	j["inverse_shadow"] = (data.flags & DIR_LIGHT_FLAG_INVERSE) != 0;
	return j;
}

void DirectionalLight::FromJson(const json& j, const ECS::SerializeContext& ctx) {
	if (!init) {
		float3 color = data.color;
		float3 direction = data.direction;
		ToFloat3(j, "color", color);
		ToFloat3(j, "direction", direction);
		Init(color, direction,
			j.value("cast_shadow", data.cast_shadow != 0),
			j.value("resolution", shadow_resolution_divisor),
			j.value("density", data.density * 1000.0f));
	}
	else {
		ToFloat3(j, "color", data.color);
		if (j.contains("direction")) {
			float3 dir = data.direction;
			ToFloat3(j, "direction", dir);
			XMStoreFloat3(&data.direction, XMVector3Normalize(XMVECTOR{ dir.x, dir.y, dir.z }));
		}
		data.cast_shadow = j.value("cast_shadow", data.cast_shadow != 0) ? 1u : 0u;
		if (j.contains("density")) {
			data.density = j["density"].get<float>() / 1000.0f;
		}
	}
	ToFloat3(j, "position", data.position);
	data.intensity = j.value("intensity", data.intensity);
	data.range = j.value("range", data.range);
	SetFog(j.value("fog", (data.flags & DIR_LIGHT_FLAG_FOG) != 0));
	SetInverse(j.value("inverse_shadow", (data.flags & DIR_LIGHT_FLAG_INVERSE) != 0));
	SetDirty();
}

json PointLight::ToJson(const ECS::SerializeContext& ctx) const {
	json j;
	j["color"] = FromFloat3(data.color);
	j["position"] = FromFloat3(data.position);
	j["range"] = data.range;
	j["density"] = data.density * 1000.0f;
	j["tilt_ratio"] = data.tilt_ratio;
	j["cast_shadow"] = (data.cast_shadow != 0);
	j["resolution"] = shadow_resolution_divisor;
	return j;
}

//A point light's range doubles as the far plane of its shadow projection, so it
//has to clear the 0.1 near plane PointLightSystem::Update builds with - at or below
//it XMMatrixPerspectiveFovLH asserts (far <= near) and takes the process down.
//A level is free to author a small range, but not a degenerate one.
static constexpr float MIN_POINT_LIGHT_RANGE = 0.2f;
//What an added-from-nothing light should look like: a white lamp you can actually
//see. Defaults of zero would give a black light of zero radius - technically valid,
//useless to author with, and (for the range) the degenerate case above.
static constexpr float DEFAULT_POINT_LIGHT_RANGE = 10.0f;

void PointLight::FromJson(const json& j, const ECS::SerializeContext& ctx) {
	if (!init) {
		//Fresh component: fall back to visible defaults for anything the JSON does
		//not specify, rather than to the zero-initialized Data.
		float3 color = data.color;
		if (color.x == 0.0f && color.y == 0.0f && color.z == 0.0f) {
			color = { 1.0f, 1.0f, 1.0f };
		}
		ToFloat3(j, "color", color);
		float range = j.value("range",
			(data.range > 0.0f) ? data.range : DEFAULT_POINT_LIGHT_RANGE);
		//Parenthesized to stop the Windows max() macro from eating the call - this
		//translation unit reaches windows.h and NOMINMAX is not set project-wide.
		Init(color,
			(std::max)(range, MIN_POINT_LIGHT_RANGE),
			j.value("cast_shadow", data.cast_shadow != 0),
			j.value("resolution", shadow_resolution_divisor),
			j.value("density", data.density * 1000.0f));
	}
	else {
		ToFloat3(j, "color", data.color);
		data.range = (std::max)(j.value("range", data.range), MIN_POINT_LIGHT_RANGE);
		data.cast_shadow = j.value("cast_shadow", data.cast_shadow != 0) ? 1 : 0;
		if (j.contains("density")) {
			data.density = j["density"].get<float>() / 1000.0f;
		}
	}
	ToFloat3(j, "position", data.position);
	data.tilt_ratio = j.value("tilt_ratio", data.tilt_ratio);
	dirty = true;
}

