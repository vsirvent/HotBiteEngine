#include "SelectionGizmo.h"
#include "Inspector.h"
#include "EditorHistory.h"
#include "Selection.h"
#include "ViewportOverlay.h"
#include "GridSnap.h"

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
		//Non-primary members of a multi-entity selection: same hue, dimmer.
		static constexpr ImU32 COLOR_AABB_SECONDARY = IM_COL32(255, 200, 40, 120);
		static constexpr ImU32 COLOR_AXIS[3] = {
			IM_COL32(235, 70, 70, 255),   // X
			IM_COL32(90, 220, 90, 255),   // Y
			IM_COL32(80, 140, 255, 255),  // Z
		};
		static constexpr ImU32 COLOR_AXIS_HOT = IM_COL32(255, 230, 60, 255);
		static const char* AXIS_LABEL[3] = { "X", "Y", "Z" };
		//Screen-space pick radius around an axis line, in pixels.
		static constexpr float PICK_DISTANCE = 10.0f;
		//Segments per rotation circle (drawing and picking share them).
		static constexpr int CIRCLE_SEGMENTS = 48;
		//Handle index of the scale gizmo's uniform-scale center square.
		static constexpr int UNIFORM_HANDLE = 3;
		//Screen-space pick radius of the uniform-scale center square, in pixels.
		static constexpr float CENTER_PICK_DISTANCE = 12.0f;

		//A gizmo drag in progress. The reference geometry (axis line, rotation
		//plane, start transform) is captured in world space at mouse-down and stays
		//fixed for the whole drag, so the entity tracks the mouse no matter how the
		//projection foreshortens the gizmo or how the edit moves it.
		//One entity taking part in a drag, with the transform it had when the drag
		//started. Every frame of the drag re-derives that entity's transform from
		//this pre-drag state and the gesture so far, never from its current one:
		//accumulating frame-to-frame deltas would drift, and a rotation applied to
		//an already-rotated entity would compound.
		struct DragTarget {
			std::string name;
			Inspector::TransformSnapshot before;
			vector3d start_pivot;   //world-space rendered pivot at mouse-down
		};

		struct DragState {
			bool active = false;
			GizmoMode mode = GizmoMode::Translate;
			int axis = -1;          //0..2, or UNIFORM_HANDLE in scale mode
			vector3d axis_dir;      //world-space axis direction, normalized
			vector3d start_pivot;   //world-space gizmo pivot at mouse-down
			float start_param;      //axis-line parameter under the mouse at mouse-down
			vector3d rotate_start;  //unit pivot->hit vector in the circle plane at mouse-down
			ImVec2 start_mouse;     //mouse position at mouse-down (uniform scale)
			float angle_deg = 0.0f; //last applied rotation delta, for the readout
			//Undo bookkeeping: the whole drag records as ONE history action covering
			//every entity it moved, so the per-frame ApplyTransform calls run with
			//record_history=false and the pre-drag snapshots captured here are
			//recorded once at mouse release.
			std::vector<DragTarget> targets;
		};
		static DragState drag;

		//Projection shared with the collider overlay (see ViewportOverlay.h).
		using ViewportOverlay::WorldToScreen;
		using ViewportOverlay::DrawSegment;

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

		bool ComputeMouseRay(Coordinator* c, const ImVec2& display, const ImVec2& mouse,
			vector3d& origin, vector3d& dir)
		{
			if (c == nullptr) {
				return false;
			}
			auto camera_system = c->GetSystem<CameraSystem>();
			if (camera_system == nullptr || camera_system->GetCameras().GetData().empty()) {
				return false;
			}
			MouseRay(camera_system->GetCameras().GetData()[0].camera, display, mouse, origin, dir);
			return true;
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

		//Unit vector from `center` to where the ray C + r*t crosses the plane through
		//`center` with normal `n`. False when the ray is near-parallel to the plane,
		//the plane is behind the ray, or the hit lands on the center itself - all
		//degenerate for measuring a rotation angle.
		static bool RayPlaneDir(const vector3d& center, const vector3d& n,
			const vector3d& c, const vector3d& r, vector3d& dir_out)
		{
			float denom = XMVectorGetX(XMVector3Dot(r, n));
			if (std::fabsf(denom) < 1e-4f) {
				return false;
			}
			float t = XMVectorGetX(XMVector3Dot(center - c, n)) / denom;
			if (t <= 0.0f) {
				return false;
			}
			vector3d v = (c + r * t) - center;
			if (XMVectorGetX(XMVector3LengthSq(v)) < 1e-8f) {
				return false;
			}
			dir_out = XMVector3Normalize(v);
			return true;
		}

		//Signed angle (radians, right-handed around `n`) that takes unit vector `a`
		//to unit vector `b`, both lying in the plane with normal `n`.
		static float SignedAngle(const vector3d& n, const vector3d& a, const vector3d& b)
		{
			float sin_a = XMVectorGetX(XMVector3Dot(n, XMVector3Cross(a, b)));
			float cos_a = XMVectorGetX(XMVector3Dot(a, b));
			return std::atan2f(sin_a, cos_a);
		}

		//The entity's rendered pivot and orientation in world space, composed the
		//same way StaticMeshSystem builds world_xmmatrix (scale*rot*trans, then the
		//parent's rotation/translation when the Base flags enable them). Using the
		//raw Transform fields here is what previously left the gizmo offset from
		//the mesh on parented entities.
		//The matrix that carries an attached entity's own transform into world space: the
		//joint it rides, then the parent's whole world matrix. False when the entity is
		//not riding a bone, which is the ordinary parent path below.
		static bool GetSocketMatrix(Coordinator* c, const Base& base, matrix& out)
		{
			if (base.parent == INVALID_ENTITY_ID || base.parent_bone.empty() ||
				!c->ContainsComponent<Components::Mesh>(base.parent) ||
				!c->ContainsComponent<Transform>(base.parent)) {
				return false;
			}
			Components::Mesh& parent_mesh = c->GetComponent<Components::Mesh>(base.parent);
			matrix joint{};
			const int index = (base.parent_joint >= 0) ? base.parent_joint
				: parent_mesh.FindJoint(base.parent_bone);
			if (index < 0 || !parent_mesh.GetJointPose(index, joint)) {
				return false;
			}
			out = joint * c->GetComponent<Transform>(base.parent).world_xmmatrix;
			return true;
		}

		static void GetWorldPivot(Coordinator* c, const Base& base, const Transform& t,
			vector3d& pivot, vector4d& orientation)
		{
			pivot = XMLoadFloat3(&t.position);
			orientation = XMLoadFloat4(&t.rotation);
			matrix socket{};
			if (GetSocketMatrix(c, base, socket)) {
				//Riding a joint: the offset is in the parent mesh's space, so the whole
				//chain from there to the world is one matrix. Decomposing it is the only
				//way to get an orientation out - the socket carries the parent's scale,
				//which a quaternion cannot.
				pivot = XMVector3Transform(pivot, socket);
				vector3d socket_scale{};
				vector4d socket_rotation{};
				vector3d socket_translation{};
				if (XMMatrixDecompose(&socket_scale, &socket_rotation, &socket_translation, socket)) {
					orientation = XMQuaternionMultiply(orientation, socket_rotation);
				}
				return;
			}
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
			matrix socket{};
			if (GetSocketMatrix(c, base, socket)) {
				float3 out;
				XMStoreFloat3(&out, XMVector3Transform(local, XMMatrixInverse(nullptr, socket)));
				return out;
			}
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

		//The gizmo for the current selection.
		//  - One entity: at its rendered pivot, axes along its own local frame (so
		//    the handles match how the mesh is oriented).
		//  - Several: at the center of the combined world AABB, axes along the world
		//    frame - the selected entities generally disagree about "local", and a
		//    world-aligned gizmo is what makes a shared pivot predictable.
		static Geometry ComputeGeometry(EditorState& state)
		{
			Coordinator* c = state.world->GetCoordinator();
			Geometry g;
			if (c == nullptr || state.selected_entities.empty()) {
				return g;
			}

			if (state.selected_entities.size() == 1) {
				Entity e = state.selected_entity;
				if (!c->ContainsComponent<Transform>(e) || !c->ContainsComponent<Base>(e)) {
					return g;
				}
				if (c->ContainsComponent<Bounds>(e)) {
					const box& b = c->GetComponent<Bounds>(e).final_box;
					g.max_extent = (std::max)({ b.Extents.x, b.Extents.y, b.Extents.z });
				}
				//Sized against the AABB so the axes stay visible at any object scale.
				g.axis_len = (std::max)(g.max_extent * 1.4f, 1.0f);
				vector4d orientation;
				GetWorldPivot(c, c->GetComponent<Base>(e), c->GetComponent<Transform>(e),
					g.origin, orientation);
				g.axis_dir[0] = XMVector3Rotate(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), orientation);
				g.axis_dir[1] = XMVector3Rotate(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), orientation);
				g.axis_dir[2] = XMVector3Rotate(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), orientation);
				g.valid = true;
				return g;
			}

			//Union of the selection's world AABBs, falling back to an entity's pivot
			//when it has no Bounds, so the gizmo sits at the middle of everything.
			float3 lo{ FLT_MAX, FLT_MAX, FLT_MAX };
			float3 hi{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
			bool any = false;
			for (Entity e : state.selected_entities) {
				if (!c->ContainsComponent<Transform>(e) || !c->ContainsComponent<Base>(e)) {
					continue;
				}
				float3 min_p, max_p;
				if (c->ContainsComponent<Bounds>(e)) {
					const box& b = c->GetComponent<Bounds>(e).final_box;
					min_p = { b.Center.x - b.Extents.x, b.Center.y - b.Extents.y, b.Center.z - b.Extents.z };
					max_p = { b.Center.x + b.Extents.x, b.Center.y + b.Extents.y, b.Center.z + b.Extents.z };
				}
				else {
					vector3d pivot;
					vector4d orientation;
					GetWorldPivot(c, c->GetComponent<Base>(e), c->GetComponent<Transform>(e),
						pivot, orientation);
					XMStoreFloat3(&min_p, pivot);
					max_p = min_p;
				}
				lo.x = (std::min)(lo.x, min_p.x); lo.y = (std::min)(lo.y, min_p.y); lo.z = (std::min)(lo.z, min_p.z);
				hi.x = (std::max)(hi.x, max_p.x); hi.y = (std::max)(hi.y, max_p.y); hi.z = (std::max)(hi.z, max_p.z);
				any = true;
			}
			if (!any) {
				return g;
			}
			g.origin = XMVectorSet((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f, 1.0f);
			g.max_extent = (std::max)({ (hi.x - lo.x), (hi.y - lo.y), (hi.z - lo.z) }) * 0.5f;
			g.axis_len = (std::max)(g.max_extent * 1.4f, 1.0f);
			g.axis_dir[0] = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
			g.axis_dir[1] = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
			g.axis_dir[2] = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
			g.valid = true;
			return g;
		}

		//Captures the pre-drag state of every entity the gizmo will move. Entities
		//without a Base/Transform (nothing to edit) are left out.
		static std::vector<DragTarget> CaptureDragTargets(EditorState& state)
		{
			Coordinator* c = state.world->GetCoordinator();
			std::vector<DragTarget> targets;
			for (Entity e : state.selected_entities) {
				if (!c->ContainsComponent<Base>(e) || !c->ContainsComponent<Transform>(e)) {
					continue;
				}
				const Transform& t = c->GetComponent<Transform>(e);
				DragTarget target;
				target.name = c->GetComponent<Base>(e).name;
				target.before = { t.position, t.rotation, t.scale };
				vector4d orientation;
				GetWorldPivot(c, c->GetComponent<Base>(e), t, target.start_pivot, orientation);
				targets.push_back(target);
			}
			return targets;
		}

		//Writes `target`'s transform for a rendered pivot of `world_pivot` plus the
		//given rotation/scale, running the same bookkeeping a manual Inspector edit
		//does (instance sync, FBX override tracking, physics body and collider).
		static void ApplyToTarget(EditorState& state, const DragTarget& target,
			const vector3d& world_pivot, const float4& rotation, const float3& scale)
		{
			Coordinator* c = state.world->GetCoordinator();
			Entity e = c->GetEntityByName(target.name);
			if (e == INVALID_ENTITY_ID || !c->ContainsComponent<Base>(e)) {
				return;
			}
			Inspector::TransformSnapshot snapshot;
			snapshot.position = WorldPivotToLocal(c, c->GetComponent<Base>(e), world_pivot);
			snapshot.rotation = rotation;
			snapshot.scale = scale;
			std::string error;
			//Deferred: this runs every frame of a drag (ApplyTranslate/Rotate/Scale
			//are only ever called while the mouse is still down), and rebuilding a
			//mesh collider that often is what made scaling a large single mesh peg
			//the main thread for the length of the drag - see FlushPendingColliderRebuilds,
			//called once the drag finishes below.
			Inspector::ApplySnapshot(state, target.name, snapshot, error, /*rebuild_collider=*/false);
		}

		//Moves the whole drag along `axis_dir` by `distance` from where it started.
		static void ApplyTranslate(EditorState& state, const vector3d& axis_dir, float distance)
		{
			for (const auto& target : drag.targets) {
				vector3d world_pivot = target.start_pivot + axis_dir * distance;
				if (state.grid_snap_enabled) {
					world_pivot = GridSnap::SnapVector3(world_pivot, state.grid_size);
				}
				ApplyToTarget(state, target, world_pivot, target.before.rotation, target.before.scale);
			}
		}

		//Spins the drag by `angle` radians about `axis_dir` through the gizmo pivot:
		//each entity turns on itself *and* orbits the shared pivot, which is what
		//makes rotating a multi-entity selection behave like rotating one rigid
		//object. With a single entity the pivot is its own, so the orbit vanishes and
		//this is exactly a local-axis spin.
		static void ApplyRotate(EditorState& state, const vector3d& axis_dir, float angle)
		{
			if (state.grid_snap_enabled) {
				//Snap the total angle swept since mouse-down, not the composed
				//quaternion afterwards - that avoids any Euler/gimbal ambiguity and
				//matches "angle" already being measured from the drag's own start.
				angle = GridSnap::SnapAngleRadians(angle, state.grid_rotation_step_degrees);
			}
			vector4d delta = XMQuaternionRotationNormal(axis_dir, angle);
			for (const auto& target : drag.targets) {
				vector3d offset = target.start_pivot - drag.start_pivot;
				vector3d pivot = drag.start_pivot + XMVector3Rotate(offset, delta);
				//"Turn by `delta` in world space, after whatever rotation the entity
				//already had" - the world-space composition order.
				float4 rotation;
				XMStoreFloat4(&rotation, XMQuaternionMultiply(
					XMLoadFloat4(&target.before.rotation), delta));
				ApplyToTarget(state, target, pivot, rotation, target.before.scale);
			}
		}

		//Scales the drag about the gizmo pivot. `factor` multiplies the entity's own
		//scale (per-axis on `axis`, or every axis for the uniform handle) and, with
		//several entities selected, the spacing between them - they spread out from
		//the shared pivot instead of growing in place.
		//Per-axis handles scale every axis of a *multi-entity* selection uniformly:
		//the handles are world-aligned while each entity's scale is expressed in its
		//own local frame, so a per-axis factor would shear a rotated entity.
		static void ApplyScale(EditorState& state, int axis, float factor)
		{
			bool uniform = (axis == UNIFORM_HANDLE) || (drag.targets.size() > 1);
			for (const auto& target : drag.targets) {
				float3 scale = target.before.scale;
				if (uniform) {
					scale.x *= factor;
					scale.y *= factor;
					scale.z *= factor;
				}
				else {
					(&scale.x)[axis] *= factor;
				}
				if (state.grid_snap_enabled) {
					scale.x = GridSnap::SnapScale(scale.x, state.grid_scale_step);
					scale.y = GridSnap::SnapScale(scale.y, state.grid_scale_step);
					scale.z = GridSnap::SnapScale(scale.z, state.grid_scale_step);
				}
				vector3d offset = target.start_pivot - drag.start_pivot;
				vector3d pivot = drag.start_pivot + offset * factor;
				ApplyToTarget(state, target, pivot, target.before.rotation, scale);
			}
		}

		//k-th sample point of the rotation circle around axis `axis` (the circle lies
		//in the plane spanned by the other two axes; the frame is orthonormal, so
		//they are a valid basis for it).
		static vector3d CirclePoint(const Geometry& g, int axis, int k)
		{
			float a = 2.0f * XM_PI * (float)k / (float)CIRCLE_SEGMENTS;
			return g.origin + (g.axis_dir[(axis + 1) % 3] * std::cosf(a) +
				g.axis_dir[(axis + 2) % 3] * std::sinf(a)) * g.axis_len;
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
		static Entity Pick(Coordinator* c, const vector3d& ray_origin, const vector3d& ray_dir,
			float* out_distance = nullptr)
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
			if (best != INVALID_ENTITY_ID && out_distance != nullptr) {
				*out_distance = best_dist;
			}
			return best;
		}

		Entity RaycastScene(Coordinator* c, const float3& ray_origin, const float3& ray_dir,
			float* out_distance)
		{
			vector3d origin = XMLoadFloat3(&ray_origin);
			vector3d dir = XMLoadFloat3(&ray_dir);
			//A zero direction (e.g. a camera the CameraSystem hasn't updated yet)
			//would normalize to NaN and trip BoundingBox::Intersects' unit-vector
			//assert; there is nothing meaningful to hit anyway.
			if (XMVectorGetX(XMVector3LengthSq(dir)) < 1e-8f) {
				return INVALID_ENTITY_ID;
			}
			//Pick's collider/AABB distances are parameters along the direction, so it
			//must be unit length for them to come out in world units.
			dir = XMVector3Normalize(dir);
			return Pick(c, origin, dir, out_distance);
		}

		void SimulateDrag(EditorState& state, GizmoMode mode, int axis, float amount)
		{
			Geometry geom = ComputeGeometry(state);
			if (!geom.valid) {
				return;
			}
			DragState d;
			d.mode = mode;
			d.axis = axis;
			d.axis_dir = (axis >= 0 && axis < 3) ? geom.axis_dir[axis] : XMVectorZero();
			d.start_pivot = geom.origin;
			d.targets = CaptureDragTargets(state);
			if (d.targets.empty()) {
				return;
			}
			//ApplyTranslate/ApplyRotate/ApplyScale read the module-level `drag` for
			//their target list, exactly as the interactive per-frame update does.
			drag = d;
			if (mode == GizmoMode::Translate) {
				ApplyTranslate(state, d.axis_dir, amount);
			}
			else if (mode == GizmoMode::Rotate) {
				ApplyRotate(state, d.axis_dir, amount);
			}
			else {
				ApplyScale(state, axis, amount);
			}
			//One atomic action from automation's point of view - record it now
			//rather than leaving `drag` looking like an in-progress interactive one.
			std::vector<std::string> names;
			std::vector<Inspector::TransformSnapshot> befores;
			for (const auto& target : drag.targets) {
				names.push_back(target.name);
				befores.push_back(target.before);
			}
			Inspector::RecordTransformEdits(state, names, befores);
			drag = DragState();
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

			//Tool switching on 1/2/3 (W/E/R belong to the camera fly keys). Gated the
			//same way the fly keys are: only an active text field owns the keyboard.
			//Not mid-drag, so the captured drag geometry always matches its mode.
			if (!io.WantTextInput && !drag.active) {
				if (ImGui::IsKeyPressed(ImGuiKey_1, false)) {
					state.gizmo_mode = GizmoMode::Translate;
				}
				if (ImGui::IsKeyPressed(ImGuiKey_2, false)) {
					state.gizmo_mode = GizmoMode::Rotate;
				}
				if (ImGui::IsKeyPressed(ImGuiKey_3, false)) {
					state.gizmo_mode = GizmoMode::Scale;
				}
			}
			GizmoMode mode = state.gizmo_mode;

			//Active-tool reminder, top-center under the menu bar (background draw
			//list, so panels cover it rather than the other way around).
			static const char* MODE_LABEL[3] = { "Move (1)", "Rotate (2)", "Scale (3)" };
			dl->AddText(ImVec2(display.x * 0.5f - 30.0f, 28.0f),
				IM_COL32(255, 255, 255, 170), MODE_LABEL[(int)mode]);

			Geometry geom = ComputeGeometry(state);
			if (!geom.valid) {
				drag.active = false;
			}

			//Hover test of the selection's handles in screen space (skipped while a
			//panel owns the mouse or a drag is running).
			int hot_axis = drag.active ? drag.axis : -1;
			//The mask-paint brush (MaskPaint::UpdateBrush) owns left-click-and-drag
			//over the viewport while it's on, the same kind of mode-exclusivity the
			//translate/rotate/scale tools already have with each other.
			bool mouse_free = !drag.active && !io.WantCaptureMouse && !state.mask_paint_brush_mode;
			if (mouse_free && geom.valid) {
				float best = PICK_DISTANCE;
				if (mode == GizmoMode::Rotate) {
					for (int i = 0; i < 3; ++i) {
						for (int k = 0; k < CIRCLE_SEGMENTS; ++k) {
							ImVec2 pa, pb;
							if (WorldToScreen(view_proj, display, CirclePoint(geom, i, k), pa) &&
								WorldToScreen(view_proj, display, CirclePoint(geom, i, k + 1), pb)) {
								float dist = DistancePointToSegment(io.MousePos, pa, pb);
								if (dist < best) {
									best = dist;
									hot_axis = i;
								}
							}
						}
					}
				}
				else {
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
					if (mode == GizmoMode::Scale) {
						//The center square (uniform scale) wins over the axis lines,
						//which all pass through it.
						ImVec2 pc;
						if (WorldToScreen(view_proj, display, geom.origin, pc)) {
							float dx = io.MousePos.x - pc.x;
							float dy = io.MousePos.y - pc.y;
							if (std::sqrtf(dx * dx + dy * dy) < CENTER_PICK_DISTANCE) {
								hot_axis = UNIFORM_HANDLE;
							}
						}
					}
				}
			}

			if (mouse_free && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				vector3d ray_origin, ray_dir;
				MouseRay(cam, display, io.MousePos, ray_origin, ray_dir);
				if (hot_axis >= 0) {
					//Grabbed a handle: capture the reference geometry for the drag.
					DragState d;
					d.mode = mode;
					d.axis = hot_axis;
					d.axis_dir = (hot_axis < 3) ? geom.axis_dir[hot_axis] : XMVectorZero();
					d.start_pivot = geom.origin;
					d.targets = CaptureDragTargets(state);
					if (d.targets.empty()) {
						//Nothing editable under the handle; fall through as a no-drag.
					}
					else if (mode == GizmoMode::Rotate) {
						//Angle is measured against the pivot->grab-point direction in
						//the circle's plane.
						d.active = RayPlaneDir(geom.origin, d.axis_dir, ray_origin, ray_dir, d.rotate_start);
					}
					else if (mode == GizmoMode::Scale && hot_axis == UNIFORM_HANDLE) {
						//Uniform scale follows the mouse delta (right/up grows), so
						//it works from any view angle.
						d.start_mouse = io.MousePos;
						d.active = true;
					}
					else {
						//Translate and per-axis scale both measure along the axis line.
						d.active = ClosestAxisParam(geom.origin, d.axis_dir, ray_origin, ray_dir, d.start_param);
						if (mode == GizmoMode::Scale && std::fabsf(d.start_param) < 1e-3f) {
							d.active = false; //grabbed at the pivot: no scale ratio to measure
						}
					}
					if (d.active) {
						drag = d;
					}
				}
				else {
					//Clicked the scene: select what's under the cursor (or clear
					//the selection on empty space), like clicking in the Outliner.
					//Ctrl extends the selection, so several objects can be gathered
					//straight from the viewport; without it a click replaces it.
					Entity picked = Pick(c, ray_origin, ray_dir);
					if (io.KeyCtrl) {
						if (picked != INVALID_ENTITY_ID) {
							Selection::Toggle(state, picked);
						}
					}
					else {
						Selection::Set(state, picked);
					}
					geom = ComputeGeometry(state);
					hot_axis = -1;
				}
			}

			if (drag.active) {
				if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
					//Drag finished: record the whole gesture - every entity it moved -
					//as one undo step.
					std::vector<std::string> names;
					std::vector<Inspector::TransformSnapshot> befores;
					for (const auto& target : drag.targets) {
						names.push_back(target.name);
						befores.push_back(target.before);
					}
					Inspector::RecordTransformEdits(state, names, befores);
					//Rebuild the physics collider(s) the drag deferred, once, now that
					//the entity's Transform holds its final value.
					Inspector::FlushPendingColliderRebuilds(state);
					drag.active = false;
				}
				else {
					vector3d ray_origin, ray_dir;
					MouseRay(cam, display, io.MousePos, ray_origin, ray_dir);
					if (drag.mode == GizmoMode::Translate) {
						float param;
						if (ClosestAxisParam(drag.start_pivot, drag.axis_dir, ray_origin, ray_dir, param)) {
							ApplyTranslate(state, drag.axis_dir, param - drag.start_param);
						}
					}
					else if (drag.mode == GizmoMode::Rotate) {
						vector3d now;
						if (RayPlaneDir(drag.start_pivot, drag.axis_dir, ray_origin, ray_dir, now)) {
							float angle = SignedAngle(drag.axis_dir, drag.rotate_start, now);
							drag.angle_deg = XMConvertToDegrees(angle);
							ApplyRotate(state, drag.axis_dir, angle);
						}
					}
					else { //GizmoMode::Scale
						float factor = 0.0f;
						bool have_factor = false;
						if (drag.axis == UNIFORM_HANDLE) {
							//Exponential in the mouse delta: ~150px right/up doubles,
							//left/down halves, symmetric and never zero.
							float delta = (io.MousePos.x - drag.start_mouse.x) -
								(io.MousePos.y - drag.start_mouse.y);
							factor = std::exp2f(delta / 150.0f);
							have_factor = true;
						}
						else {
							float param;
							if (ClosestAxisParam(drag.start_pivot, drag.axis_dir, ray_origin, ray_dir, param)) {
								factor = param / drag.start_param;
								have_factor = true;
							}
						}
						if (have_factor) {
							//No mirroring through the pivot: dragging past it clamps
							//instead of flipping the mesh inside-out.
							ApplyScale(state, drag.axis, (std::max)(factor, 0.01f));
						}
					}
					//Redraw from the just-applied transform so the gizmo tracks the
					//mouse this frame instead of lagging one frame behind. The pivot
					//is deliberately *not* recomputed mid-drag (drag.start_pivot stays
					//fixed), so a rotate/scale keeps turning about where it started.
					geom = ComputeGeometry(state);
				}
			}

			if (!geom.valid) {
				return;
			}

			//AABB: the 12 edges of the world-space bounding box, for every selected
			//entity, so a multi-entity selection shows exactly what the gizmo will
			//move. The primary is drawn brighter, since it is the one the Components
			//panel edits and the one single-entity commands act on.
			for (Entity e : state.selected_entities) {
				if (!c->ContainsComponent<Bounds>(e)) {
					continue;
				}
				const box& b = c->GetComponent<Bounds>(e).final_box;
				bool primary = (e == state.selected_entity);
				ImU32 color = primary ? COLOR_AABB : COLOR_AABB_SECONDARY;
				float thickness = primary ? 1.5f : 1.0f;
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
							DrawSegment(dl, view_proj, display, corners[i], corners[i | bit], color, thickness);
						}
					}
				}
			}

			if (mode == GizmoMode::Rotate) {
				for (int i = 0; i < 3; ++i) {
					bool hot = (i == hot_axis);
					ImU32 color = hot ? COLOR_AXIS_HOT : COLOR_AXIS[i];
					float thickness = hot ? 4.0f : 2.5f;
					for (int k = 0; k < CIRCLE_SEGMENTS; ++k) {
						DrawSegment(dl, view_proj, display,
							CirclePoint(geom, i, k), CirclePoint(geom, i, k + 1), color, thickness);
					}
					ImVec2 pl;
					if (WorldToScreen(view_proj, display, CirclePoint(geom, i, 0), pl)) {
						dl->AddText(ImVec2(pl.x + 6.0f, pl.y - 6.0f), color, AXIS_LABEL[i]);
					}
				}
				//While rotating, rubber-band the grab direction and the current one.
				if (drag.active) {
					vector3d ray_origin, ray_dir, now;
					MouseRay(cam, display, io.MousePos, ray_origin, ray_dir);
					DrawSegment(dl, view_proj, display, drag.start_pivot,
						drag.start_pivot + drag.rotate_start * geom.axis_len, COLOR_AXIS_HOT, 1.5f);
					if (RayPlaneDir(drag.start_pivot, drag.axis_dir, ray_origin, ray_dir, now)) {
						DrawSegment(dl, view_proj, display, drag.start_pivot,
							drag.start_pivot + now * geom.axis_len, COLOR_AXIS_HOT, 1.5f);
					}
				}
			}
			else {
				for (int i = 0; i < 3; ++i) {
					bool hot = (i == hot_axis);
					ImU32 color = hot ? COLOR_AXIS_HOT : COLOR_AXIS[i];
					vector3d end = geom.origin + geom.axis_dir[i] * geom.axis_len;
					DrawSegment(dl, view_proj, display, geom.origin, end, color, hot ? 4.0f : 2.5f);
					ImVec2 pa, pb;
					if (WorldToScreen(view_proj, display, geom.origin, pa) &&
						WorldToScreen(view_proj, display, end, pb)) {
						if (mode == GizmoMode::Scale) {
							//Square end caps: the visual cue that this is the scale tool.
							dl->AddRectFilled(ImVec2(pb.x - 5.0f, pb.y - 5.0f),
								ImVec2(pb.x + 5.0f, pb.y + 5.0f), color);
						}
						else {
							DrawArrowHead(dl, pa, pb, color);
						}
						dl->AddText(ImVec2(pb.x + 6.0f, pb.y - 6.0f), color, AXIS_LABEL[i]);
					}
				}
				if (mode == GizmoMode::Scale) {
					ImVec2 pc;
					if (WorldToScreen(view_proj, display, geom.origin, pc)) {
						ImU32 color = (hot_axis == UNIFORM_HANDLE) ? COLOR_AXIS_HOT
							: IM_COL32(230, 230, 230, 255);
						dl->AddRectFilled(ImVec2(pc.x - 6.0f, pc.y - 6.0f),
							ImVec2(pc.x + 6.0f, pc.y + 6.0f), color);
					}
				}
			}

			//Numeric readout at the cursor. It reports the primary entity's values;
			//with several selected they all moved by the same gesture, and the
			//count says how many.
			if (drag.active && c->ContainsComponent<Transform>(state.selected_entity)) {
				const Transform& t = c->GetComponent<Transform>(state.selected_entity);
				char buf[96];
				char suffix[24] = "";
				if (drag.targets.size() > 1) {
					snprintf(suffix, sizeof(suffix), "  (%d)", (int)drag.targets.size());
				}
				if (drag.mode == GizmoMode::Rotate) {
					snprintf(buf, sizeof(buf), "%s %+.1f%s%s", AXIS_LABEL[drag.axis],
						drag.angle_deg, "\xC2\xB0", suffix);
				}
				else if (drag.mode == GizmoMode::Scale) {
					snprintf(buf, sizeof(buf), "%.2f  %.2f  %.2f%s",
						t.scale.x, t.scale.y, t.scale.z, suffix);
				}
				else {
					snprintf(buf, sizeof(buf), "%.2f  %.2f  %.2f%s",
						t.position.x, t.position.y, t.position.z, suffix);
				}
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
