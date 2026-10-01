#include "MeshyImport.h"
#include "AssetBrowser.h"
#include "MaterialPanel.h"
#include "TemplatePanel.h"

#include <Components/Base.h>

#include <Windows.h>
#include <commdlg.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
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

			//A rigged Meshy export names its skinned model "<base>_Character_output.fbx",
			//one "<base>_Animation_<Clip>_without_skin.fbx" per clip (skeleton and motion,
			//no mesh) and its maps "<base>_texture_0[_<map>].png" - the maps are named
			//after <base>, not after the character file.
			const char* const kCharacterSuffix = "_character_output";
			const char* const kAnimationInfix = "_animation_";

			bool EndsWith(const std::string& s, const std::string& suffix) {
				return s.size() >= suffix.size() &&
					s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
			}

			bool IsAnimationFile(const fs::path& p) {
				return LowerExt(p) == ".fbx" &&
					ToLower(p.stem().string()).find(kAnimationInfix) != std::string::npos;
			}

			//The package name with the "_Character_output" tag taken off, lower case;
			//empty when `fbx_path` is not a rigged character file.
			std::string RiggedBase(const std::string& fbx_path) {
				const std::string stem = ToLower(fs::path(fbx_path).stem().string());
				const std::string suffix = kCharacterSuffix;
				return EndsWith(stem, suffix) ? stem.substr(0, stem.size() - suffix.size()) : std::string();
			}

			//Finds the one .fbx under `root_dir` (recursively - a zip's own top-level
			//folder counts) when `fbx_path` does not already name one, then matches
			//every image beside it against the fbx's stem: an exact match is the
			//diffuse map, "<stem>_<suffix>" classifies by ClassifySuffix, anything
			//else (a stray thumbnail, an unrelated file) is silently ignored.
			//
			//A rigged package has one character file and any number of animation files
			//beside it: the animation files do not count as "more than one .fbx", are
			//returned in `animations`, and the maps are matched against
			//"<base>_texture_0" instead of the character file's own stem.
			bool FindFbxAndTextures(const std::string& root_dir, std::string& fbx_path,
				std::map<std::string, std::string>& textures,
				std::vector<std::string>& animations, std::string& error) {
				std::error_code ec;
				auto list_fbx = [&](std::vector<std::string>& out) {
					for (auto& entry : fs::recursive_directory_iterator(root_dir,
						fs::directory_options::skip_permission_denied, ec)) {
						if (entry.is_regular_file(ec) && LowerExt(entry.path()) == ".fbx") {
							out.push_back(entry.path().string());
						}
					}
					};
				if (fbx_path.empty()) {
					std::vector<std::string> found;
					list_fbx(found);
					std::vector<std::string> models;
					for (const std::string& f : found) {
						if (!IsAnimationFile(f)) {
							models.push_back(f);
						}
					}
					if (found.empty()) {
						error = "no .fbx file found in " + root_dir;
						return false;
					}
					if (models.empty()) {
						error = "only animation .fbx files found in " + root_dir +
							" - the package has no character model";
						return false;
					}
					if (models.size() > 1) {
						error = "more than one .fbx file found in " + root_dir +
							" - pick the .fbx file directly to disambiguate";
						return false;
					}
					fbx_path = models.front();
				}

				std::string stem = ToLower(fs::path(fbx_path).stem().string());
				const std::string base = RiggedBase(fbx_path);
				if (!base.empty()) {
					std::vector<std::string> found;
					list_fbx(found);
					for (const std::string& f : found) {
						if (IsAnimationFile(f) &&
							ToLower(fs::path(f).stem().string()).compare(0, base.size() + 11,
								base + kAnimationInfix) == 0) {
							animations.push_back(f);
						}
					}
					std::sort(animations.begin(), animations.end());
					stem = base + "_texture_0";
				}
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

			//"<base>_Animation_Long_Breathe_and_Look_Around_without_skin" ->
			//"long_breathe_and_look_around": the name a template knows the clip by.
			//The few names game code is likely to want are mapped to the usual roles
			//(Walking -> walk, Running -> run, anything breathing -> idle); the rest
			//keep Meshy's own wording, and all of them can be renamed in the
			//template's Animations section.
			std::string AnimationLabel(const fs::path& file) {
				const std::string stem = ToLower(file.stem().string());
				std::string label = stem;
				const size_t at = stem.find(kAnimationInfix);
				if (at != std::string::npos) {
					label = stem.substr(at + std::string(kAnimationInfix).size());
				}
				for (const char* tail : { "_without_skin", "_with_skin" }) {
					if (EndsWith(label, tail)) {
						label.resize(label.size() - std::string(tail).size());
						break;
					}
				}
				if (label == "walking") return "walk";
				if (label == "running") return "run";
				if (label.find("breathe") != std::string::npos) return "idle";
				return label.empty() ? std::string("clip") : label;
			}

			//A rigged character is authored Z-up where the engine is Y-up. Meshy's
			//skinned FBX normally carries the correction as the mesh node's own rotation
			//(-90 degrees about X), which CreateFromModel keeps; but a package whose node
			//carries none would stand on its back. So the template's rotation is applied
			//to the mesh's bounds and, only when the long axis (a standing character's
			//height) still ends up on Z rather than Y, ComposeZUpFix turns it upright.
			bool StandsOnItsSide(const nlohmann::json& rotation, const nlohmann::json& extents) {
				const float x = rotation.value("x", 0.0f), y = rotation.value("y", 0.0f),
					z = rotation.value("z", 0.0f), w = rotation.value("w", 1.0f);
				const float ex = extents.value("x", 0.0f), ey = extents.value("y", 0.0f),
					ez = extents.value("z", 0.0f);
				const float up = std::fabs(2 * (x * y + z * w)) * ex +
					std::fabs(1 - 2 * (x * x + z * z)) * ey + std::fabs(2 * (y * z - x * w)) * ez;
				const float depth = std::fabs(2 * (x * z - y * w)) * ex +
					std::fabs(2 * (y * z + x * w)) * ey + std::fabs(1 - 2 * (x * x + y * y)) * ez;
				return depth > up * 1.2f;
			}

			//Multiplies a -90 degree turn about X (Z up -> Y up) into the rotation:
			//q = q_node * q_fix.
			void ComposeZUpFix(nlohmann::json& rotation) {
				const float s = std::sqrt(0.5f);
				const float fx = -s, fy = 0.0f, fz = 0.0f, fw = s;
				const float ax = rotation.value("x", 0.0f), ay = rotation.value("y", 0.0f),
					az = rotation.value("z", 0.0f), aw = rotation.value("w", 1.0f);
				rotation["x"] = aw * fx + ax * fw + ay * fz - az * fy;
				rotation["y"] = aw * fy - ax * fz + ay * fw + az * fx;
				rotation["z"] = aw * fz + ax * fy - ay * fx + az * fw;
				rotation["w"] = aw * fw - ax * fx - ay * fy - az * fz;
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
			std::vector<std::string> animation_files; //rigged packages only
			if (!FindFbxAndTextures(root_dir, fbx_path, textures, animation_files, error)) {
				cleanup_temp();
				return false;
			}
			const bool rigged = !RiggedBase(fbx_path).empty();

			//Each clip file becomes a model of its own ("<name>_<label>"), the engine's
			//unit for an animation set. Names are settled before anything imports, for
			//the reason the checks above give.
			struct ClipFile { std::string path, label, model; };
			std::vector<ClipFile> clip_files;
			std::set<std::string> used_labels;
			for (const std::string& file : animation_files) {
				ClipFile clip{ file, AnimationLabel(file), "" };
				const std::string label = clip.label;
				for (int n = 2; !used_labels.insert(clip.label).second; ++n) {
					clip.label = label + "_" + std::to_string(n);
				}
				clip.model = name + "_" + clip.label;
				if (state.world->IsModelLoaded(clip.model) || state.world->IsTemplateLoaded(clip.model)) {
					error = "a model named '" + clip.model + "' already exists";
					cleanup_temp();
					return false;
				}
				clip_files.push_back(std::move(clip));
			}

			//The character takes 0..0.45 of the bar when there are clips to follow, the
			//clips share 0.45..0.7, and everything after is unchanged.
			const float model_end = rigged ? 0.45f : 0.7f;
			auto scaled = [&](float from, float to) {
				return [on_progress, from, to](float p, const std::string& s) {
					if (on_progress) on_progress(from + (to - from) * p, s);
					};
				};

			if (on_progress) on_progress(0.05f, "Importing model...");
			//Under Assets/Objects/<name>/, not flat: everything this package brings in
			//lands in one subfolder named after it, alongside its textures below.
			if (!AssetBrowser::ImportModel(state, fbx_path, name, error,
				scaled(0.05f, model_end), name)) {
				cleanup_temp();
				return false;
			}

			std::vector<std::string> imported_clip_models;
			auto rollback_models = [&]() {
				std::string ignored;
				for (const std::string& m : imported_clip_models) {
					AssetBrowser::RemoveModel(state, m, ignored);
				}
				AssetBrowser::RemoveModel(state, name, ignored);
				};
			for (size_t i = 0; i < clip_files.size(); ++i) {
				const float from = model_end + 0.25f * (float)i / (float)clip_files.size();
				const float to = model_end + 0.25f * (float)(i + 1) / (float)clip_files.size();
				if (on_progress) on_progress(from, "Importing animation " + clip_files[i].label + "...");
				if (!AssetBrowser::ImportModel(state, clip_files[i].path, clip_files[i].model,
					error, scaled(from, to), name)) {
					rollback_models();
					cleanup_temp();
					return false;
				}
				imported_clip_models.push_back(clip_files[i].model);
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

			int clips_added = 0;
			if (rigged) {
				//The mesh skins with the skeleton its own FBX carries; each clip file's
				//set is a different rig's export of the same bones (other joint names,
				//extra leaf joints), which MeshData::AddSkeleton retargets onto it by
				//joint name when the template's Mesh is built.
				const World::ModelAssets* character = state.world->GetModelAssets(name);
				if (character == nullptr || character->animation_sets.empty()) {
					error = "the character .fbx carries no skeleton - is it really a rigged export?";
					return false;
				}
				std::string idle_label;
				for (const ClipFile& clip : clip_files) {
					const World::ModelAssets* assets = state.world->GetModelAssets(clip.model);
					if (assets == nullptr) {
						continue;
					}
					int within = 0; //a file with several takes: label, label_2, ...
					for (const std::string& set : assets->animation_sets) {
						for (const std::string& animation : state.world->GetAnimationSetClips(set)) {
							const std::string logical = within++ == 0
								? clip.label : clip.label + "_" + std::to_string(within);
							if (!TemplateOps::AddClip(state, name, logical, animation, error)) {
								return false;
							}
							++clips_added;
							if (idle_label.empty() && clip.label == "idle") {
								idle_label = logical;
							}
						}
					}
				}
				if (!idle_label.empty() &&
					!TemplateOps::SetDefaultClip(state, name, idle_label, true, 1.0f, error)) {
					return false;
				}

				//Z-up in the file, Y-up in the engine (see StandsOnItsSide).
				nlohmann::json transform = TemplateOps::GetComponent(state, name, Transform::NAME);
				const nlohmann::json bounds = TemplateOps::GetComponent(state, name, Bounds::NAME);
				if (transform.contains("rotation") && bounds.contains("extents") &&
					StandsOnItsSide(transform["rotation"], bounds["extents"])) {
					ComposeZUpFix(transform["rotation"]);
					if (!TemplateOps::SetComponent(state, name, Transform::NAME, transform, error)) {
						return false;
					}
				}
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
						if (!state.world->GenerateMeshLod(source_mesh, ratio, generated_name, error, name)) {
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
				std::to_string(lods_built) + " LOD level(s)" +
				(rigged ? ", rigged, " + std::to_string(clips_added) + " animation(s)" : std::string()) + ")";
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
