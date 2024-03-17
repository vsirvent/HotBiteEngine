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

#include <Defines.h>
#include <Core/Texture.h>
#include <Core/Update.h>
#include <ECS/Coordinator.h>

#define MAX_LIGHTS 8
#define MAX_OBJECTS 64
#define MAX_INTENSITY 1000.0f

namespace HotBite {
	namespace Engine {
		namespace Systems {
			class SkySystem;
			class DirectionalLightSystem;
			class PointLightSystem;
		}

		namespace Components {

			enum class LightType {
				Ambient, Directional, Point,
				//Not implemented
				Spot, Area, Volume
			};

			/**
			 * Light parent class, all light components are child of Light
			 */
			class Light {
			protected:
				LightType type;
				Light(LightType t) : type(t) {}
				virtual ~Light() {}
			public:
				virtual LightType GetType() const { return type; }
			};

			/**
			 * Ambient light component.
			 */
			class AmbientLight : public Light
			{
			public:
				struct Data {
					float3 colorDown{};
					float  padding0;
					float3 colorUp{};
					float  padding1;
				};
			private:
				struct Data data;

			public:
				AmbientLight();
				AmbientLight(const float3& color_down, const float3& color_up);
				struct Data& GetData();
			};

			/**
			 * Directional light component.
			 */
			class DirectionalLight : public Light
			{
			public:
#define DIR_LIGHT_FLAG_FOG 1
#define DIR_LIGHT_FLAG_INVERSE 2
				struct Data {
					float3 color{};
					float  intensity = 1.0f;
					float3 direction{};
					float density;
					uint32_t cast_shadow = 0;
					float3 position{};
					float range = 0.0f;
					int flags = 0;
					float2 padding;
				};

			private:
				struct Data data;

				bool init = false;
				int texture_resolution_ratio = 8;
				Core::DepthTexture2D static_texture;
				Core::DepthTexture2D texture;
				ECS::Entity parent = ECS::INVALID_ENTITY_ID;
				std::unordered_set<ECS::Entity> skip;
				float4x4 lightPerspectiveValues = {};
				float4x4 worldMatrix = {};
				float4x4 viewMatrix = {};
				float4x4 static_viewMatrix = {};
				float4x4 spotMatrix = {};
				float4x4 projectionMatrix = {};
				float3 last_cam_pos = {};
				bool dirty = true;
				D3D11_VIEWPORT shadow_vp = {};

			public:
				DirectionalLight();
				DirectionalLight(const DirectionalLight& other) :DirectionalLight() {
					assert(!other.init && "Non copyable after init.");
					*this = other;
				}
				void SetPosition(const float3& pos) { data.position = pos; }
				const float3& GetPosition() const { return data.position; }
				void SetFog(bool enable) {
					if (enable) data.flags |= DIR_LIGHT_FLAG_FOG;
					else data.flags &= ~DIR_LIGHT_FLAG_FOG;
				}
				bool GetFog() { return data.flags & DIR_LIGHT_FLAG_INVERSE; }
				void SetInverse(bool enable) {
					if (enable) data.flags |= DIR_LIGHT_FLAG_INVERSE;
					else data.flags &= ~DIR_LIGHT_FLAG_INVERSE;
				}
				bool GetInverse() { return data.flags & DIR_LIGHT_FLAG_INVERSE; }
				void SetRange(float range) { data.range = range; }
				float GetRange() const { return data.range; }
				
				void SetParent(ECS::Entity e) { parent = e; }
				ECS::Entity GetParent() const { return parent; }
				void AddSkipEntity(ECS::Entity e) { skip.insert(e); }
				void RemoteSkipEntity(ECS::Entity e) { if (skip.contains(e)) skip.erase(e); }
				bool IsSkipEntity(ECS::Entity e) const { return skip.contains(e); }
				
				HRESULT Init(const float3& c, const float3& dir,
					bool cast_shadow, int shadow_resolution_divisor,
					float volume_density);
				HRESULT Release();
				void RefreshStaticViewMatrix();
				ID3D11ShaderResourceView* StaticDepthResource();
				ID3D11DepthStencilView* StaticDepthView();
				ID3D11ShaderResourceView* DepthResource();
				ID3D11DepthStencilView* DepthView();
				bool CastShadow() const;
				struct Data& GetData();
				const D3D11_VIEWPORT& GetShadowViewPort() const;
				const float4x4* GetViewMatrix() const;
				const float4x4* GetStaticViewMatrix() const;
				const float4x4& GetLightPerspectiveValues() const;
				const float4x4& GetProjectionMatrix() const;
				const float4x4& GetSpotMatrix() const;
				void SetSpotMatrix(const matrix& m);

				friend class HotBite::Engine::Systems::DirectionalLightSystem;
				friend class HotBite::Engine::Systems::SkySystem;
			};

			/**
			 * Point light component.
			 */
			class PointLight : public Light, public Core::IDepthResource
			{
			public:
				struct Data
				{
					float3 position = {};
					float  range = {};
					float3 color = {};
					float density = 0.0f;
					int cast_shadow = 0;
					float tilt_ratio = 10.0f;
					float2 padding;
				};

			private:
				struct Data data = {};
				Core::DepthTextureCube texture;
				bool dirty = false;
				float4x4 lightPerspectiveValues = {};
				float4x4 worldMatrix = {};
				float4x4 viewMatrix[6] = {};
				float4x4 projectionMatrix = {};
				D3D11_VIEWPORT shadow_vp;
				bool init = false;

			public:
				PointLight();
				PointLight(const PointLight& other) :PointLight() {
					assert(!other.init && "Non copyable after init.");
					*this = other;
				}
				HRESULT Init(const float3& color, float range, bool cast_shadow, int shadow_resolution_divisor, float volume_density);
				HRESULT Release();

				bool CastShadow() const;
				const D3D11_VIEWPORT& GetShadowViewPort() const;

				virtual ID3D11ShaderResourceView* DepthResource() override;
				virtual ID3D11DepthStencilView* DepthView() override;

				struct Data& GetData();

				const float4x4* GetViewMatrix() const;
				const float4x4& GetLightPerspectiveValues() const;
				const float4x4& GetProjectionMatrix() const;

				friend class HotBite::Engine::Systems::PointLightSystem;
			};

			/**
			 * Component for entities that are affected by lights.
			 * Used as a standard class in the RenderSystem to have global illumation
			 */
			struct Lighted {
				std::vector<PointLight::Data> point_lights;
				std::vector<DirectionalLight::Data> dir_lights;
				std::vector<ID3D11ShaderResourceView*> dir_shadows;
				std::vector<ID3D11ShaderResourceView*> dir_static_shadows;
				std::vector<float4x4> dir_shadows_perspectives;
				std::vector<float4x4> dir_static_shadows_perspectives;
				std::vector<ID3D11ShaderResourceView*> shadows;
				std::vector<float2> shadows_perspectives;
			};
		}
	}
}