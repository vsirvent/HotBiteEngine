#include "MeshyImport.h"
#include "AssetBrowser.h"
#include "MaterialPanel.h"
#include "TemplatePanel.h"

#include <Components/Base.h>

#include <Windows.h>
#include <commdlg.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <vector>

#pragma comment(lib, "comdlg32.lib")

namespace fs = std::filesystem;
using namespace HotBite::Engine;
using namespace HotBite::Engine::Components;

namespace HotBiteEditor {
	namespace MeshyImport {

		namespace {

			std::string ToLower(std::string s) {
				std::transform(s.begin(), s.end(), s.begin(),
					[](unsigned char c) { return (char)std::tolower(c); });
				return s;
			}

			std::string LowerExt(const fs::path& p) {
				return ToLower(p.extension().string());
			}

			//The suffix after the fbx's own stem ("bridge_normal" -> "normal"),
			//mapped to the MaterialTextures slot it fills. Meshy's own vocabulary
			//plus the common synonyms other PBR exporters use, so this is not
			//brittle to one tool's exact wording.
			std::string ClassifySuffix(const std::string& suffix) {
				static const std::map<std::string, std::string> table = {
					{"normal", "normal"}, {"nor", "normal"}, {"norm", "normal"},
					{"metallic", "metallic"}, {"metalness", "metallic"}, {"metal", "metallic"},
					{"roughness", "roughness"}, {"rough", "roughness"},
					{"ao", "ao"}, {"occlusion", "ao"}, {"ambientocclusion", "ao"},
					{"emissive", "emission"}, {"emission", "emission"},
					{"height", "height"}, {"displacement", "height"}, {"disp", "height"},
					{"opacity", "opacity"}, {"alpha", "opacity"},
					{"specular", "spec"}, {"spec", "spec"},
				};
				auto it = table.find(suffix);
				return it != table.end() ? it->second : std::string();
			}

			//Extracts a .zip into `dest_dir` with the tar.exe (bsdtar) Windows has
			//shipped in System32 since 1803 - deliberately not a bundled zip library,
			//since the OS already carries one that reads the format this needs.
			bool ExtractZip(const std::string& zip_path, const std::string& dest_dir, std::string& error) {
				wchar_t sysdir[MAX_PATH] = {};
				GetSystemDirectoryW(sysdir, MAX_PATH);
				const std::wstring exe = std::wstring(sysdir) + L"\\tar.exe";
				const std::wstring wzip(zip_path.begin(), zip_path.end());
				const std::wstring wdest(dest_dir.begin(), dest_dir.end());
				std::wstring cmdline = L"tar.exe -xf \"" + wzip + L"\" -C \"" + wdest + L"\"";
				std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
				buf.push_back(L'\0');

				STARTUPINFOW si = {};
				si.cb = sizeof(si);
				si.dwFlags = STARTF_USESHOWWINDOW;
				si.wShowWindow = SW_HIDE;
				PROCESS_INFORMATION pi = {};
				if (!CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE,
					CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
					error = "could not launch the system tar.exe to extract the .zip";
					return false;
				}
				WaitForSingleObject(pi.hProcess, INFINITE);
				DWORD code = 1;
				GetExitCodeProcess(pi.hProcess, &code);
				CloseHandle(pi.hProcess);
				CloseHandle(pi.hThread);
				if (code != 0) {
					error = "tar.exe could not extract " + zip_path + " (exit code " +
						std::to_string(code) + ")";
					return false;
				}
				return true;
			}

			//Finds the one .fbx under `root_dir` (recursively - a zip's own top-level
			//folder counts) when `fbx_path` does not already name one, then matches
			//every image beside it against the fbx's stem: an exact match is the
			//diffuse map, "<stem>_<suffix>" classifies by ClassifySuffix, anything
			//else (a stray thumbnail, an unrelated file) is silently ignored.
			bool FindFbxAndTextures(const std::string& root_dir, std::string& fbx_path,
				std::map<std::string, std::string>& textures, std::string& error) {
				std::error_code ec;
				if (fbx_path.empty()) {
					std::vector<std::string> found;
					for (auto& entry : fs::recursive_directory_iterator(root_dir,
						fs::directory_options::skip_permission_denied, ec)) {
						if (entry.is_regular_file(ec) && LowerExt(entry.path()) == ".fbx") {
							found.push_back(entry.path().string());
						}
					}
					if (found.empty()) {
						error = "no .fbx file found in " + root_dir;
						return false;
					}
					if (found.size() > 1) {
						error = "more than one .fbx file found in " + root_dir +
							" - pick the .fbx file directly to disambiguate";
						return false;
					}
					fbx_path = found.front();
				}

				const std::string stem = ToLower(fs::path(fbx_path).stem().string());
				static const char* kImageExts[] = { ".png", ".jpg", ".jpeg", ".tga", ".bmp" };
				for (auto& entry : fs::recursive_directory_iterator(root_dir,
					fs::directory_options::skip_permission_denied, ec)) {
					if (!entry.is_regular_file(ec)) {
						continue;
					}
					const std::string ext = LowerExt(entry.path());
					bool is_image = false;
					for (const char* e : kImageExts) {
						if (ext == e) { is_image = true; break; }
					}
					if (!is_image) {
						continue;
					}
					const std::string file_stem = ToLower(entry.path().stem().string());
					if (file_stem == stem) {
						textures["diffuse"] = entry.path().string();
					}
					else if (file_stem.size() > stem.size() + 1 &&
						file_stem.compare(0, stem.size(), stem) == 0 &&
						file_stem[stem.size()] == '_') {
						const std::string slot = ClassifySuffix(file_stem.substr(stem.size() + 1));
						if (!slot.empty()) {
							textures[slot] = entry.path().string();
						}
					}
				}
				if (textures.empty()) {
					error = "no textures found next to " + fbx_path + " matching its file name";
					return false;
				}
				return true;
			}

		} //namespace

		std::vector<float> DefaultLodRatios(int count) {
			std::vector<float> ratios;
			float ratio = 1.0f;
			for (int i = 0; i < count; ++i) {
				ratio *= 0.5f;
				ratios.push_back(ratio);
			}
			return ratios;
		}

		bool Import(EditorState& state, const std::string& source_path,
			const std::string& model_name, std::string& out_template_name, std::string& error,
			std::function<void(float, const std::string&)> on_progress,
			const std::vector<float>& lod_ratios) {
			if (state.world == nullptr || state.project_root.empty()) {
				error = "no project open";
				return false;
			}
			std::error_code ec;
			if (!fs::exists(source_path, ec)) {
				error = "not found: " + source_path;
				return false;
			}
			const std::string name = model_name.empty()
				? fs::path(source_path).filename().replace_extension().string()
				: model_name;
			if (name.empty() || name.find_first_of("\\/:*?\"<>|") != std::string::npos) {
				error = "a name cannot contain \\ / : * ? \" < > |";
				return false;
			}
			//Checked up front, before anything is imported: a Meshy import registers
			//three things under one name (model, material, template), and
			//AssetBrowser::ImportModel below only guards the first of them. Without
			//this a name collision on the material or the template would leave a
			//model imported with nothing built out of it.
			if (state.world->IsTemplateLoaded(name)) {
				error = "a template named '" + name + "' already exists";
				return false;
			}
			if (state.world->GetMaterials().Get(name) != nullptr && !state.world->IsMaterialRemoved(name)) {
				error = "a material named '" + name + "' already exists";
				return false;
			}
			if (state.world->GetMaterialFiles().empty()) {
				error = "the level has no material file to add '" + name + "' to";
				return false;
			}

			if (on_progress) on_progress(0.0f, "Reading Meshy package...");

			std::string temp_dir; //non-empty only when a .zip was extracted into it
			std::string root_dir;
			std::string fbx_path; //pre-selected when the user pointed at the .fbx directly
			const std::string ext = LowerExt(fs::path(source_path));
			const bool is_dir = fs::is_directory(source_path, ec);
			if (is_dir) {
				root_dir = source_path;
			}
			else if (ext == ".zip") {
				temp_dir = (fs::temp_directory_path(ec) / "HotBiteMeshyImport" / name).string();
				fs::remove_all(temp_dir, ec);
				fs::create_directories(temp_dir, ec);
				if (!ExtractZip(source_path, temp_dir, error)) {
					return false;
				}
				root_dir = temp_dir;
			}
			else if (ext == ".fbx") {
				root_dir = fs::path(source_path).parent_path().string();
				fbx_path = source_path;
			}
			else {
				error = "expected a Meshy .zip export, an .fbx file, or an extracted folder: " + source_path;
				return false;
			}

			auto cleanup_temp = [&]() {
				if (!temp_dir.empty()) {
					std::error_code cd;
					fs::remove_all(temp_dir, cd);
				}
				};

			std::map<std::string, std::string> textures;
			if (!FindFbxAndTextures(root_dir, fbx_path, textures, error)) {
				cleanup_temp();
				return false;
			}

			if (on_progress) on_progress(0.05f, "Importing model...");
			//Under Assets/Objects/<name>/, not flat: everything this package brings in
			//lands in one subfolder named after it, alongside its textures below.
			if (!AssetBrowser::ImportModel(state, fbx_path, name, error, on_progress, name)) {
				cleanup_temp();
				return false;
			}

			if (on_progress) on_progress(0.7f, "Importing textures...");
			const std::string tex_subfolder = name;
			Core::MaterialTextures tex_names;
			auto import_slot = [&](const char* key, std::string& out_field) -> bool {
				auto it = textures.find(key);
				if (it == textures.end()) {
					return true;
				}
				std::string out_path;
				if (!MaterialOps::ImportTexture(state, it->second, tex_subfolder, out_path, error)) {
					return false;
				}
				out_field = out_path;
				return true;
				};
			//The plain spec slot (SPECULAR_MAP_ENABLED_FLAG), in priority order: an
			//explicit specular map first if the package has one, otherwise metallic -
			//the engine has no metallic/roughness workflow to give either map a slot
			//of its own, and of the two a metallic map is the closer match to "how
			//shiny is this surface", so it stands in for spec rather than being
			//dropped. Roughness is the last resort, only when neither of the above
			//is present.
			const char* spec_source = textures.count("spec") ? "spec"
				: textures.count("metallic") ? "metallic"
				: textures.count("roughness") ? "roughness" : nullptr;
			const bool imported_ok =
				import_slot("diffuse", tex_names.diffuse_texname) &&
				import_slot("normal", tex_names.normal_textname) &&
				import_slot("emission", tex_names.emission_textname) &&
				import_slot("height", tex_names.high_textname) &&
				import_slot("opacity", tex_names.opacity_textname) &&
				import_slot("ao", tex_names.ao_textname) &&
				(spec_source == nullptr || import_slot(spec_source, tex_names.spec_textname));
			cleanup_temp();
			if (!imported_ok) {
				return false;
			}

			if (on_progress) on_progress(0.85f, "Creating material...");
			const std::string mat_file = state.world->GetMaterialFiles().begin()->first;
			if (!MaterialOps::CreateMaterial(state, name, mat_file, error)) {
				return false;
			}
			MaterialOps::MaterialSnapshot before;
			MaterialOps::GetSnapshot(state, name, before);
			MaterialOps::MaterialSnapshot after = before;
			after.texture_names = tex_names;
			if (!MaterialOps::ApplySnapshot(state, name, after, error)) {
				return false;
			}
			MaterialOps::RecordEdit(state, name, before);

			if (on_progress) on_progress(0.92f, "Creating template...");
			if (!TemplateOps::CreateFromModel(state, name, name, error)) {
				return false;
			}
			if (!TemplateOps::SetMaterial(state, name, name, error)) {
				return false;
			}

			int lods_built = 0;
			if (!lod_ratios.empty()) {
				if (on_progress) on_progress(0.96f, "Generating levels of detail...");
				nlohmann::json mesh_block = TemplateOps::GetComponent(state, name, Mesh::NAME);
				//The name CreateFromModel just gave the template's Mesh block, i.e. the
				//mesh asset level 0 of the chain is built from - never chained level-
				//over-level, for the reason MeshOps.h documents (each level approximates
				//an approximation, and the ratios stop meaning their own share of the
				//full mesh).
				const std::string source_mesh = mesh_block.value("name", std::string());
				if (!source_mesh.empty()) {
					nlohmann::json lods = nlohmann::json::array();
					for (float ratio : lod_ratios) {
						std::string generated_name;
						if (!state.world->GenerateMeshLod(source_mesh, ratio, generated_name, error)) {
							return false;
						}
						lods.push_back(nlohmann::json{ {"name", generated_name}, {"distance", 0.0f} });
						++lods_built;
					}
					mesh_block["lods"] = lods;
					if (!TemplateOps::SetComponent(state, name, Mesh::NAME, mesh_block, error)) {
						return false;
					}
				}
			}

			out_template_name = name;
			state.selected_template = name;
			state.show_template_panel = true;
			state.status_message = "Imported Meshy model '" + name + "' as a template (" +
				std::to_string(textures.size()) + " texture map(s), " +
				std::to_string(lods_built) + " LOD level(s))";
			if (on_progress) on_progress(1.0f, "Done");
			return true;
		}

		void ImportWithDialog(EditorState& state) {
			char file[MAX_PATH] = {};
			OPENFILENAMEA ofn = {};
			ofn.lStructSize = sizeof(ofn);
			ofn.hwndOwner = nullptr;
			ofn.lpstrFilter = "Meshy export\0*.zip;*.fbx\0"
							  "Zip archive\0*.zip\0"
							  "FBX model\0*.fbx\0"
							  "All files\0*.*\0";
			ofn.lpstrFile = file;
			ofn.nMaxFile = sizeof(file);
			ofn.lpstrTitle = "Import Meshy Model";
			ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
			if (!GetOpenFileNameA(&ofn)) {
				return;
			}
			//Named before anything loads, exactly like AssetBrowser::ImportModelWithDialog
			//and for the same reason: the name is the key the model, the material and the
			//template are all registered under.
			state.pending_meshy_import_path = file;
			state.pending_meshy_import_name =
				fs::path(state.pending_meshy_import_path).filename().replace_extension().string();
		}
	}
}
