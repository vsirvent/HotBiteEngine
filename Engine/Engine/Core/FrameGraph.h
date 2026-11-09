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

#include "Texture.h"

#include <functional>
#include <mutex>
#include <string>
#include <vector>

//The frame graph: the passes of a frame, the textures each of them reads and writes, and a pool the textures that only
//live within a frame take their memory from.
//
//A frame is described before it is recorded: RenderSystem adds its passes in the order they run (AddPass), each saying
//which frame textures it uses (Read/Write), Compile works out from that how long every texture lives (from the first
//pass that uses it to the last) and gives each one a texture of the pool, and Execute records the passes in order.
//Two textures of the same kind (size, format, flags) whose lives do not overlap get the same pool texture, so a frame
//costs the memory of the textures that are alive at the same time, not of all of them; a texture no pass of the frame
//uses costs nothing. The passes are recorded in the order they were added - the graph does not reorder them, and the
//barriers are still GfxContext's - so the order the engine was written in, and what it relies on, stays.
//
//Two kinds of frame texture (FrameTextureLifetime):
//  - transient: its contents last from its first pass to its last within a frame. Its memory is the pool's and is shared.
//    Every pass that touches it must declare it; one that does not is caught (see Validation) and the texture is taken
//    out of the sharing from the next frame on, so a missed declaration costs memory, not a broken picture for good.
//  - persistent: its contents last from frame to frame (a history), or something outside the graph reads it. A texture of
//    its own, made the first time it is used, given back once nothing has used it for TexturePool::IDLE_FRAMES frames
//    (the textures of a feature that is off - the ray tracing, the temporal filters - cost nothing). Needs no declaration.
//
//Validation: while a pass runs, a transient texture used outside the passes it lives through (its memory may then
//belong to another texture) is logged once, counted (FrameGraph::Stats::violations) and made persistent.
namespace HotBite {
	namespace Engine {
		namespace Core {
			class FrameGraph;
			class TexturePool;

			enum class FrameTextureLifetime {
				Transient,
				Persistent,
			};

			//What a frame texture is made as. Two transients with the same description can share a pool texture.
			struct FrameTextureDesc {
				//The size at the native resolution for a scaled texture (or its fixed size).
				int width = 0;
				int height = 0;
				DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
				//GFX_BIND_UNORDERED_ACCESS or 0 (every frame texture can be sampled and drawn into).
				uint32_t bind_flags = 0;
				//Follows the render scale (RenderScale): its size is the internal size of the frame, in proportion to width x
				//height (`divider` 0), or the internal size / divider (+1 when `plus_one`).
				bool scaled = false;
				int divider = 0;
				bool plus_one = false;
				bool operator==(const FrameTextureDesc& o) const {
					return width == o.width && height == o.height && format == o.format && bind_flags == o.bind_flags &&
						scaled == o.scaled && divider == o.divider && plus_one == o.plus_one;
				}
				bool operator!=(const FrameTextureDesc& o) const { return !(*this == o); }
			};

			//A texture of the frame, shaped like RenderTexture2D (SRV, UAV, RenderTarget, Width, Height, Clear) so the
			//passes use it the way they used one. The texture behind it is the pool's (transient) or its own (persistent),
			//and is made when a frame first needs it, not by Init.
			class FrameTexture : public IRenderTarget {
			public:
				FrameTexture() = default;
				FrameTexture(const FrameTexture&) = delete;
				FrameTexture& operator=(const FrameTexture&) = delete;
				virtual ~FrameTexture();

				//A texture of a fixed size. Only records what it is: no GPU memory is taken here.
				void Init(const char* name, int w, int h, DXGI_FORMAT format, uint32_t bind_flags = 0,
					FrameTextureLifetime lifetime = FrameTextureLifetime::Transient);
				//A texture that follows the render scale (dynamic resolution), w x h at the native size; `divider` and
				//`plus_one` as in FrameTextureDesc.
				void InitScaled(const char* name, int w, int h, DXGI_FORMAT format, uint32_t bind_flags = 0,
					FrameTextureLifetime lifetime = FrameTextureLifetime::Transient, int divider = 0, bool plus_one = false);
				//Forgets the description and gives back what it holds.
				void Release();
				bool Valid() const { return desc.format != DXGI_FORMAT_UNKNOWN; }

				void SetLifetime(FrameTextureLifetime lifetime);
				FrameTextureLifetime Lifetime() const { return lifetime; }
				//Persistent: what a texture holds when it is made (again). Zero by default.
				void SetInitialValue(const float color[4]);
				//Transient: the first pass of every frame writes all of it, so a clear asked for before the frame is
				//dropped (a post-process stage's texture, which the stage before it covers with a full screen draw).
				void SetFullyWritten(bool fully_written) { this->fully_written = fully_written; }

				//Whether a texture is behind it now.
				bool Allocated() const;
				int Width() const;
				int Height() const;
				GfxSRV* SRV() const;
				GfxUAV* UAV() const;
				GfxRTV* RenderTarget() const override;
				GfxTexture* Texture() const;
				GfxResource* Resource() const;
				//In a pass that uses it, clears it now. Before the frame (RenderSystem::Clear) a transient texture is cleared
				//when its first pass starts, and only if the frame uses it; a persistent one is cleared now if it is
				//allocated (one that is not starts from its initial value when it is made).
				void Clear(const float color[4]);

				const std::string& Name() const { return name; }
				const FrameTextureDesc& Desc() const { return desc; }
				//GPU memory one texture of this description takes (at the native size for a scaled one).
				uint64_t Bytes() const;

			private:
				friend class FrameGraph;
				friend class TexturePool;
				//The texture behind it for a use now (validating it, making one if it has to).
				RenderTexture2D* Use() const;
				RenderTexture2D* MakeOwn() const;
				void ReleaseOwn();

				std::string name;
				FrameTextureDesc desc;
				FrameTextureLifetime lifetime = FrameTextureLifetime::Transient;
				//Persistent (or a transient used where the graph had not given it a texture): a texture of its own.
				mutable RenderTexture2D* own = nullptr;
				//Transient: the pool texture the last compiled frame gave it.
				RenderTexture2D* bound = nullptr;
				//The graph that bound it, and the passes it lives through there.
				FrameGraph* bound_graph = nullptr;
				int first_pass = -1;
				int last_pass = -1;
				float initial_value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				bool fully_written = false;
				bool pending_clear = false;
				float pending_color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				//Validation already reported this texture.
				mutable bool reported = false;
				//Frame (GfxDevice::FrameSerial) it was last used in: a persistent texture idle long enough is given back.
				mutable uint64_t last_use = 0;
				mutable uint64_t bytes_cache = 0;
				//In the pool's registry.
				bool registered = false;
			};

			//The textures transient frame textures take their memory from, and the registry of every frame texture.
			//One for the process: the frames of every World draw from it, one after the other.
			class TexturePool {
			public:
				//A persistent texture nothing used for this many frames is given back; a pool texture no frame asked for
				//for as long is released.
				static constexpr uint64_t IDLE_FRAMES = 240;

				static TexturePool& Get();

				//The `index`-th pool texture of a description, made if there are not that many.
				RenderTexture2D* Acquire(const FrameTextureDesc& desc, uint32_t index);
				//Once a frame: releases what has been idle for IDLE_FRAMES.
				void Trim(uint64_t frame);
				//Releases every pool texture (a device going away); persistent textures keep theirs.
				void ReleaseAll();

				struct Stats {
					uint32_t pool_textures = 0;		//textures in the pool
					uint64_t pool_bytes = 0;
					uint32_t persistent_alive = 0;	//persistent frame textures with a texture behind them now
					uint64_t persistent_bytes = 0;
					uint32_t persistent_idle = 0;	//persistent frame textures with nothing behind them (a feature that is off)
					uint64_t persistent_idle_bytes = 0;
					uint32_t frame_textures = 0;		//frame textures that exist (Init done)
				};
				Stats GetStats();

				static uint64_t BytesOf(const FrameTextureDesc& desc);

			private:
				friend class FrameTexture;
				friend class FrameGraph;
				TexturePool() = default;
				void Register(FrameTexture* t);
				void Unregister(FrameTexture* t);

				struct Entry {
					FrameTextureDesc desc;
					uint32_t index = 0;
					RenderTexture2D* texture = nullptr;
					uint64_t last_use = 0;
					uint64_t bytes = 0;
				};
				std::recursive_mutex mutex;
				std::vector<Entry> entries;
				std::vector<FrameTexture*> textures;
			};

			class FrameGraph {
			public:
				enum PassFlags : uint32_t {
					PASS_NONE = 0,
					//The pass only makes transient textures for later passes: it is left out of a frame where no later pass
					//reads any of them.
					PASS_CULLABLE = 1,
				};

				//What a pass declares. Null textures (a stage with no frame texture of its own) are ignored, as are
				//persistent ones (they need no lifetime).
				class PassBuilder {
				public:
					PassBuilder(FrameGraph* graph, size_t pass) : graph(graph), pass(pass) {}
					PassBuilder& Read(FrameTexture* texture);
					PassBuilder& Write(FrameTexture* texture);
					PassBuilder& Read(FrameTexture& texture) { return Read(&texture); }
					PassBuilder& Write(FrameTexture& texture) { return Write(&texture); }
				private:
					FrameGraph* graph;
					size_t pass;
				};

				FrameGraph() = default;
				FrameGraph(const FrameGraph&) = delete;
				FrameGraph& operator=(const FrameGraph&) = delete;
				~FrameGraph();

				//Starts describing a frame (forgets the last one's passes).
				void Begin();
				PassBuilder AddPass(const char* name, std::function<void()> run, uint32_t flags = PASS_NONE);
				//Lifetimes, culling and the pool textures.
				void Compile();
				//Records the passes, in the order they were added.
				void Execute(GfxContext* context);

				//The graph whose passes are being recorded now, and the pass (-1 when none).
				static FrameGraph* Executing() { return executing; }
				int CurrentPass() const { return current_pass; }

				struct Stats {
					uint32_t passes = 0;
					uint32_t culled = 0;
					//Transient textures the frame used and the pool textures they were given.
					uint32_t transient_textures = 0;
					uint32_t physical_textures = 0;
					//What the transients would cost with a texture each, and what the pool textures they shared cost.
					uint64_t transient_bytes = 0;
					uint64_t physical_bytes = 0;
					//What they would cost if any two whose lives do not overlap could share memory whatever their kind
					//(placed in one heap): how much more heap aliasing could save.
					uint64_t aliased_bytes = 0;
					//Clears asked for before the frame: done at the first pass, and dropped (a texture the frame did not
					//use, or one its first pass covers).
					uint32_t clears_done = 0;
					uint32_t clears_dropped = 0;
					//Uses of a transient texture outside its passes, since the start (see Validation).
					uint32_t violations = 0;
				};
				const Stats& GetStats() const { return stats; }
				//One line per pass (what it reads and writes, culled or not) and per transient (its passes and pool texture).
				std::string Describe() const;

			private:
				friend class FrameTexture;
				struct Pass {
					std::string name;
					std::function<void()> run;
					uint32_t flags = PASS_NONE;
					std::vector<FrameTexture*> reads;
					std::vector<FrameTexture*> writes;
					bool culled = false;
				};
				struct Resource {
					FrameTexture* texture = nullptr;
					int first = -1;
					int last = -1;
					RenderTexture2D* physical = nullptr;
					uint32_t slot = 0;
				};
				void Declare(size_t pass, FrameTexture* texture, bool write);
				//A frame texture going away: no pass or resource of the last frame may keep pointing at it.
				void Forget(FrameTexture* texture);
				//A use of `texture` while a pass runs: false when it is outside the passes the texture lives through.
				void Validate(const FrameTexture* texture);
				void Unbind();

				std::vector<Pass> passes;
				std::vector<Resource> resources;
				Stats stats;
				uint32_t total_violations = 0;
				int current_pass = -1;
				bool compiled = false;
				//Per thread: a frame texture used by another thread while a frame is recorded is outside any pass.
				static thread_local FrameGraph* executing;
			};
		}
	}
}

using HotBite::Engine::Core::FrameGraph;
using HotBite::Engine::Core::FrameTexture;
using HotBite::Engine::Core::FrameTextureLifetime;
using HotBite::Engine::Core::TexturePool;
