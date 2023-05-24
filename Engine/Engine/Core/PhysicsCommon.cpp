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

#include "PhysicsCommon.h"
#include <DirectXMath.h>

namespace HotBite {
    namespace Engine {
        namespace Core {
            std::recursive_mutex physics_mutex;
            reactphysics3d::PhysicsCommon physics_common;

            reactphysics3d::Quaternion slerp(reactphysics3d::Quaternion q1, reactphysics3d::Quaternion q2, float t) {
                float angle = acos(q1.dot(q2));
                float denom = sin(angle);
                if (denom < 0.01f) {
                    return q2;
                }
                denom = 1.0f / denom;
                if (angle < DirectX::XM_PI / 2.0f) {
                    return ((q1 * sin((1 - t) * angle) + q2 * sin(t * angle)) * denom).getUnit();
                }
                else {
                    return ((q1 * sin((1 - t) * angle) - q2 * sin(t * angle)) * denom).getUnit();
                }
            }

        }
    }
}