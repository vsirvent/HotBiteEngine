#pragma once

#include "imgui.h"
#include <Defines.h>
#include <DirectXMath.h>

namespace HotBiteEditor {
	// Projection helpers shared by the things drawn over the 3D viewport into
	// ImGui's background draw list: the selection gizmo and the collider overlay.
	// Both work by projecting world points through the active camera rather than
	// issuing engine draw calls, so neither needs a render-pipeline change.
	namespace ViewportOverlay {

		// World position -> pixel coordinates. False when the point is behind (or
		// grazing) the near plane; segments with such an endpoint are skipped rather
		// than clipped, which is fine for an editor overlay.
		inline bool WorldToScreen(const HotBite::Engine::matrix& view_proj, const ImVec2& display,
			const HotBite::Engine::vector3d& world, ImVec2& out)
		{
			HotBite::Engine::vector3d clip = DirectX::XMVector4Transform(
				DirectX::XMVectorSetW(world, 1.0f), view_proj);
			float w = DirectX::XMVectorGetW(clip);
			if (w < 0.05f) {
				return false;
			}
			out.x = (DirectX::XMVectorGetX(clip) / w * 0.5f + 0.5f) * display.x;
			out.y = (0.5f - DirectX::XMVectorGetY(clip) / w * 0.5f) * display.y;
			return true;
		}

		inline void DrawSegment(ImDrawList* dl, const HotBite::Engine::matrix& view_proj,
			const ImVec2& display, const HotBite::Engine::vector3d& a,
			const HotBite::Engine::vector3d& b, ImU32 color, float thickness)
		{
			ImVec2 pa, pb;
			if (WorldToScreen(view_proj, display, a, pa) && WorldToScreen(view_proj, display, b, pb)) {
				dl->AddLine(pa, pb, color, thickness);
			}
		}
	}
}
