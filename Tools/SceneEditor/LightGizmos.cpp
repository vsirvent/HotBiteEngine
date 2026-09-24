#include "LightGizmos.h"
#include "EntityOps.h"
#include "Selection.h"
#include "ViewportOverlay.h"

#include "imgui.h"
#include <Components/Base.h>
#include <Components/Camera.h>
#include <Components/Lights.h>
#include <Systems/CameraSystem.h>
#include <algorithm>
#include <cmath>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::Systems;
using namespace DirectX;

namespace HotBiteEditor {
	namespace LightGizmos {

		//Segments per circle. Three of these make a point light's sphere.
		static constexpr int CIRCLE_SEGMENTS = 40;
		static constexpr float MARKER_RADIUS = 5.0f;

		static FrameInfo last_frame;

		struct DrawContext {
			ImDrawList* dl = nullptr;
			matrix view_proj{};
			ImVec2 display{};
			int segments = 0;
		};

		//The light's own colour, so two lights are told apart at a glance. Clamped and
		//floored: a colour authored above 1 would wrap in the 8-bit channel, and a black
		//light would draw a black gizmo on a dark viewport.
		static ImU32 LightColor(const float3& c, int alpha)
		{
			auto ch = [](float v) { return (int)(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
			int r = ch(c.x), g = ch(c.y), b = ch(c.z);
			int m = (std::max)({ r, g, b });
			if (m < 90) {
				int lift = 90 - m;
				r += lift; g += lift; b += lift;
			}
			return IM_COL32(r, g, b, alpha);
		}

		static void Segment(DrawContext& ctx, const vector3d& a, const vector3d& b, ImU32 color, float thickness)
		{
			++ctx.segments;
			ViewportOverlay::DrawSegment(ctx.dl, ctx.view_proj, ctx.display, a, b, color, thickness);
		}

		//A circle of `radius` about `center`, in the plane spanned by unit vectors u and v.
		static void Circle(DrawContext& ctx, const vector3d& center, const vector3d& u,
			const vector3d& v, float radius, ImU32 color, float thickness)
		{
			vector3d prev = center + u * radius;
			for (int k = 1; k <= CIRCLE_SEGMENTS; ++k) {
				float t = 2.0f * XM_PI * (float)k / (float)CIRCLE_SEGMENTS;
				vector3d p = center + (u * std::cosf(t) + v * std::sinf(t)) * radius;
				Segment(ctx, prev, p, color, thickness);
				prev = p;
			}
		}

		//The cone of a spotlight: apex at the light, opening along `axis`. The base sits
		//where the cone meets the light's range sphere (distance range*cos, radius
		//range*sin), so the drawn cone ends exactly where the light stops reaching -
		//a fixed-length cone would read as a reach the shader does not have.
		static void Cone(DrawContext& ctx, const vector3d& apex, const vector3d& axis,
			float range, float half_angle_deg, ImU32 color, float thickness, bool edges)
		{
			float a = XMConvertToRadians(half_angle_deg);
			float dist = range * std::cosf(a);
			float radius = range * std::sinf(a);
			vector3d ref = (std::fabsf(XMVectorGetY(axis)) < 0.99f)
				? XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f) : XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
			vector3d u = XMVector3Normalize(XMVector3Cross(ref, axis));
			vector3d v = XMVector3Cross(axis, u);
			vector3d base = apex + axis * dist;
			Circle(ctx, base, u, v, radius, color, thickness);
			if (edges) {
				for (int k = 0; k < 4; ++k) {
					float t = XM_PIDIV2 * (float)k;
					Segment(ctx, apex, base + (u * std::cosf(t) + v * std::sinf(t)) * radius, color, thickness);
				}
			}
		}

		static void DrawShape(DrawContext& ctx, const PointLight& light, bool primary)
		{
			const PointLight::Data& d = const_cast<PointLight&>(light).GetData();
			vector3d pos = XMVectorSet(d.position.x, d.position.y, d.position.z, 1.0f);
			int alpha = primary ? 235 : 120;
			float thickness = primary ? 1.6f : 1.0f;
			ImU32 color = LightColor(d.color, alpha);
			if (d.is_spot) {
				vector3d axis = XMVector3Normalize(XMVectorSet(d.direction.x, d.direction.y, d.direction.z, 0.0f));
				//Outer angle: where the light reaches zero. Inner: where it starts to fall
				//off - dimmer, so the soft edge between the two reads as a band.
				Cone(ctx, pos, axis, d.range, light.GetSpotOuterAngle(), color, thickness, true);
				Cone(ctx, pos, axis, d.range, light.GetSpotInnerAngle(),
					LightColor(d.color, alpha / 2), 1.0f, false);
				Segment(ctx, pos, pos + axis * d.range, color, thickness);
			}
			else {
				vector3d x = XMVectorSet(1, 0, 0, 0), y = XMVectorSet(0, 1, 0, 0), z = XMVectorSet(0, 0, 1, 0);
				Circle(ctx, pos, x, y, d.range, color, thickness);
				Circle(ctx, pos, x, z, d.range, color, thickness);
				Circle(ctx, pos, y, z, d.range, color, thickness);
			}
		}

		//A small sun: a filled disc in the light's colour with a dark ring behind it
		//(legible on a bright floor) and eight rays. Screen-space, so it is the same
		//size at any distance - it marks a position, not a volume.
		static bool DrawMarker(DrawContext& ctx, const PointLight& light, bool primary, ImVec2& screen)
		{
			const PointLight::Data& d = const_cast<PointLight&>(light).GetData();
			if (!ViewportOverlay::WorldToScreen(ctx.view_proj, ctx.display,
				XMVectorSet(d.position.x, d.position.y, d.position.z, 1.0f), screen)) {
				return false;
			}
			float r = primary ? MARKER_RADIUS + 2.0f : MARKER_RADIUS;
			ImU32 fill = LightColor(d.color, 255);
			ctx.dl->AddCircleFilled(screen, r + 1.5f, IM_COL32(15, 15, 15, 220), 20);
			ctx.dl->AddCircleFilled(screen, r, fill, 20);
			for (int k = 0; k < 8; ++k) {
				float t = XM_PIDIV4 * (float)k;
				float cx = std::cosf(t), sy = std::sinf(t);
				ctx.dl->AddLine(ImVec2(screen.x + cx * (r + 3.0f), screen.y + sy * (r + 3.0f)),
					ImVec2(screen.x + cx * (r + 7.0f), screen.y + sy * (r + 7.0f)), fill, 1.5f);
			}
			return true;
		}

		const FrameInfo& LastFrame()
		{
			return last_frame;
		}

		void Draw(EditorState& state)
		{
			last_frame = FrameInfo();
			if (state.light_view == LightView::Off && !state.light_positions) {
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

			DrawContext ctx;
			ctx.dl = ImGui::GetBackgroundDrawList();
			ctx.view_proj = cam->xm_view_projection;
			ctx.display = ImGui::GetIO().DisplaySize;

			for (const auto& [name, e] : c->GetEntites()) {
				if (!c->ContainsComponent<PointLight>(e) || !c->ContainsComponent<Base>(e) ||
					EntityOps::IsParkedName(name)) {
					continue;
				}
				const PointLight& light = c->GetComponent<PointLight>(e);
				const bool selected = Selection::Contains(state, e);
				const bool primary = (e == state.selected_entity);

				if (state.light_view == LightView::All ||
					(state.light_view == LightView::Selection && selected)) {
					DrawShape(ctx, light, primary);
					++last_frame.lights_drawn;
					if (light.IsSpot()) {
						++last_frame.spots_drawn;
					}
				}
				if (state.light_positions) {
					ImVec2 screen;
					if (DrawMarker(ctx, light, primary, screen)) {
						Marker m;
						m.name = name;
						m.spot = light.IsSpot();
						m.x = screen.x / ctx.display.x;
						m.y = screen.y / ctx.display.y;
						last_frame.markers.push_back(m);
					}
				}
			}
			last_frame.segments = ctx.segments;
		}
	}
}
