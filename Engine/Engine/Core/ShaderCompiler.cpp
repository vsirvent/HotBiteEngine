#include "ShaderCompiler.h"
#include "Log.h"
#include "DxcSupport.h"

#include <Windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#pragma comment(lib, "d3dcompiler.lib")

namespace fs = std::filesystem;

namespace HotBite {
	namespace Engine {
		namespace Core {

			ShaderCompiler* ShaderCompiler::sInstance = nullptr;

			namespace {

				std::string ToLower(const std::string& s)
				{
					std::string out = s;
					std::transform(out.begin(), out.end(), out.begin(),
						[](unsigned char c) { return (char)std::tolower(c); });
					return out;
				}

				std::string Normalize(const std::string& path)
				{
					std::error_code ec;
					fs::path p = fs::weakly_canonical(fs::path(path), ec);
					return ec ? fs::path(path).string() : p.string();
				}

				// Answers DXC's #include requests. DXC resolves a quoted include against the including file's folder and
				// every -I folder itself and asks for each candidate path in turn; the first one that exists is the file.
				// Every file it opens is recorded, which is how a reload knows that an edit to a shared .hlsli reaches this
				// shader.
				class IncludeHandler : public IDxcIncludeHandler {
				public:
					IncludeHandler(const std::vector<std::string>& search_dirs, std::vector<std::string>& deps,
						std::vector<uint64_t>* times = nullptr)
						: search(search_dirs), dependencies(deps), dependency_times(times) {
					}

					HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR file_name, IDxcBlob** out_blob) override
					{
						*out_blob = nullptr;
						std::error_code ec;
						fs::path candidate(file_name);
						std::string resolved;
						if (fs::is_regular_file(candidate, ec)) {
							resolved = candidate.string();
						}
						else if (candidate.is_relative()) {
							for (const std::string& dir : search) {
								fs::path in_dir = fs::path(dir) / candidate;
								if (fs::is_regular_file(in_dir, ec)) {
									resolved = in_dir.string();
									break;
								}
							}
						}
						if (resolved.empty()) {
							return E_FAIL;
						}
						std::ifstream in(resolved, std::ios::binary);
						if (!in) {
							return E_FAIL;
						}
						std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
						Microsoft::WRL::ComPtr<IDxcBlobEncoding> blob;
						if (FAILED(DxcSupport::Utils()->CreateBlob(text.data(), (UINT32)text.size(), DXC_CP_UTF8, &blob))) {
							return E_FAIL;
						}
						dependencies.push_back(Normalize(resolved));
						if (dependency_times != nullptr) {
							dependency_times->push_back(ShaderCompiler::FileTime(resolved));
						}
						*out_blob = blob.Detach();
						return S_OK;
					}

					HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
					{
						if (riid == __uuidof(IDxcIncludeHandler) || riid == __uuidof(IUnknown)) {
							*object = static_cast<IDxcIncludeHandler*>(this);
							return S_OK;
						}
						*object = nullptr;
						return E_NOINTERFACE;
					}

					//Lives on the stack of the compile that uses it.
					ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
					ULONG STDMETHODCALLTYPE Release() override { return 1; }

				private:
					const std::vector<std::string>& search;
					std::vector<std::string>& dependencies;
					std::vector<uint64_t>* dependency_times;
				};

				// The same for fxc (ID3D12 shaders that stay on it, see UsesFxc): relative to the file that contains the #include first,
				// then against the registered source folders, and every file opened is recorded.
				class FxcIncludeHandler : public ID3DInclude {
				public:
					FxcIncludeHandler(const std::string& source_dir, const std::vector<std::string>& search_dirs,
						std::vector<std::string>& deps, std::vector<uint64_t>* times)
						: root(source_dir), search(search_dirs), dependencies(deps), dependency_times(times) {
					}

					HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR file_name, LPCVOID parent_data, LPCVOID* out_data, UINT* out_bytes) override
					{
						//The compiler identifies the includer only by the data pointer it was handed, so the directory of every open file
						//is kept under that pointer. A null parent is the top-level source.
						std::string base = root;
						if (parent_data != nullptr) {
							auto it = dir_of_data.find(parent_data);
							if (it != dir_of_data.end()) {
								base = it->second;
							}
						}
						std::string resolved;
						std::error_code ec;
						fs::path direct = fs::path(base) / file_name;
						if (fs::exists(direct, ec)) {
							resolved = direct.string();
						}
						else {
							for (const std::string& dir : search) {
								fs::path candidate = fs::path(dir) / file_name;
								if (fs::exists(candidate, ec)) {
									resolved = candidate.string();
									break;
								}
							}
						}
						if (resolved.empty()) {
							return E_FAIL;
						}
						std::ifstream in(resolved, std::ios::binary);
						if (!in) {
							return E_FAIL;
						}
						std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
						char* buffer = new char[text.size() + 1];
						memcpy(buffer, text.data(), text.size());
						buffer[text.size()] = '\0';
						dir_of_data[buffer] = fs::path(resolved).parent_path().string();
						dependencies.push_back(Normalize(resolved));
						if (dependency_times != nullptr) {
							dependency_times->push_back(ShaderCompiler::FileTime(resolved));
						}
						*out_data = buffer;
						*out_bytes = (UINT)text.size();
						return S_OK;
					}

					HRESULT __stdcall Close(LPCVOID data) override
					{
						dir_of_data.erase(data);
						delete[](char*)data;
						return S_OK;
					}

				private:
					std::string root;
					const std::vector<std::string>& search;
					std::vector<std::string>& dependencies;
					std::vector<uint64_t>* dependency_times;
					std::unordered_map<const void*, std::string> dir_of_data;
				};

				// Shaders that are built by fxc (SM 5.0, /O3) and not by DXC, because DXC makes them 2-2.5x slower from the same
				// HLSL: keep equal to the FxCompile items with HotBiteFxc metadata in Engine.vcxproj (Docs/D3D12_MIGRATION.md,
				// Performance). A reload has to make what the build made, or editing the shader would change what it costs.
				bool UsesFxc(const std::string& source_path)
				{
					static const char* const FXC_SHADERS[] = { "deferredlightcs", "deferredlightrayscs" };
					const std::string stem = ToLower(fs::path(source_path).stem().string());
					for (const char* name : FXC_SHADERS) {
						if (stem == name) {
							return true;
						}
					}
					return false;
				}

				std::string BlobText(ID3DBlob* blob)
				{
					if (blob == nullptr || blob->GetBufferSize() == 0) {
						return {};
					}
					return std::string((const char*)blob->GetBufferPointer(), blob->GetBufferSize());
				}

				std::string BlobText(IDxcBlobUtf8* blob)
				{
					if (blob == nullptr || blob->GetStringLength() == 0) {
						return {};
					}
					return std::string(blob->GetStringPointer(), blob->GetStringLength());
				}

				//Compilation flags, the same as the build's (Engine/Dxc.targets): HLSL 2018 (what fxc compiled), strictness, and
				//no warnings - the sources carry a few that DXC alone reports and that nothing acts on. Nothing else in Release, so a
				//shader reloaded here is generated the way the .cso beside it was and a measurement taken after a reload stays
				//comparable to one taken before. Debug matches the Debug configuration's -Od.
#ifdef _DEBUG
				const wchar_t* const COMPILE_ARGS[] = { L"-HV", L"2018", L"-Ges", L"-no-warnings", L"-Od" };
#else
				const wchar_t* const COMPILE_ARGS[] = { L"-HV", L"2018", L"-Ges", L"-no-warnings" };
#endif

				//Debug symbols (-Zi) go into the bytecode, as the build's default does (HotBiteShaderDebug=embed): the variable names,
				//the line table and the source text, which is what PIX needs to show a shader's source and to attribute its cost to
				//lines. They are a separate part of the container: the code itself is the same with or without them. A Debug build
				//always has them; HOTBITE_SHADER_DEBUG=0 leaves them out of the shaders compiled here.
				bool DebugSymbolsWanted()
				{
#ifdef _DEBUG
					return true;
#else
					static const bool wanted = []() {
						char value[8] = {};
						size_t len = 0;
						return !(getenv_s(&len, value, sizeof(value), "HOTBITE_SHADER_DEBUG") == 0 && len > 1 && value[0] == '0');
					}();
					return wanted;
#endif
				}
			}

			ShaderCompiler* ShaderCompiler::Get()
			{
				if (sInstance == nullptr) {
					sInstance = new ShaderCompiler();
				}
				return sInstance;
			}

			void ShaderCompiler::Release()
			{
				delete sInstance;
				sInstance = nullptr;
			}

			uint64_t ShaderCompiler::FileTime(const std::string& path)
			{
				std::error_code ec;
				auto t = fs::last_write_time(fs::path(path), ec);
				if (ec) {
					return 0;
				}
				return (uint64_t)t.time_since_epoch().count();
			}

			int ShaderCompiler::IndexFolder(const std::string& folder)
			{
				int count = 0;
				std::error_code ec;
				for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
					it != end; it.increment(ec)) {
					if (ec) {
						break;
					}
					if (!it->is_regular_file(ec)) {
						continue;
					}
					if (ToLower(it->path().extension().string()) != ".hlsl") {
						continue;
					}
					sources[ToLower(it->path().stem().string())] = Normalize(it->path().string());
					++count;
				}
				return count;
			}

			void ShaderCompiler::Reindex()
			{
				sources.clear();
				//In registration order, so the folder added last wins a name collision.
				for (const std::string& folder : folders) {
					IndexFolder(folder);
				}
			}

			bool ShaderCompiler::AddSourceFolder(const std::string& folder, int* found)
			{
				std::error_code ec;
				if (folder.empty() || !fs::is_directory(fs::path(folder), ec)) {
					if (found != nullptr) {
						*found = 0;
					}
					return false;
				}
				const std::string normalized = Normalize(folder);
				auto existing = std::find(folders.begin(), folders.end(), normalized);
				if (existing != folders.end()) {
					//Re-adding is a rescan, and moves the folder to the top of the
					//priority order - which is what a caller re-adding it means.
					folders.erase(existing);
				}
				folders.push_back(normalized);
				Reindex();
				if (found != nullptr) {
					*found = IndexFolder(normalized);
				}
				LOG_INFO("ShaderCompiler: source folder %s (%zu shader sources indexed)",
					normalized.c_str(), sources.size());
				return true;
			}

			bool ShaderCompiler::RemoveSourceFolder(const std::string& folder)
			{
				const std::string normalized = Normalize(folder);
				auto existing = std::find(folders.begin(), folders.end(), normalized);
				if (existing == folders.end()) {
					return false;
				}
				folders.erase(existing);
				Reindex();
				return true;
			}

			void ShaderCompiler::AddDefaultSourceFolders()
			{
				if (defaults_added) {
					return;
				}
				defaults_added = true;

				//Walk up from the executable looking for the engine's shader tree. The
				//build outputs sit at <root>\Solution\x64\<config>, so the repo root is
				//three levels up - but a host may be launched from anywhere, so the
				//search is by content rather than by a fixed number of steps.
				char exe[MAX_PATH] = {};
				GetModuleFileNameA(nullptr, exe, MAX_PATH);
				std::error_code ec;
				fs::path dir = fs::path(exe).parent_path();
				for (int level = 0; level < 8 && !dir.empty(); ++level) {
					fs::path engine_shaders = dir / "Engine" / "Engine" / "Core" / "Shaders";
					if (fs::is_directory(engine_shaders, ec)) {
						AddSourceFolder(engine_shaders.string());
						//Game-side shaders (Tests\DemoGame\TerrainPS.hlsl and friends)
						//compile into the same output folder and are reloadable on the
						//same terms.
						fs::path tests = dir / "Tests";
						if (fs::is_directory(tests, ec)) {
							AddSourceFolder(tests.string());
						}
						break;
					}
					if (!dir.has_parent_path() || dir.parent_path() == dir) {
						break;
					}
					dir = dir.parent_path();
				}

				//An explicit list always wins over what was probed: this is how a game
				//outside this repo points the reloader at its own shaders.
				char* env = nullptr;
				size_t env_len = 0;
				if (_dupenv_s(&env, &env_len, "HOTBITE_SHADER_SRC") == 0 && env != nullptr) {
					std::string list(env);
					free(env);
					size_t start = 0;
					while (start <= list.size()) {
						size_t sep = list.find(';', start);
						std::string item = list.substr(start, (sep == std::string::npos) ? std::string::npos : sep - start);
						if (!item.empty() && !AddSourceFolder(item)) {
							LOG_WARN("ShaderCompiler: HOTBITE_SHADER_SRC entry is not a folder: %s", item.c_str());
						}
						if (sep == std::string::npos) {
							break;
						}
						start = sep + 1;
					}
				}

				if (sources.empty()) {
					LOG_WARN("ShaderCompiler: no shader sources found; only .cso reloading is available. "
						"Set HOTBITE_SHADER_SRC to the folder holding the .hlsl files.");
				}
			}

			std::string ShaderCompiler::FindSource(const std::string& shader_name) const
			{
				if (shader_name.empty()) {
					return {};
				}
				//Callers name shaders the way the engine loads them, "MainRenderPS.cso";
				//a bare stem is accepted too so the automation surface can take either.
				std::string stem = ToLower(fs::path(shader_name).stem().string());
				auto it = sources.find(stem);
				return (it == sources.end()) ? std::string() : it->second;
			}

			bool ShaderCompiler::Compile(const std::string& source_path, const std::string& profile,
				Result& out, const std::string& entry)
			{
				return CompileWith(source_path, profile, folders, out, entry);
			}

			bool ShaderCompiler::CompileWith(const std::string& source_path, const std::string& profile,
				const std::vector<std::string>& search_dirs, Result& out, const std::string& entry)
			{
				out.blob = nullptr;
				out.error.clear();
				out.dependencies.clear();
				out.dependency_times.clear();

				std::error_code ec;
				if (!fs::is_regular_file(fs::path(source_path), ec)) {
					out.error = "shader source not found: " + source_path;
					return false;
				}
				out.dependencies.push_back(Normalize(source_path));
				out.dependency_times.push_back(FileTime(source_path));

				if (UsesFxc(source_path)) {
					return CompileFxc(source_path, entry, search_dirs, out);
				}
				if (!DxcSupport::Available()) {
					out.error = "dxcompiler.dll is not available";
					return false;
				}
				IncludeHandler includes(search_dirs, out.dependencies, &out.dependency_times);

				std::ifstream in(source_path, std::ios::binary);
				std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				DxcBuffer source = {};
				source.Ptr = text.data();
				source.Size = text.size();
				source.Encoding = DXC_CP_UTF8;

				//The file's own folder is searched first (DXC does that for quoted includes), then the registered folders.
				const std::wstring wide_source = fs::path(source_path).wstring();
				const std::wstring wide_entry = fs::path(entry).wstring();
				const std::wstring wide_profile = fs::path(profile).wstring();
				std::vector<std::wstring> wide_dirs;
				wide_dirs.reserve(search_dirs.size());
				for (const std::string& dir : search_dirs) {
					wide_dirs.push_back(fs::path(dir).wstring());
				}
				std::vector<LPCWSTR> arguments = { wide_source.c_str(), L"-E", wide_entry.c_str(), L"-T", wide_profile.c_str() };
				for (const wchar_t* argument : COMPILE_ARGS) {
					arguments.push_back(argument);
				}
				if (DebugSymbolsWanted()) {
					arguments.push_back(L"-Zi");
				}
				for (const std::wstring& dir : wide_dirs) {
					arguments.push_back(L"-I");
					arguments.push_back(dir.c_str());
				}

				Microsoft::WRL::ComPtr<IDxcCompiler3> compiler;
				Microsoft::WRL::ComPtr<IDxcResult> result;
				HRESULT hr = DxcSupport::CreateCompiler(&compiler);
				if (SUCCEEDED(hr)) {
					hr = compiler->Compile(&source, arguments.data(), (UINT32)arguments.size(), &includes, IID_PPV_ARGS(&result));
				}
				if (FAILED(hr) || result == nullptr) {
					char hr_text[64] = {};
					sprintf_s(hr_text, "compilation failed (hr=0x%08lX)", (unsigned long)hr);
					out.error = hr_text;
					return false;
				}
				Microsoft::WRL::ComPtr<IDxcBlobUtf8> messages;
				result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&messages), nullptr);
				out.error = BlobText(messages.Get());
				HRESULT status = E_FAIL;
				result->GetStatus(&status);
				Microsoft::WRL::ComPtr<IDxcBlob> object;
				result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
				if (FAILED(status) || object == nullptr || object->GetBufferSize() == 0) {
					if (out.error.empty()) {
						char hr_text[64] = {};
						sprintf_s(hr_text, "compilation failed (hr=0x%08lX)", (unsigned long)status);
						out.error = hr_text;
					}
					return false;
				}
				//Callers hold the bytecode as an ID3DBlob.
				ID3DBlob* code = nullptr;
				if (FAILED(D3DCreateBlob(object->GetBufferSize(), &code))) {
					out.error = "out of memory";
					return false;
				}
				memcpy(code->GetBufferPointer(), object->GetBufferPointer(), object->GetBufferSize());
				out.blob = code;
				return true;
			}

			//fxc, for the shaders UsesFxc names: the profile is 5.0 of the stage the caller asked for, whatever model it named.
			bool ShaderCompiler::CompileFxc(const std::string& source_path, const std::string& entry,
				const std::vector<std::string>& search_dirs, Result& out)
			{
				std::ifstream in(source_path, std::ios::binary);
				std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				FxcIncludeHandler includes(fs::path(source_path).parent_path().string(), search_dirs, out.dependencies, &out.dependency_times);
#ifdef _DEBUG
				const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_DEBUG;
#else
				const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3 | (DebugSymbolsWanted() ? D3DCOMPILE_DEBUG : 0u);
#endif
				ID3DBlob* code = nullptr;
				ID3DBlob* errors = nullptr;
				//Every shader that is built by fxc is a compute shader at the moment (UsesFxc), whose profile it is.
				const HRESULT hr = D3DCompile(text.data(), text.size(), source_path.c_str(), nullptr, &includes, entry.c_str(), "cs_5_0",
					flags, 0, &code, &errors);
				out.error = BlobText(errors);
				if (errors != nullptr) {
					errors->Release();
				}
				if (FAILED(hr) || code == nullptr) {
					if (code != nullptr) {
						code->Release();
					}
					if (out.error.empty()) {
						char hr_text[64] = {};
						sprintf_s(hr_text, "compilation failed (hr=0x%08lX)", (unsigned long)hr);
						out.error = hr_text;
					}
					return false;
				}
				out.blob = code;
				return true;
			}

			bool ShaderCompiler::CompileToFile(const std::string& source_path, const std::string& profile,
				const std::string& cso_path, Result& out,
				const std::vector<std::string>& extra_search_dirs, const std::string& entry)
			{
				std::vector<std::string> search_dirs = folders;
				search_dirs.insert(search_dirs.end(), extra_search_dirs.begin(), extra_search_dirs.end());
				if (!CompileWith(source_path, profile, search_dirs, out, entry)) {
					return false;
				}
				const std::wstring wide = fs::path(cso_path).wstring();
				//TRUE: overwrite. The caller has already refused to reuse the name of an
				//existing shader (see MaterialOps::CreateShaderFile), so a file here can
				//only be a stale .cso left by an earlier failed attempt at this same name.
				HRESULT hr = D3DWriteBlobToFile(out.blob, wide.c_str(), TRUE);
				if (FAILED(hr)) {
					out.error = "could not write " + cso_path;
					out.blob->Release();
					out.blob = nullptr;
					return false;
				}
				return true;
			}

			bool ShaderCompiler::Preprocess(const std::string& source_path, std::vector<std::string>& dependencies,
				std::string& error)
			{
				dependencies.clear();
				error.clear();

				std::error_code ec;
				if (!fs::is_regular_file(fs::path(source_path), ec)) {
					error = "shader source not found: " + source_path;
					return false;
				}
				//Follows every #include line, wherever it sits (a conditional section is followed too: the set is a superset of
				//what a compile opens, so an edit is never missed, at worst a reload is asked for that changed nothing).
				std::vector<std::string> pending = { Normalize(source_path) };
				std::unordered_map<std::string, bool> seen;
				while (!pending.empty()) {
					const std::string current = pending.back();
					pending.pop_back();
					if (seen.emplace(ToLower(current), true).second == false) {
						continue;
					}
					dependencies.push_back(current);
					std::ifstream in(current, std::ios::binary);
					if (!in) {
						if (dependencies.size() == 1) {
							error = "cannot read " + source_path;
							return false;
						}
						continue;
					}
					const fs::path folder = fs::path(current).parent_path();
					std::string line;
					while (std::getline(in, line)) {
						size_t i = line.find_first_not_of(" \t");
						if (i == std::string::npos || line[i] != '#') {
							continue;
						}
						i = line.find_first_not_of(" \t", i + 1);
						if (i == std::string::npos || line.compare(i, 7, "include") != 0) {
							continue;
						}
						const size_t open = line.find_first_of("\"<", i + 7);
						if (open == std::string::npos) {
							continue;
						}
						const size_t close = line.find_first_of(line[open] == '<' ? ">" : "\"", open + 1);
						if (close == std::string::npos) {
							continue;
						}
						const std::string name = line.substr(open + 1, close - open - 1);
						std::string resolved;
						if (fs::is_regular_file(folder / name, ec)) {
							resolved = (folder / name).string();
						}
						else {
							for (const std::string& dir : folders) {
								if (fs::is_regular_file(fs::path(dir) / name, ec)) {
									resolved = (fs::path(dir) / name).string();
									break;
								}
							}
						}
						if (!resolved.empty()) {
							pending.push_back(Normalize(resolved));
						}
					}
				}
				return true;
			}
		}
	}
}
