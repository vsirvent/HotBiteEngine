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

			class ProgressBar : public Label {
			public:
				static inline uint64_t PROGRESSBAR_ID = GetTypeId<ProgressBar>();
				std::shared_ptr<Label> bar;
				float vmin;
				float vmax;
				float value = 0.0f;
				float* dyn_value = nullptr;

			public:

				virtual uint64_t GetType() override {
					return PROGRESSBAR_ID;
				}

				ProgressBar(ECS::Coordinator* c, const std::string& name, float min_value, float max_value,
					        float size = 16.0f, const std::string& fname = "Courier") :
					Label(c, name, "", size, fname), vmin(min_value), vmax(max_value)
				{
					ShowText(false);
					bar = std::make_shared<Label>(c, name + "_bar");
					AddWidget(bar);
					SetText("");
				}

				virtual void SetRect(const float2& pos, float w, float h) override {
					Label::SetRect(pos, w, h);
					bar->SetRect(pos, w, h);
				}

				virtual void SetMaxValue(float val) {
					vmax = val;
				}

				virtual void SetMinValue(float val) {
					vmin = val;
				}

				virtual void SetValue(float val) {
					value = val;
				}

				virtual void SetDynamicValue(float* val) {
					dyn_value = val;
				}
				
				virtual void SetBarColor(float4 color) {
					bar->SetBackgroundColor(color);
				}

				virtual void SetBarImage(const std::string& filename) {
					bar->SetBackGroundImage(filename);
				}
								
				virtual void Refresh() override {
					Label::Refresh();
					float v = std::clamp(value, vmin, vmax);
					float bar_w = width * 0.5f * (v / vmax);
					bar->SetWidth(bar_w);
				}

				virtual void Render(SimplePixelShader* ps) override {
					if (dyn_value != nullptr) {
						if (*dyn_value != value) {
							value = *dyn_value;
							Refresh();
						}
					}
					bar->Render(ps);
					Label::Render(ps);
				}
			};
		}
	}
}
