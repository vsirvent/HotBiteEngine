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
#include <Components\Camera.h>

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
				virtual void ClearData(const float color[4]) = 0;

			public:

				PostProcess(ID3D11DeviceContext* dxcontext,
					SimpleVertexShader* vertex_shader,
					SimplePixelShader* pixel_shader);

				int GetW() const { return w; }
				int GetH() const { return h; }

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
				virtual void SetView(const Components::Camera& camera);

				virtual ID3D11ShaderResourceView* RenderResource() override;
				virtual ID3D11UnorderedAccessView* RenderUAV() override;
				virtual ID3D11RenderTargetView* RenderTarget() const override;
				virtual ID3D11ShaderResourceView* DepthResource() override;
				virtual ID3D11DepthStencilView* DepthView() override;

				SimpleVertexShader* GetVS() { return vs; }
				SimplePixelShader* GetPS() { return ps; }
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
			
			class MainEffect : public PostProcess
			{
			private:
				RenderTexture2D text;
				DepthTexture2D depth;
				virtual void ClearData(const float color[4]);

			public:
				MainEffect(ID3D11DeviceContext* dxcontext, int width, int height);
				virtual ~MainEffect();

				virtual ID3D11ShaderResourceView* RenderResource() override;
				virtual ID3D11UnorderedAccessView* RenderUAV() override;
				virtual ID3D11RenderTargetView* RenderTarget() const override;
				virtual ID3D11ShaderResourceView* DepthResource() override;
				virtual ID3D11DepthStencilView* DepthView() override;
			};

			class MotionBlurEffect : public PostProcess
			{
			private:
				RenderTexture2D text{ 3 };

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
				virtual void SetView(const Components::Camera& camera) override {
					prev_view = current_view;
					current_view = camera.view_projection;
					PostProcess::SetView(camera);
				}
			};

			class BaseDOFProcess: public HotBite::Engine::Core::GenericPostProcess
			{
			protected:
				bool enabled = false;
				float focus = 0.0f;
				float amplitude = 0.0f;
			public:

				BaseDOFProcess(ID3D11DeviceContext* dxcontext,
					int width, int height, HotBite::Engine::ECS::Coordinator* c,
					const std::string& vs, const std::string& ps) : GenericPostProcess(dxcontext, width, height, c, vs, ps) {}

				virtual ~BaseDOFProcess() {}

				void SetFocus(float val) {
					focus = val;
				}

				float GetFocus() const {
					return focus;
				}

				void SetAmplitude(float val) {
					amplitude = val;
				}

				float GetAmplitude() const {
					return amplitude;
				}

				void SetEnabled(bool val) {
					enabled = val;
				}

				bool GetEnabled() const {
					return enabled;
				}
			};

			class DOFProcess : public BaseDOFProcess
			{
			private:
				HotBite::Engine::Core::RenderTexture2D temp;

			public:

				DOFProcess(ID3D11DeviceContext* dxcontext,
					int width, int height, HotBite::Engine::ECS::Coordinator* c) : 
					BaseDOFProcess(dxcontext, width, height, c, "PostMainVS.cso", "PostDOP.cso")
				{
					temp.Init(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT);
				}

				virtual ~DOFProcess() {
					temp.Release();
				}

				
		
				void Render() override {
					ID3D11RenderTargetView* rv[1] = { temp.RenderTarget() };
					context->OMSetRenderTargets(1, rv, TargetDepthView());
					ps->SetInt("dopActive", enabled);
					ps->SetFloat("focusZ", focus);
					ps->SetFloat("amplitude", amplitude);
					ps->SetInt("type", 1);
					ps->CopyAllBufferData();
					PostProcess::Render();

					rv[0] = { TargetRenderView() };
					context->OMSetRenderTargets(1, rv, TargetDepthView());
					ps->SetShaderResourceView("renderTexture", temp.SRV());
					ps->SetInt("type", 2);
					ps->CopyAllBufferData();
					PostProcess::Render();
				}
			};

			class DOFBokeProcess : public BaseDOFProcess
			{
			private:
				static const int32_t KERNEL_SIZE = 61;

				HotBite::Engine::Core::RenderTexture2D temp;
				HotBite::Engine::Core::RenderTexture2D kernel;

				void InitKernels() {
					auto init_kernel = [](ID3D11DeviceContext* dxcontext, SimpleComputeShader* shader, uint32_t kernel_size, uint32_t num_kernels,
						uint32_t num_components, ID3D11UnorderedAccessView* uav) {
							shader->SetInt("kernel_size", kernel_size);
							shader->SetInt("ncomponents", num_components);
							shader->SetUnorderedAccessView("kernels", uav);
							shader->SetShader();
							shader->CopyAllBufferData();
							dxcontext->Dispatch(1, num_kernels, 1);
							shader->SetUnorderedAccessView("kernels", nullptr);
							shader->CopyAllBufferData();
					};

					SimpleComputeShader* kernel_init_shader = ShaderFactory::Get()->GetShader<SimpleComputeShader>("InitDoFBokeCS.cso");
					init_kernel(context, kernel_init_shader, KERNEL_SIZE, kernel.Height(), 1, kernel.UAV());
				}
			public:

				DOFBokeProcess(ID3D11DeviceContext* dxcontext,
					int width, int height, HotBite::Engine::ECS::Coordinator* c) :
					BaseDOFProcess(dxcontext, width, height, c, "PostMainVS.cso", "PostDOFBoke.cso")
				{
					temp.Init(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT);

					int32_t size = (KERNEL_SIZE + 1 + 15) & ~15; // Round to upper multiple of 16
					float max_variance = ((float)KERNEL_SIZE) / 5.0f;
					float min_variance = 0.1f;
					int32_t num_kernels = (int32_t)(max_variance * 100 - 10) + 1;
					num_kernels = (num_kernels + 15) & ~15;

					kernel.Init(size, num_kernels, DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS);
					InitKernels();
				}

				virtual ~DOFBokeProcess() {
					temp.Release();
					kernel.Release();
				}

				void Render() override {
#if 0 //Uncomment to allow shader test and reload
					InitKernels();
#endif
					ID3D11RenderTargetView* rv[1] = { temp.RenderTarget() };
					context->OMSetRenderTargets(1, rv, TargetDepthView());
					ps->SetInt("kernel_size", KERNEL_SIZE);
					ps->SetInt("dopActive", enabled);
					ps->SetFloat("focusZ", focus);
					ps->SetFloat("amplitude", amplitude);
					ps->SetInt("type", 1);
					ps->SetShaderResourceView("kernels", kernel.SRV());
					ps->CopyAllBufferData();
					PostProcess::Render();
					
					rv[0] = { TargetRenderView() };
					context->OMSetRenderTargets(1, rv, TargetDepthView());
					ps->SetShaderResourceView("renderTexture", temp.SRV());
					ps->SetInt("type", 2);
					ps->CopyAllBufferData();
					PostProcess::Render();
					ps->SetShaderResourceView("kernels", nullptr);
					ps->SetShaderResourceView("renderTexture", nullptr);
				}
			};
		}
	}
}