#pragma once

#include "Defines.h"
#include "SimpleShader.h"
#include <Core/GfxDevice.h>
#include <Components\Camera.h>
#include <map>
#include <string>
#include <vector>

namespace HotBite {
	namespace Engine {
		namespace Core {

			/**
			 * The G-buffer as the geometry pass left it, through the views a compute shader writes it with, and the frame it is
			 * of. Each view is the target of the same name in Common/PixelCommon.hlsli's RenderTargetRT (the splat raster writes
			 * the same set, SplatRasterCS.hlsl): what a G-buffer effect puts in them is lit, shadowed, reflected and anti-aliased
			 * like any surface the geometry pass drew. Any view can be null (the chain is then skipped, see RenderSystem).
			 */
			struct GBufferViews {
				//RenderTargetRT::scene: the surface colour (albedo, AO and emission folded in), RGBA.
				GfxUAV* scene = nullptr;
				//RenderTargetRT::light_map: the light the surface gives off itself.
				GfxUAV* light = nullptr;
				//RenderTargetRT::bloom_map.
				GfxUAV* bloom = nullptr;
				//RenderTargetRT::rt_ray0_map / rt_ray1_map: world position and normal, with the material scalars packed in w
				//(Common/ShaderStructs.hlsli, getColor0 / getColor1 / fromColor).
				GfxUAV* ray0 = nullptr;
				GfxUAV* ray1 = nullptr;
				//RenderTargetRT::pos0_map / pos1_map: where the point was last frame and where it is now (world). The lighting,
				//the tile culling, the ambient occlusion and the motion vectors read the second.
				GfxUAV* prev_position = nullptr;
				GfxUAV* position = nullptr;
				//RenderTargetRT::material_map: how the deferred pass shades the pixel (Common/GBuffer.hlsli).
				GfxUAV* material = nullptr;
				//The depth pre-pass's distance from the camera (R32F, world units), which the post-process chain reads.
				GfxUAV* depth = nullptr;
				//The surface colour as the geometry pass left it, before any stage wrote over it: a stage that reads the colour of
				//another pixel than its own (the ground a blade grows from) reads it here, since `scene` may already hold what another
				//thread of the same dispatch wrote. Bound as `sceneSource` (Texture2D<float4>).
				GfxSRV* scene_source = nullptr;
				GfxSampler* sampler = nullptr;
				int width = 0;
				int height = 0;
				//Seconds, the clock the post-process chain's `time` runs on.
				float time = 0.0f;
				const Components::Camera* camera = nullptr;
			};

			/**
			 * A stage of the G-buffer chain: compute work that runs after the geometry pass has filled the G-buffer and before
			 * the deferred lighting reads it, so whatever it writes there is lit and shadowed as the rest of the scene (a screen
			 * space surface: grass, decals, puddles, fur...). A game builds its stages, links them with SetNext and hands the
			 * first to World::SetGBufferPipeline, the way it does with the post-process chain. The chain is not owned by the
			 * engine.
			 */
			class GBufferEffect {
			public:
				virtual ~GBufferEffect() = default;
				void SetNext(GBufferEffect* effect) { next = effect; }
				GBufferEffect* GetNext() const { return next; }
				void SetEnabled(bool on) { enabled = on; }
				bool IsEnabled() const { return enabled; }
				//Runs this stage (when enabled) and then the rest of the chain.
				void Process(GfxContext* context, const GBufferViews& views);
			protected:
				virtual void Render(GfxContext* context, const GBufferViews& views) = 0;
			private:
				GBufferEffect* next = nullptr;
				bool enabled = true;
			};

			/**
			 * A G-buffer stage made of one compute shader run over every pixel in 8x8 groups. It binds the G-buffer by the
			 * names the splat raster uses (scene_out, light_out, bloom_out, rt_ray0_out, rt_ray1_out, prev_position_out,
			 * position_out, material_out, depth_out; RWTexture2D<float4>, depth_out RWTexture2D<float>), the sampler as
			 * basicSampler, and the frame's screenW, screenH, time, cameraPosition, view_inverse, projection_inverse and
			 * view_projection, and the untouched surface colour as sceneSource. A shader declares only what it uses. SetShaderResourceView adds resources of the stage's own;
			 * SetConstants is where a subclass sets its own constants before each dispatch.
			 */
			class GenericGBufferEffect : public GBufferEffect {
			public:
				//`cs` is a compiled shader's file name, loaded through the ShaderFactory like every engine shader.
				explicit GenericGBufferEffect(const std::string& cs);
				bool Valid() const { return shader != nullptr; }
				//Bound for every dispatch of this stage (null takes it off).
				void SetShaderResourceView(const std::string& name, GfxSRV* srv);
			protected:
				virtual void SetConstants(SimpleComputeShader* cs, const GBufferViews& views) {}
				void Render(GfxContext* context, const GBufferViews& views) override;
				SimpleComputeShader* shader = nullptr;
			private:
				std::map<std::string, GfxSRV*> srvs;
			};
		}
	}
}
