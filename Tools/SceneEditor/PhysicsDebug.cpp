#include "PhysicsDebug.h"
#include "Selection.h"
#include "ViewportOverlay.h"

#include "imgui.h"
#include <Components/Base.h>
#include <Components/Camera.h>
#include <Components/Physics.h>
#include <Systems/CameraSystem.h>
#include <cmath>
#include <mutex>
#include <vector>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::Systems;
using namespace DirectX;

namespace HotBiteEditor {
	namespace PhysicsDebug {

		//Green like reactphysics3d's own COLLISION_SHAPE debug colour; the selection's
		//primary is brighter so it stands out when everything is drawn.
		static constexpr ImU32 COLOR_COLLIDER = IM_COL32(60, 230, 90, 150);
		static constexpr ImU32 COLOR_COLLIDER_PRIMARY = IM_COL32(120, 255, 140, 230);
		//Segments per circle used to approximate spheres and capsule caps.
		static constexpr int CIRCLE_SEGMENTS = 24;

		//Accumulates the frame's segments so the budget is enforced across every
		//entity rather than per entity (one terrain mesh must not starve the rest).
		struct DrawContext {
			ImDrawList* dl = nullptr;
			matrix view_proj{};
			ImVec2 display{};
			int segments = 0;
			bool truncated = false;
		};

		static void Segment(DrawContext& ctx, const vector3d& a, const vector3d& b, ImU32 color)
		{
			if (ctx.segments >= SEGMENT_BUDGET) {
				ctx.truncated = true;
				return;
			}
			++ctx.segments;
			ViewportOverlay::DrawSegment(ctx.dl, ctx.view_proj, ctx.display, a, b, color, 1.0f);
		}

		//Shape-local point -> world, through the collider's offset within the body
		//and then the body's own transform. Everything below builds its geometry in
		//shape-local space and comes through here, so what is drawn is exactly what
		//reactphysics3d collides with - offsets and all.
		static vector3d ToWorld(const reactphysics3d::Transform& body,
			const reactphysics3d::Transform& local, const float3& p)
		{
			reactphysics3d::Vector3 v = body * (local * reactphysics3d::Vector3(p.x, p.y, p.z));
			return XMVectorSet(v.x, v.y, v.z, 1.0f);
		}

		static void DrawBox(DrawContext& ctx, const reactphysics3d::Transform& body,
			const reactphysics3d::Transform& local, const reactphysics3d::Vector3& half, ImU32 color)
		{
			vector3d corners[8];
			for (int i = 0; i < 8; ++i) {
				corners[i] = ToWorld(body, local, float3{
					(i & 1) ? half.x : -half.x,
					(i & 2) ? half.y : -half.y,
					(i & 4) ? half.z : -half.z });
			}
			//Corner i and i|bit differ in exactly one axis: the 12 edges.
			for (int i = 0; i < 8; ++i) {
				for (int bit = 1; bit < 8; bit <<= 1) {
					if ((i & bit) == 0) {
						Segment(ctx, corners[i], corners[i | bit], color);
					}
				}
			}
		}

		//A circle of `radius` in the plane spanned by axes (a0,a1), centred at
		//`center` in shape-local space.
		static void DrawCircle(DrawContext& ctx, const reactphysics3d::Transform& body,
			const reactphysics3d::Transform& local, const float3& center, float radius,
			int a0, int a1, ImU32 color)
		{
			float3 prev{};
			for (int k = 0; k <= CIRCLE_SEGMENTS; ++k) {
				float t = 2.0f * XM_PI * (float)k / (float)CIRCLE_SEGMENTS;
				float3 p = center;
				(&p.x)[a0] += std::cosf(t) * radius;
				(&p.x)[a1] += std::sinf(t) * radius;
				if (k > 0) {
					Segment(ctx, ToWorld(body, local, prev), ToWorld(body, local, p), color);
				}
				prev = p;
			}
		}

		static void DrawSphere(DrawContext& ctx, const reactphysics3d::Transform& body,
			const reactphysics3d::Transform& local, float radius, ImU32 color)
		{
			//Three great circles read as a sphere from any angle.
			DrawCircle(ctx, body, local, {}, radius, 0, 1, color);
			DrawCircle(ctx, body, local, {}, radius, 0, 2, color);
			DrawCircle(ctx, body, local, {}, radius, 1, 2, color);
		}

		static void DrawCapsule(DrawContext& ctx, const reactphysics3d::Transform& body,
			const reactphysics3d::Transform& local, float radius, float height, ImU32 color)
		{
			//reactphysics3d capsules run along the shape's local Y, `height` being the
			//distance between the two cap centres.
			float half = height * 0.5f;
			float3 top{ 0.0f, half, 0.0f };
			float3 bottom{ 0.0f, -half, 0.0f };
			DrawCircle(ctx, body, local, top, radius, 0, 2, color);
			DrawCircle(ctx, body, local, bottom, radius, 0, 2, color);
			//The caps, as half circles in the two vertical planes.
			DrawCircle(ctx, body, local, top, radius, 0, 1, color);
			DrawCircle(ctx, body, local, bottom, radius, 0, 1, color);
			DrawCircle(ctx, body, local, top, radius, 2, 1, color);
			DrawCircle(ctx, body, local, bottom, radius, 2, 1, color);
			//Four side lines joining the caps.
			for (int k = 0; k < 4; ++k) {
				float t = 2.0f * XM_PI * (float)k / 4.0f;
				float x = std::cosf(t) * radius;
				float z = std::sinf(t) * radius;
				Segment(ctx, ToWorld(body, local, { x, half, z }),
					ToWorld(body, local, { x, -half, z }), color);
			}
		}

		//The concave mesh case, and the one worth having. `vertices`/`indices` are the
		//triangles the collider actually holds - the entity's own pre-scaled copy when
		//it has one, otherwise the shared unscaled ShapeData - so a collider that does
		//not match the mesh shows up as a wireframe that fails to wrap it.
		static void DrawMesh(DrawContext& ctx, const reactphysics3d::Transform& body,
			const reactphysics3d::Transform& local, const std::vector<float3>& vertices,
			const std::vector<unsigned int>& indices, const reactphysics3d::Vector3& scale,
			ImU32 color)
		{
			for (size_t i = 0; i + 2 < indices.size(); i += 3) {
				if (ctx.segments >= SEGMENT_BUDGET) {
					ctx.truncated = true;
					return;
				}
				unsigned int i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
				if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
					continue;
				}
				//`scale` is the live shape's own scaling factor, which is 1 for baked
				//geometry; applied anyway so the drawing stays correct if a shape ever
				//does carry one.
				auto scaled = [&scale](const float3& v) {
					return float3{ v.x * scale.x, v.y * scale.y, v.z * scale.z };
				};
				vector3d a = ToWorld(body, local, scaled(vertices[i0]));
				vector3d b = ToWorld(body, local, scaled(vertices[i1]));
				vector3d c = ToWorld(body, local, scaled(vertices[i2]));
				Segment(ctx, a, b, color);
				Segment(ctx, b, c, color);
				Segment(ctx, c, a, color);
			}
		}

		static void DrawEntityCollider(DrawContext& ctx, EditorState& state, Entity e, bool primary)
		{
			Coordinator* c = state.world->GetCoordinator();
			if (!c->ContainsComponent<Physics>(e) || !c->ContainsComponent<Base>(e)) {
				return;
			}
			Physics& ph = c->GetComponent<Physics>(e);
			if (ph.body == nullptr || ph.collider == nullptr) {
				return;
			}
			ImU32 color = primary ? COLOR_COLLIDER_PRIMARY : COLOR_COLLIDER;
			const reactphysics3d::Transform body = ph.body->getTransform();
			const reactphysics3d::Transform local = ph.collider->getLocalToBodyTransform();
			const reactphysics3d::CollisionShape* shape = ph.collider->getCollisionShape();
			if (shape == nullptr) {
				return;
			}
			switch (shape->getName()) {
			case reactphysics3d::CollisionShapeName::BOX:
				DrawBox(ctx, body, local,
					((const reactphysics3d::BoxShape*)shape)->getHalfExtents(), color);
				break;
			case reactphysics3d::CollisionShapeName::SPHERE:
				DrawSphere(ctx, body, local,
					((const reactphysics3d::SphereShape*)shape)->getRadius(), color);
				break;
			case reactphysics3d::CollisionShapeName::CAPSULE: {
				const auto* cap = (const reactphysics3d::CapsuleShape*)shape;
				DrawCapsule(ctx, body, local, cap->getRadius(), cap->getHeight(), color);
			} break;
			case reactphysics3d::CollisionShapeName::TRIANGLE_MESH: {
				//Prefer the entity's own pre-scaled triangles; only fall back to the
				//shared ShapeData when it has none, which is precisely when the
				//collider does share that geometry.
				const reactphysics3d::Vector3& shape_scale =
					((const reactphysics3d::ConcaveShape*)shape)->getScale();
				const std::vector<float3>* verts = ph.GetOwnedMeshVertices();
				const std::vector<unsigned int>* idx = ph.GetOwnedMeshIndices();
				if (verts != nullptr && idx != nullptr) {
					DrawMesh(ctx, body, local, *verts, *idx, shape_scale, color);
				}
				else {
					const Core::ShapeData* data =
						state.world->GetEntityShape(c->GetComponent<Base>(e).name);
					if (data != nullptr) {
						DrawMesh(ctx, body, local, data->vertices, data->indices, shape_scale, color);
					}
				}
			} break;
			default:
				break;
			}
		}

		void Draw(EditorState& state)
		{
			if (state.collider_view == ColliderView::Off) {
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

			//Bodies are stepped on the physics thread; reading their transforms and
			//shapes needs the same global lock every other body access takes.
			std::lock_guard<std::recursive_mutex> lock(Core::physics_mutex);
			if (state.collider_view == ColliderView::Selection) {
				for (Entity e : state.selected_entities) {
					DrawEntityCollider(ctx, state, e, e == state.selected_entity);
				}
			}
			else {
				for (const auto& [name, e] : c->GetEntites()) {
					DrawEntityCollider(ctx, state, e, Selection::Contains(state, e));
				}
			}

			//Say what is on screen: which mode, and whether the budget cut it short
			//(a silently partial collider view would be worse than none).
			char label[96];
			if (ctx.truncated) {
				snprintf(label, sizeof(label), "Colliders: %s (truncated at %d segments)",
					state.collider_view == ColliderView::All ? "all" : "selection", SEGMENT_BUDGET);
			}
			else {
				snprintf(label, sizeof(label), "Colliders: %s (%d segments)",
					state.collider_view == ColliderView::All ? "all" : "selection", ctx.segments);
			}
			ctx.dl->AddText(ImVec2(ctx.display.x * 0.5f - 90.0f, 44.0f),
				ctx.truncated ? IM_COL32(255, 180, 60, 220) : IM_COL32(120, 255, 140, 200), label);
		}

	}
}
