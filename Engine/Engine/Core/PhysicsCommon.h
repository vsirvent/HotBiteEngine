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

#include <reactphysics3d/reactphysics3d.h>
#include <Defines.h>

#pragma comment(lib, "reactphysics3d.lib")

namespace HotBite {
	namespace Engine {
		namespace Core {

			struct ShapeData {
				reactphysics3d::CollisionShape* shape = nullptr;
				std::vector<float3> vertices;
				std::vector<float3> normals;
				std::vector<unsigned int> indices;
			};

			extern reactphysics3d::PhysicsCommon physics_common;
			extern std::recursive_mutex physics_mutex;

			reactphysics3d::Quaternion slerp(reactphysics3d::Quaternion q1, reactphysics3d::Quaternion q2, float t);
		}
	}
}

