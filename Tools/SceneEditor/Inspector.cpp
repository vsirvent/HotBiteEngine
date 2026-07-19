#include "Inspector.h"
#include "ComponentOps.h"
#include "EditorHistory.h"
#include "EditorLayout.h"
#include "EntityOps.h"

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
#include <cmath>
#include <cstring>

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

		//Shared post-edit bookkeeping for both the interactive (Draw) and programmatic
		//(ApplyTransform/ApplySnapshot) paths: marks the transform dirty and records
		//what needs to be written back on save. `before` is the Transform as it was
		//prior to this edit; it decides whether the physics collider has to be rebuilt.
		static void CommitTransformEdit(EditorState& state, Entity entity, const Base& base,
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
							c->GetComponent<Bounds>(entity).bounding_box.Extents,
							t.scale, t.rotation);
					}
				}
			}

			if (state.instance_entity_ids.count(entity) != 0) {
				//This entity is an editor-placed instance: update its bookkeeping
				//entry directly so a save writes the new transform out.
				for (auto& inst : state.placed_instances) {
					if (inst.name == base.name) {
						inst.position = t.position;
						inst.rotation = t.rotation;
						inst.scale = t.scale;
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
			if (b.parent != INVALID_ENTITY_ID) {
				ImGui::Text("Parent: %u", (unsigned)b.parent);
				ImGui::Checkbox("Parent position", &b.parent_position);
				ImGui::Checkbox("Parent rotation", &b.parent_rotation);
			}
			ImGui::Checkbox("Visible", &b.visible);
			ImGui::SameLine();
			ImGui::Checkbox("Scene visible", &b.scene_visible);
			ImGui::Checkbox("Cast shadow", &b.cast_shadow);
			ImGui::SameLine();
			ImGui::Checkbox("Draw depth", &b.draw_depth);
			ImGui::Checkbox("Static", &b.is_static);
			int draw_method = (int)b.draw_method;
			if (ImGui::Combo("Draw mode", &draw_method, "Always\0Screen only\0")) {
				b.draw_method = (eDrawMethod)draw_method;
			}
			int pass = (int)b.pass;
			if (ImGui::DragInt("Pass", &pass, 0.1f, 0, 16) && pass >= 0) {
				b.pass = (uint32_t)pass;
			}
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
		}

		static void DrawMaterial(Coordinator* c, Entity e)
		{
			Material& m = c->GetComponent<Material>(e);
			if (m.data == nullptr) {
				ImGui::TextDisabled("(no material data)");
				return;
			}
			Core::MaterialData& md = *m.data;
			ImGui::Text("Name: %s", md.name.c_str());
			Core::MaterialProps& p = md.props;
			ImGui::ColorEdit4("Diffuse", &p.diffuseColor.x);
			ImGui::ColorEdit4("Ambient", &p.ambientColor.x);
			ImGui::DragFloat("Specular", &p.specIntensity, 0.01f, 0.0f, 16.0f);
			ImGui::SliderFloat("Opacity", &p.opacity, 0.0f, 1.0f);
			ImGui::DragFloat("Emission", &p.emission, 0.01f, 0.0f, 100.0f);
			ImGui::ColorEdit3("Emissive", &p.emission_color.x);
			ImGui::DragFloat("Bloom", &p.bloom_scale, 0.01f, 0.0f, 10.0f);
			ImGui::DragFloat("RT reflex", &p.rt_reflex, 0.01f, 0.0f, 1.0f);
			ImGui::DragFloat("Parallax", &p.parallax_scale, 0.01f);
			ImGui::DragFloat("Displace", &md.displacement_scale, 0.01f);
			ImGui::DragFloat("Tessellate", &md.tessellation_factor, 0.1f, 0.0f, 64.0f);

			const auto& tn = md.texture_names;
			auto texture_row = [](const char* label, const std::string& file) {
				if (!file.empty()) {
					ImGui::Text("%s: %s", label, file.c_str());
				}
			};
			texture_row("Diffuse map", tn.diffuse_texname);
			texture_row("Normal map", tn.normal_textname);
			texture_row("Height map", tn.high_textname);
			texture_row("Specular map", tn.spec_textname);
			texture_row("AO map", tn.ao_textname);
			texture_row("ARM map", tn.arm_textname);
			texture_row("Emission map", tn.emission_textname);
			texture_row("Opacity map", tn.opacity_textname);
			if (m.multi_material.multi_texture_count > 0) {
				ImGui::Text("Multi-texture layers: %u", m.multi_material.multi_texture_count);
			}
		}

		static void DrawMesh(Coordinator* c, Entity e)
		{
			Mesh& mesh = c->GetComponent<Mesh>(e);
			Core::MeshData* data = mesh.GetData();
			if (data == nullptr) {
				ImGui::TextDisabled("(no mesh data)");
				return;
			}
			ImGui::Text("Name: %s", data->name.c_str());
			ImGui::Text("Vertices: %u  Indices: %u", data->vertexCount, data->indexCount);
			ImGui::Text("Skeletons: %d", (int)data->skeletons.size());
			std::string anim = mesh.GetCurrentAnimationName();
			if (!anim.empty()) {
				ImGui::Text("Animation: %s (frame %d)", anim.c_str(), mesh.GetCurrentFrame());
				ImGui::DragFloat("Anim speed", &mesh.current_animation.speed, 0.01f, 0.0f, 10.0f);
			}
		}

		static void DrawAmbientLight(Coordinator* c, Entity e)
		{
			AmbientLight::Data& d = c->GetComponent<AmbientLight>(e).GetData();
			ImGui::ColorEdit3("Color down", &d.colorDown.x);
			ImGui::ColorEdit3("Color up", &d.colorUp.x);
		}

		static void DrawDirectionalLight(Coordinator* c, Entity e)
		{
			DirectionalLight& l = c->GetComponent<DirectionalLight>(e);
			DirectionalLight::Data& d = l.GetData();
			bool changed = false;
			changed |= ImGui::ColorEdit3("Color", &d.color.x);
			changed |= ImGui::DragFloat("Intensity", &d.intensity, 0.05f, 0.0f, MAX_INTENSITY);
			if (ImGui::DragFloat3("Direction", &d.direction.x, 0.01f, -1.0f, 1.0f)) {
				//Keep the light direction normalized; a zero vector would break the
				//shadow view matrix, so ignore edits that pass through it.
				float len = std::sqrtf(d.direction.x * d.direction.x + d.direction.y * d.direction.y + d.direction.z * d.direction.z);
				if (len > 1e-4f) {
					d.direction = { d.direction.x / len, d.direction.y / len, d.direction.z / len };
					changed = true;
				}
			}
			changed |= ImGui::DragFloat3("Position", &d.position.x, 0.05f);
			changed |= ImGui::DragFloat("Range", &d.range, 0.1f, 0.0f, 10000.0f);
			changed |= ImGui::DragFloat("Fog density", &d.density, 0.001f, 0.0f, 10.0f);
			bool fog = (d.flags & DIR_LIGHT_FLAG_FOG) != 0;
			if (ImGui::Checkbox("Fog", &fog)) {
				l.SetFog(fog);
				changed = true;
			}
			ImGui::SameLine();
			bool inverse = (d.flags & DIR_LIGHT_FLAG_INVERSE) != 0;
			if (ImGui::Checkbox("Inverse", &inverse)) {
				l.SetInverse(inverse);
				changed = true;
			}
			ImGui::Text("Casts shadow: %s", l.CastShadow() ? "yes" : "no");
			if (changed) {
				l.SetDirty();
			}
		}

		static void DrawPointLight(Coordinator* c, Entity e)
		{
			PointLight& l = c->GetComponent<PointLight>(e);
			PointLight::Data& d = l.GetData();
			ImGui::ColorEdit3("Color", &d.color.x);
			ImGui::DragFloat3("Position", &d.position.x, 0.05f);
			ImGui::DragFloat("Range", &d.range, 0.1f, 0.0f, 10000.0f);
			ImGui::DragFloat("Fog density", &d.density, 0.001f, 0.0f, 10.0f);
			ImGui::DragFloat("Tilt ratio", &d.tilt_ratio, 0.1f);
			ImGui::Text("Casts shadow: %s", l.CastShadow() ? "yes" : "no");
		}

		static void DrawPhysics(Coordinator* c, Entity e)
		{
			Physics& ph = c->GetComponent<Physics>(e);
			const char* type = "static";
			if (ph.type == reactphysics3d::BodyType::DYNAMIC) {
				type = "dynamic";
			}
			else if (ph.type == reactphysics3d::BodyType::KINEMATIC) {
				type = "kinematic";
			}
			static const char* SHAPE_NAMES[] = { "none", "capsule", "box", "sphere" };
			int shape = (int)ph.shape;
			ImGui::Text("Body: %s  Shape: %s", type,
				(shape >= 0 && shape < 4) ? SHAPE_NAMES[shape] : "?");
			//bounce/friction < 0 mean "engine default": editing them here only takes
			//effect on the live collider material when one exists.
			if (ImGui::DragFloat("Bounce", &ph.bounce, 0.01f, 0.0f, 1.0f) && ph.collider != nullptr) {
				ph.collider->getMaterial().setBounciness(ph.bounce);
			}
			if (ImGui::DragFloat("Friction", &ph.friction, 0.01f, 0.0f, 1.0f) && ph.collider != nullptr) {
				ph.collider->getMaterial().setFrictionCoefficient(ph.friction);
			}
			if (ImGui::DragFloat("Air friction", &ph.air_friction, 0.01f, 0.0f, 1.0f) && ph.body != nullptr) {
				ph.body->setLinearDamping(ph.air_friction);
			}
		}

		static void DrawCamera(Coordinator* c, Entity e)
		{
			const Camera& cam = c->GetConstComponent<Camera>(e);
			ImGui::Text("Position:  %.2f %.2f %.2f", cam.world_position.x, cam.world_position.y, cam.world_position.z);
			ImGui::Text("Direction: %.2f %.2f %.2f", cam.direction.x, cam.direction.y, cam.direction.z);
			ImGui::Text("Rotation:  %.2f %.2f %.2f", cam.rotation.x, cam.rotation.y, cam.rotation.z);
		}

		static void DrawSky(Coordinator* c, Entity e)
		{
			Sky& sky = c->GetComponent<Sky>(e);
			int hour = (int)(sky.second_of_day / 3600.0f) % 24;
			int minute = (int)(sky.second_of_day / 60.0f) % 60;
			ImGui::Text("Time of day: %02d:%02d", hour, minute);
			ImGui::DragFloat("Second of day", &sky.second_of_day, 60.0f, 0.0f, 86400.0f);
			ImGui::DragFloat("Time speed", &sky.second_speed, 0.1f, 0.0f, 10000.0f);
			ImGui::SliderFloat("Cloud density", &sky.cloud_density, 0.0f, 1.0f);
			ImGui::ColorEdit3("Day color", &sky.day_backcolor.x);
			ImGui::ColorEdit3("Mid color", &sky.mid_backcolor.x);
			ImGui::ColorEdit3("Night color", &sky.night_backcolor.x);
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
				{ Mesh::NAME,             [](EditorState& s, Coordinator* c, Entity e) { DrawMesh(c, e); } },
				{ Material::NAME,         [](EditorState& s, Coordinator* c, Entity e) { DrawMaterial(c, e); } },
				{ AmbientLight::NAME,     [](EditorState& s, Coordinator* c, Entity e) { DrawAmbientLight(c, e); } },
				{ DirectionalLight::NAME, [](EditorState& s, Coordinator* c, Entity e) { DrawDirectionalLight(c, e); } },
				{ PointLight::NAME,       [](EditorState& s, Coordinator* c, Entity e) { DrawPointLight(c, e); } },
				{ Physics::NAME,          [](EditorState& s, Coordinator* c, Entity e) { DrawPhysics(c, e); } },
				{ Sky::NAME,              [](EditorState& s, Coordinator* c, Entity e) { DrawSky(c, e); } },
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

		// Editable widgets for a component this binary has no type for, built from the
		// shape of its JSON alone. Enough for the numbers, flags and names that make up
		// most game components; nested objects/arrays are shown as text rather than
		// guessed at. Returns true when something changed.
		static bool DrawJsonGrid(nlohmann::json& value)
		{
			bool changed = false;
			for (auto& [key, field] : value.items()) {
				ImGui::PushID(key.c_str());
				if (field.is_boolean()) {
					bool v = field.get<bool>();
					if (ImGui::Checkbox(key.c_str(), &v)) {
						field = v;
						changed = true;
					}
				}
				else if (field.is_number_float()) {
					float v = field.get<float>();
					if (ImGui::DragFloat(key.c_str(), &v, 0.05f)) {
						field = v;
						changed = true;
					}
				}
				else if (field.is_number_integer()) {
					int v = field.get<int>();
					if (ImGui::DragInt(key.c_str(), &v, 0.1f)) {
						field = v;
						changed = true;
					}
				}
				else if (field.is_string()) {
					char buf[256] = "";
					strncpy_s(buf, field.get<std::string>().c_str(), sizeof(buf) - 1);
					if (ImGui::InputText(key.c_str(), buf, sizeof(buf),
						ImGuiInputTextFlags_EnterReturnsTrue)) {
						field = std::string(buf);
						changed = true;
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
			const bool open = ImGui::CollapsingHeader(desc.name.c_str(),
				ImGuiTreeNodeFlags_DefaultOpen);

			if (desc.Removable()) {
				//Right-aligned on the header's own line, so it reads as belonging to the
				//header rather than to the first property.
				ImGui::SameLine(ImGui::GetWindowWidth() - 30.0f);
				if (ImGui::SmallButton("x")) {
					std::string error;
					if (!ComponentOps::RemoveComponent(state, entity_name, desc.name, error)) {
						state.status_message = "Remove failed: " + error;
					}
					//The component is gone; nothing left to draw this frame.
					ImGui::PopID();
					return;
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Remove %s from %s", desc.name.c_str(), entity_name.c_str());
				}
			}

			if (open) {
				ComponentDrawer drawer = FindDrawer(desc.name);
				if (drawer != nullptr) {
					drawer(state, c, e);
				}
				else {
					//A game component: no hand-written editor, so show its serialized
					//state through the generic grid.
					ImGui::TextDisabled("(game component)");
					nlohmann::json value = desc.serialize(state.world->MakeSerializeContext(), e);
					if (DrawJsonGrid(value)) {
						desc.apply(state.world->MakeSerializeContext(), e, value);
						state.component_deltas[entity_name].added[desc.name] = value;
					}
				}
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
