/*
The HotBite Game Engine

Copyright(c) 2023 Vicente Sirvent Orts

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include <ECS\Coordinator.h>
#include <ECS\EntityVector.h>
#include <Components\Base.h>
#include <Components\Physics.h>
#include <Components\Camera.h>
#include <Systems\CameraSystem.h>
#include <Core\Scheduler.h>
#include <Core\SpinLock.h>

namespace HotBite {
	namespace Engine {
		namespace Systems {

			/**
			* This is a system for controlling camera movement for typical RTS game.
			* As every system in the engine, it's registered in the coordinator.
			* This system works with CameraSystem::CameraData components, therefore it has a specific signature
			* to register entities including the specific components.
			* 
			* This class also makes use of the EventManager to receive mouse inputs from the DXCore class. Note that
			* it's not required to have access to the event sender, just register as event listener in the coordinator.
			* 
			* Zoom is performed with smooth movement as a periodic task executed in the background worker scheduler at 120 fps.
			*/
			class RTSCameraSystem : public ECS::System, public ECS::EventListener {
			public:
				static inline ECS::EventId EVENT_ID_CAMERA_ZOOMED = ECS::GetEventId<RTSCameraSystem>(0x00);
				static inline ECS::ParamId EVENT_PARAM_CAMERA_ZOOM = 0x00;
				static constexpr float MAX_ZOOM = 30.0f;
				static constexpr float MIN_ZOOM = 10.0f;
				static constexpr float ZOOM_RANGE = MAX_ZOOM - MIN_ZOOM;

			private:
				struct TerrainData {
					HotBite::Engine::Components::Base* base = nullptr;
					HotBite::Engine::Components::Transform* transform = nullptr;;
					HotBite::Engine::Components::Bounds* bounds = nullptr;;
					HotBite::Engine::Components::Physics* physics = nullptr;;
					TerrainData() {
					}

					TerrainData(const TerrainData& other) {
						base = other.base;
						transform = other.transform;
						bounds = other.bounds;
						physics = other.physics;
					}

					TerrainData(HotBite::Engine::ECS::Coordinator* c, HotBite::Engine::ECS::Entity entity) {
						base = c->GetComponentPtr<HotBite::Engine::Components::Base>(entity);
						transform = c->GetComponentPtr<HotBite::Engine::Components::Transform>(entity);
						bounds = c->GetComponentPtr<HotBite::Engine::Components::Bounds>(entity);
						//Optional, and checked rather than assumed: GetComponentPtr throws on a component
						//the entity does not have (same pattern as PlatformSystem/ForceSystem) - a terrain
						//entity with no Physics just skips the raycast-based zoom/height check below.
						if (c->ContainsComponent<HotBite::Engine::Components::Physics>(entity)) {
							physics = c->GetComponentPtr<HotBite::Engine::Components::Physics>(entity);
						}
					}
				};
				TerrainData terrain;
				//The ECS coordinator
				ECS::Coordinator* coordinator = nullptr;
				//Signature of the entities we are interested
				ECS::Signature signature;
				//Entity vector (this is a flat map), where we are storing the entity and components
				ECS::EntityVector<CameraSystem::CameraData> cameras;

				float2 last_mouse_pos{};
				int scroll_timer = Core::Scheduler::INVALID_TIMER_ID;
				
				static constexpr float MAX_RY = DirectX::XM_PIDIV4 / 2.0f;
				static constexpr float MIN_RY = 0.0f;

				//Panning by pushing the mouse against a screen edge. A game that pans with the
				//keyboard turns it off so the pointer can rest on a HUD at the edge.
				bool edge_pan_enabled = true;
				//Multipliers on the wheel zoom (both the size of a notch and how fast the smooth
				//zoom travels) and on the middle-drag rotation, for a game that exposes them as
				//options. 1 is the engine's own tuning.
				float zoom_speed = 1.0f;
				float mouse_rotate_speed = 1.0f;
				bool zooming = false;
				float current_zoom = MAX_ZOOM;
				float new_zoom = current_zoom;
				float min_zoom = MIN_ZOOM;
				float max_zoom = MAX_ZOOM;
				float current_ry = DirectX::XM_PIDIV4 / 4.0f;
				float3 move_direction = {};
				int zoom_check_timer = 0;
				bool check_zoom = true;
				std::recursive_mutex lock;

				void RotateX(CameraSystem::CameraData& entity, float x);
				void RotateY(CameraSystem::CameraData& entity, float y);
				void RotateZ(CameraSystem::CameraData& entity, float z);
				void Move(CameraSystem::CameraData& entity, float2 dpos);
				void Zoom(CameraSystem::CameraData& entity, float delta);

				//Events for mouse movement and wheel
				void OnMouseMove(ECS::Event& ev);
				void OnMouseWheel(ECS::Event& ev);
				//timer to check zoom with terrain
				void OnCameraMoved(ECS::Event& ev);
				bool OnZoomCheck(const HotBite::Engine::Core::Scheduler::TimerData& t);
			public:
				//Callback from coordinator when system is registered
				void OnRegister(ECS::Coordinator* c) override;
				//Callback from coordinator when entity signature is changed (a component has been added/removed)
				void OnEntitySignatureChanged(ECS::Entity entity, const ECS::Signature& entity_signature) override;
				//Callback from coordinatir when an entity is destroyed
				void OnEntityDestroyed(ECS::Entity entity) override;

			public:
				RTSCameraSystem() = default;
				virtual ~RTSCameraSystem();

				void SetCameraDirection(const float2& position);
				void SetCameraPosition(const float2& position, bool centered = false);
				void SetCameraRelativePosition(const float2& position, bool centered = false);
				ECS::EntityVector<CameraSystem::CameraData>& GetCameras();

				// One pan step for a keyboard (or any non-mouse) source, in the same
				// screen-edge-relative frame and zoom-dependent speed edge-pan already
				// uses - `direction` is typically a unit vector, so a caller driving WASD
				// gets the same speed/feel as pushing the mouse to the matching screen
				// edge. Call once per tick while movement should continue; there is no
				// separate start/stop pair, the caller owns that timing (same "explicit
				// per-tick call" shape the rest of this engine's gameplay systems use).
				void Pan(const float2& direction);

				// One pan step in the camera's own frame: `right_forward.x` is to the right of
				// where the camera is looking and `right_forward.y` is forward, both on the ground
				// plane, whatever the camera's yaw. Same speed as Pan()/edge-pan, same terrain
				// clamp. Pan() is a fixed screen-edge frame that Move() then flips by which side
				// of its target the camera *started* on, so it stays put when the camera rotates;
				// this reads the camera's actual heading each call.
				void PanRelative(const float2& right_forward);

				void SetEdgePanEnabled(bool enabled) { edge_pan_enabled = enabled; }
				bool GetEdgePanEnabled() const { return edge_pan_enabled; }

				// One yaw step around the camera's own vertical axis, in radians. Public
				// wrapper around the same per-entity RotateY the middle-mouse-drag path
				// already drives, for a caller with no CameraData of its own to pass -
				// e.g. a keyboard rotate key, called once per tick with a small step
				// while held, the same "explicit per-tick call" shape as Pan() above.
				void Rotate(float yaw_radians);

				void SetZoomSpeed(float value) { zoom_speed = value; }
				float GetZoomSpeed() const { return zoom_speed; }
				void SetMouseRotateSpeed(float value) { mouse_rotate_speed = value; }
				float GetMouseRotateSpeed() const { return mouse_rotate_speed; }

				void SetMaximumZoom(float value) { max_zoom = value; }
				void SetMinimumZoom(float value) { min_zoom = value; }

				float GetMaximumZoom() const { return max_zoom; }
				float GetMinimumZoom() const { return min_zoom; }
				float GetCurrentZoom() const { return current_zoom; }
				// For a caller (e.g. game setup code) that places the camera at a distance
				// of its own choosing rather than the class's MAX_ZOOM default - keeps this
				// system's internal zoom bookkeeping in sync with the real camera-to-target
				// distance, the same reason current_zoom defaults to MAX_ZOOM: a mismatch
				// here makes the first Zoom() call jump the camera to reconcile the two
				// instead of moving smoothly from where it actually is. Also resets
				// new_zoom (Zoom()'s smoothing target), though nothing reads it until the
				// next Zoom() call recomputes it from current_zoom anyway.
				void SetCurrentZoom(float value) { current_zoom = value; new_zoom = value; }
				void SetTerrain(ECS::Entity e);
			};
		}
	}
}
