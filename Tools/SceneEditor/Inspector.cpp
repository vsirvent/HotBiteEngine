#include "Inspector.h"
#include "ComponentOps.h"
#include "EditorHistory.h"
#include "EditorLayout.h"
#include "EntityOps.h"
#include "MaterialPanel.h"
#include "TemplatePanel.h"

#include "imgui.h"
#include <ECS/ComponentRegistry.h>
#include <World.h>
#include <Components/Base.h>
#include <Components/Camera.h>
#include <Components/Lights.h>
#include <Components/Physics.h>
#include <Components/Particles.h>
#include <Components/Sky.h>
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;

namespace HotBiteEditor {
	namespace Inspector {

		//Manual quaternion -> Euler (pitch=X, yaw=Y, roll=Z) extraction, matching
		//DirectX::XMQuaternionRotationRollPitchYaw's composition convention used by
		//float3_to_quaternion (Defines.h) for the reverse direction on edit.
		float3 QuaternionToEulerDegrees(const float4& q)
		{
			float sinp = 2.0f * (q.w * q.x - q.y * q.z);
			float pitch = std::fabsf(sinp) >= 1.0f
				? std::copysignf(DirectX::XM_PIDIV2, sinp)
				: std::asinf(sinp);
			float yaw = std::atan2f(2.0f * (q.w * q.y + q.z * q.x), 1.0f - 2.0f * (q.x * q.x + q.y * q.y));
			float roll = std::atan2f(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.x * q.x + q.z * q.z));

			return {
				DirectX::XMConvertToDegrees(pitch),
				DirectX::XMConvertToDegrees(yaw),
				DirectX::XMConvertToDegrees(roll)
			};
		}

		void RefreshEulerCache(EditorState& state)
		{
			state.inspector_euler_degrees = { 0.0f, 0.0f, 0.0f };
			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr || state.selected_entity == INVALID_ENTITY_ID) {
				return;
			}
			if (!c->ContainsComponent<Transform>(state.selected_entity)) {
				return;
			}
			const float4& q = c->GetComponent<Transform>(state.selected_entity).rotation;
			state.inspector_euler_degrees = QuaternionToEulerDegrees(q);
		}

		//Held by every path that writes a Transform, from before the first field is
		//touched until the commit is done. It is uncontended in the normal (paused)
		//editor; it matters while a physics preview runs, where an unlocked read-modify-
		//write of a Transform races the physics thread writing body poses into that very
		//Transform (see CommitTransformEdit).
		using EditTransformLock = std::lock_guard<std::recursive_mutex>;

		//Pushes a just-written Transform out to the renderer and the physics body.
		//Shared by every write path, including the physics preview's rewind, which
		//needs exactly this and none of the save bookkeeping below. `before` is the
		//Transform as it was prior to the write; it decides whether the physics
		//collider has to be rebuilt.
		static void SyncTransformTargets(EditorState& state, Entity entity, const Base& base,
			Transform& t, const TransformSnapshot& before)
		{
			t.dirty = true;

			//Move the physics body (when there is one) along with the edit: bodies
			//live in world space independently of the Transform, so without this the
			//collider stays behind at the old spot and, the moment the simulation
			//runs, PhysicsSystem snaps the entity right back to it.
			Coordinator* c = state.world->GetCoordinator();
			if (c->ContainsComponent<Physics>(entity)) {
				Physics& ph = c->GetComponent<Physics>(entity);
				if (ph.body != nullptr) {
					std::lock_guard<std::recursive_mutex> lock(Core::physics_mutex);
					reactphysics3d::Transform bt(
						{ t.position.x, t.position.y, t.position.z },
						{ t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w });
					ph.body->setTransform(bt);
					if (ph.type == reactphysics3d::BodyType::DYNAMIC) {
						ph.body->setLinearVelocity({ 0.0f, 0.0f, 0.0f });
						ph.body->setAngularVelocity({ 0.0f, 0.0f, 0.0f });
					}
					//Prime the change detector so a paused PhysicsSystem doesn't
					//treat the teleport itself as pending body movement to sync back.
					ph.last_body_transform = bt;

					//The body's *pose* is now right, but its collision shape was sized
					//and oriented from the Transform back when the body was created:
					//a scale or rotation edit leaves the entity wearing the collider of
					//its old shape (visibly wrong once Simulate Physics runs, and it
					//also misdirects viewport click-picking, which raycasts colliders).
					//Rebuild it whenever either channel actually moved.
					bool same_scale = before.scale.x == t.scale.x &&
						before.scale.y == t.scale.y && before.scale.z == t.scale.z;
					bool same_rotation = before.rotation.x == t.rotation.x &&
						before.rotation.y == t.rotation.y &&
						before.rotation.z == t.rotation.z &&
						before.rotation.w == t.rotation.w;
					if (c->ContainsComponent<Bounds>(entity) &&
						(!same_scale || !same_rotation)) {
						//Dynamic bodies always carry a primitive shape; everything else
						//may own the FBX mesh shape Init resolved for it.
						Core::ShapeData* shape_data =
							(ph.type != reactphysics3d::BodyType::DYNAMIC)
							? state.world->GetEntityShape(base.name) : nullptr;
						ph.UpdateShape(shape_data,
							c->GetComponent<Bounds>(entity).local_box.Extents,
							t.scale, t.rotation);
					}
				}
			}

		}

		//Writes a placed instance's live pose back into its record, in the space the
		//record is actually in.
		//
		//A record is *not* the object's world transform: World::SpawnInstance composes
		//it with the template's own base transform (position added, rotation multiplied,
		//scale multiplied), so the record has to be the live pose with that composition
		//taken back out. Storing the live pose as-is means every respawn - a paste, the
		//undo of a place, the next load of the level - composes the base a second time.
		//For the troll template, whose base scale is 0.01, that is exactly the reported
		//"my copy came out at 0.01": the edited scale went into the record and the
		//template's own scale was multiplied in again on top of it.
		static void StoreInstanceTransform(EditorState& state, PlacedInstance& inst,
			const Transform& t)
		{
			float3 base_position;
			float4 base_rotation;
			float3 base_scale;
			state.world->GetTemplateBaseTransform(inst.template_name, base_position,
				base_rotation, base_scale);
			inst.position = SUB_F3_F3(t.position, base_position);
			//q_live = q_record * q_base, so q_record = q_live * conj(q_base) - which is
			//what express_rotation_with_respect_to computes.
			inst.rotation = express_rotation_with_respect_to(t.rotation, base_rotation);
			//A zero base scale has no ratio to undo; keep the live value on that axis
			//rather than dividing by zero (the instance is degenerate either way).
			inst.scale = {
				(base_scale.x != 0.0f) ? t.scale.x / base_scale.x : t.scale.x,
				(base_scale.y != 0.0f) ? t.scale.y / base_scale.y : t.scale.y,
				(base_scale.z != 0.0f) ? t.scale.z / base_scale.z : t.scale.z,
			};
		}

		//Shared post-edit bookkeeping for both the interactive (Draw) and programmatic
		//(ApplyTransform/ApplySnapshot) paths: syncs the edit out and records what
		//needs to be written back on save.
		static void CommitTransformEdit(EditorState& state, Entity entity, const Base& base,
			Transform& t, const TransformSnapshot& before)
		{
			SyncTransformTargets(state, entity, base, t, before);

			//While the physics preview runs, an edit is authoring against the pose the
			//scene will rewind to, not against whatever the simulation happens to have
			//moved this body to - so retarget the rewind (see PhysicsPreview.h). Without
			//this, switching the preview off would silently undo the edit.
			//
			//Reading it back out of the Transform is only sound because every caller
			//holds the physics lock across its write and this commit (see
			//EditTransformLock): with the simulation live, an unlocked edit can have a
			//physics tick overwrite the Transform in between, and the rewind would then
			//latch the *simulated* pose - so switching the preview off would leave the
			//entity wherever it happened to be mid-fall, which is precisely what an
			//unlocked version did in testing.
			auto baseline = state.physics_preview_baseline.find(base.name);
			if (baseline != state.physics_preview_baseline.end()) {
				baseline->second = { t.position, t.rotation, t.scale };
			}

			if (state.instance_entity_ids.count(entity) != 0) {
				//This entity is an editor-placed instance: update its bookkeeping
				//entry directly so a save writes the new transform out.
				for (auto& inst : state.placed_instances) {
					if (inst.name == base.name) {
						StoreInstanceTransform(state, inst, t);
						break;
					}
				}
			}
			else {
				//An FBX-authored entity: remember its name so SceneSerializer
				//writes a position/scale/rotation override for it under "entities".
				state.overridden_entities.insert(base.name);
			}
		}

		bool ApplyTransform(EditorState& state,
			const float3* position,
			const float3* scale,
			const float3* euler_degrees,
			std::string& error,
			bool record_history)
		{
			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr || state.selected_entity == INVALID_ENTITY_ID) {
				error = "no entity selected";
				return false;
			}
			if (!c->ContainsComponent<Base>(state.selected_entity) ||
				!c->ContainsComponent<Transform>(state.selected_entity)) {
				error = "selected entity has no Base/Transform component";
				return false;
			}
			const Base& base = c->GetComponent<Base>(state.selected_entity);
			Transform& t = c->GetComponent<Transform>(state.selected_entity);
			EditTransformLock lock(Core::physics_mutex);
			TransformSnapshot before{ t.position, t.rotation, t.scale };

			if (position != nullptr) {
				t.position = *position;
			}
			if (scale != nullptr) {
				t.scale = *scale;
			}
			if (euler_degrees != nullptr) {
				//Only touch the rotation when explicitly requested, so pure
				//position/scale edits don't accumulate quaternion<->Euler
				//round-trip error through the cached Euler angles.
				state.inspector_euler_degrees = *euler_degrees;
				t.rotation = float3_to_quaternion(state.inspector_euler_degrees);
			}
			CommitTransformEdit(state, state.selected_entity, base, t, before);
			if (record_history) {
				RecordTransformEdit(state, base.name, before);
			}
			return true;
		}

		bool GetSnapshot(EditorState& state, const std::string& entity_name, TransformSnapshot& out)
		{
			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr) {
				return false;
			}
			Entity e = c->GetEntityByName(entity_name);
			if (e == INVALID_ENTITY_ID || !c->ContainsComponent<Transform>(e)) {
				return false;
			}
			const Transform& t = c->GetComponent<Transform>(e);
			out = { t.position, t.rotation, t.scale };
			return true;
		}

		bool ApplySnapshot(EditorState& state, const std::string& entity_name,
			const TransformSnapshot& snapshot, std::string& error)
		{
			Coordinator* c = state.world->GetCoordinator();
			Entity e = (c != nullptr) ? c->GetEntityByName(entity_name) : INVALID_ENTITY_ID;
			if (e == INVALID_ENTITY_ID) {
				error = "entity not found: " + entity_name;
				return false;
			}
			if (!c->ContainsComponent<Base>(e) || !c->ContainsComponent<Transform>(e)) {
				error = "entity has no Base/Transform component: " + entity_name;
				return false;
			}
			const Base& base = c->GetComponent<Base>(e);
			Transform& t = c->GetComponent<Transform>(e);
			EditTransformLock lock(Core::physics_mutex);
			TransformSnapshot before{ t.position, t.rotation, t.scale };
			t.position = snapshot.position;
			t.rotation = snapshot.rotation;
			t.scale = snapshot.scale;
			CommitTransformEdit(state, e, base, t, before);
			if (e == state.selected_entity) {
				RefreshEulerCache(state);
			}
			return true;
		}

		bool RestoreSnapshot(EditorState& state, const std::string& entity_name,
			const TransformSnapshot& snapshot, std::string& error)
		{
			Coordinator* c = state.world->GetCoordinator();
			Entity e = (c != nullptr) ? c->GetEntityByName(entity_name) : INVALID_ENTITY_ID;
			if (e == INVALID_ENTITY_ID) {
				error = "entity not found: " + entity_name;
				return false;
			}
			if (!c->ContainsComponent<Base>(e) || !c->ContainsComponent<Transform>(e)) {
				error = "entity has no Base/Transform component: " + entity_name;
				return false;
			}
			const Base& base = c->GetComponent<Base>(e);
			Transform& t = c->GetComponent<Transform>(e);
			EditTransformLock lock(Core::physics_mutex);
			TransformSnapshot before{ t.position, t.rotation, t.scale };
			t.position = snapshot.position;
			t.rotation = snapshot.rotation;
			t.scale = snapshot.scale;
			SyncTransformTargets(state, e, base, t, before);
			if (e == state.selected_entity) {
				RefreshEulerCache(state);
			}
			return true;
		}

		void RecordTransformEdit(EditorState& state, const std::string& entity_name,
			const TransformSnapshot& before)
		{
			TransformSnapshot after;
			if (!GetSnapshot(state, entity_name, after)) {
				return;
			}
			auto same3 = [](const float3& a, const float3& b) {
				return a.x == b.x && a.y == b.y && a.z == b.z;
			};
			if (same3(before.position, after.position) && same3(before.scale, after.scale) &&
				before.rotation.x == after.rotation.x && before.rotation.y == after.rotation.y &&
				before.rotation.z == after.rotation.z && before.rotation.w == after.rotation.w) {
				return;
			}
			EditorHistory::Push({
				"transform " + entity_name,
				[entity_name, before](EditorState& s) {
					std::string err;
					ApplySnapshot(s, entity_name, before, err);
				},
				[entity_name, after](EditorState& s) {
					std::string err;
					ApplySnapshot(s, entity_name, after, err);
				} });
		}

		void ApplySnapshots(EditorState& state, const std::vector<std::string>& entity_names,
			const std::vector<TransformSnapshot>& snapshots)
		{
			for (size_t i = 0; i < entity_names.size() && i < snapshots.size(); ++i) {
				std::string err;
				ApplySnapshot(state, entity_names[i], snapshots[i], err);
			}
		}

		void RecordTransformEdits(EditorState& state,
			const std::vector<std::string>& entity_names,
			const std::vector<TransformSnapshot>& befores)
		{
			auto same3 = [](const float3& a, const float3& b) {
				return a.x == b.x && a.y == b.y && a.z == b.z;
			};
			std::vector<std::string> names;
			std::vector<TransformSnapshot> from, to;
			for (size_t i = 0; i < entity_names.size() && i < befores.size(); ++i) {
				TransformSnapshot after;
				if (!GetSnapshot(state, entity_names[i], after)) {
					continue; //entity is gone; nothing to restore it to
				}
				const TransformSnapshot& before = befores[i];
				if (same3(before.position, after.position) && same3(before.scale, after.scale) &&
					before.rotation.x == after.rotation.x && before.rotation.y == after.rotation.y &&
					before.rotation.z == after.rotation.z && before.rotation.w == after.rotation.w) {
					continue;
				}
				names.push_back(entity_names[i]);
				from.push_back(before);
				to.push_back(after);
			}
			if (names.empty()) {
				return;
			}
			if (names.size() == 1) {
				//Keep the single-entity description ("transform box1"), which the
				//status line and the automation `undo` response report.
				EditorHistory::Push({
					"transform " + names[0],
					[names, from](EditorState& s) { ApplySnapshots(s, names, from); },
					[names, to](EditorState& s) { ApplySnapshots(s, names, to); } });
				return;
			}
			EditorHistory::Push({
				"transform " + std::to_string(names.size()) + " entities",
				[names, from](EditorState& s) { ApplySnapshots(s, names, from); },
				[names, to](EditorState& s) { ApplySnapshots(s, names, to); } });
		}

		static constexpr ImGuiTreeNodeFlags SECTION_FLAGS = ImGuiTreeNodeFlags_DefaultOpen;

		//== Making a section's widgets an undoable, saved edit ==
		//
		//Most sections below edit their component in place (a DragFloat writes straight
		//into the light's Data), so there is nothing to "apply" - what is missing is the
		//other two thirds of an editor edit: the value has to reach the entity's record
		//so a save writes it, and the whole drag has to become ONE history action.
		//
		//This is the same shape DrawTransform has always used, generalized: serialize the
		//component as the frame starts, latch that as the "before" when a widget is
		//activated, mark the component for save on every frame that changes anything, and
		//record one action when the drag ends. A discrete widget (a checkbox) activates
		//and deactivates in the same frame and so lands as one action too.
		//
		//Combos are NOT tracked this way: their change happens inside a popup, where the
		//activation of the combo itself says nothing about the edit. Those call
		//ComponentOps::SetValue directly, which applies and records in one step.
		namespace {

			//The pre-edit snapshot of a drag in progress. One at a time is enough - ImGui
			//has a single active widget - but it must remember *which* component it
			//belongs to, so switching sections mid-drag records what it did instead of
			//attributing it to the next one.
			nlohmann::json pending_before;
			std::string pending_entity;
			std::string pending_component;
			bool pending_valid = false;

			class SectionEdit {
			public:
				SectionEdit(EditorState& state, const std::string& entity_name,
					const std::string& component)
					: state(state), entity_name(entity_name), component(component),
					frame_before(ComponentOps::GetValue(state, entity_name, component)) {
				}

				//Accumulates one widget's return value together with ImGui's activation
				//state, which is only valid for the item just submitted.
				void Track(bool widget_changed) {
					changed |= widget_changed;
					activated |= ImGui::IsItemActivated();
					finished |= ImGui::IsItemDeactivatedAfterEdit();
				}

				//A widget that is complete the instant it changes (a dropdown, whose value
				//is picked inside a popup and which therefore never reports the
				//activate/deactivate pair a drag does).
				void Discrete(bool widget_changed) {
					if (widget_changed) {
						changed = activated = finished = true;
					}
				}

				//The generic property grid, which is a row of independent widgets with no
				//single activation moment: the first frame that changes anything opens the
				//edit, and the grid reports its own completion.
				void Grid(bool grid_changed, bool grid_finished) {
					changed |= grid_changed;
					activated |= grid_changed;
					finished |= grid_finished;
				}

				//Call once, after the section's last widget.
				void Commit() {
					if (!changed && !activated && !finished) {
						return;
					}
					if (pending_valid && (pending_entity != entity_name ||
						pending_component != component)) {
						//A drag whose end was never seen (the selection changed under it):
						//record what it did rather than losing it from the history.
						ComponentOps::RecordEdit(state, pending_entity, pending_component,
							pending_before);
						pending_valid = false;
					}
					if (!pending_valid && (activated || changed)) {
						pending_before = frame_before;
						pending_entity = entity_name;
						pending_component = component;
						pending_valid = true;
					}
					if (changed) {
						ComponentOps::MarkEdited(state, entity_name, component);
					}
					if (finished && pending_valid) {
						ComponentOps::RecordEdit(state, entity_name, component, pending_before);
						pending_valid = false;
					}
				}

			private:
				EditorState& state;
				std::string entity_name;
				std::string component;
				nlohmann::json frame_before;
				bool changed = false;
				bool activated = false;
				bool finished = false;
			};

			std::string EntityName(Coordinator* c, Entity e) {
				return c->ContainsComponent<Base>(e) ? c->GetConstComponent<Base>(e).name
					: std::string();
			}
		}

		static void DrawBase(EditorState& state, Coordinator* c, Entity e)
		{
			if (!ImGui::CollapsingHeader("Base", SECTION_FLAGS)) {
				return;
			}
			Base& b = c->GetComponent<Base>(e);

			//Editable name. The rename is committed once, when the field loses focus
			//after an edit (Enter or click-away); RenameEntity vetoes empty/duplicate/
			//reserved names, leaving the entity's name unchanged. The buffer is
			//reseeded from the live name on every frame the field is not being typed
			//in (checked *after* InputText, when IsItemActive refers to it), so it
			//follows selection changes, undo/redo and rejected renames.
			static char name_buf[128] = "";
			std::string prev_name = b.name;
			ImGui::InputText("Name", name_buf, sizeof(name_buf), ImGuiInputTextFlags_EnterReturnsTrue);
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				std::string error;
				if (!EntityOps::RenameEntity(state, prev_name, name_buf, error)) {
					state.status_message = "Rename failed: " + error;
				}
			}
			if (!ImGui::IsItemActive()) {
				strncpy_s(name_buf, b.name.c_str(), sizeof(name_buf) - 1);
			}
			ImGui::Text("Id: %u", (unsigned)b.id);
			SectionEdit edit(state, b.name, Base::NAME);
			if (b.parent != INVALID_ENTITY_ID) {
				ImGui::Text("Parent: %u", (unsigned)b.parent);
				edit.Track(ImGui::Checkbox("Parent position", &b.parent_position));
				edit.Track(ImGui::Checkbox("Parent rotation", &b.parent_rotation));
			}
			edit.Track(ImGui::Checkbox("Visible", &b.visible));
			ImGui::SameLine();
			edit.Track(ImGui::Checkbox("Scene visible", &b.scene_visible));
			edit.Track(ImGui::Checkbox("Cast shadow", &b.cast_shadow));
			ImGui::SameLine();
			edit.Track(ImGui::Checkbox("Draw depth", &b.draw_depth));
			edit.Track(ImGui::Checkbox("Static", &b.is_static));
			int draw_method = (int)b.draw_method;
			const bool method_changed = ImGui::Combo("Draw mode", &draw_method,
				"Always\0Screen only\0");
			if (method_changed) {
				b.draw_method = (eDrawMethod)draw_method;
			}
			//A dropdown picks its value in a popup, so it never reports the
			//activate/deactivate pair a drag does: it is one complete edit as it happens.
			edit.Discrete(method_changed);
			int pass = (int)b.pass;
			const bool pass_changed = ImGui::DragInt("Pass", &pass, 0.1f, 0, 16);
			if (pass_changed && pass >= 0) {
				b.pass = (uint32_t)pass;
			}
			edit.Track(pass_changed);
			edit.Commit();
		}

		static void DrawTransform(EditorState& state, Coordinator* c, Entity e)
		{
			const Base& base = c->GetComponent<Base>(e);
			Transform& t = c->GetComponent<Transform>(e);
			//One history action per completed edit (a whole drag, or one typed
			//value), not one per frame: the pre-edit snapshot is taken the frame a
			//field activates and recorded when it deactivates. Only one field can be
			//active at a time, so a single pending snapshot covers all three.
			static TransformSnapshot pending_before;
			//Covers the widgets too, not just the commit: they edit t in place, so with
			//a physics preview running the drag would otherwise read a pose the physics
			//thread is concurrently rewriting.
			EditTransformLock lock(Core::physics_mutex);
			TransformSnapshot frame_before{ t.position, t.rotation, t.scale };
			bool changed = false;
			bool activated = false;
			bool finished = false;
			changed |= ImGui::DragFloat3("Position", &t.position.x, 0.05f);
			activated |= ImGui::IsItemActivated();
			finished |= ImGui::IsItemDeactivatedAfterEdit();
			changed |= ImGui::DragFloat3("Scale", &t.scale.x, 0.01f);
			activated |= ImGui::IsItemActivated();
			finished |= ImGui::IsItemDeactivatedAfterEdit();
			changed |= ImGui::DragFloat3("Rotation (deg)", &state.inspector_euler_degrees.x, 0.5f);
			activated |= ImGui::IsItemActivated();
			finished |= ImGui::IsItemDeactivatedAfterEdit();
			if (activated) {
				pending_before = frame_before;
			}
			if (changed) {
				t.rotation = float3_to_quaternion(state.inspector_euler_degrees);
				CommitTransformEdit(state, e, base, t, frame_before);
			}
			if (finished) {
				RecordTransformEdit(state, base.name, pending_before);
			}
		}

		static void DrawBounds(Coordinator* c, Entity e)
		{
			const Bounds& b = c->GetConstComponent<Bounds>(e);
			ImGui::Text("Local  center  %.2f %.2f %.2f", b.local_box.Center.x, b.local_box.Center.y, b.local_box.Center.z);
			ImGui::Text("Local  extents %.2f %.2f %.2f", b.local_box.Extents.x, b.local_box.Extents.y, b.local_box.Extents.z);
			ImGui::Text("World  center  %.2f %.2f %.2f", b.final_box.Center.x, b.final_box.Center.y, b.final_box.Center.z);
			ImGui::Text("World  extents %.2f %.2f %.2f", b.final_box.Extents.x, b.final_box.Extents.y, b.final_box.Extents.z);
			//Deliberately a readout, not fields: StaticMeshSystem::Update re-measures a
			//mesh entity's local box from its mesh every time the transform is dirty, so
			//a typed value would be silently overwritten within a frame or two. (A
			//*template's* bounds are a different thing - those are authored JSON the
			//spawner clones, and the Templates panel does let you override them.)
			ImGui::TextDisabled("Measured from the mesh each time the transform changes.");
		}

		static void DrawMaterial(EditorState& state, Coordinator* c, Entity e)
		{
			Material& m = c->GetComponent<Material>(e);
			if (m.data == nullptr) {
				ImGui::TextDisabled("(no material data)");
				return;
			}
			Core::MaterialData& md = *m.data;

			//Which of the level's materials this entity uses. Note how this differs
			//from everything below it: the combo repoints *this entity*, while the
			//property editor changes the material itself and so affects every entity
			//sharing it (which the editor's "Used by" list spells out).
			const std::vector<std::string> materials = MaterialOps::ListMaterials(state);
			if (ImGui::BeginCombo("Material", md.name.c_str())) {
				for (const std::string& name : materials) {
					if (ImGui::Selectable(name.c_str(), name == md.name) && name != md.name) {
						std::string error;
						const std::string entity_name = c->ContainsComponent<Base>(e)
							? c->GetComponent<Base>(e).name : std::string();
						if (!MaterialOps::AssignMaterial(state, entity_name, name, error)) {
							state.status_message = "Assign material failed: " + error;
						}
					}
				}
				ImGui::EndCombo();
			}
			ImGui::Separator();

			//The Materials panel's property editor, reused verbatim: a material edited
			//from here is undoable and marks its .mat file dirty exactly as it does
			//there. The thumbnail is suppressed - this panel is narrow, and the
			//Materials panel is where you go to look at the sphere.
			MaterialPanel::DrawMaterialProperties(state, md.name, false);

			if (m.multi_material.multi_texture_count > 0) {
				ImGui::Text("Multi-texture layers: %u", m.multi_material.multi_texture_count);
			}
		}

		//The animation sets currently attached to `data`, by the names the level loaded
		//them under. The MeshData holds them as unnamed shared pointers, exactly as
		//Mesh::ToJson finds out, so the names have to come back from the world.
		static std::set<std::string> AttachedAnimationSets(EditorState& state, Core::MeshData* data)
		{
			std::set<std::string> attached;
			if (data == nullptr) {
				return attached;
			}
			auto& named = state.world->GetSkeletons();
			for (const std::string& name : named.Keys()) {
				std::shared_ptr<Core::Skeleton>* skl = named.Get(name);
				if (skl == nullptr) {
					continue;
				}
				for (const auto& in_use : data->skeletons) {
					if (in_use == *skl) {
						attached.insert(name);
						break;
					}
				}
			}
			return attached;
		}

		static void DrawMesh(EditorState& state, Coordinator* c, Entity e)
		{
			Mesh& mesh = c->GetComponent<Mesh>(e);
			const std::string entity_name = EntityName(c, e);
			Core::MeshData* data = mesh.GetData();
			const std::string mesh_name = (data != nullptr) ? data->name : std::string();

			//Which mesh asset this entity draws. Same picker the Templates panel has, and
			//the same reason it is a picker rather than a text field: the name has to
			//resolve against the level's loaded meshes or the entity would end up holding
			//the default cube.
			const std::vector<std::string> meshes = TemplateOps::ListMeshes(state);
			if (ImGui::BeginCombo("Mesh", mesh_name.empty() ? "(none)" : mesh_name.c_str())) {
				for (const std::string& option : meshes) {
					if (ImGui::Selectable(option.c_str(), option == mesh_name) &&
						option != mesh_name) {
						nlohmann::json block = ComponentOps::GetValue(state, entity_name, Mesh::NAME);
						block["name"] = option;
						//The animation belonged to the old mesh. Mesh::SetAnimation
						//silently ignores a name the new one cannot play, which would
						//leave the entity claiming an animation it never runs - so it is
						//dropped as part of the same edit.
						const std::string animation = block.value("animation", std::string());
						if (!animation.empty()) {
							const std::vector<std::string> available =
								state.world->GetMeshAnimations(option);
							if (std::find(available.begin(), available.end(), animation) ==
								available.end()) {
								block["animation"] = "";
							}
						}
						std::string error;
						if (!ComponentOps::SetValue(state, entity_name, Mesh::NAME, block, error)) {
							state.status_message = "Set mesh failed: " + error;
						}
					}
				}
				if (meshes.empty()) {
					ImGui::TextDisabled("(this level has no mesh assets)");
				}
				ImGui::EndCombo();
			}
			if (data == nullptr) {
				ImGui::TextDisabled("(no mesh data)");
				return;
			}
			ImGui::Text("Vertices: %u  Indices: %u", data->vertexCount, data->indexCount);

			//Which animation sets are attached to the mesh. This has to come before the
			//animation picker, because a mesh can only play animations belonging to a set
			//attached to it - and it is why that picker is empty on a level that attached
			//none. Note the attachment is to the *shared* MeshData, so it is visible to
			//every entity using this mesh; that is how the engine has always done it.
			const std::vector<std::string> sets = TemplateOps::ListAnimationSets(state);
			if (!sets.empty() && ImGui::TreeNode("Animation sets")) {
				const std::set<std::string> attached = AttachedAnimationSets(state, data);
				for (const std::string& set : sets) {
					bool on = attached.count(set) != 0;
					if (ImGui::Checkbox(set.c_str(), &on)) {
						nlohmann::json block = ComponentOps::GetValue(state, entity_name, Mesh::NAME);
						std::vector<std::string> declared;
						for (const std::string& existing : attached) {
							if (existing != set) {
								declared.push_back(existing);
							}
						}
						if (on) {
							declared.push_back(set);
						}
						block["skeletons"] = declared;
						std::string error;
						if (!ComponentOps::SetValue(state, entity_name, Mesh::NAME, block, error)) {
							state.status_message = "Animation set failed: " + error;
						}
					}
				}
				ImGui::TextDisabled("Detaching only stops this entity declaring the set;\n"
					"the set stays on the shared mesh for this session.");
				ImGui::TreePop();
			}

			//The animation this entity plays, out of everything its mesh offers. A
			//template can bring several (one per set attached to its mesh) and each
			//entity picks its own, which is what makes two instances of one creature able
			//to idle and walk side by side.
			const std::vector<std::string> animations = state.world->GetMeshAnimations(mesh_name);
			const std::string animation = mesh.GetCurrentAnimationName();
			ImGui::BeginDisabled(animations.empty());
			if (ImGui::BeginCombo("Animation", animation.empty() ? "(none)" : animation.c_str())) {
				auto choose = [&](const std::string& option) {
					nlohmann::json block = ComponentOps::GetValue(state, entity_name, Mesh::NAME);
					block["animation"] = option;
					block["animation_loop"] = mesh.current_animation.loop;
					block["animation_speed"] = (mesh.current_animation.speed > 0.0f)
						? mesh.current_animation.speed : 1.0f;
					std::string error;
					if (!ComponentOps::SetValue(state, entity_name, Mesh::NAME, block, error)) {
						state.status_message = "Set animation failed: " + error;
					}
				};
				//"(none)" is an explicit choice, not the absence of one: it is how an
				//instance stands still while its template animates (Mesh::StopAnimation).
				if (ImGui::Selectable("(none)", animation.empty()) && !animation.empty()) {
					choose("");
				}
				for (const std::string& option : animations) {
					if (ImGui::Selectable(option.c_str(), option == animation) &&
						option != animation) {
						choose(option);
					}
				}
				ImGui::EndCombo();
			}
			ImGui::EndDisabled();
			if (animations.empty()) {
				ImGui::TextDisabled(sets.empty()
					? "(this level loaded no animation sets)"
					: "(attach an animation set above to choose an animation)");
			}

			if (!animation.empty()) {
				ImGui::Text("Frame: %d", mesh.GetCurrentFrame());
				SectionEdit edit(state, entity_name, Mesh::NAME);
				bool loop = mesh.current_animation.loop;
				const bool loop_changed = ImGui::Checkbox("Loop", &loop);
				if (loop_changed) {
					mesh.current_animation.loop = loop;
				}
				edit.Track(loop_changed);
				edit.Track(ImGui::DragFloat("Speed", &mesh.current_animation.speed, 0.01f,
					0.0f, 10.0f));
				edit.Commit();
			}
		}

		static void DrawAmbientLight(EditorState& state, Coordinator* c, Entity e)
		{
			AmbientLight::Data& d = c->GetComponent<AmbientLight>(e).GetData();
			SectionEdit edit(state, EntityName(c, e), AmbientLight::NAME);
			edit.Track(ImGui::ColorEdit3("Color down", &d.colorDown.x));
			edit.Track(ImGui::ColorEdit3("Color up", &d.colorUp.x));
			edit.Commit();
		}

		static void DrawDirectionalLight(EditorState& state, Coordinator* c, Entity e)
		{
			DirectionalLight& l = c->GetComponent<DirectionalLight>(e);
			DirectionalLight::Data& d = l.GetData();
			SectionEdit edit(state, EntityName(c, e), DirectionalLight::NAME);
			bool changed = false;
			auto track = [&](bool widget_changed) {
				changed |= widget_changed;
				edit.Track(widget_changed);
			};
			track(ImGui::ColorEdit3("Color", &d.color.x));
			track(ImGui::DragFloat("Intensity", &d.intensity, 0.05f, 0.0f, MAX_INTENSITY));
			bool aimed = ImGui::DragFloat3("Direction", &d.direction.x, 0.01f, -1.0f, 1.0f);
			if (aimed) {
				//Keep the light direction normalized; a zero vector would break the
				//shadow view matrix, so ignore edits that pass through it.
				float len = std::sqrtf(d.direction.x * d.direction.x + d.direction.y * d.direction.y + d.direction.z * d.direction.z);
				if (len > 1e-4f) {
					d.direction = { d.direction.x / len, d.direction.y / len, d.direction.z / len };
				}
				else {
					aimed = false;
				}
			}
			track(aimed);
			track(ImGui::DragFloat3("Position", &d.position.x, 0.05f));
			track(ImGui::DragFloat("Range", &d.range, 0.1f, 0.0f, 10000.0f));
			track(ImGui::DragFloat("Fog density", &d.density, 0.001f, 0.0f, 10.0f));
			bool fog = (d.flags & DIR_LIGHT_FLAG_FOG) != 0;
			const bool fog_changed = ImGui::Checkbox("Fog", &fog);
			if (fog_changed) {
				l.SetFog(fog);
			}
			track(fog_changed);
			ImGui::SameLine();
			bool inverse = (d.flags & DIR_LIGHT_FLAG_INVERSE) != 0;
			const bool inverse_changed = ImGui::Checkbox("Inverse", &inverse);
			if (inverse_changed) {
				l.SetInverse(inverse);
			}
			track(inverse_changed);
			ImGui::Text("Casts shadow: %s", l.CastShadow() ? "yes" : "no");
			if (changed) {
				l.SetDirty();
			}
			edit.Commit();
		}

		static void DrawPointLight(EditorState& state, Coordinator* c, Entity e)
		{
			PointLight& l = c->GetComponent<PointLight>(e);
			PointLight::Data& d = l.GetData();
			SectionEdit edit(state, EntityName(c, e), PointLight::NAME);
			edit.Track(ImGui::ColorEdit3("Color", &d.color.x));
			edit.Track(ImGui::DragFloat3("Position", &d.position.x, 0.05f));
			edit.Track(ImGui::DragFloat("Range", &d.range, 0.1f, 0.0f, 10000.0f));
			edit.Track(ImGui::DragFloat("Fog density", &d.density, 0.001f, 0.0f, 10.0f));
			edit.Track(ImGui::DragFloat("Tilt ratio", &d.tilt_ratio, 0.1f));
			ImGui::Text("Casts shadow: %s", l.CastShadow() ? "yes" : "no");
			edit.Commit();
		}

		static void DrawPhysics(EditorState& state, Coordinator* c, Entity e)
		{
			Physics& ph = c->GetComponent<Physics>(e);
			const std::string entity_name = EntityName(c, e);

			//Body type and shape go through the component's own serialization rather than
			//being poked into the struct: both need the rigid body rebuilt in the physics
			//world to mean anything (Physics::FromJson does that), and both have to reach
			//the entity's record or the change would vanish on the next load.
			static const char* TYPES[] = { "STATIC", "KINEMATIC", "DYNAMIC" };
			static const char* SHAPES[] = { "NONE", "CAPSULE", "BOX", "SPHERE" };
			auto enum_combo = [&](const char* label, const char* key,
				const char* const* options, int count, const char* current) {
					if (!ImGui::BeginCombo(label, current)) {
						return;
					}
					for (int i = 0; i < count; ++i) {
						if (ImGui::Selectable(options[i], std::strcmp(current, options[i]) == 0) &&
							std::strcmp(current, options[i]) != 0) {
							nlohmann::json block =
								ComponentOps::GetValue(state, entity_name, Physics::NAME);
							block[key] = options[i];
							std::string error;
							//Under the physics lock: the rebuild swaps the collider out
							//from under whatever the physics thread is doing with it, and a
							//preview may well be running.
							std::lock_guard<std::recursive_mutex> lock(Core::physics_mutex);
							if (!ComponentOps::SetValue(state, entity_name, Physics::NAME,
								block, error)) {
								state.status_message = "Set physics failed: " + error;
							}
						}
					}
					ImGui::EndCombo();
				};
			const char* type = (ph.type == reactphysics3d::BodyType::DYNAMIC) ? "DYNAMIC"
				: (ph.type == reactphysics3d::BodyType::KINEMATIC) ? "KINEMATIC" : "STATIC";
			const int shape_index = (int)ph.shape;
			const char* shape = (shape_index >= 0 && shape_index < 4) ? SHAPES[shape_index] : "NONE";
			enum_combo("Body", "type", TYPES, 3, type);
			enum_combo("Shape", "shape", SHAPES, 4, shape);
			//A non-dynamic body collides against its own FBX mesh when the level brought
			//one in, and then the primitive above is not what is in the physics world.
			//Say so rather than leaving the combo looking inert.
			if (ph.type != reactphysics3d::BodyType::DYNAMIC &&
				state.world->GetEntityShape(entity_name) != nullptr) {
				ImGui::TextDisabled("Using this object's mesh collider; the shape above\n"
					"applies when the body is dynamic.");
			}

			//bounce/friction < 0 mean "engine default"; the drags start there and the
			//value is only written once one is actually set, matching Physics::ToJson.
			SectionEdit edit(state, entity_name, Physics::NAME);
			edit.Track(ImGui::DragFloat("Bounce", &ph.bounce, 0.01f, -1.0f, 1.0f, "%.2f"));
			if (ph.collider != nullptr && ph.bounce >= 0.0f) {
				ph.collider->getMaterial().setBounciness(ph.bounce);
			}
			edit.Track(ImGui::DragFloat("Friction", &ph.friction, 0.01f, -1.0f, 1.0f, "%.2f"));
			if (ph.collider != nullptr && ph.friction >= 0.0f) {
				ph.collider->getMaterial().setFrictionCoefficient(ph.friction);
			}
			edit.Track(ImGui::DragFloat("Air friction", &ph.air_friction, 0.01f, -1.0f, 1.0f, "%.2f"));
			if (ph.body != nullptr && ph.air_friction >= 0.0f) {
				ph.body->setLinearDamping(ph.air_friction);
			}
			edit.Commit();
			ImGui::TextDisabled("-1 leaves the engine default alone.");
		}

		static void DrawCamera(Coordinator* c, Entity e)
		{
			const Camera& cam = c->GetConstComponent<Camera>(e);
			ImGui::Text("Position:  %.2f %.2f %.2f", cam.world_position.x, cam.world_position.y, cam.world_position.z);
			ImGui::Text("Direction: %.2f %.2f %.2f", cam.direction.x, cam.direction.y, cam.direction.z);
			ImGui::Text("Rotation:  %.2f %.2f %.2f", cam.rotation.x, cam.rotation.y, cam.rotation.z);
		}

		static void DrawSky(EditorState& state, Coordinator* c, Entity e)
		{
			Sky& sky = c->GetComponent<Sky>(e);
			int hour = (int)(sky.second_of_day / 3600.0f) % 24;
			int minute = (int)(sky.second_of_day / 60.0f) % 60;
			ImGui::Text("Time of day: %02d:%02d", hour, minute);
			SectionEdit edit(state, EntityName(c, e), Sky::NAME);
			edit.Track(ImGui::DragFloat("Second of day", &sky.second_of_day, 60.0f, 0.0f, 86400.0f));
			edit.Track(ImGui::DragFloat("Time speed", &sky.second_speed, 0.1f, 0.0f, 10000.0f));
			edit.Track(ImGui::SliderFloat("Cloud density", &sky.cloud_density, 0.0f, 1.0f));
			edit.Track(ImGui::ColorEdit3("Day color", &sky.day_backcolor.x));
			edit.Track(ImGui::ColorEdit3("Mid color", &sky.mid_backcolor.x));
			edit.Track(ImGui::ColorEdit3("Night color", &sky.night_backcolor.x));
			edit.Commit();
		}

		static void DrawParticles(Coordinator* c, Entity e)
		{
			Particles& p = c->GetComponent<Particles>(e);
			ImGui::Text("Emitters: %d", (int)p.data.GetData().size());
		}

		static void DrawLighted(Coordinator* c, Entity e)
		{
			const Lighted& l = c->GetConstComponent<Lighted>(e);
			ImGui::Text("Point lights: %d  Dir lights: %d",
				(int)l.point_lights.size(), (int)l.dir_lights.size());
			ImGui::Text("Shadow maps: %d", (int)(l.shadows.size() + l.dir_shadows.size()));
		}

		// Maps a registered component name to the hand-written editor for it. Drawing
		// is the one part of a component's editor support that cannot live in the
		// engine (which has no ImGui dependency), so it is bound here by the same name
		// the registry uses. A component with no entry - anything a game registers -
		// still gets a section, drawn by the generic property grid.
		using ComponentDrawer = void(*)(EditorState&, Coordinator*, Entity);

		static ComponentDrawer FindDrawer(const std::string& name)
		{
			struct Entry { const char* name; ComponentDrawer draw; };
			static const Entry TABLE[] = {
				{ Transform::NAME,        [](EditorState& s, Coordinator* c, Entity e) { DrawTransform(s, c, e); } },
				{ Bounds::NAME,           [](EditorState& s, Coordinator* c, Entity e) { DrawBounds(c, e); } },
				{ Mesh::NAME,             [](EditorState& s, Coordinator* c, Entity e) { DrawMesh(s, c, e); } },
				{ Material::NAME,         [](EditorState& s, Coordinator* c, Entity e) { DrawMaterial(s, c, e); } },
				{ AmbientLight::NAME,     [](EditorState& s, Coordinator* c, Entity e) { DrawAmbientLight(s, c, e); } },
				{ DirectionalLight::NAME, [](EditorState& s, Coordinator* c, Entity e) { DrawDirectionalLight(s, c, e); } },
				{ PointLight::NAME,       [](EditorState& s, Coordinator* c, Entity e) { DrawPointLight(s, c, e); } },
				{ Physics::NAME,          [](EditorState& s, Coordinator* c, Entity e) { DrawPhysics(s, c, e); } },
				{ Sky::NAME,              [](EditorState& s, Coordinator* c, Entity e) { DrawSky(s, c, e); } },
				{ Lighted::NAME,          [](EditorState& s, Coordinator* c, Entity e) { DrawLighted(c, e); } },
				{ Camera::NAME,           [](EditorState& s, Coordinator* c, Entity e) { DrawCamera(c, e); } },
				{ Particles::NAME,        [](EditorState& s, Coordinator* c, Entity e) { DrawParticles(c, e); } },
				{ Player::NAME,           [](EditorState& s, Coordinator* c, Entity e) {
					ImGui::TextDisabled("(tag component, no properties)"); } },
			};
			for (const Entry& entry : TABLE) {
				if (name == entry.name) {
					return entry.draw;
				}
			}
			return nullptr;
		}

		// See Inspector.h. Enough for the numbers, flags and names that make up most
		// game components; nested objects/arrays are shown as text rather than guessed
		// at.
		bool DrawJsonGrid(nlohmann::json& value, bool* finished)
		{
			bool changed = false;
			auto commit = [&](bool discrete) {
				if (finished != nullptr && (discrete || ImGui::IsItemDeactivatedAfterEdit())) {
					*finished = true;
				}
			};
			for (auto& [key, field] : value.items()) {
				ImGui::PushID(key.c_str());
				if (field.is_boolean()) {
					bool v = field.get<bool>();
					if (ImGui::Checkbox(key.c_str(), &v)) {
						field = v;
						changed = true;
						commit(true); //a checkbox is one discrete edit, not a drag
					}
				}
				else if (field.is_number_float()) {
					float v = field.get<float>();
					if (ImGui::DragFloat(key.c_str(), &v, 0.05f)) {
						field = v;
						changed = true;
					}
					commit(false);
				}
				else if (field.is_number_integer()) {
					int v = field.get<int>();
					if (ImGui::DragInt(key.c_str(), &v, 0.1f)) {
						field = v;
						changed = true;
					}
					commit(false);
				}
				else if (field.is_string()) {
					char buf[256] = "";
					strncpy_s(buf, field.get<std::string>().c_str(), sizeof(buf) - 1);
					if (ImGui::InputText(key.c_str(), buf, sizeof(buf),
						ImGuiInputTextFlags_EnterReturnsTrue)) {
						field = std::string(buf);
						changed = true;
						commit(true); //committed with Enter, so it is already complete
					}
				}
				else {
					ImGui::LabelText(key.c_str(), "%s", field.dump().c_str());
				}
				ImGui::PopID();
			}
			return changed;
		}

		// One collapsing section for `desc`, with a remove button right-aligned in the
		// header when the component may be removed.
		static void DrawComponentSection(EditorState& state, Coordinator* c, Entity e,
			const std::string& entity_name, const ComponentDesc& desc)
		{
			ImGui::PushID(desc.name.c_str());

			//The remove affordance is CollapsingHeader's own close button (the p_visible
			//overload), not a SmallButton placed over the header with SameLine.
			//
			//A hand-placed button does not work here: the header is one item spanning the
			//full window width, so it claims the click for the pixels the button is drawn
			//on and the only visible effect is the header collapsing - the exact symptom
			//of the bug this replaced. ImGui lays this button out inside the header,
			//reserves the space for it, and handles the overlap itself.
			bool visible = true;
			const bool removable = desc.Removable();
			const bool open = ImGui::CollapsingHeader(desc.name.c_str(),
				removable ? &visible : nullptr, ImGuiTreeNodeFlags_DefaultOpen);
			if (removable) {
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Remove %s from %s", desc.name.c_str(), entity_name.c_str());
				}
				if (!visible) {
					std::string error;
					if (!ComponentOps::RemoveComponent(state, entity_name, desc.name, error)) {
						state.status_message = "Remove failed: " + error;
					}
					//The component is gone; nothing left to draw this frame.
					ImGui::PopID();
					return;
				}
			}

			if (open) {
				//The body gets its own ID scope, distinct from the header's.
				//
				//Without it, a widget whose label equals the component name collides with
				//the header itself: both hash the same string under the same PushID, so
				//they share one ImGui ID, and the header - submitted first - owns it. The
				//widget still draws and still highlights on hover (that is positional),
				//but can never activate. That is exactly what happened to the Material
				//section's "Material" combo, which rendered but refused to open.
				ImGui::PushID("body");
				ComponentDrawer drawer = FindDrawer(desc.name);
				if (drawer != nullptr) {
					drawer(state, c, e);
				}
				else {
					//A game component: no hand-written editor, so show its serialized
					//state through the generic grid. It edits, saves and undoes exactly
					//like the hand-written sections - the editor does not need to know
					//what the fields mean to do that.
					ImGui::TextDisabled("(game component)");
					nlohmann::json value = ComponentOps::GetValue(state, entity_name, desc.name);
					SectionEdit edit(state, entity_name, desc.name);
					bool finished = false;
					const bool grid_changed = DrawJsonGrid(value, &finished);
					if (grid_changed) {
						std::string error;
						if (!ComponentOps::ApplyValue(state, entity_name, desc.name, value, error)) {
							state.status_message = "Edit failed: " + error;
						}
					}
					edit.Grid(grid_changed, finished);
					edit.Commit();
				}
				ImGui::PopID();
			}
			ImGui::PopID();
		}

		static void DrawOpaqueComponents(EditorState& state, const std::string& entity_name)
		{
			auto it = state.opaque_components.find(entity_name);
			if (it == state.opaque_components.end() || it->second.empty()) {
				return;
			}
			for (auto& [name, value] : it->second) {
				ImGui::PushID(name.c_str());
				if (ImGui::CollapsingHeader(name.c_str())) {
					ImGui::TextDisabled("(defined by the game, not by the editor)");
					//Read-only on purpose: the editor has no type behind this block, so
					//it cannot tell a meaningful edit from a corrupting one. It is shown
					//so the data is visible, and preserved byte-for-byte on save.
					ImGui::TextUnformatted(value.dump(2).c_str());
				}
				ImGui::PopID();
			}
		}

		static void DrawAddComponent(EditorState& state, const std::string& entity_name,
			Coordinator* c, Entity e)
		{
			if (ImGui::Button("Add Component")) {
				ImGui::OpenPopup("add_component_popup");
			}
			if (!ImGui::BeginPopup("add_component_popup")) {
				return;
			}
			bool any = false;
			for (const ComponentDesc& desc : ComponentRegistry::Instance().All()) {
				if (!desc.Addable() || desc.has(c, e)) {
					continue;
				}
				any = true;
				if (ImGui::MenuItem(desc.name.c_str())) {
					std::string error;
					//An empty payload means "defaults": every FromJson treats missing
					//keys as "leave alone", so this is the component as constructed.
					if (!ComponentOps::AddComponent(state, entity_name, desc.name,
						nlohmann::json::object(), error)) {
						state.status_message = "Add failed: " + error;
					}
				}
			}
			if (!any) {
				ImGui::TextDisabled("(nothing left to add)");
			}
			ImGui::EndPopup();
		}

		void Draw(EditorState& state)
		{
			ImGui::Begin(EditorLayout::INSPECTOR_WINDOW);

			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr || state.selected_entity == INVALID_ENTITY_ID) {
				ImGui::TextUnformatted("No entity selected.");
				ImGui::End();
				return;
			}
			Entity e = state.selected_entity;
			if (!c->ContainsComponent<Base>(e)) {
				ImGui::TextUnformatted("(no Base component)");
				ImGui::End();
				return;
			}

			//Right-align widgets leaving a fixed gutter so labels stay readable in a
			//narrow docked panel instead of being clipped by the window edge.
			ImGui::PushItemWidth(-110.0f);

			//Base is drawn first and unconditionally: it owns the entity's name, which
			//is the header of the whole panel rather than one section among many.
			DrawBase(state, c, e);

			//One section per component the entity actually has, driven by the ECS
			//component registry rather than a hardcoded list - so a component a *game*
			//registers appears here too, even though this binary has no compile-time
			//knowledge of it (it falls through to the generic grid below).
			const std::string entity_name = c->GetConstComponent<Base>(e).name;
			for (const ComponentDesc& desc : ComponentRegistry::Instance().All()) {
				if (desc.name == Base::NAME || !desc.has(c, e)) {
					continue;
				}
				DrawComponentSection(state, c, e, entity_name, desc);
			}

			//Components carried by the level that this binary has no registered type
			//for. Shown so they are visibly part of the entity rather than invisibly
			//round-tripping, and removable, but read-only: without the type there is
			//nothing to validate an edit against.
			DrawOpaqueComponents(state, entity_name);

			ImGui::PopItemWidth();
			ImGui::Spacing();
			DrawAddComponent(state, entity_name, c, e);

			ImGui::Spacing();
			ImGui::TextDisabled("Component add/remove, transforms, names, copies and\n"
				"deletions persist on Save Level.");

			ImGui::End();
		}

	}
}
