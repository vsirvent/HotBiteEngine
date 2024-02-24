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
#include "Scheduler.h"
#include "Texture.h"
#include "PostProcess.h"
#include "Vertex.h"

#include <WindowsX.h>
#include <sstream>
#include <algorithm>

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

	ID3D11RenderTargetView* none[] = { nullptr };
	context->OMSetRenderTargets(_countof(none), none, nullptr);
	context->RSSetViewports(0, nullptr);
	context->RSSetState(nullptr);

	if (depthTexture) { delete depthTexture; }

	// Release all DirectX resources
	if (basic_sampler) { basic_sampler->Release(); }
	if (shadow_sampler) { shadow_sampler->Release(); }
	if (blend) { blend->Release(); }
	if (no_blend) { no_blend->Release(); }
	if (shadow_rasterizer) { shadow_rasterizer->Release(); }
	if (dir_shadow_rasterizer) { dir_shadow_rasterizer->Release(); }
	if (wireframe_rasterizer) { wireframe_rasterizer->Release(); }
	if (drawing_rasterizer) { drawing_rasterizer->Release(); }
	if (sky_rasterizer) { sky_rasterizer->Release(); }
	if (normal_depth) { normal_depth->Release(); }
	if (transparent_depth) { transparent_depth->Release(); }
	if (backBufferRTV) { backBufferRTV->Release(); }
	if (swapChain) { swapChain->Release(); }
	if (context) { context->Flush();  context->Release(); }
	
#if 1
	//Check if all d3d instances are correctly released
	ID3D11Debug* debug;
	device->QueryInterface(IID_ID3D11Debug, (void**)(&debug));
	if (debug)
	{
		debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
		debug->Release();
	}
#endif
	if (device) { device->Release(); }
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
			style |= WS_CAPTION | WS_THICKFRAME | CS_DBLCLKS;
		}
		SetWindowLong(wnd, GWL_STYLE, style);

		LONG_PTR class_style = GetClassLongPtr(wnd, GCL_STYLE) | CS_DBLCLKS;
		SetClassLongPtr(wnd, GCL_STYLE, class_style);

		if (windowed) {
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
	unsigned int deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	int w = width;
	int h = height;
	if (!windowed) {
		RECT rect;
		GetClientRect(GetDesktopWindow(), &rect);
		w = rect.right - rect.left;
		h = rect.bottom - rect.top;
	}
#if defined(DEBUG) || defined(_DEBUG)
	deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
	DXGI_SWAP_CHAIN_DESC swapDesc = {};
	swapDesc.BufferCount = 1;
	swapDesc.BufferDesc.Width = w;
	swapDesc.BufferDesc.Height = h;
	swapDesc.BufferDesc.RefreshRate.Numerator = 60;
	swapDesc.BufferDesc.RefreshRate.Denominator = 1;
	swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapDesc.Flags = 0;
	swapDesc.OutputWindow = wnd;
	swapDesc.SampleDesc.Count = 1;
	swapDesc.SampleDesc.Quality = 0;
	swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	swapDesc.Windowed = true;

	HRESULT hr = S_OK;

	IDXGIAdapter* adapter = nullptr;

	std::vector<IDXGIAdapter*> adapters = EnumerateAdapters();
	SIZE_T video_mem = 0;
	for (IDXGIAdapter* a : adapters) {
		DXGI_ADAPTER_DESC desc;
		a->GetDesc(&desc);
		if (desc.DedicatedVideoMemory > video_mem) {
			adapter = a;
			video_mem = desc.DedicatedVideoMemory;
		}
	}

	hr = D3D11CreateDeviceAndSwapChain(
		adapter,					// Video adapter (physical GPU) to use, or null for default
		D3D_DRIVER_TYPE_UNKNOWN,	// We want to use the hardware (GPU)
		0,							// Used when doing software rendering
		deviceFlags,				// Any special options
		0,							// Optional array of possible verisons we want as fallbacks
		0,							// The number of fallbacks in the above param
		D3D11_SDK_VERSION,			// Current version of the SDK
		&swapDesc,					// Address of swap chain options
		&swapChain,					// Pointer to our Swap Chain pointer
		&device,					// Pointer to our Device pointer
		&dxFeatureLevel,			// This will hold the actual feature level the app will use
		&context);					// Pointer to our Device Context pointer
	if (FAILED(hr)) return hr;

	context->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ScreenDraw::Init(context);

	ID3D11Texture2D* backBufferTexture = nullptr;
	swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBufferTexture);
	if (FAILED(hr)) return hr;
	hr = device->CreateRenderTargetView(backBufferTexture, nullptr, &backBufferRTV);
	backBufferTexture->Release();
	if (FAILED(hr)) return hr;
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

	D3D11_RASTERIZER_DESC drawingRenderStateDesc;
	ZeroMemory(&drawingRenderStateDesc, sizeof(D3D11_RASTERIZER_DESC));
	drawingRenderStateDesc.CullMode = D3D11_CULL_NONE;
	drawingRenderStateDesc.FillMode = D3D11_FILL_SOLID;
	drawingRenderStateDesc.AntialiasedLineEnable = true;
	drawingRenderStateDesc.DepthClipEnable = true;
	if (FAILED(hr = device->CreateRasterizerState(&drawingRenderStateDesc, &drawing_rasterizer))) {
		return hr;
	}

	D3D11_RASTERIZER_DESC skyRenderStateDesc;
	ZeroMemory(&skyRenderStateDesc, sizeof(D3D11_RASTERIZER_DESC));
	skyRenderStateDesc.CullMode = D3D11_CULL_NONE;
	skyRenderStateDesc.FillMode = D3D11_FILL_SOLID;
	skyRenderStateDesc.AntialiasedLineEnable = false;
	skyRenderStateDesc.DepthClipEnable = false;
	if (FAILED(hr = device->CreateRasterizerState(&skyRenderStateDesc, &sky_rasterizer))) {
		return hr;
	}


	D3D11_RASTERIZER_DESC wireFrameRenderStateDesc;
	ZeroMemory(&wireFrameRenderStateDesc, sizeof(D3D11_RASTERIZER_DESC));
	wireFrameRenderStateDesc.CullMode = D3D11_CULL_BACK;
	wireFrameRenderStateDesc.FillMode = D3D11_FILL_WIREFRAME;
	wireFrameRenderStateDesc.AntialiasedLineEnable = true;
	wireFrameRenderStateDesc.DepthClipEnable = true;
	if (FAILED(hr = device->CreateRasterizerState(&wireFrameRenderStateDesc, &wireframe_rasterizer))) {
		return hr;
	}

	D3D11_RASTERIZER_DESC shadowRenderStateDesc;
	ZeroMemory(&shadowRenderStateDesc, sizeof(D3D11_RASTERIZER_DESC));
	shadowRenderStateDesc.CullMode = D3D11_CULL_BACK;
	shadowRenderStateDesc.FillMode = D3D11_FILL_SOLID;
	shadowRenderStateDesc.DepthBias = 0;
	shadowRenderStateDesc.DepthClipEnable = false;
	shadowRenderStateDesc.SlopeScaledDepthBias = 2.0f;
	shadowRenderStateDesc.DepthBias = 0;
	if (FAILED(hr = device->CreateRasterizerState(&shadowRenderStateDesc, &shadow_rasterizer))) {
		return hr;
	}
	shadowRenderStateDesc.CullMode = D3D11_CULL_BACK;
	shadowRenderStateDesc.SlopeScaledDepthBias = 2.0f;
	//shadowRenderStateDesc.DepthBias = 1000;
	if (FAILED(hr = device->CreateRasterizerState(&shadowRenderStateDesc, &dir_shadow_rasterizer))) {
		return hr;
	}

	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.Filter = D3D11_FILTER_ANISOTROPIC;
	samplerDesc.MaxAnisotropy = 16;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	device->CreateSamplerState(&samplerDesc, &basic_sampler);

	D3D11_SAMPLER_DESC comparisonSamplerDesc;
	ZeroMemory(&comparisonSamplerDesc, sizeof(D3D11_SAMPLER_DESC));
	comparisonSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
	comparisonSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
	comparisonSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
	comparisonSamplerDesc.MinLOD = 0.f;
	comparisonSamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	comparisonSamplerDesc.MipLODBias = 0.f;
	comparisonSamplerDesc.MaxAnisotropy = 0;
	comparisonSamplerDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
	comparisonSamplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;


	D3D11_BLEND_DESC BlendState;
	ZeroMemory(&BlendState, sizeof(D3D11_BLEND_DESC));
	BlendState.RenderTarget[0].BlendEnable = FALSE;
	BlendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	device->CreateBlendState(&BlendState, &no_blend);

	BlendState.AlphaToCoverageEnable = FALSE;
	BlendState.IndependentBlendEnable = FALSE;
	BlendState.RenderTarget[0].BlendEnable = TRUE;
	BlendState.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	BlendState.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	BlendState.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	BlendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
	BlendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	BlendState.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	BlendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	device->CreateBlendState(&BlendState, &blend);

	if (FAILED(hr = device->CreateSamplerState(&comparisonSamplerDesc, &shadow_sampler))) {
		return hr;
	}
#if 1
	ID3D11DepthStencilState* pDepthStencilState = nullptr;
	D3D11_DEPTH_STENCIL_DESC  DepthState;
	DepthState.DepthEnable = TRUE;
	DepthState.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	DepthState.DepthFunc = D3D11_COMPARISON_LESS;
	DepthState.StencilEnable = FALSE;
	DepthState.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
	DepthState.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;

	const D3D11_DEPTH_STENCILOP_DESC defaultStencilOp =
	{ D3D11_STENCIL_OP_KEEP,
	  D3D11_STENCIL_OP_KEEP,
	  D3D11_STENCIL_OP_KEEP,
	  D3D11_COMPARISON_ALWAYS };

	DepthState.FrontFace = defaultStencilOp;
	DepthState.BackFace = defaultStencilOp;
	device->CreateDepthStencilState(&DepthState, &normal_depth);
	DepthState.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	device->CreateDepthStencilState(&DepthState, &transparent_depth);
#endif
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
	context->ClearDepthStencilView(depthTexture->Depth(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

ID3D11RenderTargetView* DXCore::RenderTarget() const {
	return backBufferRTV;
}

ID3D11ShaderResourceView* DXCore::DepthResource() {
	return depthTexture->SRV();
}

ID3D11DepthStencilView* DXCore::DepthView() {
	return depthTexture->Depth();
}

void DXCore::Present()
{
	swapChain->Present(1, 0);
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

	SetThreadAffinityMask(GetCurrentThread(), 1);
	for (int i = 1; i < NTHREADS; ++i) {
		threads.emplace(std::thread([&e = this->end, i](){
			while (!e) {
				Scheduler::Get(i)->Update();
				Scheduler::Get(i)->Idle();
			}
		}));
		DWORD_PTR dw = SetThreadAffinityMask(threads.front().native_handle(), DWORD_PTR(1) << (i));
		if (dw == 0)
		{
			DWORD dwErr = GetLastError();
			printf("SetThreadAffinityMask failed: %d\n", dwErr);
		}
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
	switch (dxFeatureLevel)
	{
	case D3D_FEATURE_LEVEL_11_1: output << "    DX 11.1"; break;
	case D3D_FEATURE_LEVEL_11_0: output << "    DX 11.0"; break;
	case D3D_FEATURE_LEVEL_10_1: output << "    DX 10.1"; break;
	case D3D_FEATURE_LEVEL_10_0: output << "    DX 10.0"; break;
	case D3D_FEATURE_LEVEL_9_3:  output << "    DX 9.3";  break;
	case D3D_FEATURE_LEVEL_9_2:  output << "    DX 9.2";  break;
	case D3D_FEATURE_LEVEL_9_1:  output << "    DX 9.1";  break;
	default:                     output << "    DX ???";  break;
	}

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
	if (end) {
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
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
}


IDWriteTextLayout* Direct2D::GetText(const std::string& text, IDWriteTextFormat* format, float width, float height) {
	IDWriteTextLayout* ret = nullptr;
	std::wstring wtext(text.begin(), text.end());
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
	std::wstring wfont_name(font_name.begin(), font_name.end());
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

ID2D1RenderTarget* Direct2D::GetD2DRenderTargetView(ID3D11RenderTargetView* d3dRenderTargetView) {
	ID2D1RenderTarget* d2dRenderTarget = nullptr;
	ID3D11Resource* d3dResource = nullptr;
	IDXGISurface* dxgiSurface = nullptr;

	// Get the underlying resource from the render target view
	d3dRenderTargetView->GetResource(&d3dResource);

	// Query the resource for the DXGI surface interface
	HRESULT hr = d3dResource->QueryInterface(__uuidof(IDXGISurface), (void**)&dxgiSurface);
	d3dResource->Release();
	if (dxgiSurface != nullptr)
	{
		float dpi = (float)DXCore::Get()->GetDpi();

		D2D1_RENDER_TARGET_PROPERTIES renderTargetProperties = D2D1::RenderTargetProperties(
			D2D1_RENDER_TARGET_TYPE_HARDWARE,
			D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_IGNORE),
			dpi,
			dpi);
		d2d_factory->CreateDxgiSurfaceRenderTarget(dxgiSurface, renderTargetProperties, &d2dRenderTarget);
		dxgiSurface->Release();
		if (d2dRenderTarget != nullptr) {
			// Set the antialiasing mode for text rendering
			d2dRenderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
			// Create the text rendering parameters
			IDWriteRenderingParams* renderingParams;
			write_factory->CreateRenderingParams(&renderingParams);
			d2dRenderTarget->SetTextRenderingParams(renderingParams);
			renderingParams->Release();
		}
	}
	return d2dRenderTarget;
}