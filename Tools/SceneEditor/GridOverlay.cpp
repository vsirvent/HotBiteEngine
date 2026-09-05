#include "GridOverlay.h"

#include "imgui.h"
#include <Components/Camera.h>
#include <Systems/CameraSystem.h>
#include <algorithm>
#include <cmath>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::Systems;
using namespace DirectX;

namespace HotBiteEditor {
	namespace GridOverlay {

		static constexpr ImU32 COLOR_LINE = IM_COL32(120, 120, 130, 70);
		static constexpr ImU32 COLOR_AXIS = IM_COL32(150, 150, 160, 140);
		//Lines drawn each side of the camera snap point, per axis - a cap rather
		//than however many a small grid_size would otherwise need to reach
		//target_extent, so a fine step thins out (larger line_step) instead of
		//turning into thousands of draw calls.
		static constexpr int MAX_LINES_PER_AXIS = 100;
		//A line closer to the camera than this (in clip-space w) is behind the
		//near plane; ViewportOverlay::WorldToScreen would just drop a segment
		//with either endpoint this close, which is what silently culled most of
		//a ground-sized grid (a long line very often has one end behind the
		//camera even though the other is in plain view) - see ClipAndDraw below.
		static constexpr float NEAR_W = 0.05f;

		//World-space segment -> a screen-space line, clipped against the near
		//plane in clip space (where the clip boundary is the linear w == NEAR_W)
		//instead of dropping the whole segment when one end is behind it. A grid
		//line is long enough, relative to the camera, that "drop it" and "half of
		//it is off-screen anyway" are not the same thing.
		static void ClipAndDraw(ImDrawList* dl, const matrix& view_proj, const ImVec2& display,
			const vector3d& a, const vector3d& b, ImU32 color, float thickness)
		{
			XMVECTOR clip_a = XMVector4Transform(a, view_proj);
			XMVECTOR clip_b = XMVector4Transform(b, view_proj);
			float wa = XMVectorGetW(clip_a);
			float wb = XMVectorGetW(clip_b);
			if (wa < NEAR_W && wb < NEAR_W) {
				return; //the whole segment is behind the camera
			}
			if (wa < NEAR_W) {
				clip_a = XMVectorLerp(clip_a, clip_b, (NEAR_W - wa) / (wb - wa));
				wa = NEAR_W;
			}
			else if (wb < NEAR_W) {
				clip_b = XMVectorLerp(clip_b, clip_a, (NEAR_W - wb) / (wa - wb));
				wb = NEAR_W;
			}
			ImVec2 pa((XMVectorGetX(clip_a) / wa * 0.5f + 0.5f) * display.x,
				(0.5f - XMVectorGetY(clip_a) / wa * 0.5f) * display.y);
			ImVec2 pb((XMVectorGetX(clip_b) / wb * 0.5f + 0.5f) * display.x,
				(0.5f - XMVectorGetY(clip_b) / wb * 0.5f) * display.y);
			dl->AddLine(pa, pb, color, thickness);
		}

		void Draw(EditorState& state)
		{
			if (!state.grid_lines_visible || state.grid_size <= 0.0f) {
				return;
			}
			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr) {
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

			const float step = state.grid_size;
			//How far out the grid should reach to cover "the full scene" rather
			//than a fixed patch: scales with how high above the plane the camera
			//is, clamped to a sane range so a close-up view isn't swamped and a
			//far-out one still ends somewhere.
			const float target_extent = (std::min)(300.0f,
				(std::max)(20.0f, std::fabs(cam->world_position.y) * 2.5f));
			const int lines_wanted = (int)(target_extent / step);
			const int skip = (std::max)(1, (lines_wanted + MAX_LINES_PER_AXIS - 1) / MAX_LINES_PER_AXIS);
			const float line_step = step * (float)skip;
			const int half_count = (std::min)(MAX_LINES_PER_AXIS, (int)(target_extent / line_step));
			const float extent = half_count * line_step;
			//Snap the visible patch to the grid itself, centered near the camera,
			//so the lines stay put as the camera moves instead of swimming.
			const float cx = std::roundf(cam->world_position.x / line_step) * line_step;
			const float cz = std::roundf(cam->world_position.z / line_step) * line_step;

			for (int k = -half_count; k <= half_count; ++k) {
				float x = cx + k * line_step;
				ImU32 color = (k == 0) ? COLOR_AXIS : COLOR_LINE;
				ClipAndDraw(dl, view_proj, display,
					XMVectorSet(x, 0.0f, cz - extent, 1.0f),
					XMVectorSet(x, 0.0f, cz + extent, 1.0f), color, 1.0f);
			}
			for (int k = -half_count; k <= half_count; ++k) {
				float z = cz + k * line_step;
				ImU32 color = (k == 0) ? COLOR_AXIS : COLOR_LINE;
				ClipAndDraw(dl, view_proj, display,
					XMVectorSet(cx - extent, 0.0f, z, 1.0f),
					XMVectorSet(cx + extent, 0.0f, z, 1.0f), color, 1.0f);
			}
		}
	}
}
