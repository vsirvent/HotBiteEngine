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

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "CookedModel.h"

namespace HotBite {
	namespace Engine {
		namespace Loader {
			// The cooked-model cache: `AcquireModel` is the one way anything gets the
			// contents of an .fbx. It reads `<file>.cooked` when that is current, and
			// otherwise reads the .fbx (the only slow, single-threaded path left), writes
			// the cooked file and hands back what it read.
			//
			// Safe to call from any number of threads at once. Reading a cooked file is
			// pure parsing and runs in parallel; the FBX SDK is not thread-safe, so the
			// cooks are serialised behind one lock while the reads go on around them.
			// Two threads asking for the *same* file is the caller's mistake (World's
			// batch loader asks once per path), but is still harmless: both get a
			// complete model and the file is replaced atomically.

			// The cache is on unless the environment says otherwise: HOTBITE_MODEL_CACHE=0
			// reads every .fbx and writes nothing, which is how a cooked file's content is
			// compared with a fresh read, and how one is ruled out as the cause of a bug.
			bool ModelCacheEnabled();

			// Never null on success. Null when neither the cooked file nor the .fbx could
			// be read; `error` then says why. `from_cache` reports which side served it.
			std::shared_ptr<CookedModel> AcquireModel(const std::string& full_path, bool triangulate,
				bool use_animation_names, std::function<void(float, const std::string&)> on_progress = nullptr,
				bool* from_cache = nullptr, std::string* error = nullptr);

			// Makes sure the cooked file of `full_path` exists and is current, without
			// keeping the model. `force` rebuilds it even when it is current. False when
			// the .fbx cannot be read or the file cannot be written.
			bool CookModel(const std::string& full_path, bool triangulate, bool use_animation_names,
				bool force, std::string* error = nullptr);

			// Counters since the process started (or ResetModelCacheStats), for a log line,
			// a test and a profile. They are the only way to know *which* path a load took.
			struct ModelCacheStats {
				uint32_t cooked_reads = 0;   // served from a current cooked file
				uint32_t fbx_reads = 0;      // the .fbx had to be read
				uint32_t cooked_writes = 0;  // cooked files written
				uint32_t failures = 0;       // models neither path could supply
				double cooked_read_ms = 0.0; // summed over all threads
				double fbx_read_ms = 0.0;
				// What installing models into a world took (World::InstallCookedModel), by part.
				double install_meshes_ms = 0.0;
				double install_shapes_ms = 0.0;
				double install_nodes_ms = 0.0;
			};
			ModelCacheStats GetModelCacheStats();
			void ResetModelCacheStats();
			enum class InstallPart { Meshes, Shapes, Nodes };
			void RecordInstallTime(InstallPart part, double ms);

			// A skeleton from the joints a cook kept (Skeleton::CpuData()), numbered
			// exactly as the one they came from.
			std::shared_ptr<Core::Skeleton> MakeSkeleton(const std::vector<Core::JointCpuData>& joints);
		}
	}
}
