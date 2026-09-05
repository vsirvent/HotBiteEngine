#include "GridOverlay.h"
#include "ViewportOverlay.h"

#include "imgui.h"
#include <Components/Camera.h>
#include <Systems/CameraSystem.h>
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
		//Lines drawn each side of the camera, along each axis - a fixed count
		//rather than a fixed world radius, so a tiny grid_size still draws a
		//cheap, bounded number of segments (just a smaller patch of ground).
		static constexpr int HALF_LINES = 25;

		using ViewportOverlay::DrawSegment;

		void Draw(EditorState& state)
		{
			if (!state.grid_snap_enabled || state.grid_size <= 0.0f) {
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
			const float extent = step * HALF_LINES;
			//Snap the visible patch to the grid itself, centered near the camera,
			//so the lines stay put as the camera moves instead of swimming.
			const float cx = std::roundf(cam->world_position.x / step) * step;
			const float cz = std::roundf(cam->world_position.z / step) * step;

			for (int i = -HALF_LINES; i <= HALF_LINES; ++i) {
				float x = cx + i * step;
				ImU32 color = (i == 0) ? COLOR_AXIS : COLOR_LINE;
				DrawSegment(dl, view_proj, display,
					XMVectorSet(x, 0.0f, cz - extent, 1.0f),
					XMVectorSet(x, 0.0f, cz + extent, 1.0f), color, 1.0f);
			}
			for (int i = -HALF_LINES; i <= HALF_LINES; ++i) {
				float z = cz + i * step;
				ImU32 color = (i == 0) ? COLOR_AXIS : COLOR_LINE;
				DrawSegment(dl, view_proj, display,
					XMVectorSet(cx - extent, 0.0f, z, 1.0f),
					XMVectorSet(cx + extent, 0.0f, z, 1.0f), color, 1.0f);
			}
		}
	}
}
