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

#include <Defines.h>
#include <ECS/Serialization.h>

namespace HotBite {
	namespace Engine {
		namespace Components {

			struct Camera
			{
				static constexpr const char* NAME = "Camera";

				//Registered so the Inspector can show it, but every field here is
				//derived: the camera system rebuilds the whole set of matrices from the
				//entity's Transform each frame. Nothing to author, so ToJson reports the
				//current pose for display and FromJson does nothing. Registered with
				//ComponentPolicy::Locked - neither added nor removed by hand.
				nlohmann::json ToJson(const ECS::SerializeContext& ctx) const {
					return nlohmann::json{
						{"position", ECS::JsonUtil::FromFloat3(world_position)},
						{"direction", ECS::JsonUtil::FromFloat3(direction)},
						{"rotation", ECS::JsonUtil::FromFloat3(rotation)},
					};
				}
				void FromJson(const nlohmann::json& j, const ECS::SerializeContext& ctx) {}

				/**
				* The Camera view matrix is used to transform from
				* world 3d coords to the camera 3d coords.
				* When rendering a triangle mesh, its vertices are transformed first from
				* model space to world space, and then from world space to view space.
				* This way we get the vertex position related to the camera position.
				* We don't move the camera, we move the world to the camera.
				* To perform this latter transformation, we need the world-to-view matrix, which
				* is the inverse of the view-to-world matrix. This matrix is sometimes called the
				* view matrix:
				* (M)W→V = (M^−1)V→W = (M)view.
				*/
				matrix xm_view;
				/**
				* In order to render a 3D scene onto a 2D image plane, we use a special kind
				* of transformation known as a projection => projectionMatrix
				*/
				float4x4 projection;
				float4x4 inverse_projection;
				matrix xm_projection;

				float4x4 prev_view_projection;
				float4x4 view_projection;
				matrix xm_view_projection;

				/**
				 * This is the location and orientation of the camera in the world,
				 * including a direction and a rotation.
				 */
				float3 world_position = {};
				float3 direction = {};
				/* This is a rotation in XYZ, different from Transform rotation quaternion */
				float3 rotation = {};
				float3 final_position = {};
				float3 last_parent_pos = {};
				vector3d xm_direction;
				// Rotation quaternion
				vector4d xm_rotation;
				float4x4 view;
				float4x4 inverse_view;

				static float3 GetScreenPixel(const matrix& camera_matrix, const float3& world_position);

				/**
				* The frustum shape, recovered from `xm_projection` rather than from the
				* constants CameraSystem::Init happens to build it with, so anything
				* reasoning about the view volume (shadow cascade fitting, and the editor
				* overlay that draws it) stays correct if the camera's field of view,
				* aspect ratio or clip planes ever change. Returns false for a projection
				* that is not a finite left-handed perspective one.
				*
				* `tan_half_h`/`tan_half_v` are the tangents of the horizontal and vertical
				* half angles: a frustum corner at view depth z sits tan_half_h * z off the
				* view axis horizontally and tan_half_v * z vertically.
				*/
				bool GetFrustumParams(float& tan_half_h, float& tan_half_v,
					float& near_z, float& far_z) const {
					DirectX::XMFLOAT4X4 p;
					DirectX::XMStoreFloat4x4(&p, xm_projection);
					//_11 = 1/(aspect*tan(fovY/2)), _22 = 1/tan(fovY/2),
					//_33 = f/(f-n), _43 = -n*f/(f-n).
					if (p._11 <= 1e-6f || p._22 <= 1e-6f || p._33 <= 1e-6f) {
						return false;
					}
					const float denom = 1.0f - p._33;
					if (fabsf(denom) < 1e-9f) {
						return false;
					}
					tan_half_h = 1.0f / p._11;
					tan_half_v = 1.0f / p._22;
					near_z = -p._43 / p._33;
					far_z = p._43 / denom;
					return near_z > 0.0f && far_z > near_z;
				}
			};
		}
	}
}
