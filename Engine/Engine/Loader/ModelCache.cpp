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
#include <Core/PhysicsCommon.h>
#include <Core/Json.h>
#include "ModelCache.h"
#include "FBXLoader.h"
#include <Core/Log.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <mutex>

using namespace HotBite::Engine;
using namespace HotBite::Engine::Loader;
using namespace HotBite::Engine::Core;

namespace {
	std::atomic<uint32_t> cooked_reads{ 0 };
	std::atomic<uint32_t> fbx_reads{ 0 };
	std::atomic<uint32_t> cooked_writes{ 0 };
	std::atomic<uint32_t> failures{ 0 };
	// Milliseconds are summed as microseconds so they can stay integers.
	std::atomic<uint64_t> cooked_read_us{ 0 };
	std::atomic<uint64_t> fbx_read_us{ 0 };
	std::atomic<uint64_t> install_us[3] = { 0, 0, 0 };

	// One FBX SDK user at a time. Each FBXLoader owns its own manager and scene, but
	// the SDK keeps process-wide state behind them, and the point of the cooked
	// files is that this lock stops mattering once they exist.
	std::mutex fbx_mutex;

	using Clock = std::chrono::steady_clock;
	uint64_t MicrosSince(Clock::time_point start) {
		return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
	}

	void SetError(std::string* error, const std::string& text) {
		if (error != nullptr) {
			*error = text;
		}
	}
}

bool Loader::ModelCacheEnabled() {
	char value[8] = {};
	const DWORD length = GetEnvironmentVariableA("HOTBITE_MODEL_CACHE", value, (DWORD)sizeof(value));
	return !(length > 0 && length < sizeof(value) && value[0] == '0');
}

ModelCacheStats Loader::GetModelCacheStats() {
	ModelCacheStats s;
	s.cooked_reads = cooked_reads;
	s.fbx_reads = fbx_reads;
	s.cooked_writes = cooked_writes;
	s.failures = failures;
	s.cooked_read_ms = (double)cooked_read_us / 1000.0;
	s.fbx_read_ms = (double)fbx_read_us / 1000.0;
	s.install_meshes_ms = (double)install_us[0] / 1000.0;
	s.install_shapes_ms = (double)install_us[1] / 1000.0;
	s.install_nodes_ms = (double)install_us[2] / 1000.0;
	return s;
}

void Loader::ResetModelCacheStats() {
	cooked_reads = 0;
	fbx_reads = 0;
	cooked_writes = 0;
	failures = 0;
	cooked_read_us = 0;
	fbx_read_us = 0;
	for (auto& us : install_us) { us = 0; }
}

void Loader::RecordInstallTime(InstallPart part, double ms) {
	install_us[(int)part] += (uint64_t)(ms * 1000.0);
}

std::shared_ptr<Skeleton> Loader::MakeSkeleton(const std::vector<JointCpuData>& joints) {
	std::shared_ptr<Skeleton> skeleton = std::make_shared<Skeleton>();
	//Added in index order, so each parent's children keep the order they had and
	//Flush numbers the joints exactly as the skeleton they were read from was
	//numbered: a mesh's vertices name joints by that number.
	for (const JointCpuData& joint : joints) {
		Joint added;
		added.cpu_data = joint;
		const int parent = (joint.parent_id < 0 || joint.parent_id >= (int)joints.size())
			? -1 : joints[joint.parent_id].joint_id;
		skeleton->AddJoint(added, parent);
	}
	skeleton->Flush();
	return skeleton;
}

namespace {
	// A current cooked file, or null. `why` says what was wrong with one that exists.
	std::shared_ptr<CookedModel> ReadCooked(const std::string& full_path, bool triangulate,
		bool use_animation_names, std::string& why) {
		const std::string cooked_file = CookedModel::PathFor(full_path);
		std::error_code ec;
		if (!std::filesystem::exists(cooked_file, ec)) {
			return nullptr;
		}
		std::shared_ptr<CookedModel> model = std::make_shared<CookedModel>();
		if (!CookedModel::Read(cooked_file, *model, &why)) {
			return nullptr;
		}
		if (!model->IsCurrentFor(full_path, triangulate, use_animation_names)) {
			why = "out of date";
			return nullptr;
		}
		return model;
	}
}

std::shared_ptr<CookedModel> Loader::AcquireModel(const std::string& full_path, bool triangulate,
	bool use_animation_names, std::function<void(float, const std::string&)> on_progress,
	bool* from_cache, std::string* error) {
	if (from_cache != nullptr) {
		*from_cache = false;
	}
	const bool cache = ModelCacheEnabled();
	if (cache) {
		const Clock::time_point start = Clock::now();
		std::string why;
		std::shared_ptr<CookedModel> model = ReadCooked(full_path, triangulate, use_animation_names, why);
		if (model != nullptr) {
			cooked_read_us += MicrosSince(start);
			++cooked_reads;
			if (from_cache != nullptr) {
				*from_cache = true;
			}
			if (on_progress != nullptr) { on_progress(0.9f, "Reading cooked model..."); }
			return model;
		}
		if (!why.empty()) {
			LOG_INFO("Model cache: '%s' is %s, cooking it again.", full_path.c_str(), why.c_str());
		}
	}

	std::error_code ec;
	if (!std::filesystem::exists(full_path, ec)) {
		++failures;
		SetError(error, "no such model: " + full_path);
		return nullptr;
	}
	std::shared_ptr<CookedModel> model = std::make_shared<CookedModel>();
	{
		std::lock_guard<std::mutex> lock(fbx_mutex);
		const Clock::time_point start = Clock::now();
		FBXLoader loader;
		if (!loader.Extract(full_path, triangulate, use_animation_names, *model, on_progress)) {
			++failures;
			SetError(error, "cannot read " + full_path);
			return nullptr;
		}
		fbx_read_us += MicrosSince(start);
		++fbx_reads;
	}
	if (cache) {
		std::string write_error;
		if (model->Write(CookedModel::PathFor(full_path), &write_error)) {
			++cooked_writes;
		}
		else {
			// Not fatal: the model is loaded all the same, only the next start is slow.
			LOG_WARN("Model cache: %s", write_error.c_str());
		}
	}
	return model;
}

bool Loader::CookModel(const std::string& full_path, bool triangulate, bool use_animation_names,
	bool force, std::string* error) {
	//A splat cloud is a model file too (World::LoadModel) and has nothing to cook.
	std::string extension = std::filesystem::path(full_path).extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	if (extension == ".ply") {
		return true;
	}
	if (!force) {
		std::string why;
		if (ReadCooked(full_path, triangulate, use_animation_names, why) != nullptr) {
			return true;
		}
	}
	std::error_code ec;
	if (!std::filesystem::exists(full_path, ec)) {
		SetError(error, "no such model: " + full_path);
		return false;
	}
	CookedModel model;
	{
		std::lock_guard<std::mutex> lock(fbx_mutex);
		FBXLoader loader;
		if (!loader.Extract(full_path, triangulate, use_animation_names, model)) {
			SetError(error, "cannot read " + full_path);
			return false;
		}
	}
	if (!model.Write(CookedModel::PathFor(full_path), error)) {
		return false;
	}
	++cooked_writes;
	return true;
}
