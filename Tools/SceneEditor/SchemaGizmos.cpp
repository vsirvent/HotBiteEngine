#include "SchemaGizmos.h"
#include "ComponentOps.h"
#include "EntityOps.h"
#include "ViewportOverlay.h"

#include "imgui.h"
#include <Components/Base.h>
#include <Components/Camera.h>
#include <Systems/CameraSystem.h>
#include <cmath>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::Systems;
using namespace DirectX;

namespace HotBiteEditor {
	namespace SchemaGizmos {

		static constexpr int CIRCLE_SEGMENTS = 40;
		//A game hint, not a light: one fixed colour, distinct from the light gizmos'
		//per-light colours and the collider overlay's green.
		static const ImU32 COLOR = IM_COL32(255, 170, 40, 230);

		static FrameInfo last_frame;

		struct DrawContext {
			ImDrawList* dl = nullptr;
			matrix view_proj{};
			ImVec2 display{};
		};

		static void Segment(DrawContext& ctx, const vector3d& a, const vector3d& b)
		{
			++last_frame.segments;
			ViewportOverlay::DrawSegment(ctx.dl, ctx.view_proj, ctx.display, a, b, COLOR, 1.5f);
		}

		static void Circle(DrawContext& ctx, const vector3d& center, const vector3d& u,
			const vector3d& v, float radius)
		{
			vector3d prev = center + u * radius;
			for (int k = 1; k <= CIRCLE_SEGMENTS; ++k) {
				float t = 2.0f * XM_PI * (float)k / (float)CIRCLE_SEGMENTS;
				vector3d p = center + (u * std::cosf(t) + v * std::sinf(t)) * radius;
				Segment(ctx, prev, p);
				prev = p;
			}
		}

		static void Sphere(DrawContext& ctx, const vector3d& center, float radius)
		{
			vector3d x = XMVectorSet(1, 0, 0, 0), y = XMVectorSet(0, 1, 0, 0), z = XMVectorSet(0, 0, 1, 0);
			Circle(ctx, center, x, y, radius);
			Circle(ctx, center, x, z, radius);
			Circle(ctx, center, y, z, radius);
			++last_frame.spheres;
		}

		//World-axis aligned: a hint about extent, not a collider.
		static void Box(DrawContext& ctx, const vector3d& center, const float3& half)
		{
			vector3d corner[8];
			for (int i = 0; i < 8; ++i) {
				corner[i] = center + XMVectorSet((i & 1) ? half.x : -half.x,
					(i & 2) ? half.y : -half.y, (i & 4) ? half.z : -half.z, 0.0f);
			}
			static const int EDGES[12][2] = {
				{0,1},{2,3},{4,5},{6,7}, {0,2},{1,3},{4,6},{5,7}, {0,4},{1,5},{2,6},{3,7} };
			for (const auto& e : EDGES) {
				Segment(ctx, corner[e[0]], corner[e[1]]);
			}
			++last_frame.boxes;
		}

		//Where the entity is in the world. The world matrix when the transform
		//system has built one (a part of a composed instance is placed relative to
		//its parent, so its own position alone would be in the wrong space).
		static vector3d WorldPosition(const Transform& t)
		{
			vector3d translation = t.world_xmmatrix.r[3];
			if (XMVectorGetW(translation) != 0.0f) {
				return XMVectorSetW(translation, 1.0f);
			}
			return XMVectorSet(t.position.x, t.position.y, t.position.z, 1.0f);
		}

		const FrameInfo& LastFrame()
		{
			return last_frame;
		}

		void Draw(EditorState& state)
		{
			last_frame = FrameInfo();
			if (state.component_schemas.schemas.empty() || state.selected_entities.empty()) {
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
			DrawContext ctx;
			ctx.dl = ImGui::GetBackgroundDrawList();
			ctx.view_proj = camera_system->GetCameras().GetData()[0].camera->xm_view_projection;
			ctx.display = ImGui::GetIO().DisplaySize;

			for (Entity e : state.selected_entities) {
				if (!c->ContainsComponent<Base>(e) || !c->ContainsComponent<Transform>(e)) {
					continue;
				}
				const std::string name = c->GetConstComponent<Base>(e).name;
				auto blocks = state.opaque_components.find(name);
				if (blocks == state.opaque_components.end() || EntityOps::IsParkedName(name)) {
					continue;
				}
				const vector3d center = WorldPosition(c->GetConstComponent<Transform>(e));
				for (const auto& [component, stored] : blocks->second) {
					const ComponentSchema* schema = ComponentSchemas::Find(state, component);
					if (schema == nullptr) {
						continue;
					}
					//Defaults filled in, so a field the file never wrote still draws.
					const nlohmann::json value = ComponentOps::GetValue(state, name, component);
					for (const SchemaField& f : schema->fields) {
						if (f.gizmo.empty() || !value.contains(f.name)) {
							continue;
						}
						const nlohmann::json& v = value[f.name];
						if (f.gizmo == "sphere" && v.is_number() && v.get<float>() > 0.0f) {
							Sphere(ctx, center, v.get<float>());
						}
						else if (f.gizmo == "box" && v.is_object()) {
							Box(ctx, center, { v.value("x", 0.0f), v.value("y", 0.0f), v.value("z", 0.0f) });
						}
					}
				}
			}
		}
	}
}
