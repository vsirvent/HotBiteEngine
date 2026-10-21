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

#include <algorithm>
#include <cmath>

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
			namespace {
				D3D12_RESOURCE_DESC Texture2DDesc(int w, int h, int slices, int mips, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags) {
					D3D12_RESOURCE_DESC d = {};
					d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
					d.Alignment = 0;
					d.Width = (UINT64)w;
					d.Height = (UINT)h;
					d.DepthOrArraySize = (UINT16)slices;
					d.MipLevels = (UINT16)mips;
					d.Format = format;
					d.SampleDesc.Count = 1;
					d.SampleDesc.Quality = 0;
					d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
					d.Flags = flags;
					return d;
				}

				D3D12_CLEAR_VALUE DepthClear() {
					D3D12_CLEAR_VALUE cv = {};
					cv.Format = DXGI_FORMAT_D32_FLOAT;
					cv.DepthStencil.Depth = 1.0f;
					cv.DepthStencil.Stencil = 0;
					return cv;
				}
			}

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
				GfxDevice* device = DXCore::Get()->device;
				HRESULT hr = S_OK;
				init = true;
				//Six faces; typeless so the same memory is a depth target and a float texture.
				const D3D12_RESOURCE_DESC texDesc = Texture2DDesc(w, h, 6, 1, DXGI_FORMAT_R32_TYPELESS,
					D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
				const D3D12_CLEAR_VALUE clear = DepthClear();
				hr = device->CreateTexture2D(texDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, nullptr, 0, &cube_texture);
				if (hr != S_OK)
				{
					return hr;
				}
				//The Shader Resource view description
				D3D12_SHADER_RESOURCE_VIEW_DESC SMViewDesc = {};
				SMViewDesc.Format = DXGI_FORMAT_R32_FLOAT;
				SMViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
				SMViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				SMViewDesc.TextureCube.MipLevels = 1;
				SMViewDesc.TextureCube.MostDetailedMip = 0;

				//If we have created the texture resource for the six faces
				//we create the Shader Resource View to use in our shaders.
				hr = device->CreateSRV(cube_texture, &SMViewDesc, &shader_resource_view);
				if (hr != S_OK)
				{
					return hr;
				}

				D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = { };
				dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
				dsvDesc.Texture2DArray.ArraySize = 6;
				dsvDesc.Texture2DArray.FirstArraySlice = 0;
				dsvDesc.Texture2DArray.MipSlice = 0;
				dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
				hr = device->CreateDSV(cube_texture, &dsvDesc, &depth_stencil_view);
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
					GfxContext* context = DXCore::Get()->context;
					context->ClearDepthStencilView(depth_stencil_view, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);
				}
			}

			GfxSRV* DepthTextureCube::SRV() const {
				return shader_resource_view;
			}

			GfxDSV* DepthTextureCube::Depth() const {
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
				GfxDevice* device = DXCore::Get()->device;
				HRESULT hr = S_OK;
				init = true;
				width = w;
				height = h;
				//Description of each face
				if (w > MAX_TEXTURE_SIZE || h > MAX_TEXTURE_SIZE) {
					return E_FAIL;
				}
				const D3D12_RESOURCE_DESC texDesc = Texture2DDesc(w, h, 1, 1, DXGI_FORMAT_R32_TYPELESS,
					D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
				const D3D12_CLEAR_VALUE clear = DepthClear();
				hr = device->CreateTexture2D(texDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, nullptr, 0, &texture);
				if (hr != S_OK)
				{
					return hr;
				}
				//The Shader Resource view description
				D3D12_SHADER_RESOURCE_VIEW_DESC SMViewDesc = {};
				SMViewDesc.Format = DXGI_FORMAT_R32_FLOAT;
				SMViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				SMViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				SMViewDesc.Texture2D.MipLevels = 1;
				SMViewDesc.Texture2D.MostDetailedMip = 0;

				hr = device->CreateSRV(texture, &SMViewDesc, &shader_resource_view);
				if (hr != S_OK)
				{
					return hr;
				}

				D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = { };
				dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
				dsvDesc.Texture2D.MipSlice = 0;
				dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
				hr = device->CreateDSV(texture, &dsvDesc, &depth_stencil_view);
				return hr;
			}

			void DepthTexture2D::Release() {
				ReleaseVariants();
				UnregisterScaled();
				scaled = false;
				if (texture) {
					if (depth_stencil_view) { depth_stencil_view->Release(); }
					if (shader_resource_view) { shader_resource_view->Release(); }
					texture->Release();

					depth_stencil_view = nullptr;
					shader_resource_view = nullptr;
					texture = nullptr;
				}
				init = false;
			}

			void DepthTexture2D::Clear() {
				if (texture) {
					GfxContext* context = DXCore::Get()->context;
					context->ClearDepthStencilView(depth_stencil_view, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);
				}
			}

			GfxSRV* DepthTexture2D::SRV() const {
				return shader_resource_view;
			}

			GfxDSV* DepthTexture2D::Depth() const {
				return depth_stencil_view;
			}

			DepthTexture2DArray::DepthTexture2DArray() {
			}

			DepthTexture2DArray::DepthTexture2DArray(const DepthTexture2DArray& other) {
				assert(!other.init && "DepthTexture2DArray can't be copied once initialized.");
				*this = other;
			}

			DepthTexture2DArray::~DepthTexture2DArray() {
				Release();
			}

			int DepthTexture2DArray::Width() const {
				return width;
			}

			int DepthTexture2DArray::Height() const {
				return height;
			}

			int DepthTexture2DArray::Slices() const {
				return slices;
			}

			HRESULT DepthTexture2DArray::Init(int w, int h, int n) {
				Release();
				GfxDevice* device = DXCore::Get()->device;
				HRESULT hr = S_OK;
				if (w > MAX_TEXTURE_SIZE || h > MAX_TEXTURE_SIZE || n < 1 ||
					n > D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION) {
					return E_FAIL;
				}
				init = true;
				width = w;
				height = h;
				slices = n;

				const D3D12_RESOURCE_DESC texDesc = Texture2DDesc(w, h, n, 1, DXGI_FORMAT_R32_TYPELESS,
					D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
				const D3D12_CLEAR_VALUE clear = DepthClear();
				hr = device->CreateTexture2D(texDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, nullptr, 0, &texture);
				if (FAILED(hr)) {
					Release();
					return hr;
				}

				D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
				srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
				srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srvDesc.Texture2DArray.MostDetailedMip = 0;
				srvDesc.Texture2DArray.MipLevels = 1;
				srvDesc.Texture2DArray.FirstArraySlice = 0;
				srvDesc.Texture2DArray.ArraySize = (UINT)n;
				hr = device->CreateSRV(texture, &srvDesc, &shader_resource_view);
				if (FAILED(hr)) {
					Release();
					return hr;
				}

				//The whole-array view: the shadow geometry shader picks the slice per
				//triangle, so every cascade is filled through this one binding.
				D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
				dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
				dsvDesc.Texture2DArray.MipSlice = 0;
				dsvDesc.Texture2DArray.FirstArraySlice = 0;
				dsvDesc.Texture2DArray.ArraySize = (UINT)n;
				hr = device->CreateDSV(texture, &dsvDesc, &depth_stencil_view);
				if (FAILED(hr)) {
					Release();
					return hr;
				}

				slice_views.resize(n, nullptr);
				for (int i = 0; i < n; ++i) {
					dsvDesc.Texture2DArray.FirstArraySlice = (UINT)i;
					dsvDesc.Texture2DArray.ArraySize = 1;
					hr = device->CreateDSV(texture, &dsvDesc, &slice_views[i]);
					if (FAILED(hr)) {
						Release();
						return hr;
					}
				}
				return hr;
			}

			void DepthTexture2DArray::Release() {
				for (GfxDSV* v : slice_views) {
					if (v != nullptr) {
						v->Release();
					}
				}
				slice_views.clear();
				if (depth_stencil_view) {
					depth_stencil_view->Release();
					depth_stencil_view = nullptr;
				}
				if (shader_resource_view) {
					shader_resource_view->Release();
					shader_resource_view = nullptr;
				}
				if (texture) {
					texture->Release();
					texture = nullptr;
				}
				width = 0;
				height = 0;
				slices = 0;
				init = false;
			}

			void DepthTexture2DArray::Clear() {
				if (texture) {
					GfxContext* context = DXCore::Get()->context;
					context->ClearDepthStencilView(depth_stencil_view,
						D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);
				}
			}

			GfxSRV* DepthTexture2DArray::SRV() const {
				return shader_resource_view;
			}

			GfxDSV* DepthTexture2DArray::Depth() const {
				return depth_stencil_view;
			}

			GfxDSV* DepthTexture2DArray::Depth(int slice) const {
				if (slice < 0 || slice >= (int)slice_views.size()) {
					return nullptr;
				}
				return slice_views[slice];
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
				GfxDevice* device = DXCore::Get()->device;
				HRESULT hr = S_OK;
				init = true;

				width = w;
				height = h;
				//Description of each face
				if (w > MAX_TEXTURE_SIZE || h > MAX_TEXTURE_SIZE) {
					return E_FAIL;
				}
				//One mip, as it always was (the mip count asked for was overwritten, so the chain was never made).
				D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
				if (bind_flags & GFX_BIND_UNORDERED_ACCESS) {
					flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
				}
				tex_desc = Texture2DDesc(w, h, 1, 1, format, flags);
				if (data != nullptr && len > 0) {
					GfxSubresourceData src_data;
					src_data.data = data;
					src_data.row_pitch = (UINT)((size_t)w * BitsPerPixel(format) / 8);
					src_data.slice_pitch = 0;
					hr = device->CreateTexture2D(tex_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, &src_data, 1, &texture);
				}
				else {
					//Never drawn into or read yet: the first use moves it where it has to be.
					hr = device->CreateTexture2D(tex_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, nullptr, 0, &texture);
				}
				if (hr != S_OK)
				{
					return hr;
				}

				//The Shader Resource view description
				D3D12_SHADER_RESOURCE_VIEW_DESC SMViewDesc = {};
				SMViewDesc.Format = format;
				SMViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				SMViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				SMViewDesc.Texture2D.MipLevels = 1;
				SMViewDesc.Texture2D.MostDetailedMip = 0;

				hr = device->CreateSRV(texture, &SMViewDesc, &shader_resource_view);
				if (hr != S_OK)
				{
					return hr;
				}
				if (bind_flags & GFX_BIND_UNORDERED_ACCESS) {
					device->CreateUAV(texture, nullptr, &uav);
				}
				D3D12_RENDER_TARGET_VIEW_DESC renderTargetViewDesc = {};
				// Setup the description of the render target view.
				renderTargetViewDesc.Format = format;
				renderTargetViewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
				renderTargetViewDesc.Texture2D.MipSlice = 0;
				// Create the render target view.
				hr = device->CreateRTV(texture, &renderTargetViewDesc, &render_view);

				return hr;
			}

			DynamicTexture2D::~DynamicTexture2D() {
				Release();
			}

			HRESULT DynamicTexture2D::Init(int w, int h, TextureFormat texture_format) {
				Release();
				if (w <= 0 || h <= 0 || w > MAX_TEXTURE_SIZE || h > MAX_TEXTURE_SIZE) {
					return E_INVALIDARG;
				}
				const DXGI_FORMAT dxgi = texture_format == TextureFormat::R8 ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
				GfxDevice* device = DXCore::Get()->device;
				const D3D12_RESOURCE_DESC desc = Texture2DDesc(w, h, 1, 1, dxgi, D3D12_RESOURCE_FLAG_NONE);
				HRESULT hr = device->CreateTexture2D(desc, D3D12_RESOURCE_STATE_COMMON, nullptr, nullptr, 0, &texture);
				if (FAILED(hr)) {
					texture = nullptr;
					return hr;
				}
				hr = device->CreateSRV(texture, nullptr, &shader_resource_view);
				if (FAILED(hr)) {
					shader_resource_view = nullptr;
					Release();
					return hr;
				}
				width = w;
				height = h;
				format = texture_format;
				return S_OK;
			}

			void DynamicTexture2D::Update(const void* pixels, uint32_t row_pitch) {
				if (texture == nullptr || pixels == nullptr) {
					return;
				}
				if (row_pitch == 0) {
					row_pitch = (uint32_t)width * (format == TextureFormat::R8 ? 1u : 4u);
				}
				DXCore::Get()->context->UpdateTexture(texture, 0, pixels, row_pitch);
			}

			void DynamicTexture2D::Release() {
				if (shader_resource_view != nullptr) {
					shader_resource_view->Release();
					shader_resource_view = nullptr;
				}
				if (texture != nullptr) {
					texture->Release();
					texture = nullptr;
				}
				width = 0;
				height = 0;
			}

			void RenderTexture2D::Release() {
				ReleaseVariants();
				UnregisterScaled();
				scaled = false;
				if (texture) {
					texture->Release();
					if (shader_resource_view) { shader_resource_view->Release(); }
					if (render_view) { render_view->Release(); }
					if (uav != nullptr) {
						uav->Release();
						uav = nullptr;
					}
					texture = nullptr;
					shader_resource_view = nullptr;
					render_view = nullptr;
				}
				init = false;
			}

			void RenderTexture2D::Clear(const float color[4]) {
				if (texture) {
					GfxContext* context = DXCore::Get()->context;
					context->ClearRenderTargetView(RenderTarget(), color);
				}
			}

			const D3D12_RESOURCE_DESC& RenderTexture2D::Descriptor() const {
				return tex_desc;
			}

			GfxTexture* RenderTexture2D::Texture() const {
				return texture;
			}

			GfxResource* RenderTexture2D::Resource() const {
				return texture;
			}

			GfxSRV* RenderTexture2D::SRV() const {
				return shader_resource_view;
			}

			GfxUAV* RenderTexture2D::UAV() const {
				return uav;
			}

			GfxRTV* RenderTexture2D::RenderTarget() const {
				return render_view;
			}

			//-------------------------------------------------------------------------------------------
			// Dynamic resolution: the textures that follow the render scale
			//-------------------------------------------------------------------------------------------

			ScaledResource::~ScaledResource() {
				UnregisterScaled();
			}

			void ScaledResource::RegisterScaled() {
				if (!registered) {
					RenderScale::Get().Add(this);
					registered = true;
				}
			}

			void ScaledResource::UnregisterScaled() {
				if (registered) {
					RenderScale::Get().Remove(this);
					registered = false;
				}
			}

			void ScaledResource::SetNative(bool native) {
				pinned_native = native;
				//Pinned after the sizes were made: back to the native one now, and it stays there.
				if (native && RenderScale::Get().Built()) {
					SelectStep(0);
				}
			}

			RenderScale& RenderScale::Get() {
				//Never destroyed: a texture that outlives the static destructors still unregisters from it.
				static RenderScale* instance = new RenderScale();
				return *instance;
			}

			void RenderScale::SetNativeSize(int w, int h) {
				std::lock_guard<std::recursive_mutex> lock(mutex);
				native_w = w;
				native_h = h;
				if (!built) {
					width = w;
					height = h;
				}
			}

			float RenderScale::StepScale(int s) const {
				return s <= 0 ? 1.0f : std::pow(STEP_RATIO, (float)s);
			}

			void RenderScale::StepSize(int s, int& w, int& h) const {
				if (s <= 0) {
					w = native_w;
					h = native_h;
					return;
				}
				const float f = StepScale(s);
				w = (std::max)(16, (int)((float)native_w * f + 0.5f) & ~1);
				h = (std::max)(16, (int)((float)native_h * f + 0.5f) & ~1);
			}

			int RenderScale::StepFor(float scale) const {
				for (int s = STEPS - 1; s > 0; --s) {
					if (StepScale(s) >= scale - 0.001f) {
						return s;
					}
				}
				return 0;
			}

			int RenderScale::StepAtMost(float scale) const {
				for (int s = 0; s < STEPS - 1; ++s) {
					if (StepScale(s) <= scale + 0.001f) {
						return s;
					}
				}
				return STEPS - 1;
			}

			void RenderScale::Add(ScaledResource* resource) {
				std::lock_guard<std::recursive_mutex> lock(mutex);
				resources.push_back(resource);
			}

			void RenderScale::Remove(ScaledResource* resource) {
				std::lock_guard<std::recursive_mutex> lock(mutex);
				resources.erase(std::remove(resources.begin(), resources.end(), resource), resources.end());
			}

			bool RenderScale::Build() {
				std::lock_guard<std::recursive_mutex> lock(mutex);
				if (built) {
					return true;
				}
				if (native_w <= 0 || native_h <= 0) {
					SetNativeSize(DXCore::Get()->GetWidth(), DXCore::Get()->GetHeight());
				}
				sizes.clear();
				for (int s = 0; s < STEPS; ++s) {
					int w = 0;
					int h = 0;
					StepSize(s, w, h);
					sizes.emplace_back(w, h);
				}
				//Set before the textures are made: they ask for the step in use.
				built = true;
				step = 0;
				width = native_w;
				height = native_h;
				bool ok = true;
				for (ScaledResource* r : resources) {
					if (FAILED(r->BuildSteps(sizes))) {
						ok = false;
					}
				}
				if (!ok) {
					//What was made stays at step 0, the native size, which is how it was; nothing can switch.
					LOG_ERROR("RenderScale: a texture could not be made in every size, dynamic resolution is not available");
					for (ScaledResource* r : resources) {
						r->SelectStep(0);
					}
					built = false;
				}
				++generation;
				return ok;
			}

			void RenderScale::Select(int s) {
				std::lock_guard<std::recursive_mutex> lock(mutex);
				if (!built) {
					return;
				}
				s = (std::min)((std::max)(s, 0), STEPS - 1);
				for (ScaledResource* r : resources) {
					if (!r->IsNative()) {
						r->SelectStep(s);
					}
				}
				step = s;
				StepSize(s, width, height);
				++generation;
			}

			//The size a texture takes when the frame is iw x ih.
			void RenderTexture2D::SizeAt(int iw, int ih, int& w, int& h) const {
				if (scaled_size) {
					scaled_size(iw, ih, w, h);
				}
				else {
					const RenderScale& rs = RenderScale::Get();
					const float fx = (float)iw / (float)(std::max)(rs.NativeWidth(), 1);
					const float fy = (float)ih / (float)(std::max)(rs.NativeHeight(), 1);
					w = (int)((float)base_w * fx + 0.5f);
					h = (int)((float)base_h * fy + 0.5f);
				}
				w = (std::max)(w, 1);
				h = (std::max)(h, 1);
			}

			HRESULT RenderTexture2D::InitScaled(int w, int h, DXGI_FORMAT format, uint32_t bind_flags, ScaledSizeFn size) {
				RenderScale& rs = RenderScale::Get();
				if (rs.NativeWidth() <= 0) {
					rs.SetNativeSize(DXCore::Get()->GetWidth(), DXCore::Get()->GetHeight());
				}
				HRESULT hr = Init(w, h, format, nullptr, 0, bind_flags);
				if (FAILED(hr)) {
					return hr;
				}
				scaled = true;
				base_w = w;
				base_h = h;
				scaled_format = format;
				scaled_bind_flags = bind_flags;
				scaled_size = size;
				RegisterScaled();
				if (rs.Built() && !pinned_native) {
					hr = BuildSteps(rs.Sizes());
				}
				return hr;
			}

			HRESULT RenderTexture2D::BuildSteps(const std::vector<std::pair<int, int>>& screen_sizes) {
				if (!scaled || !init || pinned_native || !variants.empty() || screen_sizes.empty()) {
					return S_OK;
				}
				GfxDevice* device = DXCore::Get()->device;
				D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
				if (scaled_bind_flags & GFX_BIND_UNORDERED_ACCESS) {
					flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
				}
				const UINT count = (UINT)screen_sizes.size();
				std::vector<D3D12_RESOURCE_DESC> descs(count);
				std::vector<GfxTexture*> textures(count, nullptr);
				std::vector<Variant> made(count);
				for (UINT i = 0; i < count; ++i) {
					SizeAt(screen_sizes[i].first, screen_sizes[i].second, made[i].w, made[i].h);
					if (made[i].w > MAX_TEXTURE_SIZE || made[i].h > MAX_TEXTURE_SIZE) {
						return E_FAIL;
					}
					descs[i] = Texture2DDesc(made[i].w, made[i].h, 1, 1, scaled_format, flags);
				}
				HRESULT hr = device->CreateAliasedTextures2D(descs.data(), count, textures.data());
				if (FAILED(hr)) {
					return hr;
				}
				for (UINT i = 0; i < count && SUCCEEDED(hr); ++i) {
					made[i].texture = textures[i];
					D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
					srv_desc.Format = scaled_format;
					srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
					srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
					srv_desc.Texture2D.MipLevels = 1;
					srv_desc.Texture2D.MostDetailedMip = 0;
					hr = device->CreateSRV(textures[i], &srv_desc, &made[i].srv);
					if (SUCCEEDED(hr) && (scaled_bind_flags & GFX_BIND_UNORDERED_ACCESS)) {
						hr = device->CreateUAV(textures[i], nullptr, &made[i].uav);
					}
					if (SUCCEEDED(hr)) {
						D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
						rtv_desc.Format = scaled_format;
						rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
						rtv_desc.Texture2D.MipSlice = 0;
						hr = device->CreateRTV(textures[i], &rtv_desc, &made[i].rtv);
					}
				}
				if (FAILED(hr)) {
					for (Variant& v : made) {
						if (v.srv) { v.srv->Release(); }
						if (v.rtv) { v.rtv->Release(); }
						if (v.uav) { v.uav->Release(); }
						if (v.texture) { v.texture->Release(); }
					}
					return hr;
				}
				//The texture made by Init gives way to the ones that share their memory.
				if (texture) {
					if (shader_resource_view) { shader_resource_view->Release(); }
					if (render_view) { render_view->Release(); }
					if (uav) { uav->Release(); }
					texture->Release();
				}
				texture = nullptr;
				shader_resource_view = nullptr;
				render_view = nullptr;
				uav = nullptr;
				variants = std::move(made);
				active_step = -1;
				SelectStep((std::min)((std::max)(RenderScale::Get().Step(), 0), (int)variants.size() - 1));
				return S_OK;
			}

			void RenderTexture2D::SelectStep(int step) {
				if (variants.empty()) {
					return;
				}
				step = (std::min)((std::max)(step, 0), (int)variants.size() - 1);
				GfxContext* context = DXCore::Get()->context;
				GfxTexture* from = (active_step >= 0 && active_step < (int)variants.size()) ? variants[active_step].texture : nullptr;
				const Variant& v = variants[step];
				context->SwitchAliased(from, v.texture);
				texture = v.texture;
				shader_resource_view = v.srv;
				render_view = v.rtv;
				uav = v.uav;
				width = v.w;
				height = v.h;
				tex_desc = v.texture->desc;
				active_step = step;
				//What the memory held belonged to the size before; zero is also what the temporal filters start from.
				static const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				context->ClearRenderTargetView(render_view, zero);
			}

			void RenderTexture2D::ReleaseVariants() {
				if (variants.empty()) {
					return;
				}
				for (Variant& v : variants) {
					if (v.srv) { v.srv->Release(); }
					if (v.rtv) { v.rtv->Release(); }
					if (v.uav) { v.uav->Release(); }
					if (v.texture) { v.texture->Release(); }
				}
				variants.clear();
				active_step = 0;
				texture = nullptr;
				shader_resource_view = nullptr;
				render_view = nullptr;
				uav = nullptr;
			}

			void DepthTexture2D::SizeAt(int iw, int ih, int& w, int& h) const {
				if (scaled_size) {
					scaled_size(iw, ih, w, h);
				}
				else {
					const RenderScale& rs = RenderScale::Get();
					const float fx = (float)iw / (float)(std::max)(rs.NativeWidth(), 1);
					const float fy = (float)ih / (float)(std::max)(rs.NativeHeight(), 1);
					w = (int)((float)base_w * fx + 0.5f);
					h = (int)((float)base_h * fy + 0.5f);
				}
				w = (std::max)(w, 1);
				h = (std::max)(h, 1);
			}

			HRESULT DepthTexture2D::InitScaled(int w, int h, ScaledSizeFn size) {
				RenderScale& rs = RenderScale::Get();
				if (rs.NativeWidth() <= 0) {
					rs.SetNativeSize(DXCore::Get()->GetWidth(), DXCore::Get()->GetHeight());
				}
				HRESULT hr = Init(w, h);
				if (FAILED(hr)) {
					return hr;
				}
				scaled = true;
				base_w = w;
				base_h = h;
				scaled_size = size;
				RegisterScaled();
				if (rs.Built() && !pinned_native) {
					hr = BuildSteps(rs.Sizes());
				}
				return hr;
			}

			HRESULT DepthTexture2D::BuildSteps(const std::vector<std::pair<int, int>>& screen_sizes) {
				if (!scaled || !init || pinned_native || !variants.empty() || screen_sizes.empty()) {
					return S_OK;
				}
				GfxDevice* device = DXCore::Get()->device;
				const UINT count = (UINT)screen_sizes.size();
				std::vector<D3D12_RESOURCE_DESC> descs(count);
				std::vector<GfxTexture*> textures(count, nullptr);
				std::vector<Variant> made(count);
				for (UINT i = 0; i < count; ++i) {
					SizeAt(screen_sizes[i].first, screen_sizes[i].second, made[i].w, made[i].h);
					if (made[i].w > MAX_TEXTURE_SIZE || made[i].h > MAX_TEXTURE_SIZE) {
						return E_FAIL;
					}
					descs[i] = Texture2DDesc(made[i].w, made[i].h, 1, 1, DXGI_FORMAT_R32_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
				}
				HRESULT hr = device->CreateAliasedTextures2D(descs.data(), count, textures.data());
				if (FAILED(hr)) {
					return hr;
				}
				for (UINT i = 0; i < count && SUCCEEDED(hr); ++i) {
					made[i].texture = textures[i];
					D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
					srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
					srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
					srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
					srv_desc.Texture2D.MipLevels = 1;
					srv_desc.Texture2D.MostDetailedMip = 0;
					hr = device->CreateSRV(textures[i], &srv_desc, &made[i].srv);
					if (SUCCEEDED(hr)) {
						D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
						dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
						dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
						dsv_desc.Texture2D.MipSlice = 0;
						dsv_desc.Flags = D3D12_DSV_FLAG_NONE;
						hr = device->CreateDSV(textures[i], &dsv_desc, &made[i].dsv);
					}
				}
				if (FAILED(hr)) {
					for (Variant& v : made) {
						if (v.srv) { v.srv->Release(); }
						if (v.dsv) { v.dsv->Release(); }
						if (v.texture) { v.texture->Release(); }
					}
					return hr;
				}
				if (texture) {
					if (depth_stencil_view) { depth_stencil_view->Release(); }
					if (shader_resource_view) { shader_resource_view->Release(); }
					texture->Release();
				}
				texture = nullptr;
				shader_resource_view = nullptr;
				depth_stencil_view = nullptr;
				variants = std::move(made);
				active_step = -1;
				SelectStep((std::min)((std::max)(RenderScale::Get().Step(), 0), (int)variants.size() - 1));
				return S_OK;
			}

			void DepthTexture2D::SelectStep(int step) {
				if (variants.empty()) {
					return;
				}
				step = (std::min)((std::max)(step, 0), (int)variants.size() - 1);
				GfxContext* context = DXCore::Get()->context;
				GfxTexture* from = (active_step >= 0 && active_step < (int)variants.size()) ? variants[active_step].texture : nullptr;
				const Variant& v = variants[step];
				context->SwitchAliased(from, v.texture);
				texture = v.texture;
				shader_resource_view = v.srv;
				depth_stencil_view = v.dsv;
				width = v.w;
				height = v.h;
				active_step = step;
				//A depth buffer must be cleared before it is used after its memory was somebody else's.
				context->ClearDepthStencilView(depth_stencil_view, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);
			}

			void DepthTexture2D::ReleaseVariants() {
				if (variants.empty()) {
					return;
				}
				for (Variant& v : variants) {
					if (v.srv) { v.srv->Release(); }
					if (v.dsv) { v.dsv->Release(); }
					if (v.texture) { v.texture->Release(); }
				}
				variants.clear();
				active_step = 0;
				texture = nullptr;
				shader_resource_view = nullptr;
				depth_stencil_view = nullptr;
			}
		}
	}
}
