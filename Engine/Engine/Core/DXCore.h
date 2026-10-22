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
#include "GfxContext.h"
#include <string>
#include <filesystem>

#include "../Defines.h"
#include "Scheduler.h"
#include "Interfaces.h"

#include <ECS/Coordinator.h>
#include <d2d1.h>
#include <wincodec.h>
#include <dwrite_2.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

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
				static inline ECS::ParamId PARAM_SENDER_NAME = 0x07;

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

				// Open the window maximized on the primary monitor rather than at the
				// client size passed to the constructor. Must be called before
				// InitWindow: the maximized client area becomes the render target size,
				// and that has to be settled before InitDirectX creates the swap chain.
				//
				// Meant for tool windows, whose panels want all the vertical space the
				// screen has; a fixed request like 1600x900 overflows its own panels on
				// a smaller display and wastes room on a larger one.
				void SetStartMaximized(bool enabled) { start_maximized = enabled; }

				// Initialization and game-loop related methods
				HRESULT InitWindow(HWND parent = NULL);
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

				// Hook for embedding a UI overlay (e.g. Dear ImGui) into the Win32 message
				// stream. Called unconditionally as the first statement of ProcessMessage.
				// Default is a no-op, so consumers that don't override it are unaffected.
				virtual void ForwardWindowMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) { }

				// Lets such an overlay keep the mouse and the keyboard to itself: when this says true for a mouse or keyboard
				// message, the message is not sent on as an event (nothing under the overlay sees the click, the wheel or the key).
				// The overlay decides per message, so it can let the key-ups of keys that went down before it opened through.
				// Called after ForwardWindowMessage. Default false.
				virtual bool OverlayCapturesInput(UINT uMsg) { return false; }

				// Helper function for allocating a console window
				void CreateConsoleWindow(int bufferLines, int bufferColumns, int windowLines, int windowColumns);
				//Frame stats function
				void OnFrame();
				float GetAspectRatio() const { return (float)width / (float)height; }
				int GetWidth() const { return width; }
				int GetHeight() const { return height; }
				uint32_t GetDpi() const { return dpi; }
				virtual void ClearScreen(const float color[4]);
								//The finished frame (what Present would show) as 8-bit RGBA, top row first. Waits for the GPU.
								bool ReadBackBuffer(std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h);
								//The finished frame written as a PNG (the automation `screenshot`). Returns false and sets `error` on failure.
								bool SaveBackBuffer(const std::filesystem::path& png, std::string& error);
								GfxTexture* BackBufferTexture() const { return backBufferTexture; }
				virtual void Present();

				virtual GfxRTV* RenderTarget() const override;
				virtual GfxSRV* DepthResource() override;
				virtual GfxDSV* DepthView() override;
				
				virtual ECS::Coordinator* GetCoordinator() = 0;

				const float2& GetScreenDimensions() const { return screen_dimensions; }
				float GetScreenWidth() const { return screen_dimensions.x; }
				float GetScreenHeight() const { return screen_dimensions.y; }

			public:

				//Whether a compute shader may bind more than 8 UAVs at once. D3D12 has 64 UAV slots, so this is
				//always true; kept so the code that asked before D3D11_1 could be assumed needs no change.
				bool SupportsExtendedUAVSlots() const {
					return true;
				}

				//The D3D12 device and the immediate-context-shaped recorder over its command list. `context` is
				//what `GfxContext* context` was: every draw, dispatch, clear and state change goes
				//through it. See GfxContext.h and Docs/D3D12_MIGRATION.md.
				GfxDevice* device = nullptr;
				GfxContext* context = nullptr;

				//State objects. They were D3D11 state objects; they are descriptions now, turned into pipeline
				//state objects by the context when something draws with them.
				GfxSampler* basic_sampler = nullptr;
				GfxSampler* shadow_sampler = nullptr;
				GfxRasterizerState* shadow_rasterizer = nullptr;
				GfxRasterizerState* dir_shadow_rasterizer = nullptr;
				GfxRasterizerState* drawing_rasterizer = nullptr;
				//Same as drawing_rasterizer plus a small depth bias, used by the depth
				//pre-pass. The bias pushes the pre-pass depth slightly away from the
				//camera so the main pass, which reaches the same surface through a
				//different chain of matrix products (and through tessellation), never
				//loses to it in the LESS_EQUAL test. See depth_prepass below.
				GfxRasterizerState* depth_rasterizer = nullptr;
				GfxRasterizerState* sky_rasterizer = nullptr;
				GfxRasterizerState* wireframe_rasterizer = nullptr;
				GfxBlendState* no_blend = nullptr;
				GfxBlendState* blend = nullptr;
				GfxDepthStencilState* normal_depth = nullptr;
				GfxDepthStencilState* transparent_depth = nullptr;
				//Depth states for passes that render on top of the depth pre-pass buffer.
				//LESS_EQUAL, not LESS: the pre-pass already wrote the depth of every
				//opaque surface, so the main pass draws that same surface again and must
				//not be rejected by its own pre-pass value. The hardware then rejects
				//everything the pre-pass found an occluder for before the pixel shader
				//runs - which is what the pre-pass is for.
				//_prepass writes depth (geometry the pre-pass skipped - alpha/blended
				//materials, second-pass entities - still has to occlude itself),
				//_prepass_read only tests (the sky, which is blended in behind
				//everything and must not disturb the buffer).
				GfxDepthStencilState* depth_prepass = nullptr;
				GfxDepthStencilState* depth_prepass_read = nullptr;

				D3D12_VIEWPORT viewport = {};
				D3D12_VIEWPORT half_viewport = {};
				D3D12_VIEWPORT screenport = {};
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
				bool start_maximized = false;
				float2 screen_dimensions = {};
				// DirectX related objects and variables
				D3D_FEATURE_LEVEL dxFeatureLevel = D3D_FEATURE_LEVEL_12_0;
				//Everything is drawn into backBufferTexture, a texture of the window's size at a fixed address, and
				//Present() copies it into the swap chain's current buffer. The swap chain flips between several
				//buffers, but code all over the engine keeps the RenderTarget() view it was given (the GUI, the
				//post-process chain), so what they hold has to stay the same one.
				Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain;
				static constexpr UINT SWAP_BUFFERS = 3;
				GfxTexture* swapBuffers[SWAP_BUFFERS] = {};
				GfxTexture* backBufferTexture = nullptr;
				GfxRTV* backBufferRTV;
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
				bool wnd_update = true;
			};

			class Direct2D {
			private:
				ID2D1Factory* d2d_factory = nullptr;
				IDWriteFactory* write_factory = nullptr;
								IWICImagingFactory* wic_factory = nullptr;

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

				//Draws a text layout into a w x h bitmap of its own and hands back the pixels as 8-bit RGBA with straight
								//(not premultiplied) alpha, rows top to bottom, transparent where there is no text. Direct2D draws it on
								//the CPU (a WIC bitmap), which is what lets a D3D12 renderer have text at all: Direct2D cannot target a
								//D3D12 resource, and the label that wants the text keeps it as a texture and draws it as a quad like any
								//other widget (Label.h). `dpi` is the scale the layout was made for.
								bool RenderText(IDWriteTextLayout* layout, const D2D1_COLOR_F& color, uint32_t w, uint32_t h, float dpi,
									std::vector<uint8_t>& rgba);
			};
		}
	}
}