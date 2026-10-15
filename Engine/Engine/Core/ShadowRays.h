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
#include <Core/GfxDevice.h>
#include <vector>
#include "BVH.h"

namespace HotBite {
	namespace Engine {
		namespace Core {

			//Where one scene lives in the buffers (mirrored by the uint4 srScenes reads in
			//Shaders/Common/ShadowRays.hlsli): x the first node of its top level hierarchy in the node
			//buffer, y its first object in the object buffer, z how many objects it has (0: nothing can
			//shadow anything there), w unused.
			struct ShadowScene {
				uint32_t node_offset = 0;
				uint32_t object_offset = 0;
				uint32_t object_count = 0;
				uint32_t padding = 0;
			};
			static_assert(sizeof(ShadowScene) == 16, "ShadowScene is read by HLSL as a uint4");

			//The scenes a ray traced point light shadow is traced through. A ray from a surface to a
			//light can only be stopped by what lies inside that light's range, so every ray traced
			//light has a scene of its own: the objects its range reaches, and a top level hierarchy
			//over their boxes. (One scene for all the lights made every ray walk the objects that only
			//matter to the other lights; the walk of that hierarchy is most of the cost of a ray traced
			//light.) The sun, which reaches everything round the camera, has one too.
			//Each object points at its mesh's own triangle hierarchy, which is already on the GPU (the
			//reflection and indirect light tracers read the same one).
			//
			//RenderSystem decides *which* objects go in which scene and calls Begin, AddScene for each
			//and Upload once per frame; this owns the buffers and the hierarchies.
			//Shaders/Common/ShadowRays.hlsli reads them.
			class ShadowRays {
			public:
				//Objects in one scene: the nearest to its light are kept when more are in reach.
				static constexpr uint32_t MAX_SCENE_OBJECTS = 1024;
				//What all the frame's scenes hold together. An object in the range of three lights is in
				//three scenes (each has its own copy of its matrices), which is what lets a scene be one
				//contiguous run. A light whose scene does not fit in what is left gets none (it lights
				//unshadowed) and is counted in Dropped().
				static constexpr uint32_t MAX_FRAME_OBJECTS = 8192;
				static constexpr uint32_t MAX_FRAME_NODES = 2 * MAX_FRAME_OBJECTS;
				static constexpr uint32_t MAX_SCENES = 1025;
				//Answered by AddScene when the scene did not fit.
				static constexpr uint32_t NO_SCENE = 0xFFFFFFu;

				ShadowRays();
				~ShadowRays();

				bool Init();
				void Release();

				//Empties the frame's scenes.
				void Begin();
				//Adds one: `n` objects (clamped to MAX_SCENE_OBJECTS). Returns its index, or NO_SCENE
				//when there is nothing to put in it or the frame's budget is spent.
				uint32_t AddScene(const ObjectInfo* objects, uint32_t n);
				//Sends the frame's scenes to the GPU.
				void Upload();

				//Objects over all the scenes, the scenes, and the lights' scenes that did not fit.
				uint32_t Count() const { return (uint32_t)objects.size(); }
				uint32_t SceneCount() const { return (uint32_t)scenes.size(); }
				uint32_t Dropped() const { return dropped; }
				bool Ready() const { return ready; }
				GfxSRV* const* TlasSRV() const { return tlas_buffer.SRV(); }
				GfxSRV* const* ObjectsSRV() const { return object_buffer.SRV(); }
				GfxSRV* const* ScenesSRV() const { return scene_buffer.SRV(); }

			private:
				TBVH tbvh;
				ExtBuffer<BVHNode> tlas_buffer;
				ExtBuffer<ObjectInfo> object_buffer;
				ExtBuffer<ShadowScene> scene_buffer;
				//The frame's scenes as the CPU built them, until Upload.
				std::vector<BVHNode> nodes;
				std::vector<ObjectInfo> objects;
				std::vector<ShadowScene> scenes;
				uint32_t dropped = 0;
				bool ready = false;
			};
		}
	}
}