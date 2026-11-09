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

#include "FrameGraph.h"
#include "GfxContext.h"
#include "Log.h"

#include <algorithm>

namespace HotBite {
	namespace Engine {
		namespace Core {
			namespace {
				uint64_t CurrentFrame() {
					GfxDevice* device = GfxDevice::Get();
					return device != nullptr ? device->FrameSerial() : 0;
				}

				ScaledSizeFn SizeFn(const FrameTextureDesc& desc) {
					if (!desc.scaled || desc.divider <= 0) {
						return nullptr;
					}
					const int d = desc.divider;
					if (desc.plus_one) {
						return [d](int iw, int ih, int& w, int& h) { w = iw / d + 1; h = ih / d + 1; };
					}
					return [d](int iw, int ih, int& w, int& h) { w = iw / d; h = ih / d; };
				}

				//A texture of the description, made now. Null if it could not be.
				RenderTexture2D* MakeTexture(const FrameTextureDesc& desc, const std::string& name) {
					RenderTexture2D* t = new RenderTexture2D();
					const HRESULT hr = desc.scaled
						? t->InitScaled(desc.width, desc.height, desc.format, desc.bind_flags, SizeFn(desc))
						: t->Init(desc.width, desc.height, desc.format, nullptr, 0, desc.bind_flags);
					if (FAILED(hr)) {
						LOG_ERROR("FrameGraph: the texture for '%s' (%dx%d, format %d) could not be made", name.c_str(), desc.width,
							desc.height, (int)desc.format);
						delete t;
						return nullptr;
					}
					return t;
				}

				//What a transient is matched to the pool by. Until the render scale is built every scaled texture is the size it
				//was given (RenderTexture2D::InitScaled), so it is the same kind as a fixed one of that size: the scene's
				//textures and the post-process chain's share the pool then.
				FrameTextureDesc PoolKey(const FrameTextureDesc& desc) {
					if (!desc.scaled || RenderScale::Get().Built()) {
						return desc;
					}
					FrameTextureDesc key = desc;
					key.scaled = false;
					key.divider = 0;
					key.plus_one = false;
					return key;
				}

				const char* LifetimeName(FrameTextureLifetime l) {
					return l == FrameTextureLifetime::Transient ? "transient" : "persistent";
				}
			}

			thread_local FrameGraph* FrameGraph::executing = nullptr;

			//-------------------------------------------------------------------------------------------
			// FrameTexture
			//-------------------------------------------------------------------------------------------

			FrameTexture::~FrameTexture() {
				Release();
			}

			void FrameTexture::Init(const char* texture_name, int w, int h, DXGI_FORMAT format, uint32_t bind_flags,
				FrameTextureLifetime texture_lifetime) {
				FrameTextureDesc d;
				d.width = w;
				d.height = h;
				d.format = format;
				d.bind_flags = bind_flags;
				//Same texture again (a resize back, a reload): what is behind it stays.
				if (Valid() && d == desc && name == texture_name && lifetime == texture_lifetime) {
					return;
				}
				Release();
				name = texture_name;
				desc = d;
				lifetime = texture_lifetime;
				TexturePool::Get().Register(this);
			}

			void FrameTexture::InitScaled(const char* texture_name, int w, int h, DXGI_FORMAT format, uint32_t bind_flags,
				FrameTextureLifetime texture_lifetime, int divider, bool plus_one) {
				FrameTextureDesc d;
				d.width = w;
				d.height = h;
				d.format = format;
				d.bind_flags = bind_flags;
				d.scaled = true;
				//A divider of 1 is the frame's own size, which is what a texture in proportion to the native size is.
				d.divider = (divider == 1 && !plus_one) ? 0 : divider;
				d.plus_one = plus_one;
				if (Valid() && d == desc && name == texture_name && lifetime == texture_lifetime) {
					return;
				}
				Release();
				name = texture_name;
				desc = d;
				lifetime = texture_lifetime;
				TexturePool::Get().Register(this);
			}

			void FrameTexture::Release() {
				ReleaseOwn();
				//The graph that listed it forgets it (it is alive: a graph going away unbinds what it bound first).
				if (bound_graph != nullptr) {
					bound_graph->Forget(this);
				}
				bound = nullptr;
				bound_graph = nullptr;
				first_pass = last_pass = -1;
				pending_clear = false;
				bytes_cache = 0;
				if (registered) {
					TexturePool::Get().Unregister(this);
				}
				desc = FrameTextureDesc();
			}

			void FrameTexture::ReleaseOwn() {
				if (own != nullptr) {
					delete own;
					own = nullptr;
				}
			}

			void FrameTexture::SetLifetime(FrameTextureLifetime l) {
				lifetime = l;
			}

			void FrameTexture::SetInitialValue(const float color[4]) {
				for (int i = 0; i < 4; ++i) {
					initial_value[i] = color[i];
				}
			}

			RenderTexture2D* FrameTexture::MakeOwn() const {
				if (own == nullptr && Valid()) {
					own = MakeTexture(desc, name);
					//A committed texture starts zeroed, which is the initial value unless one was given. A clear is recorded
					//only for another one: those are the history textures, made by their own pass on the render thread, and
					//a texture something asks for from another thread (a stage built at load) must not touch the context.
					const bool zero = initial_value[0] == 0.0f && initial_value[1] == 0.0f && initial_value[2] == 0.0f &&
						initial_value[3] == 0.0f;
					if (own != nullptr && !zero) {
						own->Clear(initial_value);
					}
				}
				return own;
			}

			RenderTexture2D* FrameTexture::Use() const {
				if (!Valid()) {
					return nullptr;
				}
				if (lifetime == FrameTextureLifetime::Persistent) {
					last_use = CurrentFrame();
					return own != nullptr ? own : MakeOwn();
				}
				FrameGraph* graph = FrameGraph::Executing();
				if (bound != nullptr) {
					if (graph != nullptr && graph == bound_graph) {
						graph->Validate(this);
					}
					return bound;
				}
				if (graph != nullptr && graph->CurrentPass() >= 0 && first_pass < 0) {
					//A pass of the frame being recorded uses a transient that no pass of it declared.
					FrameTexture* self = const_cast<FrameTexture*>(this);
					++graph->total_violations;
					if (!reported) {
						reported = true;
						LOG_WARN("FrameGraph: pass '%s' uses '%s', which no pass of the frame declared; it is persistent from now on",
							graph->passes[graph->current_pass].name.c_str(), name.c_str());
					}
					self->lifetime = FrameTextureLifetime::Persistent;
				}
				//Outside any frame, before a frame gave it a texture: one of its own until one does.
				last_use = CurrentFrame();
				return own != nullptr ? own : MakeOwn();
			}

			bool FrameTexture::Allocated() const {
				return own != nullptr || bound != nullptr;
			}

			int FrameTexture::Width() const {
				const RenderTexture2D* t = (lifetime == FrameTextureLifetime::Transient && bound != nullptr) ? bound : own;
				if (t != nullptr) {
					return t->Width();
				}
				const RenderScale& rs = RenderScale::Get();
				if (!desc.scaled || !rs.Built()) {
					return desc.width;
				}
				if (desc.divider > 0) {
					return (std::max)(rs.Width() / desc.divider + (desc.plus_one ? 1 : 0), 1);
				}
				return (std::max)((int)((float)desc.width * (float)rs.Width() / (float)(std::max)(rs.NativeWidth(), 1) + 0.5f), 1);
			}

			int FrameTexture::Height() const {
				const RenderTexture2D* t = (lifetime == FrameTextureLifetime::Transient && bound != nullptr) ? bound : own;
				if (t != nullptr) {
					return t->Height();
				}
				const RenderScale& rs = RenderScale::Get();
				if (!desc.scaled || !rs.Built()) {
					return desc.height;
				}
				if (desc.divider > 0) {
					return (std::max)(rs.Height() / desc.divider + (desc.plus_one ? 1 : 0), 1);
				}
				return (std::max)((int)((float)desc.height * (float)rs.Height() / (float)(std::max)(rs.NativeHeight(), 1) + 0.5f), 1);
			}

			GfxSRV* FrameTexture::SRV() const {
				RenderTexture2D* t = Use();
				return t != nullptr ? t->SRV() : nullptr;
			}

			GfxUAV* FrameTexture::UAV() const {
				RenderTexture2D* t = Use();
				return t != nullptr ? t->UAV() : nullptr;
			}

			GfxRTV* FrameTexture::RenderTarget() const {
				RenderTexture2D* t = Use();
				return t != nullptr ? t->RenderTarget() : nullptr;
			}

			GfxTexture* FrameTexture::Texture() const {
				RenderTexture2D* t = Use();
				return t != nullptr ? t->Texture() : nullptr;
			}

			GfxResource* FrameTexture::Resource() const {
				RenderTexture2D* t = Use();
				return t != nullptr ? t->Resource() : nullptr;
			}

			void FrameTexture::Clear(const float color[4]) {
				if (!Valid()) {
					return;
				}
				if (lifetime == FrameTextureLifetime::Persistent) {
					//One that is not there starts from its initial value when it is made.
					if (own != nullptr) {
						own->Clear(color);
					}
					return;
				}
				FrameGraph* graph = FrameGraph::Executing();
				if (graph != nullptr && graph->CurrentPass() >= 0) {
					RenderTexture2D* t = Use();
					if (t != nullptr) {
						t->Clear(color);
					}
					return;
				}
				//Before the frame: when its first pass starts, if the frame uses it.
				pending_clear = true;
				for (int i = 0; i < 4; ++i) {
					pending_color[i] = color[i];
				}
			}

			uint64_t FrameTexture::Bytes() const {
				if (!Valid()) {
					return 0;
				}
				if (bytes_cache == 0) {
					bytes_cache = TexturePool::BytesOf(desc);
				}
				return bytes_cache;
			}

			//-------------------------------------------------------------------------------------------
			// TexturePool
			//-------------------------------------------------------------------------------------------

			TexturePool& TexturePool::Get() {
				//Never destroyed, like RenderScale: textures that outlive the static destructors still unregister.
				static TexturePool* instance = new TexturePool();
				return *instance;
			}

			uint64_t TexturePool::BytesOf(const FrameTextureDesc& desc) {
				GfxDevice* device = GfxDevice::Get();
				if (device == nullptr || desc.format == DXGI_FORMAT_UNKNOWN) {
					return 0;
				}
				D3D12_RESOURCE_DESC d = {};
				d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
				d.Width = (UINT64)(std::max)(desc.width, 1);
				d.Height = (UINT)(std::max)(desc.height, 1);
				d.DepthOrArraySize = 1;
				d.MipLevels = 1;
				d.Format = desc.format;
				d.SampleDesc.Count = 1;
				d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
				d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
				if (desc.bind_flags & GFX_BIND_UNORDERED_ACCESS) {
					d.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
				}
				return device->Device()->GetResourceAllocationInfo(0, 1, &d).SizeInBytes;
			}

			void TexturePool::Register(FrameTexture* t) {
				std::lock_guard<std::recursive_mutex> lock(mutex);
				if (!t->registered) {
					textures.push_back(t);
					t->registered = true;
				}
			}

			void TexturePool::Unregister(FrameTexture* t) {
				std::lock_guard<std::recursive_mutex> lock(mutex);
				textures.erase(std::remove(textures.begin(), textures.end(), t), textures.end());
				t->registered = false;
			}

			RenderTexture2D* TexturePool::Acquire(const FrameTextureDesc& desc, uint32_t index) {
				std::lock_guard<std::recursive_mutex> lock(mutex);
				const uint64_t frame = CurrentFrame();
				for (Entry& e : entries) {
					if (e.index == index && e.desc == desc) {
						e.last_use = frame;
						return e.texture;
					}
				}
				Entry e;
				e.desc = desc;
				e.index = index;
				e.texture = MakeTexture(desc, "pool");
				if (e.texture == nullptr) {
					return nullptr;
				}
				e.last_use = frame;
				e.bytes = BytesOf(desc);
				entries.push_back(e);
				return e.texture;
			}

			void TexturePool::Trim(uint64_t frame) {
				std::lock_guard<std::recursive_mutex> lock(mutex);
				for (size_t i = 0; i < entries.size();) {
					Entry& e = entries[i];
					if (e.last_use + IDLE_FRAMES < frame) {
						//Nobody may keep pointing at it.
						for (FrameTexture* t : textures) {
							if (t->bound == e.texture) {
								t->bound = nullptr;
							}
						}
						delete e.texture;
						entries.erase(entries.begin() + i);
						continue;
					}
					++i;
				}
				for (FrameTexture* t : textures) {
					if (t->own != nullptr && t->last_use + IDLE_FRAMES < frame) {
						t->ReleaseOwn();
					}
				}
			}

			void TexturePool::ReleaseAll() {
				std::lock_guard<std::recursive_mutex> lock(mutex);
				for (FrameTexture* t : textures) {
					t->bound = nullptr;
				}
				for (Entry& e : entries) {
					delete e.texture;
				}
				entries.clear();
			}

			TexturePool::Stats TexturePool::GetStats() {
				std::lock_guard<std::recursive_mutex> lock(mutex);
				Stats s;
				for (const Entry& e : entries) {
					++s.pool_textures;
					s.pool_bytes += e.bytes;
				}
				for (const FrameTexture* t : textures) {
					if (!t->Valid()) {
						continue;
					}
					++s.frame_textures;
					if (t->lifetime == FrameTextureLifetime::Persistent) {
						if (t->own != nullptr) {
							++s.persistent_alive;
							s.persistent_bytes += t->Bytes();
						}
						else {
							++s.persistent_idle;
							s.persistent_idle_bytes += t->Bytes();
						}
					}
				}
				return s;
			}

			//-------------------------------------------------------------------------------------------
			// FrameGraph
			//-------------------------------------------------------------------------------------------

			FrameGraph::~FrameGraph() {
				Unbind();
				passes.clear();
				resources.clear();
			}

			void FrameGraph::Unbind() {
				TexturePool& pool = TexturePool::Get();
				std::lock_guard<std::recursive_mutex> lock(pool.mutex);
				for (FrameTexture* t : pool.textures) {
					if (t->bound_graph == this) {
						t->bound = nullptr;
						t->bound_graph = nullptr;
						t->first_pass = t->last_pass = -1;
					}
				}
				//What the passes recorded may point at a texture that is going away.
				for (Pass& p : passes) {
					p.reads.clear();
					p.writes.clear();
				}
				resources.clear();
				compiled = false;
			}

			void FrameGraph::Forget(FrameTexture* texture) {
				for (Pass& p : passes) {
					p.reads.erase(std::remove(p.reads.begin(), p.reads.end(), texture), p.reads.end());
					p.writes.erase(std::remove(p.writes.begin(), p.writes.end(), texture), p.writes.end());
				}
				resources.erase(std::remove_if(resources.begin(), resources.end(), [texture](const Resource& r) { return r.texture == texture; }),
					resources.end());
			}

			void FrameGraph::Begin() {
				passes.clear();
				resources.clear();
				compiled = false;
				current_pass = -1;
			}

			FrameGraph::PassBuilder FrameGraph::AddPass(const char* name, std::function<void()> run, uint32_t flags) {
				Pass p;
				p.name = name;
				p.run = std::move(run);
				p.flags = flags;
				passes.push_back(std::move(p));
				return PassBuilder(this, passes.size() - 1);
			}

			FrameGraph::PassBuilder& FrameGraph::PassBuilder::Read(FrameTexture* texture) {
				graph->Declare(pass, texture, false);
				return *this;
			}

			FrameGraph::PassBuilder& FrameGraph::PassBuilder::Write(FrameTexture* texture) {
				graph->Declare(pass, texture, true);
				return *this;
			}

			void FrameGraph::Declare(size_t pass, FrameTexture* texture, bool write) {
				if (texture == nullptr || !texture->Valid() || pass >= passes.size()) {
					return;
				}
				std::vector<FrameTexture*>& list = write ? passes[pass].writes : passes[pass].reads;
				if (std::find(list.begin(), list.end(), texture) == list.end()) {
					list.push_back(texture);
				}
			}

			void FrameGraph::Compile() {
				stats = Stats();
				stats.passes = (uint32_t)passes.size();
				stats.violations = total_violations;

				//Culling, from the last pass back: a cullable pass stays only if a pass that stays after it reads what it writes.
				std::vector<const FrameTexture*> needed;
				for (size_t i = passes.size(); i-- > 0;) {
					Pass& p = passes[i];
					p.culled = false;
					if (p.flags & PASS_CULLABLE) {
						bool keep = false;
						for (const FrameTexture* w : p.writes) {
							if (w->lifetime == FrameTextureLifetime::Persistent ||
								std::find(needed.begin(), needed.end(), w) != needed.end()) {
								keep = true;
								break;
							}
						}
						if (!keep) {
							p.culled = true;
							++stats.culled;
							continue;
						}
					}
					for (const FrameTexture* r : p.reads) {
						needed.push_back(r);
					}
				}

				//Lifetimes of the transients, over the passes that stay.
				TexturePool& pool = TexturePool::Get();
				std::lock_guard<std::recursive_mutex> lock(pool.mutex);
				for (FrameTexture* t : pool.textures) {
					if (t->bound_graph == this) {
						t->bound = nullptr;
						t->bound_graph = nullptr;
						t->first_pass = t->last_pass = -1;
					}
				}
				resources.clear();
				const auto touch = [&](FrameTexture* t, int pass) {
					if (t->lifetime != FrameTextureLifetime::Transient) {
						return;
					}
					for (Resource& r : resources) {
						if (r.texture == t) {
							r.first = (std::min)(r.first, pass);
							r.last = (std::max)(r.last, pass);
							return;
						}
					}
					Resource r;
					r.texture = t;
					r.first = r.last = pass;
					resources.push_back(r);
				};
				for (size_t i = 0; i < passes.size(); ++i) {
					if (passes[i].culled) {
						continue;
					}
					for (FrameTexture* t : passes[i].reads) {
						touch(t, (int)i);
					}
					for (FrameTexture* t : passes[i].writes) {
						touch(t, (int)i);
					}
				}

				//Pool textures: in the order the textures start, each takes the first texture of its kind whose last user is
				//done (the lowest one, so a frame like the last gets the same ones).
				std::vector<size_t> order(resources.size());
				for (size_t i = 0; i < order.size(); ++i) {
					order[i] = i;
				}
				std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return resources[a].first < resources[b].first; });
				struct Slots {
					FrameTextureDesc desc;
					std::vector<int> busy_until;
					uint64_t bytes;
				};
				std::vector<Slots> kinds;
				for (size_t oi : order) {
					Resource& r = resources[oi];
					const FrameTextureDesc key = PoolKey(r.texture->desc);
					Slots* k = nullptr;
					for (Slots& s : kinds) {
						if (s.desc == key) {
							k = &s;
							break;
						}
					}
					if (k == nullptr) {
						kinds.push_back(Slots{ key, {}, r.texture->Bytes() });
						k = &kinds.back();
					}
					uint32_t slot = (uint32_t)k->busy_until.size();
					for (uint32_t s = 0; s < k->busy_until.size(); ++s) {
						if (k->busy_until[s] < r.first) {
							slot = s;
							break;
						}
					}
					if (slot == k->busy_until.size()) {
						k->busy_until.push_back(r.last);
					}
					else {
						k->busy_until[slot] = r.last;
					}
					r.slot = slot;
					r.physical = pool.Acquire(key, slot);
					r.texture->bound = r.physical;
					r.texture->bound_graph = this;
					r.texture->first_pass = r.first;
					r.texture->last_pass = r.last;
				}

				//What it costs.
				struct Interval {
					uint64_t bytes;
					int first;
					int last;
					uint64_t offset;
				};
				std::vector<Interval> intervals;
				for (const Slots& k : kinds) {
					stats.physical_textures += (uint32_t)k.busy_until.size();
					stats.physical_bytes += k.bytes * k.busy_until.size();
				}
				for (const Resource& r : resources) {
					const uint64_t bytes = r.texture->Bytes();
					++stats.transient_textures;
					stats.transient_bytes += bytes;
					intervals.push_back(Interval{ bytes, r.first, r.last, 0 });
				}
				//The heap that could hold them all if memory were shared whatever the kind: biggest first, each at the lowest
				//offset clear of every placed texture whose life overlaps its own.
				std::sort(intervals.begin(), intervals.end(), [](const Interval& a, const Interval& b) { return a.bytes > b.bytes; });
				std::vector<Interval> placed;
				for (Interval& iv : intervals) {
					uint64_t offset = 0;
					bool moved = true;
					while (moved) {
						moved = false;
						for (const Interval& p : placed) {
							const bool time = iv.first <= p.last && p.first <= iv.last;
							const bool space = offset < p.offset + p.bytes && p.offset < offset + iv.bytes;
							if (time && space) {
								offset = p.offset + p.bytes;
								moved = true;
							}
						}
					}
					iv.offset = offset;
					placed.push_back(iv);
					stats.aliased_bytes = (std::max)(stats.aliased_bytes, offset + iv.bytes);
				}
				compiled = true;
			}

			void FrameGraph::Validate(const FrameTexture* texture) {
				if (current_pass < 0) {
					return;
				}
				if (current_pass >= texture->first_pass && current_pass <= texture->last_pass) {
					return;
				}
				FrameTexture* t = const_cast<FrameTexture*>(texture);
				++total_violations;
				if (!t->reported) {
					t->reported = true;
					LOG_WARN("FrameGraph: pass '%s' uses '%s' outside the passes it lives through (%d..%d); it is persistent from now on",
						passes[current_pass].name.c_str(), t->name.c_str(), t->first_pass, t->last_pass);
				}
				//From the next frame on: this one goes on with the texture it was given.
				t->lifetime = FrameTextureLifetime::Persistent;
			}

			void FrameGraph::Execute(GfxContext* context) {
				if (!compiled) {
					Compile();
				}
				//Each pass's transients that start there, for their clears.
				std::vector<std::vector<FrameTexture*>> starts(passes.size());
				for (const Resource& r : resources) {
					if (r.first >= 0 && r.first < (int)passes.size()) {
						starts[r.first].push_back(r.texture);
					}
				}
				FrameGraph* outer = executing;
				executing = this;
				for (size_t i = 0; i < passes.size(); ++i) {
					Pass& p = passes[i];
					if (p.culled) {
						continue;
					}
					current_pass = (int)i;
					for (FrameTexture* t : starts[i]) {
						if (!t->pending_clear) {
							continue;
						}
						t->pending_clear = false;
						if (t->fully_written || t->bound == nullptr) {
							++stats.clears_dropped;
						}
						else {
							t->bound->Clear(t->pending_color);
							++stats.clears_done;
						}
					}
					if (context != nullptr) {
						context->Marker(p.name.c_str());
					}
					if (p.run) {
						p.run();
					}
				}
				current_pass = -1;
				executing = outer;
				stats.violations = total_violations;

				//Clears asked for textures this frame did not use.
				TexturePool& pool = TexturePool::Get();
				{
					std::lock_guard<std::recursive_mutex> lock(pool.mutex);
					for (FrameTexture* t : pool.textures) {
						if (t->pending_clear && t->lifetime == FrameTextureLifetime::Transient) {
							t->pending_clear = false;
							++stats.clears_dropped;
						}
					}
				}
				pool.Trim(CurrentFrame());
			}

			std::string FrameGraph::Describe() const {
				std::string out;
				char line[512];
				for (size_t i = 0; i < passes.size(); ++i) {
					const Pass& p = passes[i];
					snprintf(line, sizeof(line), "pass %zu %s%s:", i, p.name.c_str(), p.culled ? " (culled)" : "");
					out += line;
					for (const FrameTexture* t : p.reads) {
						out += " r:" + t->name;
					}
					for (const FrameTexture* t : p.writes) {
						out += " w:" + t->name;
					}
					out += "\n";
				}
				for (const Resource& r : resources) {
					snprintf(line, sizeof(line), "texture %s %s passes %d..%d slot %u %dx%d fmt %d %.1f MB\n", r.texture->name.c_str(),
						LifetimeName(r.texture->lifetime), r.first, r.last, r.slot, r.texture->Width(), r.texture->Height(),
						(int)r.texture->desc.format, (double)r.texture->Bytes() / (1024.0 * 1024.0));
					out += line;
				}
				return out;
			}
		}
	}
}
