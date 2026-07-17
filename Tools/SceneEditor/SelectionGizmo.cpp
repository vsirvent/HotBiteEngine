#include "SelectionGizmo.h"
#include "Inspector.h"

#include "imgui.h"
#include <Components/Base.h>
#include <Components/Camera.h>
#include <Components/Physics.h>
#include <Systems/CameraSystem.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <mutex>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::Systems;
using namespace DirectX;

namespace HotBiteEditor {
	namespace SelectionGizmo {

		static constexpr ImU32 COLOR_AABB = IM_COL32(255, 200, 40, 220);
		static constexpr ImU32 COLOR_AXIS[3] = {
			IM_COL32(235, 70, 70, 255),   // X
			IM_COL32(90, 220, 90, 255),   // Y
			IM_COL32(80, 140, 255, 255),  // Z
		};
		static constexpr ImU32 COLOR_AXIS_HOT = IM_COL32(255, 230, 60, 255);
		static const char* AXIS_LABEL[3] = { "X", "Y", "Z" };
		//Screen-space pick radius around an axis line, in pixels.
		static constexpr float PICK_DISTANCE = 10.0f;

		//A translate drag in progress. The axis line is captured in world space at
		//mouse-down and stays fixed for the whole drag, so the entity tracks the
		//mouse along it no matter how the projection foreshortens the axis.
		struct DragState {
			bool active = false;
			int axis = -1;
			vector3d axis_dir;      //world-space axis direction, normalized
			vector3d start_pivot;   //world-space pivot at mouse-down
			float3 start_local;     //Transform.position at mouse-down
			float start_param;      //axis-line parameter under the mouse at mouse-down
		};
		static DragState drag;

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

		static float DistancePointToSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b)
		{
			float abx = b.x - a.x;
			float aby = b.y - a.y;
			float len_sq = abx * abx + aby * aby;
			float t = 0.0f;
			if (len_sq > 1e-6f) {
				t = ((p.x - a.x) * abx + (p.y - a.y) * aby) / len_sq;
				t = (std::min)(1.0f, (std::max)(0.0f, t));
			}
			float dx = p.x - (a.x + abx * t);
			float dy = p.y - (a.y + aby * t);
			return std::sqrtf(dx * dx + dy * dy);
		}

		//World-space picking ray from the camera through a pixel.
		static void MouseRay(const Camera* cam, const ImVec2& display, const ImVec2& mouse,
			vector3d& origin, vector3d& dir)
		{
			float ndc_x = mouse.x / display.x * 2.0f - 1.0f;
			float ndc_y = 1.0f - mouse.y / display.y * 2.0f;
			matrix inv_vp = XMMatrixInverse(nullptr, cam->xm_view_projection);
			vector3d far_point = XMVector3TransformCoord(XMVectorSet(ndc_x, ndc_y, 1.0f, 1.0f), inv_vp);
			origin = XMLoadFloat3(&cam->world_position);
			dir = XMVector3Normalize(far_point - origin);
		}

		//Parameter t of the point on line P0 + axis*t closest to the ray C + r*s
		//(both directions normalized). False when the axis is near-parallel to the
		//view ray, where the closest-point problem degenerates.
		static bool ClosestAxisParam(const vector3d& p0, const vector3d& axis,
			const vector3d& c, const vector3d& r, float& t_out)
		{
			float b = XMVectorGetX(XMVector3Dot(axis, r));
			float denom = 1.0f - b * b;
			if (denom < 1e-4f) {
				return false;
			}
			vector3d w0 = p0 - c;
			float d = XMVectorGetX(XMVector3Dot(axis, w0));
			float e = XMVectorGetX(XMVector3Dot(r, w0));
			t_out = (b * e - d) / denom;
			return true;
		}

		//The entity's rendered pivot and orientation in world space, composed the
		//same way StaticMeshSystem builds world_xmmatrix (scale*rot*trans, then the
		//parent's rotation/translation when the Base flags enable them). Using the
		//raw Transform fields here is what previously left the gizmo offset from
		//the mesh on parented entities.
		static void GetWorldPivot(Coordinator* c, const Base& base, const Transform& t,
			vector3d& pivot, vector4d& orientation)
		{
			pivot = XMLoadFloat3(&t.position);
			orientation = XMLoadFloat4(&t.rotation);
			if (base.parent != INVALID_ENTITY_ID && c->ContainsComponent<Transform>(base.parent)) {
				const Transform& pt = c->GetComponent<Transform>(base.parent);
				if (base.parent_rotation) {
					vector4d parent_rot = XMLoadFloat4(&pt.rotation);
					pivot = XMVector3Rotate(pivot, parent_rot);
					orientation = XMQuaternionMultiply(orientation, parent_rot);
				}
				if (base.parent_position) {
					pivot += XMLoadFloat3(&pt.position);
				}
			}
		}

		//Inverse of GetWorldPivot's translation path: what Transform.position must be
		//for the rendered pivot to land at world_pivot.
		static float3 WorldPivotToLocal(Coordinator* c, const Base& base, const vector3d& world_pivot)
		{
			vector3d local = world_pivot;
			if (base.parent != INVALID_ENTITY_ID && c->ContainsComponent<Transform>(base.parent)) {
				const Transform& pt = c->GetComponent<Transform>(base.parent);
				if (base.parent_position) {
					local -= XMLoadFloat3(&pt.position);
				}
				if (base.parent_rotation) {
					vector4d parent_rot = XMLoadFloat4(&pt.rotation);
					local = XMVector3Rotate(local, XMQuaternionInverse(parent_rot));
				}
			}
			float3 out;
			XMStoreFloat3(&out, local);
			return out;
		}

		static void DrawArrowHead(ImDrawList* dl, const ImVec2& from, const ImVec2& tip, ImU32 color)
		{
			float dx = tip.x - from.x;
			float dy = tip.y - from.y;
			float len = std::sqrtf(dx * dx + dy * dy);
			if (len < 1e-3f) {
				return;
			}
			dx /= len;
			dy /= len;
			ImVec2 p0(tip.x + dx * 12.0f, tip.y + dy * 12.0f);
			ImVec2 p1(tip.x - dy * 5.0f, tip.y + dx * 5.0f);
			ImVec2 p2(tip.x + dy * 5.0f, tip.y - dx * 5.0f);
			dl->AddTriangleFilled(p0, p1, p2, color);
		}

		//The gizmo geometry of one entity, in world space.
		struct Geometry {
			bool valid = false;
			vector3d origin{};
			vector3d axis_dir[3]{};
			float axis_len = 1.0f;
			float max_extent = 0.0f;
		};

		static Geometry ComputeGeometry(Coordinator* c, Entity e)
		{
			Geometry g;
			if (e == INVALID_ENTITY_ID ||
				!c->ContainsComponent<Transform>(e) || !c->ContainsComponent<Base>(e)) {
				return g;
			}
			if (c->ContainsComponent<Bounds>(e)) {
				const box& b = c->GetComponent<Bounds>(e).final_box;
				g.max_extent = (std::max)({ b.Extents.x, b.Extents.y, b.Extents.z });
			}
			//Sized against the AABB so the axes stay visible at any object scale.
			g.axis_len = (std::max)(g.max_extent * 1.4f, 1.0f);
			vector4d orientation;
			GetWorldPivot(c, c->GetComponent<Base>(e), c->GetComponent<Transform>(e), g.origin, orientation);
			g.axis_dir[0] = XMVector3Rotate(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), orientation);
			g.axis_dir[1] = XMVector3Rotate(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), orientation);
			g.axis_dir[2] = XMVector3Rotate(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), orientation);
			g.valid = true;
			return g;
		}

		//Entity under a click ray; the nearest hit wins. Two tests contribute:
		//the entity's physics collider when it has a live body (precise even for
		//terrain-sized meshes — same raycast RTSCameraSystem uses for ground
		//height), and the world-space AABB (props may have crude colliders, e.g.
		//a capsule approximating a box, so the AABB keeps their full silhouette
		//clickable; it also covers physics-less entities). AABB hits at distance
		//zero mean the camera is inside that box (terrain/water/sky volumes) and
		//are skipped so they can't shadow real hits — clicking open ground still
		//selects the terrain through its collider, and a sky click hits nothing,
		//which clears the selection.
		static Entity Pick(Coordinator* c, const vector3d& ray_origin, const vector3d& ray_dir)
		{
			float3 o, d;
			XMStoreFloat3(&o, ray_origin);
			XMStoreFloat3(&d, ray_dir);
			constexpr float RAY_LENGTH = 10000.0f;
			reactphysics3d::Ray phys_ray(
				{ o.x, o.y, o.z },
				{ o.x + d.x * RAY_LENGTH, o.y + d.y * RAY_LENGTH, o.z + d.z * RAY_LENGTH });

			Entity best = INVALID_ENTITY_ID;
			float best_dist = FLT_MAX;
			//Physics ticks on a background thread; collider raycasts need the same
			//global lock every other body access in the engine takes.
			std::lock_guard<std::recursive_mutex> lock(Core::physics_mutex);
			for (const auto& [name, e] : c->GetEntites()) {
				if (!c->ContainsComponent<Base>(e) || !c->GetComponent<Base>(e).visible) {
					continue;
				}
				if (c->ContainsComponent<Physics>(e)) {
					reactphysics3d::CollisionBody* body = c->GetComponent<Physics>(e).body;
					reactphysics3d::RaycastInfo info{};
					if (body != nullptr && body->raycast(phys_ray, info)) {
						float dist = info.hitFraction * RAY_LENGTH;
						if (dist < best_dist) {
							best_dist = dist;
							best = e;
						}
					}
				}
				if (c->ContainsComponent<Bounds>(e)) {
					const box& b = c->GetComponent<Bounds>(e).final_box;
					float dist = 0.0f;
					if (b.Intersects(ray_origin, ray_dir, dist) && dist > 0.0f && dist < best_dist) {
						best_dist = dist;
						best = e;
					}
				}
			}
			return best;
		}

		void Draw(EditorState& state)
		{
			Coordinator* c = state.world->GetCoordinator();
			if (c == nullptr) {
				drag.active = false;
				return;
			}
			auto camera_system = c->GetSystem<CameraSystem>();
			if (camera_system == nullptr || camera_system->GetCameras().GetData().empty()) {
				return;
			}
			const Camera* cam = camera_system->GetCameras().GetData()[0].camera;
			const matrix& view_proj = cam->xm_view_projection;
			ImGuiIO& io = ImGui::GetIO();
			ImVec2 display = io.DisplaySize;
			ImDrawList* dl = ImGui::GetBackgroundDrawList();

			Geometry geom = ComputeGeometry(c, state.selected_entity);
			if (!geom.valid) {
				drag.active = false;
			}

			//Hover test of the selection's axes in screen space (skipped while a
			//panel owns the mouse or a drag is running).
			int hot_axis = drag.active ? drag.axis : -1;
			bool mouse_free = !drag.active && !io.WantCaptureMouse;
			if (mouse_free && geom.valid) {
				float best = PICK_DISTANCE;
				for (int i = 0; i < 3; ++i) {
					ImVec2 pa, pb;
					if (WorldToScreen(view_proj, display, geom.origin, pa) &&
						WorldToScreen(view_proj, display, geom.origin + geom.axis_dir[i] * geom.axis_len, pb)) {
						float dist = DistancePointToSegment(io.MousePos, pa, pb);
						if (dist < best) {
							best = dist;
							hot_axis = i;
						}
					}
				}
			}

			if (mouse_free && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				vector3d ray_origin, ray_dir;
				MouseRay(cam, display, io.MousePos, ray_origin, ray_dir);
				if (hot_axis >= 0) {
					//Grabbed an axis arrow: begin a translate drag.
					float param;
					if (ClosestAxisParam(geom.origin, geom.axis_dir[hot_axis], ray_origin, ray_dir, param)) {
						drag.active = true;
						drag.axis = hot_axis;
						drag.axis_dir = geom.axis_dir[hot_axis];
						drag.start_pivot = geom.origin;
						drag.start_local = c->GetComponent<Transform>(state.selected_entity).position;
						drag.start_param = param;
					}
				}
				else {
					//Clicked the scene: select what's under the cursor (or clear
					//the selection on empty space), like clicking in the Outliner.
					Entity picked = Pick(c, ray_origin, ray_dir);
					if (picked != state.selected_entity) {
						state.selected_entity = picked;
						Inspector::RefreshEulerCache(state);
						geom = ComputeGeometry(c, state.selected_entity);
						hot_axis = -1;
					}
				}
			}

			if (drag.active) {
				if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
					drag.active = false;
				}
				else {
					vector3d ray_origin, ray_dir;
					MouseRay(cam, display, io.MousePos, ray_origin, ray_dir);
					float param;
					if (ClosestAxisParam(drag.start_pivot, drag.axis_dir, ray_origin, ray_dir, param)) {
						vector3d new_pivot = drag.start_pivot + drag.axis_dir * (param - drag.start_param);
						float3 new_local = WorldPivotToLocal(c,
							c->GetComponent<Base>(state.selected_entity), new_pivot);
						std::string error;
						Inspector::ApplyTransform(state, &new_local, nullptr, nullptr, error);
					}
					//Redraw from the just-applied position so the gizmo tracks the
					//mouse this frame instead of lagging one frame behind.
					geom = ComputeGeometry(c, state.selected_entity);
				}
			}

			if (!geom.valid) {
				return;
			}

			//AABB: the 12 edges of the world-space bounding box.
			if (c->ContainsComponent<Bounds>(state.selected_entity)) {
				const box& b = c->GetComponent<Bounds>(state.selected_entity).final_box;
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

			for (int i = 0; i < 3; ++i) {
				bool hot = (i == hot_axis);
				ImU32 color = hot ? COLOR_AXIS_HOT : COLOR_AXIS[i];
				vector3d end = geom.origin + geom.axis_dir[i] * geom.axis_len;
				DrawSegment(dl, view_proj, display, geom.origin, end, color, hot ? 4.0f : 2.5f);
				ImVec2 pa, pb;
				if (WorldToScreen(view_proj, display, geom.origin, pa) &&
					WorldToScreen(view_proj, display, end, pb)) {
					DrawArrowHead(dl, pa, pb, color);
					dl->AddText(ImVec2(pb.x + 6.0f, pb.y - 6.0f), color, AXIS_LABEL[i]);
				}
			}

			if (drag.active && c->ContainsComponent<Transform>(state.selected_entity)) {
				const float3& pos = c->GetComponent<Transform>(state.selected_entity).position;
				char buf[96];
				snprintf(buf, sizeof(buf), "%.2f  %.2f  %.2f", pos.x, pos.y, pos.z);
				ImVec2 text_pos(io.MousePos.x + 14.0f, io.MousePos.y + 14.0f);
				ImVec2 text_size = ImGui::CalcTextSize(buf);
				dl->AddRectFilled(ImVec2(text_pos.x - 4.0f, text_pos.y - 2.0f),
					ImVec2(text_pos.x + text_size.x + 4.0f, text_pos.y + text_size.y + 2.0f),
					IM_COL32(20, 20, 20, 200), 3.0f);
				dl->AddText(text_pos, IM_COL32(255, 255, 255, 255), buf);
			}
		}

	}
}
