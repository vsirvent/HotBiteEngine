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
#include <ECS/Types.h>
#include <Core/Material.h>
#include <Core/Mesh.h>
#include <Components/Lights.h>
#include <d3d11.h>

namespace HotBite {
	namespace Engine {
		namespace Components {

			/**
			 * The sky component, used by the Renderer to add Sky to the scene.
			 */
			struct Sky {

				Core::MeshData* space_mesh = nullptr;
				Core::MaterialData* space_material = nullptr;
				Components::DirectionalLight* dir_light = nullptr;
				float4x4 space_matrix;

				float cloud_density = 0.5f;
				float3 current_backcolor = {};
				float3 day_backcolor = {};
				float3 mid_backcolor = {};
				float3 night_backcolor = {};

				float second_of_day = 0.0f;
				float second_speed = 1.0f;
				int current_minute = 0;

				Sky() {
					SetTimeOfDay(12, 0, 0);
				}

				~Sky() {
				}

				void SetTimeOfDay(int hour, int minutes, int seconds) {
					second_of_day = (float)(hour * 60 * 60 + minutes * 60 + seconds);					
				}
			};
		}
	}
}