#include "AssetBrowser.h"
#include "EditorHistory.h"
#include "EditorLayout.h"
#include "Inspector.h"
#include "Selection.h"
#include "SelectionGizmo.h"
#include "TemplatePanel.h"

#include "imgui.h"

#include <Systems/CameraSystem.h>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;
using namespace HotBite::Engine;

namespace HotBiteEditor {
	namespace AssetBrowser {

		static void ScanObjectsFolder(EditorState& state)
		{
			fs::path objects_dir = fs::path(state.project_root) / "Assets" / "Objects";
			if (!fs::exists(objects_dir)) {
				return;
			}
			for (auto& entry : fs::directory_iterator(objects_dir)) {
				if (!entry.is_regular_file() || entry.path().extension() != ".fbx") {
					continue;
				}
				std::string name = entry.path().filename().replace_extension().string();
				bool already_known = false;
				for (auto& t : state.templates) {
					if (t.name == name) { already_known = true; break; }
				}
				if (!already_known) {
					TemplateAsset asset;
					asset.name = name;
					asset.file_path = entry.path().string();
					state.templates.push_back(asset);
				}
			}
			//Make every discovered template immediately usable for placement this session.
			//Templates the level.json already loaded (their World registry key is the
			//filename stem, same as our TemplateAsset name) must NOT be loaded again:
			//World's dedup is by path string, and re-loading under our absolute path
			//replaces the registered template entities/meshes with copies whose vertex
			//data was never uploaded to the GPU (Init() ran before this scan), which
			//makes every instance placed from them render as nothing.
			bool loaded_any = false;
			for (auto& t : state.templates) {
				if (!t.loaded) {
					if (!state.world->IsTemplateLoaded(t.name)) {
						state.world->LoadTemplate(t.file_path, false, false);
						loaded_any = true;
					}
					t.loaded = true;
				}
			}
			if (loaded_any) {
				//Scan runs after World::Init has already uploaded the GPU buffers.
				state.world->RefreshMeshBuffers();
			}
		}

		void EnsureTemplatesScanned(EditorState& state)
		{
			static std::string scanned_root;
			if (!state.project_root.empty() && scanned_root != state.project_root) {
				ScanObjectsFolder(state);
				//Authored templates (.tpl) are the project's other kind of placeable
				//object; both scans run together so every surface listing templates -
				//this panel, the Templates panel, `list_templates` - sees one set.
				TemplateOps::ScanTemplatesFolder(state);
				scanned_root = state.project_root;
			}
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

		//SpawnInstance names multi-part instances "<name>_<index>" per part and
		//single-part ones plain "<name>"; this mirrors that.
		std::vector<std::string> InstancePartNames(EditorState& state,
			const std::string& instance_name, const std::string& template_name)
		{
			std::vector<std::string> part_names;
			size_t parts = state.world->GetTemplateEntities(template_name).size();
			if (parts > 1) {
				for (size_t i = 0; i < parts; ++i) {
					part_names.push_back(instance_name + "_" + std::to_string(i));
				}
			}
			else {
				part_names.push_back(instance_name);
			}
			return part_names;
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

		//Where a ViewCenter placement puts the instance record's position: the point
		//the middle of the view is aimed at, raised so the object rests on that
		//surface, minus the template's own base transform (which SpawnInstance adds
		//back). False when there is no camera to aim with.
		static bool ViewCenterPosition(EditorState& state, const std::string& template_name,
			float3& out, bool& hit_something)
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

			float3 base_position;
			float bottom_offset = 0.0f;
			TemplateFootprint(state, template_name, base_position, bottom_offset);

			out = { origin.x + dir.x * distance,
					origin.y + dir.y * distance,
					origin.z + dir.z * distance };
			if (hit_something) {
				//Sit the object's underside on the surface instead of burying its
				//middle in it.
				out.y -= bottom_offset;
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

		void Draw(EditorState& state)
		{
			ImGui::Begin(EditorLayout::ASSET_BROWSER_WINDOW);

			if (state.project_root.empty()) {
				ImGui::TextUnformatted("No project open.");
				ImGui::End();
				return;
			}

			EnsureTemplatesScanned(state);

			ImGui::SeparatorText("Templates");
			for (auto& t : state.templates) {
				bool is_selected = (t.name == state.selected_template);
				//Authored templates are marked so it is obvious which ones the
				//Templates panel can edit and which came out of an .fbx.
				std::string label = t.name;
				if (t.authored) {
					label += "  [tpl]";
					if (state.dirty_templates.count(t.name) != 0) {
						label += " *";
					}
				}
				if (ImGui::Selectable(label.c_str(), is_selected)) {
					state.selected_template = t.name;
				}
			}

			ImGui::Separator();
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

			ImGui::End();
		}

	}
}
