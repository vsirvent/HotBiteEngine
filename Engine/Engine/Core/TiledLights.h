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
#include <Core/GfxDevice.h>
#include <cstdint>
#include "RWBuffer.h"
#include "SimpleShader.h"

namespace HotBite {
	namespace Engine {
		namespace Core {

			//One point or spot light as the shaders read it - 32 bytes, mirrored by
			//PackedLight in Shaders/Common/TiledLights.hlsli (which documents the fields).
			struct PackedLight {
				float position[3];
				float range;
				uint32_t color;
				uint32_t direction;
				uint32_t cones;
				uint32_t flags;
			};
			static_assert(sizeof(PackedLight) == 32, "PackedLight is read by HLSL as a 32 byte structured element");

			//What the shaders without a screen tile get: the light plus what only glows and
			//fog read. Mirrored by AuxLight in TiledLights.hlsli, 48 bytes (three registers).
			struct AuxLight {
				PackedLight light;
				float density;
				float tilt_ratio;
				float padding[2];
			};
			static_assert(sizeof(AuxLight) == 48, "AuxLight is a constant buffer array element of three registers");

			static constexpr uint32_t PACKED_LIGHT_SPOT = 1u;
			static constexpr uint32_t PACKED_LIGHT_SHADOW = 2u;
			static constexpr uint32_t PACKED_LIGHT_SLOT_SHIFT = 2u;
			//Bits 2..5 hold the shadow map slot; this one says the light's shadow is a ray
			//traced in the lighting pass (Core/ShadowRays.h) instead. Never set together with
			//PACKED_LIGHT_SHADOW: a light casts a shadow map or a ray traced shadow, not both.
			static constexpr uint32_t PACKED_LIGHT_RAYTRACED = 64u;
			//Bits 8 and up of a ray traced light: its shadow ray scene (Core::ShadowRays), set by
			//RenderSystem::BuildShadowRays once the scenes are made.
			static constexpr uint32_t PACKED_LIGHT_SCENE_SHIFT = 8u;

			//Packs a light for the GPU. `shadow_slot` is the PointShadowMapTexture slot the
			//light's cube map is bound to, or -1 for no shadow map. `ray_traced` marks a light
			//whose shadow the lighting pass traces; it is ignored when the light holds a slot.
			//The colour keeps a 9 bit mantissa per channel under one shared exponent; the cone
			//is stored as two float16 cosines and the axis as an octahedral pair of int16.
			PackedLight PackLight(const float position[3], float range, const float color[3], bool is_spot,
				const float direction[3], float cos_inner, float cos_outer, int shadow_slot, bool ray_traced = false);

			//The inverse of the three lossy fields, in C++: what the GPU will decode. Tests
			//use these to check that packing does not move a light.
			void UnpackLightColor(const PackedLight& l, float out[3]);
			void UnpackLightDirection(const PackedLight& l, float out[3]);

			//What the culling pass saw, read back a few frames late (see RWByteBuffer).
			struct TiledLightStats {
				uint32_t lights = 0;            //point/spot lights packed this frame
				uint32_t shadowed = 0;          //of those, how many hold a shadow slot
				uint32_t raytraced = 0;         //of those, how many trace their shadow instead
				uint32_t raytraced_directional = 0;//ray traced directional lights
				uint32_t shadow_ray_objects = 0;//objects in the scene those rays are traced through
				uint64_t shadow_ray_indices = 0;     //index count of the geometry those objects hold in the scene (the level the rays hit)
				uint64_t shadow_ray_full_indices = 0;//what their finest levels would have been
				uint32_t shadow_ray_scenes = 0;      //scenes the rays go through (one per ray traced light, one for the sun)
				uint32_t shadow_ray_dropped = 0;     //scenes that did not fit the frame's budget: those lights trace nothing in the world
				uint32_t shadow_ray_owned_skipped = 0;//casters left out because their mesh has a buffer of its own (the BVH cannot hold one): the terrain
				uint32_t tiles_x = 0;
				uint32_t tiles_y = 0;
				uint32_t tiles_lit = 0;         //tiles whose mask holds at least one light
				uint32_t tiles_with_geometry = 0;
				uint32_t max_per_tile = 0;      //the busiest tile's light count
				uint64_t total_bits = 0;        //sum over tiles of the lights in their mask
				uint32_t frame = 0;             //the frame these counters describe
				bool valid = false;             //false until the first readback has landed
			};

			//The brute-force check of the masks (TileLightValidateCS), taken synchronously.
			struct TiledLightValidation {
				uint64_t pixels = 0;   //pixels showing a surface
				uint64_t reached = 0;  //(pixel, light) pairs where the light's range covers the pixel
				uint64_t missed = 0;   //of those, the pairs the pixel's tile mask does not hold. 0 when right.
				uint64_t reached_raytraced = 0;//of `reached`, the pairs whose light traces its shadow: the most rays the lighting pass can cast
				bool valid = false;
			};

			//The GPU side of the tiled point light pass: the one 1024 light buffer, the
			//per-tile bitmasks, the culling dispatch that fills them and the counters that
			//describe the result. RenderSystem decides *which* lights go in; this owns where
			//they live and how the screen is divided.
			class TiledLights {
			public:
				static constexpr uint32_t TILE_SIZE = 16;
				static constexpr uint32_t CAPACITY = 1024;
				static constexpr uint32_t MASK_WORDS = CAPACITY / 32;

				TiledLights() = default;
				~TiledLights() { Release(); }
				TiledLights(const TiledLights&) = delete;
				TiledLights& operator=(const TiledLights&) = delete;

				bool Init(int width, int height);
				void Release();

				uint32_t TilesX() const { return tiles_x; }
				uint32_t TilesY() const { return tiles_y; }
				uint32_t LightCount() const { return light_count; }
				GfxSRV* LightsSRV() const { return lights_srv; }
				GfxSRV* MasksSRV() const { return masks.SRV(); }

				//Replaces the frame's lights. At most CAPACITY are taken.
				void Upload(const PackedLight* lights, uint32_t count);

				//Fills the masks for the lights uploaded this frame, against the surfaces in
				//`positions` (the G-buffer's world positions). `view` and `projection_inverse`
				//are the camera's, uploaded like every other matrix. Counts are collected
				//only when `collect_stats` is set, which costs a few atomics per tile.
				//`culling` false puts every light in every tile that shows a surface - the
				//reference the culling is measured against, not a mode for a game.
				void Cull(const float4x4& view, const float4x4& projection_inverse,
					GfxSRV* positions, bool collect_stats, bool culling = true);

				//Brute-force check that Cull's masks hold every light that reaches a visible
				//pixel. Stalls the GPU to read the answer: tests only.
				TiledLightValidation Validate(GfxSRV* positions);

				//The counters of the last Cull with collect_stats, a few frames stale.
				//`lights`/`shadowed`/`frame` are filled in by the caller.
				void ReadStats(TiledLightStats& stats);

			private:
				uint32_t width = 0;
				uint32_t height = 0;
				uint32_t tiles_x = 0;
				uint32_t tiles_y = 0;
				uint32_t light_count = 0;
				//The masks are all zero: set when a frame without lights has written them, so
				//the next such frame does not.
				bool masks_cleared = false;

				GfxBuffer* lights_buffer = nullptr;
				GfxSRV* lights_srv = nullptr;
				RWByteBuffer masks;
				RWByteBuffer stats_buffer;

				SimpleComputeShader* cull_shader = nullptr;
				SimpleComputeShader* validate_shader = nullptr;
			};
		}
	}
}
