#include "ShaderCompiler.h"
#include "Log.h"

#include <Windows.h>
#include <d3dcompiler.h>

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

				// Resolves #include directives the way fxc does: relative to the file
				// that contains them first, then against the registered source folders.
				// Every file it opens is recorded, which is how a reload knows that an
				// edit to a shared .hlsli reaches this shader.
				class IncludeHandler : public ID3DInclude {
				public:
					IncludeHandler(const std::string& source_dir, const std::vector<std::string>& search_dirs,
						std::vector<std::string>& deps, std::vector<uint64_t>* times = nullptr)
						: root(source_dir), search(search_dirs), dependencies(deps), dependency_times(times) {
					}

					HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR file_name, LPCVOID parent_data,
						LPCVOID* out_data, UINT* out_bytes) override
					{
						//The compiler identifies the includer only by the data pointer it
						//was handed, so the directory of every open file is kept under
						//that pointer. A null parent is the top-level source.
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

				std::string BlobText(ID3DBlob* blob)
				{
					if (blob == nullptr || blob->GetBufferSize() == 0) {
						return {};
					}
					return std::string((const char*)blob->GetBufferPointer(), blob->GetBufferSize());
				}

				//Compilation flags. Release deliberately passes none beyond the strictness
				//flag: FxCompile in this tree sets no optimization properties either, so
				//a shader reloaded here is generated the same way the .cso beside it was,
				//and a measurement taken after a reload stays comparable to one taken
				//before. Debug matches the Debug configuration's /Od /Zi.
#ifdef _DEBUG
				constexpr UINT COMPILE_FLAGS = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
				constexpr UINT COMPILE_FLAGS = D3DCOMPILE_ENABLE_STRICTNESS;
#endif
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

				IncludeHandler includes(fs::path(source_path).parent_path().string(), search_dirs,
					out.dependencies, &out.dependency_times);
				ID3DBlob* code = nullptr;
				ID3DBlob* errors = nullptr;
				const std::wstring wide = fs::path(source_path).wstring();
				HRESULT hr = D3DCompileFromFile(wide.c_str(), nullptr, &includes, entry.c_str(),
					profile.c_str(), COMPILE_FLAGS, 0, &code, &errors);
				out.error = BlobText(errors);
				if (errors != nullptr) {
					errors->Release();
				}
				if (FAILED(hr) || code == nullptr) {
					if (out.error.empty()) {
						char hr_text[64] = {};
						sprintf_s(hr_text, "compilation failed (hr=0x%08lX)", (unsigned long)hr);
						out.error = hr_text;
					}
					if (code != nullptr) {
						code->Release();
					}
					return false;
				}
				out.blob = code;
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
				std::ifstream in(source_path, std::ios::binary);
				if (!in) {
					error = "cannot read " + source_path;
					return false;
				}
				std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				dependencies.push_back(Normalize(source_path));

				IncludeHandler includes(fs::path(source_path).parent_path().string(), folders, dependencies);
				ID3DBlob* processed = nullptr;
				ID3DBlob* errors = nullptr;
				HRESULT hr = D3DPreprocess(text.data(), text.size(), source_path.c_str(), nullptr,
					&includes, &processed, &errors);
				error = BlobText(errors);
				if (errors != nullptr) {
					errors->Release();
				}
				if (processed != nullptr) {
					processed->Release();
				}
				//A preprocessor failure still leaves the includes it did open in the
				//list, which is all this is for - the caller wants the dependency set,
				//not the text.
				return SUCCEEDED(hr);
			}
		}
	}
}
