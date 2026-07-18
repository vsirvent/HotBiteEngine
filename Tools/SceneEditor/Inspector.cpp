#include "Inspector.h"
#include "EditorHistory.h"
#include "EditorLayout.h"

#include "imgui.h"
#include <Components/Base.h>
#include <Components/Camera.h>
#include <Components/Lights.h>
#include <Components/Physics.h>
#include <Components/Particles.h>
#include <Components/Sky.h>
#include <DirectXMath.h>
#include <cmath>

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
		//what needs to be written back on save.
		static void CommitTransformEdit(EditorState& state, Entity entity, const Base& base, Transform& t)
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
			CommitTransformEdit(state, state.selected_entity, base, t);
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
			t.position = snapshot.position;
			t.rotation = snapshot.rotation;
			t.scale = snapshot.scale;
			CommitTransformEdit(state, e, base, t);
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

		static constexpr ImGuiTreeNodeFlags SECTION_FLAGS = ImGuiTreeNodeFlags_DefaultOpen;

		static void DrawBase(Coordinator* c, Entity e)
		{
			if (!ImGui::CollapsingHeader("Base", SECTION_FLAGS)) {
				return;
			}
			Base& b = c->GetComponent<Base>(e);
			ImGui::Text("Name: %s", b.name.c_str());
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
			if (!ImGui::CollapsingHeader("Transform", SECTION_FLAGS)) {
				return;
			}
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
				CommitTransformEdit(state, e, base, t);
			}
			if (finished) {
				RecordTransformEdit(state, base.name, pending_before);
			}
		}

		static void DrawBounds(Coordinator* c, Entity e)
		{
			if (!ImGui::CollapsingHeader("Bounds")) {
				return;
			}
			const Bounds& b = c->GetConstComponent<Bounds>(e);
			ImGui::Text("Local  center  %.2f %.2f %.2f", b.local_box.Center.x, b.local_box.Center.y, b.local_box.Center.z);
			ImGui::Text("Local  extents %.2f %.2f %.2f", b.local_box.Extents.x, b.local_box.Extents.y, b.local_box.Extents.z);
			ImGui::Text("World  center  %.2f %.2f %.2f", b.final_box.Center.x, b.final_box.Center.y, b.final_box.Center.z);
			ImGui::Text("World  extents %.2f %.2f %.2f", b.final_box.Extents.x, b.final_box.Extents.y, b.final_box.Extents.z);
		}

		static void DrawMaterial(Coordinator* c, Entity e)
		{
			if (!ImGui::CollapsingHeader("Material", SECTION_FLAGS)) {
				return;
			}
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
			if (!ImGui::CollapsingHeader("Mesh", SECTION_FLAGS)) {
				return;
			}
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
			if (!ImGui::CollapsingHeader("Ambient Light", SECTION_FLAGS)) {
				return;
			}
			AmbientLight::Data& d = c->GetComponent<AmbientLight>(e).GetData();
			ImGui::ColorEdit3("Color down", &d.colorDown.x);
			ImGui::ColorEdit3("Color up", &d.colorUp.x);
		}

		static void DrawDirectionalLight(Coordinator* c, Entity e)
		{
			if (!ImGui::CollapsingHeader("Directional Light", SECTION_FLAGS)) {
				return;
			}
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
			if (!ImGui::CollapsingHeader("Point Light", SECTION_FLAGS)) {
				return;
			}
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
			if (!ImGui::CollapsingHeader("Physics", SECTION_FLAGS)) {
				return;
			}
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
			if (!ImGui::CollapsingHeader("Camera", SECTION_FLAGS)) {
				return;
			}
			const Camera& cam = c->GetConstComponent<Camera>(e);
			ImGui::Text("Position:  %.2f %.2f %.2f", cam.world_position.x, cam.world_position.y, cam.world_position.z);
			ImGui::Text("Direction: %.2f %.2f %.2f", cam.direction.x, cam.direction.y, cam.direction.z);
			ImGui::Text("Rotation:  %.2f %.2f %.2f", cam.rotation.x, cam.rotation.y, cam.rotation.z);
		}

		static void DrawSky(Coordinator* c, Entity e)
		{
			if (!ImGui::CollapsingHeader("Sky", SECTION_FLAGS)) {
				return;
			}
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
			if (!ImGui::CollapsingHeader("Particles", SECTION_FLAGS)) {
				return;
			}
			Particles& p = c->GetComponent<Particles>(e);
			ImGui::Text("Emitters: %d", (int)p.data.GetData().size());
		}

		static void DrawLighted(Coordinator* c, Entity e)
		{
			if (!ImGui::CollapsingHeader("Lighted")) {
				return;
			}
			const Lighted& l = c->GetConstComponent<Lighted>(e);
			ImGui::Text("Point lights: %d  Dir lights: %d",
				(int)l.point_lights.size(), (int)l.dir_lights.size());
			ImGui::Text("Shadow maps: %d", (int)(l.shadows.size() + l.dir_shadows.size()));
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

			//One section per component present on the entity, in a stable order.
			//The ECS has no runtime component reflection, so this enumerates every
			//type World::PreLoad registers explicitly.
			DrawBase(c, e);
			if (c->ContainsComponent<Transform>(e)) {
				DrawTransform(state, c, e);
			}
			if (c->ContainsComponent<Bounds>(e)) {
				DrawBounds(c, e);
			}
			if (c->ContainsComponent<Mesh>(e)) {
				DrawMesh(c, e);
			}
			if (c->ContainsComponent<Material>(e)) {
				DrawMaterial(c, e);
			}
			if (c->ContainsComponent<AmbientLight>(e)) {
				DrawAmbientLight(c, e);
			}
			if (c->ContainsComponent<DirectionalLight>(e)) {
				DrawDirectionalLight(c, e);
			}
			if (c->ContainsComponent<PointLight>(e)) {
				DrawPointLight(c, e);
			}
			if (c->ContainsComponent<Physics>(e)) {
				DrawPhysics(c, e);
			}
			if (c->ContainsComponent<Camera>(e)) {
				DrawCamera(c, e);
			}
			if (c->ContainsComponent<Sky>(e)) {
				DrawSky(c, e);
			}
			if (c->ContainsComponent<Particles>(e)) {
				DrawParticles(c, e);
			}
			if (c->ContainsComponent<Lighted>(e)) {
				DrawLighted(c, e);
			}
			if (c->ContainsComponent<Player>(e)) {
				if (ImGui::CollapsingHeader("Player")) {
					ImGui::TextDisabled("(tag component, no properties)");
				}
			}

			ImGui::PopItemWidth();
			ImGui::Spacing();
			ImGui::TextDisabled("Only transform edits persist on Save Level.");

			ImGui::End();
		}

	}
}
