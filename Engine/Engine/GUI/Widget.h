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
#include <Core\Json.h>
#include <Components\Base.h>
#include <SpriteBatch.h>
#include <SpriteFont.h>
#include <CommonStates.h>
#include <Effects.h>
#include <variant>

using namespace nlohmann;
using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;
using namespace DirectX;
using namespace DirectX::SimpleMath;

namespace HotBite {
	namespace Engine {
		namespace UI {

			//Calculate required font size depending the screen 
			//resolution and the desired font size in screen percetnage
			float CalculateFontSize(float font_size_percentage);

			class InteractiveWidget;
			class Widget: public ECS::IEventSender {
			public:
				static inline uint64_t WIDGET_ID = GetTypeId<Widget>();
			protected:
#define WIDGET_FLAG (1 << 0)
#define BACKGROUND_IMAGE_FLAG (1 << 1)
#define BACKGROUND_IMAGE_ALPHA_FLAG (1 << 2)				
#define WIDGET_INVERT_UV_X (1 << 3)
#define WIDGET_INVERT_UV_Y (1 << 4)

				using VertexType = VertexPositionColorTexture;
				float4 background_color;
				ID3D11ShaderResourceView* background_image = nullptr;
				ID3D11RenderTargetView* d3d_render_target = nullptr;
				float3 background_alpha_color;
				bool background_alpha_color_enabled = false;
				float background_alpha = 1.0f;
				float2 position;
				float  width = 0.0f;
				float  height = 0.0f;
				bool init = false;
				std::string name;
				std::unique_ptr<PrimitiveBatch<VertexType>> sprite;
				std::map<std::string, std::shared_ptr<Widget>> background_widgets;
				std::map<std::string, std::shared_ptr<Widget>> foreground_widgets;
				bool visible = true;
				float flayer = 0.001f;
				VertexType v[4];
				ECS::Entity id;
				ECS::Coordinator* coordinator;

			public:
				Widget(ECS::Coordinator* c, const json& config, const std::string& root): coordinator(c) {
					name = config["name"];
					sprite = std::make_unique<PrimitiveBatch<VertexType>>(DXCore::Get()->context);
					id = coordinator->CreateEntity(name);
					SetLayer(config["layer"]);
					SetVisible(config["visible"]);
					std::string background_image = config["background_image"];
					if (!background_image.empty()) {
						SetBackGroundImage(root + "\\" + background_image);
					}
					SetBackgroundColor(parseColorStringF4(config["background_color"]));
					SetBackgroundAlphaColor(parseColorStringF3(config["background_alpha_color"]));
					SetBackgroundAlpha(config["background_alpha"]);
					SetPosition({ config["x"], config["y"] });
					SetHeight(config["height"]);
					SetWidth(config["width"]);
				}

				Widget(ECS::Coordinator* c, const std::string& widget_name) : name(widget_name), coordinator(c) {
					sprite = std::make_unique<PrimitiveBatch<VertexType>>(DXCore::Get()->context);
					id = coordinator->CreateEntity(name);
				}

				virtual ~Widget() {
					if (background_image != nullptr) {
						background_image->Release();
					}
					coordinator->DestroyEntity(id);
				}

				ECS::Entity GetId() const { return id; }

				virtual void AddBackgroundWidget(std::shared_ptr<Widget> widget) {
					background_widgets[widget->GetName()] = widget;
					widget->SetRenderTarget(d3d_render_target);
					widget->SetVisible(visible);
				}

				virtual void AddWidget(std::shared_ptr<Widget> widget) {
					foreground_widgets[widget->GetName()] = widget;
					widget->SetRenderTarget(d3d_render_target);					
					widget->SetVisible(visible);
				}

				virtual void RemoveWidget(const std::string& name) {
					auto w = foreground_widgets.find(name);
					if (w != foreground_widgets.end()) {
						foreground_widgets.erase(w);
					}
				}

				virtual void RemoveBackgroundWidget(const std::string& name) {
					auto w = background_widgets.find(name);
					if (w != background_widgets.end()) {
						background_widgets.erase(w);
					}
				}

				virtual void SetLayer(int layer) {
					flayer = (float)layer / 100.0f;
					Refresh();
				}

				void SetVisible(bool v) {
					visible = v;
					for (const auto& w : background_widgets) {
						w.second->SetVisible(v);
					}
					for (const auto& w : foreground_widgets) {
						w.second->SetVisible(v);
					}
				}

				bool IsVisible() const {
					return visible;
				}

				virtual void SetRenderTarget(ID3D11RenderTargetView* target) {
					d3d_render_target = target;
					for (const auto& w : background_widgets) {
						w.second->SetRenderTarget(target);
					}
					for (const auto& w : foreground_widgets) {
						w.second->SetRenderTarget(target);
					}
					Refresh();
				}

				virtual uint64_t GetType() {
					return WIDGET_ID;
				}

				const std::string& GetName() const { return name; }

				void SetBackGroundImage(ID3D11ShaderResourceView* image) {
					if (background_image != nullptr) {
						background_image->Release();
					}
					background_image = image;
					if (background_image != nullptr) {
						background_image->AddRef();
					}
				}

				void SetBackGroundImage(const std::string& filename) {
					if (background_image != nullptr) {
						background_image->Release();
					}
					background_image = LoadTexture(filename);
				}

				virtual void SetBackgroundAlphaColor(const float3& color) {
					background_alpha_color = color;
					SetEnabledBackgroundAlphaColor(true);
				}

				virtual void SetEnabledBackgroundAlphaColor(bool enabled) {
					background_alpha_color_enabled = enabled;
				}

				virtual void SetBackgroundColor(const float4& color) {
					background_color = color;
					Refresh();
				}

				virtual void SetRect(const float2& pos, float w, float h) {
					position = { 2.0f * pos.x - 1.0f,  2.0f * pos.y - 1.0f };
					width = 2.0f * w;
					height = 2.0f * h;
					Refresh();
				}

				virtual float2 GetPosition() const {
					return position;
				}

				virtual void SetPosition(const float2& pos) {
					position = { 2.0f * pos.x - 1.0f,  2.0f * pos.y - 1.0f };
					Refresh();
				}

				virtual float GetWidth(void) const {
					return (width / 2.0f);
				}

				virtual float GetHeight(void) const {
					return (height / 2.0f);
				}

				virtual void SetWidth(float w) {
					width = 2.0f * w;
					Refresh();
				}

				virtual void SetHeight(float h) {
					height = 2.0f * h;
					Refresh();
				}

				virtual void SetBackgroundAlpha(float a) {
					background_alpha = a;
				}

				virtual void Refresh() {
					init = false;
					if (width > 0.0f && height > 0.0f) {
						v[0] = { float3(position.x, position.y + height, flayer), background_color, float2(0.f, 0.f) };
						v[1] = { float3(position.x + width, position.y + height, flayer), background_color , float2(1.f, 0.f) };
						v[2] = { float3(position.x + width, position.y, flayer), background_color, float2(1.f, 1.f) };
						v[3] = { float3(position.x, position.y, flayer), background_color,  float2(0.f, 1.f) };
						init = true;
					}
				}

				virtual void Render(SimplePixelShader* ps) {
					if (visible && d3d_render_target != nullptr) {
						if (!init) {
							Refresh();
						}
						if (init) {
							for (const auto& w : background_widgets) {
								w.second->Render(ps);
							}
							int ui_flags = WIDGET_FLAG;
							if (background_image != nullptr) {
								ui_flags |= BACKGROUND_IMAGE_FLAG;
								ps->SetShaderResourceView("background_texture", background_image);
							}
							if (background_alpha_color_enabled) {
								ui_flags |= BACKGROUND_IMAGE_ALPHA_FLAG;
								ps->SetFloat3("background_alpha_color", background_alpha_color);
							}
							ps->SetFloat("alpha", background_alpha);
							ps->SetInt(SimpleShaderKeys::UI_FLAGS, ui_flags);
							ps->CopyAllBufferData();
							sprite->Begin();
							sprite->DrawQuad(v[0], v[1], v[2], v[3]);
							sprite->End();
							ps->SetInt(SimpleShaderKeys::UI_FLAGS, 0);
							if (background_image != nullptr) {
								ps->SetShaderResourceView("background_texture", nullptr);
							}
							ps->CopyAllBufferData();	
							for (const auto& w : foreground_widgets) {
								w.second->Render(ps);
							}
						}
					}
				}

				friend class InteractiveWidget;
			};
		}
	}
}