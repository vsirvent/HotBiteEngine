#include "DxcSupport.h"
#include "Log.h"

#include <d3dcompiler.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

namespace HotBite {
	namespace Engine {
		namespace Core {

			namespace {
				std::once_flag loaded;
				HMODULE dxc_module = nullptr;
				DxcCreateInstanceProc create_instance = nullptr;
				Microsoft::WRL::ComPtr<IDxcUtils> utils;

				void Load() {
					std::call_once(loaded, []() {
						dxc_module = LoadLibraryW(L"dxcompiler.dll");
						if (dxc_module == nullptr) {
							LOG_ERROR("DxcSupport: dxcompiler.dll was not found next to the executable (error %lu)",
								(unsigned long)GetLastError());
							return;
						}
						create_instance = (DxcCreateInstanceProc)GetProcAddress(dxc_module, "DxcCreateInstance");
						if (create_instance == nullptr ||
							FAILED(create_instance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)))) {
							LOG_ERROR("DxcSupport: dxcompiler.dll does not provide DxcUtils");
							utils.Reset();
						}
					});
				}
			}

			bool DxcSupport::Available() {
				Load();
				return utils != nullptr;
			}

			IDxcUtils* DxcSupport::Utils() {
				Load();
				return utils.Get();
			}

			HRESULT DxcSupport::CreateCompiler(IDxcCompiler3** out) {
				Load();
				if (create_instance == nullptr) {
					return E_FAIL;
				}
				return create_instance(CLSID_DxcCompiler, IID_PPV_ARGS(out));
			}

			namespace {
				// True if a DXBC container carries a DXIL part, i.e. it was made by DXC (an fxc container holds SHEX/SHDR).
				bool IsDxil(const void* bytecode, size_t size) {
					const uint8_t* bytes = (const uint8_t*)bytecode;
					if (size < 32 || memcmp(bytes, "DXBC", 4) != 0) {
						return false;
					}
					uint32_t part_count = 0;
					memcpy(&part_count, bytes + 28, sizeof(part_count));
					for (uint32_t i = 0; i < part_count && 32 + (size_t)i * 4 + 4 <= size; ++i) {
						uint32_t offset = 0;
						memcpy(&offset, bytes + 32 + (size_t)i * 4, sizeof(offset));
						if ((size_t)offset + 8 <= size && memcmp(bytes + offset, "DXIL", 4) == 0) {
							return true;
						}
					}
					return false;
				}
			}

			namespace {
				//The registers the resources of a DXIL shader really have, from the pipeline state validation part (PSV0) of its
				//container. DXC's reflection gets this wrong for a resource declared without `register(tN)` in a shader that also
				//has explicit ones (it numbers the implicit ones from 1, ignoring what the explicit ones take), so a name looked up
				//through it - "bind PointShadowMapTexture" - lands on the wrong register and the shader reads whatever is there.
				//The PSV lists the resources class by class (constant buffers, samplers, SRVs, UAVs), each in resource-id order,
				//which is the `uID` the reflection hands out.
				struct PsvBinding {
					UINT space = 0;
					UINT lower_bound = 0;
				};

				struct PsvBindings {
					std::vector<PsvBinding> cbv, sampler, srv, uav;
					bool valid = false;
				};

				PsvBindings ReadPsvBindings(const void* bytecode, size_t size) {
					PsvBindings out;
					const uint8_t* bytes = (const uint8_t*)bytecode;
					if (size < 32 || memcmp(bytes, "DXBC", 4) != 0) {
						return out;
					}
					uint32_t part_count = 0;
					memcpy(&part_count, bytes + 28, sizeof(part_count));
					for (uint32_t i = 0; i < part_count && 32 + (size_t)i * 4 + 4 <= size; ++i) {
						uint32_t offset = 0;
						memcpy(&offset, bytes + 32 + (size_t)i * 4, sizeof(offset));
						if ((size_t)offset + 8 > size || memcmp(bytes + offset, "PSV0", 4) != 0) {
							continue;
						}
						uint32_t part_size = 0;
						memcpy(&part_size, bytes + offset + 4, sizeof(part_size));
						const uint8_t* p = bytes + offset + 8;
						const uint8_t* end = p + part_size;
						if (end > bytes + size || p + 4 > end) {
							return out;
						}
						uint32_t runtime_info_size = 0;
						memcpy(&runtime_info_size, p, sizeof(runtime_info_size));
						p += 4 + runtime_info_size;
						if (p + 4 > end) {
							return out;
						}
						uint32_t count = 0;
						memcpy(&count, p, sizeof(count));
						p += 4;
						out.valid = true;
						if (count == 0) {
							return out;
						}
						if (p + 4 > end) {
							out.valid = false;
							return out;
						}
						uint32_t bind_size = 0;
						memcpy(&bind_size, p, sizeof(bind_size));
						p += 4;
						if (bind_size < 16 || p + (size_t)count * bind_size > end) {
							out.valid = false;
							return out;
						}
						for (uint32_t r = 0; r < count; ++r) {
							uint32_t type = 0;
							PsvBinding binding;
							memcpy(&type, p + (size_t)r * bind_size, sizeof(type));
							memcpy(&binding.space, p + (size_t)r * bind_size + 4, sizeof(UINT));
							memcpy(&binding.lower_bound, p + (size_t)r * bind_size + 8, sizeof(UINT));
							if (type == 1) { out.sampler.push_back(binding); }
							else if (type == 2) { out.cbv.push_back(binding); }
							else if (type >= 3 && type <= 5) { out.srv.push_back(binding); }
							else if (type >= 6 && type <= 9) { out.uav.push_back(binding); }
						}
						return out;
					}
					return out;
				}

				//The reflection DXC hands out, with the register of every resource put right (see above); everything else is
				//forwarded as it is.
				class PatchedShaderReflection final : public ID3D12ShaderReflection {
				public:
					PatchedShaderReflection(ID3D12ShaderReflection* wrapped, PsvBindings&& psv) : inner(wrapped), bindings(std::move(psv)) {
					}

					HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
						if (riid == __uuidof(ID3D12ShaderReflection) || riid == __uuidof(IUnknown)) {
							*object = static_cast<ID3D12ShaderReflection*>(this);
							AddRef();
							return S_OK;
						}
						*object = nullptr;
						return E_NOINTERFACE;
					}
					ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs); }
					ULONG STDMETHODCALLTYPE Release() override {
						const ULONG left = (ULONG)InterlockedDecrement(&refs);
						if (left == 0) {
							delete this;
						}
						return left;
					}

					HRESULT STDMETHODCALLTYPE GetDesc(D3D12_SHADER_DESC* desc) override { return inner->GetDesc(desc); }
					ID3D12ShaderReflectionConstantBuffer* STDMETHODCALLTYPE GetConstantBufferByIndex(UINT index) override { return inner->GetConstantBufferByIndex(index); }
					ID3D12ShaderReflectionConstantBuffer* STDMETHODCALLTYPE GetConstantBufferByName(LPCSTR name) override { return inner->GetConstantBufferByName(name); }
					HRESULT STDMETHODCALLTYPE GetResourceBindingDesc(UINT index, D3D12_SHADER_INPUT_BIND_DESC* desc) override {
						const HRESULT hr = inner->GetResourceBindingDesc(index, desc);
						if (SUCCEEDED(hr)) {
							Patch(*desc);
						}
						return hr;
					}
					HRESULT STDMETHODCALLTYPE GetInputParameterDesc(UINT index, D3D12_SIGNATURE_PARAMETER_DESC* desc) override { return inner->GetInputParameterDesc(index, desc); }
					HRESULT STDMETHODCALLTYPE GetOutputParameterDesc(UINT index, D3D12_SIGNATURE_PARAMETER_DESC* desc) override { return inner->GetOutputParameterDesc(index, desc); }
					HRESULT STDMETHODCALLTYPE GetPatchConstantParameterDesc(UINT index, D3D12_SIGNATURE_PARAMETER_DESC* desc) override { return inner->GetPatchConstantParameterDesc(index, desc); }
					ID3D12ShaderReflectionVariable* STDMETHODCALLTYPE GetVariableByName(LPCSTR name) override { return inner->GetVariableByName(name); }
					HRESULT STDMETHODCALLTYPE GetResourceBindingDescByName(LPCSTR name, D3D12_SHADER_INPUT_BIND_DESC* desc) override {
						const HRESULT hr = inner->GetResourceBindingDescByName(name, desc);
						if (SUCCEEDED(hr)) {
							Patch(*desc);
						}
						return hr;
					}
					UINT STDMETHODCALLTYPE GetMovInstructionCount() override { return inner->GetMovInstructionCount(); }
					UINT STDMETHODCALLTYPE GetMovcInstructionCount() override { return inner->GetMovcInstructionCount(); }
					UINT STDMETHODCALLTYPE GetConversionInstructionCount() override { return inner->GetConversionInstructionCount(); }
					UINT STDMETHODCALLTYPE GetBitwiseInstructionCount() override { return inner->GetBitwiseInstructionCount(); }
					D3D_PRIMITIVE STDMETHODCALLTYPE GetGSInputPrimitive() override { return inner->GetGSInputPrimitive(); }
					BOOL STDMETHODCALLTYPE IsSampleFrequencyShader() override { return inner->IsSampleFrequencyShader(); }
					UINT STDMETHODCALLTYPE GetNumInterfaceSlots() override { return inner->GetNumInterfaceSlots(); }
					HRESULT STDMETHODCALLTYPE GetMinFeatureLevel(enum D3D_FEATURE_LEVEL* level) override { return inner->GetMinFeatureLevel(level); }
					UINT STDMETHODCALLTYPE GetThreadGroupSize(UINT* x, UINT* y, UINT* z) override { return inner->GetThreadGroupSize(x, y, z); }
					UINT64 STDMETHODCALLTYPE GetRequiresFlags() override { return inner->GetRequiresFlags(); }

				private:
					void Patch(D3D12_SHADER_INPUT_BIND_DESC& desc) const {
						const std::vector<PsvBinding>* list = nullptr;
						switch (desc.Type) {
						case D3D_SIT_CBUFFER: list = &bindings.cbv; break;
						case D3D_SIT_SAMPLER: list = &bindings.sampler; break;
						case D3D_SIT_TEXTURE:
						case D3D_SIT_TBUFFER:
						case D3D_SIT_STRUCTURED:
						case D3D_SIT_BYTEADDRESS: list = &bindings.srv; break;
						case D3D_SIT_UAV_RWTYPED:
						case D3D_SIT_UAV_RWSTRUCTURED:
						case D3D_SIT_UAV_RWBYTEADDRESS:
						case D3D_SIT_UAV_APPEND_STRUCTURED:
						case D3D_SIT_UAV_CONSUME_STRUCTURED:
						case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER: list = &bindings.uav; break;
						default: break;
					}
						if (list != nullptr && desc.uID < list->size()) {
							desc.BindPoint = (*list)[desc.uID].lower_bound;
							desc.Space = (*list)[desc.uID].space;
						}
					}

					LONG refs = 1;
					Microsoft::WRL::ComPtr<ID3D12ShaderReflection> inner;
					PsvBindings bindings;
				};
			}

			HRESULT DxcSupport::Reflect(const void* bytecode, size_t size, ID3D12ShaderReflection** out) {
				*out = nullptr;
				if (IsDxil(bytecode, size) && Available()) {
					DxcBuffer buffer = {};
					buffer.Ptr = bytecode;
					buffer.Size = size;
					buffer.Encoding = 0;
					Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
					if (SUCCEEDED(utils->CreateReflection(&buffer, IID_PPV_ARGS(&reflection)))) {
						PsvBindings psv = ReadPsvBindings(bytecode, size);
						if (psv.valid) {
							*out = new PatchedShaderReflection(reflection.Get(), std::move(psv));
							return S_OK;
						}
						*out = reflection.Detach();
						return S_OK;
					}
				}
				return D3DReflect(bytecode, size, IID_ID3D12ShaderReflection, (void**)out);
			}
		}
	}
}
