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

				float3 alphaColor = {};				
#define NORMAL_MAP_ENABLED_FLAG 1
#define PARALLAX_MAP_ENABLED_FLAG (1 << 1)
#define DIFFUSSE_MAP_ENABLED_FLAG (1 << 2)
#define SPEC_MAP_ENABLED_FLAG (1 << 3)
#define AO_MAP_ENABLED_FLAG (1 << 4)
#define ALPHA_ENABLED_FLAG (1 << 5)
#define ARM_MAP_ENABLED_FLAG (1 << 6)
#define EMISSION_MAP_ENABLED_FLAG (1 << 7)
#define BLEND_ENABLED_FLAG (1 << 10)
#define PARALLAX_SHADOW_ENABLED_FLAG (1 << 11)
				unsigned int flags = 0;
			};

			struct MaterialShaders {
				Core::SimpleVertexShader* vs = nullptr;
				Core::SimpleHullShader* hs = nullptr;
				Core::SimpleDomainShader* ds = nullptr;
				Core::SimpleGeometryShader* gs = nullptr;
				Core::SimplePixelShader* ps = nullptr;
			};

			struct MaterialTextures {
				std::string diffuse_texname;
				std::string normal_textname;
				std::string high_textname;
				std::string spec_textname;
				std::string ao_textname;
				std::string arm_textname;
				std::string emission_textname;
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

				MaterialProps props;
				MaterialTextures texture_names;
				MaterialShaders shaders;
				MaterialShaders shadow_shaders;
				MaterialShaders depth_shaders;
				int tessellation_type = 0;
				float tessellation_factor = 0.0f;
				float displacement_scale = 0.0f;
				float bloom_scale = 0.0f;

				bool init = false;
				
				MaterialData();
				MaterialData(const std::string& name);
				MaterialData(const MaterialData& other);
				~MaterialData();
				void Load(const std::string& root, const std::string& mat);
				bool Init();
				void Release();
			};
		}
	}
}