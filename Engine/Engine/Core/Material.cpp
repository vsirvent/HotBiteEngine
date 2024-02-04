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

#include <Core/Json.h>
#include "Material.h"
#include "Utils.h"

using namespace HotBite::Engine::Core;

void 
MaterialData::_MaterialData() {
	//Set default shaders, after loading the scene the shaders can be changed
	shaders.vs = ShaderFactory::Get()->GetShader<SimpleVertexShader>("MainRenderVS.cso");
	shaders.hs = ShaderFactory::Get()->GetShader<SimpleHullShader>("MainRenderHS.cso");
	shaders.ds = ShaderFactory::Get()->GetShader<SimpleDomainShader>("MainRenderDS.cso");
	shaders.gs = ShaderFactory::Get()->GetShader<SimpleGeometryShader>("MainRenderGS.cso");
	shaders.ps = ShaderFactory::Get()->GetShader<SimplePixelShader>("MainRenderPS.cso");

	shadow_shaders.vs = ShaderFactory::Get()->GetShader<SimpleVertexShader>("ShadowVS.cso");
	shadow_shaders.gs = ShaderFactory::Get()->GetShader<SimpleGeometryShader>("ShadowMapCubeGS.cso");

	depth_shaders.vs = ShaderFactory::Get()->GetShader<SimpleVertexShader>("DepthVS.cso");
	depth_shaders.ps = ShaderFactory::Get()->GetShader<SimplePixelShader>("DepthPS.cso");
}

MaterialData::MaterialData() {
	_MaterialData();
}

MaterialData::MaterialData(const std::string& n): name(n) {
	_MaterialData();
}

MaterialData::MaterialData(const MaterialData& other) {
	assert(!other.init && "Material can't be copied once initialized.");
	*this = other;
}

MaterialData::~MaterialData() {
	Release();
}

void MaterialData::SetTexture(ID3D11ShaderResourceView*& texture, std::string& current_texture, const std::string& root, const std::string& file) {
	std::string new_texure = root + std::string("\\") + file;
	if (current_texture != new_texure) {
		current_texture = new_texure;
		if (texture) {
			texture->Release();
		}
		if (!file.empty()) {
			texture = LoadTexture(current_texture);
		}
		else {
			texture = nullptr;
		}
	}
}

void MaterialData::Load(const std::string& root, const std::string& mat) {
	nlohmann::json j = nlohmann::json::parse(mat);
	name = j["name"];

	props.diffuseColor = parseColorStringF4(j["diffuse_color"]);
	props.bloom_scale = j["bloom_scale"];
	props.parallax_scale = j["parallax_scale"];
	props.parallax_steps = j["parallax_steps"];
	props.parallax_angle_steps = j["parallax_angle_steps"];
	props.parallax_shadow_scale = j["parallax_shadow_scale"];
	props.specIntensity = j["specular"];
	props.opacity = j["opacity"];
	props.density = j["density"];
	props.flags = 0;

	tessellation_type = j["tess_type"];
	tessellation_factor = j["tess_factor"];
	displacement_scale = j["displacement_scale"];
	if (j["alpha_enabled"]) {
		props.flags |= ALPHA_ENABLED_FLAG;
	}
	if (j["blend_enabled"]) {
		props.flags |= BLEND_ENABLED_FLAG;
	}
	if (j["parallax_shadows"]) {
		props.flags |= PARALLAX_SHADOW_ENABLED_FLAG;
	}
	SetTexture(diffuse, texture_names.diffuse_texname, root, j["diffuse_textname"]);
	SetTexture(high, texture_names.high_textname, root, j["high_textname"]);
	SetTexture(normal, texture_names.normal_textname, root, j["normal_textname"]);
	SetTexture(spec, texture_names.spec_textname, root, j["spec_textname"]);
	SetTexture(ao, texture_names.ao_textname, root, j["ao_textname"]);
	SetTexture(arm, texture_names.arm_textname, root, j["arm_textname"]);
	SetTexture(emission, texture_names.emission_textname, root, j["emission_textname"]);
	UpdateFlags();
}

void MaterialData::UpdateFlags() {
	if (diffuse != nullptr) {
		props.flags |= DIFFUSSE_MAP_ENABLED_FLAG;
	}
	if (high != nullptr) {
		props.flags |= PARALLAX_MAP_ENABLED_FLAG;
	}
	if (normal != nullptr) {
		props.flags |= NORMAL_MAP_ENABLED_FLAG;
	}
	if (spec != nullptr) {
		props.flags |= SPEC_MAP_ENABLED_FLAG;
	}
	if (ao != nullptr) {
		props.flags |= AO_MAP_ENABLED_FLAG;
	}
	if (arm != nullptr) {
		props.flags |= ARM_MAP_ENABLED_FLAG;
	}
	if (emission != nullptr) {
		props.flags |= EMISSION_MAP_ENABLED_FLAG;
	}
}

bool MaterialData::Init() {
	bool ret = false;
	init = true;
	diffuse = LoadTexture(texture_names.diffuse_texname);
	high = LoadTexture(texture_names.high_textname);
	normal = LoadTexture(texture_names.normal_textname);
	spec = LoadTexture(texture_names.spec_textname);
	ao = LoadTexture(texture_names.ao_textname);
	arm = LoadTexture(texture_names.arm_textname);
	emission = LoadTexture(texture_names.emission_textname);
	UpdateFlags();
	return ret;
}

void MaterialData::Release() {
	init = false;
	if (diffuse) {
		diffuse->Release();
		diffuse = nullptr;
	}
	if (normal) {
		normal->Release();
		normal = nullptr;
	}
	if (high) {
		high->Release();
		high = nullptr;
	}
	if (spec) {
		spec->Release();
		spec = nullptr;
	}
	if (ao) {
		ao->Release();
		ao = nullptr;
	}
	if (arm) {
		arm->Release();
		arm = nullptr;
	}
}
