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
#include <d3d11.h>
#include <vector>
#include "BVH.h"

namespace HotBite {
	namespace Engine {
		namespace Core {

			//The scene a ray traced point light shadow is traced through: the objects that can
			//stand between a surface and a light, and a top level hierarchy over their boxes.
			//Each object points at its mesh's own triangle hierarchy, which is already on the
			//GPU (the reflection and indirect light tracers read the same one).
			//
			//RenderSystem decides *which* objects go in (those whose boxes a ray traced light's
			//range reaches) and calls Load once per frame; this owns the buffers and the
			//hierarchy. Shaders/Common/ShadowRays.hlsli reads them.
			class ShadowRays {
			public:
				//Objects per frame. The top level hierarchy's child links are 16 bit, which would
				//allow far more; this is what the per-frame upload and build are sized for. A
				//scene with more objects in reach of ray traced lights keeps the nearest ones.
				static constexpr uint32_t MAX_SHADOW_OBJECTS = 2048;

				ShadowRays();
				~ShadowRays();

				bool Init();
				void Release();

				//Replaces the scene. `count` is clamped to MAX_SHADOW_OBJECTS; 0 empties it.
				void Load(const ObjectInfo* objects, uint32_t count);

				uint32_t Count() const { return count; }
				bool Ready() const { return ready; }
				ID3D11ShaderResourceView* const* TlasSRV() const { return tlas_buffer.SRV(); }
				ID3D11ShaderResourceView* const* ObjectsSRV() const { return object_buffer.SRV(); }

			private:
				TBVH tbvh;
				ExtBuffer<BVHNode> tlas_buffer;
				ExtBuffer<ObjectInfo> object_buffer;
				uint32_t count = 0;
				bool ready = false;
			};
		}
	}
}
