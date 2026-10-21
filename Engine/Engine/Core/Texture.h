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

#include "DXCore.h"

#include <vector>
#include <functional>
#include <mutex>

namespace HotBite {
	namespace Engine {
		namespace Core {
			//What D3D11 called GFX_BIND_UNORDERED_ACCESS, for the `bind_flags` of RenderTexture2D::Init.
			constexpr uint32_t GFX_BIND_UNORDERED_ACCESS = 0x80;

			//The pixel layouts game code can ask for without naming a graphics API.
			enum class TextureFormat {
				R8,		//one 8-bit channel, read as 0..1
				RGBA8,	//four 8-bit channels, byte order R, G, B, A
			};

			//A texture the CPU rewrites whenever it likes (a minimap, a fog mask, a procedural image) and shaders sample.
			//This is the way for game code to make a texture: it names no graphics API, so it keeps working when the
			//backend changes. `SRV()` goes to `SetShaderResourceView` / `Widget::SetTexture`.
			class DynamicTexture2D {
			private:
				GfxTexture* texture = nullptr;
				GfxSRV* shader_resource_view = nullptr;
				int width = 0;
				int height = 0;
				TextureFormat format = TextureFormat::RGBA8;
			public:
				DynamicTexture2D() = default;
				DynamicTexture2D(const DynamicTexture2D&) = delete;
				DynamicTexture2D& operator=(const DynamicTexture2D&) = delete;
				~DynamicTexture2D();
				//Creates the texture (replacing a previous one); the contents are undefined until the first Update.
				HRESULT Init(int w, int h, TextureFormat texture_format);
				//Replaces the whole image. `row_pitch` is the bytes between rows in `pixels`; 0 means tightly packed.
				void Update(const void* pixels, uint32_t row_pitch = 0);
				void Release();
				bool Valid() const { return texture != nullptr; }
				int Width() const { return width; }
				int Height() const { return height; }
				GfxSRV* SRV() const { return shader_resource_view; }
			};

			//Dynamic resolution. The textures of the frame (the G-buffer, the lights, the post-process chain up to the
			//upscale) are the size of the *internal* image, which a controller changes while the game runs; the interface
			//and the back buffer stay native. A texture that follows the scale (InitScaled) is made once for every size
			//(RenderScale::Build): the sizes are placed over one heap, so all of them together cost the memory of the
			//biggest and switching is only a barrier, with no allocation to stall a frame. Every shader reads the size of
			//the texture it is given (GetDimensions), so none of them know about the scale.
			class ScaledResource {
			public:
				ScaledResource() = default;
				//A copy is never registered: only the texture that was made through InitScaled is.
				ScaledResource(const ScaledResource&) {}
				ScaledResource& operator=(const ScaledResource&) { return *this; }
				virtual ~ScaledResource();

				//Makes the texture of every size (the one it has now is released), given the internal size of each step.
				virtual HRESULT BuildSteps(const std::vector<std::pair<int, int>>& screen_sizes) = 0;
				//Makes `step` the texture in use: nothing in it is kept.
				virtual void SelectStep(int step) = 0;
				//A texture that stays at the native size whatever the scale is (the one the interface is composed on).
				void SetNative(bool native);
				bool IsNative() const { return pinned_native; }

			protected:
				void RegisterScaled();
				void UnregisterScaled();
				bool pinned_native = false;
				bool registered = false;
			};

			class RenderScale {
			public:
				//Sizes made by Build: step 0 is the native size and each one after it is STEP_RATIO of the one before.
				static constexpr int STEPS = 12;
				static constexpr float STEP_RATIO = 0.92f;

				static RenderScale& Get();

				//The native (output) size every internal size is a fraction of.
				void SetNativeSize(int w, int h);
				int NativeWidth() const { return native_w; }
				int NativeHeight() const { return native_h; }

				//Makes every registered texture in every size. True when it is done (or was already). The textures
				//created later through InitScaled are made in every size at once.
				bool Build();
				bool Built() const { return built; }
				int StepCount() const { return built ? STEPS : 1; }
				const std::vector<std::pair<int, int>>& Sizes() const { return sizes; }
				//Internal size of a step, even numbers so a half is exact.
				void StepSize(int step, int& w, int& h) const;
				float StepScale(int step) const;
				//The last step whose scale is not under `scale` (0 for 1 or more).
				int StepFor(float scale) const;
				//The first step (the biggest picture) whose scale is not over `scale`.
				int StepAtMost(float scale) const;

				//Switches every registered texture to `step` (clearing them: nothing is kept across a change). Needs
				//Build() done. The size in use is Width() x Height() from then on.
				void Select(int step);
				int Step() const { return step; }
				int Width() const { return width; }
				int Height() const { return height; }
				//Counts the switches, for what remembers a view of a scaled texture.
				uint32_t Generation() const { return generation; }

				void Add(ScaledResource* resource);
				void Remove(ScaledResource* resource);

			private:
				std::recursive_mutex mutex;
				std::vector<ScaledResource*> resources;
				std::vector<std::pair<int, int>> sizes;
				int native_w = 0;
				int native_h = 0;
				int width = 0;
				int height = 0;
				int step = 0;
				bool built = false;
				uint32_t generation = 0;
			};

			//How a scaled texture is sized from the internal size of the frame: by default in proportion to its own size
			//at native resolution; give one when it is a fixed fraction of the frame (a half size effect, a pyramid).
			using ScaledSizeFn = std::function<void(int internal_w, int internal_h, int& w, int& h)>;

			class DepthTextureCube {
			private:
				GfxTexture* cube_texture = nullptr;
				GfxSRV* shader_resource_view = nullptr;
				GfxDSV* depth_stencil_view = nullptr;
				bool init = false;
			public:
				DepthTextureCube();
				DepthTextureCube(const DepthTextureCube& other);
				virtual ~DepthTextureCube();
				HRESULT Init(int w, int h);
				void Release();
				void Clear();
				GfxSRV* SRV() const;
				GfxDSV* Depth() const;
			};

			class DepthTexture2D : public ScaledResource {
			private:
				GfxTexture* texture = nullptr;
				GfxSRV* shader_resource_view = nullptr;
				GfxDSV* depth_stencil_view = nullptr;
				bool init = false;
				int width = 0;
				int height = 0;
				struct Variant {
					GfxTexture* texture = nullptr;
					GfxSRV* srv = nullptr;
					GfxDSV* dsv = nullptr;
					int w = 0;
					int h = 0;
				};
				bool scaled = false;
				int base_w = 0;
				int base_h = 0;
				ScaledSizeFn scaled_size;
				std::vector<Variant> variants;
				int active_step = 0;
				void SizeAt(int internal_w, int internal_h, int& w, int& h) const;
				void ReleaseVariants();
			public:
				DepthTexture2D();
				DepthTexture2D(const DepthTexture2D& other);
				virtual ~DepthTexture2D();
				HRESULT Init(int w, int h);
				//Init for a depth buffer that follows the render scale (RenderScale), in proportion to w x h unless `size` says.
				HRESULT InitScaled(int w, int h, ScaledSizeFn size = nullptr);
				bool IsScaled() const { return scaled; }
				HRESULT BuildSteps(const std::vector<std::pair<int, int>>& screen_sizes) override;
				void SelectStep(int step) override;
				void Release();
				void Clear();
				int Width() const;
				int Height() const;
				GfxSRV* SRV() const;
				GfxDSV* Depth() const;
			};

			//A depth texture with several slices, read in shaders as one Texture2DArray.
			//
			//This is what a directional light's shadow cascades live in: one SRV covering
			//every slice (so a pixel shader picks its cascade with a texture coordinate,
			//not a resource index - an array of Texture2Ds would cost one register per
			//cascade per light, and the ray tracers are already at the 128-register limit),
			//and one depth-stencil view covering every slice, which is what lets the shadow
			//geometry shader route a triangle to slice N via SV_RenderTargetArrayIndex and
			//fill all the cascades in a single pass. Depth(i) hands out a single-slice view
			//for anything that needs to render one cascade on its own.
			class DepthTexture2DArray {
			private:
				GfxTexture* texture = nullptr;
				GfxSRV* shader_resource_view = nullptr;
				GfxDSV* depth_stencil_view = nullptr;
				std::vector<GfxDSV*> slice_views;
				bool init = false;
				int width = 0;
				int height = 0;
				int slices = 0;
			public:
				DepthTexture2DArray();
				DepthTexture2DArray(const DepthTexture2DArray& other);
				virtual ~DepthTexture2DArray();
				HRESULT Init(int w, int h, int slices);
				void Release();
				void Clear();
				int Width() const;
				int Height() const;
				int Slices() const;
				GfxSRV* SRV() const;
				GfxDSV* Depth() const;
				GfxDSV* Depth(int slice) const;
			};


			class RenderTexture2D: public IRenderTarget, public ScaledResource {
			private:
				D3D12_RESOURCE_DESC tex_desc = {};
				GfxTexture* texture = nullptr;
				GfxSRV* shader_resource_view = nullptr;
				GfxRTV* render_view = nullptr;
				GfxUAV* uav = nullptr;

				int width = 0;
				int height = 0;
				int mip_levels = 0;
				bool init = false;

				//InitScaled: what it takes to make the texture again in another size, and the ones made.
				struct Variant {
					GfxTexture* texture = nullptr;
					GfxSRV* srv = nullptr;
					GfxRTV* rtv = nullptr;
					GfxUAV* uav = nullptr;
					int w = 0;
					int h = 0;
				};
				bool scaled = false;
				int base_w = 0;
				int base_h = 0;
				DXGI_FORMAT scaled_format = DXGI_FORMAT_UNKNOWN;
				uint32_t scaled_bind_flags = 0;
				ScaledSizeFn scaled_size;
				std::vector<Variant> variants;
				int active_step = 0;
				void SizeAt(int internal_w, int internal_h, int& w, int& h) const;
				void ReleaseVariants();
			public:
				RenderTexture2D(int mip_levels = 1);
				RenderTexture2D(const RenderTexture2D& other);
				virtual ~RenderTexture2D();
				int Width() const;
				int Height() const;
				//`bind_flags` is GFX_BIND_UNORDERED_ACCESS or 0: every render texture can be sampled and drawn to.
				HRESULT Init(int w, int h, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM, const uint8_t* data = nullptr, uint32_t len = 0, uint32_t bind_flags = 0);
				//Init for a texture that follows the render scale (see RenderScale); `size` null = in proportion to w x h.
				HRESULT InitScaled(int w, int h, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM, uint32_t bind_flags = 0, ScaledSizeFn size = nullptr);
				bool IsScaled() const { return scaled; }
				HRESULT BuildSteps(const std::vector<std::pair<int, int>>& screen_sizes) override;
				void SelectStep(int step) override;
				void Release();
				void Clear(const float color[4]);
				GfxResource* Resource() const;
				const D3D12_RESOURCE_DESC& Descriptor() const;
				GfxTexture* Texture() const;
				GfxSRV* SRV() const;
				GfxUAV* UAV() const;
				GfxRTV* RenderTarget() const override;
			};
		}
	}
}
