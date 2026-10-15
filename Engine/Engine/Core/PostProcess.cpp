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

ScreenDraw::ScreenDraw(GfxContext* context) {
	(void)context;
	GfxDevice* device = DXCore::Get()->device;
	const uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
	device->CreateBuffer(GfxBufferKind::Static, sizeof(indices), sizeof(uint16_t), D3D12_RESOURCE_FLAG_NONE,
		indices, GFX_STATE_BUFFER_READ, &index_buffer);
	device->CreateBuffer(GfxBufferKind::Dynamic, sizeof(VertexType) * 4, sizeof(VertexType), D3D12_RESOURCE_FLAG_NONE,
		nullptr, D3D12_RESOURCE_STATE_GENERIC_READ, &vertex_buffer);
};

ScreenDraw::~ScreenDraw() {
	if (vertex_buffer) { vertex_buffer->Release(); }
	if (index_buffer) { index_buffer->Release(); }
}

void ScreenDraw::Init(GfxContext* context) {
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
	GfxContext* context = DXCore::Get()->context;
	const VertexType quad[4] = { v1, v2, v3, v4 };
	context->UpdateBuffer(vertex_buffer, quad, sizeof(quad));
	const UINT stride = sizeof(VertexType);
	const UINT offset = 0;
	context->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);
	context->IASetIndexBuffer(index_buffer, DXGI_FORMAT_R16_UINT, 0);
	context->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->DrawIndexed(6, 0, 0);
}

GfxRTV* PostProcess::TargetRenderView() {
	GfxRTV* ret = nullptr;
	if (target_render != nullptr) {
		ret = target_render->RenderTarget();
	}
	return ret;
}

GfxDSV* PostProcess::TargetDepthView() {
	GfxDSV* ret = nullptr;
	if (target_depth != nullptr) {
		ret = target_depth->DepthView();
	}
	return ret;
}

void PostProcess::Prepare() {
	GfxRTV* rv[1] = { TargetRenderView() };
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
	GfxRTV* rv[1] = { nullptr };
	context->OMSetRenderTargets(1, rv, nullptr);
	ps->SetShaderResourceView("renderTexture", nullptr);
	for (auto srv : srvs) {
		ps->SetShaderResourceView(srv.first, nullptr);
	}
}

void PostProcess::Render() {
	ScreenDraw::Get()->Draw();
}

GfxSRV* PostProcess::RenderResource() {
	return nullptr;
}

GfxUAV* PostProcess::RenderUAV() {
	return nullptr;
}

GfxRTV* PostProcess::RenderTarget() const {
	return nullptr;
}

GfxSRV* PostProcess::DepthResource() {
	return nullptr;
}

GfxDSV* PostProcess::DepthView() {
	return nullptr;
}

PostProcess::PostProcess(GfxContext* dxcontext,
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

void PostProcess::SetShaderResourceView(const std::string& name, GfxSRV* srv) {
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

GenericPostProcess::GenericPostProcess(GfxContext* dxcontext,
	int width, int height, ECS::Coordinator* c, const std::string& vs, const std::string& ps) :
	PostProcess(dxcontext,
		ShaderFactory::Get()->GetShader<SimpleVertexShader>(vs), //This is the default engine post-process vertex shader
		ShaderFactory::Get()->GetShader<SimplePixelShader>(ps)), //This is the custom post-process pixel shader of the demo
	coordinator(c)
{
	text.Init(width, height, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, GFX_BIND_UNORDERED_ACCESS);
}

GenericPostProcess::~GenericPostProcess() {
	text.Release();
}

void GenericPostProcess::ClearData(const float color[4]) {
	context->ClearRenderTargetView(text.RenderTarget(), color);
}

GfxSRV* GenericPostProcess::RenderResource() {
	return text.SRV();
}

GfxRTV* GenericPostProcess::RenderTarget() const {
	return text.RenderTarget();
}

GfxUAV* GenericPostProcess::RenderUAV() {
	return text.UAV();
}


void MainEffect::ClearData(const float color[4]) {
	context->ClearRenderTargetView(text.RenderTarget(), color);
	context->ClearDepthStencilView(
		depth.Depth(),
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f,
		0);
}

MainEffect::MainEffect(GfxContext* dxcontext,
	int width, int height) :
	PostProcess(dxcontext,
		ShaderFactory::Get()->GetShader<SimpleVertexShader>("PostMainVS.cso"),
		ShaderFactory::Get()->GetShader<SimplePixelShader>("PostMainPS.cso"))
{
	//x2 scale to avoid resolution artifacts in motion blur
	text.Init(width, height, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, GFX_BIND_UNORDERED_ACCESS);
	depth.Init(width, height);
}

MainEffect::~MainEffect() {
	text.Release();
	depth.Release();
}

GfxSRV* MainEffect::RenderResource() {
	return text.SRV();
}

GfxUAV* MainEffect::RenderUAV() {
	return text.UAV();
}

GfxRTV* MainEffect::RenderTarget() const {
	return text.RenderTarget();
}

GfxSRV* MainEffect::DepthResource() {
	return depth.SRV();
}

GfxDSV* MainEffect::DepthView() {
	return depth.Depth();
}

void MotionBlurEffect::ClearData(const float color[4]) {
	context->ClearRenderTargetView(RenderTarget(), color);
}

GfxSRV* MotionBlurEffect::RenderResource() {
	return text.SRV();
}

GfxRTV* MotionBlurEffect::RenderTarget() const {
	return text.RenderTarget();
}

MotionBlurEffect::MotionBlurEffect(GfxContext* dxcontext,
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