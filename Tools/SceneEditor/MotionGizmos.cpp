#include "MotionGizmos.h"
#include "EntityOps.h"
#include "Selection.h"
#include "ViewportOverlay.h"

#include "imgui.h"
#include <Components/Base.h>
#include <Components/Camera.h>
#include <Components/Force.h>
#include <Components/Platform.h>
#include <Systems/CameraSystem.h>
#include <Systems/ForceSystem.h>
#include <algorithm>
#include <cmath>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::Systems;
using namespace DirectX;

namespace HotBiteEditor {
	namespace MotionGizmos {

		static constexpr int CIRCLE_SEGMENTS = 32;
		//Chevrons along a projection beam. Enough to read the direction and the ramp,
		//few enough that a long beam is not a solid stripe.
		static constexpr int BEAM_ARROWS = 6;
		//What a shape with no natural size falls back to, in world units: the spin circle
		//of a platform with no Bounds, and the arrow of a TOUCH field.
		static constexpr float DEFAULT_EXTENT = 1.0f;

		//Cyan for what moves, green for what patrols, amber for what pushes. Distinct
		//hues rather than distinct shapes, because an entity commonly carries two of them
		//and the shapes then overlap.
		static constexpr ImU32 PLATFORM_COLOR = IM_COL32(90, 220, 235, 235);
		static constexpr ImU32 LINEAR_COLOR = IM_COL32(120, 235, 130, 235);
		static constexpr ImU32 FORCE_COLOR = IM_COL32(255, 175, 70, 235);
		//An inert field (Force::NONE) still draws its direction, dimmed: "this component
		//is here and does nothing yet" is worth seeing, and is not the same as no
		//component at all.
		static constexpr ImU32 INERT_COLOR = IM_COL32(150, 150, 150, 140);

		static FrameInfo last_frame;

		struct DrawContext {
			ImDrawList* dl = nullptr;
			matrix view_proj{};
			ImVec2 display{};
			int segments = 0;
		};

		//Dim a colour for the unselected members of an "All" view, so the primary
		//selection still stands out.
		static ImU32 Fade(ImU32 color, bool primary)
		{
			if (primary) {
				return color;
			}
			const ImU32 alpha = (ImU32)(((color >> IM_COL32_A_SHIFT) & 0xFF) / 2);
			return (color & ~((ImU32)0xFF << IM_COL32_A_SHIFT)) | (alpha << IM_COL32_A_SHIFT);
		}

		static void Segment(DrawContext& ctx, const vector3d& a, const vector3d& b,
			ImU32 color, float thickness)
		{
			++ctx.segments;
			ViewportOverlay::DrawSegment(ctx.dl, ctx.view_proj, ctx.display, a, b, color, thickness);
		}

		static vector3d ToVector(const float3& v)
		{
			return XMVectorSet(v.x, v.y, v.z, 0.0f);
		}

		//Two unit vectors spanning the plane perpendicular to `axis`, for drawing a circle
		//about it. The reference is switched away from Y near the poles, or the cross
		//product degenerates and the circle collapses to a line.
		static void Basis(const vector3d& axis, vector3d& u, vector3d& v)
		{
			vector3d ref = (std::fabsf(XMVectorGetY(axis)) < 0.99f)
				? XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f) : XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
			u = XMVector3Normalize(XMVector3Cross(ref, axis));
			v = XMVector3Cross(axis, u);
		}

		static void Circle(DrawContext& ctx, const vector3d& center, const vector3d& u,
			const vector3d& v, float radius, ImU32 color, float thickness)
		{
			vector3d prev = center + u * radius;
			for (int k = 1; k <= CIRCLE_SEGMENTS; ++k) {
				const float t = 2.0f * XM_PI * (float)k / (float)CIRCLE_SEGMENTS;
				vector3d p = center + (u * std::cosf(t) + v * std::sinf(t)) * radius;
				Segment(ctx, prev, p, color, thickness);
				prev = p;
			}
		}

		//A chevron pointing along `dir` with its point at `tip`: two segments swept back
		//into the perpendicular plane. Drawn in world space rather than as a screen-space
		//arrowhead so it foreshortens with the beam it belongs to - a fan pointing at the
		//camera should look like one.
		static void Chevron(DrawContext& ctx, const vector3d& tip, const vector3d& dir,
			float size, ImU32 color, float thickness)
		{
			vector3d u, v;
			Basis(dir, u, v);
			const vector3d back = tip - dir * size;
			Segment(ctx, tip, back + u * (size * 0.5f), color, thickness);
			Segment(ctx, tip, back - u * (size * 0.5f), color, thickness);
			Segment(ctx, tip, back + v * (size * 0.5f), color, thickness);
			Segment(ctx, tip, back - v * (size * 0.5f), color, thickness);
		}

		//A short cross-bar marking one end of a travel path, in the plane perpendicular to
		//it. A bare segment end is impossible to place along a line seen edge-on.
		static void EndTick(DrawContext& ctx, const vector3d& at, const vector3d& axis,
			float size, ImU32 color, float thickness)
		{
			vector3d u, v;
			Basis(axis, u, v);
			Segment(ctx, at - u * size, at + u * size, color, thickness);
			Segment(ctx, at - v * size, at + v * size, color, thickness);
		}

		//Where the entity is in the world - the engine's own answer, not a second copy of
		//the rule. ForceSystem evaluates its fields against exactly this, so a gizmo drawn
		//anywhere else would put a beam where nothing is pushed.
		static float3 WorldPosition(const Transform& t)
		{
			return ForceSystem::WorldPosition(t);
		}

		//Something to scale a shape that has no size of its own by. The entity's own
		//footprint is the only honest answer available: a spin circle sized to a constant
		//is invisible on a terrain-sized platform and swamps a small one.
		static float EntityExtent(Coordinator* c, Entity e)
		{
			if (!c->ContainsComponent<Bounds>(e)) {
				return DEFAULT_EXTENT;
			}
			const Bounds& b = c->GetConstComponent<Bounds>(e);
			const float extent = (std::max)({ b.final_box.Extents.x, b.final_box.Extents.y,
				b.final_box.Extents.z });
			return (extent > 0.01f) ? extent : DEFAULT_EXTENT;
		}

		//Records a marker for `motion_gizmo_info`, when the point projects on screen at
		//all. Deliberately separate from the drawn counters: a shape behind the camera is
		//still a shape that was drawn (its segments were emitted and clipped), so tying the
		//count to the projection would make "is this component being drawn" and "can I see
		//it from here" the same question - and a test asserting the first would fail on the
		//camera.
		static void Mark(DrawContext& ctx, const float3& at, const char* kind,
			const std::string& name, const std::string& detail)
		{
			ImVec2 screen;
			if (!ViewportOverlay::WorldToScreen(ctx.view_proj, ctx.display, ToVector(at), screen)) {
				return;
			}
			Marker m;
			m.name = name;
			m.kind = kind;
			m.detail = detail;
			m.x = screen.x / ctx.display.x;
			m.y = screen.y / ctx.display.y;
			last_frame.markers.push_back(m);
		}

		// ---------------------------------------------------------------- Platform

		static void DrawPlatform(DrawContext& ctx, Coordinator* c, Entity e,
			const std::string& name, const Platform& p, const Transform& t, bool primary)
		{
			const ImU32 color = Fade(PLATFORM_COLOR, primary);
			const float thickness = primary ? 1.8f : 1.0f;
			const float extent = EntityExtent(c, e);
			//The centre the oscillation is measured from. Until the platform has ticked
			//once (which in the editor means until Edit/Simulate Physics has been on) the
			//system has not captured one, and the authored position is exactly what it
			//will capture.
			const float3 centre_f = p.rt.latched ? p.rt.centre : WorldPosition(t);
			const vector3d centre = ToVector(centre_f);
			const vector3d current = ToVector(WorldPosition(t));

			const bool oscillating = (p.amplitude != 0.0f && LENGHT_SQUARE_F3(p.linear_dir) > 0.0f);
			const bool spinning = (p.angular_speed != 0.0f && LENGHT_SQUARE_F3(p.angular_dir) > 0.0f);

			if (oscillating) {
				const vector3d dir = XMVector3Normalize(ToVector(p.linear_dir));
				const vector3d a = centre - dir * p.amplitude;
				const vector3d b = centre + dir * p.amplitude;
				Segment(ctx, a, b, color, thickness);
				//The reach, which is the whole point of `amplitude`: the ends are where the
				//platform turns round, so they are what has to line up with the geometry
				//the platform is meant to serve.
				const float tick = (std::min)(extent, p.amplitude) * 0.5f;
				EndTick(ctx, a, dir, tick, color, thickness);
				EndTick(ctx, b, dir, tick, color, thickness);
				//Which way along the path it starts, so a phase of 0.5 reads as "starts
				//going the other way" rather than as a number with no visible effect.
				const float start_slope = std::cosf(XM_2PI * p.phase);
				if (start_slope != 0.0f) {
					const vector3d go = (start_slope > 0.0f) ? dir : -dir;
					Chevron(ctx, centre + go * (p.amplitude * 0.45f), go,
						(std::min)(extent, p.amplitude) * 0.4f, color, thickness);
				}
			}
			if (spinning) {
				const vector3d axis = XMVector3Normalize(ToVector(p.angular_dir));
				vector3d u, v;
				Basis(axis, u, v);
				Circle(ctx, current, u, v, extent, color, thickness);
				Segment(ctx, current - axis * extent, current + axis * extent, color, thickness);
				//Sense of rotation: a chevron on the circle, tangent to it. Sign follows
				//angular_speed, so a negative speed visibly turns the other way.
				const vector3d tangent = (p.angular_speed > 0.0f) ? v : -v;
				Chevron(ctx, current + u * extent, tangent, extent * 0.3f, color, thickness);
			}
			if (!oscillating && !spinning) {
				//Authored but inert. A cross at the entity says the component is there.
				EndTick(ctx, current, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), extent * 0.5f,
					INERT_COLOR, 1.0f);
			}

			const std::string detail = oscillating ? (spinning ? "linear+spin" : "linear")
				: (spinning ? "spin" : "inert");
			++last_frame.platforms_drawn;
			Mark(ctx, centre_f, "platform", name, detail);
		}

		// ---------------------------------------------------------- LinearPlatform

		static void DrawLinearPlatform(DrawContext& ctx, Coordinator* c, Entity e,
			const std::string& name, const LinearPlatform& p, const Transform& t, bool primary)
		{
			const ImU32 color = Fade(LINEAR_COLOR, primary);
			const float thickness = primary ? 1.8f : 1.0f;
			const float extent = EntityExtent(c, e);
			const float3 start_f = p.rt.latched ? p.rt.start : WorldPosition(t);
			const float length = LENGHT_F3(p.travel);
			if (length <= 0.0f) {
				EndTick(ctx, ToVector(start_f), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),
					extent * 0.5f, INERT_COLOR, 1.0f);
				++last_frame.linears_drawn;
				Mark(ctx, start_f, "linear", name, "inert");
				return;
			}
			const vector3d start = ToVector(start_f);
			const vector3d travel = ToVector(p.travel);
			const vector3d dir = XMVector3Normalize(travel);
			const vector3d end = start + travel;
			Segment(ctx, start, end, color, thickness);
			const float tick = (std::min)(extent, length) * 0.5f;
			EndTick(ctx, start, dir, tick, color, thickness);
			EndTick(ctx, end, dir, tick, color, thickness);
			//Which way it is going now, at the point it has reached. With ping_pong off
			//there is only ever one direction, which is itself worth seeing.
			const vector3d go = p.rt.forward ? dir : -dir;
			const vector3d at = start + travel * (p.rt.latched ? p.rt.t : 0.0f);
			Chevron(ctx, at, go, (std::min)(extent, length) * 0.4f, color, thickness);

			++last_frame.linears_drawn;
			Mark(ctx, start_f, "linear", name, p.ping_pong ? "ping_pong" : "one_way");
		}

		// ------------------------------------------------------------------- Force

		static void DrawForce(DrawContext& ctx, Coordinator* c, Entity e,
			const std::string& name, const Force& f, const Transform& t, bool primary)
		{
			const float extent = EntityExtent(c, e);
			const float3 origin_f = WorldPosition(t);
			const vector3d origin = ToVector(origin_f);
			//The same resolution the simulation does, so the arrow cannot point one way
			//while bodies are pushed another.
			const float3 dir_f = ForceSystem::WorldDirection(f, t);
			const bool inert = (f.type == Force::NONE || LENGHT_SQUARE_F3(dir_f) <= 0.0f ||
				f.force == 0.0f);
			const ImU32 color = inert ? INERT_COLOR : Fade(FORCE_COLOR, primary);
			const float thickness = primary ? 1.8f : 1.0f;

			if (LENGHT_SQUARE_F3(dir_f) <= 0.0f) {
				EndTick(ctx, origin, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), extent * 0.5f,
					INERT_COLOR, 1.0f);
				++last_frame.forces_drawn;
				Mark(ctx, origin_f, "force", name, "no_dir");
				return;
			}
			const vector3d dir = XMVector3Normalize(ToVector(dir_f));

			if (f.type == Force::PROJECTION && f.range > 0.0f) {
				vector3d u, v;
				Basis(dir, u, v);
				const vector3d far_end = origin + dir * f.range;
				//The beam's actual shape: `radius` wide the whole way, ending at `range`.
				//A cone would be the intuitive drawing and would be wrong - the field is a
				//cylinder, and a body just outside it feels nothing.
				if (f.radius > 0.0f) {
					Circle(ctx, origin, u, v, f.radius, color, thickness);
					Circle(ctx, far_end, u, v, f.radius, color, thickness);
					for (int k = 0; k < 4; ++k) {
						const float a = XM_PIDIV2 * (float)k;
						const vector3d offset = (u * std::cosf(a) + v * std::sinf(a)) * f.radius;
						Segment(ctx, origin + offset, far_end + offset, color, thickness);
					}
				}
				Segment(ctx, origin, far_end, color, thickness);
				//Chevrons whose size tracks the magnitude at that distance, which is what
				//makes the origin_force ramp visible: the default profile grows along the
				//beam, a uniform field draws even chevrons, and a thruster shrinks.
				const float peak = (std::max)(std::fabsf(f.force), std::fabsf(f.origin_force));
				const float base = (std::min)(extent, f.range / (float)BEAM_ARROWS);
				for (int k = 1; k <= BEAM_ARROWS; ++k) {
					const float s = (float)k / (float)BEAM_ARROWS;
					const float magnitude = f.origin_force + (f.force - f.origin_force) * s;
					const float scale = (peak > 0.0f)
						? std::fabsf(magnitude) / peak : 0.0f;
					if (scale <= 0.02f) {
						continue;
					}
					//A negative magnitude pushes back down the beam, so the chevron has to
					//turn round with it rather than only shrink.
					const vector3d go = (magnitude >= 0.0f) ? dir : -dir;
					Chevron(ctx, origin + dir * (f.range * s), go,
						base * (0.35f + 0.65f * scale), color, thickness);
				}
				++last_frame.forces_drawn;
				Mark(ctx, origin_f, "force", name, "PROJECTION");
				return;
			}

			//TOUCH, and PROJECTION with no reach authored yet: there is no volume to draw,
			//so show the direction at the entity. The length is the entity's own footprint
			//- the field acts wherever the collider does, which the collider overlay is the
			//thing to show.
			const float length = extent * 1.5f;
			const vector3d tip = origin + dir * length;
			Segment(ctx, origin, tip, color, thickness);
			Chevron(ctx, tip, dir, length * 0.3f, color, thickness);
			EndTick(ctx, origin, dir, extent * 0.4f, color, thickness);
			++last_frame.forces_drawn;
			Mark(ctx, origin_f, "force", name, Force::TypeName(f.type));
		}

		// -------------------------------------------------------------------------

		const FrameInfo& LastFrame()
		{
			return last_frame;
		}

		void Draw(EditorState& state)
		{
			last_frame = FrameInfo();
			if (state.platform_view == MotionView::Off && state.force_view == MotionView::Off) {
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
				if (!c->ContainsComponent<Transform>(e) || EntityOps::IsParkedName(name)) {
					continue;
				}
				const bool selected = Selection::Contains(state, e);
				const bool primary = (e == state.selected_entity);
				const auto visible = [&](MotionView view) {
					return view == MotionView::All ||
						(view == MotionView::Selection && selected);
					};
				const Transform& t = c->GetConstComponent<Transform>(e);

				if (visible(state.platform_view)) {
					if (c->ContainsComponent<Platform>(e)) {
						DrawPlatform(ctx, c, e, name, c->GetConstComponent<Platform>(e), t, primary);
					}
					if (c->ContainsComponent<LinearPlatform>(e)) {
						DrawLinearPlatform(ctx, c, e, name,
							c->GetConstComponent<LinearPlatform>(e), t, primary);
					}
				}
				if (visible(state.force_view) && c->ContainsComponent<Force>(e)) {
					DrawForce(ctx, c, e, name, c->GetConstComponent<Force>(e), t, primary);
				}
			}
			last_frame.segments = ctx.segments;
		}
	}
}
