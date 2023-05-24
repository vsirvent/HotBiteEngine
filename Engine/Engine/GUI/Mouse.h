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

#include "Label.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;
using namespace DirectX;
using namespace DirectX::SimpleMath;

namespace HotBite {
	namespace Engine {
		namespace UI {

			class Mouse : public Widget, public ECS::EventListener {
			public:
				static inline uint64_t MOUSE_ID = GetTypeId<Mouse>();
				static inline ECS::EventId EVENT_ID_MOUSE_CURSOR_SET = ECS::GetEventId<Mouse>(0x00);
				static inline ECS::ParamId EVENT_PARAM_CURSOR_ID = 0x00;

				std::unordered_map<uint32_t, ID3D11ShaderResourceView*> cursors;
				uint32_t current_cursor = 0;


				virtual void OnSetCursor(ECS::Event& ev) {
					uint32_t id = ev.GetParam<uint32_t>(EVENT_PARAM_CURSOR_ID);
					SetCursor(id);
				}

				virtual void OnMouseMove(ECS::Event& ev) {
					float w = (float)DXCore::Get()->GetWidth();
					float h = (float)DXCore::Get()->GetHeight();
					float2 coords;

					coords.x = std::clamp((float)ev.GetParam<int>(DXCore::PARAM_ID_X) / w, 0.0f, 1.0f);
					coords.y = std::clamp(1.0f - (float)ev.GetParam<int>(DXCore::PARAM_ID_Y) / h - height / 2.0f, 0.0f, 1.0f);

					SetPosition(coords);
				}

			public:

				virtual uint64_t GetType() override {
					return MOUSE_ID;
				}
				
				Mouse(ECS::Coordinator* c, const std::string& name) :
					Widget(c, name)
				{
					EventListener::Init(c);
					AddEventListener(DXCore::EVENT_ID_MOUSE_MOVE, std::bind(&Mouse::OnMouseMove, this, std::placeholders::_1));
					AddEventListener(Mouse::EVENT_ID_MOUSE_CURSOR_SET, std::bind(&Mouse::OnSetCursor, this, std::placeholders::_1));
					ShowCursor(false);
				}

				virtual ~Mouse() {
					ShowCursor(true);
					for (auto const& c : cursors) {
						if (c.second) {
							c.second->Release();
						}
					}
				}

				void AddCursor(uint32_t id, const std::string& image_file) {
					auto it = cursors.find(id);
					if (it != cursors.end()) {
						if (it->second) {
							it->second->Release();
						}
						it->second = LoadTexture(image_file);
					}
					else {
						cursors[id] = LoadTexture(image_file);
					}
				}

				void SetCursor(uint32_t id) {
					auto it = cursors.find(id);
					if (it != cursors.end()) {
						if (it->second) {
							if (background_image != nullptr) {
								background_image->Release();
							}
							background_image = it->second;
							background_image->AddRef();
						}
					}
				}
			};
		}
	}
}
