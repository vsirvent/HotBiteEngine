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

#include "Defines.h"
#include "SimpleShader.h"
#include "Texture.h"
#include <d3d11.h>
#include <PrimitiveBatch.h>
#include <VertexTypes.h>
#include <DirectXColors.h>
#include <SimpleMath.h>
#include <vector>

namespace HotBite {
	namespace Engine {
		namespace Core {

			/** 
			 * Class to draw the full screen as a rectangle.
			 * Useful to render 2d post-process textures.
			 */
			class ScreenDraw {
			private:
				using VertexType = DirectX::VertexPositionColor;

				DirectX::PrimitiveBatch<VertexType>* batch;
				VertexType v1{ DirectX::SimpleMath::Vector3(-1.f, -1.f, 0.2f), DirectX::Colors::Transparent };
				VertexType v2{ DirectX::SimpleMath::Vector3(-1.f, 1.f, 0.2f), DirectX::Colors::Transparent };
				VertexType v3{ DirectX::SimpleMath::Vector3(1.f, 1.f, 0.2f), DirectX::Colors::Transparent };
				VertexType v4{ DirectX::SimpleMath::Vector3(1.f, -1.f, 0.2f), DirectX::Colors::Transparent };

				static ScreenDraw* instance;
				ScreenDraw(ID3D11DeviceContext* context);
				virtual ~ScreenDraw();

			public:
				//Singleton pattern
				static void Init(ID3D11DeviceContext* context);
				static void Release();
				static ScreenDraw* Get();

				//Draw to screen
				void Draw();
			};

			/**
			 * Post-process abstract parent class, every post-process effect is child of this class.
			 * It can be used as a source texture or as a render target.
			 * Several post-process modules are connected in a chain until final render target.
			 */
			class PostProcess : public IRenderTarget, public IRenderSource, public IDepthResource
			{
			protected:
				ID3D11DeviceContext* context = nullptr;
				SimpleVertexShader* vs = nullptr;
				SimplePixelShader* ps = nullptr;

				IRenderTarget* target_render = nullptr;
				IDepthResource* target_depth = nullptr;
				int w = 0;
				int h = 0;
				PostProcess* next = nullptr;
				std::map<std::string, ID3D11ShaderResourceView*> srvs;
				std::map<std::string, std::vector<uint8_t>> vars;

				virtual ID3D11RenderTargetView* TargetRenderView();
				virtual ID3D11DepthStencilView* TargetDepthView();
								
				virtual void Prepare();
				virtual void UnPrepare();
				virtual void Render();
				virtual ID3D11ShaderResourceView* RenderResource() override;
				virtual ID3D11RenderTargetView* RenderTarget() const override;
				virtual ID3D11ShaderResourceView* DepthResource() override;
				virtual ID3D11DepthStencilView* DepthView() override;
				virtual void ClearData(const float color[4]) = 0;

			public:

				PostProcess(ID3D11DeviceContext* dxcontext,
					SimpleVertexShader* vertex_shader,
					SimplePixelShader* pixel_shader);

				//Clear post-effect internal texture data
				void Clear(const float color[4]);

				//Execute post-chain and render from source texture to final target renderer
				//of the full chain
				void Process();
			
				//Connection chain
				void SetNext(PostProcess* next);
				PostProcess* GetNext() const;

				//Shader resources set to pixel shader during rendering
				void SetShaderResourceView(const std::string& name, ID3D11ShaderResourceView* rsv);

				//Set a variable to post-chain shaders
				void SetVariable(const std::string& name, const void* value, size_t size);

				//Set post-process target, can be another post-process or application render targets
				virtual void SetTarget(IRenderTarget* t, IDepthResource* d);

				//Useful data that is set to shaders in the post-chain
				virtual void SetView(const float4x4& view, const float4x4& view_inverse, const float4x4& projection_inverse);
			};

			class GenericPostProcess : public HotBite::Engine::Core::PostProcess
			{
			private:
				HotBite::Engine::ECS::Coordinator* coordinator = nullptr;
				HotBite::Engine::Core::RenderTexture2D text;

				//This is pure abstract from HotBite::Engine::Core::PostProcess to clean internal process texture
				virtual void ClearData(const float color[4]);

			public:
				GenericPostProcess(ID3D11DeviceContext* dxcontext,
					int width, int height, HotBite::Engine::ECS::Coordinator* c,
					const std::string& vs = "PostMainVS.cso", const std::string& ps = "PostMainPS.cso");
				virtual ~GenericPostProcess();

				virtual ID3D11ShaderResourceView* RenderResource() override;
				virtual ID3D11RenderTargetView* RenderTarget() const override;
			};
			
			class DOPEffect : public PostProcess
			{
			private:
				RenderTexture2D text{ 3 };
				DepthTexture2D depth;
				float4x4 current_projection_inverse{};
				float near_factor = 0.0f;
				float far_factor = 0.0f;
				bool active = true;
				virtual void ClearData(const float color[4]);

			public:
				DOPEffect(ID3D11DeviceContext* dxcontext,
					int width, int height, float near_factor, float far_factor);
				virtual ~DOPEffect();

				void SetFar(float far_factor) { this->far_factor = far_factor; }
				void SetNear(float near_factor) { this->near_factor = near_factor; }
				void SetActive(bool active) { this->active = active; }
				float GetFar(void) const { return far_factor; }
				float GetNear(void) const { return near_factor; }
				bool GetActive(void) const { return active; }

				virtual ID3D11ShaderResourceView* RenderResource() override;
				virtual ID3D11RenderTargetView* RenderTarget() const override;
				virtual ID3D11ShaderResourceView* DepthResource() override;
				virtual ID3D11DepthStencilView* DepthView() override;

				virtual void Prepare() override;
				virtual void UnPrepare() override;

				//Override parent method ans save the information used by our post-effect.
				virtual void SetView(const float4x4& view, const float4x4& view_inverse, const float4x4& projection_inverse) {
					current_projection_inverse = projection_inverse;
					PostProcess::SetView(view, view_inverse, projection_inverse);
				}
			};

			class MotionBlurEffect : public PostProcess
			{
			private:
				RenderTexture2D text{ 3 };

				float4x4 current_view_inverse{};
				float4x4 current_view{};
				float4x4 prev_view{};

				virtual void ClearData(const float color[4]);
			
			public:

				MotionBlurEffect(ID3D11DeviceContext* dxcontext,
					int width, int height);
				virtual ~MotionBlurEffect();
				virtual ID3D11ShaderResourceView* RenderResource() override;
				virtual ID3D11RenderTargetView* RenderTarget() const override;

				virtual void Prepare() override;

				//Override parent method ans save the information used by our post-effect.
				virtual void SetView(const float4x4& view, const float4x4& view_inverse, const float4x4& projection_inverse) override {
					prev_view = current_view;
					current_view = view;
					current_view_inverse = view_inverse;
					PostProcess::SetView(view, view_inverse, projection_inverse);
				}
			};
		}
	}
}