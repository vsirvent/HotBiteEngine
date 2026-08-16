/// <summary>
/// By Chris Cascioli, Professor@RIT
/// </summary>
/// 
#include "SimpleShader.h"
#include "ShaderCompiler.h"
#include "Log.h"

#include <algorithm>

namespace HotBite {
	namespace Engine {
		namespace Core {

			ShaderFactory* ShaderFactory::sInstance = nullptr;

			//Shader file names are ASCII (they name a .cso next to the executable), so a
			//byte-wise narrowing is enough to get one back out of the wide string the
			//loader keeps.
			static std::string NarrowName(const std::wstring& wide) {
				std::string out;
				out.reserve(wide.size());
				for (wchar_t c : wide) {
					out.push_back((c < 128) ? (char)c : '?');
				}
				return out;
			}

			std::string SimpleShaderKeys::CLOUD_TEST = "cloud_test";
			std::string SimpleShaderKeys::DOP_ACTIVE = "dopActive";
			std::string SimpleShaderKeys::IS_SPACE = "is_space";
			std::string SimpleShaderKeys::MULTI_TEXTURE_COUNT = "multi_texture_count";
			std::string SimpleShaderKeys::NJOINTS = "njoints";
			std::string SimpleShaderKeys::UI_FLAGS = "ui_flags";
			std::string SimpleShaderKeys::VERTEX_COLOR = "vertex_color";
			std::string SimpleShaderKeys::DEPTH_TEXTURE = "depthTexture";
			std::string SimpleShaderKeys::NEAR_FACTOR = "nearFactor";
			std::string SimpleShaderKeys::FAR_FACTOR = "farFactor";
			std::string SimpleShaderKeys::PROJECTION_INVERSE = "projection_inverse";
			std::string SimpleShaderKeys::SCREEN_W = "screenW";

			ShaderFactory::~ShaderFactory() {
				for (auto shader : shaders) {
					delete shader.second;
				}
			}

			ShaderFactory* ShaderFactory::Get() {
				if (sInstance == nullptr) {
					sInstance = new ShaderFactory();
				}
				return sInstance;
			}

			void ShaderFactory::Release() {
				if (sInstance != nullptr) {
					delete sInstance;
				}
				sInstance = nullptr;
			}

			std::vector<std::string> ShaderFactory::GetShaderNames() const {
				std::vector<std::string> names;
				names.reserve(shaders.size());
				for (const auto& shader : shaders) {
					names.push_back(shader.first);
				}
				std::sort(names.begin(), names.end());
				return names;
			}

			void ShaderFactory::ForgetSourcePaths() {
				for (auto& shader : shaders) {
					shader.second->ForgetSourcePath();
				}
			}


			///////////////////////////////////////////////////////////////////////////////
			// ------ BASE SIMPLE SHADER --------------------------------------------------
			///////////////////////////////////////////////////////////////////////////////

			// --------------------------------------------------------
			// Constructor accepts DirectX device & context
			// --------------------------------------------------------
			ISimpleShader::ISimpleShader(ID3D11Device* device, ID3D11DeviceContext* context)
			{
				// Save the device
				this->device = device;
				this->deviceContext = context;

				// Set up fields
				constantBufferCount = 0;
				constantBuffers = 0;
				shaderBlob = 0;
			}

			// --------------------------------------------------------
			// Destructor
			// --------------------------------------------------------
			ISimpleShader::~ISimpleShader()
			{
				// Derived class destructors will call this class's CleanUp method
				if (shaderBlob)
					shaderBlob->Release();
			}

			// --------------------------------------------------------
			// Cleans up the variable table and buffers - Some things will
			// be handled by derived classes
			// --------------------------------------------------------
			void ISimpleShader::CleanUp()
			{
				// Handle constant buffers and local data buffers
				for (unsigned int i = 0; i < constantBufferCount; i++)
				{
					//Null only if CreateBuffer failed for this slot - a device-removed or
					//out-of-memory frame. Worth the check because the alternative is
					//taking the process down while tearing a shader down.
					if (constantBuffers[i].ConstantBuffer != nullptr)
					{
						constantBuffers[i].ConstantBuffer->Release();
						constantBuffers[i].ConstantBuffer = nullptr;
					}
					delete[] constantBuffers[i].LocalDataBuffer;
					constantBuffers[i].LocalDataBuffer = nullptr;
				}

				if (constantBuffers)
				{
					delete[] constantBuffers;
					constantBuffers = nullptr;
					constantBufferCount = 0;
				}

				for (unsigned int i = 0; i < shaderResourceViews.size(); i++) {
					if (shaderResourceViews[i]) {
						delete shaderResourceViews[i];
					}
				}

				for (unsigned int i = 0; i < samplerStates.size(); i++)
				{
					if (samplerStates[i]) {
						delete samplerStates[i];
					}
				}
				shaderResourceViews.clear();
				samplerStates.clear();

				// Clean up tables
				varTable.clear();
				cbTable.clear();
				samplerTable.clear();
				textureTable.clear();
			}

			// --------------------------------------------------------
			// Loads the specified shader and builds the variable table using shader
			// reflection.  This must be a separate step from the constructor since
			// we can't invoke derived class overrides in the base class constructor.
			//
			// shaderFile - A "wide string" specifying the compiled shader to load
			// 
			// Returns true if shader is loaded properly, false otherwise
			// --------------------------------------------------------
			bool ISimpleShader::Reload()
			{
				return LoadShaderFile(shaderFile.c_str());
			}

			const std::string& ISimpleShader::GetSourcePath()
			{
				if (sourcePath.empty()) {
					//Remembered either way: a shader with no source is asked about once
					//per reload sweep, and a filesystem probe per shader per sweep is
					//not free once there are seventy of them.
					std::string found = ShaderCompiler::Get()->FindSource(
						shaderName.empty() ? NarrowName(shaderFile) : shaderName);
					sourcePath = found.empty() ? "-" : found;
				}
				static const std::string none;
				return (sourcePath == "-") ? none : sourcePath;
			}

			void ISimpleShader::RecordDependencies(const std::vector<std::string>& deps,
				const std::vector<uint64_t>* times)
			{
				const bool use_times = (times != nullptr && times->size() == deps.size());
				dependencies.clear();
				dependencies.reserve(deps.size());
				for (size_t i = 0; i < deps.size(); ++i) {
					dependencies.push_back({ deps[i],
						use_times ? (*times)[i] : ShaderCompiler::FileTime(deps[i]) });
				}
			}

			bool ISimpleShader::IsSourceOutdated()
			{
				const std::string& source = GetSourcePath();
				if (source.empty()) {
					return false;
				}
				if (dependencies.empty()) {
					//Never compiled here, so the include set is unknown: learn it with a
					//preprocess-only pass (far cheaper than a compile) and take the
					//current timestamps as the baseline. The .cso on disk was built from
					//these files, so "unchanged" is the right answer for a fresh tree.
					std::vector<std::string> deps;
					std::string error;
					ShaderCompiler::Get()->Preprocess(source, deps, error);
					if (deps.empty()) {
						deps.push_back(source);
					}
					RecordDependencies(deps);
					return false;
				}
				for (const auto& dep : dependencies) {
					if (ShaderCompiler::FileTime(dep.first) != dep.second) {
						return true;
					}
				}
				return false;
			}

			bool ISimpleShader::CompileSource(const std::vector<std::string>& search_dirs,
				ShaderCompiler::Result& result)
			{
				//The source path is resolved (and cached) by the caller before this runs
				//- see ShaderReload - so this half reads it without writing anything.
				const std::string& source = GetSourcePath();
				if (source.empty()) {
					result.error = "no .hlsl source found for " +
						(shaderName.empty() ? NarrowName(shaderFile) : shaderName);
					return false;
				}
				return ShaderCompiler::Get()->CompileWith(source, GetShaderProfile(), search_dirs, result);
			}

			bool ISimpleShader::ApplyCompiled(ShaderCompiler::Result& result, std::string& error)
			{
				//The include set is recorded whether or not the compile succeeded: it is
				//what tells a "reload changed" sweep to try this shader again after the
				//next edit, and a failed compile still reports the files it opened.
				if (!result.dependencies.empty()) {
					RecordDependencies(result.dependencies, &result.dependency_times);
				}
				if (result.blob == nullptr) {
					error = result.error;
					return false;
				}
				if (!result.error.empty()) {
					LOG_WARN("Shader %s compiled with warnings:\n%s",
						GetSourcePath().c_str(), result.error.c_str());
				}
				//LoadShaderBlob takes the blob, so the caller must not release it.
				ID3DBlob* blob = result.blob;
				result.blob = nullptr;
				if (!LoadShaderBlob(blob)) {
					error = "the compiled shader could not be created on the device";
					LOG_ERROR("Shader reload failed to create device shader: %s", GetSourcePath().c_str());
					return false;
				}
				LOG_INFO("Shader reloaded from source: %s", GetSourcePath().c_str());
				return true;
			}

			bool ISimpleShader::ReloadFromSource(std::string& error)
			{
				const std::string& source = GetSourcePath();
				if (source.empty()) {
					error = "no .hlsl source found for " +
						(shaderName.empty() ? NarrowName(shaderFile) : shaderName);
					return false;
				}

				//Compile before touching anything. A shader that fails to compile leaves
				//the running one exactly as it is - the frame after a typo looks like the
				//frame before it, with the compiler's message in the log.
				ShaderCompiler::Result result;
				if (!ShaderCompiler::Get()->Compile(source, GetShaderProfile(), result)) {
					error = result.error;
					RecordDependencies(result.dependencies, &result.dependency_times);
					LOG_ERROR("Shader compile failed: %s\n%s", source.c_str(), error.c_str());
					return false;
				}
				return ApplyCompiled(result, error);
			}

			bool ISimpleShader::LoadShaderFile(LPCWSTR shaderFile)
			{
				this->shaderFile = shaderFile;

				// Load the shader to a blob and ensure it worked.
				//
				// A missing or unreadable .cso is a normal outcome here, not a
				// programming error: the editor's set_shader takes a file name that can
				// name something that is not there, and ShaderFactory::GetShader probes
				// for a stage a material may not have. An assert alone is compiled out
				// of Release, and CreateShader then dereferences the null blob - so the
				// whole process died where the caller only wanted `false` back.
				ID3DBlob* blob = nullptr;
				HRESULT hr = D3DReadFileToBlob(shaderFile, &blob);
				if (FAILED(hr) || blob == nullptr)
				{
					shaderValid = false;
					return false;
				}
				return LoadShaderBlob(blob);
			}

			bool ISimpleShader::LoadShaderBlob(ID3DBlob* blob)
			{
				// Snapshot the constant buffer contents before CreateShader tears the
				// tables down, so values set once (at init, or on resize) survive a
				// reload. Keyed by name and only restored at an identical size, which is
				// what makes it safe when the edit being reloaded changed a cbuffer.
				std::unordered_map<std::string, std::vector<unsigned char>> saved;
				for (const auto& entry : varTable)
				{
					const SimpleShaderVariable& var = entry.second;
					if (var.ConstantBufferIndex >= constantBufferCount ||
						constantBuffers == nullptr ||
						constantBuffers[var.ConstantBufferIndex].LocalDataBuffer == nullptr)
					{
						continue;
					}
					const unsigned char* src =
						constantBuffers[var.ConstantBufferIndex].LocalDataBuffer + var.ByteOffset;
					saved[entry.first].assign(src, src + var.Size);
				}

				if (shaderBlob != nullptr)
				{
					shaderBlob->Release();
				}
				shaderBlob = blob;

				// Create the shader - Calls an overloaded version of this abstract
				// method in the appropriate child class
				shaderValid = CreateShader(shaderBlob);
				if (!shaderValid)
				{
					return false;
				}

				// Set up shader reflection to get information about
				// this shader and its variables,  buffers, etc.
				ID3D11ShaderReflection* refl;
				D3DReflect(
					shaderBlob->GetBufferPointer(),
					shaderBlob->GetBufferSize(),
					IID_ID3D11ShaderReflection,
					(void**)&refl);

				// Get the description of the shader
				D3D11_SHADER_DESC shaderDesc;
				refl->GetDesc(&shaderDesc);

				// Create resource arrays
				constantBufferCount = shaderDesc.ConstantBuffers;
				constantBuffers = new SimpleConstantBuffer[constantBufferCount];

				// Handle bound resources (like shaders and samplers)
				unsigned int resourceCount = shaderDesc.BoundResources;
				for (unsigned int r = 0; r < resourceCount; r++)
				{
					// Get this resource's description
					D3D11_SHADER_INPUT_BIND_DESC resourceDesc;
					refl->GetResourceBindingDesc(r, &resourceDesc);

					// Check the type
					switch (resourceDesc.Type)
					{
					//Buffer SRVs bind exactly like texture SRVs - same `t` register
					//space, same *SetShaderResources call - so they belong in the same
					//table. Only D3D_SIT_TEXTURE used to be registered here, which
					//meant SetShaderResourceView("<a ByteAddressBuffer>", srv) looked
					//the name up, found nothing, and returned false; callers do not
					//check that, so the buffer silently stayed unbound and every read
					//of it returned zero. That is why the BVH and vertex buffers are
					//bound by explicit register number through CSSetShaderResources
					//rather than by name - this was the reason, and it no longer
					//applies.
					case D3D_SIT_BYTEADDRESS:
					case D3D_SIT_STRUCTURED:
					case D3D_SIT_TEXTURE: // A texture resource
					{
						// Create the SRV wrapper
						SimpleSRV* srv = new SimpleSRV();
						srv->BindIndex = resourceDesc.BindPoint;				// Shader bind point
						srv->Index = (unsigned int)shaderResourceViews.size();	// Raw index

						textureTable.insert(std::pair<std::string, SimpleSRV*>(resourceDesc.Name, srv));
						shaderResourceViews.push_back(srv);
					}
					break;

					case D3D_SIT_SAMPLER: // A sampler resource
					{
						// Create the sampler wrapper
						SimpleSampler* samp = new SimpleSampler();
						samp->BindIndex = resourceDesc.BindPoint;			// Shader bind point
						samp->Index = (unsigned int)samplerStates.size();	// Raw index

						samplerTable.insert(std::pair<std::string, SimpleSampler*>(resourceDesc.Name, samp));
						samplerStates.push_back(samp);
					}
					break;
					}
				}

				// Loop through all constant buffers.
				//
				// `shaderDesc.ConstantBuffers` is not the number of *cbuffers*: reflection
				// reports one entry per constant-buffer-shaped thing, and a
				// StructuredBuffer/ByteAddressBuffer contributes one of type
				// D3D_CT_RESOURCE_BIND_INFO describing its element layout - which is why
				// `fxc /dumpbin` prints a "Resource bind info for <name>" block next to the
				// real cbuffers. Those are not buffers to create or to upload: their Size is
				// the element stride (60 for SplatView), CreateBuffer rejects it as a
				// constant buffer size, and the slot is left holding a null. CopyAllBufferData
				// then hands that null to UpdateSubresource and the process dies inside D3D,
				// with a stack pointing at whichever pass happened to be drawing.
				//
				// Nothing hit this until a compute pass took a StructuredBuffer by name; the
				// BVH and vertex buffers are bound by explicit register, which never reaches
				// this loop. So the count is recomputed from what is actually kept, and the
				// slots stay contiguous because SetShader and CopyAllBufferData index them.
				unsigned int keptBuffers = 0;
				for (unsigned int b = 0; b < constantBufferCount; b++)
				{
					// Get this buffer
					ID3D11ShaderReflectionConstantBuffer* cb =
						refl->GetConstantBufferByIndex(b);

					// Get the description of this buffer
					D3D11_SHADER_BUFFER_DESC bufferDesc;
					cb->GetDesc(&bufferDesc);

					if (bufferDesc.Type != D3D_CT_CBUFFER && bufferDesc.Type != D3D_CT_TBUFFER)
					{
						continue;
					}
					const unsigned int slot = keptBuffers++;

					// Get the description of the resource binding, so
					// we know exactly how it's bound in the shader
					D3D11_SHADER_INPUT_BIND_DESC bindDesc;
					refl->GetResourceBindingDescByName(bufferDesc.Name, &bindDesc);

					// Set up the buffer and put its pointer in the table
					constantBuffers[slot].BindIndex = bindDesc.BindPoint;
					constantBuffers[slot].Name = bufferDesc.Name;
					cbTable.insert(std::pair<std::string, SimpleConstantBuffer*>(bufferDesc.Name, &constantBuffers[slot]));

					// Create this constant buffer
					D3D11_BUFFER_DESC newBuffDesc;
					newBuffDesc.Usage = D3D11_USAGE_DEFAULT;
					newBuffDesc.ByteWidth = bufferDesc.Size;
					newBuffDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
					newBuffDesc.CPUAccessFlags = 0;
					newBuffDesc.MiscFlags = 0;
					newBuffDesc.StructureByteStride = 0;
					device->CreateBuffer(&newBuffDesc, 0, &constantBuffers[slot].ConstantBuffer);

					// Set up the data buffer for this constant buffer
					constantBuffers[slot].Size = bufferDesc.Size;
					constantBuffers[slot].LocalDataBuffer = new unsigned char[bufferDesc.Size];
					ZeroMemory(constantBuffers[slot].LocalDataBuffer, bufferDesc.Size);

					// Loop through all variables in this buffer
					for (unsigned int v = 0; v < bufferDesc.Variables; v++)
					{
						// Get this variable
						ID3D11ShaderReflectionVariable* var =
							cb->GetVariableByIndex(v);

						// Get the description of the variable and its type
						D3D11_SHADER_VARIABLE_DESC varDesc;
						var->GetDesc(&varDesc);

						// Create the variable struct
						SimpleShaderVariable varStruct;
						varStruct.ConstantBufferIndex = slot;
						varStruct.ByteOffset = varDesc.StartOffset;
						varStruct.Size = varDesc.Size;

						// Get a string version
						std::string varName(varDesc.Name);

						// Add this variable to the table and the constant buffer
						varTable.insert(std::pair<std::string, SimpleShaderVariable>(varName, varStruct));
						constantBuffers[slot].Variables.push_back(varStruct);
					}
				}
				//Only the kept slots are initialised, and every loop over this array -
				//CopyAllBufferData, SetShader, CleanUp - is bounded by it.
				constantBufferCount = keptBuffers;

				// Put back the values a previous build of this shader was holding (see
				// the snapshot at the top). Only an exact name and size match is
				// restored - a variable that changed type or size is left at zero,
				// because the bytes meant something else.
				for (const auto& entry : varTable)
				{
					auto old = saved.find(entry.first);
					if (old == saved.end() || old->second.size() != entry.second.Size)
					{
						continue;
					}
					memcpy(constantBuffers[entry.second.ConstantBufferIndex].LocalDataBuffer +
						entry.second.ByteOffset, old->second.data(), old->second.size());
				}

				// All set
				refl->Release();
				return true;
			}

			// --------------------------------------------------------
			// Helper for looking up a variable by name and also
			// verifying that it is the requested size
			// 
			// name - the name of the variable to look for
			// size - the size of the variable (for verification), or -1 to bypass
			// --------------------------------------------------------
			SimpleShaderVariable* ISimpleShader::FindVariable(const std::string& name, int size)
			{
				// Look for the key
				std::unordered_map<std::string, SimpleShaderVariable>::iterator result =
					varTable.find(name);

				// Did we find the key?
				if (result == varTable.end())
					return 0;

				// Grab the result from the iterator
				SimpleShaderVariable* var = &(result->second);

				// Is the data size correct ?
				assert(size > 0 && size <= (int)var->Size);

				// Success
				return var;
			}

			// --------------------------------------------------------
			// Helper for looking up a constant buffer by name
			// --------------------------------------------------------
			SimpleConstantBuffer* ISimpleShader::FindConstantBuffer(const std::string& name)
			{
				// Look for the key
				std::unordered_map<std::string, SimpleConstantBuffer*>::iterator result =
					cbTable.find(name);

				// Did we find the key?
				if (result == cbTable.end())
					return 0;

				// Success
				return result->second;
			}

			// --------------------------------------------------------
			// Sets the shader and associated constant buffers in DirectX
			// --------------------------------------------------------
			void ISimpleShader::SetShader()
			{
				// Ensure the shader is valid
				if (!shaderValid) return;

				// Set the shader and any relevant constant buffers, which
				// is an overloaded method in a subclass
				SetShaderAndCBs();
			}

			// --------------------------------------------------------
			// Copies the relevant data to the all of this 
			// shader's constant buffers.  To just copy one
			// buffer, use CopyBufferData()
			// --------------------------------------------------------
			void ISimpleShader::CopyAllBufferData()
			{
				// Ensure the shader is valid
				if (!shaderValid) return;

				// Loop through the constant buffers and copy all data
				for (unsigned int i = 0; i < constantBufferCount; i++)
				{
					// Copy the entire local data buffer
					deviceContext->UpdateSubresource(
						constantBuffers[i].ConstantBuffer, 0, 0,
						constantBuffers[i].LocalDataBuffer, 0, 0);
				}
			}

			// --------------------------------------------------------
			// Copies local data to the shader's specified constant buffer
			//
			// index - The index of the buffer to copy.
			//         Useful for updating more frequently-changing
			//         variables without having to re-copy all buffers.
			//  
			// NOTE: The "index" of the buffer might NOT be the same
			//       as its register, especially if you have buffers
			//       bound to non-sequential registers!
			// --------------------------------------------------------
			void ISimpleShader::CopyBufferData(unsigned int index)
			{
				// Ensure the shader is valid
				if (!shaderValid) return;

				// Validate the index
				if (index >= this->constantBufferCount)
					return;

				// Check for the buffer
				SimpleConstantBuffer* cb = &this->constantBuffers[index];
				if (!cb) return;

				// Copy the data and get out
				deviceContext->UpdateSubresource(
					cb->ConstantBuffer, 0, 0,
					cb->LocalDataBuffer, 0, 0);
			}

			// --------------------------------------------------------
			// Copies local data to the shader's specified constant buffer
			//
			// bufferName - Specifies the name of the buffer to copy.
			//              Useful for updating more frequently-changing
			//              variables without having to re-copy all buffers.
			// --------------------------------------------------------
			void ISimpleShader::CopyBufferData(const std::string& bufferName)
			{
				// Ensure the shader is valid
				if (!shaderValid) return;

				// Check for the buffer
				SimpleConstantBuffer* cb = this->FindConstantBuffer(bufferName);
				if (!cb) return;

				// Copy the data and get out
				deviceContext->UpdateSubresource(
					cb->ConstantBuffer, 0, 0,
					cb->LocalDataBuffer, 0, 0);
			}


			// --------------------------------------------------------
			// Sets a variable by name with arbitrary data of the specified size
			//
			// name - The name of the shader variable
			// data - The data to set in the buffer
			// size - The size of the data (this must match the variable's size)
			//
			// Returns true if data is copied, false if variable doesn't 
			// exist or sizes don't match
			// --------------------------------------------------------
			bool ISimpleShader::SetData(const std::string& name, const void* data, unsigned int size)
			{
				// Look for the variable and verify
				SimpleShaderVariable* var = FindVariable(name, size);
				if (var == 0)
					return false;

				if (data != nullptr) {
					// Set the data in the local data buffer
					memcpy(
						constantBuffers[var->ConstantBufferIndex].LocalDataBuffer + var->ByteOffset,
						data,
						size);
				}
				// Success
				return true;
			}

			// --------------------------------------------------------
			// Sets INTEGER data
			// --------------------------------------------------------
			bool ISimpleShader::SetInt(const std::string& name, int data)
			{
				return this->SetData(name, (void*)(&data), sizeof(int));
			}

			// --------------------------------------------------------
			// Sets a FLOAT variable by name in the local data buffer
			// --------------------------------------------------------
			bool ISimpleShader::SetFloat(const std::string& name, float data)
			{
				return this->SetData(name, (void*)(&data), sizeof(float));
			}

			// --------------------------------------------------------
			// Sets a FLOAT2 variable by name in the local data buffer
			// --------------------------------------------------------
			bool ISimpleShader::SetFloat2(const std::string& name, const float data[2])
			{
				return this->SetData(name, (void*)data, sizeof(float) * 2);
			}

			// --------------------------------------------------------
			// Sets a FLOAT2 variable by name in the local data buffer
			// --------------------------------------------------------
			bool ISimpleShader::SetFloat2(const std::string& name, const float2 data)
			{
				return this->SetData(name, &data, sizeof(float) * 2);
			}

			// --------------------------------------------------------
			// Sets a FLOAT3 variable by name in the local data buffer
			// --------------------------------------------------------
			bool ISimpleShader::SetFloat3(const std::string& name, const float data[3])
			{
				return this->SetData(name, (void*)data, sizeof(float) * 3);
			}

			// --------------------------------------------------------
			// Sets a FLOAT3 variable by name in the local data buffer
			// --------------------------------------------------------
			bool ISimpleShader::SetFloat3(const std::string& name, const float3 data)
			{
				return this->SetData(name, &data, sizeof(float) * 3);
			}

			// --------------------------------------------------------
			// Sets a FLOAT4 variable by name in the local data buffer
			// --------------------------------------------------------
			bool ISimpleShader::SetFloat4(const std::string& name, const float data[4])
			{
				return this->SetData(name, (void*)data, sizeof(float) * 4);
			}

			// --------------------------------------------------------
			// Sets a FLOAT4 variable by name in the local data buffer
			// --------------------------------------------------------
			bool ISimpleShader::SetFloat4(const std::string& name, const float4 data)
			{
				return this->SetData(name, &data, sizeof(float) * 4);
			}

			// --------------------------------------------------------
			// Sets a MATRIX (4x4) variable by name in the local data buffer
			// --------------------------------------------------------
			bool ISimpleShader::SetMatrix4x4(const std::string& name, const float data[16])
			{
				return this->SetData(name, (void*)data, sizeof(float) * 16);
			}

			// --------------------------------------------------------
			// Sets a MATRIX (4x4) variable by name in the local data buffer
			// --------------------------------------------------------
			bool ISimpleShader::SetMatrix4x4(const std::string& name, const float4x4& data)
			{
				return this->SetData(name, &data, sizeof(float) * 16);
			}

			// --------------------------------------------------------
			// Gets info about a shader variable, if it exists
			// --------------------------------------------------------
			const SimpleShaderVariable* ISimpleShader::GetVariableInfo(const std::string& name)
			{
				return FindVariable(name, -1);
			}

			// --------------------------------------------------------
			// Gets info about an SRV in the shader (or null)
			//
			// name - the name of the SRV
			// --------------------------------------------------------
			const SimpleSRV* ISimpleShader::GetShaderResourceViewInfo(const std::string& name)
			{
				// Look for the key
				std::unordered_map<std::string, SimpleSRV*>::iterator result =
					textureTable.find(name);

				// Did we find the key?
				if (result == textureTable.end())
					return 0;

				// Success
				return result->second;
			}


			// --------------------------------------------------------
			// Gets info about an SRV in the shader (or null)
			//
			// index - the index of the SRV
			// --------------------------------------------------------
			const SimpleSRV* ISimpleShader::GetShaderResourceViewInfo(unsigned int index)
			{
				// Valid index?
				if (index >= shaderResourceViews.size()) return 0;

				// Grab the bind index
				return shaderResourceViews[index];
			}


			// --------------------------------------------------------
			// Gets info about a sampler in the shader (or null)
			// 
			// name - the name of the sampler
			// --------------------------------------------------------
			const SimpleSampler* ISimpleShader::GetSamplerInfo(const std::string& name)
			{
				// Look for the key
				std::unordered_map<std::string, SimpleSampler*>::iterator result =
					samplerTable.find(name);

				// Did we find the key?
				if (result == samplerTable.end())
					return 0;

				// Success
				return result->second;
			}

			// --------------------------------------------------------
			// Gets info about a sampler in the shader (or null)
			// 
			// index - the index of the sampler
			// --------------------------------------------------------
			const SimpleSampler* ISimpleShader::GetSamplerInfo(unsigned int index)
			{
				// Valid index?
				if (index >= samplerStates.size()) return 0;

				// Grab the bind index
				return samplerStates[index];
			}


			// --------------------------------------------------------
			// Gets the number of constant buffers in this shader
			// --------------------------------------------------------
			unsigned int ISimpleShader::GetBufferCount() { return constantBufferCount; }



			// --------------------------------------------------------
			// Gets the size of a particular constant buffer, or -1
			// --------------------------------------------------------
			unsigned int ISimpleShader::GetBufferSize(unsigned int index)
			{
				// Valid index?
				if (index >= constantBufferCount)
					return -1;

				// Grab the size
				return constantBuffers[index].Size;
			}

			// --------------------------------------------------------
			// Gets info about a particular constant buffer 
			// by name, if it exists
			// --------------------------------------------------------
			const SimpleConstantBuffer* ISimpleShader::GetBufferInfo(const std::string& name)
			{
				return FindConstantBuffer(name);
			}

			// --------------------------------------------------------
			// Gets info about a particular constant buffer 
			//
			// index - the index of the constant buffer
			// --------------------------------------------------------
			const SimpleConstantBuffer* ISimpleShader::GetBufferInfo(unsigned int index)
			{
				// Check for valid index
				if (index >= constantBufferCount) return 0;

				// Return the specific buffer
				return &constantBuffers[index];
			}





			///////////////////////////////////////////////////////////////////////////////
			// ------ SIMPLE VERTEX SHADER ------------------------------------------------
			///////////////////////////////////////////////////////////////////////////////

			// --------------------------------------------------------
			// Constructor just calls the base
			// --------------------------------------------------------
			SimpleVertexShader::SimpleVertexShader(ID3D11Device* device, ID3D11DeviceContext* context)
				: ISimpleShader(device, context)
			{
				// Ensure we set to zero to successfully trigger
				// the Input Layout creation during LoadShader()
				this->inputLayout = 0;
				this->shader = 0;
				this->perInstanceCompatible = false;
			}

			// --------------------------------------------------------
			// Constructor overload which takes a custom input layout
			//
			// Passing in a valid input layout will stop LoadShader()
			// from creating an input layout from shader reflection
			// --------------------------------------------------------
			SimpleVertexShader::SimpleVertexShader(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11InputLayout* inputLayout, bool perInstanceCompatible)
				: ISimpleShader(device, context)
			{
				// Save the custom input layout
				this->inputLayout = inputLayout;
				this->shader = 0;

				// Unable to determine from an input layout, require user to tell us
				this->perInstanceCompatible = perInstanceCompatible;
			}

			// --------------------------------------------------------
			// Destructor - Clean up actual shader (base will be called automatically)
			// --------------------------------------------------------
			SimpleVertexShader::~SimpleVertexShader()
			{
				CleanUp();
			}

			// --------------------------------------------------------
			// Handles cleaning up shader and base class clean up
			// --------------------------------------------------------
			void SimpleVertexShader::CleanUp()
			{
				ISimpleShader::CleanUp();
				if (shader) { shader->Release(); shader = 0; }
				if (inputLayout) { inputLayout->Release(); inputLayout = 0; }
			}

			// --------------------------------------------------------
			// Creates the DirectX vertex shader
			//
			// shaderBlob - The shader's compiled code
			//
			// Returns true if shader is created correctly, false otherwise
			// --------------------------------------------------------
			bool SimpleVertexShader::CreateShader(ID3DBlob* shaderBlob)
			{
				// Clean up first, in the event this method is
				// called more than once on the same object
				this->CleanUp();

				// Create the shader from the blob
				HRESULT result = device->CreateVertexShader(
					shaderBlob->GetBufferPointer(),
					shaderBlob->GetBufferSize(),
					0,
					&shader);

				// Did the creation work?
				if (result != S_OK)
					return false;

				// Do we already have an input layout?
				// (This would come from one of the constructor overloads)
				if (inputLayout)
					return true;

				// Vertex shader was created successfully, so we now use the
				// shader code to re-reflect and create an input layout that 
				// matches what the vertex shader expects.  Code adapted from:
				// https://takinginitiative.wordpress.com/2011/12/11/directx-1011-basic-shader-reflection-automatic-input-layout-creation/

				// Reflect shader info
				ID3D11ShaderReflection* refl;
				D3DReflect(
					shaderBlob->GetBufferPointer(),
					shaderBlob->GetBufferSize(),
					IID_ID3D11ShaderReflection,
					(void**)&refl);

				// Get shader info
				D3D11_SHADER_DESC shaderDesc;
				refl->GetDesc(&shaderDesc);

				// Read input layout description from shader info
				std::vector<D3D11_INPUT_ELEMENT_DESC> inputLayoutDesc;
				for (unsigned int i = 0; i < shaderDesc.InputParameters; i++)
				{
					D3D11_SIGNATURE_PARAMETER_DESC paramDesc;
					refl->GetInputParameterDesc(i, &paramDesc);

					// Check the semantic name for "_PER_INSTANCE"
					std::string perInstanceStr = "_PER_INSTANCE";
					std::string sem = paramDesc.SemanticName;
					int lenDiff = (int)sem.size() - (int)perInstanceStr.size();
					bool isPerInstance =
						lenDiff >= 0 &&
						sem.compare(lenDiff, perInstanceStr.size(), perInstanceStr) == 0;

					// Fill out input element desc
					D3D11_INPUT_ELEMENT_DESC elementDesc;
					elementDesc.SemanticName = paramDesc.SemanticName;
					elementDesc.SemanticIndex = paramDesc.SemanticIndex;
					elementDesc.InputSlot = 0;
					elementDesc.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
					elementDesc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
					elementDesc.InstanceDataStepRate = 0;

					// Replace anything affected by "per instance" data
					if (isPerInstance)
					{
						elementDesc.InputSlot = 1; // Assume per instance data comes from another input slot!
						elementDesc.InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
						elementDesc.InstanceDataStepRate = 1;

						perInstanceCompatible = true;
					}

					// Determine DXGI format
					if (paramDesc.Mask == 1)
					{
						if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) elementDesc.Format = DXGI_FORMAT_R32_UINT;
						else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) elementDesc.Format = DXGI_FORMAT_R32_SINT;
						else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32_FLOAT;
					}
					else if (paramDesc.Mask <= 3)
					{
						if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) elementDesc.Format = DXGI_FORMAT_R32G32_UINT;
						else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) elementDesc.Format = DXGI_FORMAT_R32G32_SINT;
						else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
					}
					else if (paramDesc.Mask <= 7)
					{
						if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) elementDesc.Format = DXGI_FORMAT_R32G32B32_UINT;
						else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) elementDesc.Format = DXGI_FORMAT_R32G32B32_SINT;
						else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
					}
					else if (paramDesc.Mask <= 15)
					{
						if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) elementDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
						else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) elementDesc.Format = DXGI_FORMAT_R32G32B32A32_SINT;
						else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
					}

					// Save element desc
					inputLayoutDesc.push_back(elementDesc);
				}

				// Try to create Input Layout
				HRESULT hr = device->CreateInputLayout(
					&inputLayoutDesc[0],
					(unsigned int)inputLayoutDesc.size(),
					shaderBlob->GetBufferPointer(),
					shaderBlob->GetBufferSize(),
					&inputLayout);

				// All done, clean up
				refl->Release();
				return true;
			}

			// --------------------------------------------------------
			// Sets the vertex shader, input layout and constant buffers
			// for future DirectX drawing
			// --------------------------------------------------------
			void SimpleVertexShader::SetShaderAndCBs()
			{
				// Is shader valid?
				if (!shaderValid) return;

				// Set the shader and input layout
				deviceContext->IASetInputLayout(inputLayout);
				deviceContext->VSSetShader(shader, 0, 0);

				// Set the constant buffers
				for (unsigned int i = 0; i < constantBufferCount; i++)
				{
					deviceContext->VSSetConstantBuffers(
						constantBuffers[i].BindIndex,
						1,
						&constantBuffers[i].ConstantBuffer);
				}
			}

			// --------------------------------------------------------
			// Sets a shader resource view in the vertex shader stage
			//
			// name - The name of the texture resource in the shader
			// srv - The shader resource view of the texture in GPU memory
			//
			// Returns true if a texture of the given name was found, false otherwise
			// --------------------------------------------------------
			bool SimpleVertexShader::SetShaderResourceView(const std::string& name, ID3D11ShaderResourceView* srv)
			{
				// Look for the variable and verify
				const SimpleSRV* srvInfo = GetShaderResourceViewInfo(name);
				if (srvInfo == 0)
					return false;

				// Set the shader resource view
				deviceContext->VSSetShaderResources(srvInfo->BindIndex, 1, &srv);

				// Success
				return true;
			}

			bool SimpleVertexShader::SetShaderResourceViewArray(const std::string& name, ID3D11ShaderResourceView** srv, int nviews)
			{
				// Look for the variable and verify
				const SimpleSRV* srvInfo = GetShaderResourceViewInfo(name);
				if (srvInfo == 0)
					return false;

				// Set the shader resource view
				deviceContext->VSSetShaderResources(srvInfo->BindIndex, nviews, srv);

				// Success
				return true;
			}


			// --------------------------------------------------------
			// Sets a sampler state in the vertex shader stage
			//
			// name - The name of the sampler state in the shader
			// samplerState - The sampler state in GPU memory
			//
			// Returns true if a sampler of the given name was found, false otherwise
			// --------------------------------------------------------
			bool SimpleVertexShader::SetSamplerState(const std::string& name, ID3D11SamplerState* samplerState)
			{
				// Look for the variable and verify
				const SimpleSampler* sampInfo = GetSamplerInfo(name);
				if (sampInfo == 0)
					return false;

				// Set the shader resource view
				deviceContext->VSSetSamplers(sampInfo->BindIndex, 1, &samplerState);

				// Success
				return true;
			}


			///////////////////////////////////////////////////////////////////////////////
			// ------ SIMPLE PIXEL SHADER -------------------------------------------------
			///////////////////////////////////////////////////////////////////////////////

			// --------------------------------------------------------
			// Constructor just calls the base
			// --------------------------------------------------------
			SimplePixelShader::SimplePixelShader(ID3D11Device* device, ID3D11DeviceContext* context)
				: ISimpleShader(device, context)
			{
				this->shader = 0;
			}

			// --------------------------------------------------------
			// Destructor - Clean up actual shader (base will be called automatically)
			// --------------------------------------------------------
			SimplePixelShader::~SimplePixelShader()
			{
				CleanUp();
			}

			// --------------------------------------------------------
			// Handles cleaning up shader and base class clean up
			// --------------------------------------------------------
			void SimplePixelShader::CleanUp()
			{
				ISimpleShader::CleanUp();
				if (shader) { shader->Release(); shader = 0; }
			}

			// --------------------------------------------------------
			// Creates the DirectX pixel shader
			//
			// shaderBlob - The shader's compiled code
			//
			// Returns true if shader is created correctly, false otherwise
			// --------------------------------------------------------
			bool SimplePixelShader::CreateShader(ID3DBlob* shaderBlob)
			{
				// Clean up first, in the event this method is
				// called more than once on the same object
				this->CleanUp();

				// Create the shader from the blob
				HRESULT result = device->CreatePixelShader(
					shaderBlob->GetBufferPointer(),
					shaderBlob->GetBufferSize(),
					0,
					&shader);

				// Check the result
				return (result == S_OK);
			}

			// --------------------------------------------------------
			// Sets the pixel shader and constant buffers for
			// future DirectX drawing
			// --------------------------------------------------------
			void SimplePixelShader::SetShaderAndCBs()
			{
				// Is shader valid?
				if (!shaderValid) return;

				// Set the shader
				deviceContext->PSSetShader(shader, 0, 0);

				// Set the constant buffers
				for (unsigned int i = 0; i < constantBufferCount; i++)
				{
					deviceContext->PSSetConstantBuffers(
						constantBuffers[i].BindIndex,
						1,
						&constantBuffers[i].ConstantBuffer);
				}
			}

			// --------------------------------------------------------
			// Sets a shader resource view in the pixel shader stage
			//
			// name - The name of the texture resource in the shader
			// srv - The shader resource view of the texture in GPU memory
			//
			// Returns true if a texture of the given name was found, false otherwise
			// --------------------------------------------------------
			bool SimplePixelShader::SetShaderResourceView(const std::string& name, ID3D11ShaderResourceView* srv)
			{
				// Look for the variable and verify
				const SimpleSRV* srvInfo = GetShaderResourceViewInfo(name);
				if (srvInfo == 0)
					return false;

				// Set the shader resource view
				deviceContext->PSSetShaderResources(srvInfo->BindIndex, 1, &srv);

				// Success
				return true;
			}

			bool SimplePixelShader::SetShaderResourceViewArray(const std::string& name, ID3D11ShaderResourceView** srv, int nviews)
			{
				// Look for the variable and verify
				const SimpleSRV* srvInfo = GetShaderResourceViewInfo(name);
				if (srvInfo == 0)
					return false;

				// Set the shader resource view
				deviceContext->PSSetShaderResources(srvInfo->BindIndex, nviews, srv);

				// Success
				return true;
			}
			// --------------------------------------------------------
			// Sets a sampler state in the pixel shader stage
			//
			// name - The name of the sampler state in the shader
			// samplerState - The sampler state in GPU memory
			//
			// Returns true if a sampler of the given name was found, false otherwise
			// --------------------------------------------------------
			bool SimplePixelShader::SetSamplerState(const std::string& name, ID3D11SamplerState* samplerState)
			{
				// Look for the variable and verify
				const SimpleSampler* sampInfo = GetSamplerInfo(name);
				if (sampInfo == 0)
					return false;

				// Set the shader resource view
				deviceContext->PSSetSamplers(sampInfo->BindIndex, 1, &samplerState);

				// Success
				return true;
			}




			///////////////////////////////////////////////////////////////////////////////
			// ------ SIMPLE DOMAIN SHADER ------------------------------------------------
			///////////////////////////////////////////////////////////////////////////////

			// --------------------------------------------------------
			// Constructor just calls the base
			// --------------------------------------------------------
			SimpleDomainShader::SimpleDomainShader(ID3D11Device* device, ID3D11DeviceContext* context)
				: ISimpleShader(device, context)
			{
				this->shader = 0;
			}

			// --------------------------------------------------------
			// Destructor - Clean up actual shader (base will be called automatically)
			// --------------------------------------------------------
			SimpleDomainShader::~SimpleDomainShader()
			{
				CleanUp();
			}

			// --------------------------------------------------------
			// Handles cleaning up shader and base class clean up
			// --------------------------------------------------------
			void SimpleDomainShader::CleanUp()
			{
				ISimpleShader::CleanUp();
				if (shader) { shader->Release(); shader = 0; }
			}

			// --------------------------------------------------------
			// Creates the DirectX domain shader
			//
			// shaderBlob - The shader's compiled code
			//
			// Returns true if shader is created correctly, false otherwise
			// --------------------------------------------------------
			bool SimpleDomainShader::CreateShader(ID3DBlob* shaderBlob)
			{
				// Clean up first, in the event this method is
				// called more than once on the same object
				this->CleanUp();

				// Create the shader from the blob
				HRESULT result = device->CreateDomainShader(
					shaderBlob->GetBufferPointer(),
					shaderBlob->GetBufferSize(),
					0,
					&shader);

				// Check the result
				return (result == S_OK);
			}

			// --------------------------------------------------------
			// Sets the domain shader and constant buffers for
			// future DirectX drawing
			// --------------------------------------------------------
			void SimpleDomainShader::SetShaderAndCBs()
			{
				// Is shader valid?
				if (!shaderValid) return;

				// Set the shader
				deviceContext->DSSetShader(shader, 0, 0);

				// Set the constant buffers
				for (unsigned int i = 0; i < constantBufferCount; i++)
				{
					deviceContext->DSSetConstantBuffers(
						constantBuffers[i].BindIndex,
						1,
						&constantBuffers[i].ConstantBuffer);
				}
			}

			// --------------------------------------------------------
			// Sets a shader resource view in the domain shader stage
			//
			// name - The name of the texture resource in the shader
			// srv - The shader resource view of the texture in GPU memory
			//
			// Returns true if a texture of the given name was found, false otherwise
			// --------------------------------------------------------
			bool SimpleDomainShader::SetShaderResourceView(const std::string& name, ID3D11ShaderResourceView* srv)
			{
				// Look for the variable and verify
				const SimpleSRV* srvInfo = GetShaderResourceViewInfo(name);
				if (srvInfo == 0)
					return false;

				// Set the shader resource view
				deviceContext->DSSetShaderResources(srvInfo->BindIndex, 1, &srv);

				// Success
				return true;
			}

			bool SimpleDomainShader::SetShaderResourceViewArray(const std::string& name, ID3D11ShaderResourceView** srv, int nviews)
			{
				// Look for the variable and verify
				const SimpleSRV* srvInfo = GetShaderResourceViewInfo(name);
				if (srvInfo == 0)
					return false;

				// Set the shader resource view
				deviceContext->DSSetShaderResources(srvInfo->BindIndex, nviews, srv);

				// Success
				return true;
			}

			// --------------------------------------------------------
			// Sets a sampler state in the domain shader stage
			//
			// name - The name of the sampler state in the shader
			// samplerState - The sampler state in GPU memory
			//
			// Returns true if a sampler of the given name was found, false otherwise
			// --------------------------------------------------------
			bool SimpleDomainShader::SetSamplerState(const std::string& name, ID3D11SamplerState* samplerState)
			{
				// Look for the variable and verify
				const SimpleSampler* sampInfo = GetSamplerInfo(name);
				if (sampInfo == 0)
					return false;

				// Set the shader resource view
				deviceContext->DSSetSamplers(sampInfo->BindIndex, 1, &samplerState);

				// Success
				return true;
			}



			///////////////////////////////////////////////////////////////////////////////
			// ------ SIMPLE HULL SHADER --------------------------------------------------
			///////////////////////////////////////////////////////////////////////////////

			// --------------------------------------------------------
			// Constructor just calls the base
			// --------------------------------------------------------
			SimpleHullShader::SimpleHullShader(ID3D11Device* device, ID3D11DeviceContext* context)
				: ISimpleShader(device, context)
			{
				this->shader = 0;
			}

			// --------------------------------------------------------
			// Destructor - Clean up actual shader (base will be called automatically)
			// --------------------------------------------------------
			SimpleHullShader::~SimpleHullShader()
			{
				CleanUp();
			}

			// --------------------------------------------------------
			// Handles cleaning up shader and base class clean up
			// --------------------------------------------------------
			void SimpleHullShader::CleanUp()
			{
				ISimpleShader::CleanUp();
				if (shader) { shader->Release(); shader = 0; }
			}

			// --------------------------------------------------------
			// Creates the DirectX hull shader
			//
			// shaderBlob - The shader's compiled code
			//
			// Returns true if shader is created correctly, false otherwise
			// --------------------------------------------------------
			bool SimpleHullShader::CreateShader(ID3DBlob* shaderBlob)
			{
				// Clean up first, in the event this method is
				// called more than once on the same object
				this->CleanUp();

				// Create the shader from the blob
				HRESULT result = device->CreateHullShader(
					shaderBlob->GetBufferPointer(),
					shaderBlob->GetBufferSize(),
					0,
					&shader);

				// Check the result
				return (result == S_OK);
			}

			// --------------------------------------------------------
			// Sets the hull shader and constant buffers for
			// future DirectX drawing
			// --------------------------------------------------------
			void SimpleHullShader::SetShaderAndCBs()
			{
				// Is shader valid?
				if (!shaderValid) return;

				// Set the shader
				deviceContext->HSSetShader(shader, 0, 0);

				// Set the constant buffers?
				for (unsigned int i = 0; i < constantBufferCount; i++)
				{
					deviceContext->HSSetConstantBuffers(
						constantBuffers[i].BindIndex,
						1,
						&constantBuffers[i].ConstantBuffer);
				}
			}

			// --------------------------------------------------------
			// Sets a shader resource view in the hull shader stage
			//
			// name - The name of the texture resource in the shader
			// srv - The shader resource view of the texture in GPU memory
			//
			// Returns true if a texture of the given name was found, false otherwise
			// --------------------------------------------------------
			bool SimpleHullShader::SetShaderResourceView(const std::string& name, ID3D11ShaderResourceView* srv)
			{
				// Look for the variable and verify
				const SimpleSRV* srvInfo = GetShaderResourceViewInfo(name);
				if (srvInfo == 0)
					return false;

				// Set the shader resource view
				deviceContext->HSSetShaderResources(srvInfo->BindIndex, 1, &srv);

				// Success
				return true;
			}

			bool SimpleHullShader::SetShaderResourceViewArray(const std::string& name, ID3D11ShaderResourceView** srv, int nviews)
			{
				// Look for the variable and verify
				const SimpleSRV* srvInfo = GetShaderResourceViewInfo(name);
				if (srvInfo == 0)
					return false;

				// Set the shader resource view
				deviceContext->HSSetShaderResources(srvInfo->BindIndex, nviews, srv);

				// Success
				return true;
			}

			// --------------------------------------------------------
			// Sets a sampler state in the hull shader stage
			//
			// name - The name of the sampler state in the shader
			// samplerState - The sampler state in GPU memory
			//
			// Returns true if a sampler of the given name was found, false otherwise
			// --------------------------------------------------------
			bool SimpleHullShader::SetSamplerState(const std::string& name, ID3D11SamplerState* samplerState)
			{
				// Look for the variable and verify
				const SimpleSampler* sampInfo = GetSamplerInfo(name);
				if (sampInfo == 0)
					return false;

				// Set the shader resource view
				deviceContext->HSSetSamplers(sampInfo->BindIndex, 1, &samplerState);

				// Success
				return true;
			}




			///////////////////////////////////////////////////////////////////////////////
			// ------ SIMPLE GEOMETRY SHADER ----------------------------------------------
			///////////////////////////////////////////////////////////////////////////////

			// --------------------------------------------------------
			// Constructor calls the base and sets up potential stream-out options
			// --------------------------------------------------------
			SimpleGeometryShader::SimpleGeometryShader(ID3D11Device* device, ID3D11DeviceContext* context, bool useStreamOut, bool allowStreamOutRasterization)
				: ISimpleShader(device, context)
			{
				this->shader = 0;
				this->useStreamOut = useStreamOut;
				this->allowStreamOutRasterization = allowStreamOutRasterization;
			}

			// --------------------------------------------------------
			// Destructor - Clean up actual shader (base will be called automatically)
			// --------------------------------------------------------
			SimpleGeometryShader::~SimpleGeometryShader()
			{
				CleanUp();
			}

			// --------------------------------------------------------
			// Handles cleaning up shader and base class clean up
			// --------------------------------------------------------
			void SimpleGeometryShader::CleanUp()
			{
				ISimpleShader::CleanUp();
				if (shader) { shader->Release(); shader = 0; }
			}

			// --------------------------------------------------------
			// Creates the DirectX Geometry shader
			//
			// shaderBlob - The shader's compiled code
			//
			// Returns true if shader is created correctly, false otherwise
			// --------------------------------------------------------
			bool SimpleGeometryShader::CreateShader(ID3DBlob* shaderBlob)
			{
				// Clean up first, in the event this method is
				// called more than once on the same object
				this->CleanUp();

				// Using stream out?
				if (useStreamOut)
					return this->CreateShaderWithStreamOut(shaderBlob);

				// Create the shader from the blob
				HRESULT result = device->CreateGeometryShader(
					shaderBlob->GetBufferPointer(),
					shaderBlob->GetBufferSize(),
					0,
					&shader);

				// Check the result
				return (result == S_OK);
			}

			// --------------------------------------------------------
			// Creates the DirectX Geometry shader and sets it up for
			// stream output, if possible.
			//
			// shaderBlob - The shader's compiled code
			//
			// Returns true if shader is created correctly, false otherwise
			// --------------------------------------------------------
			bool SimpleGeometryShader::CreateShaderWithStreamOut(ID3DBlob* shaderBlob)
			{
				// Clean up first, in the event this method is
				// called more than once on the same object
				this->CleanUp();

				// Reflect shader info
				ID3D11ShaderReflection* refl;
				D3DReflect(
					shaderBlob->GetBufferPointer(),
					shaderBlob->GetBufferSize(),
					IID_ID3D11ShaderReflection,
					(void**)&refl);

				// Get shader info
				D3D11_SHADER_DESC shaderDesc;
				refl->GetDesc(&shaderDesc);

				// Set up the output signature
				streamOutVertexSize = 0;
				std::vector<D3D11_SO_DECLARATION_ENTRY> soDecl;
				for (unsigned int i = 0; i < shaderDesc.OutputParameters; i++)
				{
					// Get the info about this entry
					D3D11_SIGNATURE_PARAMETER_DESC paramDesc;
					refl->GetOutputParameterDesc(i, &paramDesc);

					// Create the SO Declaration
					D3D11_SO_DECLARATION_ENTRY entry;
					entry.SemanticIndex = paramDesc.SemanticIndex;
					entry.SemanticName = paramDesc.SemanticName;
					entry.Stream = paramDesc.Stream;
					entry.StartComponent = 0; // Assume starting at 0
					entry.OutputSlot = 0; // Assume the first output slot

					// Check the mask to determine how many components are used
					entry.ComponentCount = CalcComponentCount(paramDesc.Mask);

					// Increment the size
					streamOutVertexSize += entry.ComponentCount * sizeof(float);

					// Add to the declaration
					soDecl.push_back(entry);
				}

				// Rasterization allowed?
				unsigned int rast = allowStreamOutRasterization ? 0 : D3D11_SO_NO_RASTERIZED_STREAM;

				// Create the shader
				HRESULT result = device->CreateGeometryShaderWithStreamOutput(
					shaderBlob->GetBufferPointer(), // Shader blob pointer
					shaderBlob->GetBufferSize(),    // Shader blob size
					&soDecl[0],                     // Stream out declaration
					(unsigned int)soDecl.size(),    // Number of declaration entries
					NULL,                           // Buffer strides (not used - assume tightly packed?)
					0,                              // No buffer strides
					rast,                           // Index of the stream to rasterize (if any)
					NULL,                           // Not using class linkage
					&shader);

				return (result == S_OK);
			}

			// --------------------------------------------------------
			// Creates a vertex buffer that is compatible with the stream output
			// delcaration that was used to create the shader.  This buffer will
			// not be cleaned up (Released) by the simple shader - you must clean
			// it up yourself when you're done with it.  Immediately returns
			// false if the shader was not created with stream output, the shader
			// isn't valid or the determined stream out vertex size is zero.
			//
			// buffer - Pointer to an ID3D11Buffer pointer to hold the buffer ref
			// vertexCount - Amount of vertices the buffer should hold
			//
			// Returns true if buffer is created successfully AND stream output
			// was used to create the shader.  False otherwise.
			// --------------------------------------------------------
			bool SimpleGeometryShader::CreateCompatibleStreamOutBuffer(ID3D11Buffer** buffer, int vertexCount)
			{
				// Was stream output actually used?
				if (!this->useStreamOut || !shaderValid || streamOutVertexSize == 0)
					return false;

				// Set up the buffer description
				D3D11_BUFFER_DESC desc;
				desc.BindFlags = D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_VERTEX_BUFFER;
				desc.ByteWidth = streamOutVertexSize * vertexCount;
				desc.CPUAccessFlags = 0;
				desc.MiscFlags = 0;
				desc.StructureByteStride = 0;
				desc.Usage = D3D11_USAGE_DEFAULT;

				// Attempt to create the buffer and return the result
				HRESULT result = device->CreateBuffer(&desc, 0, buffer);
				return (result == S_OK);
			}

			// --------------------------------------------------------
			// Helper method to unbind all stream out buffers from the SO stage
			// --------------------------------------------------------
			void SimpleGeometryShader::UnbindStreamOutStage(ID3D11DeviceContext* deviceContext)
			{
				unsigned int offset = 0;
				ID3D11Buffer* unset[1] = { 0 };
				deviceContext->SOSetTargets(1, unset, &offset);
			}

			// --------------------------------------------------------
			// Sets the geometry shader and constant buffers for
			// future DirectX drawing
			// --------------------------------------------------------
			void SimpleGeometryShader::SetShaderAndCBs()
			{
				// Is shader valid?
				if (!shaderValid) return;

				// Set the shader
				deviceContext->GSSetShader(shader, 0, 0);

				// Set the constant buffers?
				for (unsigned int i = 0; i < constantBufferCount; i++)
				{
					deviceContext->GSSetConstantBuffers(
						constantBuffers[i].BindIndex,
						1,
						&constantBuffers[i].ConstantBuffer);
				}
			}

			// --------------------------------------------------------
			// Sets a shader resource view in the Geometry shader stage
			//
			// name - The name of the texture resource in the shader
			// srv - The shader resource view of the texture in GPU memory
			//
			// Returns true if a texture of the given name was found, false otherwise
			// --------------------------------------------------------
			bool SimpleGeometryShader::SetShaderResourceView(const std::string& name, ID3D11ShaderResourceView* srv)
			{
				// Look for the variable and verify
				const SimpleSRV* srvInfo = GetShaderResourceViewInfo(name);
				if (srvInfo == 0)
					return false;

				// Set the shader resource view
				deviceContext->GSSetShaderResources(srvInfo->BindIndex, 1, &srv);

				// Success
				return true;
			}

			bool SimpleGeometryShader::SetShaderResourceViewArray(const std::string& name, ID3D11ShaderResourceView** srv, int nviews)
			{
				// Look for the variable and verify
				const SimpleSRV* srvInfo = GetShaderResourceViewInfo(name);
				if (srvInfo == 0)
					return false;

				// Set the shader resource view
				deviceContext->GSSetShaderResources(srvInfo->BindIndex, nviews, srv);

				// Success
				return true;
			}

			// --------------------------------------------------------
			// Sets a sampler state in the Geometry shader stage
			//
			// name - The name of the sampler state in the shader
			// samplerState - The sampler state in GPU memory
			//
			// Returns true if a sampler of the given name was found, false otherwise
			// --------------------------------------------------------
			bool SimpleGeometryShader::SetSamplerState(const std::string& name, ID3D11SamplerState* samplerState)
			{
				// Look for the variable and verify
				const SimpleSampler* sampInfo = GetSamplerInfo(name);
				if (sampInfo == 0)
					return false;

				// Set the shader resource view
				deviceContext->GSSetSamplers(sampInfo->BindIndex, 1, &samplerState);

				// Success
				return true;
			}

			// --------------------------------------------------------
			// Calculates the number of components specified by a parameter description mask
			//
			// mask - The mask to check (only values 0 - 15 are considered)
			//
			// Returns an integer between 0 - 4 inclusive
			// --------------------------------------------------------
			unsigned int SimpleGeometryShader::CalcComponentCount(unsigned int mask)
			{
				unsigned int result = 0;
				result += (unsigned int)((mask & 1) == 1);
				result += (unsigned int)((mask & 2) == 2);
				result += (unsigned int)((mask & 4) == 4);
				result += (unsigned int)((mask & 8) == 8);
				return result;
			}



			///////////////////////////////////////////////////////////////////////////////
			// ------ SIMPLE COMPUTE SHADER -----------------------------------------------
			///////////////////////////////////////////////////////////////////////////////

			// --------------------------------------------------------
			// Constructor just calls the base
			// --------------------------------------------------------
			SimpleComputeShader::SimpleComputeShader(ID3D11Device* device, ID3D11DeviceContext* context)
				: ISimpleShader(device, context)
			{
				this->shader = 0;
			}

			// --------------------------------------------------------
			// Destructor - Clean up actual shader (base will be called automatically)
			// --------------------------------------------------------
			SimpleComputeShader::~SimpleComputeShader()
			{
				CleanUp();
			}

			// --------------------------------------------------------
			// Handles cleaning up shader and base class clean up
			// --------------------------------------------------------
			void SimpleComputeShader::CleanUp()
			{
				ISimpleShader::CleanUp();
				if (shader) { shader->Release(); shader = 0; }

				uavTable.clear();
			}

			// --------------------------------------------------------
			// Creates the DirectX Compute shader
			//
			// shaderBlob - The shader's compiled code
			//
			// Returns true if shader is created correctly, false otherwise
			// --------------------------------------------------------
			bool SimpleComputeShader::CreateShader(ID3DBlob* shaderBlob)
			{
				// Clean up first, in the event this method is
				// called more than once on the same object
				this->CleanUp();

				// Create the shader from the blob
				HRESULT result = device->CreateComputeShader(
					shaderBlob->GetBufferPointer(),
					shaderBlob->GetBufferSize(),
					0,
					&shader);

				// Was the shader created correctly?
				if (result != S_OK)
					return false;

				// Set up shader reflection to get information about UAV's
				ID3D11ShaderReflection* refl;
				D3DReflect(
					shaderBlob->GetBufferPointer(),
					shaderBlob->GetBufferSize(),
					IID_ID3D11ShaderReflection,
					(void**)&refl);

				// Get the description of the shader
				D3D11_SHADER_DESC shaderDesc;
				result = refl->GetDesc(&shaderDesc);

				// Grab the thread info
				threadsTotal = refl->GetThreadGroupSize(
					&threadsX,
					&threadsY,
					&threadsZ);

				// Loop and get all UAV resources
				unsigned int resourceCount = shaderDesc.BoundResources;
				for (unsigned int r = 0; r < resourceCount; r++)
				{
					// Get this resource's description
					D3D11_SHADER_INPUT_BIND_DESC resourceDesc;
					refl->GetResourceBindingDesc(r, &resourceDesc);

					// Check the type, looking for any kind of UAV
					switch (resourceDesc.Type)
					{
					case D3D_SIT_UAV_APPEND_STRUCTURED:
					case D3D_SIT_UAV_CONSUME_STRUCTURED:
					case D3D_SIT_UAV_RWBYTEADDRESS:
					case D3D_SIT_UAV_RWSTRUCTURED:
					case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
					case D3D_SIT_UAV_RWTYPED:
						uavTable.insert(std::pair<std::string, unsigned int>(resourceDesc.Name, resourceDesc.BindPoint));
					}
				}

				// All set
				refl->Release();
				return true;
			}

			// --------------------------------------------------------
			// Sets the Compute shader and constant buffers for
			// future DirectX drawing
			// --------------------------------------------------------
			void SimpleComputeShader::SetShaderAndCBs()
			{
				// Is shader valid?
				if (!shaderValid) return;

				// Set the shader
				deviceContext->CSSetShader(shader, 0, 0);

				// Set the constant buffers?
				for (unsigned int i = 0; i < constantBufferCount; i++)
				{
					deviceContext->CSSetConstantBuffers(
						constantBuffers[i].BindIndex,
						1,
						&constantBuffers[i].ConstantBuffer);
				}
			}

			// --------------------------------------------------------
			// Dispatches the compute shader with the specified amount 
			// of groups, using the number of threads per group
			// specified in the shader file itself
			//
			// For example, calling this method with params (5,1,1) on
			// a shader with (8,2,2) threads per group will launch a 
			// total of 160 threads: ((5 * 8) * (1 * 2) * (1 * 2))
			//
			// This is identical to using the device context's 
			// Dispatch() method yourself.  
			//
			// Note: This will dispatch the currently active shader, 
			// not necessarily THIS shader. Be sure to activate this
			// shader with SetShader() before calling Dispatch
			//
			// groupsX - Numbers of groups in the X dimension
			// groupsY - Numbers of groups in the Y dimension
			// groupsZ - Numbers of groups in the Z dimension
			// --------------------------------------------------------
			void SimpleComputeShader::DispatchByGroups(unsigned int groupsX, unsigned int groupsY, unsigned int groupsZ)
			{
				deviceContext->Dispatch(groupsX, groupsY, groupsZ);
			}

			// --------------------------------------------------------
			// Dispatches the compute shader with AT LEAST the 
			// specified amount of threads, calculating the number of
			// groups to dispatch using the number of threads per group
			// specified in the shader file itself
			//
			// For example, calling this method with params (10,3,3) on
			// a shader with (5,2,2) threads per group will launch 
			// 8 total groups and 160 total threads, calculated by:
			// Groups: ceil(10/5) * ceil(3/2) * ceil(3/2) = 8
			// Threads: ((2 * 5) * (2 * 2) * (2 * 2)) = 160
			//
			// Note: This will dispatch the currently active shader, 
			// not necessarily THIS shader. Be sure to activate this
			// shader with SetShader() before calling Dispatch
			//
			// threadsX - Desired numbers of threads in the X dimension
			// threadsY - Desired numbers of threads in the Y dimension
			// threadsZ - Desired numbers of threads in the Z dimension
			// --------------------------------------------------------
			void SimpleComputeShader::DispatchByThreads(unsigned int threadsX, unsigned int threadsY, unsigned int threadsZ)
			{
				deviceContext->Dispatch(
					max((unsigned int)ceil((float)threadsX / this->threadsX), 1),
					max((unsigned int)ceil((float)threadsY / this->threadsY), 1),
					max((unsigned int)ceil((float)threadsZ / this->threadsZ), 1));
			}

			// --------------------------------------------------------
			// Sets a shader resource view in the Compute shader stage
			//
			// name - The name of the texture resource in the shader
			// srv - The shader resource view of the texture in GPU memory
			//
			// Returns true if a texture of the given name was found, false otherwise
			// --------------------------------------------------------
			bool SimpleComputeShader::SetShaderResourceView(const std::string& name, ID3D11ShaderResourceView* srv)
			{
				// Look for the variable and verify
				const SimpleSRV* srvInfo = GetShaderResourceViewInfo(name);
				if (srvInfo == 0)
					return false;

				// Set the shader resource view
				deviceContext->CSSetShaderResources(srvInfo->BindIndex, 1, &srv);

				// Success
				return true;
			}

			bool SimpleComputeShader::SetShaderResourceViewArray(const std::string& name, ID3D11ShaderResourceView** srv, int nviews)
			{
				// Look for the variable and verify
				const SimpleSRV* srvInfo = GetShaderResourceViewInfo(name);
				if (srvInfo == 0)
					return false;

				// Set the shader resource view
				deviceContext->CSSetShaderResources(srvInfo->BindIndex, nviews, srv);

				// Success
				return true;
			}

			// --------------------------------------------------------
			// Sets a sampler state in the Compute shader stage
			//
			// name - The name of the sampler state in the shader
			// samplerState - The sampler state in GPU memory
			//
			// Returns true if a sampler of the given name was found, false otherwise
			// --------------------------------------------------------
			bool SimpleComputeShader::SetSamplerState(const std::string& name, ID3D11SamplerState* samplerState)
			{
				// Look for the variable and verify
				const SimpleSampler* sampInfo = GetSamplerInfo(name);
				if (sampInfo == 0)
					return false;

				// Set the shader resource view
				deviceContext->CSSetSamplers(sampInfo->BindIndex, 1, &samplerState);

				// Success
				return true;
			}

			// --------------------------------------------------------
			// Sets an unordered access view in the Compute shader stage
			//
			// name - The name of the sampler state in the shader
			// uav - The UAV in GPU memory
			// appendConsumeOffset - Used for append or consume UAV's (optional)
			//
			// Returns true if a UAV of the given name was found, false otherwise
			// --------------------------------------------------------
			bool SimpleComputeShader::SetUnorderedAccessView(const std::string& name, ID3D11UnorderedAccessView* uav, unsigned int appendConsumeOffset)
			{
				// Look for the variable and verify
				unsigned int bindIndex = GetUnorderedAccessViewIndex(name);
				if (bindIndex == -1)
					return false;

				// Set the shader resource view
				deviceContext->CSSetUnorderedAccessViews(bindIndex, 1, &uav, &appendConsumeOffset);

				// Success
				return true;
			}

			// --------------------------------------------------------
			// Gets the index of the specified UAV (or -1)
			// --------------------------------------------------------
			int SimpleComputeShader::GetUnorderedAccessViewIndex(const std::string& name)
			{
				// Look for the key
				std::unordered_map<std::string, unsigned int>::iterator result =
					uavTable.find(name);

				// Did we find the key?
				if (result == uavTable.end())
					return -1;

				// Success
				return result->second;
			}

		}
	}
}