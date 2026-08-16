#include "ShaderReload.h"

#include <Core/Log.h>
#include <Core/ShaderCompiler.h>
#include <Core/SimpleShader.h>

#include <Windows.h>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

using namespace HotBite::Engine;
namespace fs = std::filesystem;

namespace HotBiteEditor {
	namespace ShaderReload {

		namespace {

			// One shader to compile. Nothing here refers to engine objects: the worker
			// may still be running while the editor tears itself down (see Shutdown).
			struct Job {
				std::string name;
				std::string source;
				std::string profile;
				std::vector<std::string> search_dirs;
			};

			struct Finished {
				std::string name;
				Core::ShaderCompiler::Result result;
				bool ok = false;
			};

			// Shared with the worker through a shared_ptr, so a detached worker keeps
			// it alive after the editor has dropped its own reference.
			struct Channel {
				std::mutex mutex;
				std::condition_variable signal;
				std::deque<Job> queue;
				std::vector<Finished> finished;
				bool stop = false;
				bool compiling = false;
			};

			std::shared_ptr<Channel> channel;
			std::thread worker;

			bool auto_enabled = false;
			std::string last_report;
			std::vector<std::string> last_errors;
			std::string adopted_project;

			// Counters for the batch being applied, reset when the queue drains.
			int batch_reloaded = 0;
			int batch_failed = 0;
			int batch_total = 0;

			//Watcher poll interval. A poll stats one file per dependency, so this is
			//cheap - but there is no reason to do it at frame rate either, and a save
			//caught halfway through a write is better read a moment later.
			constexpr ULONGLONG POLL_PERIOD_MS = 700;
			ULONGLONG last_poll = 0;

			void StartWorker()
			{
				if (channel != nullptr) {
					return;
				}
				channel = std::make_shared<Channel>();
				std::shared_ptr<Channel> ch = channel;
				worker = std::thread([ch]() {
					while (true) {
						Job job;
						{
							std::unique_lock<std::mutex> lock(ch->mutex);
							ch->signal.wait(lock, [&] { return ch->stop || !ch->queue.empty(); });
							if (ch->stop) {
								return;
							}
							job = ch->queue.front();
							ch->queue.pop_front();
							ch->compiling = true;
						}

						Finished done;
						done.name = job.name;
						done.ok = Core::ShaderCompiler::Get()->CompileWith(
							job.source, job.profile, job.search_dirs, done.result);

						{
							std::lock_guard<std::mutex> lock(ch->mutex);
							ch->compiling = false;
							if (ch->stop) {
								//Nobody is going to apply this; drop the bytecode rather
								//than hand it to a factory that may be gone.
								if (done.result.blob != nullptr) {
									done.result.blob->Release();
								}
								return;
							}
							ch->finished.push_back(std::move(done));
						}
					}
					});
			}

			// Queues one shader. Everything the worker needs is resolved here, on the
			// thread that owns the shader objects.
			bool Enqueue(const std::string& name, std::string& error)
			{
				Core::ISimpleShader* shader = Core::ShaderFactory::Get()->Find(name);
				if (shader == nullptr) {
					error = "shader is not loaded: " + name;
					return false;
				}
				const std::string source = shader->GetSourcePath();
				if (source.empty()) {
					error = "no .hlsl source found for " + name;
					return false;
				}
				StartWorker();
				Job job;
				job.name = name;
				job.source = source;
				job.profile = shader->GetShaderProfile();
				job.search_dirs = Core::ShaderCompiler::Get()->SourceFolders();
				{
					std::lock_guard<std::mutex> lock(channel->mutex);
					//Asking twice for the same shader before the first compile has been
					//applied is a wasted 30 seconds, and after an auto-reload poll it is
					//the common case.
					for (const Job& queued : channel->queue) {
						if (queued.name == name) {
							return true;
						}
					}
					channel->queue.push_back(std::move(job));
				}
				channel->signal.notify_all();
				++batch_total;
				return true;
			}

			// Resolves the name a shader should be looked up by: the automation surface
			// takes "MainRenderPS.cso" or "MainRenderPS".
			std::string ResolveName(const std::string& name)
			{
				if (Core::ShaderFactory::Get()->Find(name) != nullptr) {
					return name;
				}
				const std::string with_ext = name + ".cso";
				return (Core::ShaderFactory::Get()->Find(with_ext) != nullptr) ? with_ext : name;
			}

			// Applies whatever the worker finished. Called once per render tick.
			void ApplyFinished(EditorState& state)
			{
				if (channel == nullptr) {
					return;
				}
				std::vector<Finished> ready;
				int pending = 0;
				{
					std::lock_guard<std::mutex> lock(channel->mutex);
					ready.swap(channel->finished);
					pending = (int)channel->queue.size() + (channel->compiling ? 1 : 0);
				}
				if (ready.empty()) {
					return;
				}

				for (Finished& done : ready) {
					Core::ISimpleShader* shader = Core::ShaderFactory::Get()->Find(done.name);
					if (shader == nullptr) {
						//Only reachable if the factory was released under us; nothing to
						//apply the bytecode to.
						if (done.result.blob != nullptr) {
							done.result.blob->Release();
						}
						continue;
					}
					std::string error;
					if (shader->ApplyCompiled(done.result, error)) {
						++batch_reloaded;
					}
					else {
						++batch_failed;
						last_errors.push_back(done.name + ": " + error);
						LOG_ERROR("ShaderReload: %s: %s", done.name.c_str(), error.c_str());
					}
				}

				if (pending > 0) {
					state.status_message = "Shaders: compiling, " + std::to_string(pending) + " left";
					return;
				}
				last_report = std::to_string(batch_reloaded) + " reloaded, " +
					std::to_string(batch_failed) + " failed";
				state.status_message = "Shaders: " + last_report +
					(last_errors.empty() ? "" : " - " + last_errors.front());
				LOG_INFO("ShaderReload: %s", last_report.c_str());
				batch_reloaded = 0;
				batch_failed = 0;
				batch_total = 0;
			}
		}

		void Init()
		{
			Core::ShaderCompiler::Get()->AddDefaultSourceFolders();
			LOG_INFO("ShaderReload: %s", SourcesReport().c_str());
		}

		void Shutdown()
		{
			if (channel == nullptr) {
				return;
			}
			{
				std::lock_guard<std::mutex> lock(channel->mutex);
				channel->stop = true;
				channel->queue.clear();
			}
			channel->signal.notify_all();
			if (worker.joinable()) {
				//A compile in flight cannot be interrupted and can take half a minute,
				//which is far too long to hold up closing the editor. The worker holds
				//its own reference to the channel and touches nothing else, so letting
				//it finish on its own is safe.
				worker.detach();
			}
			channel.reset();
			//Also the way Shaders/Cancel Pending drops a queue: the next request starts
			//a fresh worker, so the counters must not carry the abandoned batch.
			batch_reloaded = 0;
			batch_failed = 0;
			batch_total = 0;
		}

		bool AutoEnabled()
		{
			return auto_enabled;
		}

		void SetAuto(bool enabled)
		{
			auto_enabled = enabled;
			//Reset the clock so switching it on acts at the next poll rather than
			//immediately queueing whatever was edited before it was switched on - that
			//is what Reload All is for.
			last_poll = GetTickCount64();
		}

		int Pending()
		{
			if (channel == nullptr) {
				return 0;
			}
			std::lock_guard<std::mutex> lock(channel->mutex);
			return (int)channel->queue.size() + (channel->compiling ? 1 : 0) + (int)channel->finished.size();
		}

		const std::string& LastReport()
		{
			return last_report;
		}

		const std::vector<std::string>& LastErrors()
		{
			return last_errors;
		}

		std::string SourcesReport()
		{
			Core::ShaderCompiler* compiler = Core::ShaderCompiler::Get();
			std::string text = std::to_string(compiler->SourceCount()) + " shader sources";
			const auto& folders = compiler->SourceFolders();
			if (folders.empty()) {
				text += " (no source folders; set HOTBITE_SHADER_SRC)";
			}
			for (const std::string& folder : folders) {
				text += "\n  " + folder;
			}
			return text;
		}

		bool AddSourceFolder(const std::string& folder, std::string& error)
		{
			int found = 0;
			if (!Core::ShaderCompiler::Get()->AddSourceFolder(folder, &found)) {
				error = "not a folder: " + folder;
				return false;
			}
			//A shader whose source is now a different file must look it up again - the
			//resolved path is cached on the shader.
			Core::ShaderFactory::Get()->ForgetSourcePaths();
			last_report = std::to_string(found) + " sources from " + folder;
			return true;
		}

		bool RemoveSourceFolder(const std::string& folder, std::string& error)
		{
			if (!Core::ShaderCompiler::Get()->RemoveSourceFolder(folder)) {
				error = "not a registered source folder: " + folder;
				return false;
			}
			Core::ShaderFactory::Get()->ForgetSourcePaths();
			return true;
		}

		bool ReloadOne(EditorState& state, const std::string& name, std::string& error)
		{
			last_errors.clear();
			const std::string resolved = ResolveName(name);
			if (!Enqueue(resolved, error)) {
				state.status_message = "Shader reload failed: " + error;
				return false;
			}
			state.status_message = "Shaders: compiling " + resolved;
			return true;
		}

		int ReloadAll(EditorState& state, bool only_changed)
		{
			last_errors.clear();
			int queued = 0;
			for (const std::string& name : Core::ShaderFactory::Get()->GetShaderNames()) {
				Core::ISimpleShader* shader = Core::ShaderFactory::Get()->Find(name);
				if (shader == nullptr || shader->GetSourcePath().empty()) {
					continue;
				}
				if (only_changed && !shader->IsSourceOutdated()) {
					continue;
				}
				std::string error;
				if (Enqueue(name, error)) {
					++queued;
				}
			}
			if (queued == 0) {
				last_report = "0 reloaded, 0 failed";
				state.status_message = only_changed ? "Shaders: nothing changed" : "Shaders: nothing to reload";
			}
			else {
				state.status_message = "Shaders: compiling " + std::to_string(queued) + " shader(s)";
			}
			LOG_INFO("ShaderReload: queued %d shader(s)%s", queued, only_changed ? " (changed)" : "");
			return queued;
		}

		void Tick(EditorState& state)
		{
			//A project may carry shaders of its own; adopt its folder the first time it
			//is seen, whichever way it was opened (--project, --level, the File menu).
			if (!state.project_root.empty() && state.project_root != adopted_project) {
				adopted_project = state.project_root;
				std::error_code ec;
				if (fs::is_directory(fs::path(state.project_root), ec)) {
					int found = 0;
					Core::ShaderCompiler::Get()->AddSourceFolder(state.project_root, &found);
					Core::ShaderFactory::Get()->ForgetSourcePaths();
					if (found > 0) {
						LOG_INFO("ShaderReload: %d shader sources in the project folder", found);
					}
				}
			}

			ApplyFinished(state);

			if (!auto_enabled) {
				return;
			}
			ULONGLONG now = GetTickCount64();
			if (now - last_poll < POLL_PERIOD_MS) {
				return;
			}
			last_poll = now;
			//Nothing new while a batch is still compiling: the poll would queue the
			//same shaders again (their timestamps only move on apply).
			if (Pending() > 0) {
				return;
			}

			int queued = 0;
			for (const std::string& name : Core::ShaderFactory::Get()->GetShaderNames()) {
				Core::ISimpleShader* shader = Core::ShaderFactory::Get()->Find(name);
				if (shader == nullptr || shader->GetSourcePath().empty() || !shader->IsSourceOutdated()) {
					continue;
				}
				std::string error;
				if (Enqueue(name, error)) {
					++queued;
				}
			}
			if (queued > 0) {
				last_errors.clear();
				state.status_message = "Shaders (auto): compiling " + std::to_string(queued) + " shader(s)";
				LOG_INFO("ShaderReload (auto): queued %d shader(s)", queued);
			}
		}
	}
}
