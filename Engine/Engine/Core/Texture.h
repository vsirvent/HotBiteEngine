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

#include "DXCore.h"

namespace HotBite {
	namespace Engine {
		namespace Core {
			class DepthTextureCube {
			private:
				ID3D11Texture2D* cube_texture = nullptr;
				ID3D11ShaderResourceView* shader_resource_view = nullptr;
				ID3D11DepthStencilView* depth_stencil_view = nullptr;
				bool init = false;
			public:
				DepthTextureCube();
				DepthTextureCube(const DepthTextureCube& other);
				virtual ~DepthTextureCube();
				HRESULT Init(int w, int h);
				void Release();
				void Clear();
				ID3D11ShaderResourceView* SRV() const;
				ID3D11DepthStencilView* Depth() const;
			};

			class DepthTexture2D {
			private:
				ID3D11Texture2D* texture = nullptr;
				ID3D11ShaderResourceView* shader_resource_view = nullptr;
				ID3D11DepthStencilView* depth_stencil_view = nullptr;
				bool init = false;
				int width = 0;
				int height = 0;
			public:
				DepthTexture2D();
				DepthTexture2D(const DepthTexture2D& other);
				virtual ~DepthTexture2D();
				HRESULT Init(int w, int h);
				void Release();
				void Clear();
				int Width() const;
				int Height() const;
				ID3D11ShaderResourceView* SRV() const;
				ID3D11DepthStencilView* Depth() const;
			};

			class RenderTexture2D: public IRenderTarget {
			private:
				D3D11_TEXTURE2D_DESC tex_desc;
				ID3D11Resource* resource = nullptr;
				ID3D11Texture2D* texture = nullptr;
				ID3D11ShaderResourceView* shader_resource_view = nullptr;
				ID3D11RenderTargetView* render_view = nullptr;
				ID3D11UnorderedAccessView* uav = nullptr;

				int width = 0;
				int height = 0;
				int mip_levels = 0;
				bool init = false;
			public:
				RenderTexture2D(int mip_levels = 1);
				RenderTexture2D(const RenderTexture2D& other);
				virtual ~RenderTexture2D();
				int Width() const;
				int Height() const;
				HRESULT Init(int w, int h, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM, const uint8_t* data = nullptr, uint32_t len = 0, uint32_t bind_flags = 0);
				void Release();
				void Clear(const float color[4]);
				ID3D11Resource* Resource() const;
				const D3D11_TEXTURE2D_DESC& Descriptor() const;
				ID3D11Texture2D* Texture() const;
				ID3D11ShaderResourceView* SRV() const;
				ID3D11UnorderedAccessView* UAV() const;
				ID3D11RenderTargetView* RenderTarget() const override;
			};
		}
	}
}