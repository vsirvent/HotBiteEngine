#include "AssetBrowser.h"
#include "EditorHistory.h"
#include "EditorLayout.h"
#include "GridSnap.h"
#include "Inspector.h"
#include "Selection.h"
#include "SelectionGizmo.h"
#include "TemplatePanel.h"

#include "imgui.h"

#include <Systems/CameraSystem.h>
#include <Windows.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>
#include <filesystem>

#pragma comment(lib, "comdlg32.lib")

namespace fs = std::filesystem;
using namespace HotBite::Engine;

namespace HotBiteEditor {
	namespace AssetBrowser {

		static ModelAsset* FindModel(EditorState& state, const std::string& name)
		{
			for (ModelAsset& m : state.models) {
				if (m.name == name) {
					return &m;
				}
			}
			return nullptr;
		}

		//Loads every model listed in `state.models` that is not in the World yet.
		//Models the level.json already loaded (their World registry key is the file
		//stem, same as our ModelAsset name) must NOT be loaded again: World's dedup is
		//by path string, and re-loading under our absolute path replaces the registered
		//entities and meshes with copies whose vertex data was never uploaded to the GPU
		//(Init() ran before this scan), which makes everything using them render as
		//nothing.
		static void LoadPendingModels(EditorState& state)
		{
			bool loaded_any = false;
			for (ModelAsset& m : state.models) {
				if (m.loaded) {
					continue;
				}
				if (!state.world->IsModelLoaded(m.name) && !m.file_path.empty()) {
					//Under the name the browser knows it by, which is the file stem
					//unless the import gave it one of its own.
					state.world->LoadModel(m.file_path, false, false, false, m.name);
					loaded_any = true;
				}
				m.loaded = true;
			}
			if (loaded_any) {
				//Scan runs after World::Init has already uploaded the GPU buffers.
				state.world->RefreshMeshBuffers();
			}
		}

		static void ScanModelsFolder(EditorState& state)
		{
			//What the level already pulled in, *before* the folder is walked. The order
			//matters: these carry the name the level gave them, which for an import that
			//was named is not the file stem - and the folder loop below recognizes a file
			//it already has only by comparing paths against this list. Walk the folder
			//first and the same .ply comes back a second time under its stem.
			for (const std::string& name : state.world->ListModels()) {
				if (FindModel(state, name) != nullptr) {
					continue;
				}
				ModelAsset asset;
				asset.name = name;
				asset.loaded = true;
				const World::ModelAssets* assets = state.world->GetModelAssets(name);
				if (assets != nullptr && !assets->file.empty()) {
					//As the level referenced it, which is relative to the assets path.
					fs::path file(assets->file);
					asset.file_path = file.is_absolute()
						? file.string()
						: (fs::path(state.world->GetAssetsPath()) / file).lexically_normal().string();
				}
				state.models.push_back(asset);
			}

			fs::path objects_dir = fs::path(state.project_root) / "Assets" / "Objects";
			std::error_code ec;
			if (fs::exists(objects_dir, ec)) {
				for (auto& entry : fs::directory_iterator(objects_dir, ec)) {
					if (!entry.is_regular_file()) {
						continue;
					}
					//A model file is an .fbx or a Gaussian splat .ply. Both register
					//under their file stem and neither is placeable by itself, so from
					//here down they are the same kind of thing.
					std::string ext = entry.path().extension().string();
					std::transform(ext.begin(), ext.end(), ext.begin(),
						[](unsigned char c) { return (char)std::tolower(c); });
					if (ext != ".fbx" && ext != ".ply") {
						continue;
					}
					const std::string name = entry.path().filename().replace_extension().string();
					if (FindModel(state, name) != nullptr) {
						continue;
					}
					//By path as well as by name: a model imported under a name of its
					//own is already listed under that name, and matching only the stem
					//would adopt the same file a second time - two entries, two loads,
					//and a "models" array that grows a duplicate on every save.
					const std::string path = entry.path().string();
					bool already_listed = false;
					for (const ModelAsset& m : state.models) {
						std::error_code same;
						if (!m.file_path.empty() &&
							(m.file_path == path || fs::equivalent(m.file_path, path, same))) {
							already_listed = true;
							break;
						}
					}
					if (already_listed) {
						continue;
					}
					ModelAsset asset;
					asset.name = name;
					asset.file_path = path;
					state.models.push_back(asset);
				}
			}
			LoadPendingModels(state);
			std::sort(state.models.begin(), state.models.end(),
				[](const ModelAsset& a, const ModelAsset& b) { return a.name < b.name; });
		}

		void EnsureAssetsScanned(EditorState& state)
		{
			static std::string scanned_root;
			if (!state.project_root.empty() && scanned_root != state.project_root) {
				ScanModelsFolder(state);
				//Templates second: a .tpl names meshes and animation clips, and those
				//only exist once the models carrying them are loaded.
				TemplateOps::ScanTemplatesFolder(state);
				scanned_root = state.project_root;
			}
		}

		bool ImportModel(EditorState& state, const std::string& fbx_path,
			const std::string& model_name, std::string& error)
		{
			if (state.world == nullptr || state.project_root.empty()) {
				error = "no project open";
				return false;
			}
			std::error_code ec;
			if (!fs::exists(fbx_path, ec)) {
				error = "file not found: " + fbx_path;
				return false;
			}
			//The file stem unless the import was given a name. The name is the key the
			//whole editor addresses a model by, so it has to be free.
			const std::string name = model_name.empty()
				? fs::path(fbx_path).filename().replace_extension().string()
				: model_name;
			if (name.find_first_of("\\/:*?\"<>|") != std::string::npos) {
				error = "a model name cannot contain \\ / : * ? \" < > |";
				return false;
			}
			if (state.world->IsModelLoaded(name) || FindModel(state, name) != nullptr) {
				error = "a model named '" + name + "' is already imported";
				return false;
			}

			//Brought into the project rather than referenced where it lies: a level that
			//points outside the project cannot be opened on another machine. Skipped when
			//the file already is the project's copy (browsing to Assets/Objects itself).
			fs::path dest = fs::path(state.project_root) / "Assets" / "Objects" /
				fs::path(fbx_path).filename();
			fs::create_directories(dest.parent_path(), ec);
			if (!fs::exists(dest, ec) || !fs::equivalent(fs::path(fbx_path), dest, ec)) {
				fs::copy_file(fbx_path, dest, fs::copy_options::overwrite_existing, ec);
				if (ec) {
					error = "could not copy into " + dest.string() + ": " + ec.message();
					return false;
				}
			}

			ModelAsset asset;
			asset.name = name;
			asset.file_path = dest.string();
			state.models.push_back(asset);
			LoadPendingModels(state);
			std::sort(state.models.begin(), state.models.end(),
				[](const ModelAsset& a, const ModelAsset& b) { return a.name < b.name; });
			state.selected_model = name;

			const World::ModelAssets* assets = state.world->GetModelAssets(name);
			state.status_message = "Imported model: " + name;
			if (assets != nullptr) {
				state.status_message += " (" + std::to_string(assets->meshes.size()) + " mesh(es), " +
					std::to_string(assets->animation_sets.size()) + " animation set(s))";
			}
			return true;
		}

		bool RemoveModel(EditorState& state, const std::string& name, std::string& error)
		{
			if (state.world == nullptr) {
				error = "no project open";
				return false;
			}
			ModelAsset* asset = FindModel(state, name);
			if (asset == nullptr && !state.world->IsModelLoaded(name)) {
				error = "unknown model: " + name;
				return false;
			}
			//Deliberately outside the undo history, exactly like the import it undoes
			//and like File/Import Model: what it takes away is a registration and a set
			//of FBX nodes, and the assets the file contributed stay for the session (see
			//World::RemoveModel). Nothing that draws changes; what changes is what the
			//next load pulls in.
			state.world->RemoveModel(name);
			for (auto it = state.models.begin(); it != state.models.end(); ++it) {
				if (it->name == name) {
					state.models.erase(it);
					break;
				}
			}
			if (state.selected_model == name) {
				state.selected_model.clear();
			}
			state.status_message = "Removed model: " + name +
				" (its meshes stay loaded until the level is reopened)";
			return true;
		}

		void ImportModelWithDialog(EditorState& state)
		{
			char file[MAX_PATH] = {};
			OPENFILENAMEA ofn = {};
			ofn.lStructSize = sizeof(ofn);
			ofn.hwndOwner = nullptr;
			ofn.lpstrFilter = "Model files\0*.fbx;*.ply\0"
							  "FBX models\0*.fbx\0"
							  "Gaussian splat clouds\0*.ply\0"
							  "All files\0*.*\0";
			ofn.lpstrFile = file;
			ofn.nMaxFile = sizeof(file);
			ofn.lpstrTitle = "Import Model";
			ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
			if (!GetOpenFileNameA(&ofn)) {
				return;
			}
			//The import itself is deferred to the naming modal Draw puts up (see
			//pending_import_path): the name has to be settled *before* the file is
			//loaded, because it is the registry key the assets are filed under.
			state.pending_import_path = file;
			state.pending_import_name =
				fs::path(state.pending_import_path).filename().replace_extension().string();
		}

		//Spawns `inst` into the world and registers the save/selection bookkeeping.
		//Shared by the user-facing PlaceTemplate, paste (EntityOps) and the undo/redo
		//closures of both, so every path runs exactly the original placement code.
		bool SpawnRecordedInstance(EditorState& state, const PlacedInstance& inst, std::string& error)
		{
			ECS::Entity e = state.world->SpawnInstance(inst.name, inst.template_name,
				inst.position, inst.rotation, inst.scale, inst.material_name);
			if (e == ECS::INVALID_ENTITY_ID) {
				error = "SpawnInstance failed for template: " + inst.template_name;
				return false;
			}
			state.placed_instances.push_back(inst);
			state.instance_entity_ids.insert(e);
			Selection::Set(state, e);
			return true;
		}

		//Asked of the engine rather than reproduced here: the naming is SpawnInstance's,
		//and a composed template's parts make it more than "<name>_<index>" (see
		//World::InstanceEntityNames).
		std::vector<std::string> InstancePartNames(EditorState& state,
			const std::string& instance_name, const std::string& template_name)
		{
			return state.world->InstanceEntityNames(instance_name, template_name);
		}

		//Undo of a place / cut of an instance: destroys the instance's entities
		//(every part of a multi-part template) and drops its bookkeeping. Unlike a cut
		//scene entity, an instance is destroyed outright rather than parked: it can be
		//respawned from its template at any time, and the Physics component an authored
		//template may have given it releases its rigid body on destruction, so undo
		//rebuilds a clean body rather than reviving a stale one.
		void RemovePlacedInstance(EditorState& state, const std::string& instance_name)
		{
			auto record = state.placed_instances.end();
			for (auto it = state.placed_instances.begin(); it != state.placed_instances.end(); ++it) {
				if (it->name == instance_name) {
					record = it;
					break;
				}
			}
			ECS::Coordinator* c = state.world->GetCoordinator();
			if (record == state.placed_instances.end() || c == nullptr) {
				return;
			}
			for (const auto& part : InstancePartNames(state, instance_name, record->template_name)) {
				ECS::Entity e = c->GetEntityByName(part);
				if (e == ECS::INVALID_ENTITY_ID) {
					continue;
				}
				Selection::Remove(state, e);
				state.instance_entity_ids.erase(e);
				c->DestroyEntity(e);
			}
			state.placed_instances.erase(record);
		}

		//The template's base transform and the local-space offset from its origin down
		//to the bottom of its bounding box, read off the entity SpawnInstance clones.
		//
		//Both are needed to land an instance *on* a surface rather than through it:
		//the bottom offset says how far below the origin the object's underside sits,
		//and the base transform is what SpawnInstance adds to whatever position the
		//instance record carries - so the record has to be the aim point minus it.
		//For a multi-part template the first renderable part is used, which is the one
		//SpawnInstance calls the primary.
		static bool TemplateFootprint(EditorState& state, const std::string& template_name,
			float3& base_position, float& bottom_offset)
		{
			base_position = { 0.0f, 0.0f, 0.0f };
			bottom_offset = 0.0f;
			ECS::Coordinator* tc = state.world->GetTemplatesCoordinator();
			if (tc == nullptr) {
				return false;
			}
			for (ECS::Entity te : state.world->GetTemplateEntities(template_name)) {
				if (!tc->ContainsComponent<Components::Mesh>(te) ||
					!tc->ContainsComponent<Components::Bounds>(te) ||
					!tc->ContainsComponent<Components::Transform>(te)) {
					continue;
				}
				const Components::Transform& t = tc->GetConstComponent<Components::Transform>(te);
				const box& local = tc->GetConstComponent<Components::Bounds>(te).local_box;
				base_position = t.position;
				bottom_offset = (local.Center.y - local.Extents.y) * t.scale.y;
				return true;
			}
			return false;
		}

		bool ViewCenterPoint(EditorState& state, float3& out, bool& hit_something)
		{
			hit_something = false;
			ECS::Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr) {
				return false;
			}
			auto camera_system = c->GetSystem<Systems::CameraSystem>();
			if (camera_system == nullptr || camera_system->GetCameras().GetData().empty()) {
				return false;
			}
			const Components::Camera* cam = camera_system->GetCameras().GetData()[0].camera;
			const float3 origin = cam->world_position;
			float3 dir = cam->direction;
			const float len = std::sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
			if (len < 1e-6f) {
				return false;
			}
			dir = { dir.x / len, dir.y / len, dir.z / len };

			//Nothing in front of the camera (sky, or an empty scene) still has to place
			//something reachable, so the object goes a fixed way down the view ray -
			//close enough to be on screen, far enough not to be inside the near plane.
			constexpr float FALLBACK_DISTANCE = 15.0f;
			float distance = FALLBACK_DISTANCE;
			//The same test a viewport click runs, so "where it lands" matches "what I
			//would have clicked on".
			if (SelectionGizmo::RaycastScene(c, origin, dir, &distance) != ECS::INVALID_ENTITY_ID) {
				hit_something = true;
			}
			else {
				distance = FALLBACK_DISTANCE;
			}

			out = { origin.x + dir.x * distance,
					origin.y + dir.y * distance,
					origin.z + dir.z * distance };
			return true;
		}

		//Where a ViewCenter placement puts the instance record's position: the point
		//the middle of the view is aimed at, raised so the object rests on that
		//surface, minus the template's own base transform (which SpawnInstance adds
		//back). False when there is no camera to aim with.
		static bool ViewCenterPosition(EditorState& state, const std::string& template_name,
			float3& out, bool& hit_something)
		{
			if (!ViewCenterPoint(state, out, hit_something)) {
				return false;
			}

			float3 base_position;
			float bottom_offset = 0.0f;
			TemplateFootprint(state, template_name, base_position, bottom_offset);

			if (hit_something) {
				//Sit the object's underside on the surface instead of burying its
				//middle in it.
				out.y -= bottom_offset;
			}
			if (state.grid_snap_enabled) {
				//World-space XZ only: Y already rests on whatever surface was hit
				//(or sits at a fixed view-ray distance with nothing hit), and
				//snapping it too would either bury the object or float it.
				out.x = GridSnap::SnapValue(out.x, state.grid_size);
				out.z = GridSnap::SnapValue(out.z, state.grid_size);
			}
			out = { out.x - base_position.x, out.y - base_position.y, out.z - base_position.z };
			return true;
		}

		bool PlaceTemplate(EditorState& state, const std::string& template_name,
			PlacementMode mode, std::string& error, float3* out_position)
		{
			bool known = false;
			for (auto& t : state.templates) {
				if (t.name == template_name) { known = true; break; }
			}
			if (!known) {
				error = "unknown template: " + template_name;
				return false;
			}

			static int place_counter = 0;
			PlacedInstance inst;
			inst.name = template_name + "_inst_" + std::to_string(place_counter++);
			inst.template_name = template_name;

			std::string where = "at the origin";
			if (mode == PlacementMode::ViewCenter) {
				bool hit_something = false;
				if (ViewCenterPosition(state, template_name, inst.position, hit_something)) {
					where = hit_something ? "in view" : "in view (nothing under the view center)";
				}
				else {
					//No camera to aim with; the origin is the honest fallback rather
					//than refusing to place anything.
					where = "at the origin (no camera to aim with)";
				}
			}

			if (!SpawnRecordedInstance(state, inst, error)) {
				return false;
			}
			if (out_position != nullptr) {
				*out_position = inst.position;
			}
			state.status_message = "Placed " + where + ": " + inst.name;
			EditorHistory::Push({
				"place " + inst.name,
				[inst](EditorState& s) {
					RemovePlacedInstance(s, inst.name);
				},
				[inst](EditorState& s) {
					std::string err;
					SpawnRecordedInstance(s, inst, err);
				} });
			return true;
		}

		//The Models half of the panel: what this project has imported, and what each
		//file brought with it. A model is not placeable, so it has no Place button -
		//"Create Template" is the whole path from an imported file to an object, and
		//having it here is what makes the separation workable rather than a chore.
		static void DrawModels(EditorState& state)
		{
			if (state.models.empty()) {
				ImGui::TextDisabled("No models imported.");
				ImGui::TextDisabled("File/Import Model... brings an .fbx or a splat .ply in,\n"
					"or drop one into Assets/Objects.");
				return;
			}
			for (const ModelAsset& m : state.models) {
				ImGui::PushID(m.name.c_str());
				const World::ModelAssets* assets = state.world->GetModelAssets(m.name);
				ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
				if (m.name == state.selected_model) {
					flags |= ImGuiTreeNodeFlags_Selected;
				}
				const bool open = ImGui::TreeNodeEx("##model", flags, "%s", m.name.c_str());
				if (ImGui::IsItemClicked()) {
					state.selected_model = m.name;
				}
				if (open) {
					auto list = [](const char* label, const std::vector<std::string>& names) {
						if (names.empty()) {
							return;
						}
						if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_SpanAvailWidth,
							"%s (%d)", label, (int)names.size())) {
							for (const std::string& n : names) {
								ImGui::BulletText("%s", n.c_str());
							}
							ImGui::TreePop();
						}
						};
					if (assets == nullptr) {
						ImGui::TextDisabled("(not loaded yet)");
					}
					else {
						list("Meshes", assets->meshes);
						list("Materials", assets->materials);
						//Clips rather than sets: a set is a file, and the file is the
						//node this sits under. What matters here is which animations
						//it can give a template.
						std::vector<std::string> clips;
						for (const std::string& set : assets->animation_sets) {
							for (const std::string& clip : state.world->GetAnimationSetClips(set)) {
								clips.push_back(clip);
							}
						}
						list("Animations", clips);
						if (assets->meshes.empty() && assets->materials.empty() && clips.empty()) {
							ImGui::TextDisabled("(brought in nothing this level uses)");
						}
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			ImGui::Separator();
			ImGui::BeginDisabled(state.selected_model.empty());
			if (ImGui::Button("Create Template")) {
				std::string error;
				if (!TemplateOps::CreateFromModel(state, state.selected_model,
					TemplateOps::UniqueTemplateName(state, state.selected_model), error)) {
					state.status_message = "Create template failed: " + error;
				}
				else {
					state.show_template_panel = true;
				}
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered() && !state.selected_model.empty()) {
				ImGui::SetTooltip("Make a placeable object out of %s: its mesh, its material\n"
					"and its own transform, ready to have animations and physics added",
					state.selected_model.c_str());
			}
			ImGui::SameLine();
			if (ImGui::Button("Import Model...")) {
				ImportModelWithDialog(state);
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(state.selected_model.empty());
			if (ImGui::Button("Remove")) {
				ImGui::OpenPopup("remove_model");
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
				!state.selected_model.empty()) {
				ImGui::SetTooltip("Take %s out of this level's assets. The file stays in\n"
					"Assets/Objects; what goes is the level's reference to it.",
					state.selected_model.c_str());
			}
			if (ImGui::BeginPopupModal("remove_model", nullptr,
				ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::Text("Remove model '%s'?", state.selected_model.c_str());
				//Said out loud because it is the surprising half: nothing on screen
				//changes, and the templates built from it keep working for the session.
				ImGui::TextDisabled("Its meshes, materials and clips stay loaded until the\n"
					"level is reopened, and the file is left in Assets/Objects.\n"
					"Templates still pointing at its meshes will lose them on reload.\n"
					"This cannot be undone.");
				if (ImGui::Button("Remove")) {
					std::string error;
					if (!RemoveModel(state, state.selected_model, error)) {
						state.status_message = "Remove model failed: " + error;
					}
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel")) {
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			//The naming step of File/Import Model..., which runs between the file dialog
			//and the load (see ImportModelWithDialog).
			if (!state.pending_import_path.empty()) {
				ImGui::OpenPopup("name_import");
			}
			if (ImGui::BeginPopupModal("name_import", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::Text("Import %s", fs::path(state.pending_import_path).filename().string().c_str());
				char buf[128] = "";
				strncpy_s(buf, state.pending_import_name.c_str(), sizeof(buf) - 1);
				ImGui::SetNextItemWidth(320.0f);
				if (ImGui::InputText("Name", buf, sizeof(buf))) {
					state.pending_import_name = buf;
				}
				ImGui::TextDisabled("The name this project knows the model by. The meshes,\n"
					"materials and clips inside it keep their own names.");
				const bool taken = state.world->IsModelLoaded(state.pending_import_name) ||
					FindModel(state, state.pending_import_name) != nullptr;
				if (taken) {
					ImGui::TextDisabled("A model called that is already imported.");
				}
				ImGui::BeginDisabled(state.pending_import_name.empty() || taken);
				if (ImGui::Button("Import")) {
					std::string error;
					if (!ImportModel(state, state.pending_import_path,
						state.pending_import_name, error)) {
						state.status_message = "Import failed: " + error;
					}
					state.pending_import_path.clear();
					state.pending_import_name.clear();
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				if (ImGui::Button("Cancel")) {
					state.pending_import_path.clear();
					state.pending_import_name.clear();
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
		}

		void Draw(EditorState& state)
		{
			ImGui::Begin(EditorLayout::ASSET_BROWSER_WINDOW);

			if (state.project_root.empty()) {
				ImGui::TextUnformatted("No project open.");
				ImGui::End();
				return;
			}

			EnsureAssetsScanned(state);

			//Templates first: they are what a level is built out of. Models are below,
			//as the assets those templates are built from.
			ImGui::SeparatorText("Templates");
			if (state.templates.empty()) {
				ImGui::TextDisabled("No templates yet - pick a model below and\n"
					"\"Create Template\", or use the Templates panel.");
			}
			for (auto& t : state.templates) {
				bool is_selected = (t.name == state.selected_template);
				std::string label = t.name;
				if (state.dirty_templates.count(t.name) != 0) {
					label += "  *";
				}
				if (ImGui::Selectable(label.c_str(), is_selected)) {
					state.selected_template = t.name;
				}
			}

			ImGui::BeginDisabled(state.selected_template.empty());
			//"In View" first: dropping an object where you are already looking is the
			//everyday action, and the origin is often nowhere near the work.
			if (ImGui::Button("Place in View")) {
				std::string error;
				if (!PlaceTemplate(state, state.selected_template, PlacementMode::ViewCenter, error)) {
					state.status_message = "Place failed: " + error;
				}
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Drop it on whatever the middle of the view is looking at");
			}
			ImGui::SameLine();
			if (ImGui::Button("At Origin")) {
				std::string error;
				if (!PlaceTemplate(state, state.selected_template, PlacementMode::Origin, error)) {
					state.status_message = "Place failed: " + error;
				}
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Edit Templates...")) {
				state.show_template_panel = true;
			}

			ImGui::Spacing();
			ImGui::SeparatorText("Models");
			DrawModels(state);

			ImGui::End();
		}

	}
}
