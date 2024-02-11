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

#include "PostProcess.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;

ScreenDraw* ScreenDraw::instance;

ScreenDraw::ScreenDraw(ID3D11DeviceContext* context) {
	batch = new DirectX::PrimitiveBatch<VertexType>(context);
};

ScreenDraw::~ScreenDraw() {
	delete batch;
}

void ScreenDraw::Init(ID3D11DeviceContext* context) {
	if (instance == nullptr) {
		instance = new ScreenDraw(context);
	}
}

void ScreenDraw::Release() {
	if (instance != nullptr) {
		delete instance;
		instance = nullptr;
	}
}

ScreenDraw* ScreenDraw::Get() {
	return instance;
}

void ScreenDraw::Draw() {
	batch->Begin();
	batch->DrawQuad(v1, v2, v3, v4);
	batch->End();
}

ID3D11RenderTargetView* PostProcess::TargetRenderView() {
	ID3D11RenderTargetView* ret = nullptr;
	if (target_render != nullptr) {
		ret = target_render->RenderTarget();
	}
	return ret;
}

ID3D11DepthStencilView* PostProcess::TargetDepthView() {
	ID3D11DepthStencilView* ret = nullptr;
	if (target_depth != nullptr) {
		ret = target_depth->DepthView();
	}
	return ret;
}

void PostProcess::Prepare() {
	ID3D11RenderTargetView* rv[1] = { TargetRenderView() };
	context->OMSetRenderTargets(1, rv, TargetDepthView());
	vs->SetShader();
	ps->SetShader();
	ps->SetShaderResourceView("renderTexture", RenderResource());
	ps->SetSamplerState("basicSampler", DXCore::Get()->basic_sampler);
	ps->SetInt("screenW", w);
	ps->SetInt("screenH", h);
	ps->SetFloat("time", (float)Scheduler::Get()->GetElapsedNanoSeconds() / 1000000000.0f);
	if (TargetRenderView() == DXCore::Get()->RenderTarget()) {
		context->RSSetViewports(1, &DXCore::Get()->screenport);
	}
	for (const auto& srv : srvs) {
		ps->SetShaderResourceView(srv.first, srv.second);
	}
	for (const auto& v : vars) {
		ps->SetData(v.first, v.second.data(), (uint8_t)v.second.size());
	}
}

void PostProcess::UnPrepare() {
	ID3D11RenderTargetView* rv[1] = { nullptr };
	context->OMSetRenderTargets(1, rv, nullptr);
	ps->SetShaderResourceView("renderTexture", nullptr);
	for (auto srv : srvs) {
		ps->SetShaderResourceView(srv.first, nullptr);
	}
}

void PostProcess::Render() {
	ScreenDraw::Get()->Draw();
}

ID3D11ShaderResourceView* PostProcess::RenderResource() {
	return nullptr;
}

ID3D11UnorderedAccessView* PostProcess::RenderUAV() {
	return nullptr;
}

ID3D11RenderTargetView* PostProcess::RenderTarget() const {
	return nullptr;
}

ID3D11ShaderResourceView* PostProcess::DepthResource() {
	return nullptr;
}

ID3D11DepthStencilView* PostProcess::DepthView() {
	return nullptr;
}

PostProcess::PostProcess(ID3D11DeviceContext* dxcontext,
	SimpleVertexShader* vertex_shader,
	SimplePixelShader* pixel_shader) :
	context(dxcontext), vs(vertex_shader), ps(pixel_shader) {
}

void PostProcess::Clear(const float color[4]) {
	this->ClearData(color);
	if (next != nullptr) {
		next->Clear(color);
	}
}

void PostProcess::Process() {
	Prepare();
	vs->CopyAllBufferData();
	ps->CopyAllBufferData();
	Render();
	UnPrepare();
	vs->CopyAllBufferData();
	ps->CopyAllBufferData();
	if (next != nullptr) {
		next->Process();
	}
}

void PostProcess::SetTarget(IRenderTarget* t, IDepthResource* d) {
	this->target_render = t;
	this->target_depth = d;

	if (TargetRenderView() == DXCore::Get()->RenderTarget()) {
		w = (int)DXCore::Get()->screenport.Width;
		h = (int)DXCore::Get()->screenport.Height;
	}
	else {
		w = (int)DXCore::Get()->viewport.Width;
		h = (int)DXCore::Get()->viewport.Height;
	}
}

void PostProcess::SetShaderResourceView(const std::string& name, ID3D11ShaderResourceView* srv) {
	srvs[name] = srv;
	if (next != nullptr) {
		next->SetShaderResourceView(name, srv);
	}
}

void PostProcess::SetVariable(const std::string& name, const void* value, size_t size) {
	std::vector<uint8_t>& data = vars[name];
	data.clear();
	data.resize(size);
	for (int i = 0; i < size; ++i) {
		data[i] = ((uint8_t*)value)[i];
	}
}

void PostProcess::SetNext(PostProcess* next) {
	this->next = next;
	SetTarget(next, next);
}

void PostProcess::SetView(const Components::Camera& camera) {
	if (next != nullptr) {
		next->SetView(camera);
	}
}

PostProcess* PostProcess::GetNext() const {
	return next;
}

GenericPostProcess::GenericPostProcess(ID3D11DeviceContext* dxcontext,
	int width, int height, ECS::Coordinator* c, const std::string& vs, const std::string& ps) :
	PostProcess(dxcontext,
		ShaderFactory::Get()->GetShader<SimpleVertexShader>(vs), //This is the default engine post-process vertex shader
		ShaderFactory::Get()->GetShader<SimplePixelShader>(ps)), //This is the custom post-process pixel shader of the demo
	coordinator(c)
{
	text.Init(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT);
}

GenericPostProcess::~GenericPostProcess() {
	text.Release();
}

void GenericPostProcess::ClearData(const float color[4]) {
	context->ClearRenderTargetView(text.RenderTarget(), color);
}

ID3D11ShaderResourceView* GenericPostProcess::RenderResource() {
	return text.SRV();
}

ID3D11RenderTargetView* GenericPostProcess::RenderTarget() const {
	return text.RenderTarget();
}



void MainEffect::ClearData(const float color[4]) {
	context->ClearRenderTargetView(text.RenderTarget(), color);
	context->ClearDepthStencilView(
		depth.Depth(),
		D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
		1.0f,
		0);
}

MainEffect::MainEffect(ID3D11DeviceContext* dxcontext,
	int width, int height) :
	PostProcess(dxcontext,
		ShaderFactory::Get()->GetShader<SimpleVertexShader>("PostMainVS.cso"),
		ShaderFactory::Get()->GetShader<SimplePixelShader>("PostMainPS.cso"))
{
	//x2 scale to avoid resolution artifacts in motion blur
	text.Init(width, height, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS);
	depth.Init(width, height);
}

MainEffect::~MainEffect() {
	text.Release();
	depth.Release();
}

ID3D11ShaderResourceView* MainEffect::RenderResource() {
	return text.SRV();
}

ID3D11UnorderedAccessView* MainEffect::RenderUAV() {
	return text.UAV();
}

ID3D11RenderTargetView* MainEffect::RenderTarget() const {
	return text.RenderTarget();
}

ID3D11ShaderResourceView* MainEffect::DepthResource() {
	return depth.SRV();
}

ID3D11DepthStencilView* MainEffect::DepthView() {
	return depth.Depth();
}

void MotionBlurEffect::ClearData(const float color[4]) {
	context->ClearRenderTargetView(RenderTarget(), color);
}

ID3D11ShaderResourceView* MotionBlurEffect::RenderResource() {
	return text.SRV();
}

ID3D11RenderTargetView* MotionBlurEffect::RenderTarget() const {
	return text.RenderTarget();
}

MotionBlurEffect::MotionBlurEffect(ID3D11DeviceContext* dxcontext,
	int width, int height) :
	PostProcess(dxcontext,
		ShaderFactory::Get()->GetShader<SimpleVertexShader>("PostMainVS.cso"),
		ShaderFactory::Get()->GetShader<SimplePixelShader>("PostMotionBlurPS.cso")) {
	text.Init(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT);
	matrix id = DirectX::XMMatrixIdentity();
	XMStoreFloat4x4(&prev_view, id);
	XMStoreFloat4x4(&current_view, id);
}

MotionBlurEffect::~MotionBlurEffect() {
}

void MotionBlurEffect::Prepare() {
	PostProcess::Prepare();

	ps->SetMatrix4x4("view_proj", current_view);
	ps->SetMatrix4x4("prev_view_proj", prev_view);
}