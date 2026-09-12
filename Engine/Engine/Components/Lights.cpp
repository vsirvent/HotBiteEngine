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

//Resolution of one cascade slice before the authored multiplier. The old single map
//was sized at (8 * screen width) texels and covered (its width / 50) world units, so
//coverage was a side effect of how wide the monitor was and a 1080p screen asked for
//a 15360-square map - most of a gigabyte, twice over, before it fell back by halving.
//Cascades make that untenable (every slice pays it again) and unnecessary: coverage
//is now fitted to the view, so resolution is free to be a plain authored number.
static constexpr int BASE_CASCADE_RESOLUTION = 2048;
//The `resolution` key has always been a multiplier for directional lights (it divides
//for point lights), so it keeps that meaning here. Clamped because it now multiplies a
//figure that is already sensible, and because a slice is allocated per cascade.
static constexpr int MIN_CASCADE_RESOLUTION = 256;
static constexpr int MAX_CASCADE_RESOLUTION = 8192;

static int CascadeResolutionFor(int divisor) {
	int w = BASE_CASCADE_RESOLUTION * ((divisor > 0) ? divisor : 1);
	return (std::min)((std::max)(w, MIN_CASCADE_RESOLUTION), MAX_CASCADE_RESOLUTION);
}

//Allocates the cascade array (`count` slices) and the single static map, both `w`
//square, halving the resolution and retrying if the device will not give them up.
//Leaves the light with whatever it had on failure.
HRESULT DirectionalLight::AllocateShadowMaps(int count, int w) {
	HRESULT hr = E_FAIL;
	while (w >= MIN_CASCADE_RESOLUTION) {
		hr = texture.Init(w, w, count);
		if (SUCCEEDED(hr)) {
			hr = static_texture.Init(w, w);
		}
		if (SUCCEEDED(hr)) {
			shadow_vp.TopLeftX = 0;
			shadow_vp.TopLeftY = 0;
			shadow_vp.Width = (float)w;
			shadow_vp.Height = (float)w;
			shadow_vp.MinDepth = 0.0f;
			shadow_vp.MaxDepth = 1.0f;
			return hr;
		}
		texture.Release();
		static_texture.Release();
		w /= 2;
	}
	return hr;
}

HRESULT DirectionalLight::Init(const float3& c, const float3& dir,
	bool cast_shadow, int shadow_resolution_divisor,
	float volume_density, const CascadeSettings& cascades) {
	HRESULT hr = S_OK;
	XMVECTOR v = { dir.x, dir.y, dir.z };
	init = true;
	this->shadow_resolution_divisor = shadow_resolution_divisor;
	this->cascade_settings = cascades;
	this->cascade_settings.count =
		(std::min)((std::max)(cascades.count, 1), MAX_SHADOW_CASCADES);
	XMStoreFloat3(&this->data.direction, XMVector3Normalize(v));
	this->data.density = volume_density / 1000.0f;
	this->data.color = c;
	this->data.cast_shadow = cast_shadow;
	//Nothing has been fitted yet. Until the system runs, no cascade is safe to sample.
	this->data.cascade_count = 0;

	if (cast_shadow) {
		hr = AllocateShadowMaps(this->cascade_settings.count,
			CascadeResolutionFor(shadow_resolution_divisor));
		if (FAILED(hr)) {
			printf("DirectionalLight::Init: Warning, can't create shadow texture, cast_shadow disabled\n");
			this->data.cast_shadow = 0;
		}
	}
	dirty = true;
	return hr;
}

bool DirectionalLight::SetCascadeCount(int count) {
	count = (std::min)((std::max)(count, 1), MAX_SHADOW_CASCADES);
	if (count == cascade_settings.count) {
		return true;
	}
	if (!init || !data.cast_shadow) {
		//No maps to rebuild: Init will pick the new count up when it allocates.
		cascade_settings.count = count;
		return true;
	}
	const int previous = cascade_settings.count;
	if (FAILED(AllocateShadowMaps(count, CascadeResolutionFor(shadow_resolution_divisor)))) {
		//Put back what was there. If even that fails the light has no shadow maps left,
		//so it stops casting rather than sampling released views.
		if (FAILED(AllocateShadowMaps(previous, CascadeResolutionFor(shadow_resolution_divisor)))) {
			data.cast_shadow = 0;
		}
		return false;
	}
	cascade_settings.count = count;
	//The slices are empty and the stale static matrices no longer describe them.
	data.cascade_count = 0;
	data.flags &= ~DIR_LIGHT_FLAG_STATIC_SHADOW;
	dirty = true;
	return true;
}

bool DirectionalLight::SetShadowResolution(int divisor) {
	divisor = (std::max)(divisor, 1);
	if (divisor == shadow_resolution_divisor) {
		return true;
	}
	if (!init || !data.cast_shadow) {
		//No maps to rebuild: Init will pick the new multiplier up when it allocates.
		shadow_resolution_divisor = divisor;
		return true;
	}
	const int previous = shadow_resolution_divisor;
	if (FAILED(AllocateShadowMaps(cascade_settings.count, CascadeResolutionFor(divisor)))) {
		//Put back what was there; if even that fails the light has no shadow maps left,
		//so it stops casting rather than sampling released views.
		if (FAILED(AllocateShadowMaps(cascade_settings.count, CascadeResolutionFor(previous)))) {
			data.cast_shadow = 0;
		}
		return false;
	}
	shadow_resolution_divisor = divisor;
	//The maps are new and empty, and the static one's matrix no longer describes it.
	data.cascade_count = 0;
	data.flags &= ~DIR_LIGHT_FLAG_STATIC_SHADOW;
	dirty = true;
	return true;
}

void DirectionalLight::SetCascadeDistance(float d) {
	cascade_settings.distance = (std::max)(d, 1.0f);
	dirty = true;
}

void DirectionalLight::SetCascadeSplitLambda(float l) {
	cascade_settings.split_lambda = (std::min)((std::max)(l, 0.0f), 1.0f);
	dirty = true;
}

void DirectionalLight::SetCasterExtrusion(float e) {
	cascade_settings.caster_extrusion = (std::max)(e, 0.0f);
	dirty = true;
}

const DirectionalLight::CascadeInfo& DirectionalLight::GetCascadeInfo(int i) const {
	static const CascadeInfo none{};
	if (i < 0 || i >= MAX_SHADOW_CASCADES) {
		return none;
	}
	return cascade_info[i];
}

int DirectionalLight::GetCascadeResolution() const {
	return texture.Width();
}

int DirectionalLight::GetStaticResolution() const {
	return static_texture.Width();
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

bool DirectionalLight::SetCastShadow(bool enable) {
	if (enable == (data.cast_shadow != 0)) {
		return true;
	}
	if (!enable) {
		//Maps are left allocated - toggling this back on is common enough (the
		//Inspector checkbox) that releasing them here would just mean reallocating
		//on the next enable, and CastShadow() already keeps every reader off them.
		data.cast_shadow = 0;
		data.cascade_count = 0;
		dirty = true;
		return true;
	}
	if (!init) {
		//Init hasn't run yet: it will allocate the maps itself when it does.
		data.cast_shadow = 1;
		return true;
	}
	if (texture.Width() == 0) {
		//Never allocated - this light was constructed or loaded with shadows off.
		if (FAILED(AllocateShadowMaps(cascade_settings.count,
			CascadeResolutionFor(shadow_resolution_divisor)))) {
			return false;
		}
	}
	data.cast_shadow = 1;
	data.cascade_count = 0;
	dirty = true;
	return true;
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
	//The widest cascade, so the one map covers everything the cascade set does. Anything
	//narrower would leave static casters unshadowed in the outer cascades, which is the
	//half of the scene the static map is carrying almost all of.
	const int last = (std::max)(data.cascade_count - 1, 0);
	static_viewMatrix = viewMatrix[last];
	static_cascade_info = cascade_info[last];
	//The static map is about to be rendered under this matrix, so from here on it is
	//safe for shaders to sample it.
	data.flags |= DIR_LIGHT_FLAG_STATIC_SHADOW;
}

bool DirectionalLight::StaticShadowStale() const {
	if (data.cascade_count <= 0) {
		return false;
	}
	if (!(data.flags & DIR_LIGHT_FLAG_STATIC_SHADOW) || static_cascade_info.radius <= 0.0f) {
		//Never rendered: it is as stale as it gets.
		return true;
	}
	const CascadeInfo& live = cascade_info[data.cascade_count - 1];
	//A refit is also due if the cascade itself resized (slice count or shadow distance
	//changed), since the map would then cover the wrong extent entirely.
	if (fabsf(live.radius - static_cascade_info.radius) > static_cascade_info.radius * 0.01f) {
		return true;
	}
	//Otherwise: has the widest cascade slid far enough off the rendered footprint to
	//start exposing its edge? A quarter of the radius leaves plenty of margin while
	//still being loose enough that ordinary movement does not re-render every frame -
	//which would cost exactly what the static map exists to avoid.
	const float3& a = live.center;
	const float3& b = static_cascade_info.center;
	const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
	const float moved2 = dx * dx + dy * dy + dz * dz;
	const float limit = static_cascade_info.radius * 0.25f;
	return moved2 > limit * limit;
}

const float4x4* DirectionalLight::GetViewMatrix() const {
	return viewMatrix;
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

void DirectionalLight::CascadeSettings::FromJson(const json& j) {
	count = j.value("cascades", count);
	distance = j.value("shadow_distance", distance);
	split_lambda = j.value("cascade_split_lambda", split_lambda);
	caster_extrusion = j.value("caster_extrusion", caster_extrusion);
}

void DirectionalLight::CascadeSettings::ToJson(json& j) const {
	j["cascades"] = count;
	j["shadow_distance"] = distance;
	j["cascade_split_lambda"] = split_lambda;
	j["caster_extrusion"] = caster_extrusion;
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
	cascade_settings.ToJson(j);
	return j;
}

void DirectionalLight::FromJson(const json& j, const ECS::SerializeContext& ctx) {
	if (!init) {
		float3 color = data.color;
		float3 direction = data.direction;
		ToFloat3(j, "color", color);
		ToFloat3(j, "direction", direction);
		CascadeSettings cascades = cascade_settings;
		cascades.FromJson(j);
		Init(color, direction,
			j.value("cast_shadow", data.cast_shadow != 0),
			j.value("resolution", shadow_resolution_divisor),
			j.value("density", data.density * 1000.0f),
			cascades);
	}
	else {
		//Slice count and resolution cost a reallocation, so they go through the setters
		//that rebuild the depth maps; the rest just change the next fit. Handling
		//`resolution` here matters: it is the only lever on texel density, and before
		//this it was read at Init and silently ignored afterwards - so an editor edit
		//appeared to do nothing until the level was reloaded.
		if (j.contains("cascades")) {
			SetCascadeCount(j["cascades"].get<int>());
		}
		if (j.contains("resolution")) {
			SetShadowResolution(j["resolution"].get<int>());
		}
		if (j.contains("shadow_distance")) {
			SetCascadeDistance(j["shadow_distance"].get<float>());
		}
		if (j.contains("cascade_split_lambda")) {
			SetCascadeSplitLambda(j["cascade_split_lambda"].get<float>());
		}
		if (j.contains("caster_extrusion")) {
			SetCasterExtrusion(j["caster_extrusion"].get<float>());
		}
		ToFloat3(j, "color", data.color);
		if (j.contains("direction")) {
			float3 dir = data.direction;
			ToFloat3(j, "direction", dir);
			XMStoreFloat3(&data.direction, XMVector3Normalize(XMVECTOR{ dir.x, dir.y, dir.z }));
		}
		if (j.contains("cast_shadow")) {
			SetCastShadow(j["cast_shadow"].get<bool>());
		}
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

