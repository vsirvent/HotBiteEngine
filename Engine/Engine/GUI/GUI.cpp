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

#include "GUI.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Core;

namespace HotBite {
	namespace Engine {
		namespace UI {

			
			void GUI::ClearData(const float color[4]) {
				context->ClearRenderTargetView(text.RenderTarget(), color);
			}

			GUI::GUI(ID3D11DeviceContext* dxcontext,
				int width, int height, ECS::Coordinator* c) :
				PostProcess(dxcontext,
					ShaderFactory::Get()->GetShader<SimpleVertexShader>("GUIVS.cso"),
					ShaderFactory::Get()->GetShader<SimplePixelShader>("GUIPS.cso")),
				coordinator(c)
			{
				text.Init(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT);
			}

			GUI::~GUI() {
				text.Release();
			}

			ID3D11ShaderResourceView* GUI::RenderResource() {
				return text.SRV();
			}

			ID3D11RenderTargetView* GUI::RenderTarget() const {
				return text.RenderTarget();
			}

			void GUI::Prepare() {
				PostProcess::Prepare();
				ps->SetInt(SimpleShaderKeys::VERTEX_COLOR, 0);
				ps->SetInt(SimpleShaderKeys::UI_FLAGS, 0);
			}

			void GUI::UnPrepare() {
				PostProcess::UnPrepare();
			}
		}
	}
}