/// <summary>
/// By Chris Cascioli, Professor@RIT
/// </summary>
#pragma once
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")

#include "Defines.h"
#include "DXCore.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

#include <unordered_map>
#include <vector>
#include <string>

namespace HotBite {
	namespace Engine {
		namespace Core {

			struct SimpleShaderKeys {
				static std::string NJOINTS;
				static std::string SCREEN_W;
				static std::string SCREEN_H;
				static std::string DOP_ACTIVE;
				static std::string VERTEX_COLOR;
				static std::string UI_FLAGS;
				static std::string CLOUD_TEST;
				static std::string IS_SPACE;
				static std::string MULTI_TEXTURE_COUNT;
				static std::string DEPTH_TEXTURE;
				static std::string NEAR_FACTOR;
				static std::string FAR_FACTOR;
				static std::string PROJECTION_INVERSE;
			};

			struct SimpleShaderVariable
			{
				unsigned int ByteOffset;
				unsigned int Size;
				unsigned int ConstantBufferIndex;
			};

			struct SimpleConstantBuffer
			{
				std::string Name;
				unsigned int Size;
				unsigned int BindIndex;
				ID3D11Buffer* ConstantBuffer;
				unsigned char* LocalDataBuffer;
				std::vector<SimpleShaderVariable> Variables;
			};

			struct SimpleSRV
			{
				unsigned int Index;		// The raw index of the SRV
				unsigned int BindIndex; // The register of the SRV
			};

			struct SimpleSampler
			{
				unsigned int Index;		// The raw index of the Sampler
				unsigned int BindIndex; // The register of the Sampler
			};

			class ISimpleShader
			{
			public:
				ISimpleShader(ID3D11Device* device, ID3D11DeviceContext* context);
				virtual ~ISimpleShader();

				// Initialization method (since we can't invoke derived class
				// overrides in the base class constructor)
				bool LoadShaderFile(LPCWSTR shaderFile);
				bool Reload();

				// Simple helpers
				bool IsShaderValid() { return shaderValid; }

				// Activating the shader and copying data
				void SetShader();
				void CopyAllBufferData();
				void CopyBufferData(unsigned int index);
				void CopyBufferData(const std::string& bufferName);

				// Sets arbitrary shader data
				bool SetData(const std::string& name, const void* data, unsigned int size);

				bool SetInt(const std::string& name, int data);
				bool SetFloat(const std::string& name, float data);
				bool SetFloat2(const std::string& name, const float data[2]);
				bool SetFloat2(const std::string& name, const float2 data);
				bool SetFloat3(const std::string& name, const float data[3]);
				bool SetFloat3(const std::string& name, const float3 data);
				bool SetFloat4(const std::string& name, const float data[4]);
				bool SetFloat4(const std::string& name, const float4 data);
				bool SetMatrix4x4(const std::string& name, const float data[16]);
				bool SetMatrix4x4(const std::string& name, const float4x4& data);

				// Setting shader resources
				virtual bool SetShaderResourceView(const std::string& name, ID3D11ShaderResourceView* srv) = 0;
				virtual bool SetShaderResourceViewArray(const std::string& name, ID3D11ShaderResourceView** srv, int nviews) = 0;
				virtual bool SetSamplerState(const std::string& name, ID3D11SamplerState* samplerState) = 0;

				// Getting data about variables and resources
				const SimpleShaderVariable* GetVariableInfo(const std::string& name);

				const SimpleSRV* GetShaderResourceViewInfo(const std::string& name);
				const SimpleSRV* GetShaderResourceViewInfo(unsigned int index);
				size_t GetShaderResourceViewCount() { return textureTable.size(); }

				const SimpleSampler* GetSamplerInfo(const std::string& name);
				const SimpleSampler* GetSamplerInfo(unsigned int index);
				size_t GetSamplerCount() { return samplerTable.size(); }

				// Get data about constant buffers
				unsigned int GetBufferCount();
				unsigned int GetBufferSize(unsigned int index);
				const SimpleConstantBuffer* GetBufferInfo(const std::string& name);
				const SimpleConstantBuffer* GetBufferInfo(unsigned int index);

				// Misc getters
				ID3DBlob* GetShaderBlob() { return shaderBlob; }

			protected:

				bool shaderValid;
				std::wstring shaderFile;
				ID3DBlob* shaderBlob;
				ID3D11Device* device;
				ID3D11DeviceContext* deviceContext;

				// Resource counts
				unsigned int constantBufferCount;

				// Maps for variables and buffers
				SimpleConstantBuffer* constantBuffers; // For index-based lookup
				std::vector<SimpleSRV*>		shaderResourceViews;
				std::vector<SimpleSampler*>	samplerStates;
				std::unordered_map<std::string, SimpleConstantBuffer*> cbTable;
				std::unordered_map<std::string, SimpleShaderVariable> varTable;
				std::unordered_map<std::string, SimpleSRV*> textureTable;
				std::unordered_map<std::string, SimpleSampler*> samplerTable;

				// Pure virtual functions for dealing with shader types
				virtual bool CreateShader(ID3DBlob* shaderBlob) = 0;
				virtual void SetShaderAndCBs() = 0;

				virtual void CleanUp();

				// Helpers for finding data by name
				SimpleShaderVariable* FindVariable(const std::string& name, int size);
				SimpleConstantBuffer* FindConstantBuffer(const std::string& name);
			};

			class SimpleVertexShader : public ISimpleShader
			{
			public:
				SimpleVertexShader(ID3D11Device* device, ID3D11DeviceContext* context);
				SimpleVertexShader(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11InputLayout* inputLayout, bool perInstanceCompatible);
				~SimpleVertexShader();
				ID3D11VertexShader* GetDirectXShader() { return shader; }
				ID3D11InputLayout* GetInputLayout() { return inputLayout; }
				bool GetPerInstanceCompatible() { return perInstanceCompatible; }

				bool SetShaderResourceView(const std::string& name, ID3D11ShaderResourceView* srv);
				bool SetShaderResourceViewArray(const std::string& name, ID3D11ShaderResourceView** srv, int nviews);
				bool SetSamplerState(const std::string& name, ID3D11SamplerState* samplerState);

			protected:
				bool perInstanceCompatible;
				ID3D11InputLayout* inputLayout;
				ID3D11VertexShader* shader;
				bool CreateShader(ID3DBlob* shaderBlob);
				void SetShaderAndCBs();
				void CleanUp();
			};

			class SimplePixelShader : public ISimpleShader
			{
			public:
				SimplePixelShader(ID3D11Device* device, ID3D11DeviceContext* context);
				~SimplePixelShader();
				ID3D11PixelShader* GetDirectXShader() { return shader; }

				bool SetShaderResourceView(const std::string& name, ID3D11ShaderResourceView* srv);
				bool SetShaderResourceViewArray(const std::string& name, ID3D11ShaderResourceView** srv, int nviews);
				bool SetSamplerState(const std::string& name, ID3D11SamplerState* samplerState);

			protected:
				ID3D11PixelShader* shader;
				bool CreateShader(ID3DBlob* shaderBlob);
				void SetShaderAndCBs();
				void CleanUp();
			};

			class SimpleDomainShader : public ISimpleShader
			{
			public:
				SimpleDomainShader(ID3D11Device* device, ID3D11DeviceContext* context);
				~SimpleDomainShader();
				ID3D11DomainShader* GetDirectXShader() { return shader; }

				bool SetShaderResourceView(const std::string& name, ID3D11ShaderResourceView* srv);
				bool SetShaderResourceViewArray(const std::string& name, ID3D11ShaderResourceView** srv, int nviews);
				bool SetSamplerState(const std::string& name, ID3D11SamplerState* samplerState);

			protected:
				ID3D11DomainShader* shader;
				bool CreateShader(ID3DBlob* shaderBlob);
				void SetShaderAndCBs();
				void CleanUp();
			};

			class SimpleHullShader : public ISimpleShader
			{
			public:
				SimpleHullShader(ID3D11Device* device, ID3D11DeviceContext* context);
				~SimpleHullShader();
				ID3D11HullShader* GetDirectXShader() { return shader; }

				bool SetShaderResourceView(const std::string& name, ID3D11ShaderResourceView* srv);
				bool SetShaderResourceViewArray(const std::string& name, ID3D11ShaderResourceView** srv, int nviews);
				bool SetSamplerState(const std::string& name, ID3D11SamplerState* samplerState);

			protected:
				ID3D11HullShader* shader;
				bool CreateShader(ID3DBlob* shaderBlob);
				void SetShaderAndCBs();
				void CleanUp();
			};

			class SimpleGeometryShader : public ISimpleShader
			{
			public:
				SimpleGeometryShader(ID3D11Device* device, ID3D11DeviceContext* context, bool useStreamOut = 0, bool allowStreamOutRasterization = 0);
				~SimpleGeometryShader();
				ID3D11GeometryShader* GetDirectXShader() { return shader; }

				bool SetShaderResourceView(const std::string& name, ID3D11ShaderResourceView* srv);
				bool SetShaderResourceViewArray(const std::string& name, ID3D11ShaderResourceView** srv, int nviews);
				bool SetSamplerState(const std::string& name, ID3D11SamplerState* samplerState);

				bool CreateCompatibleStreamOutBuffer(ID3D11Buffer** buffer, int vertexCount);

				static void UnbindStreamOutStage(ID3D11DeviceContext* deviceContext);

			protected:
				// Shader itself
				ID3D11GeometryShader* shader;

				// Stream out related
				bool useStreamOut;
				bool allowStreamOutRasterization;
				unsigned int streamOutVertexSize;

				bool CreateShader(ID3DBlob* shaderBlob);
				bool CreateShaderWithStreamOut(ID3DBlob* shaderBlob);
				void SetShaderAndCBs();
				void CleanUp();

				// Helpers
				unsigned int CalcComponentCount(unsigned int mask);
			};

			class SimpleComputeShader : public ISimpleShader
			{
			public:
				SimpleComputeShader(ID3D11Device* device, ID3D11DeviceContext* context);
				~SimpleComputeShader();
				ID3D11ComputeShader* GetDirectXShader() { return shader; }

				void DispatchByGroups(unsigned int groupsX, unsigned int groupsY, unsigned int groupsZ);
				void DispatchByThreads(unsigned int threadsX, unsigned int threadsY, unsigned int threadsZ);

				bool SetShaderResourceBuffer(const std::string& name, ID3D11Buffer* buffer);
				bool SetShaderResourceView(const std::string& name, ID3D11ShaderResourceView* srv);
				bool SetShaderResourceViewArray(const std::string& name, ID3D11ShaderResourceView** srv, int nviews);
				bool SetSamplerState(const std::string& name, ID3D11SamplerState* samplerState);
				bool SetUnorderedAccessView(const std::string& name, ID3D11UnorderedAccessView* uav, unsigned int appendConsumeOffset = -1);

				int GetUnorderedAccessViewIndex(const std::string& name);

			protected:
				ID3D11ComputeShader* shader;
				std::unordered_map<std::string, unsigned int> uavTable;

				unsigned int threadsX;
				unsigned int threadsY;
				unsigned int threadsZ;
				unsigned int threadsTotal;

				bool CreateShader(ID3DBlob* shaderBlob);
				void SetShaderAndCBs();
				void CleanUp();
			};

			static constexpr int SHADER_KEY_VS = 0;
			static constexpr int SHADER_KEY_HS = 1;
			static constexpr int SHADER_KEY_DS = 2;
			static constexpr int SHADER_KEY_GS = 3;
			static constexpr int SHADER_KEY_PS = 4;

			using ShaderKey = std::tuple<Core::SimpleVertexShader*, Core::SimpleHullShader*, Core::SimpleDomainShader*, Core::SimpleGeometryShader*, Core::SimplePixelShader*>;

			class ShaderFactory {
			private:
				static ShaderFactory* sInstance;
				std::unordered_map<std::string, ISimpleShader*> shaders;

				ShaderFactory() = default;
				~ShaderFactory();

			public:
				static ShaderFactory* Get();
				static void Release();

				void Reload() {
					for (auto& shader : shaders) {
						shader.second->Reload();
					}
				}

				template <typename T>
				T* GetShader(const std::string& name) {
					T* shader = nullptr;
					if (!name.empty()) {
						auto s = shaders.find(name);
						if (s == shaders.end()) {
							shader = new T(DXCore::Get()->device, DXCore::Get()->context);
							std::wstring ws(name.begin(), name.end());
							shader->LoadShaderFile(ws.c_str());
							shaders[name] = shader;
						}
						else {
							shader = dynamic_cast<T*>(s->second);
						}
					}
					return shader;
				}
			};
		}
	}
}

namespace std {
	template <>
	struct hash<HotBite::Engine::Core::ShaderKey>
	{
		std::size_t operator()(const HotBite::Engine::Core::ShaderKey& k) const
		{
			return hash<uint64_t>()((uint64_t)std::get<0>(k) ^ (uint64_t)std::get<1>(k) ^ (uint64_t)std::get<2>(k) ^ (uint64_t)std::get<3>(k) ^ (uint64_t)std::get<4>(k));
		}
	};	
}
