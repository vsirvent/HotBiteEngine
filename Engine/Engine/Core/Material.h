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

#pragma once

#include "SimpleShader.h"
#include "Texture.h"
#include "Json.h"

namespace HotBite {
	namespace Engine {
		namespace Core {
			struct MaterialProps {
				float4 ambientColor = {};
				float4 diffuseColor = {};
				
				float specIntensity = {};
				float parallax_scale = 0.0f;
				float parallax_steps = 4.0f;
				float parallax_angle_steps = 5.0f;

				float parallax_shadow_scale = 2.0f;
				float bloom_scale = 0.0f;
				float opacity = 1.0f;
				float density = 1.0f;

				float emission = 0.0f;
				float3 emission_color = {};

				float rt_reflex = 0.2f;
				float3 padding;

				float3 alphaColor = {};				
#define NORMAL_MAP_ENABLED_FLAG 1
#define PARALLAX_MAP_ENABLED_FLAG (1 << 1)
#define DIFFUSSE_MAP_ENABLED_FLAG (1 << 2)
#define SPEC_MAP_ENABLED_FLAG (1 << 3)
#define AO_MAP_ENABLED_FLAG (1 << 4)
#define ALPHA_ENABLED_FLAG (1 << 5)
#define ARM_MAP_ENABLED_FLAG (1 << 6)
#define EMISSION_MAP_ENABLED_FLAG (1 << 7)
#define OPACITY_MAP_ENABLED_FLAG (1 << 8)
#define BLEND_ENABLED_FLAG (1 << 10)
#define PARALLAX_SHADOW_ENABLED_FLAG (1 << 11)
#define RAY_TRACING_ENABLED_FLAG (1 << 12)
				unsigned int flags = RAY_TRACING_ENABLED_FLAG;
			};

			struct MaterialShaders {
				Core::SimpleVertexShader* vs = nullptr;
				Core::SimpleHullShader* hs = nullptr;
				Core::SimpleDomainShader* ds = nullptr;
				Core::SimpleGeometryShader* gs = nullptr;
				Core::SimplePixelShader* ps = nullptr;
			};

			//The .cso file names behind MaterialShaders. The resolved pointers alone
			//cannot answer "which shader is this material using?" - ShaderFactory hands
			//out shared instances and keeps no reverse mapping - so an editor that wants
			//to show or change a material's shaders needs the names kept alongside them.
			//These are the defaults MaterialData's constructor installs; Load() overwrites
			//them from the file and Save() writes them back.
			struct MaterialShaderNames {
				std::string draw_vs = "MainRenderVS.cso";
				std::string draw_hs = "MainRenderHS.cso";
				std::string draw_ds = "MainRenderDS.cso";
				std::string draw_gs = "MainRenderGS.cso";
				std::string draw_ps = "MainRenderPS.cso";
				std::string shadow_vs = "ShadowVS.cso";
				std::string shadow_gs = "ShadowMapCubeGS.cso";
				std::string depth_vs = "DepthVS.cso";
				std::string depth_ps = "DepthPS.cso";
			};

			struct MaterialTextures {
				std::string diffuse_texname;
				std::string normal_textname;
				std::string high_textname;
				std::string spec_textname;
				std::string ao_textname;
				std::string arm_textname;
				std::string emission_textname;
				std::string opacity_textname;
			};

			class MaterialData {
			private:
				void _MaterialData();
				void SetTexture(ID3D11ShaderResourceView*& texture, std::string& current_texture, const std::string& root, const std::string& file);
				void UpdateFlags();
			public:
				std::string name;
				ID3D11ShaderResourceView* diffuse = nullptr;
				ID3D11ShaderResourceView* normal = nullptr;
				ID3D11ShaderResourceView* high = nullptr;
				ID3D11ShaderResourceView* spec = nullptr;
				ID3D11ShaderResourceView* ao = nullptr;
				ID3D11ShaderResourceView* arm = nullptr;
				ID3D11ShaderResourceView* emission = nullptr;
				ID3D11ShaderResourceView* opacity = nullptr;

				MaterialProps props;
				//Absolute paths (root + file), not the bare file names the .mat carries.
				//Save() turns them back into root-relative names.
				MaterialTextures texture_names;
				MaterialShaders shaders;
				MaterialShaders shadow_shaders;
				MaterialShaders depth_shaders;
				MaterialShaderNames shader_names;
				int tessellation_type = 0;
				float tessellation_factor = 0.0f;
				float displacement_scale = 0.0f;
				float bloom_scale = 0.0f;

				bool init = false;

				//The JSON record this material was loaded from, kept verbatim so Save()
				//can write back the keys Load() does not consume - "ambient_color", the
				//legacy empty "vs"/"hs"/"ds"/"gs"/"ps" entries, "normal_map_enabled", and
				//any key a future engine version adds. Without it, opening a .mat in the
				//editor and saving it would quietly drop or zero those fields. Empty for a
				//material created in code or in the editor, which then saves defaults only.
				nlohmann::json source_json;

				MaterialData();
				MaterialData(const std::string& name);
				MaterialData(const MaterialData& other);
				MaterialData& operator=(const MaterialData& other);
				~MaterialData();
				void Load(const std::string& root, const std::string& mat);
				//The inverse of Load: the material as a .mat "materials" array entry.
				//`root` is the texture folder the file declares; texture paths are written
				//relative to it, so a material whose textures live outside `root` keeps its
				//absolute path rather than silently pointing at the wrong file.
				nlohmann::json Save(const std::string& root) const;

				// Re-resolves every shader stage from `names` and, on success, adopts
				// them. All-or-nothing: if any name fails to load as the stage it was
				// asked for, nothing is changed and false is returned, so a bad pick can
				// never leave the material half-rebound and undrawable.
				//
				// Callers must re-register the entities using this material with the
				// render system afterwards (World::SetMaterialShaders does): its draw
				// trees are keyed by shader tuple, and nothing else notices the change.
				bool SetShaders(const MaterialShaderNames& names);
				bool Init();
				void Release();
			};

			void ReleaseTexture(ID3D11ShaderResourceView* srv);
			ID3D11ShaderResourceView* LoadTexture(const std::string& filename);
		}
	}
}