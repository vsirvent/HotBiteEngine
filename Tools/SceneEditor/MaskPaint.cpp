#include "MaskPaint.h"
#include "MultiMaterialPanel.h"
#include "EditorHistory.h"

#include "imgui.h"
#include <World.h>
#include <Core/DXCore.h>
#include <Core/Material.h>
#include <Systems/RenderSystem.h>

#include <DirectXTex.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

using namespace HotBite::Engine;

namespace HotBiteEditor {
	namespace MaskPaint {
		namespace {

			struct Session {
				std::string multi_material;
				int layer = -1;
				int width = 0;
				int height = 0;
				//RGBA8, row-major, top-left origin - the same layout SaveToWICFile and
				//LoadFromWICFile use, so committing never has to flip or swizzle.
				std::vector<uint8_t> pixels;
				ID3D11Texture2D* texture = nullptr;
				ID3D11ShaderResourceView* srv = nullptr;
				//Absolute path the canvas will be written to on Commit. Decided at Begin
				//(either the layer's existing mask or a generated name), not at Commit, so
				//the panel can show the user where a new mask is about to land.
				std::string file_path;
				//Which channel of the RGBA canvas this session paints - the layer's
				//mask_channel at Begin time. A stroke only ever touches this one, so a
				//shared splat map's other layers are untouched by painting this one.
				int channel = 0;
				//The layer's mask UV transform at Begin time. PaintStroke takes mesh UV -
				//what a viewport brush has - and the shader samples the mask at
				//`uv * scale + offset`, so a dab has to go through the same transform or a
				//terrain with a 1/24 mask scale would paint 24 times over in the wrong
				//places. Captured once rather than read per dab: changing the transform
				//mid-session would move strokes already laid down relative to later ones.
				float uv_scale = 1.0f;
				float uv_offset_u = 0.0f;
				float uv_offset_v = 0.0f;
				bool dirty = false;
			};

			Session session;
			bool active = false;

			//Editor-only brush state, so the panel remembers the last radius/strength
			//across frames without polluting EditorState with a tool that leaves no trace
			//once its session ends.
			float brush_radius = 0.05f;
			float brush_strength = 1.0f;

			void ReleaseGpu() {
				if (session.srv != nullptr) {
					session.srv->Release();
					session.srv = nullptr;
				}
				if (session.texture != nullptr) {
					session.texture->Release();
					session.texture = nullptr;
				}
			}

			//Pushes `pixels` to the GPU texture wholesale. Called once per stroke rather
			//than only touching the dabbed region - simpler, and a hand-painted mask is
			//edited by a person watching the viewport, not a hot loop, so the extra
			//bandwidth is not a cost worth avoiding here.
			void UploadTexture() {
				if (session.texture == nullptr) {
					return;
				}
				DXCore::Get()->context->UpdateSubresource(session.texture, 0, nullptr,
					session.pixels.data(), session.width * 4, 0);
			}

			bool CreateGpuTexture(std::string& error) {
				D3D11_TEXTURE2D_DESC desc = {};
				desc.Width = (UINT)session.width;
				desc.Height = (UINT)session.height;
				desc.MipLevels = 1;
				desc.ArraySize = 1;
				desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				desc.SampleDesc.Count = 1;
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

				D3D11_SUBRESOURCE_DATA init = {};
				init.pSysMem = session.pixels.data();
				init.SysMemPitch = (UINT)(session.width * 4);

				ID3D11Device* device = DXCore::Get()->device;
				HRESULT hr = device->CreateTexture2D(&desc, &init, &session.texture);
				if (FAILED(hr)) {
					error = "could not create the paint canvas texture";
					return false;
				}
				hr = device->CreateShaderResourceView(session.texture, nullptr, &session.srv);
				if (FAILED(hr)) {
					error = "could not create the paint canvas view";
					ReleaseGpu();
					return false;
				}
				return true;
			}

			//Loads `path` (already resolved to an absolute path) as RGBA8. False if the
			//file does not exist or is not an image WIC can read - callers fall back to a
			//blank canvas rather than failing Begin outright for a mask nobody made yet.
			bool LoadCanvas(const std::string& path, int& out_w, int& out_h,
				std::vector<uint8_t>& out_pixels) {
				std::error_code ec;
				if (!std::filesystem::exists(path, ec)) {
					return false;
				}
				std::wstring wpath(path.begin(), path.end());
				DirectX::ScratchImage loaded;
				DirectX::TexMetadata meta;
				if (FAILED(DirectX::LoadFromWICFile(wpath.c_str(), DirectX::WIC_FLAGS_FORCE_RGB,
					&meta, loaded))) {
					return false;
				}
				const DirectX::Image* src = loaded.GetImage(0, 0, 0);
				if (src == nullptr) {
					return false;
				}
				DirectX::ScratchImage converted;
				const DirectX::Image* rgba = src;
				if (src->format != DXGI_FORMAT_R8G8B8A8_UNORM) {
					if (FAILED(DirectX::Convert(*src, DXGI_FORMAT_R8G8B8A8_UNORM,
						DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, converted))) {
						return false;
					}
					rgba = converted.GetImage(0, 0, 0);
				}
				out_w = (int)rgba->width;
				out_h = (int)rgba->height;
				out_pixels.resize((size_t)out_w * out_h * 4);
				//DirectXTex rows can be padded (rowPitch >= width*4); copy row by row
				//rather than assuming a tightly packed buffer.
				for (int y = 0; y < out_h; ++y) {
					std::memcpy(out_pixels.data() + (size_t)y * out_w * 4,
						rgba->pixels + (size_t)y * rgba->rowPitch, (size_t)out_w * 4);
				}
				return true;
			}

			int ChannelOffset(int channel) {
				return (std::max)(0, (std::min)(3, channel));
			}
		}

		bool Begin(EditorState& state, const std::string& multi_material, int layer_index,
			std::string& error, int default_size) {
			if (active) {
				error = "a paint session is already open (" + session.multi_material +
					" layer " + std::to_string(session.layer) + ") - commit or cancel it first";
				return false;
			}
			if (state.world == nullptr) {
				error = "no scene loaded";
				return false;
			}
			Core::MultiMaterialData* mm = state.world->GetMultiMaterial(multi_material);
			if (mm == nullptr) {
				error = "multi-material not found: " + multi_material;
				return false;
			}
			if (layer_index < 0 || layer_index >= (int)mm->layers.size()) {
				error = "no layer " + std::to_string(layer_index) + " in " + multi_material;
				return false;
			}

			const Core::MultiMaterialLayer& layer = mm->layers[layer_index];
			const std::string root = state.world->GetAssetsPath();
			bool loaded = false;
			std::string existing_path;
			if (!layer.mask.empty()) {
				existing_path = root + "\\" + layer.mask;
				int w = 0, h = 0;
				std::vector<uint8_t> pixels;
				if (LoadCanvas(existing_path, w, h, pixels)) {
					session.width = w;
					session.height = h;
					session.pixels = std::move(pixels);
					session.file_path = existing_path;
					loaded = true;
				}
				else {
					error = "could not load existing mask image: " + existing_path;
					return false;
				}
			}
			if (!loaded) {
				session.width = default_size;
				session.height = default_size;
				//Blank means "no effect anywhere" - painting is additive from there, which
				//reads as "nothing shows until you paint it", the expected starting point
				//for a fresh splat map.
				session.pixels.assign((size_t)session.width * session.height * 4, 0);
				//A generated name under a masks/ folder next to the level's materials, so a
				//brand-new layer's first paint session has somewhere to land without the
				//user having to pick a path first. Collisions are avoided by folding in the
				//layer index - two layers on the same stack never share a canvas by default.
				//
				//Built with the same "root + \\ + subpath" concatenation Rebuild/SetTexture
				//use everywhere else for a root-relative path, rather than std::filesystem::
				//path's operator/ - `root` already carries a trailing separator (it is the
				//level's own asset path), and operator/ normalizes that away while this does
				//not, so a std::filesystem::path-built path and a string-built one silently
				//disagree by one backslash and Commit's prefix strip below never matches.
				const std::string masks_dir = root + "\\masks";
				std::error_code ec;
				std::filesystem::create_directories(masks_dir, ec);
				const std::string file_name = multi_material + "_layer" + std::to_string(layer_index) + "_mask.png";
				session.file_path = masks_dir + "\\" + file_name;
			}

			if (!CreateGpuTexture(error)) {
				session.pixels.clear();
				return false;
			}

			session.multi_material = multi_material;
			session.layer = layer_index;
			session.channel = ChannelOffset(layer.mask_channel);
			session.uv_scale = layer.mask_uv_scale;
			session.uv_offset_u = layer.mask_uv_offset.x;
			session.uv_offset_v = layer.mask_uv_offset.y;
			session.dirty = false;
			active = true;

			//RenderSystem::PrepareMultiMaterial reads MultiMaterialData::multi_texture_mask
			//from the render tick, which for the editor is not the thread commands run
			//on (see World::Run's background/physics ticks and every other mutator in
			//World.cpp taking this same mutex around a material edit) - SetLiveMask
			//writes that array in place, so it needs the same lock or the render tick can
			//observe a half-updated entry. Held for the stroke and the commit/cancel
			//swap too, for the same reason.
			std::lock_guard<std::recursive_mutex> lock(Systems::RenderSystem::mutex);
			mm->SetLiveMask((uint32_t)layer_index, session.srv);
			return true;
		}

		bool Active() {
			return active;
		}

		const std::string& CurrentMultiMaterial() {
			static const std::string empty;
			return active ? session.multi_material : empty;
		}

		int CurrentLayer() {
			return active ? session.layer : -1;
		}

		int Width() {
			return active ? session.width : 0;
		}

		int Height() {
			return active ? session.height : 0;
		}

		void PaintStroke(float u, float v, float radius, float strength) {
			if (!active || session.width <= 0 || session.height <= 0) {
				return;
			}
			//Mesh UV -> the mask image's own UV, the transform the shader samples through
			//(see MultiTexture.hlsli). Left at the identity for a mask with no transform,
			//so this is a no-op for everything authored before the transform existed.
			u = u * session.uv_scale + session.uv_offset_u;
			v = v * session.uv_scale + session.uv_offset_v;
			//The radius is in mesh UV units too - it is a distance on the surface, and the
			//caller measuring it in image UV would make the same brush cover a different
			//patch of ground for every layer sharing one splat map.
			radius *= std::fabs(session.uv_scale);
			//UV wraps the way a tiled texture would, so a brush near the seam of a
			//tiling mask paints across it continuously instead of stopping dead at 0/1.
			u -= std::floor(u);
			v -= std::floor(v);
			const float cx = u * (float)session.width;
			const float cy = v * (float)session.height;
			const float radius_px = (std::max)(1.0f, radius * (float)(std::min)(session.width, session.height));
			const float strength_amount = (std::max)(-1.0f, (std::min)(1.0f, strength)) * 255.0f;

			const int min_x = (int)std::floor(cx - radius_px);
			const int max_x = (int)std::ceil(cx + radius_px);
			const int min_y = (int)std::floor(cy - radius_px);
			const int max_y = (int)std::ceil(cy + radius_px);

			for (int y = min_y; y <= max_y; ++y) {
				for (int x = min_x; x <= max_x; ++x) {
					const float dx = ((float)x + 0.5f) - cx;
					const float dy = ((float)y + 0.5f) - cy;
					const float dist = std::sqrt(dx * dx + dy * dy);
					if (dist > radius_px) {
						continue;
					}
					const float falloff = 1.0f - dist / radius_px;
					//Wrap the pixel coordinates the same way the UV was wrapped above, so a
					//dab that spans the seam paints on both sides of it.
					int px = ((x % session.width) + session.width) % session.width;
					int py = ((y % session.height) + session.height) % session.height;
					const size_t pixel_index = ((size_t)py * session.width + px) * 4;
					uint8_t& value = session.pixels[pixel_index + session.channel];
					const float updated = (float)value + strength_amount * falloff;
					value = (uint8_t)(std::max)(0.0f, (std::min)(255.0f, updated));
				}
			}
			session.dirty = true;
			//UpdateSubresource writes into the very texture PrepareMultiMaterial may be
			//binding through the live SRV right now - see the lock comment in Begin().
			//The pixel edits above are on session.pixels, a buffer the render side never
			//touches, so only the upload itself needs the lock.
			{
				std::lock_guard<std::recursive_mutex> lock(Systems::RenderSystem::mutex);
				UploadTexture();
			}
		}

		bool Commit(EditorState& state, std::string& error) {
			if (!active) {
				error = "no paint session is open";
				return false;
			}
			if (session.dirty) {
				//COM (used by SaveToWICFile) needs to be initialized on this thread; the
				//editor already does this once for CaptureBackBuffer, and WIC tolerates a
				//second CoInitializeEx from the same thread (RPC_E_CHANGED_MODE just means
				//it already was).
				static bool com_initialized = false;
				if (!com_initialized) {
					CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
					com_initialized = true;
				}
				DirectX::Image image = {};
				image.width = (size_t)session.width;
				image.height = (size_t)session.height;
				image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
				image.rowPitch = (size_t)session.width * 4;
				image.slicePitch = image.rowPitch * (size_t)session.height;
				image.pixels = session.pixels.data();

				std::wstring wpath(session.file_path.begin(), session.file_path.end());
				HRESULT hr = DirectX::SaveToWICFile(image, DirectX::WIC_FLAGS_NONE,
					DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), wpath.c_str());
				if (FAILED(hr)) {
					error = "could not write mask image: " + session.file_path;
					return false;
				}
			}

			Core::MultiMaterialData* mm = (state.world != nullptr)
				? state.world->GetMultiMaterial(session.multi_material) : nullptr;
			if (mm != nullptr) {
				//Detach the live override *before* SetLayer's Rebuild runs, and before
				//ReleaseGpu below. Rebuild adopts whatever GetLiveMask(layer) returns
				//verbatim (that is what makes a stroke visible immediately without a
				//round trip through disk) - so left attached, it would re-adopt the very
				//texture ReleaseGpu is about to destroy, leaving multi_texture_mask[layer]
				//a dangling pointer that the next frame's PrepareMultiMaterial binds and
				//crashes on. The reload this triggers (of whatever file was there before
				//this commit) is immediately superseded by SetLayer's own Rebuild below.
				//
				//Locked for the same reason as every write in Begin()/PaintStroke(): the
				//render tick reads this same array through PrepareMultiMaterial.
				std::lock_guard<std::recursive_mutex> lock(Systems::RenderSystem::mutex);
				mm->SetLiveMask((uint32_t)session.layer, nullptr);
			}
			if (mm != nullptr && session.layer < (int)mm->layers.size()) {
				const std::string root = state.world->GetAssetsPath();
				const std::string prefix = root + "\\";
				std::string relative = session.file_path;
				if (relative.size() > prefix.size() &&
					relative.compare(0, prefix.size(), prefix) == 0) {
					relative = relative.substr(prefix.size());
				}
				MultiMaterialOps::Snapshot before;
				MultiMaterialOps::GetSnapshot(state, session.multi_material, before);
				//SetLayer both rebuilds the stack (which resolves the file we just wrote,
				//so the live preview keeps showing the identical result after the session
				//ends) and records the mask-path change as its own undo step - painting is
				//out of history, but *which file a layer points at* is scene-authoring data
				//like any other layer field.
				nlohmann::json fields;
				fields["mask"] = relative;
				MultiMaterialOps::SetLayer(state, session.multi_material, session.layer, fields, error);
			}

			ReleaseGpu();
			session = Session{};
			active = false;
			return true;
		}

		void Cancel(EditorState& state) {
			if (!active) {
				return;
			}
			Core::MultiMaterialData* mm = (state.world != nullptr)
				? state.world->GetMultiMaterial(session.multi_material) : nullptr;
			if (mm != nullptr) {
				std::lock_guard<std::recursive_mutex> lock(Systems::RenderSystem::mutex);
				mm->SetLiveMask((uint32_t)session.layer, nullptr);
			}
			ReleaseGpu();
			session = Session{};
			active = false;
		}

		void End(EditorState& state) {
			Cancel(state);
		}

		void DrawSection(EditorState& state, const std::string& multi_material, int layer_index) {
			ImGui::SeparatorText("Paint mask");
			const bool this_session = active && session.multi_material == multi_material &&
				session.layer == layer_index;

			if (!this_session) {
				ImGui::BeginDisabled(active);
				if (ImGui::Button("Paint...")) {
					std::string error;
					if (!Begin(state, multi_material, layer_index, error)) {
						state.status_message = "Paint mask failed: " + error;
					}
				}
				ImGui::EndDisabled();
				if (active) {
					ImGui::SameLine();
					ImGui::TextDisabled("(a session is open on %s layer %d - commit or cancel it first)",
						session.multi_material.c_str(), session.layer);
				}
				return;
			}

			ImGui::TextWrapped("Canvas %dx%d -> %s", session.width, session.height,
				session.file_path.c_str());
			ImGui::SliderFloat("Brush radius", &brush_radius, 0.005f, 0.5f, "%.3f");
			ImGui::SliderFloat("Brush strength", &brush_strength, -1.0f, 1.0f);
			ImGui::TextDisabled("Click-drag isn't wired to the viewport yet - paint by UV "
				"through the automation channel's paint_mask command, or drag the sliders "
				"below to dab the canvas center for a quick preview.");
			if (ImGui::Button("Dab center")) {
				PaintStroke(0.5f, 0.5f, brush_radius, brush_strength);
			}
			ImGui::SameLine();
			if (ImGui::Button("Commit")) {
				std::string error;
				if (!Commit(state, error)) {
					state.status_message = "Commit mask failed: " + error;
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) {
				Cancel(state);
			}
		}
	}
}
