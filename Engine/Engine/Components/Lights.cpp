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
