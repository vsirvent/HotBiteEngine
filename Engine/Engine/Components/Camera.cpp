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

#include "Camera.h"

using namespace HotBite::Engine;
using namespace DirectX;

namespace HotBite {
	namespace Engine {
		namespace Components {

			float3 Camera::GetScreenPixel(const matrix& camera_matrix, const float3& world_position) {
				float3 screen_pos;
				vector3d xm_pos = XMVectorSet(world_position.x, world_position.y, world_position.z, 1.0f);
				vector3d xm_screen_pos = XMVector4Transform(xm_pos, camera_matrix);
				vector3d w_vector = XMVectorSplatW(xm_screen_pos);
				vector3d ndc = XMVectorDivide(xm_screen_pos, w_vector);
				XMStoreFloat3(&screen_pos, ndc);
				return screen_pos;
			}
		}
	}
}