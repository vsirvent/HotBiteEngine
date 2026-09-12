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
#include <ECS/Serialization.h>

#define MAX_LIGHTS 8
#define MAX_OBJECTS 100
#define MAX_INTENSITY 1000.0f

//Shadow cascades per directional light. Two hard ceilings meet here and both are
//real: the shadow geometry shader (Shaders/ShadowRender/ShadowMapCubeGS.hlsl, shared
//with the point light's six cube faces) routes a triangle to at most six slices in
//one pass, and MAX_SHADOW_CASCADES in Shaders/Common/Defines.hlsli sizes the
//per-cascade matrix array every lighting shader carries. Four is what that array is
//sized for; raising it means editing the HLSL define too, and paying MAX_LIGHTS *
//MAX_SHADOW_CASCADES matrices of constant buffer in every one of those shaders.
#define MAX_SHADOW_CASCADES 4

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
				static constexpr const char* NAME = "AmbientLight";

				AmbientLight();
				AmbientLight(const float3& color_down, const float3& color_up);
				struct Data& GetData();

				nlohmann::json ToJson(const ECS::SerializeContext& ctx) const;
				void FromJson(const nlohmann::json& j, const ECS::SerializeContext& ctx);
			};

			/**
			 * Directional light component.
			 */
			class DirectionalLight : public Light
			{
			public:
#define DIR_LIGHT_FLAG_FOG 1
#define DIR_LIGHT_FLAG_INVERSE 2
				//Set by RefreshStaticViewMatrix once the static shadow map holds a rendered
				//frame. Shaders must not sample DirStaticShadowMapTexture without it: an
				//unwritten/unbound depth SRV compares as fully occluded and blacks the scene.
				//Runtime-only, never serialized (see ToJson/FromJson, which persist the
				//individual flags rather than the raw value).
#define DIR_LIGHT_FLAG_STATIC_SHADOW 4
				//Debug view: makes the lighting shaders tint this light's contribution by
				//which shadow cascade shaded each pixel (DirCascadeDebugTint in
				//Shaders/Common/PixelFunctions.hlsli). Runtime-only like the flag above -
				//ToJson persists the authored flags individually and deliberately not
				//this one, so a level cannot be saved stuck in the debug view.
#define DIR_LIGHT_FLAG_DEBUG_CASCADES 8
				//Debug view: tint by the static caster map instead - what it reaches and
				//what it shadows. Runtime-only, and never set at the same time as the
				//cascade tint (they recolour the same term).
#define DIR_LIGHT_FLAG_DEBUG_STATIC 16
				//Mirrored field for field by struct DirLight in
				//Shaders/Common/PixelCommon.hlsli - it is memcpy'd straight into that
				//cbuffer layout, so the two must be edited together, trailing padding
				//included.
				struct Data {
					float3 color{};
					float  intensity = 1.0f;
					float3 direction{};
					float density;
					uint32_t cast_shadow = 0;
					float3 position{};
					float range = 0.0f;
					int flags = 0;
					//How many of this light's cascade slices hold a rendered frame. The
					//shader walks cascades 0..cascade_count-1 and takes the first whose
					//footprint contains the pixel, so an unwritten slice must never be
					//counted: its matrix is all zeros, every point projects to the middle
					//of it, and it would swallow the whole scene into an empty map.
					int cascade_count = 1;
					float padding;
				};

				//What a level author sets to shape the cascade set. Held separately from
				//Data because none of it reaches a shader: the fitting happens on the CPU
				//in DirectionalLightSystem and only the resulting matrices are uploaded.
				struct CascadeSettings {
					//Slices, 1..MAX_SHADOW_CASCADES. One is a plain single shadow map,
					//still fitted to the view (so it is not the old behaviour).
					int count = 3;
					//How far from the camera the cascade set reaches, in world units.
					//This is the knob that decides shadow quality: the whole texel budget
					//is spread over this distance, and the camera's own far plane (10000)
					//would be a catastrophic value here.
					float distance = 150.0f;
					//Blend between a uniform split (0) and a logarithmic one (1) when
					//placing the cascade boundaries - the "practical split scheme". A
					//perspective camera's screen-space texel density falls off as 1/z, so
					//the logarithmic term is the one that matches it; the uniform term
					//keeps the first cascade from collapsing to a sliver.
					float split_lambda = 0.9f;
					//How far back along the light the near plane is pulled, past what the
					//view actually needs. An object standing outside the camera frustum
					//still casts into it, and without this its depth is simply not in the
					//map - shadows pop in as their caster enters the view.
					float caster_extrusion = 500.0f;

					void FromJson(const nlohmann::json& j);
					void ToJson(nlohmann::json& j) const;
				};

				//The fit of one cascade, in world space, as the system computed it. Kept
				//alongside the matrix it produced so a debug view can draw exactly the
				//volume that was rendered (Tools/SceneEditor/ShadowDebug.cpp) rather than
				//re-deriving it and drawing something subtly different.
				struct CascadeInfo {
					//Centre of the ortho box, after texel snapping - the box the matrix
					//describes, not the raw bounding sphere centre.
					float3 center{};
					//Light basis: `dir` is the direction light travels, right/up span the
					//face of the box.
					float3 dir{ 0.0f, -1.0f, 0.0f };
					float3 right{ 1.0f, 0.0f, 0.0f };
					float3 up{ 0.0f, 0.0f, 1.0f };
					//Half width/height of the box: the fitted sphere's radius, so the box
					//is 2*radius square regardless of how the camera is turned.
					float radius = 0.0f;
					//Extra depth in front of `center` along `dir`, i.e. the caster
					//extrusion. The box runs from center - dir*(radius + extrusion) to
					//center + dir*radius.
					float extrusion = 0.0f;
					//Camera view-space depth range this cascade was fitted to.
					float near_split = 0.0f;
					float far_split = 0.0f;
					//Shadow texels per world unit, i.e. resolution / (2 * radius). The one
					//number that says whether a cascade is worth its memory.
					float texel_density = 0.0f;
				};

			private:
				struct Data data;

				bool init = false;
				//Kept so the light can be written back out as it was authored, and
				//consumed by Init to size each cascade slice.
				int shadow_resolution_divisor = 1;
				CascadeSettings cascade_settings;
				//One slice per cascade, in a Texture2DArray rather than N separate maps:
				//the pixel shader selects its cascade with a texture coordinate, which
				//costs no extra shader register (see Core::DepthTexture2DArray), and the
				//shadow geometry shader fills every slice in one pass. Dynamic casters
				//only - a static caster is in the map below instead.
				Core::DepthTexture2DArray texture;
				//Static casters get one plain map, not a cascade set, fitted to the
				//*widest* cascade so it covers everything the cascades do. Cascading it
				//would be spending three slices to re-render, on a slow cycle, geometry
				//that is not moving: the whole point of the static/dynamic split is that
				//the per-frame pass only has to touch things that move. The cost is
				//density - static casters shade at the widest cascade's texels per unit
				//everywhere, near camera included.
				Core::DepthTexture2D static_texture;
				ECS::Entity parent = ECS::INVALID_ENTITY_ID;
				std::unordered_set<ECS::Entity> skip;
				float4x4 lightPerspectiveValues = {};
				float4x4 worldMatrix = {};
				//World -> cascade clip, transposed for HLSL, one per cascade and always
				//MAX_SHADOW_CASCADES long: the shaders index this as
				//[light_index * MAX_SHADOW_CASCADES + cascade], so the stride is fixed
				//even when a light uses fewer slices.
				float4x4 viewMatrix[MAX_SHADOW_CASCADES] = {};
				CascadeInfo cascade_info[MAX_SHADOW_CASCADES] = {};
				//The single static map's matrix, and the fit it was rendered under. Both
				//are snapshots taken at the last refresh, which is why they are held apart
				//from the live ones: the map holds depths from that moment and must be
				//sampled through that moment's matrix.
				float4x4 static_viewMatrix = {};
				CascadeInfo static_cascade_info = {};
				float4x4 spotMatrix = {};
				float4x4 projectionMatrix = {};
				float3 last_cam_pos = {};
				float3 last_cam_dir = {};
				bool dirty = true;
				D3D11_VIEWPORT shadow_vp = {};

			public:
				static constexpr const char* NAME = "DirectionalLight";

				nlohmann::json ToJson(const ECS::SerializeContext& ctx) const;
				void FromJson(const nlohmann::json& j, const ECS::SerializeContext& ctx);

				DirectionalLight();
				DirectionalLight(const DirectionalLight& other) :DirectionalLight() {
					assert(!other.init && "Non copyable after init.");
					*this = other;
				}

				void SetDirty() { dirty = true; }
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
					float volume_density,
					const CascadeSettings& cascades = CascadeSettings{});
				HRESULT Release();
				void RefreshStaticViewMatrix();
				ID3D11ShaderResourceView* StaticDepthResource();
				ID3D11DepthStencilView* StaticDepthView();
				ID3D11ShaderResourceView* DepthResource();
				ID3D11DepthStencilView* DepthView();
				bool CastShadow() const;
				//Turns shadows on or off after Init. Off just stops the cascade fit
				//(CastShadow() gates it); on allocates the depth maps if this light has
				//never had them (constructed/loaded with shadows off), same as Init would.
				//Returns false if the maps could not be allocated, in which case shadows
				//stay off.
				bool SetCastShadow(bool enable);
				struct Data& GetData();
				const D3D11_VIEWPORT& GetShadowViewPort() const;

				//Cascades. The count is what the light was *initialized* with, since it
				//sizes the depth array; changing it needs the textures rebuilt, which is
				//what SetCascadeCount does.
				int GetCascadeCount() const { return data.cascade_count; }
				const CascadeSettings& GetCascadeSettings() const { return cascade_settings; }
				//Everything but `count` is pure CPU-side fitting input, so these take
				//effect on the next update with no reallocation.
				void SetCascadeDistance(float d);
				void SetCascadeSplitLambda(float l);
				void SetCasterExtrusion(float e);
				//Reallocates both depth arrays, so it is the expensive one. No-op when the
				//count is unchanged. Returns false if the new arrays could not be created,
				//in which case the light keeps the ones it had.
				bool SetCascadeCount(int count);
				//The `resolution` multiplier: every shadow map this light owns is
				//BASE_CASCADE_RESOLUTION * divisor texels square. Reallocates, like
				//SetCascadeCount, and is the knob to reach for when a wide shadow
				//distance has left the cascades too coarse - texel density is
				//resolution / cascade width, and only one of those two is free.
				bool SetShadowResolution(int divisor);
				int GetShadowResolution() const { return shadow_resolution_divisor; }
			private:
				HRESULT AllocateShadowMaps(int count, int resolution);
			public:
				//Fitted geometry of cascade `i`, for debug drawing. Only indices below
				//GetCascadeCount() hold anything.
				const CascadeInfo& GetCascadeInfo(int i) const;
				//The fit the static map currently holds. `radius` is 0 until it has been
				//rendered once.
				const CascadeInfo& GetStaticCascadeInfo() const { return static_cascade_info; }
				//Resolution of the static map, in texels (square).
				int GetStaticResolution() const;
				//Whether the static map no longer covers what the cascades do, because the
				//camera has carried the widest cascade away from where the map was
				//rendered. Without this the map is only rebuilt every
				//RenderSystem::STATIC_SHADOW_REFRESH_PERIOD frames, and walking out of its
				//footprint makes every static object's shadow simply disappear until the
				//next refresh comes round.
				bool StaticShadowStale() const;
				//Debug views: colour each pixel by the cascade that shaded it, or by what
				//the static caster map covers and shadows. The work is in the shaders
				//(DIR_LIGHT_FLAG_DEBUG_*); these are the switches. Mutually exclusive -
				//both recolour the same term - so enabling one clears the other.
				void SetDebugCascades(bool enable) {
					if (enable) data.flags = (data.flags | DIR_LIGHT_FLAG_DEBUG_CASCADES) &
						~DIR_LIGHT_FLAG_DEBUG_STATIC;
					else data.flags &= ~DIR_LIGHT_FLAG_DEBUG_CASCADES;
				}
				bool GetDebugCascades() const {
					return (data.flags & DIR_LIGHT_FLAG_DEBUG_CASCADES) != 0;
				}
				void SetDebugStaticShadow(bool enable) {
					if (enable) data.flags = (data.flags | DIR_LIGHT_FLAG_DEBUG_STATIC) &
						~DIR_LIGHT_FLAG_DEBUG_CASCADES;
					else data.flags &= ~DIR_LIGHT_FLAG_DEBUG_STATIC;
				}
				bool GetDebugStaticShadow() const {
					return (data.flags & DIR_LIGHT_FLAG_DEBUG_STATIC) != 0;
				}
				//Resolution of one cascade slice, in texels (square).
				int GetCascadeResolution() const;

				//MAX_SHADOW_CASCADES matrices, of which the first GetCascadeCount() are
				//live. Contiguous because RenderSystem uploads them as one fixed-stride
				//array covering every light.
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
				//See DirectionalLight::shadow_resolution_divisor.
				int shadow_resolution_divisor = 1;

			public:
				static constexpr const char* NAME = "PointLight";

				nlohmann::json ToJson(const ECS::SerializeContext& ctx) const;
				void FromJson(const nlohmann::json& j, const ECS::SerializeContext& ctx);

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
				static constexpr const char* NAME = "Lighted";

				//Pure runtime state: the lighting systems rebuild every one of these lists
				//each frame from the lights actually reaching the entity. Serializing it
				//would be meaningless, so the block is empty and only presence matters -
				//which is exactly what a designer toggles ("is this entity lit?").
				nlohmann::json ToJson(const ECS::SerializeContext& ctx) const {
					return nlohmann::json::object();
				}
				void FromJson(const nlohmann::json& j, const ECS::SerializeContext& ctx) {}

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