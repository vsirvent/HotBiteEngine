#include "Inspector.h"
#include "EditorLayout.h"

#include "imgui.h"
#include <Components/Base.h>
#include <DirectXMath.h>
#include <cmath>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;

namespace HotBiteEditor {
	namespace Inspector {

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

			//Manual quaternion -> Euler (pitch=X, yaw=Y, roll=Z) extraction, matching
			//DirectX::XMQuaternionRotationRollPitchYaw's composition convention used by
			//float3_to_quaternion (Defines.h) for the reverse direction on edit.
			float sinp = 2.0f * (q.w * q.x - q.y * q.z);
			float pitch = std::fabsf(sinp) >= 1.0f
				? std::copysignf(DirectX::XM_PIDIV2, sinp)
				: std::asinf(sinp);
			float yaw = std::atan2f(2.0f * (q.w * q.y + q.z * q.x), 1.0f - 2.0f * (q.x * q.x + q.y * q.y));
			float roll = std::atan2f(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.x * q.x + q.z * q.z));

			state.inspector_euler_degrees = {
				DirectX::XMConvertToDegrees(pitch),
				DirectX::XMConvertToDegrees(yaw),
				DirectX::XMConvertToDegrees(roll)
			};
		}

		//Shared post-edit bookkeeping for both the interactive (Draw) and programmatic
		//(ApplyTransform) paths: marks the transform dirty and records what needs to be
		//written back on save.
		static void CommitTransformEdit(EditorState& state, const Base& base, Transform& t)
		{
			t.dirty = true;

			if (state.instance_entity_ids.count(state.selected_entity) != 0) {
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
			std::string& error)
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
			CommitTransformEdit(state, base, t);
			return true;
		}

		void Draw(EditorState& state)
		{
			EditorLayout::PlaceInspector(state);
			ImGui::Begin("Inspector");

			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr || state.selected_entity == INVALID_ENTITY_ID) {
				ImGui::TextUnformatted("No entity selected.");
				ImGui::End();
				return;
			}

			if (c->ContainsComponent<Base>(state.selected_entity)) {
				const Base& base = c->GetComponent<Base>(state.selected_entity);
				ImGui::Text("Name: %s", base.name.c_str());
				ImGui::Text("Id: %d", (int)state.selected_entity);

				if (!c->ContainsComponent<Transform>(state.selected_entity)) {
					ImGui::TextUnformatted("(no Transform component)");
					ImGui::End();
					return;
				}

				Transform& t = c->GetComponent<Transform>(state.selected_entity);
				bool changed = false;
				changed |= ImGui::DragFloat3("Position", &t.position.x, 0.05f);
				changed |= ImGui::DragFloat3("Scale", &t.scale.x, 0.01f);
				changed |= ImGui::DragFloat3("Rotation (deg)", &state.inspector_euler_degrees.x, 0.5f);

				if (changed) {
					t.rotation = float3_to_quaternion(state.inspector_euler_degrees);
					CommitTransformEdit(state, base, t);
				}
			}
			else {
				ImGui::TextUnformatted("(no Base component)");
			}

			ImGui::End();
		}

	}
}
