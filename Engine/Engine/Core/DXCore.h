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

#include <winsock2.h>
#include <Windows.h>
#include <d3d11.h>
#include <string>

#include "../Defines.h"
#include "Scheduler.h"
#include "Interfaces.h"

#include <ECS/Coordinator.h>
#include <d2d1.h>
#include <dwrite_2.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace HotBite {
	namespace Engine {
		namespace Core {

			//Forward declaration of textures
			class DepthTexture2D;
			class RenderTexture2D;

			class DXCore : public IRenderTarget, public IDepthResource, public ECS::IEventSender
			{
			public:
				static inline ECS::EventId EVENT_ID_MOUSE_MOVE = ECS::GetEventId<DXCore>(0x00);
				static inline ECS::EventId EVENT_ID_MOUSE_WHEEL = ECS::GetEventId<DXCore>(0x01);
				static inline ECS::EventId EVENT_ID_MOUSE_LDOWN = ECS::GetEventId<DXCore>(0x02);
				static inline ECS::EventId EVENT_ID_MOUSE_LUP = ECS::GetEventId<DXCore>(0x03);
				static inline ECS::EventId EVENT_ID_MOUSE_MDOWN = ECS::GetEventId<DXCore>(0x04);
				static inline ECS::EventId EVENT_ID_MOUSE_MUP = ECS::GetEventId<DXCore>(0x05);
				static inline ECS::EventId EVENT_ID_MOUSE_RDOWN = ECS::GetEventId<DXCore>(0x06);
				static inline ECS::EventId EVENT_ID_MOUSE_RUP = ECS::GetEventId<DXCore>(0x07);
				static inline ECS::EventId EVENT_ID_DETROY = ECS::GetEventId<DXCore>(0x08);
				static inline ECS::EventId EVENT_ID_FOCUS = ECS::GetEventId<DXCore>(0x09);
				static inline ECS::EventId EVENT_ID_KEY_DOWN = ECS::GetEventId<DXCore>(0x0A);
				static inline ECS::EventId EVENT_ID_KEY_UP = ECS::GetEventId<DXCore>(0x0B);
				static inline ECS::EventId EVENT_ID_MOUSE_LDCLICK = ECS::GetEventId<DXCore>(0x0C);

				static inline ECS::ParamId PARAM_ID_X = 0x00;
				static inline ECS::ParamId PARAM_ID_Y = 0x01;
				static inline ECS::ParamId PARAM_ID_BUTTON = 0x02;
				static inline ECS::ParamId PARAM_ID_WHEEL = 0x03;
				static inline ECS::ParamId PARAM_RELATIVE_ID_X = 0x04;
				static inline ECS::ParamId PARAM_RELATIVE_ID_Y = 0x05;
				static inline ECS::ParamId PARAM_ID_KEY = 0x06;

				static constexpr int NTHREADS = 7;
				static constexpr int MAIN_THREAD = 0;
				static constexpr int BACKGROUND_THREAD = 1;
				static constexpr int BACKGROUND2_THREAD = 2;
				static constexpr int BACKGROUND3_THREAD = 3;
				static constexpr int PHYSICS_THREAD = 4;
				static constexpr int LOCKSTEP_TICK_THREAD = 5;
				static constexpr int AUDIO_THREAD = 6;

				DXCore(
					HINSTANCE hInstance,		// The application's handle
					const char* titleBarText,			// Text for the window's title bar
					unsigned int windowWidth = 0,	// Width of the window's client area
					unsigned int windowHeight = 0,	// Height of the window's client area
					bool debugTitleBarStats = false,    // Show extra stats (fps) in title bar?
					bool window = false);               // Windowed or maximized
				~DXCore();

				// Static requirements for OS-level message processing
				static LRESULT CALLBACK WindowProc(
					HWND hWnd,		// Window handle
					UINT uMsg,		// Message
					WPARAM wParam,	// Message's first parameter
					LPARAM lParam	// Message's second parameter
				);

				// Internal method for message handling
				LRESULT ProcessMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

				std::vector<IDXGIAdapter*> EnumerateAdapters();
				// Initialization and game-loop related methods
				HRESULT InitWindow();
				HRESULT InitDirectX();
				HRESULT Run();
				void Stop();
				void Quit();

				// Convenience methods for handling mouse input, since we
				// can easily grab mouse input from OS-level messages
				virtual void OnMouseDown(WPARAM buttonState, int x, int y) { }
				virtual void OnMouseUp(WPARAM buttonState, int x, int y) { }
				virtual void OnMouseMove(WPARAM buttonState, int x, int y) { }
				virtual void OnMouseWheel(float wheelDelta, int x, int y) { }

				// Helper function for allocating a console window
				void CreateConsoleWindow(int bufferLines, int bufferColumns, int windowLines, int windowColumns);
				//Frame stats function
				void OnFrame();
				float GetAspectRatio() const { return (float)width / (float)height; }
				int GetWidth() const { return width; }
				int GetHeight() const { return height; }
				uint32_t GetDpi() const { return dpi; }
				virtual void ClearScreen(const float color[4]);
				virtual void Present();

				virtual ID3D11RenderTargetView* RenderTarget() const override;
				virtual ID3D11ShaderResourceView* DepthResource() override;
				virtual ID3D11DepthStencilView* DepthView() override;
				
				virtual ECS::Coordinator* GetCoordinator() = 0;

				const float2& GetScreenDimensions() const { return screen_dimensions; }
				float GetScreenWidth() const { return screen_dimensions.x; }
				float GetScreenHeight() const { return screen_dimensions.y; }

			public:

				ID3D11Device* device = nullptr;
				ID3D11DeviceContext* context = nullptr;
				ID3D11SamplerState* basic_sampler = nullptr;
				ID3D11SamplerState* shadow_sampler = nullptr;
				ID3D11RasterizerState* shadow_rasterizer = nullptr;
				ID3D11RasterizerState* dir_shadow_rasterizer = nullptr;				
				ID3D11RasterizerState* drawing_rasterizer = nullptr;
				ID3D11RasterizerState* sky_rasterizer = nullptr;
				ID3D11RasterizerState* wireframe_rasterizer = nullptr;
				ID3D11BlendState* no_blend = NULL;
				ID3D11BlendState* blend = NULL;
				ID3D11DepthStencilState* normal_depth = NULL;
				ID3D11DepthStencilState* transparent_depth = NULL;

				D3D11_VIEWPORT viewport = {};
				D3D11_VIEWPORT half_viewport = {};
				D3D11_VIEWPORT screenport = {};
				HWND		wnd;			// The handle to the window itself

				static DXCore* Get();

			protected:

				static DXCore* DXCoreInstance;
				// Size of the window's client area
				uint32_t dpi = 0;
				unsigned int width;
				unsigned int height;
				HINSTANCE	hInstance;		// The handle to the application
				std::string titleBarText;	// Custom text in window's title bar
				bool		titleBarStats;	// Show extra stats in title bar?
				bool windowed;
				float2 screen_dimensions = {};
				// DirectX related objects and variables
				D3D_FEATURE_LEVEL dxFeatureLevel;
				IDXGISwapChain* swapChain;
				ID3D11RenderTargetView* backBufferRTV;
				DepthTexture2D* depthTexture;
				bool end = false;
				// FPS calculation
				uint32_t fpsFrameCount;
				uint32_t current_fps;
				float fpsTimeElapsed;
				std::queue<std::thread> threads;
				//mouse cache
				Int2 last_mouse_position{ -1, -1 };
				void UpdateTitleBarStats();	// Puts debug info in the title bar
			};

			class Direct2D {
			private:
				ID2D1Factory* d2d_factory = nullptr;
				IDWriteFactory* write_factory = nullptr;

				Direct2D();
				~Direct2D();

				static Direct2D* instance;
			public:

				static void Init() {
					if (instance == nullptr) {
						instance = new Direct2D();
					}
				}

				static void Release() {
					if (instance != nullptr) {
						delete instance;
						instance = nullptr;
					}
				}

				static Direct2D* Get() {
					return instance;
				}

				IDWriteTextLayout* GetText(const std::string& text, IDWriteTextFormat* format, float width, float height);

				IDWriteTextFormat* GetFont(const std::string& font_name, float font_size,
					DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL,
					DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL,					
					DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
					DWRITE_PARAGRAPH_ALIGNMENT valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
					DWRITE_FONT_STRETCH stretch = DWRITE_FONT_STRETCH_NORMAL);

				ID2D1RenderTarget* GetD2DRenderTargetView(ID3D11RenderTargetView* d3dRenderTargetView);
			};
		}
	}
}