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

namespace HotBite {
	namespace Engine {
		namespace Components {

			struct Camera
			{
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
			};
		}
	}
}
