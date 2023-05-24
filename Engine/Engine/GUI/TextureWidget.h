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

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;
using namespace DirectX;
using namespace DirectX::SimpleMath;

namespace HotBite {
	namespace Engine {
		namespace UI {

			class TextureWidget : public Widget {
			public:
				static inline uint64_t TEXTURE_WIDGET_ID = GetTypeId<TextureWidget>();

			private:
				ID3D11ShaderResourceView* texture = nullptr;
				bool invert_uv_x = false;
				bool invert_uv_y = false;
			public:

				virtual uint64_t GetType() override {
					return TEXTURE_WIDGET_ID;
				}

				TextureWidget(ECS::Coordinator* c, const std::string& name, bool invert_x = false, bool invert_y = false) :
					Widget(c, name), invert_uv_x(invert_x), invert_uv_y(invert_y) {
					Refresh();
				}

				virtual ~TextureWidget() {
				}

				virtual void Render(SimplePixelShader* ps) override {
					Widget::Render(ps);
					if (texture != nullptr) {
						int ui_flags = WIDGET_FLAG;
						ui_flags |= BACKGROUND_IMAGE_FLAG;
						if (invert_uv_x) { ui_flags |= WIDGET_INVERT_UV_X; }
						if (invert_uv_y) { ui_flags |= WIDGET_INVERT_UV_Y; }
						ps->SetShaderResourceView("background_texture", texture);
						ps->SetInt(SimpleShaderKeys::UI_FLAGS, ui_flags);
						ps->CopyAllBufferData();
						sprite->Begin();
						sprite->DrawQuad(v[0], v[1], v[2], v[3]);
						sprite->End();
						ps->SetInt(SimpleShaderKeys::UI_FLAGS, 0);
						ps->SetShaderResourceView("background_texture", nullptr);
						ps->CopyAllBufferData();
					}
				}

				void SetTexture(ID3D11ShaderResourceView* t) {
					texture = t;
				}
			};
		}
	}
}
