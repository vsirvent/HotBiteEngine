#include "TexturePanel.h"
#include "MaterialPanel.h"
#include "EditorLayout.h"

#include "imgui.h"
#include <World.h>
#include <Core/Material.h>

#include <Windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <set>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

using namespace HotBite::Engine;
namespace fs = std::filesystem;

namespace HotBiteEditor {
	namespace TextureOps {
		namespace {

			//Thumbnails hold a reference on the engine's texture cache entry, so a texture
			//the panel is showing is the very SRV a material using it would share. A null
			//entry means "tried and failed", so a broken file is not retried every frame.
			std::map<std::string, ID3D11ShaderResourceView*> thumbnails;

			std::string Lower(std::string s) {
				std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
				return s;
			}

			//Paths on Windows compare without case; the same file can be spelled two ways
			//between a .mat and the folder scan.
			bool SameTexture(const std::string& a, const std::string& b) {
				return !a.empty() && Lower(a) == Lower(b);
			}

			bool ValidRelative(const std::string& rel, std::string& error) {
				const fs::path p(rel);
				if (p.has_root_name() || p.has_root_directory()) {
					error = "the folder must be relative to Assets/Textures";
					return false;
				}
				for (const fs::path& part : p) {
					if (part == "..") {
						error = "the folder cannot leave Assets/Textures";
						return false;
					}
				}
				return true;
			}

			void DropThumbnail(const std::string& texture) {
				auto it = thumbnails.find(texture);
				if (it != thumbnails.end()) {
					if (it->second != nullptr) {
						Core::ReleaseTexture(it->second);
					}
					thumbnails.erase(it);
				}
			}

			//Deletes now-empty folders from `dir` upwards, stopping at Assets/Textures.
			void PruneEmptyParents(const EditorState& state, fs::path dir) {
				const fs::path root = fs::path(MaterialOps::TexturesDir(state)).lexically_normal();
				std::error_code ec;
				while (!dir.empty() && dir.lexically_normal() != root && fs::is_empty(dir, ec) && !ec) {
					fs::remove(dir, ec);
					dir = dir.parent_path();
				}
			}
		}

		std::vector<std::string> ListFolders(const EditorState& state) {
			std::vector<std::string> out;
			const std::string dir = MaterialOps::TexturesDir(state);
			std::error_code ec;
			if (dir.empty() || !fs::is_directory(dir, ec)) {
				return out;
			}
			for (auto it = fs::recursive_directory_iterator(dir, ec);
				!ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
				if (it->is_directory()) {
					out.push_back(fs::relative(it->path(), dir, ec).make_preferred().string());
				}
			}
			std::sort(out.begin(), out.end());
			return out;
		}

		std::vector<std::string> FindUsers(EditorState& state, const std::string& texture) {
			std::vector<std::string> users;
			if (state.world == nullptr) {
				return users;
			}
			for (const std::string& name : MaterialOps::ListMaterials(state)) {
				Core::MaterialData* m = state.world->GetMaterials().Get(name);
				if (m == nullptr) {
					continue;
				}
				const Core::MaterialTextures& t = m->texture_names;
				const std::pair<const char*, const std::string*> slots[] = {
					{ "diffuse", &t.diffuse_texname }, { "normal", &t.normal_textname },
					{ "height", &t.high_textname }, { "specular", &t.spec_textname },
					{ "ao", &t.ao_textname }, { "arm", &t.arm_textname },
					{ "emission", &t.emission_textname }, { "opacity", &t.opacity_textname } };
				for (const auto& slot : slots) {
					if (SameTexture(MaterialOps::TextureRelativeName(state, *slot.second), texture)) {
						users.push_back(name + " (" + slot.first + ")");
					}
				}
			}
			//A multi-material layer's mask is relative to the assets path, not to the
			//textures folder, so it is resolved to a full path first.
			for (const std::string& name : state.world->ListMultiMaterials()) {
				Core::MultiMaterialData* mm = state.world->GetMultiMaterial(name);
				if (mm == nullptr) {
					continue;
				}
				for (size_t i = 0; i < mm->layers.size(); ++i) {
					if (mm->layers[i].mask.empty()) {
						continue;
					}
					const std::string full = (fs::path(state.world->GetAssetsPath()) / mm->layers[i].mask)
						.lexically_normal().make_preferred().string();
					if (SameTexture(MaterialOps::TextureRelativeName(state, full), texture)) {
						users.push_back(name + " layer " + std::to_string(i + 1) + " mask");
					}
				}
			}
			return users;
		}

		bool ImportFiles(EditorState& state, const std::vector<std::string>& files,
			const std::string& subfolder, int& imported, int& skipped, std::string& error) {
			imported = 0;
			skipped = 0;
			if (MaterialOps::TexturesDir(state).empty()) {
				error = "no project open - textures are imported into <project>/Assets/Textures";
				return false;
			}
			if (!ValidRelative(subfolder, error)) {
				return false;
			}
			std::string last_error;
			for (const std::string& file : files) {
				std::string out, one_error;
				if (MaterialOps::ImportTexture(state, file, subfolder, out, one_error)) {
					++imported;
				}
				else {
					++skipped;
					last_error = one_error;
				}
			}
			if (imported == 0 && skipped > 0) {
				error = last_error;
				return false;
			}
			state.status_message = "Imported " + std::to_string(imported) + " texture(s)" +
				(skipped > 0 ? ", skipped " + std::to_string(skipped) : std::string());
			return true;
		}

		bool ImportFolder(EditorState& state, const std::string& folder,
			const std::string& subfolder, int& imported, int& skipped, std::string& error) {
			imported = 0;
			skipped = 0;
			std::error_code ec;
			if (!fs::is_directory(folder, ec)) {
				error = "folder not found: " + folder;
				return false;
			}
			std::vector<std::string> files;
			std::vector<std::string> subs;
			for (auto it = fs::recursive_directory_iterator(folder, ec);
				!ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
				if (!it->is_regular_file()) {
					continue;
				}
				//The set's own structure survives: <subfolder>\<path inside the folder>.
				const fs::path inside = fs::relative(it->path(), folder, ec).parent_path();
				files.push_back(it->path().string());
				subs.push_back((fs::path(subfolder) / inside).make_preferred().string());
			}
			if (!ValidRelative(subfolder, error)) {
				return false;
			}
			for (size_t i = 0; i < files.size(); ++i) {
				std::string out, one_error;
				//Only images count as "skipped": a set folder routinely has a readme or a
				//Thumbs.db, and reporting those as failures is noise.
				if (MaterialOps::ImportTexture(state, files[i], subs[i], out, one_error)) {
					++imported;
				}
				else {
					++skipped;
				}
			}
			if (imported == 0) {
				error = "no images found in " + folder;
				return false;
			}
			state.status_message = "Imported " + std::to_string(imported) + " texture(s) from " + folder;
			return true;
		}

		bool CreateFolder(EditorState& state, const std::string& folder, std::string& error) {
			const std::string root = MaterialOps::TexturesDir(state);
			if (root.empty()) {
				error = "no project open";
				return false;
			}
			if (folder.empty()) {
				error = "folder name is empty";
				return false;
			}
			if (!ValidRelative(folder, error)) {
				return false;
			}
			std::error_code ec;
			fs::create_directories(fs::path(root) / folder, ec);
			if (ec) {
				error = "could not create " + folder + ": " + ec.message();
				return false;
			}
			return true;
		}

		bool RemoveTexture(EditorState& state, const std::string& texture, std::string& error) {
			const std::vector<std::string>& all = MaterialOps::ListTextures(state);
			if (std::find(all.begin(), all.end(), texture) == all.end()) {
				error = "not an imported texture: " + texture;
				return false;
			}
			const std::vector<std::string> users = FindUsers(state, texture);
			if (!users.empty()) {
				error = "'" + texture + "' is in use by " + users.front() +
					(users.size() > 1 ? " and " + std::to_string(users.size() - 1) + " more" : std::string()) +
					" - point those at another texture first";
				return false;
			}
			const std::string abs = MaterialOps::TextureAbsolutePath(state, texture);
			DropThumbnail(texture);
			std::error_code ec;
			fs::remove(abs, ec);
			if (ec) {
				error = "could not delete " + abs + ": " + ec.message();
				return false;
			}
			PruneEmptyParents(state, fs::path(abs).parent_path());
			MaterialOps::RefreshTextureList();
			if (state.selected_texture == texture) {
				state.selected_texture.clear();
			}
			state.status_message = "Removed texture " + texture;
			return true;
		}

		bool RemoveFolder(EditorState& state, const std::string& folder, int& removed,
			std::string& error) {
			removed = 0;
			if (folder.empty()) {
				error = "the Assets/Textures folder itself cannot be removed";
				return false;
			}
			if (!ValidRelative(folder, error)) {
				return false;
			}
			const std::string abs = (fs::path(MaterialOps::TexturesDir(state)) / folder).string();
			std::error_code ec;
			if (!fs::is_directory(abs, ec)) {
				error = "no such folder: " + folder;
				return false;
			}
			const std::string prefix = Lower(folder) + "\\";
			std::vector<std::string> inside;
			for (const std::string& t : MaterialOps::ListTextures(state)) {
				if (Lower(t).rfind(prefix, 0) == 0) {
					inside.push_back(t);
				}
			}
			//All or nothing: one texture still in use keeps the whole folder.
			for (const std::string& t : inside) {
				const std::vector<std::string> users = FindUsers(state, t);
				if (!users.empty()) {
					error = "'" + t + "' is in use by " + users.front() +
						" - point it at another texture first";
					return false;
				}
			}
			for (const std::string& t : inside) {
				DropThumbnail(t);
				if (state.selected_texture == t) {
					state.selected_texture.clear();
				}
			}
			fs::remove_all(abs, ec);
			if (ec) {
				error = "could not delete " + abs + ": " + ec.message();
				return false;
			}
			PruneEmptyParents(state, fs::path(abs).parent_path());
			removed = (int)inside.size();
			MaterialOps::RefreshTextureList();
			state.status_message = "Removed folder " + folder + " (" + std::to_string(removed) + " texture(s))";
			return true;
		}

		void ReleaseThumbnails() {
			for (auto& entry : thumbnails) {
				if (entry.second != nullptr) {
					Core::ReleaseTexture(entry.second);
				}
			}
			thumbnails.clear();
		}

		ID3D11ShaderResourceView* Thumbnail(const EditorState& state, const std::string& texture) {
			auto it = thumbnails.find(texture);
			if (it == thumbnails.end()) {
				it = thumbnails.emplace(texture,
					Core::LoadTexture(MaterialOps::TextureAbsolutePath(state, texture))).first;
			}
			return it->second;
		}
	}

	namespace TexturePanel {
		namespace {

			constexpr float THUMB = 88.0f;

			//Several files at once. Explorer-style multi-select returns "dir\0a\0b\0\0"
			//(or one full path when a single file was picked).
			std::vector<std::string> BrowseForFiles() {
				std::vector<char> buffer(64 * 1024, 0);
				OPENFILENAMEA ofn = {};
				ofn.lStructSize = sizeof(ofn);
				ofn.lpstrFilter = "Image files\0*.dds;*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff\0"
								  "All files\0*.*\0";
				ofn.lpstrFile = buffer.data();
				ofn.nMaxFile = (DWORD)buffer.size();
				ofn.lpstrTitle = "Import Textures";
				ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
					OFN_ALLOWMULTISELECT | OFN_EXPLORER;
				std::vector<std::string> files;
				if (!GetOpenFileNameA(&ofn)) {
					return files;
				}
				const std::string first = buffer.data();
				const char* next = buffer.data() + first.size() + 1;
				if (*next == '\0') {
					files.push_back(first);
					return files;
				}
				while (*next != '\0') {
					files.push_back((fs::path(first) / next).string());
					next += strlen(next) + 1;
				}
				return files;
			}

			std::string BrowseForFolder() {
				char display[MAX_PATH] = {};
				BROWSEINFOA bi = {};
				bi.pszDisplayName = display;
				bi.lpszTitle = "Import a folder of textures";
				bi.ulFlags = BIF_RETURNONLYFSDIRS;
				PIDLIST_ABSOLUTE pidl = SHBrowseForFolderA(&bi);
				if (pidl == nullptr) {
					return {};
				}
				char path[MAX_PATH] = {};
				const bool ok = SHGetPathFromIDListA(pidl, path) != FALSE;
				CoTaskMemFree(pidl);
				return ok ? std::string(path) : std::string();
			}

			bool Contains(const std::string& haystack, const std::string& needle) {
				if (needle.empty()) {
					return true;
				}
				std::string h = haystack, n = needle;
				std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return (char)std::tolower(c); });
				std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return (char)std::tolower(c); });
				return h.find(n) != std::string::npos;
			}

			//Dimensions and format for the details pane, read off the loaded resource.
			bool Describe(ID3D11ShaderResourceView* srv, UINT& w, UINT& h, UINT& mips) {
				if (srv == nullptr) {
					return false;
				}
				ID3D11Resource* res = nullptr;
				srv->GetResource(&res);
				bool ok = false;
				if (res != nullptr) {
					ID3D11Texture2D* tex = nullptr;
					if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex)) && tex != nullptr) {
						D3D11_TEXTURE2D_DESC d;
						tex->GetDesc(&d);
						w = d.Width;
						h = d.Height;
						mips = d.MipLevels;
						ok = true;
						tex->Release();
					}
					res->Release();
				}
				return ok;
			}
		}

		void Draw(EditorState& state) {
			if (state.world == nullptr) {
				return;
			}
			static char filter[128] = "";
			static char new_folder[128] = "";
			static std::string folder_error;
			//The folder shown, and the one imports go into. "" is Assets/Textures itself
			//and, as a filter, "everything".
			static std::string folder;
			static bool all_folders = true;

			const ImGuiViewport* vp = ImGui::GetMainViewport();
			const ImGuiCond cond = state.apply_default_layout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
			ImGui::SetNextWindowPos(
				ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.30f, vp->WorkPos.y + vp->WorkSize.y * 0.20f), cond);
			ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x * 0.40f, vp->WorkSize.y * 0.55f), cond);
			if (!ImGui::Begin(EditorLayout::TEXTURES_WINDOW, &state.show_texture_panel)) {
				ImGui::End();
				return;
			}
			if (state.project_root.empty()) {
				ImGui::TextDisabled("No project open - the texture library is <project>/Assets/Textures.");
				ImGui::End();
				return;
			}

			//--- toolbar
			const std::vector<std::string> folders = TextureOps::ListFolders(state);
			if (!all_folders && !folder.empty() &&
				std::find(folders.begin(), folders.end(), folder) == folders.end()) {
				folder.clear();
				all_folders = true;
			}
			const std::string target = all_folders ? std::string() : folder;
			ImGui::SetNextItemWidth(220.0f);
			if (ImGui::BeginCombo("Folder", all_folders ? "(all folders)" : (folder.empty() ? "(Assets/Textures)" : folder.c_str()))) {
				if (ImGui::Selectable("(all folders)", all_folders)) {
					all_folders = true;
					folder.clear();
				}
				if (ImGui::Selectable("(Assets/Textures)", !all_folders && folder.empty())) {
					all_folders = false;
					folder.clear();
				}
				for (const std::string& f : folders) {
					if (ImGui::Selectable(f.c_str(), !all_folders && f == folder)) {
						all_folders = false;
						folder = f;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(160.0f);
			ImGui::InputTextWithHint("##filter", "filter by name", filter, sizeof(filter));

			if (ImGui::Button("Import files...")) {
				const std::vector<std::string> picked = BrowseForFiles();
				if (!picked.empty()) {
					int imported = 0, skipped = 0;
					folder_error.clear();
					if (!TextureOps::ImportFiles(state, picked, target, imported, skipped, folder_error)) {
						state.status_message = "Import failed: " + folder_error;
					}
				}
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Copy one or more images into the folder shown above\n"
					"(Assets/Textures when \"all folders\" is chosen).");
			}
			ImGui::SameLine();
			if (ImGui::Button("Import folder...")) {
				const std::string picked = BrowseForFolder();
				if (!picked.empty()) {
					int imported = 0, skipped = 0;
					folder_error.clear();
					if (!TextureOps::ImportFolder(state, picked, target, imported, skipped, folder_error)) {
						state.status_message = "Import failed: " + folder_error;
					}
				}
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Load a whole set: every image under the chosen folder is\n"
					"copied in, keeping the folder's own subfolders.");
			}
			ImGui::SameLine();
			if (ImGui::Button("New folder...")) {
				new_folder[0] = '\0';
				folder_error.clear();
				ImGui::OpenPopup("New Texture Folder");
			}
			ImGui::SameLine();
			if (ImGui::Button("Rescan")) {
				MaterialOps::RefreshTextureList();
				TextureOps::ReleaseThumbnails();
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(all_folders || folder.empty());
			if (ImGui::Button("Remove folder")) {
				folder_error.clear();
				ImGui::OpenPopup("Remove Texture Folder");
			}
			ImGui::EndDisabled();

			if (ImGui::BeginPopupModal("New Texture Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::TextDisabled("Relative to Assets/Textures, e.g. Wood\\Planks");
				ImGui::InputText("##nf", new_folder, sizeof(new_folder));
				if (!folder_error.empty()) {
					ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "%s", folder_error.c_str());
				}
				if (ImGui::Button("Create")) {
					if (TextureOps::CreateFolder(state, new_folder, folder_error)) {
						all_folders = false;
						folder = fs::path(new_folder).make_preferred().string();
						ImGui::CloseCurrentPopup();
					}
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel")) {
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			if (ImGui::BeginPopupModal("Remove Texture Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::Text("Delete '%s' and every texture in it from the project?", folder.c_str());
				ImGui::TextDisabled("This deletes the files. It cannot be undone.");
				if (!folder_error.empty()) {
					ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "%s", folder_error.c_str());
				}
				if (ImGui::Button("Delete")) {
					int removed = 0;
					if (TextureOps::RemoveFolder(state, folder, removed, folder_error)) {
						folder.clear();
						all_folders = true;
						ImGui::CloseCurrentPopup();
					}
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel")) {
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			//--- what is shown
			std::vector<std::string> shown;
			const std::string prefix = folder.empty() ? std::string() : folder + "\\";
			for (const std::string& t : MaterialOps::ListTextures(state)) {
				if (!all_folders) {
					const bool inside = folder.empty() ? (t.find('\\') == std::string::npos)
						: (t.size() > prefix.size() && t.compare(0, prefix.size(), prefix) == 0);
					if (!inside) {
						continue;
					}
				}
				if (!Contains(fs::path(t).filename().string(), filter)) {
					continue;
				}
				shown.push_back(t);
			}
			ImGui::Text("%d texture(s)", (int)shown.size());

			//--- grid
			const float details_height = 150.0f;
			ImGui::BeginChild("##texture_grid", ImVec2(0.0f, -details_height), true);
			if (shown.empty()) {
				ImGui::TextDisabled(MaterialOps::ListTextures(state).empty()
					? "The library is empty - use Import files... or Import folder..."
					: "Nothing matches.");
			}
			const float cell = THUMB + ImGui::GetStyle().ItemSpacing.x + ImGui::GetStyle().FramePadding.x * 2.0f;
			const int columns = (std::max)(1, (int)(ImGui::GetContentRegionAvail().x / cell));
			for (int i = 0; i < (int)shown.size(); ++i) {
				const std::string& rel = shown[i];
				if (i % columns != 0) {
					ImGui::SameLine();
				}
				ImGui::PushID(i);
				ImGui::BeginGroup();
				const bool selected = (state.selected_texture == rel);
				//Only what is on screen is loaded: a library of hundreds must not upload
				//every image the moment the panel opens.
				ID3D11ShaderResourceView* srv = ImGui::IsRectVisible(ImVec2(THUMB, THUMB + 20.0f))
					? TextureOps::Thumbnail(state, rel) : nullptr;
				if (selected) {
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.59f, 0.98f, 0.85f));
				}
				bool clicked = false;
				if (srv != nullptr) {
					clicked = ImGui::ImageButton("##t", (ImTextureID)srv, ImVec2(THUMB, THUMB));
				}
				else {
					clicked = ImGui::Button("##t", ImVec2(THUMB + ImGui::GetStyle().FramePadding.x * 2.0f,
						THUMB + ImGui::GetStyle().FramePadding.y * 2.0f));
				}
				if (selected) {
					ImGui::PopStyleColor();
				}
				if (clicked) {
					state.selected_texture = rel;
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("%s", rel.c_str());
				}
				const std::string name = fs::path(rel).filename().string();
				ImGui::Text("%.13s%s", name.c_str(), name.size() > 13 ? "..." : "");
				ImGui::EndGroup();
				ImGui::PopID();
			}
			ImGui::EndChild();

			//--- details of the selected texture
			ImGui::BeginChild("##texture_details", ImVec2(0.0f, 0.0f), true);
			const std::vector<std::string>& all = MaterialOps::ListTextures(state);
			if (state.selected_texture.empty() ||
				std::find(all.begin(), all.end(), state.selected_texture) == all.end()) {
				ImGui::TextDisabled("Select a texture to see where it is used.");
			}
			else {
				const std::string& rel = state.selected_texture;
				ID3D11ShaderResourceView* srv = TextureOps::Thumbnail(state, rel);
				if (srv != nullptr) {
					ImGui::Image((ImTextureID)srv, ImVec2(96.0f, 96.0f));
					ImGui::SameLine();
				}
				ImGui::BeginGroup();
				ImGui::Text("%s", rel.c_str());
				UINT w = 0, h = 0, mips = 0;
				if (Describe(srv, w, h, mips)) {
					ImGui::TextDisabled("%u x %u, %u mip level(s)", w, h, mips);
				}
				std::error_code ec;
				const auto bytes = fs::file_size(MaterialOps::TextureAbsolutePath(state, rel), ec);
				if (!ec) {
					ImGui::TextDisabled("%.1f KB on disk", bytes / 1024.0);
				}
				const std::vector<std::string> users = TextureOps::FindUsers(state, rel);
				if (users.empty()) {
					ImGui::TextDisabled("Not used by any material.");
				}
				else {
					ImGui::Text("Used by:");
					int shown_users = 0;
					for (const std::string& u : users) {
						if (++shown_users > 4) {
							ImGui::TextDisabled("... and %d more", (int)users.size() - 4);
							break;
						}
						ImGui::BulletText("%s", u.c_str());
					}
				}
				ImGui::BeginDisabled(!users.empty());
				if (ImGui::Button("Remove")) {
					folder_error.clear();
					ImGui::OpenPopup("Remove Texture");
				}
				ImGui::EndDisabled();
				if (!users.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
					ImGui::SetTooltip("Still in use - point those materials at another texture first.");
				}
				ImGui::EndGroup();

				if (ImGui::BeginPopupModal("Remove Texture", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
					ImGui::Text("Delete '%s' from the project?", rel.c_str());
					ImGui::TextDisabled("This deletes the file. It cannot be undone.");
					if (!folder_error.empty()) {
						ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "%s", folder_error.c_str());
					}
					if (ImGui::Button("Delete")) {
						if (TextureOps::RemoveTexture(state, rel, folder_error)) {
							ImGui::CloseCurrentPopup();
						}
					}
					ImGui::SameLine();
					if (ImGui::Button("Cancel")) {
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}
			}
			ImGui::EndChild();
			ImGui::End();
		}
	}
}
