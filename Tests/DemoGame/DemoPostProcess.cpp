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

#include "DemoPostProcess.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Core;

PostGame::PostGame(ID3D11DeviceContext* dxcontext,
	int width, int height, ECS::Coordinator* c) :
	PostProcess(dxcontext,
		ShaderFactory::Get()->GetShader<SimpleVertexShader>("PostMainVS.cso"), //This is the default engine post-process vertex shader
		ShaderFactory::Get()->GetShader<SimplePixelShader>("DemoPostEffect.cso")), //This is the custom post-process pixel shader of the demo
	coordinator(c)
{
	text.Init(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT);
}

PostGame::~PostGame() {
	text.Release();
}

void PostGame::ClearData(const float color[4]) {
	context->ClearRenderTargetView(text.RenderTarget(), color);
}

ID3D11ShaderResourceView* PostGame::RenderResource() {
	return text.SRV();
}

ID3D11RenderTargetView* PostGame::RenderTarget() const {
	return text.RenderTarget();
}

