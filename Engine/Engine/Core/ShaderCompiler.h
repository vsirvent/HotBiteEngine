#pragma once

#include <d3dcommon.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace HotBite {
	namespace Engine {
		namespace Core {

			// Compiles .hlsl sources at runtime so a shader can be edited and reloaded
			// without restarting the host (see ShaderFactory::ReloadShader). The engine
			// normally loads pre-built .cso files produced by FxCompile; this is the
			// same fxc (d3dcompiler) invoked in process, against the .hlsl files still
			// sitting in the source tree.
			//
			// Two things it owns that the build does not:
			//
			// - **A source index.** Shaders are asked for by object-file name
			//   ("MainRenderPS.cso"), which says nothing about where the source is, so
			//   every registered folder is scanned recursively for <stem>.hlsl. A
			//   folder added later wins a name collision, which is what lets a test (or
			//   a user experimenting) shadow an engine shader with a copy of its own.
			//
			// - **The include graph**, recorded per compile by the include handler.
			//   FxCompile does not track .hlsli includes in this tree at all (hence the
			//   InvalidateShadersOnSharedHeaderChange target in Engine.vcxproj); here
			//   the list is exact, because it is what the compiler actually opened. It
			//   is what makes "reload the shaders that changed" answer correctly after
			//   an edit to a shared header.
			class ShaderCompiler {
			public:
				struct Result {
					// Compiled bytecode on success, null otherwise. The caller owns it.
					ID3DBlob* blob = nullptr;
					// fxc diagnostics: the reason on failure, warnings on success.
					std::string error;
					// The source file plus every file it #included, absolute.
					std::vector<std::string> dependencies;
					// The write time each of those had when the compiler read it, in
					// lockstep with `dependencies`. Captured at read time rather than
					// afterwards: a compile can take half a minute, and a file saved
					// again while it ran must still count as changed - taking the times
					// at the end would record the edit as already built.
					std::vector<uint64_t> dependency_times;
				};

				static ShaderCompiler* Get();
				static void Release();

				// Probes for the engine's shader tree relative to the running
				// executable (the repo layout <root>\Engine\Engine\Core\Shaders, with
				// <root>\Tests alongside it for game-side shaders), then applies the
				// HOTBITE_SHADER_SRC environment variable if set - a ';'-separated list
				// of folders, each taking priority over the probed ones. Safe to call
				// more than once; the first call is what a host pays for.
				void AddDefaultSourceFolders();

				// Registers a folder and indexes the .hlsl files under it recursively.
				// Returns false if the folder does not exist. `found` receives the
				// number of sources indexed from it.
				bool AddSourceFolder(const std::string& folder, int* found = nullptr);

				// Removes a folder and reindexes what is left, so a source that was
				// being shadowed by it becomes visible again.
				bool RemoveSourceFolder(const std::string& folder);

				const std::vector<std::string>& SourceFolders() const { return folders; }
				size_t SourceCount() const { return sources.size(); }

				// Maps a shader name as the engine asks for it ("MainRenderPS.cso", or
				// a bare stem) to the .hlsl that produces it. Empty if unknown.
				std::string FindSource(const std::string& shader_name) const;

				// Compiles `source_path` for `profile` ("ps_5_0", ...) with entry point
				// `entry`. Every shader in this engine enters at main. Failure fills
				// out.error with the compiler's own message, including file and line.
				bool Compile(const std::string& source_path, const std::string& profile,
					Result& out, const std::string& entry = "main");

				// The same, against an explicit include search path instead of the
				// registered folders. This is what a compile running off the caller's
				// thread uses: it is handed a snapshot of the folders, so a folder being
				// added or removed meanwhile cannot be read while it is being written.
				// Nothing else here is touched by a compile, which is what makes running
				// one on a worker thread safe.
				bool CompileWith(const std::string& source_path, const std::string& profile,
					const std::vector<std::string>& search_dirs, Result& out,
					const std::string& entry = "main");

				// Runs the preprocessor only, to learn a shader's include set without
				// paying for code generation. Used to answer "did anything this shader
				// reads change?" for a shader that has not been compiled here yet.
				bool Preprocess(const std::string& source_path, std::vector<std::string>& dependencies,
					std::string& error);

				// Last write time as a comparable integer, 0 if the file is gone.
				static uint64_t FileTime(const std::string& path);

			private:
				ShaderCompiler() = default;

				void Reindex();
				int IndexFolder(const std::string& folder);

				static ShaderCompiler* sInstance;

				// In priority order, lowest first: a later folder overrides an earlier
				// one for the same stem.
				std::vector<std::string> folders;
				// Lower-cased file stem -> absolute .hlsl path.
				std::unordered_map<std::string, std::string> sources;
				bool defaults_added = false;
			};
		}
	}
}
