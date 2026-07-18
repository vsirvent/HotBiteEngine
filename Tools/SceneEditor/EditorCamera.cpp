#include "EditorCamera.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Systems;
using namespace DirectX;

namespace HotBiteEditor {

	static constexpr float ORBIT_RADIANS_PER_PIXEL = 0.005f;
	static constexpr float PAN_UNITS_PER_PIXEL = 0.0015f; // additionally scaled by focus distance
	static constexpr float FLY_UNITS_PER_SEC = 12.0f;
	//Wheel while navigating rescales the fly speed exponentially, clamped so a
	//long scroll can neither freeze the camera nor launch it across the level.
	static constexpr float FLY_SPEED_FACTOR_PER_STEP = 1.25f;
	static constexpr float MIN_FLY_SPEED_SCALE = 0.1f;
	static constexpr float MAX_FLY_SPEED_SCALE = 10.0f;
	static constexpr float DOLLY_DISTANCE_FRACTION = 0.15f; // per wheel step
	static constexpr float MIN_FOCUS_DISTANCE = 0.5f;
	//LookToLH uses a fixed world up, which degenerates at the poles.
	static constexpr float MAX_PITCH = XM_PIDIV2 - 0.03f;

	void EditorCamera::Init(World* world)
	{
		Coordinator* c = world->GetCoordinator();
		EventListener::Init(c);
		camera_system = c->GetSystem<CameraSystem>();
		AddEventListener(DXCore::EVENT_ID_MOUSE_MOVE, std::bind(&EditorCamera::OnMouseMove, this, std::placeholders::_1));
		AddEventListener(DXCore::EVENT_ID_MOUSE_WHEEL, std::bind(&EditorCamera::OnMouseWheel, this, std::placeholders::_1));
		AddEventListener(DXCore::EVENT_ID_KEY_DOWN, std::bind(&EditorCamera::OnKeyDown, this, std::placeholders::_1));
		AddEventListener(DXCore::EVENT_ID_KEY_UP, std::bind(&EditorCamera::OnKeyUp, this, std::placeholders::_1));
	}

	CameraSystem::CameraData* EditorCamera::GetCamera()
	{
		if (camera_system == nullptr || camera_system->GetCameras().GetData().empty()) {
			return nullptr;
		}
		return &camera_system->GetCameras().GetData()[0];
	}

	//Moves the whole camera rig: the orbit position and the focus point translate
	//together, so the rendered position shifts by the same delta while the view
	//direction and orbit radius are preserved.
	static void Translate(CameraSystem::CameraData& cam, vector3d delta)
	{
		float3 d;
		XMStoreFloat3(&d, delta);
		cam.transform->position.x += d.x;
		cam.transform->position.y += d.y;
		cam.transform->position.z += d.z;
		cam.camera->direction.x += d.x;
		cam.camera->direction.y += d.y;
		cam.camera->direction.z += d.z;
		cam.transform->dirty = true;
	}

	static float FocusDistance(const CameraSystem::CameraData& cam)
	{
		float3 to_target = cam.camera->direction - cam.transform->position;
		return XMVectorGetX(XMVector3Length(XMLoadFloat3(&to_target)));
	}

	void EditorCamera::Orbit(float dx_pixels, float dy_pixels)
	{
		CameraSystem::CameraData* cam = GetCamera();
		if (cam == nullptr) {
			return;
		}
		camera_system->RotateY(*cam, dx_pixels * ORBIT_RADIANS_PER_PIXEL);
		camera_system->RotateX(*cam, dy_pixels * ORBIT_RADIANS_PER_PIXEL);
		cam->camera->rotation.x = std::clamp(cam->camera->rotation.x, -MAX_PITCH, MAX_PITCH);
	}

	void EditorCamera::Pan(float dx_pixels, float dy_pixels)
	{
		CameraSystem::CameraData* cam = GetCamera();
		if (cam == nullptr) {
			return;
		}
		vector3d forward = XMVector3Normalize(cam->camera->xm_direction);
		vector3d world_up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		vector3d right = XMVector3Normalize(XMVector3Cross(world_up, forward));
		vector3d up = XMVector3Cross(forward, right);
		//The scene should follow the cursor: mouse right pushes the camera left,
		//mouse down pushes it up. Speed scales with the focus distance so panning
		//feels the same zoomed in or out.
		float scale = (std::max)(FocusDistance(*cam), 1.0f) * PAN_UNITS_PER_PIXEL;
		Translate(*cam, (right * (-dx_pixels) + up * dy_pixels) * scale);
	}

	void EditorCamera::Dolly(float wheel_steps)
	{
		CameraSystem::CameraData* cam = GetCamera();
		if (cam == nullptr) {
			return;
		}
		float distance = FocusDistance(*cam);
		float step = wheel_steps * (std::max)(distance * DOLLY_DISTANCE_FRACTION, 0.1f);
		//Never dolly through the focus point: the orbit math degenerates there.
		step = (std::min)(step, distance - MIN_FOCUS_DISTANCE);
		if (step != 0.0f) {
			//Zoom moves the position along the focus direction by -delta.
			camera_system->Zoom(*cam, -step);
		}
	}

	void EditorCamera::Fly(float forward, float right, float up)
	{
		CameraSystem::CameraData* cam = GetCamera();
		if (cam == nullptr) {
			return;
		}
		vector3d fwd = XMVector3Normalize(cam->camera->xm_direction);
		vector3d world_up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		vector3d right_v = XMVector3Normalize(XMVector3Cross(world_up, fwd));
		Translate(*cam, fwd * forward + right_v * right + world_up * up);
	}

	bool EditorCamera::Focus(const float3& center, float radius)
	{
		CameraSystem::CameraData* cam = GetCamera();
		if (cam == nullptr) {
			return false;
		}
		//Far enough that a sphere of `radius` fits comfortably in a ~45deg FOV,
		//but never inside the orbit math's minimum focus distance.
		float distance = (std::max)(radius * 2.5f, MIN_FOCUS_DISTANCE * 2.0f);
		//Keep the current viewing direction: the pivot moves to the target and the
		//orbit position is re-placed along the existing pivot->position offset.
		//(CameraSystem derives the rendered pose from position/direction/rotation,
		//so translating both keeps the same rotation around the new pivot.)
		float3 offset = cam->transform->position - cam->camera->direction;
		vector3d off = XMLoadFloat3(&offset);
		if (XMVectorGetX(XMVector3LengthSq(off)) < 1e-6f) {
			off = XMVectorSet(0.0f, 0.6f, -1.0f, 0.0f); //degenerate rig: default 3/4 view
		}
		off = XMVector3Normalize(off) * distance;
		vector3d pos = XMLoadFloat3(&center) + off;
		XMStoreFloat3(&cam->transform->position, pos);
		cam->camera->direction = center;
		cam->transform->dirty = true;
		return true;
	}

	void EditorCamera::Update(float elapsed_sec)
	{
		if (ImGui::GetIO().WantTextInput) {
			//A text field grabbed the keyboard mid-hold (e.g. clicking into a
			//name filter while flying): drop the held keys so the camera stops.
			//Plain panel focus deliberately does NOT block flying — WASD must
			//work without first clicking the viewport.
			keys_down.clear();
			return;
		}
		if (keys_down.empty()) {
			return;
		}
		auto down = [this](uint32_t vk) { return keys_down.count(vk) != 0; };
		float forward = (down('W') || down(VK_UP) ? 1.0f : 0.0f) - (down('S') || down(VK_DOWN) ? 1.0f : 0.0f);
		float right = (down('D') || down(VK_RIGHT) ? 1.0f : 0.0f) - (down('A') || down(VK_LEFT) ? 1.0f : 0.0f);
		float up = (down('E') ? 1.0f : 0.0f) - (down('Q') ? 1.0f : 0.0f);
		if (forward == 0.0f && right == 0.0f && up == 0.0f) {
			return;
		}
		float speed = FLY_UNITS_PER_SEC * fly_speed_scale * elapsed_sec;
		if (down(VK_SHIFT)) {
			speed *= 4.0f;
		}
		if (down(VK_CONTROL)) {
			speed *= 0.25f;
		}
		Fly(forward * speed, right * speed, up * speed);
	}

	bool EditorCamera::GetPose(Pose& out)
	{
		CameraSystem::CameraData* cam = GetCamera();
		if (cam == nullptr) {
			return false;
		}
		out.position = cam->transform->position;
		out.world_position = cam->camera->final_position;
		out.target = cam->camera->direction;
		out.rotation = cam->camera->rotation;
		return true;
	}

	bool EditorCamera::SetPose(const float3* position, const float3* target, const float3* rotation_rad)
	{
		CameraSystem::CameraData* cam = GetCamera();
		if (cam == nullptr) {
			return false;
		}
		if (position != nullptr) {
			camera_system->SetPosition(*cam, *position);
		}
		if (target != nullptr) {
			cam->camera->direction = *target;
			cam->transform->dirty = true;
		}
		if (rotation_rad != nullptr) {
			camera_system->SetRotation(*cam, *rotation_rad);
		}
		return true;
	}

	void EditorCamera::OnMouseMove(Event& ev)
	{
		int buttons = ev.GetParam<int>(DXCore::PARAM_ID_BUTTON);
		if ((buttons & (MK_RBUTTON | MK_MBUTTON)) == 0) {
			drag_active = false;
			return;
		}
		if (!drag_active) {
			//A new drag: it belongs to the camera only if it starts outside every
			//panel. Once claimed, it stays with the camera even when the cursor
			//crosses a panel, so long orbits are not interrupted.
			if (ImGui::GetIO().WantCaptureMouse) {
				return;
			}
			drag_active = true;
		}
		float dx = (float)ev.GetParam<int>(DXCore::PARAM_RELATIVE_ID_X);
		float dy = (float)ev.GetParam<int>(DXCore::PARAM_RELATIVE_ID_Y);
		if (buttons & MK_RBUTTON) {
			Orbit(dx, dy);
		}
		else if (buttons & MK_MBUTTON) {
			Pan(dx, dy);
		}
	}

	bool EditorCamera::FlyKeysDown() const
	{
		for (uint32_t vk : { (uint32_t)'W', (uint32_t)'A', (uint32_t)'S', (uint32_t)'D',
			(uint32_t)'Q', (uint32_t)'E', (uint32_t)VK_UP, (uint32_t)VK_DOWN,
			(uint32_t)VK_LEFT, (uint32_t)VK_RIGHT }) {
			if (keys_down.count(vk) != 0) {
				return true;
			}
		}
		return false;
	}

	void EditorCamera::OnMouseWheel(Event& ev)
	{
		float steps = ev.GetParam<float>(DXCore::PARAM_ID_WHEEL);
		//While navigating (orbit/pan drag held or fly keys down) the wheel tunes
		//the fly speed instead of dollying, like game-engine editors do. A held
		//drag owns the wheel outright; fly keys only claim it while the cursor is
		//not over a panel, so scrolling a list mid-flight still works.
		auto adjust_speed = [this, steps]() {
			fly_speed_scale = std::clamp(fly_speed_scale * std::pow(FLY_SPEED_FACTOR_PER_STEP, steps),
				MIN_FLY_SPEED_SCALE, MAX_FLY_SPEED_SCALE);
		};
		if (drag_active) {
			adjust_speed();
			return;
		}
		if (ImGui::GetIO().WantCaptureMouse) {
			return;
		}
		if (FlyKeysDown()) {
			adjust_speed();
			return;
		}
		Dolly(steps);
	}

	void EditorCamera::OnKeyDown(Event& ev)
	{
		//Only an active text field blocks camera keys; mere panel focus must not,
		//so WASD flying works without clicking the viewport first.
		if (ImGui::GetIO().WantTextInput) {
			return;
		}
		keys_down.insert(ev.GetParam<uint32_t>(DXCore::PARAM_ID_KEY));
	}

	void EditorCamera::OnKeyUp(Event& ev)
	{
		//Unconditional: a key pressed over the viewport must not stick if ImGui
		//owns the keyboard by the time it is released.
		keys_down.erase(ev.GetParam<uint32_t>(DXCore::PARAM_ID_KEY));
	}
}
