#pragma once

// Same include-order constraint as SceneEditor.h: PhysicsCommon.h must precede
// any header that pulls in Windows.h (min/max macros vs reactphysics3d).
#include <Core/PhysicsCommon.h>
#include <Core/DXCore.h>
#include <World.h>
#include <Systems/CameraSystem.h>
#include <set>

namespace HotBiteEditor {

	// Viewport camera controls for moving through the loaded scene:
	//  - right-drag   orbits around the camera's focus point
	//  - middle-drag  pans the camera and its focus point in the view plane
	//  - mouse wheel  dollies toward/away from the focus point
	//  - W/A/S/D or arrows + Q/E (down/up) fly the camera; Shift = fast, Ctrl = slow
	// All paths mutate the level's camera entity through the engine's CameraSystem
	// (the way games do, see Tests/DemoGame/GameCameraSystem), so rendering, DOF and
	// audio pick the motion up with no editor-specific plumbing. Input that ImGui
	// claims (hovered panel, focused text field) is ignored.
	class EditorCamera : public HotBite::Engine::ECS::EventListener {
	public:
		struct Pose {
			HotBite::Engine::float3 position;       // orbit position (Transform component)
			HotBite::Engine::float3 world_position; // final rendered camera position
			HotBite::Engine::float3 target;         // focus point the camera orbits/looks at
			HotBite::Engine::float3 rotation;       // orbit pitch/yaw/roll, radians
		};

		// Hooks the DXCore input events; call once the World's coordinator exists.
		void Init(HotBite::Engine::World* world);

		// Applies held-key fly movement; called once per rendered frame.
		void Update(float elapsed_sec);

		// The interactive motions, exposed so automation commands can exercise the
		// exact code paths the mouse/keyboard use (see EditorAutomation.cpp).
		void Orbit(float dx_pixels, float dy_pixels);
		void Pan(float dx_pixels, float dy_pixels);
		void Dolly(float wheel_steps);
		void Fly(float forward, float right, float up); // world units, camera-relative

		// Both return false when the level has no camera entity yet.
		bool GetPose(Pose& out);
		bool SetPose(const HotBite::Engine::float3* position,
			const HotBite::Engine::float3* target,
			const HotBite::Engine::float3* rotation_rad);

	private:
		HotBite::Engine::Systems::CameraSystem::CameraData* GetCamera();
		void OnMouseMove(HotBite::Engine::ECS::Event& ev);
		void OnMouseWheel(HotBite::Engine::ECS::Event& ev);
		void OnKeyDown(HotBite::Engine::ECS::Event& ev);
		void OnKeyUp(HotBite::Engine::ECS::Event& ev);

		std::shared_ptr<HotBite::Engine::Systems::CameraSystem> camera_system;
		std::set<uint32_t> keys_down;
	};
}
