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
#include "InteractiveWidget.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;
using namespace DirectX;
using namespace DirectX::SimpleMath;

namespace HotBite {
	namespace Engine {
		namespace UI {

			class Button : public Label, public ECS::EventListener {
			private:
				std::shared_ptr<InteractiveWidget> interactive;

			public:
				static inline uint64_t BUTTON_WIDGET_ID = GetTypeId<Button>();
				ID3D11ShaderResourceView* click_image = nullptr;
				ID3D11ShaderResourceView* idle_image = nullptr;

			public:

				virtual uint64_t GetType() override {
					return BUTTON_WIDGET_ID;
				}

				Button(ECS::Coordinator* c, const std::string& name, const std::string& txt = "", float size = 0.0f,
					const std::string& fname = "", DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE s = DWRITE_FONT_STYLE_NORMAL,
					DWRITE_TEXT_ALIGNMENT ha = DWRITE_TEXT_ALIGNMENT_LEADING,
					DWRITE_PARAGRAPH_ALIGNMENT va = DWRITE_PARAGRAPH_ALIGNMENT_CENTER) : Label(c, name, txt, size, fname, w, s, ha, va) {
					Init(c);
					interactive = std::make_shared<UI::InteractiveWidget>(this);
					AddEventListenerByEntity(UI::InteractiveWidget::EVENT_ID_MOUSE_LDOWN,
						GetId(),
						std::bind(&Button::OnLDown, this, std::placeholders::_1));
					AddEventListenerByEntity(UI::InteractiveWidget::EVENT_ID_MOUSE_LUP,
						GetId(),
						std::bind(&Button::OnLUp, this, std::placeholders::_1));

				}

				Button(ECS::Coordinator* c, const json& config, const std::string& root) : Label(c, config, root) {
					SetPressedImage(config["click_image"]);
					SetIdleImage(config["idle_image"]);
				}

				virtual ~Button() {
				}

				virtual void OnLDown(ECS::Event& ev) {
					if (click_image != nullptr) {
						background_image = click_image;
					}
				}

				virtual void OnLUp(ECS::Event& ev) {
					background_image = idle_image;
				}

				virtual void SetIdleImage(const std::string& idle) {
					Widget::SetBackGroundImage(idle);
					idle_image = background_image;
				}

				virtual void SetPressedImage(const std::string& filename) {
					if (click_image != nullptr) {
						click_image->Release();
					}
					click_image = LoadTexture(filename);
				}
			};
		}
	}
}