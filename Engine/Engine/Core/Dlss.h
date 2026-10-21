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

#include "DXCore.h"

#include <string>

namespace HotBite {
	namespace Engine {
		namespace Core {
			/**
			 * NVIDIA DLSS Super Resolution through NGX: the upscaler of dynamic resolution (RenderSystem::SetDynamicResolution).
			 * The frame is drawn at an internal size that changes from frame to frame and DLSS reconstructs the native image
			 * from it, using the motion vectors, the depth and the sub-pixel jitter of the camera.
			 *
			 * Dynamic resolution exists only where this does: no DLSS, no dynamic resolution (Available() says which, and
			 * Reason() why not). That is the case of any GPU that is not an RTX, of a driver too old for it, of a machine
			 * without nvngx_dlss.dll, and of an engine built without the SDK (the ThirdParty/DLSS submodule: see
			 * Docs/DYNAMIC_RESOLUTION.md), where everything here is a stub.
			 */
			class Dlss {
			public:
				static Dlss& Get();

				//Loads NGX on the device and asks it whether DLSS is there. Safe to call again; the answer is kept.
				bool Init();
				//Releases the feature and NGX. The device must be idle.
				void Shutdown();

				//Whether DLSS can be used on this machine (after Init).
				bool Available() const { return available; }
				//In a few words, why it is not available (empty when it is).
				const std::string& Reason() const { return reason; }

				struct Frame {
					//All in the render size of the frame: color (what DLSS reads), depth (the hardware depth, 1 at the far
					//plane), motion (pixels, from the current position to the one in the previous frame).
					GfxResource* color = nullptr;
					GfxResource* depth = nullptr;
					GfxResource* motion = nullptr;
					//The native-size image DLSS writes (a UAV).
					GfxResource* output = nullptr;
					int render_width = 0;
					int render_height = 0;
					int output_width = 0;
					int output_height = 0;
					//The sub-pixel offset the geometry of this frame was drawn with, in render pixels.
					float jitter_x = 0.0f;
					float jitter_y = 0.0f;
					//Nothing of the previous frames is valid (a cut, a change of size, the first frame).
					bool reset = false;
				};
				//Records the upscale into the frame. False if it could not (the caller then has no picture: it must turn
				//dynamic resolution off). The feature is created on the first call and again when the output size changes.
				bool Evaluate(const Frame& frame);

				//The smallest and the biggest share of the output size DLSS accepts as the render size. NGX answers when the
				//feature is created (the first Evaluate); until then, the most it ever allows.
				float MinScale() const { return min_scale; }
				float MaxScale() const { return max_scale; }
				//What NGX said about the sizes it accepts, for the logs and the automation dump.
				const std::string& Info() const { return info; }

			private:
				Dlss() = default;
				bool initialized = false;
				bool available = false;
				std::string reason = "DLSS has not been initialised";
				std::string info;
				float min_scale = 0.34f;
				float max_scale = 1.0f;
				struct Impl;
				Impl* impl = nullptr;
			};
		}
	}
}
