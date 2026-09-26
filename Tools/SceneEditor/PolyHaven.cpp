#include "PolyHaven.h"
#include "MaterialPanel.h"
#include "Selection.h"

#include "imgui.h"
#include <World.h>
#include <Core/Json.h>
#include <Core/Material.h>

#include <Windows.h>
#include <winhttp.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")

using namespace HotBite::Engine;
namespace fs = std::filesystem;

namespace HotBiteEditor {
	namespace PolyHaven {
		namespace {

			constexpr const char* DEFAULT_SOURCE = "https://api.polyhaven.com";
			//Poly Haven asks API users to identify themselves with a unique agent.
			constexpr const wchar_t* USER_AGENT = L"HotBiteEngine-SceneEditor/1.0";
			//The API's resolution keys, smallest first.
			const char* const RESOLUTIONS[] = { "1k", "2k", "4k", "8k" };
			constexpr int WORKER_COUNT = 4;

			//--- worker pool. Catalog, thumbnail and import jobs all run here; the catalog
			//and imports jump the queue, so a screenful of thumbnails never delays them.
			std::mutex queue_mutex;
			std::condition_variable queue_cv;
			std::deque<std::function<void()>> jobs;
			std::vector<std::thread> workers;
			std::atomic<bool> stopping{ false };

			void WorkerLoop() {
				for (;;) {
					std::function<void()> job;
					{
						std::unique_lock<std::mutex> lock(queue_mutex);
						queue_cv.wait(lock, [] { return stopping.load() || !jobs.empty(); });
						if (stopping) {
							return;
						}
						job = std::move(jobs.front());
						jobs.pop_front();
					}
					job();
				}
			}

			void Post(std::function<void()> job, bool urgent) {
				std::lock_guard<std::mutex> lock(queue_mutex);
				if (stopping) {
					return;
				}
				if (workers.empty()) {
					for (int i = 0; i < WORKER_COUNT; ++i) {
						workers.emplace_back(WorkerLoop);
					}
				}
				if (urgent) {
					jobs.push_front(std::move(job));
				}
				else {
					jobs.push_back(std::move(job));
				}
				queue_cv.notify_one();
			}

			//--- shared state, written by the workers and read by the main thread. Every
			//field is under data_mutex. `generation` is bumped by SetSource, so a job that
			//finishes for a source that has since been replaced drops its result.
			struct ThumbEntry {
				ThumbState state = ThumbState::None;
				std::string file;
			};

			struct PendingImport {
				std::string id;
				std::string resolution;
				std::string mat_file;
				std::string textures_root; //Assets/Textures when the import started
				std::map<std::string, std::string> slots; //slot -> absolute path
				std::string error;
			};

			std::mutex data_mutex;
			std::string source = DEFAULT_SOURCE;
			int generation = 0;
			CatalogState catalog_state = CatalogState::Idle;
			std::string catalog_error;
			std::vector<Asset> assets;
			std::vector<std::pair<std::string, int>> categories;
			std::map<std::string, ThumbEntry> thumbs;
			bool import_busy = false;
			bool import_finished = false; //downloaded, waiting for Tick to make the material
			std::string import_progress;
			PendingImport pending;
			bool has_last_result = false;
			bool last_result_ok = false;
			std::string last_result_message;

			//Main thread only: previews as shader resources. A null entry means the file
			//would not load, so it is not retried every frame.
			std::map<std::string, ID3D11ShaderResourceView*> thumb_srvs;

			std::string Lower(std::string s) {
				std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
				return s;
			}

			bool IsRemote(const std::string& s) {
				const std::string l = Lower(s);
				return l.rfind("http://", 0) == 0 || l.rfind("https://", 0) == 0;
			}

			//An asset id comes off the network and ends up in a folder name, so it is held
			//to the characters Poly Haven actually uses.
			bool ValidId(const std::string& id) {
				if (id.empty() || id.size() > 128) {
					return false;
				}
				return std::all_of(id.begin(), id.end(), [](unsigned char c) {
					return std::isalnum(c) || c == '_' || c == '-';
				});
			}

			//An API endpoint under the current source. A local source has no query
			//strings: "assets?t=textures" is the file "assets".
			std::string Endpoint(const std::string& src, const std::string& path) {
				if (IsRemote(src)) {
					return src + "/" + path;
				}
				return (fs::path(src) / path.substr(0, path.find('?'))).make_preferred().string();
			}

			//A URL named *inside* a response: a web URL, a file:/// URL or a path - a
			//relative one taken against the (local) source.
			std::string ResolveLocal(const std::string& src, std::string url) {
				if (Lower(url).rfind("file:///", 0) == 0) {
					url = url.substr(8);
				}
				fs::path p(url);
				if (p.is_relative() && !IsRemote(src)) {
					p = fs::path(src) / p;
				}
				return p.lexically_normal().make_preferred().string();
			}

			bool ReadFile(const std::string& path, std::string& body, std::string& error) {
				std::ifstream in(path, std::ios::binary);
				if (!in) {
					error = "cannot read " + path;
					return false;
				}
				std::ostringstream ss;
				ss << in.rdbuf();
				body = ss.str();
				return true;
			}

			//GET over WinHTTP, the whole body in memory (a 4k map is a few tens of MB at
			//most). Redirects are followed by default; shutdown is checked between reads.
			bool HttpGet(const std::string& url, std::string& body, std::string& error) {
				body.clear();
				const std::wstring wurl(url.begin(), url.end());
				URL_COMPONENTS uc = {};
				uc.dwStructSize = sizeof(uc);
				uc.dwHostNameLength = (DWORD)-1;
				uc.dwUrlPathLength = (DWORD)-1;
				uc.dwExtraInfoLength = (DWORD)-1;
				if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
					error = "bad URL: " + url;
					return false;
				}
				const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
				std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
				if (uc.lpszExtraInfo != nullptr) {
					path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
				}

				HINTERNET session = WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
					WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
				if (session == nullptr) {
					error = "WinHttpOpen failed (" + std::to_string(GetLastError()) + ")";
					return false;
				}
				WinHttpSetTimeouts(session, 10000, 10000, 20000, 30000);
				DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
				WinHttpSetOption(session, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));

				bool ok = false;
				HINTERNET connect = WinHttpConnect(session, host.c_str(), uc.nPort, 0);
				HINTERNET request = (connect == nullptr) ? nullptr
					: WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
						WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
				if (request == nullptr) {
					error = "cannot connect to " + url + " (" + std::to_string(GetLastError()) + ")";
				}
				else if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
					!WinHttpReceiveResponse(request, nullptr)) {
					error = "request to " + url + " failed (" + std::to_string(GetLastError()) +
						") - is the machine online?";
				}
				else {
					DWORD status = 0, size = sizeof(status);
					WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
						WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
					if (status != 200) {
						error = "HTTP " + std::to_string(status) + " from " + url;
					}
					else {
						ok = true;
						for (;;) {
							if (stopping) {
								error = "cancelled";
								ok = false;
								break;
							}
							DWORD available = 0;
							if (!WinHttpQueryDataAvailable(request, &available)) {
								error = "download of " + url + " broke off (" + std::to_string(GetLastError()) + ")";
								ok = false;
								break;
							}
							if (available == 0) {
								break;
							}
							const size_t at = body.size();
							body.resize(at + available);
							DWORD read = 0;
							if (!WinHttpReadData(request, &body[at], available, &read)) {
								error = "download of " + url + " broke off (" + std::to_string(GetLastError()) + ")";
								ok = false;
								break;
							}
							body.resize(at + read);
						}
					}
				}
				if (request != nullptr) WinHttpCloseHandle(request);
				if (connect != nullptr) WinHttpCloseHandle(connect);
				WinHttpCloseHandle(session);
				return ok;
			}

			bool Fetch(const std::string& src, const std::string& url, std::string& body, std::string& error) {
				return IsRemote(url) ? HttpGet(url, body, error) : ReadFile(ResolveLocal(src, url), body, error);
			}

			//Written beside the target and renamed into place, so a half-written file is
			//never seen - by the texture list, or by a later run's "already downloaded".
			bool WriteFileAtomic(const fs::path& dest, const std::string& body, std::string& error) {
				std::error_code ec;
				fs::create_directories(dest.parent_path(), ec);
				const fs::path part = dest.string() + ".part";
				{
					std::ofstream out(part, std::ios::binary | std::ios::trunc);
					if (!out) {
						error = "cannot write " + part.string();
						return false;
					}
					out.write(body.data(), (std::streamsize)body.size());
					if (!out) {
						error = "cannot write " + part.string();
						return false;
					}
				}
				fs::rename(part, dest, ec);
				if (ec) {
					fs::remove(part, ec);
					error = "cannot write " + dest.string();
					return false;
				}
				return true;
			}

			fs::path ThumbCacheDir() {
				std::error_code ec;
				return fs::temp_directory_path(ec) / "HotBitePolyHaven" / "thumbs";
			}

			//--- catalog
			void LoadCatalog(int gen, std::string src) {
				std::string error, body;
				std::vector<std::pair<std::string, int>> cats;
				std::vector<Asset> list;
				bool ok = Fetch(src, Endpoint(src, "categories/textures"), body, error);
				try {
					if (ok) {
						const nlohmann::json j = nlohmann::json::parse(body);
						for (auto it = j.begin(); it != j.end(); ++it) {
							cats.emplace_back(it.key(), it.value().get<int>());
						}
						//The API sends them by count; a JSON object here comes back sorted
						//by name, so restore that ("all" first, being every asset).
						std::stable_sort(cats.begin(), cats.end(),
							[](const auto& a, const auto& b) { return a.second > b.second; });
						ok = Fetch(src, Endpoint(src, "assets?t=textures"), body, error);
					}
					if (ok) {
						const nlohmann::json j = nlohmann::json::parse(body);
						for (auto it = j.begin(); it != j.end(); ++it) {
							if (!ValidId(it.key()) || !it.value().is_object()) {
								continue;
							}
							const nlohmann::json& v = it.value();
							Asset a;
							a.id = it.key();
							a.name = v.value("name", a.id);
							a.thumbnail_url = v.value("thumbnail_url", std::string());
							a.download_count = v.value("download_count", 0);
							if (v.contains("categories") && v["categories"].is_array()) {
								for (const auto& c : v["categories"]) {
									if (c.is_string()) a.categories.push_back(c.get<std::string>());
								}
							}
							if (v.contains("tags") && v["tags"].is_array()) {
								for (const auto& t : v["tags"]) {
									if (t.is_string()) a.tags.push_back(t.get<std::string>());
								}
							}
							list.push_back(std::move(a));
						}
						std::sort(list.begin(), list.end(), [](const Asset& a, const Asset& b) {
							return a.download_count != b.download_count ? a.download_count > b.download_count : a.id < b.id;
						});
					}
				}
				catch (const std::exception& e) {
					ok = false;
					error = std::string("unexpected catalog format: ") + e.what();
				}
				std::lock_guard<std::mutex> lock(data_mutex);
				if (gen != generation) {
					return;
				}
				if (ok) {
					assets = std::move(list);
					categories = std::move(cats);
					catalog_state = CatalogState::Ready;
					catalog_error.clear();
				}
				else {
					catalog_state = CatalogState::Failed;
					catalog_error = error;
				}
			}

			//--- thumbnails
			void LoadThumb(int gen, std::string src, std::string id, std::string url) {
				std::string file, error;
				bool ok = !url.empty();
				if (ok && IsRemote(url)) {
					//Keyed by the URL too: it carries the image version, so an updated
					//preview is fetched again rather than served stale from the cache.
					std::ostringstream name;
					name << id << "_" << std::hex << std::hash<std::string>{}(url) << ".png";
					const fs::path dest = ThumbCacheDir() / name.str();
					std::error_code ec;
					if (!(fs::is_regular_file(dest, ec) && fs::file_size(dest, ec) > 0)) {
						std::string body;
						ok = HttpGet(url, body, error) && WriteFileAtomic(dest, body, error);
					}
					file = dest.string();
				}
				else if (ok) {
					file = ResolveLocal(src, url);
					std::error_code ec;
					ok = fs::is_regular_file(file, ec);
				}
				std::lock_guard<std::mutex> lock(data_mutex);
				if (gen != generation) {
					return;
				}
				ThumbEntry& entry = thumbs[id];
				entry.state = ok ? ThumbState::Ready : ThumbState::Failed;
				entry.file = file;
			}

			//--- import
			//The files a map is available in, by resolution: the one asked for, else the
			//largest below it, else the smallest there is. JPG before PNG - Poly Haven's
			//PNGs are 16-bit and ~8x the size for no visible gain here.
			bool PickFile(const nlohmann::json& map, const std::string& resolution,
				std::string& url, size_t& size) {
				int wanted = 0;
				for (int i = 0; i < (int)std::size(RESOLUTIONS); ++i) {
					if (resolution == RESOLUTIONS[i]) {
						wanted = i;
					}
				}
				std::vector<int> order;
				for (int i = wanted; i >= 0; --i) order.push_back(i);
				for (int i = wanted + 1; i < (int)std::size(RESOLUTIONS); ++i) order.push_back(i);
				for (int i : order) {
					if (!map.contains(RESOLUTIONS[i])) {
						continue;
					}
					const nlohmann::json& formats = map[RESOLUTIONS[i]];
					for (const char* format : { "jpg", "png" }) {
						if (formats.contains(format) && formats[format].contains("url")) {
							url = formats[format]["url"].get<std::string>();
							size = formats[format].value("size", (size_t)0);
							return true;
						}
					}
				}
				return false;
			}

			void SetProgress(const std::string& text) {
				std::lock_guard<std::mutex> lock(data_mutex);
				import_progress = text;
			}

			void DownloadMaps(std::string src, PendingImport job, bool height) {
				std::string body, error;
				bool ok = Fetch(src, Endpoint(src, "files/" + job.id), body, error);
				//slot -> (url, expected size)
				std::vector<std::pair<std::string, std::pair<std::string, size_t>>> wanted;
				if (ok) {
					try {
						const nlohmann::json j = nlohmann::json::parse(body);
						auto want = [&](const char* slot, const char* key) {
							std::string url;
							size_t size = 0;
							if (j.contains(key) && j[key].is_object() && PickFile(j[key], job.resolution, url, size)) {
								wanted.push_back({ slot, { url, size } });
								return true;
							}
							return false;
						};
						if (!want("diffuse", "Diffuse")) {
							ok = false;
							error = job.id + " has no diffuse map - is it a texture?";
						}
						else {
							want("normal", "nor_dx");
							if (!want("arm", "arm")) {
								want("ao", "AO");
							}
							if (height) {
								want("height", "Displacement");
							}
						}
					}
					catch (const std::exception& e) {
						ok = false;
						error = std::string("unexpected file list for ") + job.id + ": " + e.what();
					}
				}
				const fs::path dir = fs::path(job.textures_root) / TextureFolder(job.id);
				for (size_t i = 0; ok && i < wanted.size(); ++i) {
					SetProgress(job.id + ": " + std::to_string(i) + "/" + std::to_string(wanted.size()) + " maps");
					const std::string& url = wanted[i].second.first;
					const size_t expected = wanted[i].second.second;
					//The file name is Poly Haven's own (rock_wall_08_diff_1k.jpg), which
					//already says which map and which resolution it is.
					std::string name = fs::path(url.substr(0, url.find('?'))).filename().string();
					const bool safe = !name.empty() && name != "." && name != ".." &&
						std::all_of(name.begin(), name.end(), [](unsigned char c) {
							return std::isalnum(c) || c == '_' || c == '-' || c == '.';
						});
					if (!safe) {
						ok = false;
						error = "unexpected file name in " + url;
						break;
					}
					const fs::path dest = dir / name;
					std::error_code ec;
					//Re-importing (another material file, say) does not download again.
					const bool have = fs::is_regular_file(dest, ec) &&
						(expected == 0 || fs::file_size(dest, ec) == expected);
					if (!have) {
						ok = Fetch(src, url, body, error) && WriteFileAtomic(dest, body, error);
					}
					if (ok) {
						job.slots[wanted[i].first] = dest.lexically_normal().make_preferred().string();
					}
				}
				job.error = ok ? std::string() : error;
				std::lock_guard<std::mutex> lock(data_mutex);
				pending = std::move(job);
				import_finished = true;
				import_progress.clear();
			}

			void FinishImport(EditorState& state, const PendingImport& job) {
				auto fail = [&](const std::string& why) {
					std::lock_guard<std::mutex> lock(data_mutex);
					has_last_result = true;
					last_result_ok = false;
					last_result_message = why;
					state.status_message = "Poly Haven import failed: " + why;
				};
				if (!job.error.empty()) {
					fail(job.error);
					return;
				}
				if (state.world == nullptr || MaterialOps::TexturesDir(state) != job.textures_root) {
					fail("the level changed while " + job.id + " was downloading");
					return;
				}
				if (state.world->GetMaterialFiles().count(job.mat_file) == 0) {
					fail("the level no longer declares " + job.mat_file);
					return;
				}
				MaterialOps::RefreshTextureList();

				std::string error;
				MaterialOps::MaterialSnapshot before;
				bool existed = MaterialOps::GetSnapshot(state, job.id, before);
				if (!existed) {
					if (!MaterialOps::CreateMaterial(state, job.id, job.mat_file, error)) {
						fail(error);
						return;
					}
					MaterialOps::GetSnapshot(state, job.id, before);
				}
				//The fill is its own history step, like Duplicate: undo once takes the maps
				//back off, twice removes the material.
				MaterialOps::MaterialSnapshot after = before;
				Core::MaterialTextures& t = after.texture_names;
				auto set = [&](const char* slot, std::string& field) {
					auto it = job.slots.find(slot);
					if (it != job.slots.end()) {
						field = it->second;
					}
				};
				set("diffuse", t.diffuse_texname);
				set("normal", t.normal_textname);
				set("arm", t.arm_textname);
				set("ao", t.ao_textname);
				set("height", t.high_textname);
				if (!MaterialOps::ApplySnapshot(state, job.id, after, error)) {
					fail(error);
					return;
				}
				MaterialOps::RecordEdit(state, job.id, before);
				state.selected_material = job.id;

				const std::string what = job.id + " (" + job.resolution + ", " +
					std::to_string(job.slots.size()) + " maps) " + (existed ? "updated in " : "added to ") + job.mat_file;
				std::lock_guard<std::mutex> lock(data_mutex);
				has_last_result = true;
				last_result_ok = true;
				last_result_message = what;
				state.status_message = "Poly Haven: " + what;
			}

			bool MaterialExists(EditorState& state, const std::string& name) {
				return state.world != nullptr && state.world->GetMaterials().Get(name) != nullptr &&
					!state.world->IsMaterialRemoved(name);
			}

			std::string DefaultMatFile(EditorState& state) {
				if (state.world == nullptr) {
					return {};
				}
				if (!state.selected_material.empty()) {
					const std::string origin = state.world->GetMaterialOrigin(state.selected_material);
					if (!origin.empty()) {
						return origin;
					}
				}
				const auto& files = state.world->GetMaterialFiles();
				return files.empty() ? std::string() : files.begin()->first;
			}
		}

		const std::string& Source() {
			//Only ever replaced from the main thread, which is the only reader here.
			return source;
		}

		void SetSource(const std::string& new_source) {
			std::lock_guard<std::mutex> lock(data_mutex);
			source = (new_source.empty() || new_source == "default") ? std::string(DEFAULT_SOURCE) : new_source;
			while (!source.empty() && (source.back() == '/' || source.back() == '\\')) {
				source.pop_back();
			}
			++generation;
			catalog_state = CatalogState::Idle;
			catalog_error.clear();
			assets.clear();
			categories.clear();
			thumbs.clear();
			//Previews are keyed by id, and another source may use the same ids.
			for (auto& entry : thumb_srvs) {
				if (entry.second != nullptr) {
					Core::ReleaseTexture(entry.second);
				}
			}
			thumb_srvs.clear();
		}

		void RequestCatalog(bool force) {
			int gen;
			std::string src;
			{
				std::lock_guard<std::mutex> lock(data_mutex);
				if (catalog_state == CatalogState::Loading ||
					(!force && catalog_state != CatalogState::Idle)) {
					return;
				}
				catalog_state = CatalogState::Loading;
				catalog_error.clear();
				gen = generation;
				src = source;
			}
			Post([gen, src] { LoadCatalog(gen, src); }, true);
		}

		CatalogState GetCatalogState(std::string* error) {
			std::lock_guard<std::mutex> lock(data_mutex);
			if (error != nullptr) {
				*error = catalog_error;
			}
			return catalog_state;
		}

		std::vector<std::pair<std::string, int>> Categories() {
			std::lock_guard<std::mutex> lock(data_mutex);
			return categories;
		}

		std::vector<Asset> Assets(const std::string& category, const std::string& filter) {
			const std::string f = Lower(filter);
			const bool any_category = category.empty() || category == "all";
			std::vector<Asset> out;
			std::lock_guard<std::mutex> lock(data_mutex);
			for (const Asset& a : assets) {
				if (!any_category && std::find(a.categories.begin(), a.categories.end(), category) == a.categories.end()) {
					continue;
				}
				if (!f.empty()) {
					bool match = Lower(a.id).find(f) != std::string::npos || Lower(a.name).find(f) != std::string::npos;
					for (size_t i = 0; !match && i < a.tags.size(); ++i) {
						match = Lower(a.tags[i]).find(f) != std::string::npos;
					}
					if (!match) {
						continue;
					}
				}
				out.push_back(a);
			}
			return out;
		}

		bool FindAsset(const std::string& id, Asset& out) {
			std::lock_guard<std::mutex> lock(data_mutex);
			for (const Asset& a : assets) {
				if (a.id == id) {
					out = a;
					return true;
				}
			}
			return false;
		}

		ID3D11ShaderResourceView* Thumbnail(const std::string& id, ThumbState* state_out) {
			auto srv_it = thumb_srvs.find(id);
			if (srv_it != thumb_srvs.end()) {
				if (state_out != nullptr) {
					*state_out = srv_it->second != nullptr ? ThumbState::Ready : ThumbState::Failed;
				}
				return srv_it->second;
			}
			std::string file, url, src;
			int gen = 0;
			bool start = false;
			ThumbState st;
			{
				std::lock_guard<std::mutex> lock(data_mutex);
				ThumbEntry& entry = thumbs[id];
				if (entry.state == ThumbState::None) {
					for (const Asset& a : assets) {
						if (a.id == id) {
							url = a.thumbnail_url;
							break;
						}
					}
					//An asset without a preview (or not in the catalog) fails here rather
					//than sitting in "loading" forever.
					entry.state = url.empty() ? ThumbState::Failed : ThumbState::Loading;
					start = !url.empty();
					gen = generation;
					src = source;
				}
				st = entry.state;
				file = entry.file;
			}
			if (state_out != nullptr) {
				*state_out = st;
			}
			if (start) {
				Post([gen, src, id, url] { LoadThumb(gen, src, id, url); }, false);
			}
			if (st != ThumbState::Ready) {
				return nullptr;
			}
			ID3D11ShaderResourceView* srv = Core::LoadTexture(file);
			thumb_srvs[id] = srv;
			if (state_out != nullptr && srv == nullptr) {
				*state_out = ThumbState::Failed;
			}
			return srv;
		}

		void ReleaseThumbnails() {
			for (auto& entry : thumb_srvs) {
				if (entry.second != nullptr) {
					Core::ReleaseTexture(entry.second);
				}
			}
			thumb_srvs.clear();
		}

		std::string TextureFolder(const std::string& id) {
			return (fs::path("PolyHaven") / id).make_preferred().string();
		}

		bool StartImport(EditorState& state, const std::string& id, const ImportOptions& options,
			std::string& error) {
			if (state.world == nullptr) {
				error = "no level open";
				return false;
			}
			const std::string root = MaterialOps::TexturesDir(state);
			if (root.empty()) {
				error = "no project open - textures are imported into <project>/Assets/Textures";
				return false;
			}
			if (!ValidId(id)) {
				error = "not a Poly Haven asset id: " + id;
				return false;
			}
			if (std::find(std::begin(RESOLUTIONS), std::end(RESOLUTIONS), options.resolution) == std::end(RESOLUTIONS)) {
				error = "resolution must be one of 1k 2k 4k 8k";
				return false;
			}
			std::string mat_file = options.mat_file.empty() ? DefaultMatFile(state) : options.mat_file;
			if (mat_file.empty()) {
				error = "this level declares no material files, so there is nowhere to put the material";
				return false;
			}
			if (state.world->GetMaterialFiles().count(mat_file) == 0) {
				error = "not one of this level's material files: " + mat_file;
				return false;
			}
			PendingImport job;
			job.id = id;
			job.resolution = options.resolution;
			job.mat_file = mat_file;
			job.textures_root = root;
			std::string src;
			{
				std::lock_guard<std::mutex> lock(data_mutex);
				if (import_busy) {
					error = "an import is already running";
					return false;
				}
				import_busy = true;
				import_finished = false;
				import_progress = id + ": fetching the file list";
				src = source;
			}
			const bool height = options.height;
			Post([src, job, height] { DownloadMaps(src, job, height); }, true);
			return true;
		}

		bool ImportBusy() {
			std::lock_guard<std::mutex> lock(data_mutex);
			return import_busy;
		}

		std::string ImportProgress() {
			std::lock_guard<std::mutex> lock(data_mutex);
			return import_progress;
		}

		bool LastImportResult(std::string& message) {
			std::lock_guard<std::mutex> lock(data_mutex);
			message = has_last_result ? last_result_message : std::string();
			return has_last_result && last_result_ok;
		}

		void Tick(EditorState& state) {
			PendingImport job;
			{
				std::lock_guard<std::mutex> lock(data_mutex);
				if (!import_finished) {
					return;
				}
				job = std::move(pending);
				pending = PendingImport{};
				import_finished = false;
			}
			FinishImport(state, job);
			//Busy until the material exists, so a caller polling ImportBusy() never sees
			//"idle" in the gap between the download and the material.
			std::lock_guard<std::mutex> lock(data_mutex);
			import_busy = false;
		}

		void Shutdown() {
			{
				std::lock_guard<std::mutex> lock(queue_mutex);
				stopping = true;
				jobs.clear();
			}
			queue_cv.notify_all();
			for (std::thread& t : workers) {
				t.join();
			}
			workers.clear();
			ReleaseThumbnails();
		}

		static bool show_requested = false;

		void Show(EditorState& state) {
			state.show_material_panel = true;
			show_requested = true;
		}

		bool ConsumeShowRequest() {
			const bool requested = show_requested;
			show_requested = false;
			return requested;
		}

		void DrawTab(EditorState& state) {
			constexpr float THUMB = 96.0f;
			static std::string category = "all";
			static char filter[128] = "";
			static std::string selected;
			static int resolution_index = 0;
			static std::string mat_file;
			static bool height = true;

			RequestCatalog(false);
			std::string catalog_err;
			const CatalogState cs = GetCatalogState(&catalog_err);

			//--- options: what an import does
			ImGui::SetNextItemWidth(70.0f);
			if (ImGui::BeginCombo("Resolution", RESOLUTIONS[resolution_index])) {
				for (int i = 0; i < (int)std::size(RESOLUTIONS); ++i) {
					if (ImGui::Selectable(RESOLUTIONS[i], i == resolution_index)) {
						resolution_index = i;
					}
				}
				ImGui::EndCombo();
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Per map. 1k is ~1-3 MB a material, 4k ~20-40 MB.\n"
					"Where an asset lacks the size, the nearest smaller one is used.");
			}
			ImGui::SameLine();
			const auto& files = state.world->GetMaterialFiles();
			if (mat_file.empty() || files.count(mat_file) == 0) {
				mat_file = DefaultMatFile(state);
			}
			ImGui::SetNextItemWidth(200.0f);
			if (ImGui::BeginCombo("Into", mat_file.empty() ? "(no .mat file)" : mat_file.c_str())) {
				for (const auto& entry : files) {
					if (ImGui::Selectable(entry.first.c_str(), entry.first == mat_file)) {
						mat_file = entry.first;
					}
				}
				ImGui::EndCombo();
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("The .mat file the new material is written to.");
			}
			ImGui::SameLine();
			ImGui::Checkbox("Height map", &height);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Import the displacement map into the height slot, which\n"
					"switches parallax on for the material. Untick to skip it.");
			}
			ImGui::SameLine();
			if (ImGui::Button("Refresh")) {
				RequestCatalog(true);
			}

			if (cs == CatalogState::Loading || cs == CatalogState::Idle) {
				ImGui::TextDisabled("Loading the Poly Haven catalog...");
				return;
			}
			if (cs == CatalogState::Failed) {
				ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Could not load the catalog: %s", catalog_err.c_str());
				return;
			}

			//--- categories on the left
			const float details_height = 130.0f;
			ImGui::BeginChild("##ph_categories", ImVec2(170.0f, -details_height), ImGuiChildFlags_Border);
			for (const auto& c : Categories()) {
				const std::string label = c.first + " (" + std::to_string(c.second) + ")";
				if (ImGui::Selectable(label.c_str(), c.first == category)) {
					category = c.first;
				}
			}
			ImGui::EndChild();
			ImGui::SameLine();

			//--- the grid
			ImGui::BeginGroup();
			ImGui::SetNextItemWidth(200.0f);
			ImGui::InputTextWithHint("##ph_filter", "filter by name or tag", filter, sizeof(filter));
			const std::vector<Asset> shown = Assets(category, filter);
			ImGui::SameLine();
			ImGui::TextDisabled("%d material(s)", (int)shown.size());
			ImGui::BeginChild("##ph_grid", ImVec2(0.0f, -details_height), ImGuiChildFlags_Border);
			const ImGuiStyle& style = ImGui::GetStyle();
			const float cell = THUMB + style.FramePadding.x * 2.0f + style.ItemSpacing.x;
			const int columns = (std::max)(1, (int)(ImGui::GetContentRegionAvail().x / cell));
			const int rows = ((int)shown.size() + columns - 1) / columns;
			const float row_height = THUMB + style.FramePadding.y * 2.0f + ImGui::GetTextLineHeightWithSpacing() +
				style.ItemSpacing.y;
			//Only the rows on screen are submitted, so only their previews are fetched.
			ImGuiListClipper clipper;
			clipper.Begin(rows, row_height);
			while (clipper.Step()) {
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
					for (int col = 0; col < columns; ++col) {
						const int i = row * columns + col;
						if (i >= (int)shown.size()) {
							break;
						}
						const Asset& a = shown[i];
						if (col != 0) {
							ImGui::SameLine();
						}
						ImGui::PushID(a.id.c_str());
						ImGui::BeginGroup();
						const bool is_selected = (selected == a.id);
						if (is_selected) {
							ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.59f, 0.98f, 0.85f));
						}
						ID3D11ShaderResourceView* srv = Thumbnail(a.id);
						bool clicked;
						if (srv != nullptr) {
							clicked = ImGui::ImageButton("##t", (ImTextureID)srv, ImVec2(THUMB, THUMB));
						}
						else {
							clicked = ImGui::Button("##t", ImVec2(THUMB + style.FramePadding.x * 2.0f,
								THUMB + style.FramePadding.y * 2.0f));
						}
						if (is_selected) {
							ImGui::PopStyleColor();
						}
						if (clicked) {
							selected = a.id;
						}
						if (ImGui::IsItemHovered()) {
							ImGui::SetTooltip("%s\n%s", a.name.c_str(), a.id.c_str());
						}
						//Green once this level has the material.
						const bool imported = MaterialExists(state, a.id);
						if (imported) {
							ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.85f, 0.45f, 1.0f));
						}
						ImGui::Text("%.14s%s", a.name.c_str(), a.name.size() > 14 ? "..." : "");
						if (imported) {
							ImGui::PopStyleColor();
						}
						ImGui::EndGroup();
						ImGui::PopID();
					}
				}
			}
			ImGui::EndChild();
			ImGui::EndGroup();

			//--- the selected asset
			ImGui::BeginChild("##ph_details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border);
			Asset a;
			if (selected.empty() || !FindAsset(selected, a)) {
				ImGui::TextDisabled("Select a material to import it. Poly Haven assets are CC0 - free for any use.");
			}
			else {
				if (ID3D11ShaderResourceView* srv = Thumbnail(a.id)) {
					ImGui::Image((ImTextureID)srv, ImVec2(96.0f, 96.0f));
					ImGui::SameLine();
				}
				ImGui::BeginGroup();
				ImGui::Text("%s", a.name.c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("(%s)", a.id.c_str());
				std::string cats;
				for (const std::string& c : a.categories) {
					cats += (cats.empty() ? "" : ", ") + c;
				}
				ImGui::TextDisabled("%s", cats.c_str());
				const bool busy = ImportBusy();
				const bool imported = MaterialExists(state, a.id);
				ImGui::BeginDisabled(busy || mat_file.empty());
				if (ImGui::Button(imported ? "Re-import" : "Import")) {
					ImportOptions options;
					options.resolution = RESOLUTIONS[resolution_index];
					options.mat_file = mat_file;
					options.height = height;
					std::string error;
					if (!StartImport(state, a.id, options, error)) {
						state.status_message = "Poly Haven import failed: " + error;
					}
				}
				ImGui::EndDisabled();
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
					ImGui::SetTooltip("Downloads the maps into Assets/Textures/%s\nand %s material '%s'.",
						TextureFolder(a.id).c_str(), imported ? "repoints" : "creates", a.id.c_str());
				}
				ImGui::SameLine();
				ImGui::BeginDisabled(!imported || Selection::Count(state) == 0);
				if (ImGui::Button("Apply to selection")) {
					int applied = 0;
					std::string error, last_error;
					for (const std::string& entity : Selection::Names(state)) {
						if (MaterialOps::AssignMaterial(state, entity, a.id, error)) {
							++applied;
						}
						else {
							last_error = error;
						}
					}
					state.status_message = "Applied " + a.id + " to " + std::to_string(applied) + " entit" +
						(applied == 1 ? "y" : "ies") + (last_error.empty() ? std::string() : " (" + last_error + ")");
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				if (ImGui::Button("Open on polyhaven.com")) {
					ShellExecuteA(nullptr, "open", ("https://polyhaven.com/a/" + a.id).c_str(),
						nullptr, nullptr, SW_SHOWNORMAL);
				}
				if (busy) {
					ImGui::TextDisabled("Downloading %s...", ImportProgress().c_str());
				}
				else {
					std::string message;
					const bool ok = LastImportResult(message);
					if (!message.empty()) {
						ImGui::TextColored(ok ? ImVec4(0.45f, 0.85f, 0.45f, 1.0f) : ImVec4(0.9f, 0.3f, 0.3f, 1.0f),
							"%s", message.c_str());
					}
				}
				ImGui::EndGroup();
			}
			ImGui::EndChild();
		}
	}
}
