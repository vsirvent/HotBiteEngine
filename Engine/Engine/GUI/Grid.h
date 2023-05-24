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

#include "TextureWidget.h"
#include "Label.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;
using namespace DirectX;
using namespace DirectX::SimpleMath;

namespace HotBite {
	namespace Engine {
		namespace UI {
			class Grid : public TextureWidget {
			private:
				std::vector<std::vector<std::shared_ptr<Widget>>> grid;
				uint32_t width = 0;
				uint32_t height = 0;
			public:
				static inline uint64_t GRID_ID = GetTypeId<Grid>();

				Grid(ECS::Coordinator* c, const std::string& name, int w, int h, float padding):TextureWidget(c, name), width(w), height(h) {
					grid = std::vector<std::vector<std::shared_ptr<Widget>>>(w, std::vector < std::shared_ptr<Widget>>(h));
					char tmp[128];
					for (int x = 0; x < w; ++x) {
						for (int y = 0; y < h; ++y) {
							snprintf(tmp, 128, "%s_grid_%d_%d", name, x, y);
							auto l = std::make_shared<Label>(c, name);							
							grid[x][y] = l;
						}
					}
				}
				virtual void SetPosition(const float2& pos) {					
					TextureWidget::SetPosition(pos);
#if 0
					//TODO
					for (int x = 0; x < width; ++x) {
						for (int y = 0; y < height; ++y) {
							float p	=																																	
							grid[x][y]->SetPosition(p);
						}
					}
				}
#endif
			};
		}
	}
}