#include "SelectionGizmo.h"

#include "imgui.h"
#include <Components/Base.h>
#include <Components/Camera.h>
#include <Systems/CameraSystem.h>
#include <algorithm>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::Systems;
using namespace DirectX;

namespace HotBiteEditor {
	namespace SelectionGizmo {

		static constexpr ImU32 COLOR_AABB = IM_COL32(255, 200, 40, 220);
		static constexpr ImU32 COLOR_AXIS_X = IM_COL32(235, 70, 70, 255);
		static constexpr ImU32 COLOR_AXIS_Y = IM_COL32(90, 220, 90, 255);
		static constexpr ImU32 COLOR_AXIS_Z = IM_COL32(80, 140, 255, 255);

		//Projects a world position through the camera's view-projection into pixel
		//coordinates. False when the point is behind (or grazing) the near plane;
		//segments with such an endpoint are skipped rather than clipped, which is
		//fine for an editor overlay.
		static bool WorldToScreen(const matrix& view_proj, const ImVec2& display,
			const vector3d& world, ImVec2& out)
		{
			vector3d clip = XMVector4Transform(XMVectorSetW(world, 1.0f), view_proj);
			float w = XMVectorGetW(clip);
			if (w < 0.05f) {
				return false;
			}
			out.x = (XMVectorGetX(clip) / w * 0.5f + 0.5f) * display.x;
			out.y = (0.5f - XMVectorGetY(clip) / w * 0.5f) * display.y;
			return true;
		}

		static void DrawSegment(ImDrawList* dl, const matrix& view_proj, const ImVec2& display,
			const vector3d& a, const vector3d& b, ImU32 color, float thickness)
		{
			ImVec2 pa, pb;
			if (WorldToScreen(view_proj, display, a, pa) && WorldToScreen(view_proj, display, b, pb)) {
				dl->AddLine(pa, pb, color, thickness);
			}
		}

		void Draw(EditorState& state)
		{
			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr || state.selected_entity == INVALID_ENTITY_ID ||
				!c->ContainsComponent<Transform>(state.selected_entity)) {
				return;
			}
			auto camera_system = c->GetSystem<CameraSystem>();
			if (camera_system == nullptr || camera_system->GetCameras().GetData().empty()) {
				return;
			}
			const Camera* cam = camera_system->GetCameras().GetData()[0].camera;
			const matrix& view_proj = cam->xm_view_projection;
			ImVec2 display = ImGui::GetIO().DisplaySize;
			ImDrawList* dl = ImGui::GetBackgroundDrawList();

			const Transform& t = c->GetComponent<Transform>(state.selected_entity);

			//AABB: the 12 edges of the world-space bounding box.
			float max_extent = 0.0f;
			if (c->ContainsComponent<Bounds>(state.selected_entity)) {
				const box& b = c->GetComponent<Bounds>(state.selected_entity).final_box;
				max_extent = (std::max)({ b.Extents.x, b.Extents.y, b.Extents.z });
				vector3d corners[8];
				for (int i = 0; i < 8; ++i) {
					corners[i] = XMVectorSet(
						b.Center.x + ((i & 1) ? b.Extents.x : -b.Extents.x),
						b.Center.y + ((i & 2) ? b.Extents.y : -b.Extents.y),
						b.Center.z + ((i & 4) ? b.Extents.z : -b.Extents.z),
						1.0f);
				}
				//Corner i and i|bit differ only in one axis: exactly the 12 edges.
				for (int i = 0; i < 8; ++i) {
					for (int bit = 1; bit < 8; bit <<= 1) {
						if ((i & bit) == 0) {
							DrawSegment(dl, view_proj, display, corners[i], corners[i | bit], COLOR_AABB, 1.5f);
						}
					}
				}
			}

			//Local axes from the entity pivot, rotated by the entity's orientation.
			//Sized against the AABB so they stay visible at any object scale.
			float axis_len = (std::max)(max_extent * 1.4f, 1.0f);
			vector3d origin = XMLoadFloat3(&t.position);
			vector4d rot = XMLoadFloat4(&t.rotation);
			struct { vector3d dir; ImU32 color; const char* label; } axes[3] = {
				{ XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), COLOR_AXIS_X, "X" },
				{ XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), COLOR_AXIS_Y, "Y" },
				{ XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), COLOR_AXIS_Z, "Z" },
			};
			for (const auto& axis : axes) {
				vector3d end = origin + XMVector3Rotate(axis.dir, rot) * axis_len;
				DrawSegment(dl, view_proj, display, origin, end, axis.color, 2.5f);
				ImVec2 label_pos;
				if (WorldToScreen(view_proj, display, end, label_pos)) {
					dl->AddText(label_pos, axis.color, axis.label);
				}
			}
		}

	}
}
