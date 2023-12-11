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

#include <ECS/Coordinator.h>
#include <Core/DXCore.h>
#include <Core/PostProcess.h>
#include <Core/Texture.h>
#include <Core\Utils.h>
#include <SpriteBatch.h>
#include <SpriteFont.h>
#include <CommonStates.h>
#include <Effects.h>
#include <variant>
#include "Widget.h"
#include "Label.h"
#include "Button.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;
using namespace DirectX;
using namespace DirectX::SimpleMath;

namespace HotBite {
	namespace Engine {
		namespace UI {

			class GUI : public HotBite::Engine::Core::PostProcess, public HotBite::Engine::ECS::IEventSender
			{
			private:
				HotBite::Engine::ECS::Coordinator* coordinator = nullptr;
				HotBite::Engine::Core::RenderTexture2D text;
				virtual void ClearData(const float color[4]);
				std::map<std::string, std::shared_ptr<Widget>> widgets;

			public:
				GUI(ID3D11DeviceContext* dxcontext,
					int width, int height, HotBite::Engine::ECS::Coordinator* c);
				virtual ~GUI();

				virtual ID3D11ShaderResourceView* RenderResource() override;
				virtual ID3D11RenderTargetView* RenderTarget() const override;
				virtual void Prepare() override;
				virtual void UnPrepare() override;

				void AddWidget(std::shared_ptr<Widget> widget) {
					widgets[widget->GetName()] = widget;
					widget->SetRenderTarget(TargetRenderView());
				}

				void RemoveWidget(const std::string& name) {
					auto w = widgets.find(name);
					if (w != widgets.end()) {
						widgets.erase(w);
					}
				}

				bool LoadUI(ECS::Coordinator* c, json ui) {
					Reset();
					try {
						auto& ui_node = ui["ui"];
						auto& widgets = ui_node["widgets"];
						std::unordered_map<std::string, std::shared_ptr<Widget>> widget_by_name;
						for (auto& widget : widgets) {
							//Create the UI in the GUI post process stage
							std::string name = (std::string)widget["name"];
							std::string type = (std::string)widget["type"];
							widget["name"] = name.c_str();
							std::shared_ptr<UI::Widget> w;
							if (type == "widget") {
								w = std::make_shared<UI::Widget>(c, widget, ui_node["root"]);
							}
							else if (type == "label") {
								w = std::make_shared<UI::Label>(c, widget, ui_node["root"]);
							}
							else if (type == "button") {
								w = std::make_shared<UI::Button>(c, widget, ui_node["root"]);
							}
							widget_by_name[w->GetName()] = w;
						}
						for (const auto& widget : widgets) {
							std::string name = widget["name"];
							auto w = widget_by_name[name];
							if (widget.contains("parent")) {
								std::string parent = widget["parent"];
								if (auto it = widget_by_name.find(parent); it != widget_by_name.end()) {
									it->second->AddWidget(w);
								}
								else {
									AddWidget(w);
								}
							}
							else {
								AddWidget(w);
							}
						}
					}
					catch (...) {
						printf("GUI::LoadUI: Error loading ui\n");
						return false;
					}
					return true;
				}

				void Reset() {
					widgets.clear();
				}

				std::shared_ptr<Widget> GetWidget(const std::string& name) {
					std::shared_ptr<Widget> ret;
					auto w = widgets.find(name);
					if (w != widgets.end()) {
						ret = w->second;
					}
					return ret;
				}

				std::shared_ptr<Widget> GetWidget(ECS::Entity id) {
					std::shared_ptr<Widget> ret;
					for (const auto& w : widgets) {
						if (w.second->GetId() == id) {
							ret = w.second;
							break;
						}
					}
					return ret;
				}

				virtual void Render() override {
					PostProcess::Render();
					DXCore::Get()->context->OMSetBlendState(DXCore::Get()->blend, NULL, ~0U);
					for (auto const& widget : widgets) {
						widget.second->Render(ps);
					}
					DXCore::Get()->context->OMSetBlendState(DXCore::Get()->no_blend, NULL, ~0U);
				}
				
			};
		}
	}
}