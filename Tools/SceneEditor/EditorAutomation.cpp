#include "EditorAutomation.h"
#include "EditorHistory.h"
#include "ProjectBrowser.h"
#include "Inspector.h"
#include "SelectionGizmo.h"
#include "AssetBrowser.h"
#include "MaterialPanel.h"
#include "MultiMaterialPanel.h"
#include "MaskPaint.h"
#include "MeshOps.h"
#include "TemplatePanel.h"
#include "Outliner.h"
#include "EntityOps.h"
#include "ComponentOps.h"
#include "Selection.h"
#include "RenderSettings.h"
#include "RenderDocIntegration.h"
#include "ShaderReload.h"

#include <Windows.h>
#include <Components/Base.h>
#include <Components/Physics.h>
#include <Systems/RenderSystem.h>
#include <Core/Json.h>
#include <Core/SplatCloud.h>
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
			j["mask_paint_brush_mode"] = state.mask_paint_brush_mode;
			j["grid_snap_enabled"] = state.grid_snap_enabled;
			j["grid_size"] = state.grid_size;
			j["grid_rotation_step_degrees"] = state.grid_rotation_step_degrees;
			j["grid_scale_step"] = state.grid_scale_step;
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
			j["unsaved_templates"] = (int)(state.dirty_templates.size() +
				state.removed_templates.size());
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
				//In pick order, so the first line is the root a composed template would
				//be built around - marked, because the order is the whole information.
				const std::vector<std::string> names = Selection::Names(state);
				for (size_t i = 0; i < names.size(); ++i) {
					response_lines.push_back(names[i] +
						((i == 0 && names.size() > 1) ? " [root]" : ""));
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
			else if (cmd == "lod_info") {
				//What each selected entity's mesh declares as its level-of-detail
				//chain, and which level it is being drawn at right now. The second
				//half is the point: the switch happens on the render thread from a
				//coverage nothing else reports, so without a readout the only evidence
				//of it is a silhouette changing in a screenshot - and the coarse levels
				//of a well-made chain are meant not to be visible.
				Coordinator* c = state.world->GetCoordinator();
				if (c == nullptr) {
					response_lines.push_back("ERR no coordinator");
				}
				else if (state.selected_entities.empty()) {
					response_lines.push_back("ERR nothing selected");
				}
				else {
					response_lines.push_back("OK " + std::to_string(Selection::Count(state)) + " selected");
					for (Entity e : state.selected_entities) {
						if (!c->ContainsComponent<Base>(e) || !c->ContainsComponent<Mesh>(e)) {
							continue;
						}
						const std::string& name = c->GetComponent<Base>(e).name;
						Mesh& mesh = c->GetComponent<Mesh>(e);
						Core::MeshData* data = mesh.GetData();
						if (data == nullptr) {
							response_lines.push_back(name + " no mesh data");
							continue;
						}
						std::ostringstream os;
						os << name << " mesh=" << data->name
							<< " mode=" << (data->lod_mode == Core::MeshData::LOD_DISTANCE ?
								"distance" : "auto")
							<< " bias=" << data->lod_bias
							<< " enabled=" << (mesh.lod_enabled ? "1" : "0")
							<< " levels=" << data->lods.size()
							<< " current=" << mesh.current_lod
							//The three DrawIndexed arguments, so a test can prove the
							//selection reached the draw call and not just the readout.
							<< " index_count=" << mesh.index_count
							<< " index_offset=" << mesh.index_offset
							<< " vertex_offset=" << mesh.vertex_offset;
						//What the ray tracers trace this entity against, which is not
						//what is drawn: every ray takes the coarsest level in the chain,
						//whatever is on screen. Reported because it is invisible in a
						//screenshot - a reflection tracing the wrong geometry still looks
						//like a reflection.
						{
							const int levels = (int)data->lods.size();
							os << " trace_lod=" << (levels > 0 ? levels - 1 : 0);
						}
						response_lines.push_back(os.str());
						for (size_t i = 0; i < data->lods.size(); ++i) {
							const Core::MeshData::MeshLod& lod = data->lods[i];
							std::ostringstream ls;
							ls << "  lod" << i << " mesh="
								<< (lod.mesh != nullptr ? lod.mesh->name : std::string("(null)"))
								<< " ratio=" << lod.ratio
								<< " distance=" << lod.distance
								<< " vertices=" << (lod.mesh != nullptr ? lod.mesh->vertexCount : 0);
							response_lines.push_back(ls.str());
						}
					}
				}
			}
			else if (cmd == "rt_info") {
				//What the ray tracers were last handed, in triangle indices summed over
				//the objects they were given. This is the one place the level of detail
				//selection is observable: a reflection tracing the wrong geometry still
				//looks like a reflection, and on a scene whose GI cost is its denoiser
				//the timings do not move either.
				Systems::RenderSystem* rs =
					(state.world != nullptr) ? state.world->GetSystem<Systems::RenderSystem>().get() : nullptr;
				if (rs == nullptr) {
					response_lines.push_back("ERR no render system");
				}
				else {
					const Systems::RenderSystem::RtGeometryStats stats = rs->GetRtGeometryStats();
					std::ostringstream os;
					os << "OK objects=" << stats.objects
						<< " full_indices=" << stats.full_indices
						<< " traced_indices=" << stats.traced_indices;
					response_lines.push_back(os.str());
				}
			}
			else if (cmd == "gi_cache_info") {
				//Occupancy of the world radiance cache. The cache is deliberately
				//invisible in a normal frame - a lookup that finds nothing falls back
				//to what the screen-space pass always did - so this and the
				//`gi_cache` debug buffer are the only two ways to see it at all.
				//
				//The counters lag the current frame by two (they are read back
				//without stalling the pipeline), so a test that changes something and
				//reads this immediately is reading the state from before the change.
				Systems::RenderSystem* rs =
					(state.world != nullptr) ? state.world->GetSystem<Systems::RenderSystem>().get() : nullptr;
				if (rs == nullptr) {
					response_lines.push_back("ERR no render system");
				}
				else {
					const Systems::RenderSystem::RadianceCacheStats stats = rs->GetRadianceCacheStats();
					std::ostringstream os;
					os << "OK live=" << stats.live
						<< " touched=" << stats.touched
						<< " evicted=" << stats.evicted
						<< " deposits=" << stats.deposits
						<< " dropped=" << stats.dropped
						<< " hits=" << stats.hits
						<< " misses=" << stats.misses
						<< " entries=" << stats.entries;
					response_lines.push_back(os.str());
				}
			}
			else if (cmd == "splat_info") {
				//What the splat binning did last frame. A cloud over per-tile capacity
				//renders a plausible surface with parts of it missing rather than
				//failing, so `max_per_tile` against `capacity` is the only way to tell
				//"the capacity is enough" from "the capacity is not" - and
				//`overflow_tiles` at anything but 0 means splats were dropped.
				//
				//Two frames stale, like gi_cache_info, and asking is what turns the
				//readback on: poll it rather than reading it once after a change.
				Systems::RenderSystem* rs =
					(state.world != nullptr) ? state.world->GetSystem<Systems::RenderSystem>().get() : nullptr;
				if (rs == nullptr) {
					response_lines.push_back("ERR no render system");
				}
				else {
					const Systems::RenderSystem::SplatStats stats = rs->GetSplatStats();
					std::ostringstream os;
					os << "OK tiles_used=" << stats.tiles_used
						<< " max_per_tile=" << stats.max_per_tile
						<< " total_binned=" << stats.total_binned
						<< " dropped=" << stats.dropped
						<< " capacity=" << stats.capacity << " tiles_rastered=" << stats.tiles_rastered << " pixels_written=" << stats.pixels_written
						<< " entries_walked=" << stats.entries_walked;
					response_lines.push_back(os.str());
				}
			}
			else if (cmd == "generate_lod") {
				//Builds a coarser level out of the selected entity's mesh and adds it
				//to the chain (MeshOps::GenerateLod), which is the Components panel's
				//"Generate level" button. The percentage is optional and defaults to
				//the same suggestion the panel offers - half of the coarsest level
				//there.
				Coordinator* c = state.world->GetCoordinator();
				if (c == nullptr) {
					response_lines.push_back("ERR no coordinator");
				}
				else if (state.selected_entity == INVALID_ENTITY_ID) {
					response_lines.push_back("ERR nothing selected");
				}
				else if (!c->ContainsComponent<Mesh>(state.selected_entity)) {
					response_lines.push_back("ERR selected entity has no Mesh");
				}
				else {
					const std::string name = c->GetComponent<Base>(state.selected_entity).name;
					Core::MeshData* data = c->GetComponent<Mesh>(state.selected_entity).GetData();
					float ratio = MeshOps::SuggestedRatio(data);
					bool ok = true;
					if (args.size() > 1) {
						try {
							ratio = std::stof(args[1]) / 100.0f;
						}
						catch (...) {
							response_lines.push_back("ERR usage: generate_lod [<percent>]");
							ok = false;
						}
					}
					if (ok) {
						std::string generated;
						std::string error;
						if (MeshOps::GenerateLod(state, name, ratio, generated, error)) {
							Core::MeshData* lod = state.world->GetMeshes().Get(generated);
							std::ostringstream os;
							os << "OK " << generated
								<< " vertices=" << (lod != nullptr ? lod->vertexCount : 0)
								<< " indices=" << (lod != nullptr ? lod->indexCount : 0)
								<< " source=" << (data != nullptr ? data->name : std::string())
								<< " source_vertices=" << (data != nullptr ? data->vertexCount : 0);
							response_lines.push_back(os.str());
						}
						else {
							response_lines.push_back("ERR " + error);
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
			//World-aligned texture tiling (see Material.h's WORLD_UV_ENABLED_FLAG and
			//MainRenderPS.hlsli). Goes through the same snapshot/undo path a panel
			//edit would (MaterialOps::GetSnapshot/ApplySnapshot/RecordEdit), since
			//there is no automation command yet for editing an arbitrary material
			//property and this is the smallest reuse of that machinery.
			else if (cmd == "set_material_world_uv") {
				if (args.size() < 4) {
					response_lines.push_back("ERR usage: set_material_world_uv <material name> <0|1> <scale>");
				}
				else {
					MaterialOps::MaterialSnapshot before;
					if (!MaterialOps::GetSnapshot(state, args[1], before)) {
						response_lines.push_back("ERR material not found: " + args[1]);
					}
					else {
						float scale = 0.0f;
						if (!ParseFloats(args, 3, 1, &scale)) {
							response_lines.push_back("ERR scale must be a number");
						}
						else {
							MaterialOps::MaterialSnapshot after = before;
							if (args[2] != "0") { after.props.flags |= WORLD_UV_ENABLED_FLAG; }
							else { after.props.flags &= ~WORLD_UV_ENABLED_FLAG; }
							after.props.world_uv_scale = scale;
							if (MaterialOps::ApplySnapshot(state, args[1], after, error)) {
								MaterialOps::RecordEdit(state, args[1], before);
								response_lines.push_back("OK");
							}
							else {
								response_lines.push_back("ERR " + error);
							}
						}
					}
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
			//--- Shader hot reload (see ShaderReload.h). Recompiles the engine's .hlsl
			//sources into the running editor; the same code the Shaders menu and F5 run.
			else if (cmd == "reload_shaders") {
				//Asynchronous by necessity - a compile can take half a minute and the
				//editor must keep pumping messages (see ShaderReload.h). The command
				//answers as soon as the work is queued; poll shader_reload_status for
				//the outcome.
				const std::string what = (args.size() > 1) ? args[1] : "changed";
				if (what == "changed" || what == "all") {
					const int queued = ShaderReload::ReloadAll(state, what == "changed");
					response_lines.push_back("OK queued " + std::to_string(queued) + " shader(s)");
				}
				else if (ShaderReload::ReloadOne(state, what, error)) {
					response_lines.push_back("OK queued " + what);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "shader_reload_status") {
				const int pending = ShaderReload::Pending();
				const std::vector<std::string>& errors = ShaderReload::LastErrors();
				response_lines.push_back(std::string("OK ") + (pending > 0 ? "busy" : "idle") +
					" pending=" + std::to_string(pending) +
					" last=" + (ShaderReload::LastReport().empty() ? "none" : ShaderReload::LastReport()) +
					" errors=" + std::to_string(errors.size()));
				for (const std::string& err : errors) {
					//One line each, and the compiler's own message can be several lines
					//long - flatten it so the channel stays line oriented.
					std::string flat = err;
					std::replace(flat.begin(), flat.end(), '\n', ' ');
					std::replace(flat.begin(), flat.end(), '\r', ' ');
					response_lines.push_back(flat);
				}
			}
			else if (cmd == "shaders_loaded") {
				const std::vector<std::string> names = Core::ShaderFactory::Get()->GetShaderNames();
				response_lines.push_back("OK " + std::to_string(names.size()) + " shaders loaded");
				for (const std::string& name : names) {
					Core::ISimpleShader* shader = Core::ShaderFactory::Get()->Find(name);
					const std::string source = (shader != nullptr) ? shader->GetSourcePath() : std::string();
					response_lines.push_back(name + "=" + (source.empty() ? "<no source>" : source));
				}
			}
			else if (cmd == "shader_sources") {
				const std::string action = (args.size() > 1) ? args[1] : "";
				if (action.empty()) {
					std::string report = ShaderReload::SourcesReport();
					//One response line per folder: the channel is line oriented and a
					//driver splits on newlines.
					size_t start = 0;
					bool first = true;
					while (start <= report.size()) {
						size_t nl = report.find('\n', start);
						std::string line = report.substr(start, (nl == std::string::npos) ? std::string::npos : nl - start);
						while (!line.empty() && (line.front() == ' ')) {
							line.erase(line.begin());
						}
						response_lines.push_back(first ? ("OK " + line) : line);
						first = false;
						if (nl == std::string::npos) {
							break;
						}
						start = nl + 1;
					}
				}
				else if (args.size() < 3) {
					response_lines.push_back("ERR usage: shader_sources [add|remove <folder>]");
				}
				else if (action == "add") {
					if (ShaderReload::AddSourceFolder(args[2], error)) {
						response_lines.push_back("OK " + ShaderReload::LastReport());
					}
					else {
						response_lines.push_back("ERR " + error);
					}
				}
				else if (action == "remove") {
					if (ShaderReload::RemoveSourceFolder(args[2], error)) {
						response_lines.push_back("OK removed " + args[2]);
					}
					else {
						response_lines.push_back("ERR " + error);
					}
				}
				else {
					response_lines.push_back("ERR usage: shader_sources [add|remove <folder>]");
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
			//--- Multi-materials (see MultiMaterialPanel.h). Layer stacks that live in
			//the same .mat files as plain materials, share save_materials with them, and
			//are attached to a material by name - never to an entity.
			else if (cmd == "multi_materials") {
				const std::vector<std::string> names = MultiMaterialOps::List(state);
				response_lines.push_back("OK " + std::to_string(names.size()) + " multi-materials");
				for (const std::string& name : names) {
					std::ostringstream os;
					const std::string file = state.world->GetMultiMaterialOrigin(name);
					Core::MultiMaterialData* mm = state.world->GetMultiMaterial(name);
					os << name << " file=" << (file.empty() ? "(none)" : file)
						<< " layers=" << (mm != nullptr ? mm->layers.size() : 0)
						<< " materials=" << MultiMaterialOps::FindMaterials(state, name).size();
					if (state.dirty_material_files.count(file) != 0) {
						os << " unsaved";
					}
					if (name == state.selected_multi_material) {
						os << " selected";
					}
					response_lines.push_back(os.str());
				}
			}
			else if (cmd == "select_multi_material") {
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: select_multi_material <name>");
				}
				else if (state.world->GetMultiMaterial(args[1]) == nullptr) {
					response_lines.push_back("ERR multi-material not found: " + args[1]);
				}
				else {
					state.selected_multi_material = args[1];
					state.selected_multi_material_layer = 0;
					state.show_material_panel = true;
					response_lines.push_back("OK selected multi-material: " + args[1]);
				}
			}
			else if (cmd == "create_multi_material") {
				if (args.size() < 3) {
					response_lines.push_back("ERR usage: create_multi_material <name> <mat file>");
				}
				else if (MultiMaterialOps::Create(state, args[1], args[2], error)) {
					response_lines.push_back("OK multi-material created: " + args[1] + " in " + args[2]);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "remove_multi_material") {
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: remove_multi_material <name>");
				}
				else if (MultiMaterialOps::Remove(state, args[1], error)) {
					response_lines.push_back("OK multi-material removed: " + args[1]);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "set_multi_material") {
				//`set_multi_material <material> none` detaches.
				if (args.size() < 3) {
					response_lines.push_back("ERR usage: set_multi_material <material name> <multi-material|none>");
				}
				else {
					const std::string target = (args[2] == "none") ? std::string() : args[2];
					if (MultiMaterialOps::Assign(state, args[1], target, error)) {
						response_lines.push_back("OK " + args[1] + " -> " + (target.empty() ? "(none)" : target));
					}
					else {
						response_lines.push_back("ERR " + error);
					}
				}
			}
			else if (cmd == "add_layer") {
				if (args.size() < 3) {
					response_lines.push_back("ERR usage: add_layer <multi-material> <source material>");
				}
				else if (MultiMaterialOps::AddLayer(state, args[1], args[2], error)) {
					response_lines.push_back("OK layer added to " + args[1]);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "remove_layer") {
				if (args.size() < 3) {
					response_lines.push_back("ERR usage: remove_layer <multi-material> <layer index>");
				}
				else {
					int index = -1;
					try { index = std::stoi(args[2]); } catch (...) {}
					if (MultiMaterialOps::RemoveLayer(state, args[1], index, error)) {
						response_lines.push_back("OK layer removed: " + args[1] + " " + args[2]);
					}
					else {
						response_lines.push_back("ERR " + error);
					}
				}
			}
			else if (cmd == "move_layer") {
				if (args.size() < 4) {
					response_lines.push_back("ERR usage: move_layer <multi-material> <layer index> <delta>");
				}
				else {
					int index = -1, delta = 0;
					try { index = std::stoi(args[2]); delta = std::stoi(args[3]); } catch (...) {}
					if (MultiMaterialOps::MoveLayer(state, args[1], index, delta, error)) {
						response_lines.push_back("OK layer moved: " + args[1] + " " + args[2] + " by " + args[3]);
					}
					else {
						response_lines.push_back("ERR " + error);
					}
				}
			}
			else if (cmd == "layer") {
				//Readback of one layer's authored fields plus what Rebuild derived from
				//them (flags, whether its material resolved) - the multi-material analogue
				//of the `component` command.
				if (args.size() < 3) {
					response_lines.push_back("ERR usage: layer <multi-material> <layer index>");
				}
				else {
					int index = -1;
					try { index = std::stoi(args[2]); } catch (...) {}
					const json value = MultiMaterialOps::LayerJson(state, args[1], index);
					if (value.empty()) {
						response_lines.push_back("ERR no layer " + args[2] + " in " + args[1]);
					}
					else {
						response_lines.push_back("OK " + args[1] + " layer " + args[2]);
						response_lines.push_back(value.dump());
					}
				}
			}
			else if (cmd == "set_layer") {
				//Single-quoted JSON, same rule as set_component: the tokenizer strips
				//double quotes, so a double-quoted object never arrives intact.
				if (args.size() < 4) {
					response_lines.push_back("ERR usage: set_layer <multi-material> <layer index>"
						" <json object, single-quoted keys/values>");
				}
				else {
					int index = -1;
					try { index = std::stoi(args[2]); } catch (...) {}
					std::string source = args[3];
					std::replace(source.begin(), source.end(), '\'', '"');
					json value;
					bool parsed = true;
					try {
						value = json::parse(source);
					}
					catch (const std::exception& ex) {
						parsed = false;
						response_lines.push_back(std::string("ERR bad JSON: ") + ex.what());
					}
					if (parsed && MultiMaterialOps::SetLayer(state, args[1], index, value, error)) {
						response_lines.push_back("OK " + args[1] + " layer " + args[2] + " = " +
							MultiMaterialOps::LayerJson(state, args[1], index).dump());
					}
					else if (parsed) {
						response_lines.push_back("ERR " + error);
					}
				}
			}
			else if (cmd == "set_multi_material_params") {
				if (args.size() < 3) {
					response_lines.push_back("ERR usage: set_multi_material_params <multi-material>"
						" <json object, single-quoted keys/values>");
				}
				else {
					std::string source = args[2];
					std::replace(source.begin(), source.end(), '\'', '"');
					json value;
					bool parsed = true;
					try {
						value = json::parse(source);
					}
					catch (const std::exception& ex) {
						parsed = false;
						response_lines.push_back(std::string("ERR bad JSON: ") + ex.what());
					}
					if (parsed && MultiMaterialOps::SetParams(state, args[1], value, error)) {
						response_lines.push_back("OK " + args[1] + " params updated");
					}
					else if (parsed) {
						response_lines.push_back("ERR " + error);
					}
				}
			}
			//--- Mask painting (see MaskPaint.h). One session at a time, targeting a
			//single multi-material layer; paint_mask dabs it in the layer's own UV space.
			else if (cmd == "paint_mask_begin") {
				if (args.size() < 3) {
					response_lines.push_back("ERR usage: paint_mask_begin <multi-material> <layer index> [canvas size]");
				}
				else {
					int index = -1;
					try { index = std::stoi(args[2]); } catch (...) {}
					int size = 1024;
					if (args.size() >= 4) {
						try { size = std::stoi(args[3]); } catch (...) {}
					}
					if (MaskPaint::Begin(state, args[1], index, error, size)) {
						response_lines.push_back("OK painting " + args[1] + " layer " + args[2] +
							" (" + std::to_string(MaskPaint::Width()) + "x" +
							std::to_string(MaskPaint::Height()) + ")");
					}
					else {
						response_lines.push_back("ERR " + error);
					}
				}
			}
			else if (cmd == "paint_mask") {
				if (!MaskPaint::Active()) {
					response_lines.push_back("ERR no paint session is open (paint_mask_begin first)");
				}
				else {
					float args4[4];
					if (!ParseFloats(args, 1, 4, args4)) {
						response_lines.push_back("ERR usage: paint_mask <u> <v> <radius> <strength>"
							" (uv 0..1, radius in uv units, strength -1..1)");
					}
					else {
						MaskPaint::PaintStroke(args4[0], args4[1], args4[2], args4[3]);
						response_lines.push_back("OK dab at " + args[1] + "," + args[2]);
					}
				}
			}
			else if (cmd == "paint_mask_commit") {
				if (MaskPaint::Commit(state, error)) {
					response_lines.push_back("OK mask committed");
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "paint_mask_cancel") {
				MaskPaint::Cancel(state);
				response_lines.push_back("OK paint session cancelled");
			}
			//Simulates the viewport brush (MaskPaint::TryPaintAtScreenPoint) at a
			//screen pixel instead of a literal mouse drag - a simulated "drag" is
			//just several of these at moving x,y, the same philosophy as
			//camera_orbit/camera_pan simulating a mouse drag for the camera.
			else if (cmd == "paint_stroke_screen") {
				if (!MaskPaint::Active()) {
					response_lines.push_back("ERR no paint session is open (paint_mask_begin first)");
				}
				else {
					float args4[4];
					if (!ParseFloats(args, 1, 4, args4)) {
						response_lines.push_back("ERR usage: paint_stroke_screen <x> <y> <radius> <strength>"
							" (x,y in screen pixels, radius in mesh UV units, strength -1..1)");
					}
					else {
						ImVec2 mouse(args4[0], args4[1]);
						if (MaskPaint::TryPaintAtScreenPoint(state, mouse, ImGui::GetIO().DisplaySize,
							args4[2], args4[3], error)) {
							response_lines.push_back("OK dab at " + args[1] + "," + args[2]);
						}
						else {
							response_lines.push_back("ERR " + error);
						}
					}
				}
			}
			else if (cmd == "set_mask_paint_brush_mode") {
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: set_mask_paint_brush_mode <0|1>");
				}
				else {
					state.mask_paint_brush_mode = (args[1] != "0");
					response_lines.push_back("OK");
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
			//--- Grid snapping (see GridSnap.h / SelectionGizmo.cpp). Toggling is also
			//scriptable for free via `menu "View/Grid Snap"` once registered; these two
			//exist for setting the numeric step values, which have no menu equivalent.
			else if (cmd == "set_grid_snap") {
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: set_grid_snap <0|1>");
				}
				else {
					state.grid_snap_enabled = (args[1] != "0");
					response_lines.push_back("OK");
				}
			}
			else if (cmd == "set_grid_size") {
				float size = 0.0f;
				if (!ParseFloats(args, 1, 1, &size)) {
					response_lines.push_back("ERR usage: set_grid_size <size> [rotation_deg] [scale_step]");
				}
				else {
					state.grid_size = size;
					if (args.size() >= 3) {
						try { state.grid_rotation_step_degrees = std::stof(args[2]); } catch (...) {}
					}
					if (args.size() >= 4) {
						try { state.grid_scale_step = std::stof(args[3]); } catch (...) {}
					}
					response_lines.push_back("OK");
				}
			}
			//Applies one gizmo interaction (translate/rotate/scale) to the current
			//selection without a literal mouse drag - see SelectionGizmo::SimulateDrag.
			//Exercises the exact same math and grid-snap path a real drag does, the
			//same way camera_orbit/camera_pan simulate a mouse drag for the camera.
			else if (cmd == "simulate_gizmo_drag") {
				float amount = 0.0f;
				if (args.size() < 4) {
					response_lines.push_back("ERR usage: simulate_gizmo_drag <translate|rotate|scale> <x|y|z|uniform> <amount>"
						" (amount is world units for translate, degrees for rotate, a factor for scale)");
				}
				else if (args[1] != "translate" && args[1] != "rotate" && args[1] != "scale") {
					response_lines.push_back("ERR unknown mode: " + args[1] + " (translate|rotate|scale)");
				}
				else if (args[2] != "x" && args[2] != "y" && args[2] != "z" && args[2] != "uniform") {
					response_lines.push_back("ERR unknown axis: " + args[2] + " (x|y|z|uniform)");
				}
				else if (!ParseFloats(args, 3, 1, &amount)) {
					response_lines.push_back("ERR amount must be a number");
				}
				else {
					GizmoMode mode = (args[1] == "translate") ? GizmoMode::Translate
						: (args[1] == "rotate") ? GizmoMode::Rotate : GizmoMode::Scale;
					int axis = (args[2] == "x") ? 0 : (args[2] == "y") ? 1 : (args[2] == "z") ? 2 : -1;
					if (mode == GizmoMode::Rotate) {
						amount = DirectX::XMConvertToRadians(amount);
					}
					SelectionGizmo::SimulateDrag(state, mode, axis, amount);
					response_lines.push_back("OK");
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
			else if (cmd == "component") {
				//The serialized state of one component, which is both what the Components
				//panel edits and the shape set_component expects back.
				if (args.size() < 3) {
					response_lines.push_back("ERR usage: component <entity name> <Component>");
				}
				else {
					const nlohmann::json value = ComponentOps::GetValue(state, args[1], args[2]);
					if (value.empty()) {
						response_lines.push_back("ERR " + args[1] + " has no " + args[2] +
							" (or it does not serialize)");
					}
					else {
						response_lines.push_back("OK " + args[1] + " " + args[2]);
						response_lines.push_back(value.dump());
					}
				}
			}
			else if (cmd == "set_component") {
				//Edits the fields of a component the entity already has - the automation
				//form of every picker and drag in the Components panel. Written with
				//single quotes for the same reason template_set is: the tokenizer strips
				//double quotes, so a double-quoted JSON object never arrives intact.
				if (args.size() < 4) {
					response_lines.push_back("ERR usage: set_component <entity name>"
						" <Component> <json object, single-quoted keys/values>");
				}
				else {
					std::string source = args[3];
					std::replace(source.begin(), source.end(), '\'', '"');
					json value;
					bool parsed = true;
					try {
						value = json::parse(source);
					}
					catch (const std::exception& ex) {
						parsed = false;
						response_lines.push_back(std::string("ERR bad JSON: ") + ex.what());
					}
					if (parsed && !value.is_object()) {
						parsed = false;
						response_lines.push_back("ERR component value must be a JSON object");
					}
					if (parsed) {
						std::lock_guard<std::recursive_mutex> lock(Core::physics_mutex);
						if (ComponentOps::SetValue(state, args[1], args[2], value, error)) {
							response_lines.push_back("OK " + args[1] + " " + args[2] + " = " +
								ComponentOps::GetValue(state, args[1], args[2]).dump());
						}
						else {
							response_lines.push_back("ERR " + error);
						}
					}
				}
			}
			else if (cmd == "animations") {
				//What the entity's mesh can play, and what it is playing now - the list
				//behind the Components panel's animation picker.
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: animations <entity name>");
				}
				else {
					Coordinator* c = state.world->GetCoordinator();
					Entity e = (c != nullptr) ? c->GetEntityByName(args[1]) : INVALID_ENTITY_ID;
					if (e == INVALID_ENTITY_ID || !c->ContainsComponent<Mesh>(e)) {
						response_lines.push_back("ERR no mesh entity named '" + args[1] + "'");
					}
					else {
						Mesh& mesh = c->GetComponent<Mesh>(e);
						Core::MeshData* data = mesh.GetData();
						const std::string mesh_name = (data != nullptr) ? data->name : std::string();
						const std::vector<std::string> animations =
							state.world->GetMeshAnimations(mesh_name);
						const std::string current = mesh.GetCurrentAnimationName();
						response_lines.push_back("OK " + std::to_string(animations.size()) +
							" animations on mesh " + mesh_name + ", current: " +
							(current.empty() ? "(none)" : current));
						for (const std::string& animation : animations) {
							response_lines.push_back(animation +
								(animation == current ? " [current]" : ""));
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
				AssetBrowser::EnsureAssetsScanned(state);
				response_lines.push_back("OK " + std::to_string(state.templates.size()) + " templates");
				for (auto& t : state.templates) {
					std::ostringstream os;
					os << t.name
						<< (TemplateOps::IsInline(state, t.name) ? " in=level" : " in=file");
					if (state.dirty_templates.count(t.name) != 0) {
						os << " unsaved";
					}
					if (t.name == state.selected_template) {
						os << " [selected]";
					}
					response_lines.push_back(os.str());
				}
			}
			else if (cmd == "list_models") {
				//The other half of the asset list: what has been imported, and what each
				//file brought with it. Nothing here is placeable - create_template_from_model
				//is the step between a model and an object.
				AssetBrowser::EnsureAssetsScanned(state);
				response_lines.push_back("OK " + std::to_string(state.models.size()) + " models");
				for (const auto& m : state.models) {
					std::ostringstream os;
					os << m.name;
					const World::ModelAssets* assets = state.world->GetModelAssets(m.name);
					if (assets != nullptr) {
						os << " meshes=" << assets->meshes.size()
							<< " materials=" << assets->materials.size();
						size_t clips = 0;
						for (const std::string& set : assets->animation_sets) {
							clips += state.world->GetAnimationSetClips(set).size();
						}
						os << " animations=" << clips;
						//A .ply contributes a splat cloud and none of the three above, so
						//without this a perfectly good import reads as
						//"meshes=0 materials=0 animations=0" - indistinguishable from a
						//file that failed to load.
						if (!assets->splat_clouds.empty()) {
							size_t splats = 0;
							for (const std::string& cloud : assets->splat_clouds) {
								if (const Core::SplatCloudData* c =
									state.world->GetSplatClouds().Get(cloud)) {
									splats += c->Count();
								}
							}
							os << " splat_clouds=" << assets->splat_clouds.size()
								<< " splats=" << splats;
						}
					}
					if (m.name == state.selected_model) {
						os << " [selected]";
					}
					response_lines.push_back(os.str());
				}
			}
			else if (cmd == "model_info") {
				AssetBrowser::EnsureAssetsScanned(state);
				const World::ModelAssets* assets = (args.size() >= 2)
					? state.world->GetModelAssets(args[1]) : nullptr;
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: model_info <model name>");
				}
				else if (assets == nullptr) {
					response_lines.push_back("ERR unknown model: " + args[1]);
				}
				else {
					response_lines.push_back("OK model " + args[1] + " from " + assets->file);
					for (const std::string& mesh : assets->meshes) {
						response_lines.push_back("mesh " + mesh);
					}
					for (const std::string& material : assets->materials) {
						response_lines.push_back("material " + material);
					}
					for (const std::string& set : assets->animation_sets) {
						for (const std::string& clip : state.world->GetAnimationSetClips(set)) {
							response_lines.push_back("animation " + clip);
						}
					}
				}
			}
			else if (cmd == "import_model") {
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: import_model <path to .fbx> [name]");
				}
				else if (AssetBrowser::ImportModel(state, args[1],
					args.size() > 2 ? args[2] : std::string(), error)) {
					response_lines.push_back("OK " + state.status_message);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "remove_model") {
				AssetBrowser::EnsureAssetsScanned(state);
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: remove_model <model name>");
				}
				else if (AssetBrowser::RemoveModel(state, args[1], error)) {
					response_lines.push_back("OK " + state.status_message);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "create_template_from_model") {
				AssetBrowser::EnsureAssetsScanned(state);
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: create_template_from_model"
						" <model name> [template name]");
				}
				else {
					const std::string template_name = (args.size() >= 3) ? args[2]
						: TemplateOps::UniqueTemplateName(state, args[1]);
					if (TemplateOps::CreateFromModel(state, args[1], template_name, error)) {
						response_lines.push_back("OK " + state.status_message);
					}
					else {
						response_lines.push_back("ERR " + error);
					}
				}
			}
			else if (cmd == "select_model") {
				AssetBrowser::EnsureAssetsScanned(state);
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: select_model <name>");
				}
				else if (!state.world->IsModelLoaded(args[1])) {
					response_lines.push_back("ERR unknown model: " + args[1]);
				}
				else {
					state.selected_model = args[1];
					response_lines.push_back("OK");
				}
			}
			else if (cmd == "list_meshes") {
				//The mesh names a template's Mesh component can be pointed at, which is
				//what a script needs before it can call template_mesh.
				const std::vector<std::string> meshes = TemplateOps::ListMeshes(state);
				response_lines.push_back("OK " + std::to_string(meshes.size()) + " meshes");
				for (const std::string& mesh : meshes) {
					std::ostringstream os;
					os << mesh;
					const std::vector<std::string> animations = state.world->GetMeshAnimations(mesh);
					if (!animations.empty()) {
						os << " animations=";
						for (size_t i = 0; i < animations.size(); ++i) {
							os << (i == 0 ? "" : ",") << animations[i];
						}
					}
					response_lines.push_back(os.str());
				}
			}
			else if (cmd == "list_splat_clouds") {
				//The peer of list_meshes for the splat path: what the Components panel's
				//SplatCloud picker offers, and so what a script can point one at.
				const std::vector<std::string> clouds = TemplateOps::ListSplatClouds(state);
				response_lines.push_back("OK " + std::to_string(clouds.size()) + " splat clouds");
				for (const std::string& cloud : clouds) {
					std::ostringstream os;
					os << cloud;
					const Core::SplatCloudData* data = state.world->GetSplatClouds().Get(cloud);
					if (data != nullptr) {
						os << " splats=" << data->Count();
					}
					response_lines.push_back(os.str());
				}
			}
			else if (cmd == "create_template") {
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: create_template <name>");
				}
				else if (TemplateOps::CreateTemplate(state, args[1], error)) {
					response_lines.push_back("OK " + state.status_message);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "template_from_entity") {
				if (args.size() < 3) {
					response_lines.push_back("ERR usage: template_from_entity <entity name> <template name>");
				}
				else if (TemplateOps::CreateFromEntity(state, args[1], args[2], error)) {
					response_lines.push_back("OK " + state.status_message);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "duplicate_template") {
				if (args.size() < 3) {
					response_lines.push_back("ERR usage: duplicate_template <source> <new name>");
				}
				else if (TemplateOps::DuplicateTemplate(state, args[1], args[2], error)) {
					response_lines.push_back("OK " + state.status_message);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "remove_template") {
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: remove_template <name>");
				}
				else if (TemplateOps::RemoveTemplate(state, args[1], error)) {
					response_lines.push_back("OK " + state.status_message);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "template_info") {
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: template_info <name>");
				}
				else if (!state.world->IsTemplateLoaded(args[1])) {
					response_lines.push_back("ERR unknown template: " + args[1]);
				}
				else {
					const std::vector<std::string> components =
						TemplateOps::ListComponents(state, args[1]);
					response_lines.push_back("OK " + std::to_string(components.size()) +
						" components on template " + args[1]);
					for (const std::string& component : components) {
						response_lines.push_back(component + " " +
							TemplateOps::GetComponent(state, args[1], component).dump());
					}
				}
			}
			else if (cmd == "template_mesh" || cmd == "template_material") {
				if (args.size() < 3) {
					response_lines.push_back("ERR usage: " + cmd + " <template name> <asset name>");
				}
				else {
					const bool ok = (cmd == "template_mesh")
						? TemplateOps::SetMesh(state, args[1], args[2], error)
						: TemplateOps::SetMaterial(state, args[1], args[2], error);
					response_lines.push_back(ok ? ("OK " + args[1] + " -> " + args[2])
						: ("ERR " + error));
				}
			}
			else if (cmd == "template_animations") {
				//The template's own animation library: what this object can play, by the
				//names it knows them under.
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: template_animations <template name>");
				}
				else if (!TemplateOps::IsAuthored(state, args[1])) {
					response_lines.push_back("ERR unknown template: " + args[1]);
				}
				else {
					const std::vector<TemplateOps::TemplateClip> clips =
						TemplateOps::ListClips(state, args[1]);
					response_lines.push_back("OK " + std::to_string(clips.size()) +
						" animations on template " + args[1]);
					for (const TemplateOps::TemplateClip& clip : clips) {
						std::ostringstream os;
						os << clip.name << " clip=" << clip.clip
							<< " model=" << (clip.resolved ? clip.model : "(missing)");
						if (clip.is_default) {
							os << " [default]";
						}
						response_lines.push_back(os.str());
					}
				}
			}
			else if (cmd == "template_add_animation") {
				if (args.size() < 4) {
					response_lines.push_back("ERR usage: template_add_animation"
						" <template name> <name> <imported clip>");
				}
				else if (TemplateOps::AddClip(state, args[1], args[2], args[3], error)) {
					response_lines.push_back("OK " + args[1] + " " + args[2] + " -> " + args[3]);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "template_remove_animation") {
				if (args.size() < 3) {
					response_lines.push_back("ERR usage: template_remove_animation"
						" <template name> <name>");
				}
				else if (TemplateOps::RemoveClip(state, args[1], args[2], error)) {
					response_lines.push_back("OK " + args[1] + " - " + args[2]);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "template_rename_animation") {
				if (args.size() < 4) {
					response_lines.push_back("ERR usage: template_rename_animation"
						" <template name> <name> <new name>");
				}
				else if (TemplateOps::RenameClip(state, args[1], args[2], args[3], error)) {
					response_lines.push_back("OK " + args[1] + " " + args[2] + " -> " + args[3]);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "template_default_animation") {
				//An empty name is "stands still", matching the panel's "None by default".
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: template_default_animation"
						" <template name> [name] [loop 0|1] [speed]");
				}
				else {
					const std::string animation = (args.size() >= 3) ? args[2] : std::string();
					const bool loop = (args.size() >= 4) ? (args[3] != "0") : true;
					float speed = 1.0f;
					if (args.size() >= 5) {
						try { speed = std::stof(args[4]); }
						catch (...) { speed = 1.0f; }
					}
					if (TemplateOps::SetDefaultClip(state, args[1], animation, loop, speed, error)) {
						response_lines.push_back("OK " + args[1] + " default animation -> " +
							(animation.empty() ? "(none)" : animation));
					}
					else {
						response_lines.push_back("ERR " + error);
					}
				}
			}
			else if (cmd == "list_animations") {
				//Every clip the imported models offer, which is what a script picks from
				//when calling template_add_animation.
				AssetBrowser::EnsureAssetsScanned(state);
				const std::vector<TemplateOps::AvailableClip> clips =
					TemplateOps::ListAvailableClips(state);
				response_lines.push_back("OK " + std::to_string(clips.size()) + " animations");
				for (const TemplateOps::AvailableClip& clip : clips) {
					response_lines.push_back(clip.clip + " model=" + clip.model);
				}
			}
			else if (cmd == "template_add_component" || cmd == "template_remove_component") {
				if (args.size() < 3) {
					response_lines.push_back("ERR usage: " + cmd + " <template name> <Component>");
				}
				else {
					const bool ok = (cmd == "template_add_component")
						? TemplateOps::SetComponent(state, args[1], args[2],
							nlohmann::json::object(), error)
						: TemplateOps::RemoveComponent(state, args[1], args[2], error);
					response_lines.push_back(ok ? ("OK " + args[1] + " " + args[2])
						: ("ERR " + error));
				}
			}
			else if (cmd == "template_set") {
				//The general form behind the focused commands above: add-or-update one
				//component block from JSON.
				//
				//Written with single quotes, because the tokenizer treats a double quote
				//as an argument grouping character and strips it - so a double-quoted
				//JSON object never reaches here intact. Single quotes are translated
				//below, which also spares the caller from escaping quotes through the
				//shell that writes command.txt.
				if (args.size() < 4) {
					response_lines.push_back("ERR usage: template_set <template name>"
						" <Component> <json object, single-quoted keys/values>");
				}
				else {
					std::string source = args[3];
					std::replace(source.begin(), source.end(), '\'', '"');
					json value;
					bool parsed = true;
					try {
						value = json::parse(source);
					}
					catch (const std::exception& ex) {
						parsed = false;
						response_lines.push_back(std::string("ERR bad JSON: ") + ex.what());
					}
					if (parsed && !value.is_object()) {
						parsed = false;
						response_lines.push_back("ERR component value must be a JSON object");
					}
					if (parsed) {
						if (TemplateOps::SetComponent(state, args[1], args[2], value, error)) {
							response_lines.push_back("OK " + args[1] + " " + args[2] + " = " + value.dump());
						}
						else {
							response_lines.push_back("ERR " + error);
						}
					}
				}
			}
			else if (cmd == "template_parts") {
				//What a composed template is made of. Bones are reported too, since
				//picking one is the next thing a caller does.
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: template_parts <template name>");
				}
				else if (!TemplateOps::IsAuthored(state, args[1])) {
					response_lines.push_back("ERR unknown template: " + args[1]);
				}
				else {
					const std::vector<TemplateOps::TemplatePart> parts =
						TemplateOps::ListParts(state, args[1]);
					response_lines.push_back("OK " + std::to_string(parts.size()) +
						" parts on template " + args[1]);
					for (const TemplateOps::TemplatePart& part : parts) {
						std::ostringstream os;
						os << part.name << " is=" << part.template_name
							<< " attached_to=" << (!part.attach ? "(free)"
								: part.bone.empty() ? "root" : part.bone)
							<< " pos=" << part.position.x << "," << part.position.y << "," << part.position.z
							<< " scale=" << part.scale.x << "," << part.scale.y << "," << part.scale.z;
						response_lines.push_back(os.str());
					}
					const std::vector<std::string> bones = TemplateOps::ListRootBones(state, args[1]);
					if (!bones.empty()) {
						std::ostringstream os;
						os << "bones";
						for (const std::string& bone : bones) {
							os << " " << bone;
						}
						response_lines.push_back(os.str());
					}
				}
			}
			else if (cmd == "template_add_part") {
				if (args.size() < 3) {
					response_lines.push_back("ERR usage: template_add_part <template name>"
						" <template to add as a part>");
				}
				else {
					std::string part_name;
					if (TemplateOps::AddPart(state, args[1], args[2], error, &part_name)) {
						response_lines.push_back("OK " + args[1] + " + " + part_name +
							" (" + args[2] + ")");
					}
					else {
						response_lines.push_back("ERR " + error);
					}
				}
			}
			else if (cmd == "template_remove_part") {
				if (args.size() < 3) {
					response_lines.push_back("ERR usage: template_remove_part <template name> <part>");
				}
				else if (TemplateOps::RemovePart(state, args[1], args[2], error)) {
					response_lines.push_back("OK " + args[1] + " - " + args[2]);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "template_set_part") {
				//Every field of a part in one edit, as a delta: what the JSON does not
				//name keeps its current value. Single-quoted like template_set, for the
				//same tokenizer reason.
				if (args.size() < 4) {
					response_lines.push_back("ERR usage: template_set_part <template name> <part>"
						" <json: name/template/attach/bone/position/rotation/scale,"
						" single-quoted>");
				}
				else {
					std::string source = args[3];
					std::replace(source.begin(), source.end(), '\'', '"');
					json value;
					bool parsed = true;
					try {
						value = json::parse(source);
					}
					catch (const std::exception& ex) {
						parsed = false;
						response_lines.push_back(std::string("ERR bad JSON: ") + ex.what());
					}
					if (parsed && !value.is_object()) {
						parsed = false;
						response_lines.push_back("ERR part value must be a JSON object");
					}
					if (parsed) {
						TemplateOps::TemplatePart part;
						bool found = false;
						for (const TemplateOps::TemplatePart& existing :
							TemplateOps::ListParts(state, args[1])) {
							if (existing.name == args[2]) {
								part = existing;
								found = true;
								break;
							}
						}
						if (!found) {
							response_lines.push_back("ERR unknown part: " + args[2]);
						}
						else {
							part.name = value.value("name", part.name);
							part.template_name = value.value("template", part.template_name);
							part.attach = value.value("attach", part.attach);
							part.bone = value.value("bone", part.bone);
							if (value.contains("position")) {
								const auto& p = value["position"];
								part.position = { p.value("x", part.position.x),
									p.value("y", part.position.y), p.value("z", part.position.z) };
							}
							if (value.contains("rotation")) {
								const auto& r = value["rotation"];
								part.rotation = { r.value("x", part.rotation.x),
									r.value("y", part.rotation.y), r.value("z", part.rotation.z),
									r.value("w", part.rotation.w) };
							}
							if (value.contains("scale")) {
								const auto& s = value["scale"];
								part.scale = { s.value("x", part.scale.x),
									s.value("y", part.scale.y), s.value("z", part.scale.z) };
							}
							if (TemplateOps::SetPart(state, args[1], args[2], part, error)) {
								response_lines.push_back("OK " + args[1] + " " + part.name);
							}
							else {
								response_lines.push_back("ERR " + error);
							}
						}
					}
				}
			}
			else if (cmd == "template_from_selection") {
				//The composed half of template_from_entity: the whole selection becomes
				//one template, the primary (or a named entity) being the root.
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: template_from_selection <template name>"
						" [root entity] [pivot]");
				}
				else {
					std::vector<std::string> names;
					Coordinator* c = state.world->GetCoordinator();
					for (Entity e : state.selected_entities) {
						if (c != nullptr && c->ContainsComponent<Base>(e)) {
							names.push_back(c->GetConstComponent<Base>(e).name);
						}
					}
					const bool pivot = (args.size() > 3 && args[3] == "pivot") ||
						(args.size() > 2 && args[2] == "pivot");
					//Defaults to the entity picked first, matching what the panel does and
					//what the Entities panel marks.
					std::string root = (args.size() > 2 && args[2] != "pivot") ? args[2]
						: (names.empty() ? std::string() : names.front());
					if (names.empty()) {
						response_lines.push_back("ERR nothing selected");
					}
					else if (TemplateOps::CreateFromSelection(state, names, root, pivot, args[1], error)) {
						response_lines.push_back("OK " + state.status_message);
					}
					else {
						response_lines.push_back("ERR " + error);
					}
				}
			}
			else if (cmd == "apply_to_template") {
				//The scene-to-definition direction: an instance's parts, as they now
				//stand, become what the template says.
				std::string target;
				if (args.size() > 1) {
					target = args[1];
				}
				else if (state.selected_entity != INVALID_ENTITY_ID) {
					Coordinator* c = state.world->GetCoordinator();
					if (c != nullptr && c->ContainsComponent<Base>(state.selected_entity)) {
						target = c->GetConstComponent<Base>(state.selected_entity).name;
					}
				}
				if (target.empty()) {
					response_lines.push_back("ERR usage: apply_to_template [instance or part]"
						" (defaults to the selection)");
				}
				else if (TemplateOps::ApplyInstanceToTemplate(state, target, error)) {
					response_lines.push_back("OK " + state.status_message);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "deselect") {
				Selection::Clear(state);
				response_lines.push_back("OK nothing selected");
			}
			else if (cmd == "save_templates") {
				if (TemplateOps::SaveTemplates(state, error)) {
					response_lines.push_back("OK " + state.status_message);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "select_template") {
				AssetBrowser::EnsureAssetsScanned(state);
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
				AssetBrowser::EnsureAssetsScanned(state);
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: place <template name> [origin|view]");
				}
				else {
					//"origin" stays the default for a scripted place: it is the
					//reproducible one, where "view" depends on where the camera happens
					//to be pointing.
					PlacementMode mode = PlacementMode::Origin;
					bool known_mode = true;
					if (args.size() >= 3) {
						if (args[2] == "view") { mode = PlacementMode::ViewCenter; }
						else if (args[2] != "origin") { known_mode = false; }
					}
					if (!known_mode) {
						response_lines.push_back("ERR unknown placement '" + args[2] +
							"' (expected origin or view)");
					}
					else {
						float3 position{};
						if (AssetBrowser::PlaceTemplate(state, args[1], mode, error, &position)) {
							std::ostringstream os;
							os << "OK " << state.status_message << " at "
								<< position.x << " " << position.y << " " << position.z;
							response_lines.push_back(os.str());
						}
						else {
							response_lines.push_back("ERR " + error);
						}
					}
				}
			}
			else if (cmd == "import_template") {
				if (args.size() < 2) {
					response_lines.push_back("ERR usage: import_template <.tpl path>");
				}
				else if (TemplateOps::ImportTemplate(state, args[1], error)) {
					response_lines.push_back("OK " + state.status_message);
				}
				else {
					response_lines.push_back("ERR " + error);
				}
			}
			else if (cmd == "template_storage") {
				if (args.size() < 3 || (args[2] != "file" && args[2] != "level")) {
					response_lines.push_back("ERR usage: template_storage <name> file|level");
				}
				else if (TemplateOps::SetStorage(state, args[1], args[2] == "level", error)) {
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
