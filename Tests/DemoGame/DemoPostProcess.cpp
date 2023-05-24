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

