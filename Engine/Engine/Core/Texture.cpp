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

#include "Texture.h"

//--------------------------------------------------------------------------------------
// Return the BPP for a particular format.
//--------------------------------------------------------------------------------------
static size_t BitsPerPixel(_In_ DXGI_FORMAT fmt)
{
	switch (fmt)
	{
	case DXGI_FORMAT_R32G32B32A32_TYPELESS:
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
	case DXGI_FORMAT_R32G32B32A32_UINT:
	case DXGI_FORMAT_R32G32B32A32_SINT:
		return 128;

	case DXGI_FORMAT_R32G32B32_TYPELESS:
	case DXGI_FORMAT_R32G32B32_FLOAT:
	case DXGI_FORMAT_R32G32B32_UINT:
	case DXGI_FORMAT_R32G32B32_SINT:
		return 96;

	case DXGI_FORMAT_R16G16B16A16_TYPELESS:
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
	case DXGI_FORMAT_R16G16B16A16_UNORM:
	case DXGI_FORMAT_R16G16B16A16_UINT:
	case DXGI_FORMAT_R16G16B16A16_SNORM:
	case DXGI_FORMAT_R16G16B16A16_SINT:
	case DXGI_FORMAT_R32G32_TYPELESS:
	case DXGI_FORMAT_R32G32_FLOAT:
	case DXGI_FORMAT_R32G32_UINT:
	case DXGI_FORMAT_R32G32_SINT:
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
		return 64;

	case DXGI_FORMAT_R10G10B10A2_TYPELESS:
	case DXGI_FORMAT_R10G10B10A2_UNORM:
	case DXGI_FORMAT_R10G10B10A2_UINT:
	case DXGI_FORMAT_R11G11B10_FLOAT:
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_R8G8B8A8_UINT:
	case DXGI_FORMAT_R8G8B8A8_SNORM:
	case DXGI_FORMAT_R8G8B8A8_SINT:
	case DXGI_FORMAT_R16G16_TYPELESS:
	case DXGI_FORMAT_R16G16_FLOAT:
	case DXGI_FORMAT_R16G16_UNORM:
	case DXGI_FORMAT_R16G16_UINT:
	case DXGI_FORMAT_R16G16_SNORM:
	case DXGI_FORMAT_R16G16_SINT:
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
	case DXGI_FORMAT_R32_UINT:
	case DXGI_FORMAT_R32_SINT:
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
	case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
	case DXGI_FORMAT_R8G8_B8G8_UNORM:
	case DXGI_FORMAT_G8R8_G8B8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8X8_TYPELESS:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		return 32;

	case DXGI_FORMAT_R8G8_TYPELESS:
	case DXGI_FORMAT_R8G8_UNORM:
	case DXGI_FORMAT_R8G8_UINT:
	case DXGI_FORMAT_R8G8_SNORM:
	case DXGI_FORMAT_R8G8_SINT:
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_R16_FLOAT:
	case DXGI_FORMAT_D16_UNORM:
	case DXGI_FORMAT_R16_UNORM:
	case DXGI_FORMAT_R16_UINT:
	case DXGI_FORMAT_R16_SNORM:
	case DXGI_FORMAT_R16_SINT:
	case DXGI_FORMAT_B5G6R5_UNORM:
	case DXGI_FORMAT_B5G5R5A1_UNORM:
	case DXGI_FORMAT_B4G4R4A4_UNORM:
		return 16;

	case DXGI_FORMAT_R8_TYPELESS:
	case DXGI_FORMAT_R8_UNORM:
	case DXGI_FORMAT_R8_UINT:
	case DXGI_FORMAT_R8_SNORM:
	case DXGI_FORMAT_R8_SINT:
	case DXGI_FORMAT_A8_UNORM:
		return 8;

	case DXGI_FORMAT_R1_UNORM:
		return 1;

	case DXGI_FORMAT_BC1_TYPELESS:
	case DXGI_FORMAT_BC1_UNORM:
	case DXGI_FORMAT_BC1_UNORM_SRGB:
	case DXGI_FORMAT_BC4_TYPELESS:
	case DXGI_FORMAT_BC4_UNORM:
	case DXGI_FORMAT_BC4_SNORM:
		return 4;

	case DXGI_FORMAT_BC2_TYPELESS:
	case DXGI_FORMAT_BC2_UNORM:
	case DXGI_FORMAT_BC2_UNORM_SRGB:
	case DXGI_FORMAT_BC3_TYPELESS:
	case DXGI_FORMAT_BC3_UNORM:
	case DXGI_FORMAT_BC3_UNORM_SRGB:
	case DXGI_FORMAT_BC5_TYPELESS:
	case DXGI_FORMAT_BC5_UNORM:
	case DXGI_FORMAT_BC5_SNORM:
	case DXGI_FORMAT_BC6H_TYPELESS:
	case DXGI_FORMAT_BC6H_UF16:
	case DXGI_FORMAT_BC6H_SF16:
	case DXGI_FORMAT_BC7_TYPELESS:
	case DXGI_FORMAT_BC7_UNORM:
	case DXGI_FORMAT_BC7_UNORM_SRGB:
		return 8;

	default:
		return 0;
	}
}

namespace HotBite {
	namespace Engine {
		namespace Core {
			DepthTextureCube::DepthTextureCube() {

			}

			DepthTextureCube::DepthTextureCube(const DepthTextureCube& other) {
				assert(!other.init && "DepthTextureCube can't be copied once initialized.");
				*this = other;
			}

			DepthTextureCube::~DepthTextureCube() {
				Release();
			}

			HRESULT DepthTextureCube::Init(int w, int h) {
				Release();
				ID3D11Device* device = DXCore::Get()->device;
				HRESULT hr = S_OK;
				init = true;
				//Description of each face
				D3D11_TEXTURE2D_DESC texDesc;
				texDesc.Width = w;
				texDesc.Height = h;
				texDesc.MipLevels = 1;
				texDesc.ArraySize = 6;
				texDesc.Format = DXGI_FORMAT_R32_TYPELESS;
				texDesc.CPUAccessFlags = 0;
				texDesc.SampleDesc.Count = 1;
				texDesc.SampleDesc.Quality = 0;
				texDesc.Usage = D3D11_USAGE_DEFAULT;
				texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
				texDesc.CPUAccessFlags = 0;
				texDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
				//Create the Texture Resource
				hr = device->CreateTexture2D(&texDesc, nullptr, &cube_texture);
				if (hr != S_OK)
				{
					return hr;
				}
				//The Shader Resource view description
				D3D11_SHADER_RESOURCE_VIEW_DESC SMViewDesc;
				SMViewDesc.Format = DXGI_FORMAT_R32_FLOAT;
				SMViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
				SMViewDesc.TextureCube.MipLevels = texDesc.MipLevels;
				SMViewDesc.TextureCube.MostDetailedMip = 0;

				//If we have created the texture resource for the six faces 
				//we create the Shader Resource View to use in our shaders.
				hr = device->CreateShaderResourceView(cube_texture, &SMViewDesc, &shader_resource_view);
				if (hr != S_OK)
				{
					return hr;
				}

				D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = { };
				dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
				dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
				dsvDesc.Texture2DArray.ArraySize = 6;
				dsvDesc.Texture2DArray.FirstArraySlice = 0;
				dsvDesc.Texture2DArray.MipSlice = 0;
				dsvDesc.Flags = 0;
				hr = device->CreateDepthStencilView(cube_texture, &dsvDesc, &depth_stencil_view);
				return hr;
			}

			void DepthTextureCube::Release() {
				if (cube_texture) {
					cube_texture->Release();
					cube_texture = nullptr;
				}
				if (depth_stencil_view) {
					depth_stencil_view->Release();
					depth_stencil_view = nullptr;
				}
				if (shader_resource_view) {
					shader_resource_view->Release();
					shader_resource_view = nullptr;
				}
				init = false;
			}

			void DepthTextureCube::Clear() {
				if (cube_texture) {
					ID3D11DeviceContext* context = DXCore::Get()->context;
					context->ClearDepthStencilView(depth_stencil_view, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
				}
			}

			ID3D11ShaderResourceView* DepthTextureCube::SRV() const {
				return shader_resource_view;
			}

			ID3D11DepthStencilView* DepthTextureCube::Depth() const {
				return depth_stencil_view;
			}

			DepthTexture2D::DepthTexture2D() {
			}

			DepthTexture2D::DepthTexture2D(const DepthTexture2D& other) {
				assert(!other.init && "DepthTexture2D can't be copied once initialized.");
				*this = other;
			}

			int DepthTexture2D::Width() const {
				return width;
			}

			int DepthTexture2D::Height() const {
				return height;
			}

			DepthTexture2D::~DepthTexture2D() {
				Release();
			}

			HRESULT DepthTexture2D::Init(int w, int h) {
				ID3D11Device* device = DXCore::Get()->device;
				HRESULT hr = S_OK;
				init = true;
				width = w;
				height = h;
				//Description of each face
				if (w > MAX_TEXTURE_SIZE || h > MAX_TEXTURE_SIZE) {
					return E_FAIL;
				}
				D3D11_TEXTURE2D_DESC texDesc = {};
				texDesc.Width = w;
				texDesc.Height = h;
				texDesc.MipLevels = 1;
				texDesc.ArraySize = 1;
				texDesc.Format = DXGI_FORMAT_R32_TYPELESS;
				texDesc.SampleDesc.Count = 1;
				texDesc.Usage = D3D11_USAGE_DEFAULT;
				texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
				//Create the Texture Resource
				hr = device->CreateTexture2D(&texDesc, nullptr, &texture);
				if (hr != S_OK)
				{
					return hr;
				}
				//The Shader Resource view description
				D3D11_SHADER_RESOURCE_VIEW_DESC SMViewDesc;
				SMViewDesc.Format = DXGI_FORMAT_R32_FLOAT;
				SMViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				SMViewDesc.TextureCube.MipLevels = texDesc.MipLevels;
				SMViewDesc.TextureCube.MostDetailedMip = 0;

				//If we have created the texture resource for the six faces 
				//we create the Shader Resource View to use in our shaders.
				hr = device->CreateShaderResourceView(texture, &SMViewDesc, &shader_resource_view);
				if (hr != S_OK)
				{
					return hr;
				}

				D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = { };
				dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
				dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
				dsvDesc.Texture2DArray.ArraySize = 1;
				dsvDesc.Texture2DArray.FirstArraySlice = 0;
				dsvDesc.Texture2DArray.MipSlice = 0;
				dsvDesc.Flags = 0;
				hr = device->CreateDepthStencilView(texture, &dsvDesc, &depth_stencil_view);
				return hr;
			}

			void DepthTexture2D::Release() {
				if (texture) {
					depth_stencil_view->Release();
					shader_resource_view->Release();
					texture->Release();
					
					depth_stencil_view = nullptr;
					shader_resource_view = nullptr;
					texture = nullptr;
				}
				init = false;
			}

			void DepthTexture2D::Clear() {
				if (texture) {
					ID3D11DeviceContext* context = DXCore::Get()->context;
					context->ClearDepthStencilView(depth_stencil_view, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
				}
			}

			ID3D11ShaderResourceView* DepthTexture2D::SRV() const {
				return shader_resource_view;
			}

			ID3D11DepthStencilView* DepthTexture2D::Depth() const {
				return depth_stencil_view;
			}

			RenderTexture2D::RenderTexture2D(int mip_levels) {
				this->mip_levels = mip_levels;
			}

			RenderTexture2D::RenderTexture2D(const RenderTexture2D& other) {
				assert(!other.init && "RenderTexture2D can't be copied once initialized.");
				*this = other;
			}

			RenderTexture2D::~RenderTexture2D() {
				Release();
			}

			int RenderTexture2D::Width() const {
				return width;
			}

			int RenderTexture2D::Height() const {
				return height;
			}

			HRESULT RenderTexture2D::Init(int w, int h, DXGI_FORMAT format, const uint8_t* data, uint32_t len, uint32_t bind_flags) {
				ID3D11Device* device = DXCore::Get()->device;
				HRESULT hr = S_OK;
				init = true;
				width = w;
				height = h;
				//Description of each face
				if (w > MAX_TEXTURE_SIZE || h > MAX_TEXTURE_SIZE) {
					return E_FAIL;
				}
				//Description of each face
				D3D11_TEXTURE2D_DESC texDesc = {};
				texDesc.Width = w;
				texDesc.Height = h;
				texDesc.MipLevels = mip_levels;
				texDesc.ArraySize = 1;
				texDesc.Format = format;
				texDesc.MipLevels = 1;
				texDesc.SampleDesc.Count = 1;
				texDesc.Usage = D3D11_USAGE_DEFAULT;
				texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | bind_flags;
				if (mip_levels > 1) {
					texDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
				}
				if (data != nullptr && len > 0) {
					D3D11_SUBRESOURCE_DATA src_data;
					src_data.pSysMem = data;
					src_data.SysMemPitch = (UINT)((size_t)w * BitsPerPixel(format)/8);
					src_data.SysMemSlicePitch = 0;
					//Create the Texture Resource
					hr = device->CreateTexture2D(&texDesc, &src_data, &texture);
				}
				else {
					//Create the Texture Resource
					hr = device->CreateTexture2D(&texDesc, nullptr, &texture);
				}
				if (hr != S_OK)
				{
					return hr;
				}

				texture->GetDesc(&tex_desc);
				//The Shader Resource view description
				D3D11_SHADER_RESOURCE_VIEW_DESC SMViewDesc;
				SMViewDesc.Format = texDesc.Format;
				SMViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				SMViewDesc.Texture2D.MipLevels = texDesc.MipLevels;
				SMViewDesc.Texture2D.MostDetailedMip = 0;

				//If we have created the texture resource for the six faces 
				//we create the Shader Resource View to use in our shaders.
				hr = device->CreateShaderResourceView(texture, &SMViewDesc, &shader_resource_view);
				if (hr != S_OK)
				{
					return hr;
				}
				shader_resource_view->GetResource(&resource);
				if (texDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) {
					device->CreateUnorderedAccessView(resource, nullptr, &uav);
				}
				D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
				// Setup the description of the render target view.
				renderTargetViewDesc.Format = texDesc.Format;
				renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
				renderTargetViewDesc.Texture2D.MipSlice = 0;
				// Create the render target view.
				hr = device->CreateRenderTargetView(texture, &renderTargetViewDesc, &render_view);
				
				return hr;
			}

			void RenderTexture2D::Release() {
				if (texture) {
					texture->Release();
					shader_resource_view->Release();
					render_view->Release();
					resource->Release();
					if (uav != nullptr) {
						uav->Release();
						uav = nullptr;
					}
					resource = nullptr;
					texture = nullptr;
					shader_resource_view = nullptr;
					render_view = nullptr;
				}
				init = false;
			}

			void RenderTexture2D::Clear(const float color[4]) {
				if (texture) {
					ID3D11DeviceContext* context = DXCore::Get()->context;
					context->ClearRenderTargetView(RenderTarget(), color);
				}
			}

			const D3D11_TEXTURE2D_DESC& RenderTexture2D::Descriptor() const {
				return tex_desc;
			}

			ID3D11Texture2D* RenderTexture2D::Texture() const {
				return texture;
			}

			ID3D11Resource* RenderTexture2D::Resource() const {
				return resource;
			}

			ID3D11ShaderResourceView* RenderTexture2D::SRV() const {
				return shader_resource_view;
			}

			ID3D11UnorderedAccessView* RenderTexture2D::UAV() const {
				return uav;
			}

			ID3D11RenderTargetView* RenderTexture2D::RenderTarget() const {
				return render_view;
			}
		}
	}
}