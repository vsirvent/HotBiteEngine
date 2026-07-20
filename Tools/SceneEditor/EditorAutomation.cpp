#include "EditorAutomation.h"
#include "EditorHistory.h"
#include "ProjectBrowser.h"
#include "Inspector.h"
#include "AssetBrowser.h"
#include "MaterialPanel.h"
#include "Outliner.h"
#include "EntityOps.h"
#include "ComponentOps.h"
#include "Selection.h"
#include "RenderSettings.h"
#include "RenderDocIntegration.h"

#include <Windows.h>
#include <Components/Base.h>
#include <Components/Physics.h>
#include <Core/Json.h>
#include <algorithm>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace nlohmann;
using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
namespace fs = std::filesystem;

namespace HotBiteEditor {
	namespace EditorAutomation {

		static bool enabled = false;
		static fs::path root_dir;

		//Responses for the batch consumed this frame. Screenshot results can only be
		//produced at frame end, so their slot is reserved here and filled in later.
		static std::vector<std::string> response_lines;
		static bool batch_open = false;

		struct PendingScreenshot {
			size_t response_index;
			std::string path;
		};
		static std::vector<PendingScreenshot> pending_screenshots;

		void Init(const std::string& dir)
		{
			root_dir = dir;
			std::error_code ec;
			fs::create_directories(root_dir, ec);
			enabled = true;
		}

		bool Enabled()
		{
			return enabled;
		}

		//Splits a command line into tokens; double quotes group tokens containing
		//spaces (Windows paths). No escape sequences - quotes cannot themselves be
		//part of an argument, which no supported argument needs.
		static std::vector<std::string> Tokenize(const std::string& line)
		{
			std::vector<std::string> tokens;
			std::string current;
			bool in_quotes = false;
			bool has_token = false;
			for (char ch : line) {
				if (ch == '"') {
					in_quotes = !in_quotes;
					has_token = true;
				}
				else if ((ch == ' ' || ch == '\t') && !in_quotes) {
					if (has_token) {
						tokens.push_back(current);
						current.clear();
						has_token = false;
					}
				}
				else {
					current += ch;
					has_token = true;
				}
			}
			if (has_token) {
				tokens.push_back(current);
			}
			return tokens;
		}

		static bool ParseFloats(const std::vector<std::string>& args, size_t first, size_t count, float* out)
		{
			if (args.size() < first + count) {
				return false;
			}
			try {
				for (size_t i = 0; i < count; ++i) {
					out[i] = std::stof(args[first + i]);
				}
			}
			catch (...) {
				return false;
			}
			return true;
		}

		static bool ParseFloat3(const std::vector<std::string>& args, size_t first, float3& out)
		{
			return ParseFloats(args, first, 3, &out.x);
		}

		static std::string StateJson(EditorState& state, SceneEditorApp& app)
		{
			json j;
			j["project_root"] = state.project_root;
			j["level_path"] = state.current_level_path;
			j["level_loaded"] = app.IsLevelLoaded();
			j["status"] = state.status_message;
			j["selected_entity"] = (int)state.selected_entity;
			j["selected_count"] = (int)Selection::Count(state);
			j["selected_names"] = Selection::Names(state);
			j["selected_template"] = state.selected_template;
			static const char* GIZMO_MODE_NAME[3] = { "translate", "rotate", "scale" };
			j["gizmo_mode"] = GIZMO_MODE_NAME[(int)state.gizmo_mode];
			static const char* COLLIDER_VIEW_NAME[3] = { "off", "selection", "all" };
			j["collider_view"] = COLLIDER_VIEW_NAME[(int)state.collider_view];
			j["placed_instances"] = state.placed_instances.size();
			Coordinator* c = state.world->GetCoordinator();
			j["entity_count"] = (c != nullptr) ? c->GetEntites().size() : 0;
			if (c != nullptr && state.selected_entity != INVALID_ENTITY_ID &&
				c->ContainsComponent<Base>(state.selected_entity)) {
				j["selected_entity_name"] = c->GetComponent<Base>(state.selected_entity).name;
			}
			json templates = json::array();
			for (auto& t : state.templates) {
				templates.push_back(t.name);
			}
			j["templates"] = templates;
			return j.dump();
		}

		static void Execute(const std::string& line, EditorState& state, SceneEditorApp& app)
		{
			response_lines.push_back("# " + line);
			std::vector<std::string> args = Tokenize(line);
			if (args.empty()) {
				return;
			}
			const std::string& cmd = args[0];
			std::string error;

			if (cmd == "ping") {
				response_lines.push_back("OK pong");
			}
			else if (cmd == "state") {
				response_lines.push_back("OK " + StateJson(state, app));
			}
			else if (cmd == "open_project") {
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: open_project <dir>");
				}
				else {
					app.OpenProject(args[1]);
					response_lines.push_back("OK " + state.status_message);
				}
			}
			else if (cmd == "open_level") {
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: open_level <level.json path>");
				}
				else {
					if (state.project_root.empty()) {
						state.project_root = ProjectBrowser::DeriveProjectRoot(args[1]);
					}
					if (app.OpenLevel(args[1])) {
						response_lines.push_back("OK " + state.status_message);
					}
					else {
						response_lines.push_back("ERR " + state.status_message);
					}
				}
			}
			else if (cmd == "menus") {
				response_lines.push_back("OK " + std::to_string(app.GetMenuCommands().size()) + " menu commands");
				for (const auto& mc : app.GetMenuCommands()) {
					bool is_enabled = !mc.enabled || mc.enabled();
					response_lines.push_back(mc.path + (is_enabled ? "" : " (disabled)"));
				}
			}
			else if (cmd == "menu") {
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: menu <Menu/Item>");
				}
				else if (app.ExecuteMenuCommand(args[1], error)) {
					response_lines.push_back("OK " + state.status_message);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "list_entities") {
				Coordinator* c = state.world->GetCoordinator();
				if (c == nullptr) {
					response_lines.push_back("ERR no coordinator");
				}
				else {
					size_t count = 0;
					std::vector<std::string> lines;
					for (const auto& [name, entity] : c->GetEntites()) {
						//Parked (cut) entities are hidden here just like in the panel.
						if (!c->ContainsComponent<Base>(entity) || EntityOps::IsParkedName(name)) {
							continue;
						}
						std::ostringstream os;
						os << name << " id=" << entity;
						if (c->ContainsComponent<Transform>(entity)) {
							const Transform& t = c->GetComponent<Transform>(entity);
							os << " pos=(" << t.position.x << "," << t.position.y << "," << t.position.z << ")";
						}
						auto group_it = state.entity_group_of.find(name);
						if (group_it != state.entity_group_of.end()) {
							os << " group=" << group_it->second;
						}
						if (entity == state.selected_entity) {
							os << " [selected]";
						}
						else if (Selection::Contains(state, entity)) {
							os << " [also selected]";
						}
						lines.push_back(os.str());
						++count;
					}
					response_lines.push_back("OK " + std::to_string(count) + " entities");
					for (auto& l : lines) {
						response_lines.push_back(l);
					}
				}
			}
			else if (cmd == "select" || cmd == "add_select") {
				//`select a b c` replaces the selection with all three (the last
				//becomes primary, the one the Components panel edits); `add_select`
				//extends the current selection instead, the scripted equivalent of
				//Ctrl+clicking. `select` with no arguments clears the selection.
				Coordinator* c = state.world->GetCoordinator();
				if (c == nullptr) {
					response_lines.push_back("ERR no coordinator");
				}
				else if (args.size() < 2 && cmd == "add_select") {
					response_lines.push_back("ERR usage: add_select <entity name> [<entity name> ...]");
				}
				else {
					std::vector<Entity> entities;
					std::string missing;
					for (size_t i = 1; i < args.size(); ++i) {
						Entity e = c->GetEntityByName(args[i]);
						if (e == INVALID_ENTITY_ID) {
							missing = args[i];
							break;
						}
						entities.push_back(e);
					}
					if (!missing.empty()) {
						//All-or-nothing: a typo must not leave a half-applied selection.
						response_lines.push_back("ERR entity not found: " + missing);
					}
					else {
						if (cmd == "select") {
							Selection::Set(state, entities);
						}
						else {
							for (Entity e : entities) {
								Selection::Add(state, e);
							}
						}
						response_lines.push_back("OK " + std::to_string(Selection::Count(state)) +
							" selected, primary id=" + std::to_string(state.selected_entity));
					}
				}
			}
			else if (cmd == "select_group") {
				//Selecting a group selects the entities in it, like clicking the
				//group header in the Entities panel.
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: select_group <group name> [add]");
				}
				else if (state.entity_groups.count(args[1]) == 0) {
					response_lines.push_back("ERR unknown group: " + args[1]);
				}
				else {
					Selection::SelectGroup(state, args[1], args.size() > 2 && args[2] == "add");
					response_lines.push_back("OK " + std::to_string(Selection::Count(state)) + " selected");
				}
			}
			else if (cmd == "list_selection") {
				response_lines.push_back("OK " + std::to_string(Selection::Count(state)) + " selected");
				for (const auto& name : Selection::Names(state)) {
					response_lines.push_back(name);
				}
			}
			else if (cmd == "delete") {
				//Deletes the selection. The interactive Del key confirms a
				//multi-entity delete; a scripted delete is already explicit, so it
				//goes straight through (same EntityOps call the modal's Delete button
				//makes, so it is one undo step either way).
				if (EntityOps::DeleteSelected(state, error)) {
					response_lines.push_back("OK " + state.status_message);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "colliders") {
				//The collider wireframe overlay (View/Colliders in the menu bar).
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: colliders off|selection|all");
				}
				else if (args[1] == "off") {
					state.collider_view = ColliderView::Off;
					response_lines.push_back("OK colliders off");
				}
				else if (args[1] == "selection") {
					state.collider_view = ColliderView::Selection;
					response_lines.push_back("OK colliders selection");
				}
				else if (args[1] == "all") {
					state.collider_view = ColliderView::All;
					response_lines.push_back("OK colliders all");
				}
				else {
					response_lines.push_back("ERR usage: colliders off|selection|all");
				}
			}
			else if (cmd == "physics_info") {
				//Numeric counterpart of the collider overlay: for each selected
				//entity, the collider's world AABB against the rendered mesh's, so a
				//collider that does not match the mesh is a number rather than
				//something to squint at in a screenshot.
				Coordinator* c = state.world->GetCoordinator();
				if (c == nullptr) {
					response_lines.push_back("ERR no coordinator");
				}
				else if (state.selected_entities.empty()) {
					response_lines.push_back("ERR nothing selected");
				}
				else {
					std::lock_guard<std::recursive_mutex> lock(Core::physics_mutex);
					response_lines.push_back("OK " + std::to_string(Selection::Count(state)) + " selected");
					for (Entity e : state.selected_entities) {
						if (!c->ContainsComponent<Base>(e)) {
							continue;
						}
						const std::string& name = c->GetComponent<Base>(e).name;
						std::ostringstream os;
						os << name;
						if (!c->ContainsComponent<Physics>(e)) {
							response_lines.push_back(os.str() + " no Physics component");
							continue;
						}
						Physics& ph = c->GetComponent<Physics>(e);
						if (ph.body == nullptr || ph.collider == nullptr) {
							response_lines.push_back(os.str() + " no body/collider");
							continue;
						}
						static const char* BODY_TYPE[3] = { "static", "kinematic", "dynamic" };
						os << " body=" << BODY_TYPE[(int)ph.type]
							<< " active=" << (ph.body->isActive() ? "1" : "0");
						const reactphysics3d::CollisionShape* shape = ph.collider->getCollisionShape();
						if (shape != nullptr) {
							os << " shape=" << (int)shape->getName();
							if (shape->getName() == reactphysics3d::CollisionShapeName::TRIANGLE_MESH) {
								//The scale the live shape is actually collided at - the
								//number that decides whether the collider matches the mesh.
								const reactphysics3d::Vector3& sc =
									((const reactphysics3d::ConcaveShape*)shape)->getScale();
								os << " live_shape_scale=(" << sc.x << "," << sc.y << "," << sc.z << ")";
							}
						}
						if (c->ContainsComponent<Transform>(e)) {
							const float3& s = c->GetComponent<Transform>(e).scale;
							os << " entity_scale=(" << s.x << "," << s.y << "," << s.z << ")";
						}
						Core::ShapeData* data = state.world->GetEntityShape(name);
						if (data != nullptr) {
							const float3& a = data->authored_scale;
							os << " shape_authored_scale=(" << a.x << "," << a.y << "," << a.z << ")";
							//Whether this component built its own pre-scaled geometry
							//rather than sharing the ShapeData's.
							os << " private_shape=" << (ph.owned_shape != nullptr ? "1" : "0");
						}
						else {
							os << " shape_data=none";
						}
						response_lines.push_back(os.str());

						//The comparison that matters: collider extent vs mesh extent.
						reactphysics3d::AABB cab = ph.collider->getWorldAABB();
						reactphysics3d::Vector3 cmin = cab.getMin(), cmax = cab.getMax();
						std::ostringstream cs;
						cs << "  collider_aabb=(" << cmin.x << "," << cmin.y << "," << cmin.z
							<< ")..(" << cmax.x << "," << cmax.y << "," << cmax.z << ")";
						response_lines.push_back(cs.str());
						if (c->ContainsComponent<Bounds>(e)) {
							const box& b = c->GetComponent<Bounds>(e).final_box;
							float3 mmin{ b.Center.x - b.Extents.x, b.Center.y - b.Extents.y, b.Center.z - b.Extents.z };
							float3 mmax{ b.Center.x + b.Extents.x, b.Center.y + b.Extents.y, b.Center.z + b.Extents.z };
							std::ostringstream ms;
							ms << "  mesh_aabb=(" << mmin.x << "," << mmin.y << "," << mmin.z
								<< ")..(" << mmax.x << "," << mmax.y << "," << mmax.z << ")";
							response_lines.push_back(ms.str());
							float dx = (mmax.x - mmin.x) - (cmax.x - cmin.x);
							float dy = (mmax.y - mmin.y) - (cmax.y - cmin.y);
							float dz = (mmax.z - mmin.z) - (cmax.z - cmin.z);
							float worst = (std::max)({ dx, dy, dz });
							float mesh_size = (std::max)({ mmax.x - mmin.x, mmax.y - mmin.y, mmax.z - mmin.z });
							std::ostringstream vs;
							vs << "  collider_smaller_than_mesh_by=(" << dx << "," << dy << "," << dz << ") ";
							//Only a mesh collider is supposed to track the mesh: the
							//primitive forms (the capsule every dynamic body gets, boxes,
							//spheres) are deliberate approximations and will always
							//differ, so flagging them would just be noise. For a mesh
							//collider, being clearly SMALLER than the mesh is the failure
							//that lets things fall through.
							bool is_mesh_shape = shape != nullptr &&
								shape->getName() == reactphysics3d::CollisionShapeName::TRIANGLE_MESH;
							if (!is_mesh_shape) {
								vs << "n/a (primitive shape approximates the mesh by design)";
							}
							else {
								vs << ((mesh_size > 0.0f && worst > mesh_size * 0.02f) ? "SUSPECT" : "ok");
							}
							response_lines.push_back(vs.str());
						}
					}
				}
			}
			//--- Materials (see MaterialPanel.h). Materials are keyed by name and are
			//saved separately from the level, so `save_materials` is its own command
			//rather than part of `save`.
			else if (cmd == "materials") {
				const std::vector<std::string> names = MaterialOps::ListMaterials(state);
				response_lines.push_back("OK " + std::to_string(names.size()) + " materials");
				for (const std::string& name : names) {
					std::ostringstream os;
					const std::string file = state.world->GetMaterialOrigin(name);
					os << name << " file=" << (file.empty() ? "(none)" : file);
					if (state.dirty_material_files.count(file) != 0) {
						os << " unsaved";
					}
					const std::vector<std::string> users = MaterialOps::FindUsers(state, name);
					os << " users=" << users.size();
					if (name == state.selected_material) {
						os << " selected";
					}
					response_lines.push_back(os.str());
				}
			}
			else if (cmd == "select_material") {
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: select_material <material name>");
				}
				else if (state.world->GetMaterials().Get(args[1]) == nullptr ||
					state.world->IsMaterialRemoved(args[1])) {
					response_lines.push_back("ERR material not found: " + args[1]);
				}
				else {
					state.selected_material = args[1];
					//Selecting a material is how a script gets the panel to show it, so
					//open the panel too rather than requiring a separate menu command.
					state.show_material_panel = true;
					response_lines.push_back("OK selected material: " + args[1]);
				}
			}
			else if (cmd == "create_material") {
				if (args.size() < 3) {
					response_lines.push_back("ERR usage: create_material <name> <mat file>");
				}
				else if (MaterialOps::CreateMaterial(state, args[1], args[2], error)) {
					response_lines.push_back("OK material created: " + args[1] + " in " + args[2]);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "remove_material") {
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: remove_material <name>");
				}
				else if (MaterialOps::RemoveMaterial(state, args[1], error)) {
					response_lines.push_back("OK material removed: " + args[1]);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "set_material") {
				if (args.size() < 3) {
					response_lines.push_back("ERR usage: set_material <entity name> <material name>");
				}
				else if (MaterialOps::AssignMaterial(state, args[1], args[2], error)) {
					response_lines.push_back("OK " + args[1] + " -> " + args[2]);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "shaders") {
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: shaders <material name>");
				}
				else {
					Core::MaterialData* m = state.world->GetMaterials().Get(args[1]);
					if (m == nullptr || state.world->IsMaterialRemoved(args[1])) {
						response_lines.push_back("ERR material not found: " + args[1]);
					}
					else {
						const Core::MaterialShaderNames& s = m->shader_names;
						response_lines.push_back("OK shaders for " + args[1]);
						response_lines.push_back("draw_vs=" + s.draw_vs);
						response_lines.push_back("draw_hs=" + s.draw_hs);
						response_lines.push_back("draw_ds=" + s.draw_ds);
						response_lines.push_back("draw_gs=" + s.draw_gs);
						response_lines.push_back("draw_ps=" + s.draw_ps);
						response_lines.push_back("shadow_vs=" + s.shadow_vs);
						response_lines.push_back("shadow_gs=" + s.shadow_gs);
						response_lines.push_back("depth_vs=" + s.depth_vs);
						response_lines.push_back("depth_ps=" + s.depth_ps);
					}
				}
			}
			else if (cmd == "set_shader") {
				if (args.size() < 4) {
					response_lines.push_back("ERR usage: set_shader <material> <slot> <file.cso>"
						" (slots: draw_vs draw_hs draw_ds draw_gs draw_ps shadow_vs shadow_gs"
						" depth_vs depth_ps)");
				}
				else {
					Core::MaterialData* m = state.world->GetMaterials().Get(args[1]);
					if (m == nullptr || state.world->IsMaterialRemoved(args[1])) {
						response_lines.push_back("ERR material not found: " + args[1]);
					}
					else {
						Core::MaterialShaderNames names = m->shader_names;
						const std::string& slot = args[2];
						bool known = true;
						if (slot == "draw_vs") { names.draw_vs = args[3]; }
						else if (slot == "draw_hs") { names.draw_hs = args[3]; }
						else if (slot == "draw_ds") { names.draw_ds = args[3]; }
						else if (slot == "draw_gs") { names.draw_gs = args[3]; }
						else if (slot == "draw_ps") { names.draw_ps = args[3]; }
						else if (slot == "shadow_vs") { names.shadow_vs = args[3]; }
						else if (slot == "shadow_gs") { names.shadow_gs = args[3]; }
						else if (slot == "depth_vs") { names.depth_vs = args[3]; }
						else if (slot == "depth_ps") { names.depth_ps = args[3]; }
						else { known = false; }

						if (!known) {
							response_lines.push_back("ERR unknown shader slot: " + slot);
						}
						else if (MaterialOps::SetShaders(state, args[1], names, error)) {
							response_lines.push_back("OK " + args[1] + " " + slot + " -> " + args[3]);
						}
						else {
							response_lines.push_back("ERR " + error);
						}
					}
				}
			}
			else if (cmd == "save_materials") {
				if (MaterialOps::SaveMaterials(state, error)) {
					response_lines.push_back("OK " + state.status_message);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "create_group") {
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: create_group <name>");
				}
				else if (Outliner::CreateGroup(state, args[1], error)) {
					response_lines.push_back("OK group created: " + args[1]);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "set_group") {
				//`set_group <entity> none` ungroups; naming a new group creates it.
				if (args.size() < 3) {
					response_lines.push_back("ERR usage: set_group <entity name> <group|none>");
				}
				else {
					std::string group = (args[2] == "none") ? "" : args[2];
					if (Outliner::SetEntityGroup(state, args[1], group, error)) {
						response_lines.push_back("OK " + args[1] + " -> " + (group.empty() ? "(none)" : group));
					}
					else {
						response_lines.push_back("ERR " + error);
					}
				}
			}
			else if (cmd == "list_groups") {
				response_lines.push_back("OK " + std::to_string(state.entity_groups.size()) + " groups");
				for (const auto& g : state.entity_groups) {
					std::ostringstream os;
					os << g << ":";
					for (const auto& [entity_name, group] : state.entity_group_of) {
						if (group == g) {
							os << " " << entity_name;
						}
					}
					response_lines.push_back(os.str());
				}
			}
			else if (cmd == "focus") {
				//Frames the selected entity, exactly like double-clicking it in the
				//Entities panel (same FocusSelected code path).
				if (Outliner::FocusSelected(state, app.GetEditorCamera(), error)) {
					response_lines.push_back("OK focused entity " + std::to_string(state.selected_entity));
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "set_position" || cmd == "set_scale" || cmd == "set_rotation") {
				float3 v;
				if (!ParseFloat3(args, 1, v)) {
					response_lines.push_back("ERR usage: " + cmd + " <x> <y> <z>");
				}
				else {
					const float3* position = (cmd == "set_position") ? &v : nullptr;
					const float3* scale = (cmd == "set_scale") ? &v : nullptr;
					const float3* euler = (cmd == "set_rotation") ? &v : nullptr;
					if (Inspector::ApplyTransform(state, position, scale, euler, error)) {
						response_lines.push_back("OK");
					}
					else {
						response_lines.push_back("ERR " + error);
					}
				}
			}
			else if (cmd == "rename") {
				//Renames an entity by name (not necessarily the selected one).
				if (args.size() < 3) {
					response_lines.push_back("ERR usage: rename <entity name> <new name>");
				}
				else if (EntityOps::RenameEntity(state, args[1], args[2], error)) {
					response_lines.push_back("OK " + state.status_message);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "add_component" || cmd == "remove_component") {
				if (args.size() < 3) {
					response_lines.push_back("ERR usage: " + cmd + " <entity name> <Component>");
				}
				else {
					const bool adding = (cmd == "add_component");
					//Defaults for an add: every FromJson treats a missing key as "leave
					//alone", so an empty object is the component as constructed.
					bool ok = adding
						? ComponentOps::AddComponent(state, args[1], args[2],
							nlohmann::json::object(), error)
						: ComponentOps::RemoveComponent(state, args[1], args[2], error);
					response_lines.push_back(ok ? ("OK " + state.status_message) : ("ERR " + error));
				}
			}
			else if (cmd == "components") {
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: components <entity name>");
				}
				else {
					std::vector<std::string> names = ComponentOps::ListComponents(state, args[1]);
					if (names.empty()) {
						response_lines.push_back("ERR unknown entity '" + args[1] + "'");
					}
					else {
						response_lines.push_back("OK " + std::to_string(names.size()) +
							" components on " + args[1]);
						for (const std::string& name : names) {
							response_lines.push_back(name);
						}
					}
				}
			}
			else if (cmd == "copy" || cmd == "cut") {
				//Operate on the current selection (like Ctrl+C/Ctrl+X); optional
				//argument selects an entity first for convenience.
				Coordinator* c = state.world->GetCoordinator();
				if (args.size() >= 2) {
					if (c == nullptr) {
						response_lines.push_back("ERR no coordinator");
						return;
					}
					Entity e = c->GetEntityByName(args[1]);
					if (e == INVALID_ENTITY_ID) {
						response_lines.push_back("ERR entity not found: " + args[1]);
						return;
					}
					Selection::Set(state, e);
				}
				bool ok = (cmd == "copy") ? EntityOps::CopySelected(state, error)
					: EntityOps::CutSelected(state, error);
				if (ok) {
					response_lines.push_back("OK " + state.status_message);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "paste") {
				if (EntityOps::Paste(state, error)) {
					response_lines.push_back("OK " + state.status_message);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "list_templates") {
				AssetBrowser::EnsureTemplatesScanned(state);
				response_lines.push_back("OK " + std::to_string(state.templates.size()) + " templates");
				for (auto& t : state.templates) {
					response_lines.push_back(t.name + (t.name == state.selected_template ? " [selected]" : ""));
				}
			}
			else if (cmd == "select_template") {
				AssetBrowser::EnsureTemplatesScanned(state);
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: select_template <name>");
				}
				else {
					bool known = false;
					for (auto& t : state.templates) {
						if (t.name == args[1]) { known = true; break; }
					}
					if (known) {
						state.selected_template = args[1];
						response_lines.push_back("OK");
					}
					else {
						response_lines.push_back("ERR unknown template: " + args[1]);
					}
				}
			}
			else if (cmd == "place") {
				AssetBrowser::EnsureTemplatesScanned(state);
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: place <template name>");
				}
				else if (AssetBrowser::PlaceTemplate(state, args[1], error)) {
					response_lines.push_back("OK " + state.status_message);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "import") {
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: import <fbx path>");
				}
				else if (AssetBrowser::ImportObject(state, args[1], error)) {
					response_lines.push_back("OK " + state.status_message);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "screenshot") {
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: screenshot <png path>");
				}
				else {
					//The backbuffer only holds the finished frame at Present time;
					//reserve this command's response slot and fill it in OnFrameEnd.
					pending_screenshots.push_back({ response_lines.size(), args[1] });
					response_lines.push_back("ERR screenshot never captured"); //placeholder
				}
			}
			else if (cmd == "render") {
				//`render` dumps the current settings; `render <key> <value>` changes one
				//(same keys as the Render menu, see RenderSettings.h).
				if (args.size() < 2) {
					response_lines.push_back("OK " + RenderSettings::Dump(app));
				}
				else if (args.size() < 3) {
					response_lines.push_back("ERR usage: render [<key> <value>]");
				}
				else if (RenderSettings::Set(app, args[1], args[2], error)) {
					response_lines.push_back("OK " + RenderSettings::Dump(app));
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "rdoc_capture") {
				//Queues a RenderDoc capture of the frame currently being rendered
				//(commands execute pre-frame). The .rdc is finalized after present;
				//poll rdoc_last for its path.
				if (RenderDocIntegration::TriggerCapture()) {
					response_lines.push_back("OK capture queued, use rdoc_last for the file path");
				}
				else {
					response_lines.push_back("ERR RenderDoc API not loaded (launch the editor with --renderdoc)");
				}
			}
			else if (cmd == "rdoc_last") {
				std::string path;
				if (!RenderDocIntegration::Available()) {
					response_lines.push_back("ERR RenderDoc API not loaded (launch the editor with --renderdoc)");
				}
				else if (RenderDocIntegration::GetLastCapturePath(path)) {
					response_lines.push_back("OK " + std::to_string(RenderDocIntegration::GetNumCaptures()) + " captures, last: " + path);
				}
				else {
					response_lines.push_back("ERR no captures yet");
				}
			}
			else if (cmd == "camera") {
				EditorCamera::Pose pose;
				if (!app.GetEditorCamera().GetPose(pose)) {
					response_lines.push_back("ERR no camera (load a level first)");
				}
				else {
					float3 d = pose.target - pose.position;
					float distance = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
					json j;
					j["position"] = { pose.position.x, pose.position.y, pose.position.z };
					j["world_position"] = { pose.world_position.x, pose.world_position.y, pose.world_position.z };
					j["target"] = { pose.target.x, pose.target.y, pose.target.z };
					j["rotation_deg"] = { DirectX::XMConvertToDegrees(pose.rotation.x),
						DirectX::XMConvertToDegrees(pose.rotation.y), DirectX::XMConvertToDegrees(pose.rotation.z) };
					j["distance"] = distance;
					response_lines.push_back("OK " + j.dump());
				}
			}
			else if (cmd == "camera_pos" || cmd == "camera_target" || cmd == "camera_rot") {
				float3 v;
				if (!ParseFloat3(args, 1, v)) {
					response_lines.push_back("ERR usage: " + cmd + " <x> <y> <z>" +
						(cmd == "camera_rot" ? " (pitch/yaw/roll degrees)" : ""));
				}
				else {
					if (cmd == "camera_rot") {
						v = { DirectX::XMConvertToRadians(v.x), DirectX::XMConvertToRadians(v.y), DirectX::XMConvertToRadians(v.z) };
					}
					const float3* position = (cmd == "camera_pos") ? &v : nullptr;
					const float3* target = (cmd == "camera_target") ? &v : nullptr;
					const float3* rotation = (cmd == "camera_rot") ? &v : nullptr;
					if (app.GetEditorCamera().SetPose(position, target, rotation)) {
						response_lines.push_back("OK");
					}
					else {
						response_lines.push_back("ERR no camera (load a level first)");
					}
				}
			}
			else if (cmd == "camera_orbit" || cmd == "camera_pan") {
				//Simulates a right-drag (orbit) / middle-drag (pan) of that many pixels.
				float v[2];
				if (!ParseFloats(args, 1, 2, v)) {
					response_lines.push_back("ERR usage: " + cmd + " <dx pixels> <dy pixels>");
				}
				else {
					if (cmd == "camera_orbit") {
						app.GetEditorCamera().Orbit(v[0], v[1]);
					}
					else {
						app.GetEditorCamera().Pan(v[0], v[1]);
					}
					response_lines.push_back("OK");
				}
			}
			else if (cmd == "camera_zoom") {
				//Simulates mouse-wheel steps (positive = toward the focus point).
				float steps;
				if (!ParseFloats(args, 1, 1, &steps)) {
					response_lines.push_back("ERR usage: camera_zoom <wheel steps>");
				}
				else {
					app.GetEditorCamera().Dolly(steps);
					response_lines.push_back("OK");
				}
			}
			else if (cmd == "camera_fly") {
				//Simulates WASD/QE flight by camera-relative world units.
				float v[3];
				if (!ParseFloats(args, 1, 3, v)) {
					response_lines.push_back("ERR usage: camera_fly <forward> <right> <up>");
				}
				else {
					app.GetEditorCamera().Fly(v[0], v[1], v[2]);
					response_lines.push_back("OK");
				}
			}
			else if (cmd == "undo" || cmd == "redo") {
				//Same history the Edit menu and Ctrl+Z/Ctrl+Y drive; the status
				//message carries the description of the step that was applied.
				bool ok = (cmd == "undo")
					? EditorHistory::Undo(state, error)
					: EditorHistory::Redo(state, error);
				if (ok) {
					response_lines.push_back("OK " + state.status_message);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "quit") {
				response_lines.push_back("OK quitting");
				app.Quit();
			}
			else if (cmd == "debug_crash") {
				//Deliberate access violation to exercise the crash pipeline
				//(CrashHandler report or an attached debugger). The process dies
				//here, so this command never writes a response - a driver waiting
				//on response.txt is expected to time out.
				*(volatile int*)0 = 42;
			}
			else {
				response_lines.push_back("ERR unknown command: " + cmd);
			}
		}

		void ProcessCommands(EditorState& state, SceneEditorApp& app)
		{
			if (!enabled || batch_open) {
				return;
			}
			fs::path command_file = root_dir / "command.txt";
			std::error_code ec;
			if (!fs::exists(command_file, ec)) {
				return;
			}
			std::ifstream in(command_file);
			if (!in.is_open()) {
				//The driver may still be writing; retry next frame.
				return;
			}
			std::vector<std::string> lines;
			std::string line;
			while (std::getline(in, line)) {
				if (!line.empty() && line.back() == '\r') {
					line.pop_back();
				}
				if (!line.empty()) {
					lines.push_back(line);
				}
			}
			in.close();
			fs::remove(command_file, ec);

			batch_open = true;
			response_lines.clear();
			pending_screenshots.clear();
			for (const auto& l : lines) {
				Execute(l, state, app);
			}
		}

		void OnFrameEnd(EditorState& state, SceneEditorApp& app)
		{
			if (!enabled || !batch_open) {
				return;
			}
			for (const auto& shot : pending_screenshots) {
				std::string error;
				if (app.CaptureBackBuffer(shot.path, error)) {
					response_lines[shot.response_index] = "OK " + shot.path;
				}
				else {
					response_lines[shot.response_index] = "ERR " + error;
				}
			}
			pending_screenshots.clear();

			//Write-then-rename so the driver never observes a half-written response.
			fs::path tmp = root_dir / "response.tmp";
			fs::path final_path = root_dir / "response.txt";
			{
				std::ofstream out(tmp.string(), std::ios::trunc);
				for (const auto& l : response_lines) {
					out << l << "\n";
				}
			}
			MoveFileExA(tmp.string().c_str(), final_path.string().c_str(), MOVEFILE_REPLACE_EXISTING);

			response_lines.clear();
			batch_open = false;
		}

	}
}
