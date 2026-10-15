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
#include <SimpleMath.h>
#include <VertexTypes.h>
#include <variant>
#include "Widget.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;
using namespace DirectX;
using namespace DirectX::SimpleMath;

namespace HotBite {
	namespace Engine {
		namespace UI {

			class Label : public Widget {
			public:
				static inline uint64_t LABEL_ID = GetTypeId<Label>();

				using text_param = std::variant <const uint32_t*, const float*, const int*>;

				int get_value(text_param& var) {
					switch (var.index()) {
					case 0: return (int)static_cast<uint32_t>(*std::get<const uint32_t*>(var));
					case 1:	return (int)static_cast<float>(*std::get<const float*>(var));
					case 2:	return (int)static_cast<int>(*std::get<const int*>(var));
					}
					return 0;
				}

				std::string text;

				//The text as a texture the size of its layout box, rebuilt when the text, its font or its colour changes
				//(Direct2D rasterizes it on the CPU, see Direct2D::RenderText) and drawn as a quad after the widget's
				//own, in widget order. One texture per label, kept while its size stays the same: text that changes every
				//frame (a clock, an fps counter) is only a new upload, not a new GPU resource.
				DynamicTexture2D text_texture;
				bool text_dirty = true;
				IDWriteTextFormat* text_format = nullptr;
				IDWriteTextLayout* text_layout = nullptr;
				std::string font_name;
				float font_size_orig = 0.0f;
				float font_size = 0.0f;
				D2D1_COLOR_F text_color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
				D2D1_POINT_2F origin = D2D1::Point2F(0.0f, 0.0f);
				float rotation = 0.0f;
				std::vector<text_param> parameters;
				std::vector<uint32_t> last_parameters;
				uint32_t frame = 0;
				bool show_text = true;
				float margin = 0.01f;
				DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
				DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL;
				DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING;
				DWRITE_PARAGRAPH_ALIGNMENT valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;

				void Release() {
					if (text_format != nullptr) {
						text_format->Release();
						text_format = nullptr;
					}
					if (text_layout != nullptr) {
						text_layout->Release();
						text_layout = nullptr;
					}
				}

				//Rasterizes the current layout into the label's texture. Called from Render, so on the render thread.
				void RebuildTextTexture() {
					text_dirty = false;
					if (text_layout == nullptr) {
						text_texture.Release();
						return;
					}
					const float scale = (float)DXCore::Get()->GetDpi() / 96.0f;
					const uint32_t w = (uint32_t)std::ceil(text_layout->GetMaxWidth() * scale);
					const uint32_t h = (uint32_t)std::ceil(text_layout->GetMaxHeight() * scale);
					std::vector<uint8_t> rgba;
					if (w == 0 || h == 0 || w > 8192 || h > 8192 ||
						!Direct2D::Get()->RenderText(text_layout, text_color, w, h, (float)DXCore::Get()->GetDpi(), rgba)) {
						return;
					}
					if (text_texture.Width() != (int)w || text_texture.Height() != (int)h) {
						if (FAILED(text_texture.Init((int)w, (int)h, TextureFormat::RGBA8))) {
							return;
						}
					}
					text_texture.Update(rgba.data());
				}

				void RefreshTextLayout() {
					if (text_layout != nullptr) {
						text_layout->Release();
					}
					Direct2D* d2d = Direct2D::Get();
					std::string final_text = GetText();
					last_parameters.clear();
					for (int i = 0; i < parameters.size(); ++i) {
						last_parameters.push_back(get_value(parameters[i]));
											}
											text_dirty = true;
					text_layout = d2d->GetText(final_text, text_format, (width - 2.0f* margin) * 0.5f * DXCore::Get()->GetScreenWidth(), height * 0.5f * DXCore::Get()->GetScreenHeight());
				}

			public:

				virtual uint64_t GetType() override {
					return LABEL_ID;
				}

				Label(ECS::Coordinator* c, const json& config, const std::string& root) : Widget(c, config, root) {					
					weight = config["weight"];
					style = config["style"];
					align = config["halign"];
					valign = config["valign"];
					SetFontSize(config["font_size"]);
					font_name = config["font_name"];
					SetTextMargin(config["margin"]);
					SetText(config["text"]);
					SetTextColor(parseColorStringF4(config["text_color"]));
					ShowText(config["show_text"]);
					Refresh();
				}

				Label(ECS::Coordinator* c, const std::string& name, const std::string& txt = "", float size = 0.0f,
					const std::string& fname = "", DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE s = DWRITE_FONT_STYLE_NORMAL,
					DWRITE_TEXT_ALIGNMENT ha = DWRITE_TEXT_ALIGNMENT_LEADING,
					DWRITE_PARAGRAPH_ALIGNMENT va = DWRITE_PARAGRAPH_ALIGNMENT_CENTER) :
					Widget(c, name), weight(w), style(s), align(ha), valign(va) {
					text = txt;
					font_size_orig = size;
					font_size = CalculateFontSize(font_size_orig);
					font_name = fname;
					Refresh();
				}

				virtual ~Label() {
					Release();
				}

				virtual float GetFontSize() const {
					return font_size_orig;
				}

				virtual void SetFontSize(float size) {
					font_size_orig = size;
					font_size = CalculateFontSize(font_size_orig);
					Refresh();
				}

				virtual void SetTextMargin(float m) {
					margin = m;
				}

				virtual float GetTextMargin() const {
					return margin;
				}

				virtual void Render(SimplePixelShader* ps) override {
					if (visible) {
						Widget::Render(ps);
						if (d3d_render_target != nullptr && text_layout != nullptr) {
							if (frame++ % 10 == 0) {
								for (int i = 0; i < parameters.size(); ++i) {
									if (get_value(parameters[i]) != last_parameters[i]) {
										RefreshTextLayout();
										break;
									}
								}
							}
							if (text_dirty) {
								RebuildTextTexture();
							}
							if (show_text && text_texture.Valid()) {
								//The layout box is the widget's rectangle without its horizontal margins, so the text
								//quad is that rectangle: its bottom edge is the widget's, as the layout used to be placed.
								const float left = position.x + margin;
								const float right = position.x + width - margin;
								const float bottom = position.y;
								const float top = position.y + height;
								const float4 white = { 1.0f, 1.0f, 1.0f, 1.0f };
								const VertexType q[4] = {
									{ float3(left, top, flayer), white, float2(0.f, 0.f) },
									{ float3(right, top, flayer), white, float2(1.f, 0.f) },
									{ float3(right, bottom, flayer), white, float2(1.f, 1.f) },
									{ float3(left, bottom, flayer), white, float2(0.f, 1.f) } };
								ps->SetShaderResourceView("background_texture", text_texture.SRV());
								ps->SetFloat("alpha", 1.0f);
								ps->SetInt(SimpleShaderKeys::UI_FLAGS, WIDGET_FLAG | BACKGROUND_IMAGE_FLAG);
								ps->CopyAllBufferData();
								sprite->Begin();
								sprite->DrawQuad(q[0], q[1], q[2], q[3]);
								sprite->End();
								ps->SetInt(SimpleShaderKeys::UI_FLAGS, 0);
								ps->SetShaderResourceView("background_texture", nullptr);
								ps->CopyAllBufferData();
							}
						}
					}
				}

				virtual void ShowText(bool show) {
					show_text = show;
				}

				virtual void Refresh() override {
					Widget::Refresh();
					Release();
					text_dirty = true;
					if (d3d_render_target != nullptr &&
						width > 0.0f && height > 0.0f &&
						font_size > 0.0f && !font_name.empty() && !text.empty()) {
						Direct2D* d2d = Direct2D::Get();
						text_format = d2d->GetFont(font_name, font_size, weight, style, align, valign);
						RefreshTextLayout();
						if (text_layout != nullptr) {
							origin.x = ((1.0f + position.x + margin) * 0.5f) * DXCore::Get()->GetScreenWidth();
							origin.y = DXCore::Get()->GetScreenHeight() * (1.0f - (1.0f + position.y) * 0.5f) - text_layout->GetMaxHeight();
						}
					}
				}

				virtual std::string GetText() {
					std::string final_text = text;
					if (!parameters.empty()) {
						char temp_txt[1024];
						switch (parameters.size()) {
						case 1: { snprintf(temp_txt, 1024, text.c_str(), get_value(parameters[0])); }break;
						case 2: { snprintf(temp_txt, 1024, text.c_str(), get_value(parameters[0]), get_value(parameters[1])); }break;
						case 3: { snprintf(temp_txt, 1024, text.c_str(), get_value(parameters[0]), get_value(parameters[1]), get_value(parameters[2])); }break;
						case 4: { snprintf(temp_txt, 1024, text.c_str(), get_value(parameters[0]), get_value(parameters[1]), get_value(parameters[2]), get_value(parameters[3])); }break;
						case 5: default: { snprintf(temp_txt, 1024, text.c_str(), get_value(parameters[0]), get_value(parameters[1]), get_value(parameters[2]), get_value(parameters[3]), get_value(parameters[4])); }break;
						}
						final_text = temp_txt;
					}
					return final_text;
				}

				template<typename... Args>
				void SetDynamicText(const std::string& txt, Args&... args) {
					// Save the parameters in a vector
					parameters = { text_param(&args)... };
					text = txt;
					Refresh();
				}

				virtual void SetText(const char* format, ...) {
					va_list arg;
					char tmp_text[1024];
					va_start(arg, format);
					vsnprintf(tmp_text, 1024, format, arg);
					va_end(arg);
					text = tmp_text;
					Refresh();
				}

				virtual void SetText(const std::string& txt) {
					parameters.clear();
					if (text != txt) {
						text = txt;
						Refresh();
					}
				}

				virtual void SetTextColor(const float4& color) {
					text_color.r = color.x;
					text_color.g = color.y;
					text_color.b = color.z;
					text_color.a = color.w;
					Refresh();
				}

				virtual float4 GetTextColor(void) const {
					return float4{ text_color.r,  text_color.g,  text_color.b, text_color.a };
				}

				virtual void SetRenderTarget(GfxRTV* target) override {
									if (target != d3d_render_target) {
										Widget::SetRenderTarget(target);
										Refresh();
									}
								}
			};
		}
	}
}
