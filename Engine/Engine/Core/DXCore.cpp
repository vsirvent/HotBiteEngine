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

#include "DXCore.h"
#include "Log.h"
#include "Scheduler.h"
#include "Texture.h"
#include "PostProcess.h"
#include "Vertex.h"

#include <WindowsX.h>
#include <sstream>
#include <DirectXTex.h>
#include <algorithm>
#include <chrono>

using namespace HotBite::Engine::Core;
using namespace HotBite::Engine::ECS;

DXCore* DXCore::DXCoreInstance = nullptr;
Direct2D* Direct2D::instance = nullptr;


LRESULT DXCore::WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	return DXCoreInstance->ProcessMessage(hWnd, uMsg, wParam, lParam);
}

DXCore::DXCore(
	HINSTANCE hInstance,
	const char* titleBarText,
	unsigned int windowWidth,
	unsigned int windowHeight,
	bool debugTitleBarStats,
	bool window)
{
	CoInitialize(NULL);
	RECT rect;
	GetClientRect(GetDesktopWindow(), &rect);
	screen_dimensions.x = (float)(rect.right - rect.left);
	screen_dimensions.y = (float)(rect.bottom - rect.top);
	if (windowWidth == 0 || windowHeight == 0) {
		windowWidth = (uint32_t)screen_dimensions.x;
		windowHeight = (uint32_t)screen_dimensions.y;
	}
	DXCoreInstance = this;
	Scheduler::Init(NTHREADS);
	this->windowed = window;
	this->hInstance = hInstance;
	this->titleBarText = titleBarText;
	this->width = windowWidth;
	this->height = windowHeight;
	this->titleBarStats = debugTitleBarStats;

	fpsFrameCount = 0;
	fpsTimeElapsed = 0.0f;

	device = nullptr;
	context = nullptr;
	swapChain = nullptr;
	backBufferRTV = nullptr;
	depthTexture = new DepthTexture2D();
	dpi = GetDpiForSystem();
}

DXCore::~DXCore()
{
	Stop();

	ShaderFactory::Release();
	ScreenDraw::Release();

	if (context != nullptr) {
		//Let the GPU finish before anything it may still be reading goes away.
		context->OMSetRenderTargets(0, nullptr, nullptr);
		context->Flush();
	}
	if (device != nullptr) {
		device->WaitIdle();
	}

	if (depthTexture) { delete depthTexture; }

	// Release all GPU objects
	if (basic_sampler) { basic_sampler->Release(); }
	if (shadow_sampler) { shadow_sampler->Release(); }
	if (blend) { blend->Release(); }
	if (no_blend) { no_blend->Release(); }
	if (shadow_rasterizer) { shadow_rasterizer->Release(); }
	if (dir_shadow_rasterizer) { dir_shadow_rasterizer->Release(); }
	if (wireframe_rasterizer) { wireframe_rasterizer->Release(); }
	if (drawing_rasterizer) { drawing_rasterizer->Release(); }
	if (depth_rasterizer) { depth_rasterizer->Release(); }
	if (sky_rasterizer) { sky_rasterizer->Release(); }
	if (normal_depth) { normal_depth->Release(); }
	if (transparent_depth) { transparent_depth->Release(); }
	if (depth_prepass) { depth_prepass->Release(); }
	if (depth_prepass_read) { depth_prepass_read->Release(); }
	if (backBufferRTV) { backBufferRTV->Release(); }
	if (backBufferTexture) { backBufferTexture->Release(); }
	for (GfxTexture*& t : swapBuffers) {
		if (t) { t->Release(); t = nullptr; }
	}
	swapChain.Reset();

	if (context) {
		context->Shutdown();
		delete context;
		context = nullptr;
	}
	if (device) {
		//Whatever the debug layer still has to say about leaks and invalid use.
		device->DrainDebugMessages();
		device->Shutdown();
		delete device;
		device = nullptr;
	}
}

HRESULT DXCore::InitWindow(HWND parent)
{
	wnd_update = (parent == NULL);
	if (parent != NULL) {
		wnd = parent;
		windowed = true;
		SetWindowLongPtr(wnd, GWLP_WNDPROC, (LONG_PTR)DXCore::WindowProc);
	}
	else {
		WNDCLASS wndClass = {};
		wndClass.style = CS_HREDRAW | CS_VREDRAW;
		wndClass.lpfnWndProc = DXCore::WindowProc;
		wndClass.cbClsExtra = 0;
		wndClass.cbWndExtra = 0;
		wndClass.hInstance = hInstance;
		wndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
		wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
		wndClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
		wndClass.lpszMenuName = NULL;
		wndClass.lpszClassName = "Direct3DWindowClass";

		if (!RegisterClass(&wndClass))
		{
			DWORD error = GetLastError();
			if (error != ERROR_CLASS_ALREADY_EXISTS)
				return HRESULT_FROM_WIN32(error);
		}

		RECT clientRect;
		SetRect(&clientRect, 0, 0, width, height);
		AdjustWindowRect(&clientRect, WS_OVERLAPPEDWINDOW, false);

		// Center the window to the screen
		RECT desktopRect;
		GetClientRect(GetDesktopWindow(), &desktopRect);
		int centeredX = (desktopRect.right / 2) - (clientRect.right / 2);
		int centeredY = (desktopRect.bottom / 2) - (clientRect.bottom / 2);

		wnd = CreateWindow(wndClass.lpszClassName, titleBarText.c_str(), WS_OVERLAPPEDWINDOW,
			centeredX, centeredY, clientRect.right - clientRect.left,
			clientRect.bottom - clientRect.top,
			0,			// No parent window
			0,			// No menu
			hInstance,	// The app's handle
			0);			// No other windows in our application

		if (wnd == NULL)
		{
			DWORD error = GetLastError();
			return HRESULT_FROM_WIN32(error);
		}
		LONG style = GetWindowLong(wnd, GWL_STYLE);
		style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU);
		if (windowed) {
			//WS_SYSMENU plus the two box styles are what actually draw the minimize and
			//maximize buttons in the title bar; without them a window that is already
			//resizable (WS_THICKFRAME) still offers no way to minimize or maximize it,
			//and has no system menu either.
			style |= WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | CS_DBLCLKS;
		}
		SetWindowLong(wnd, GWL_STYLE, style);

		LONG_PTR class_style = GetClassLongPtr(wnd, GCL_STYLE) | CS_DBLCLKS;
		SetClassLongPtr(wnd, GCL_STYLE, class_style);

		if (windowed && start_maximized) {
			//The window was created centered on the primary monitor, so maximizing
			//fills that monitor's work area (staying clear of the taskbar).
			ShowWindow(wnd, SW_SHOWMAXIMIZED);

			//Adopt the maximized client area as the render size. There is no WM_SIZE
			//handling in this engine - the swap chain is created once, from these
			//members, by InitDirectX - so they have to be correct BEFORE that runs or
			//the back buffer would keep the requested size and be stretched across a
			//larger window, taking the UI's hit-testing out of step with what is drawn.
			RECT clientArea;
			GetClientRect(wnd, &clientArea);
			if (clientArea.right > 0 && clientArea.bottom > 0) {
				width = (unsigned int)clientArea.right;
				height = (unsigned int)clientArea.bottom;
			}
		}
		else if (windowed) {
			ShowWindow(wnd, SW_SHOW);
			// Get the width and height of the screen
			int screenWidth = GetSystemMetrics(SM_CXSCREEN);
			int screenHeight = GetSystemMetrics(SM_CYSCREEN);

			// Get the width and height of the window
			RECT rect;
			GetWindowRect(wnd, &rect);
			int windowWidth = rect.right - rect.left;
			int windowHeight = rect.bottom - rect.top;

			// Calculate the left and top positions for the window
			int left = (screenWidth - windowWidth) / 2;
			int top = (screenHeight - windowHeight) / 2;

			// Set the position of the window
			SetWindowPos(wnd, HWND_TOP, left, top, windowWidth, windowHeight, SWP_SHOWWINDOW);
		}
		else {
			ShowWindow(wnd, SW_MAXIMIZE);
		}
	}

	RECT rect;
	GetClientRect(wnd, &rect);
	screen_dimensions.x = (float)(rect.right - rect.left);
	screen_dimensions.y = (float)(rect.bottom - rect.top);
	
	return S_OK;
}

std::vector<IDXGIAdapter*>
DXCore::EnumerateAdapters()
{
	IDXGIAdapter* pAdapter;
	std::vector <IDXGIAdapter*> vAdapters;
	IDXGIFactory* pFactory = NULL;

	// Create a DXGIFactory object.
	if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory)))
	{
		return vAdapters;
	}


	for (UINT i = 0;
		pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND;
		++i)
	{
		vAdapters.push_back(pAdapter);
	}

	if (pFactory)
	{
		pFactory->Release();
	}
	return vAdapters;
}

HRESULT DXCore::InitDirectX()
{
	//HOTBITE_LOG=<file>: an engine log (debug level) for a host that does not open one itself, e.g. the gfx frame stats.
	char log_path[MAX_PATH] = {};
	size_t log_len = 0;
	if (getenv_s(&log_len, log_path, sizeof(log_path), "HOTBITE_LOG") == 0 && log_len > 1 && !Log::Enabled(LogLevel::Fatal)) {
		Log::Init(log_path, LogLevel::Debug);
	}
	int w = width;
	int h = height;
	if (!windowed) {
		RECT rect;
		GetClientRect(GetDesktopWindow(), &rect);
		w = rect.right - rect.left;
		h = rect.bottom - rect.top;
	}
	else if (wnd != NULL) {
		//The real client area can differ from the requested size (frame metrics,
		//SW_MAXIMIZE, DPI). The swapchain must match it exactly, otherwise DXGI
		//stretches the backbuffer on present and every screen-space mapping
		//(mouse picking, UI overlays) ends up offset.
		RECT rect;
		if (GetClientRect(wnd, &rect) && rect.right > rect.left && rect.bottom > rect.top) {
			w = rect.right - rect.left;
			h = rect.bottom - rect.top;
		}
	}
	//Keep the logical dimensions in sync with the actual backbuffer so viewports
	//and camera aspect ratios agree with what is presented.
	width = w;
	height = h;

	HRESULT hr = S_OK;

	//The D3D12 device on the adapter with the most dedicated memory. A machine without feature level
	//12_0 and resource binding tier 2 ends here: there is no fallback (see Docs/D3D12_MIGRATION.md).
	device = new GfxDevice();
	hr = device->Init();
	if (FAILED(hr)) {
		LOG_ERROR("DXCore: no usable Direct3D 12 device (0x%08X)", (unsigned)hr);
		return hr;
	}
	context = new GfxContext();
	if (!context->Init(device)) {
		LOG_ERROR("DXCore: could not create the graphics context");
		return E_FAIL;
	}
	dxFeatureLevel = device->FeatureLevel();
	LOG_INFO("DXCore: D3D feature level %u.%u on %s, 64 compute UAV slots",
		(unsigned)(dxFeatureLevel >> 12), (unsigned)((dxFeatureLevel >> 8) & 0xF), device->AdapterName().c_str());

	//The swap chain: flip model, several buffers, tearing allowed when the system can (the frame rate is
	//capped by the game, not by the display).
	{
		DXGI_SWAP_CHAIN_DESC1 sd = {};
		sd.Width = w;
		sd.Height = h;
		sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.Stereo = FALSE;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.BufferCount = SWAP_BUFFERS;
		sd.Scaling = DXGI_SCALING_STRETCH;
		sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		sd.Flags = device->AllowTearing() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
		Microsoft::WRL::ComPtr<IDXGISwapChain1> sc1;
		hr = device->Factory()->CreateSwapChainForHwnd(device->Queue(), wnd, &sd, nullptr, nullptr, &sc1);
		if (FAILED(hr)) {
			LOG_ERROR("DXCore: CreateSwapChainForHwnd failed (0x%08X)", (unsigned)hr);
			return hr;
		}
		hr = sc1.As(&swapChain);
		if (FAILED(hr)) return hr;
		//Alt+Enter is not a fullscreen switch here: the "fullscreen" game is a borderless window.
		device->Factory()->MakeWindowAssociation(wnd, DXGI_MWA_NO_ALT_ENTER);
		for (UINT i = 0; i < SWAP_BUFFERS; ++i) {
			Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
			hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&buffer));
			if (FAILED(hr)) return hr;
			hr = device->WrapTexture(buffer.Get(), D3D12_RESOURCE_STATE_PRESENT, &swapBuffers[i]);
			if (FAILED(hr)) return hr;
		}
	}

	{
		D3D12_RESOURCE_DESC bb = {};
		bb.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		bb.Width = w;
		bb.Height = h;
		bb.DepthOrArraySize = 1;
		bb.MipLevels = 1;
		bb.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		bb.SampleDesc.Count = 1;
		bb.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		bb.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		D3D12_CLEAR_VALUE cv = {};
		cv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		hr = device->CreateTexture2D(bb, D3D12_RESOURCE_STATE_RENDER_TARGET, &cv, nullptr, 0, &backBufferTexture);
		if (FAILED(hr)) return hr;
		hr = device->CreateRTV(backBufferTexture, nullptr, &backBufferRTV);
		if (FAILED(hr)) return hr;
	}

	context->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ScreenDraw::Init(context);

	hr = depthTexture->Init(w, h);
	if (FAILED(hr)) return hr;

	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.Width = (float)width;
	viewport.Height = (float)height;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	half_viewport.TopLeftX = 0;
	half_viewport.TopLeftY = 0;
	half_viewport.Width = (float)width / 2;
	half_viewport.Height = (float)height / 2;
	half_viewport.MinDepth = 0.0f;
	half_viewport.MaxDepth = 1.0f;

	screenport.TopLeftX = 0;
	screenport.TopLeftY = 0;
	screenport.Width = (float)w;
	screenport.Height = (float)h;
	screenport.MinDepth = 0.0f;
	screenport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &screenport);

	D3D12_RASTERIZER_DESC drawingRenderStateDesc = {};
	drawingRenderStateDesc.CullMode = D3D12_CULL_MODE_NONE;
	drawingRenderStateDesc.FillMode = D3D12_FILL_MODE_SOLID;
	drawingRenderStateDesc.AntialiasedLineEnable = TRUE;
	drawingRenderStateDesc.DepthClipEnable = TRUE;
	if (FAILED(hr = device->CreateRasterizerState(drawingRenderStateDesc, false, &drawing_rasterizer))) {
		return hr;
	}

	//The depth pre-pass draws the same geometry as the main pass but through a shorter
	//path: DepthVS multiplies by world*view*projection while the main pass reaches
	//clip space via the domain and geometry shaders, and tessellation re-interpolates
	//the position on the way. The results differ in the last bits, and a surface that
	//comes out a hair further away in the main pass would be rejected by its own
	//pre-pass depth, speckling the whole scene. Biasing the pre-pass away from the
	//camera absorbs that. The units are relative to the depth format (D32_FLOAT), so
	//the slack is proportional to the z-buffer's own precision: fractions of a
	//millimetre up close, about a metre out near the far plane.
	//This replaces the flat 1.0f world-unit slack the old in-shader test used, which
	//was far too generous near the camera and could not reject anything early.
	D3D12_RASTERIZER_DESC depthRenderStateDesc = drawingRenderStateDesc;
	depthRenderStateDesc.DepthBias = 100;
	depthRenderStateDesc.SlopeScaledDepthBias = 1.0f;
	if (FAILED(hr = device->CreateRasterizerState(depthRenderStateDesc, false, &depth_rasterizer))) {
		return hr;
	}

	D3D12_RASTERIZER_DESC skyRenderStateDesc = {};
	skyRenderStateDesc.CullMode = D3D12_CULL_MODE_NONE;
	skyRenderStateDesc.FillMode = D3D12_FILL_MODE_SOLID;
	skyRenderStateDesc.AntialiasedLineEnable = FALSE;
	skyRenderStateDesc.DepthClipEnable = FALSE;
	if (FAILED(hr = device->CreateRasterizerState(skyRenderStateDesc, false, &sky_rasterizer))) {
		return hr;
	}

	D3D12_RASTERIZER_DESC wireFrameRenderStateDesc = {};
	wireFrameRenderStateDesc.CullMode = D3D12_CULL_MODE_BACK;
	wireFrameRenderStateDesc.FillMode = D3D12_FILL_MODE_WIREFRAME;
	wireFrameRenderStateDesc.AntialiasedLineEnable = TRUE;
	wireFrameRenderStateDesc.DepthClipEnable = TRUE;
	if (FAILED(hr = device->CreateRasterizerState(wireFrameRenderStateDesc, false, &wireframe_rasterizer))) {
		return hr;
	}

	D3D12_RASTERIZER_DESC shadowRenderStateDesc = {};
	shadowRenderStateDesc.CullMode = D3D12_CULL_MODE_BACK;
	shadowRenderStateDesc.FillMode = D3D12_FILL_MODE_SOLID;
	//shadowRenderStateDesc.DepthBias = 100;
	shadowRenderStateDesc.DepthClipEnable = FALSE;
	shadowRenderStateDesc.SlopeScaledDepthBias = 2.0f;
	if (FAILED(hr = device->CreateRasterizerState(shadowRenderStateDesc, false, &shadow_rasterizer))) {
		return hr;
	}
	shadowRenderStateDesc.CullMode = D3D12_CULL_MODE_BACK;
	shadowRenderStateDesc.SlopeScaledDepthBias = 2.0f;
	shadowRenderStateDesc.DepthBias = 1000;
	if (FAILED(hr = device->CreateRasterizerState(shadowRenderStateDesc, false, &dir_shadow_rasterizer))) {
		return hr;
	}

	D3D12_SAMPLER_DESC samplerDesc = {};
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.Filter = D3D12_FILTER_ANISOTROPIC;
	samplerDesc.MaxAnisotropy = 16;
	samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	device->CreateSampler(samplerDesc, &basic_sampler);

	D3D12_SAMPLER_DESC comparisonSamplerDesc = {};
	comparisonSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	comparisonSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	comparisonSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	comparisonSamplerDesc.MinLOD = 0.f;
	comparisonSamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	comparisonSamplerDesc.MipLODBias = 0.f;
	comparisonSamplerDesc.MaxAnisotropy = 0;
	comparisonSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS;
	comparisonSamplerDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
	if (FAILED(hr = device->CreateSampler(comparisonSamplerDesc, &shadow_sampler))) {
		return hr;
	}

	D3D12_BLEND_DESC BlendState = {};
	for (int i = 0; i < 8; ++i) {
		BlendState.RenderTarget[i].SrcBlend = D3D12_BLEND_ONE;
		BlendState.RenderTarget[i].DestBlend = D3D12_BLEND_ZERO;
		BlendState.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
		BlendState.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
		BlendState.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_ZERO;
		BlendState.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		BlendState.RenderTarget[i].LogicOp = D3D12_LOGIC_OP_NOOP;
		BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	}
	BlendState.RenderTarget[0].BlendEnable = FALSE;
	device->CreateBlendState(BlendState, &no_blend);

	BlendState.AlphaToCoverageEnable = FALSE;
	BlendState.IndependentBlendEnable = FALSE;
	BlendState.RenderTarget[0].BlendEnable = TRUE;
	BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
	BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	device->CreateBlendState(BlendState, &blend);

	D3D12_DEPTH_STENCIL_DESC DepthState = {};
	DepthState.DepthEnable = TRUE;
	DepthState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	DepthState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	DepthState.StencilEnable = FALSE;
	DepthState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
	DepthState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;

	const D3D12_DEPTH_STENCILOP_DESC defaultStencilOp =
	{ D3D12_STENCIL_OP_KEEP,
	  D3D12_STENCIL_OP_KEEP,
	  D3D12_STENCIL_OP_KEEP,
	  D3D12_COMPARISON_FUNC_ALWAYS };

	DepthState.FrontFace = defaultStencilOp;
	DepthState.BackFace = defaultStencilOp;
	device->CreateDepthStencilState(DepthState, &normal_depth);
	DepthState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	device->CreateDepthStencilState(DepthState, &transparent_depth);

	DepthState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	device->CreateDepthStencilState(DepthState, &depth_prepass_read);
	DepthState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	device->CreateDepthStencilState(DepthState, &depth_prepass);

	// Return the "everything is ok" HRESULT value
	Direct2D::Init();
	return S_OK;
}

DXCore* DXCore::Get() {
	return DXCoreInstance;
}

void DXCore::ClearScreen(const float color[4])
{
	context->ClearRenderTargetView(backBufferRTV, color);
	context->ClearDepthStencilView(depthTexture->Depth(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);
}

bool DXCore::ReadBackBuffer(std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h) {
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT rows = 0;
	UINT64 row_size = 0;
	UINT64 total = 0;
	device->Device()->GetCopyableFootprints(&backBufferTexture->desc, 0, 1, 0, &footprint, &rows, &row_size, &total);
	GfxBuffer* readback = nullptr;
	if (FAILED(device->CreateBuffer(GfxBufferKind::Readback, total, 0, D3D12_RESOURCE_FLAG_NONE, nullptr,
		D3D12_RESOURCE_STATE_COPY_DEST, &readback))) {
		return false;
	}
	context->CopyTextureToBuffer(backBufferTexture, readback, footprint);
	context->Flush();
	w = backBufferTexture->width;
	h = backBufferTexture->height;
	rgba.resize((size_t)w * h * 4);
	const uint8_t* src = (const uint8_t*)readback->mapped;
	for (uint32_t y = 0; y < h; ++y) {
		memcpy(rgba.data() + (size_t)y * w * 4, src + (size_t)y * footprint.Footprint.RowPitch, (size_t)w * 4);
	}
	readback->Release();
	return true;
}

bool DXCore::SaveBackBuffer(const std::filesystem::path& png, std::string& error) {
	//WIC needs COM on this thread; RPC_E_CHANGED_MODE just means it already was.
	static bool com_initialized = false;
	if (!com_initialized) {
		CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		com_initialized = true;
	}
	std::vector<uint8_t> pixels;
	uint32_t w = 0;
	uint32_t h = 0;
	if (!ReadBackBuffer(pixels, w, h)) {
		error = "reading the back buffer failed";
		return false;
	}
	DirectX::Image image = {};
	image.width = w;
	image.height = h;
	image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
	image.rowPitch = (size_t)w * 4;
	image.slicePitch = image.rowPitch * h;
	image.pixels = pixels.data();
	const HRESULT hr = DirectX::SaveToWICFile(image, DirectX::WIC_FLAGS_NONE,
		DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), png.wstring().c_str());
	if (FAILED(hr)) {
		error = "SaveToWICFile failed: " + png.string();
		return false;
	}
	return true;
}

GfxRTV* DXCore::RenderTarget() const {
	return backBufferRTV;
}

GfxSRV* DXCore::DepthResource() {
	return depthTexture->SRV();
}

GfxDSV* DXCore::DepthView() {
	return depthTexture->Depth();
}

void DXCore::Present()
{
	//What was drawn this frame goes to the swap chain's current buffer, which is then presented.
	const UINT index = swapChain->GetCurrentBackBufferIndex();
	GfxTexture* target = swapBuffers[index];
	context->CopyResource(target, backBufferTexture);
	context->Transition(target, D3D12_RESOURCE_STATE_PRESENT);
		//A line about the frame that is going out, now and then: how much was recorded and how much was refused.
		static uint32_t frames_presented = 0;
		static uint64_t submit_ns = 0;
		static uint64_t present_ns = 0;
		if ((frames_presented++ % 240) == 0) {
			const GfxContext::Stats& s = context->stats;
			LOG_DEBUG("gfx frame %u: %u draws, %u dispatches, %u skipped, %u barriers, %u pipelines made, %zu cached, %u descriptors copied",
				frames_presented, s.draws, s.dispatches, s.skipped, s.barriers, s.pipelines_created, context->PipelineCount(), s.descriptors_copied);
			const GfxDevice::WaitStats w = device->TakeWaitStats();
			const double frames = 240.0;
			LOG_DEBUG("gfx cpu per frame (ms, last 240): submit %.3f, present %.3f, waiting for the GPU %.3f (%u waits), immediate uploads waited %.3f (%u of %u calls)",
				submit_ns / 1e6 / frames, present_ns / 1e6 / frames, w.frame_ns / 1e6 / frames, w.frame_waits,
				w.immediate_ns / 1e6 / frames, w.immediate_waits, w.immediate_calls);
			submit_ns = 0;
			present_ns = 0;
		}
		const auto submit_begin = std::chrono::steady_clock::now();
			context->Submit();
			const auto present_begin = std::chrono::steady_clock::now();
			swapChain->Present(0, device->AllowTearing() ? DXGI_PRESENT_ALLOW_TEARING : 0);
			const auto present_end = std::chrono::steady_clock::now();
			submit_ns += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(present_begin - submit_begin).count();
			present_ns += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(present_end - present_begin).count();
	OnFrame();
}

HRESULT DXCore::Run()
{
	// Our overall game and message loop
	MSG msg = {};
	// Prepare timer timeouts
	Scheduler::Get(MAIN_THREAD)->RegisterTimer(SEC_TO_NSEC(1), [this](const Scheduler::TimerData&) {
		this->UpdateTitleBarStats();
		return true;
		});

	//SetThreadAffinityMask(GetCurrentThread(), 1);
	for (int i = 1; i < NTHREADS; ++i) {
		threads.emplace(std::thread([&e = this->end, i](){
			if (i == AUDIO_THREAD) {
				if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL)) {
					printf("DXCore::Run: Failed to set thread priority to real-time. Error code: %d\n", GetLastError());
				}
			}
			while (!e) {
				Scheduler::Get(i)->Update();
				Scheduler::Get(i)->Idle();
			}
		}));
		//DWORD_PTR dw = SetThreadAffinityMask(threads.front().native_handle(), DWORD_PTR(1) << (i));
		//if (dw == 0)
		//{
		//	DWORD dwErr = GetLastError();
		//	printf("SetThreadAffinityMask failed: %d\n", dwErr);
		//}
	}
	while (!end && msg.message != WM_QUIT && msg.message != WM_DESTROY)
	{
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (end) {
				return (HRESULT)msg.wParam;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		Scheduler::Get(0)->Update();
		Scheduler::Get(0)->Idle();
	}	
	return (HRESULT)msg.wParam;
}

void DXCore::Stop() {
	this->end = true;
	while (!threads.empty()) {
		threads.front().join();
		threads.pop();
	}
}

void DXCore::Quit()
{
	PostMessage(this->wnd, WM_CLOSE, NULL, NULL);
}

void DXCore::UpdateTitleBarStats()
{
	// How long did each frame take?  (Approx)
	float mspf = 1000.0f / (float)fpsFrameCount;

	// Quick and dirty title bar text (mostly for debugging)
	std::ostringstream output;
	output.precision(6);
	output << titleBarText <<
		"    Width: " << width <<
		"    Height: " << height <<
		"    FPS: " << fpsFrameCount <<
		"    Frame Time: " << mspf << "ms";
	current_fps = fpsFrameCount;
	// Append the version of DirectX the app is using
	output << "    D3D12 FL " << (unsigned)(dxFeatureLevel >> 12) << "_" << (unsigned)((dxFeatureLevel >> 8) & 0xF);

	// Actually update the title bar and reset fps data
	SetWindowText(wnd, output.str().c_str());
	fpsFrameCount = 0;
	fpsTimeElapsed += 1.0f;
}

void DXCore::CreateConsoleWindow(int bufferLines, int bufferColumns, int windowLines, int windowColumns)
{
	// Our temp console info struct
	CONSOLE_SCREEN_BUFFER_INFO coninfo;

	// Get the console info and set the number of lines
	AllocConsole();
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &coninfo);
	coninfo.dwSize.Y = bufferLines;
	coninfo.dwSize.X = bufferColumns;
	SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), coninfo.dwSize);

	SMALL_RECT rect;
	rect.Left = 0;
	rect.Top = 0;
	rect.Right = windowColumns;
	rect.Bottom = windowLines;
	SetConsoleWindowInfo(GetStdHandle(STD_OUTPUT_HANDLE), TRUE, &rect);

	FILE* stream;
	freopen_s(&stream, "CONIN$", "r", stdin);
	freopen_s(&stream, "CONOUT$", "w", stdout);
	freopen_s(&stream, "CONOUT$", "w", stderr);

	// Prevent accidental console window close
	HWND consoleHandle = GetConsoleWindow();
	HMENU hmenu = GetSystemMenu(consoleHandle, FALSE);
	EnableMenuItem(hmenu, SC_CLOSE, MF_GRAYED);
}

void DXCore::OnFrame() {
	fpsFrameCount++;
}

#define WINDOW_TO_GAME_X(x) ((std::clamp(x, 0, (int)(rect.right - rect.left))*width)/(rect.right - rect.left))
#define WINDOW_TO_GAME_Y(y) ((std::clamp(y, 0, (int)(rect.bottom - rect.top))*height)/(rect.bottom - rect.top))

LRESULT DXCore::ProcessMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	ForwardWindowMessage(hWnd, uMsg, wParam, lParam);
	if (end) {
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	if (OverlayCapturesInput(uMsg)) {
		return 0;
	}
	Coordinator* c = GetCoordinator();
	RECT rect;
	GetClientRect(wnd, &rect);

	// Check the incoming message and handle any we care about
	switch (uMsg)
	{
	case WM_SETFOCUS:
#if 0 //Disabled to allow better debug, re-enable to capture mouse
		printf("Window set focus\n");
		SetCapture(wnd);
		RECT wrect;
		GetWindowRect(wnd, &wrect);
		ClipCursor(&wrect);
#endif
		if (c != nullptr) {
			c->SendEvent(this, EVENT_ID_FOCUS);
		}
		return 0;
		// This is the message that signifies the window closing
	case WM_DESTROY:
		if (c != nullptr) {
			c->SendEvent(this, EVENT_ID_DETROY);
		}
		PostQuitMessage(0); // Send a quit message to our own program
		ExitProcess(0);
		return 0;

		// Prevent beeping when we "alt-enter" into fullscreen
	case WM_MENUCHAR:
		return MAKELRESULT(0, MNC_CLOSE);

	case WM_KEYDOWN:
		if (c != nullptr) {
			Event ev(this, EVENT_ID_KEY_DOWN);
			ev.SetParam<uint32_t>(PARAM_ID_KEY, (uint32_t)wParam);
			c->SendEvent(ev);
		}
		return 0;

	case WM_KEYUP:
		if (c != nullptr) {
			Event ev(this, EVENT_ID_KEY_UP);
			ev.SetParam<uint32_t>(PARAM_ID_KEY, (uint32_t)wParam);
			c->SendEvent(ev);
		}
		return 0;

		// Prevent the overall window from becoming too small
	case WM_GETMINMAXINFO:
		((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
		((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
		return 0;

		// Mouse button being pressed (while the cursor is currently over our window)
	case WM_LBUTTONDOWN: {
		if (c != nullptr) {
			Event ev(this, EVENT_ID_MOUSE_LDOWN);
			ev.SetParam<int>(PARAM_ID_X, WINDOW_TO_GAME_X(GET_X_LPARAM(lParam)));
			ev.SetParam<int>(PARAM_ID_Y, WINDOW_TO_GAME_Y(GET_Y_LPARAM(lParam)));
			ev.SetParam<int>(PARAM_ID_BUTTON, MK_LBUTTON);
			c->SendEvent(ev);
		}
		return 0;
	}
	case WM_LBUTTONDBLCLK: {
		if (c != nullptr) {
			Event ev(this, EVENT_ID_MOUSE_LDCLICK);
			ev.SetParam<int>(PARAM_ID_X, WINDOW_TO_GAME_X(GET_X_LPARAM(lParam)));
			ev.SetParam<int>(PARAM_ID_Y, WINDOW_TO_GAME_Y(GET_Y_LPARAM(lParam)));
			ev.SetParam<int>(PARAM_ID_BUTTON, MK_LBUTTON);
			c->SendEvent(ev);
		}
		return 0;
	}
	case WM_MBUTTONDOWN: {
		if (c != nullptr) {
			Event ev(this, EVENT_ID_MOUSE_MDOWN);
			ev.SetParam<int>(PARAM_ID_X, WINDOW_TO_GAME_X(GET_X_LPARAM(lParam)));
			ev.SetParam<int>(PARAM_ID_Y, WINDOW_TO_GAME_Y(GET_Y_LPARAM(lParam)));
			ev.SetParam<int>(PARAM_ID_BUTTON, MK_MBUTTON);
			c->SendEvent(ev);
		}
		return 0;
	}
	case WM_RBUTTONDOWN: {
		if (c != nullptr) {
			Event ev(this, EVENT_ID_MOUSE_RDOWN);
			ev.SetParam<int>(PARAM_ID_X, WINDOW_TO_GAME_X(GET_X_LPARAM(lParam)));
			ev.SetParam<int>(PARAM_ID_Y, WINDOW_TO_GAME_Y(GET_Y_LPARAM(lParam)));
			ev.SetParam<int>(PARAM_ID_BUTTON, MK_RBUTTON);
			c->SendEvent(ev);
		}
		return 0;
	}
	case WM_LBUTTONUP: {
		if (c != nullptr) {
			Event ev(this, EVENT_ID_MOUSE_LUP);
			ev.SetParam<int>(PARAM_ID_X, WINDOW_TO_GAME_X(GET_X_LPARAM(lParam)));
			ev.SetParam<int>(PARAM_ID_Y, WINDOW_TO_GAME_Y(GET_Y_LPARAM(lParam)));
			ev.SetParam<int>(PARAM_ID_BUTTON, MK_LBUTTON);
			c->SendEvent(ev);
		}
		return 0;
	}
	case WM_MBUTTONUP: {
		if (c != nullptr) {
			Event ev(this, EVENT_ID_MOUSE_MUP);
			ev.SetParam<int>(PARAM_ID_X, WINDOW_TO_GAME_X(GET_X_LPARAM(lParam)));
			ev.SetParam<int>(PARAM_ID_Y, WINDOW_TO_GAME_Y(GET_Y_LPARAM(lParam)));
			ev.SetParam<int>(PARAM_ID_BUTTON, MK_MBUTTON);
			c->SendEvent(ev);
		}
		return 0;
	}
	case WM_RBUTTONUP: {
		if (c != nullptr) {
			Event ev(this, EVENT_ID_MOUSE_RUP);
			ev.SetParam<int>(PARAM_ID_X, WINDOW_TO_GAME_X(GET_X_LPARAM(lParam)));
			ev.SetParam<int>(PARAM_ID_Y, WINDOW_TO_GAME_Y(GET_Y_LPARAM(lParam)));
			ev.SetParam<int>(PARAM_ID_BUTTON, MK_RBUTTON);
			c->SendEvent(ev);
		}
		return 0;
	}
	case WM_MOUSEMOVE: {
		if (c != nullptr) {
			Event ev(this, EVENT_ID_MOUSE_MOVE);
			Int2 pos = { (int)WINDOW_TO_GAME_X(GET_X_LPARAM(lParam)), (int)WINDOW_TO_GAME_Y(GET_Y_LPARAM(lParam)) };
			Int2 relative_pos = { 0, 0 };
			if (last_mouse_position.x >= 0) {
				relative_pos.x = pos.x - last_mouse_position.x;
			}
			if (last_mouse_position.y >= 0) {
				relative_pos.y = pos.y - last_mouse_position.y;
			}
			ev.SetParam<int>(PARAM_ID_X, pos.x);
			ev.SetParam<int>(PARAM_ID_Y, pos.y);
			ev.SetParam<int>(PARAM_RELATIVE_ID_X, relative_pos.x);
			ev.SetParam<int>(PARAM_RELATIVE_ID_Y, relative_pos.y);
			ev.SetParam<int>(PARAM_ID_BUTTON, LOWORD(wParam));
			last_mouse_position = pos;
			c->SendEvent(ev);
		}
		return 0;
	}
	case WM_MOUSEWHEEL: {
		if (c != nullptr) {
			Event ev(this, EVENT_ID_MOUSE_WHEEL);
			ev.SetParam<int>(PARAM_ID_X, WINDOW_TO_GAME_X(GET_X_LPARAM(lParam)));
			ev.SetParam<int>(PARAM_ID_Y, WINDOW_TO_GAME_Y(GET_Y_LPARAM(lParam)));
			ev.SetParam<int>(PARAM_ID_BUTTON, LOWORD(wParam));
			ev.SetParam<float>(PARAM_ID_WHEEL, GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA);
			c->SendEvent(ev);
		}
		return 0;
	}
	}
	// Let Windows handle any messages we're not touching
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}


Direct2D::Direct2D() {

	HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory);
	printf("2d2 factory init hr = %d => %p\n", hr, d2d_factory);
	hr = DWriteCreateFactory(
		DWRITE_FACTORY_TYPE_SHARED,
		__uuidof(write_factory),
		reinterpret_cast<IUnknown**>(&write_factory));
	printf("write_factory init hr = %d => %p\n", hr, write_factory);
}

Direct2D::~Direct2D() {
	d2d_factory->Release();
	write_factory->Release();
	if (wic_factory != nullptr) {
		wic_factory->Release();
	}
}


//UI text is UTF-8. Widening it byte by byte (the old std::wstring(begin, end)) went
//through a signed char, so every byte >= 0x80 became U+FFxx and any accented
//character rendered as a box. Bytes that are not valid UTF-8 keep the old one byte =
//one code point meaning (now unsigned), so Latin-1 text authored before this still
//shows as it did.
static std::wstring Utf8ToWide(const std::string& text) {
	if (text.empty()) {
		return std::wstring();
	}
	int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), nullptr, 0);
	if (n > 0) {
		std::wstring wide((size_t)n, L'\0');
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), wide.data(), n);
		return wide;
	}
	std::wstring wide;
	wide.reserve(text.size());
	for (char c : text) {
		wide.push_back((wchar_t)(unsigned char)c);
	}
	return wide;
}

IDWriteTextLayout* Direct2D::GetText(const std::string& text, IDWriteTextFormat* format, float width, float height) {
	IDWriteTextLayout* ret = nullptr;
	std::wstring wtext = Utf8ToWide(text);
	HRESULT hr = write_factory->CreateTextLayout(
		wtext.c_str(),  // The text to render
		(uint32_t)wtext.size(),                // The length of the text
		format,        // The text format
		width,           // The maximum width of the text layout
		height,           // The maximum height of the text layout
		&ret        // The output text layout
	);
	return ret;
}

IDWriteTextFormat* Direct2D::GetFont(const std::string& font_name, float font_size,
	DWRITE_FONT_WEIGHT weight,
	DWRITE_FONT_STYLE style,
	DWRITE_TEXT_ALIGNMENT align,
	DWRITE_PARAGRAPH_ALIGNMENT valign,
	DWRITE_FONT_STRETCH stretch) {
	IDWriteTextFormat* ret = nullptr;
	// Create a DirectWrite text format object.
	std::wstring wfont_name = Utf8ToWide(font_name);
	write_factory->CreateTextFormat(
		wfont_name.c_str(),
		NULL,
		weight,
		style,
		stretch,
		font_size,
		L"", //locale
		&ret
	);
	if (ret != nullptr) {
		// Center the text horizontally and vertically.
		ret->SetTextAlignment(align);
		ret->SetParagraphAlignment(valign);
	}
	return ret;
}

bool Direct2D::RenderText(IDWriteTextLayout* layout, const D2D1_COLOR_F& color, uint32_t w, uint32_t h, float dpi,
	std::vector<uint8_t>& rgba) {
	if (layout == nullptr || w == 0 || h == 0) {
		return false;
	}
	if (wic_factory == nullptr &&
		FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_factory)))) {
		return false;
	}
	IWICBitmap* bitmap = nullptr;
	if (FAILED(wic_factory->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &bitmap))) {
		return false;
	}
	bool ok = false;
	ID2D1RenderTarget* target = nullptr;
	const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
	if (SUCCEEDED(d2d_factory->CreateWicBitmapRenderTarget(bitmap, props, &target))) {
		ID2D1SolidColorBrush* brush = nullptr;
		if (SUCCEEDED(target->CreateSolidColorBrush(color, &brush))) {
			//Grayscale: ClearType needs to know what is behind the text, and what is behind it here is transparency.
			target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
			target->BeginDraw();
			target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
			target->DrawTextLayout(D2D1::Point2F(0.0f, 0.0f), layout, brush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
			ok = SUCCEEDED(target->EndDraw());
			brush->Release();
		}
		target->Release();
	}
	if (ok) {
		ok = false;
		IWICBitmapLock* lock = nullptr;
		const WICRect rect = { 0, 0, (INT)w, (INT)h };
		if (SUCCEEDED(bitmap->Lock(&rect, WICBitmapLockRead, &lock))) {
			UINT stride = 0;
			UINT size = 0;
			BYTE* data = nullptr;
			if (SUCCEEDED(lock->GetStride(&stride)) && SUCCEEDED(lock->GetDataPointer(&size, &data))) {
				rgba.resize((size_t)w * h * 4);
				for (uint32_t y = 0; y < h; ++y) {
					const uint8_t* src = data + (size_t)y * stride;
					uint8_t* dst = rgba.data() + (size_t)y * w * 4;
					for (uint32_t x = 0; x < w; ++x, src += 4, dst += 4) {
						//Premultiplied BGRA to straight RGBA, which is what the UI shader and its blend state expect.
						const uint32_t a = src[3];
						if (a == 0) {
							dst[0] = dst[1] = dst[2] = dst[3] = 0;
						}
						else {
							dst[0] = (uint8_t)(std::min)(255u, (src[2] * 255u + a / 2) / a);
							dst[1] = (uint8_t)(std::min)(255u, (src[1] * 255u + a / 2) / a);
							dst[2] = (uint8_t)(std::min)(255u, (src[0] * 255u + a / 2) / a);
							dst[3] = (uint8_t)a;
						}
					}
				}
				ok = true;
			}
			lock->Release();
		}
	}
	bitmap->Release();
	return ok;
}
