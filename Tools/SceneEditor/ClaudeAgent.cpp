#include "ClaudeAgent.h"
#include "EditorAutomation.h"
#include "EditorHistory.h"

#include <Windows.h>
#include <Core/Json.h>
#include <Core/Log.h>

#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace HotBiteEditor {
	namespace ClaudeAgent {

		namespace {

			//The MCP server's name in the config we write, and so the prefix Claude Code
			//puts on its tools (mcp__hotbite-editor__screenshot).
			constexpr const char* SERVER_NAME = "hotbite-editor";
			//How long an interrupt gets before the process is killed instead.
			constexpr auto STOP_GRACE = std::chrono::seconds(5);
			//How much of a tool result the transcript keeps; the model saw all of it.
			constexpr size_t MAX_TOOL_RESULT = 16 * 1024;
			constexpr size_t MAX_STDERR_TAIL = 8 * 1024;

			// ---------------------------------------------------------- text helpers

			std::wstring Widen(const std::string& s)
			{
				if (s.empty()) return std::wstring();
				int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
				std::wstring w((size_t)n, L'\0');
				MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
				return w;
			}

			std::string Narrow(const std::wstring& w)
			{
				if (w.empty()) return std::string();
				int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
				std::string s((size_t)n, '\0');
				WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
				return s;
			}

			std::string EnvVar(const wchar_t* name)
			{
				wchar_t buf[32768];
				DWORD n = GetEnvironmentVariableW(name, buf, (DWORD)std::size(buf));
				return (n > 0 && n < std::size(buf)) ? Narrow(std::wstring(buf, n)) : std::string();
			}

			bool IsFile(const fs::path& p)
			{
				std::error_code ec;
				return !p.empty() && fs::is_regular_file(p, ec);
			}

			//One argument quoted for CommandLineToArgvW, which is how both claude.exe and
			//node.exe split their command line. Backslashes are only special before a quote.
			std::wstring QuoteArg(const std::wstring& a)
			{
				if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
					return a;
				}
				std::wstring out = L"\"";
				for (auto it = a.begin();; ++it) {
					size_t backslashes = 0;
					while (it != a.end() && *it == L'\\') {
						++it;
						++backslashes;
					}
					if (it == a.end()) {
						out.append(backslashes * 2, L'\\');
						break;
					}
					if (*it == L'"') {
						out.append(backslashes * 2 + 1, L'\\');
					}
					else {
						out.append(backslashes, L'\\');
					}
					out.push_back(*it);
				}
				out.push_back(L'"');
				return out;
			}

			std::string ShortText(const std::string& s, size_t n)
			{
				std::string one = s;
				for (char& c : one) {
					if (c == '\n' || c == '\r' || c == '\t') c = ' ';
				}
				return one.size() > n ? one.substr(0, n) + "..." : one;
			}

			// ---------------------------------------------------------- locating things

			std::string FindOnPath(const wchar_t* exe)
			{
				wchar_t buf[MAX_PATH * 4];
				DWORD n = SearchPathW(nullptr, exe, nullptr, (DWORD)std::size(buf), buf, nullptr);
				return (n > 0 && n < std::size(buf)) ? Narrow(buf) : std::string();
			}

			fs::path ExeDir()
			{
				wchar_t buf[MAX_PATH * 4];
				DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
				return fs::path(std::wstring(buf, n)).parent_path();
			}

			//Claude Code, in the order a user is likeliest to have it: an explicit
			//override, the PATH, the native installer's folder, and last the copy every
			//IDE extension bundles - which on a machine that only uses Claude Code from
			//an IDE is the only one there is. Of several extension versions the newest
			//on disk wins (version strings do not sort as text).
			std::string claude_override;

			std::string FindClaude()
			{
				if (!claude_override.empty()) {
					return IsFile(claude_override) ? claude_override : std::string();
				}
				std::string env = EnvVar(L"HOTBITE_CLAUDE_EXE");
				if (!env.empty()) {
					return IsFile(env) ? env : std::string();
				}
				std::string on_path = FindOnPath(L"claude.exe");
				if (!on_path.empty()) {
					return on_path;
				}
				const fs::path home = Widen(EnvVar(L"USERPROFILE"));
				if (IsFile(home / ".local" / "bin" / "claude.exe")) {
					return (home / ".local" / "bin" / "claude.exe").string();
				}
				fs::path best;
				fs::file_time_type best_time{};
				for (const char* ide : { ".vscode", ".vscode-insiders", ".cursor", ".windsurf" }) {
					std::error_code ec;
					for (const auto& entry : fs::directory_iterator(home / ide / "extensions", ec)) {
						if (entry.path().filename().string().rfind("anthropic.claude-code", 0) != 0) {
							continue;
						}
						fs::path exe = entry.path() / "resources" / "native-binary" / "claude.exe";
						if (!IsFile(exe)) {
							continue;
						}
						auto t = fs::last_write_time(exe, ec);
						if (best.empty() || t > best_time) {
							best = exe;
							best_time = t;
						}
					}
				}
				return best.string();
			}

			std::string FindNode()
			{
				std::string env = EnvVar(L"HOTBITE_NODE_EXE");
				if (!env.empty()) {
					return IsFile(env) ? env : std::string();
				}
				std::string on_path = FindOnPath(L"node.exe");
				if (!on_path.empty()) {
					return on_path;
				}
				fs::path pf = Widen(EnvVar(L"ProgramFiles"));
				return IsFile(pf / "nodejs" / "node.exe") ? (pf / "nodejs" / "node.exe").string() : std::string();
			}

			//The server lives in the source tree, not beside the binary, so walk up from
			//Solution/x64/<Config>/ to the repository root.
			std::string FindServer()
			{
				std::string env = EnvVar(L"HOTBITE_MCP_SERVER");
				if (!env.empty()) {
					return IsFile(env) ? env : std::string();
				}
				fs::path dir = ExeDir();
				for (int i = 0; i < 8 && !dir.empty(); ++i) {
					fs::path candidate = dir / "Tools" / "SceneEditor" / "mcp" / "server.js";
					if (IsFile(candidate)) {
						return candidate.string();
					}
					if (dir == dir.parent_path()) break;
					dir = dir.parent_path();
				}
				return std::string();
			}

			bool EndsWith(const std::string& s, const char* suffix)
			{
				size_t n = strlen(suffix);
				return s.size() >= n && _stricmp(s.c_str() + s.size() - n, suffix) == 0;
			}

			// ---------------------------------------------------------- the process

			struct Process {
				HANDLE process = nullptr;
				HANDLE job = nullptr;
				HANDLE stdin_write = nullptr;
				std::thread out_reader;
				std::thread err_reader;
			};
			Process proc;

			//Filled by the reader threads, drained by Tick. A stdout line that is not
			//JSON is queued as {"type":"__text__"}; the end of the stream as __exit__.
			std::mutex queue_mutex;
			std::deque<json> events;
			std::string stderr_tail;

			// ---------------------------------------------------------- main-thread state

			Status status = Status::Idle;
			std::string last_error;
			std::string session_id;
			std::string active_model;
			std::string model;
			bool model_loaded = false;
			std::string launched_model;
			double cost_before_process = 0.0; //turns of earlier processes of this conversation
			double process_cost = 0.0;        //the running process's own total
			std::vector<Item> transcript;
			std::string live_text;
			uint64_t revision = 0;
			bool turn_group_open = false;
			std::chrono::steady_clock::time_point stop_deadline;
			int request_counter = 0;

			void Changed()
			{
				++revision;
			}

			void AddItem(Item::Kind kind, const std::string& text)
			{
				Item item;
				item.kind = kind;
				item.text = text;
				transcript.push_back(std::move(item));
				Changed();
			}

			void ReadStdout(HANDLE pipe, HANDLE process)
			{
				std::string buffer;
				char chunk[16384];
				DWORD n = 0;
				while (ReadFile(pipe, chunk, sizeof(chunk), &n, nullptr) && n > 0) {
					buffer.append(chunk, n);
					size_t nl;
					while ((nl = buffer.find('\n')) != std::string::npos) {
						std::string line = buffer.substr(0, nl);
						buffer.erase(0, nl + 1);
						if (!line.empty() && line.back() == '\r') line.pop_back();
						if (line.empty()) continue;
						json j = json::parse(line, nullptr, false);
						if (j.is_discarded() || !j.is_object()) {
							j = json{ { "type", "__text__" }, { "text", line } };
						}
						std::lock_guard<std::mutex> lock(queue_mutex);
						events.push_back(std::move(j));
					}
				}
				WaitForSingleObject(process, 5000);
				DWORD code = 0;
				GetExitCodeProcess(process, &code);
				std::lock_guard<std::mutex> lock(queue_mutex);
				events.push_back(json{ { "type", "__exit__" }, { "code", (int)code } });
			}

			void ReadStderr(HANDLE pipe)
			{
				char chunk[4096];
				DWORD n = 0;
				while (ReadFile(pipe, chunk, sizeof(chunk), &n, nullptr) && n > 0) {
					std::string text(chunk, n);
					LOG_DEBUG("claude stderr: %s", text.c_str());
					std::lock_guard<std::mutex> lock(queue_mutex);
					stderr_tail += text;
					if (stderr_tail.size() > MAX_STDERR_TAIL) {
						stderr_tail.erase(0, stderr_tail.size() - MAX_STDERR_TAIL);
					}
				}
			}

			//Ends the process tree (Claude Code and the MCP server it started - both are
			//in the job) and joins the readers, which then see their pipes close. Anything
			//they queued is dropped: it described a process that is gone.
			void Kill()
			{
				if (proc.stdin_write != nullptr) {
					CloseHandle(proc.stdin_write);
					proc.stdin_write = nullptr;
				}
				if (proc.job != nullptr) {
					TerminateJobObject(proc.job, 1);
				}
				if (proc.out_reader.joinable()) proc.out_reader.join();
				if (proc.err_reader.joinable()) proc.err_reader.join();
				if (proc.process != nullptr) CloseHandle(proc.process);
				if (proc.job != nullptr) CloseHandle(proc.job);
				proc.process = nullptr;
				proc.job = nullptr;
				std::lock_guard<std::mutex> lock(queue_mutex);
				events.clear();
				cost_before_process += process_cost;
				process_cost = 0.0;
			}

			bool WriteLine(const json& j)
			{
				if (proc.stdin_write == nullptr) {
					return false;
				}
				std::string line = j.dump() + "\n";
				DWORD written = 0;
				return WriteFile(proc.stdin_write, line.data(), (DWORD)line.size(), &written, nullptr) &&
					written == line.size();
			}

			void EndTurn()
			{
				if (turn_group_open) {
					EditorHistory::EndGroup();
					turn_group_open = false;
				}
			}

			std::string SystemPrompt(EditorState& state, const Setup& s)
			{
				fs::path readme = fs::path(s.server).parent_path().parent_path() / "automation" / "README.md";
				std::string p;
				p += "You are working inside the HotBite Scene Editor, the level editor of a C++20/DirectX 11 game engine. ";
				p += "You are its Claude panel: the person you talk to has the editor open and is looking at the level while you work on it.\n\n";
				p += "Project root: " + state.project_root + "\n";
				p += "Open level: " + state.current_level_path + "\n\n";
				p += "How to work:\n";
				p += "- Use the hotbite-editor tools. scene_summary gives an overview; screenshot shows what the person sees (the level plus the editor UI).\n";
				p += "- Change things only with editor_command. It runs the editor's automation commands, the same ones the regression tests use. The full command reference is " + readme.string() + ": read it before using a command you have not used yet.\n";
				p += "- Never write the level, .mat, .tpl or other project files on disk. The editor holds the live copy and would overwrite them. Your file tools are read-only on purpose.\n";
				p += "- Everything you do for one message is a single undo step for the person, so do not undo your own work unless they ask you to.\n";
				p += "- Do not save (File/Save Level, save_materials, save_templates) unless asked. The person decides when the level is written.\n";
				p += "- After a visible change, check it: take a screenshot, or read the entity back.\n";
				p += "- Asset layers: a model is an imported .fbx (meshes, materials, animation clips) and cannot be placed; a template is one placeable concept built from those assets; an instance is a template placed in the level. Get a new object into the level with import_model, then create_template_from_model, then place.\n";
				p += "- The person sees your replies in a narrow panel. Keep them short and plain.\n";
				return p;
			}

			bool WriteText(const fs::path& path, const std::string& text, std::string& error)
			{
				std::ofstream out(path, std::ios::binary | std::ios::trunc);
				if (!out.is_open()) {
					error = "cannot write " + path.string();
					return false;
				}
				out << text;
				return true;
			}

			bool Launch(EditorState& state, std::string& error)
			{
				Setup s = Locate(state);
				if (!s.problem.empty()) {
					error = s.problem;
					return false;
				}
				EditorAutomation::AddRoot(s.channel_dir);
				//A batch left over from a previous agent in the same folder must not fire
				//into this one.
				std::error_code ec;
				fs::remove(fs::path(s.channel_dir) / "command.txt", ec);
				fs::remove(fs::path(s.channel_dir) / "response.txt", ec);

				const fs::path base = s.base_dir;
				json mcp;
				mcp["mcpServers"][SERVER_NAME] = {
					{ "type", "stdio" },
					{ "command", s.node },
					{ "args", json::array({ s.server, "--dir", s.channel_dir, "--embedded" }) },
				};
				const fs::path mcp_file = base / "agent-mcp.json";
				const fs::path prompt_file = base / "agent-prompt.md";
				if (!WriteText(mcp_file, mcp.dump(2), error) || !WriteText(prompt_file, SystemPrompt(state, s), error)) {
					return false;
				}

				std::vector<std::wstring> argv;
				std::wstring exe;
				if (EndsWith(s.claude, ".js")) {
					//A script standing in for Claude Code (the automation suite's fake one).
					exe = Widen(s.node);
					argv.push_back(Widen(s.claude));
				}
				else {
					exe = Widen(s.claude);
				}
				const std::string allowed = std::string("mcp__") + SERVER_NAME + " Read Glob Grep TodoWrite";
				for (const char* a : { "-p", "--input-format", "stream-json", "--output-format", "stream-json",
					"--verbose", "--include-partial-messages", "--strict-mcp-config", "--permission-prompts", "none" }) {
					argv.push_back(Widen(a));
				}
				argv.push_back(L"--mcp-config");
				argv.push_back(mcp_file.wstring());
				argv.push_back(L"--append-system-prompt-file");
				argv.push_back(prompt_file.wstring());
				argv.push_back(L"--allowedTools");
				argv.push_back(Widen(allowed));
				argv.push_back(L"--disallowedTools");
				argv.push_back(L"Write Edit NotebookEdit Bash PowerShell");
				argv.push_back(L"--add-dir");
				argv.push_back(fs::path(s.server).parent_path().parent_path().wstring());
				if (!model.empty()) {
					argv.push_back(L"--model");
					argv.push_back(Widen(model));
				}
				if (!session_id.empty()) {
					argv.push_back(L"--resume");
					argv.push_back(Widen(session_id));
				}

				std::wstring cmdline = QuoteArg(exe);
				for (const std::wstring& a : argv) {
					cmdline += L" " + QuoteArg(a);
				}

				fs::path cwd = state.project_root.empty() ? base : fs::path(Widen(state.project_root));

				SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
				HANDLE in_read = nullptr, in_write = nullptr, out_read = nullptr, out_write = nullptr,
					err_read = nullptr, err_write = nullptr;
				if (!CreatePipe(&in_read, &in_write, &sa, 0) || !CreatePipe(&out_read, &out_write, &sa, 0) ||
					!CreatePipe(&err_read, &err_write, &sa, 0)) {
					error = "CreatePipe failed (" + std::to_string(GetLastError()) + ")";
					return false;
				}
				//Only the child's ends are inherited.
				SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
				SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
				SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);

				STARTUPINFOW si{};
				si.cb = sizeof(si);
				si.dwFlags = STARTF_USESTDHANDLES;
				si.hStdInput = in_read;
				si.hStdOutput = out_write;
				si.hStdError = err_write;
				PROCESS_INFORMATION pi{};
				std::vector<wchar_t> cmd(cmdline.begin(), cmdline.end());
				cmd.push_back(L'\0');
				BOOL created = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
					CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr,
					cwd.wstring().c_str(), &si, &pi);
				DWORD create_error = GetLastError();
				CloseHandle(in_read);
				CloseHandle(out_write);
				CloseHandle(err_write);
				if (!created) {
					CloseHandle(in_write);
					CloseHandle(out_read);
					CloseHandle(err_read);
					error = "could not start " + Narrow(exe) + " (error " + std::to_string(create_error) + ")";
					return false;
				}

				//The job ties the whole tree - Claude Code and the node server it starts - to
				//this editor: closing the job handle, or the editor dying, ends all of it.
				proc.job = CreateJobObjectW(nullptr, nullptr);
				JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
				limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
				SetInformationJobObject(proc.job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
				AssignProcessToJobObject(proc.job, pi.hProcess);
				ResumeThread(pi.hThread);
				CloseHandle(pi.hThread);

				proc.process = pi.hProcess;
				proc.stdin_write = in_write;
				{
					std::lock_guard<std::mutex> lock(queue_mutex);
					events.clear();
					stderr_tail.clear();
				}
				proc.out_reader = std::thread([out_read, p = pi.hProcess]() {
					ReadStdout(out_read, p);
					CloseHandle(out_read);
				});
				proc.err_reader = std::thread([err_read]() {
					ReadStderr(err_read);
					CloseHandle(err_read);
				});
				launched_model = model;
				LOG_INFO("Claude panel: started %s (pid %lu) in %s", Narrow(exe).c_str(), pi.dwProcessId, cwd.string().c_str());
				return true;
			}

			fs::path SettingsFile()
			{
				return fs::path(Widen(EnvVar(L"APPDATA"))) / "HotBite" / "claude-panel.json";
			}

			void LoadModel()
			{
				if (model_loaded) return;
				model_loaded = true;
				std::ifstream in(SettingsFile());
				if (!in.is_open()) return;
				json j = json::parse(in, nullptr, false);
				if (j.is_object() && j.contains("model") && j["model"].is_string()) {
					model = j["model"].get<std::string>();
				}
			}

			// ---------------------------------------------------------- events

			std::string ToolResultText(const json& content, int& images)
			{
				if (content.is_string()) {
					return content.get<std::string>();
				}
				std::string out;
				if (content.is_array()) {
					for (const json& c : content) {
						const std::string type = c.value("type", "");
						if (type == "text") {
							if (!out.empty()) out += "\n";
							out += c.value("text", "");
						}
						else if (type == "image") {
							++images;
						}
					}
				}
				return out;
			}

			std::string ToolName(const std::string& name)
			{
				const std::string prefix = std::string("mcp__") + SERVER_NAME + "__";
				return name.rfind(prefix, 0) == 0 ? name.substr(prefix.size()) : name;
			}

			void OnInit(const json& j)
			{
				session_id = j.value("session_id", session_id);
				active_model = j.value("model", active_model);
				if (j.contains("mcp_servers") && j["mcp_servers"].is_array()) {
					for (const json& server : j["mcp_servers"]) {
						if (server.value("name", "") == SERVER_NAME && server.value("status", "") != "connected") {
							AddItem(Item::Kind::Error, std::string("The editor tools did not start (") +
								server.value("status", "?") + "). Claude cannot see the level. Check the Log panel.");
						}
					}
				}
				Changed();
			}

			void OnAssistant(const json& j)
			{
				const json& content = j["message"]["content"];
				if (!content.is_array()) return;
				for (const json& c : content) {
					const std::string type = c.value("type", "");
					if (type == "text") {
						std::string text = c.value("text", "");
						if (!text.empty()) AddItem(Item::Kind::Assistant, text);
					}
					else if (type == "tool_use") {
						Item item;
						item.kind = Item::Kind::Tool;
						item.tool_id = c.value("id", "");
						item.tool_name = ToolName(c.value("name", ""));
						item.text = c.contains("input") ? c["input"].dump() : "{}";
						transcript.push_back(std::move(item));
					}
				}
				live_text.clear();
				Changed();
			}

			void OnUser(const json& j)
			{
				const json& content = j["message"]["content"];
				if (!content.is_array()) return;
				for (const json& c : content) {
					if (c.value("type", "") != "tool_result") continue;
					const std::string id = c.value("tool_use_id", "");
					for (auto it = transcript.rbegin(); it != transcript.rend(); ++it) {
						if (it->kind == Item::Kind::Tool && it->tool_id == id) {
							int images = 0;
							std::string text = c.contains("content") ? ToolResultText(c["content"], images) : "";
							if (text.size() > MAX_TOOL_RESULT) {
								text = text.substr(0, MAX_TOOL_RESULT) + "\n... (truncated)";
							}
							it->tool_result = std::move(text);
							it->tool_images = images;
							it->tool_error = c.value("is_error", false);
							it->tool_done = true;
							break;
						}
					}
				}
				Changed();
			}

			void OnResult(const json& j)
			{
				//total_cost_usd is the process's running total, not the turn's.
				if (j.contains("total_cost_usd") && j["total_cost_usd"].is_number()) {
					process_cost = j["total_cost_usd"].get<double>();
				}
				session_id = j.value("session_id", session_id);
				const std::string subtype = j.value("subtype", "success");
				const bool is_error = j.value("is_error", false) || subtype != "success";
				char line[160];
				snprintf(line, sizeof(line), "%s in %.1f s", is_error ? "stopped" : "done",
					j.value("duration_ms", 0.0) / 1000.0);
				if (is_error && status != Status::Stopping) {
					std::string why = j.contains("result") && j["result"].is_string() ? j["result"].get<std::string>() : subtype;
					AddItem(Item::Kind::Error, why);
				}
				AddItem(Item::Kind::Result, line);
				live_text.clear();
				//A tool call that never got its result (interrupted) is over too.
				for (Item& item : transcript) {
					if (item.kind == Item::Kind::Tool && !item.tool_done) {
						item.tool_done = true;
						item.tool_error = true;
						item.tool_result = "(interrupted)";
					}
				}
				EndTurn();
				status = Status::Ready;
			}

			void OnExit(int code)
			{
				const bool mid_turn = status == Status::Busy;
				//Kill joins the stderr reader, so the tail read after it has everything the
				//process wrote before it died - usually the one line that says why.
				Kill();
				std::string tail;
				{
					std::lock_guard<std::mutex> lock(queue_mutex);
					tail = stderr_tail;
				}
				EndTurn();
				if (mid_turn || code != 0) {
					last_error = "Claude exited (code " + std::to_string(code) + ")" +
						(tail.empty() ? std::string() : ":\n" + ShortText(tail, 1500));
					AddItem(Item::Kind::Error, last_error);
					status = Status::Failed;
				}
				else {
					status = Status::Idle;
				}
			}
		}

		// ---------------------------------------------------------- public

		Setup Locate(EditorState& state)
		{
			(void)state;
			Setup s;
			s.claude = FindClaude();
			s.node = FindNode();
			s.server = FindServer();
			fs::path base = EditorAutomation::Enabled()
				? fs::path(Widen(EditorAutomation::Dir()))
				: fs::temp_directory_path() / ("hotbite-editor-" + std::to_string(GetCurrentProcessId()));
			s.base_dir = base.string();
			s.channel_dir = (base / "agent").string();
			std::error_code ec;
			fs::create_directories(base, ec);
			if (s.claude.empty()) {
				s.problem = "Claude Code was not found. Install it (it must be logged in), put claude.exe on the PATH, "
					"or set HOTBITE_CLAUDE_EXE to its full path.";
			}
			else if (s.node.empty()) {
				s.problem = "node.exe was not found; it runs the editor's MCP server. Install Node.js or set HOTBITE_NODE_EXE.";
			}
			else if (s.server.empty()) {
				s.problem = "The editor's MCP server (Tools/SceneEditor/mcp/server.js) was not found above " +
					ExeDir().string() + ". Set HOTBITE_MCP_SERVER to its full path.";
			}
			return s;
		}

		bool Send(EditorState& state, const std::string& text, std::string& error)
		{
			LoadModel();
			if (status == Status::Busy || status == Status::Stopping) {
				error = "Claude is still working on the previous message";
				return false;
			}
			if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
				error = "empty message";
				return false;
			}
			if (proc.process != nullptr && launched_model != model) {
				Kill();
			}
			if (proc.process == nullptr) {
				if (!Launch(state, error)) {
					last_error = error;
					status = Status::Failed;
					AddItem(Item::Kind::Error, error);
					return false;
				}
			}
			AddItem(Item::Kind::User, text);
			json msg = {
				{ "type", "user" },
				{ "message", { { "role", "user" }, { "content", json::array({ { { "type", "text" }, { "text", text } } }) } } },
			};
			if (!WriteLine(msg)) {
				error = "could not write to Claude's input";
				last_error = error;
				Kill();
				status = Status::Failed;
				AddItem(Item::Kind::Error, error);
				return false;
			}
			EditorHistory::BeginGroup("Claude: " + ShortText(text, 40));
			turn_group_open = true;
			status = Status::Busy;
			last_error.clear();
			live_text.clear();
			Changed();
			return true;
		}

		void Stop()
		{
			if (status != Status::Busy) {
				return;
			}
			json req = {
				{ "type", "control_request" },
				{ "request_id", "hotbite-" + std::to_string(++request_counter) },
				{ "request", { { "subtype", "interrupt" } } },
			};
			WriteLine(req);
			status = Status::Stopping;
			stop_deadline = std::chrono::steady_clock::now() + STOP_GRACE;
			AddItem(Item::Kind::Info, "Stopping...");
		}

		void NewConversation()
		{
			Kill();
			EndTurn();
			session_id.clear();
			active_model.clear();
			transcript.clear();
			live_text.clear();
			cost_before_process = 0.0;
			process_cost = 0.0;
			last_error.clear();
			status = Status::Idle;
			Changed();
		}

		void Tick(EditorState& state)
		{
			(void)state;
			std::deque<json> batch;
			{
				std::lock_guard<std::mutex> lock(queue_mutex);
				batch.swap(events);
			}
			for (const json& j : batch) {
				const std::string type = j.value("type", "");
				if (type == "system" && j.value("subtype", "") == "init") {
					OnInit(j);
				}
				else if (type == "stream_event") {
					const json& e = j["event"];
					const std::string etype = e.value("type", "");
					if (etype == "content_block_delta" && e.contains("delta") && e["delta"].value("type", "") == "text_delta") {
						live_text += e["delta"].value("text", "");
						Changed();
					}
					else if (etype == "message_start") {
						live_text.clear();
					}
				}
				else if (type == "assistant") {
					OnAssistant(j);
				}
				else if (type == "user") {
					OnUser(j);
				}
				else if (type == "result") {
					OnResult(j);
				}
				else if (type == "__exit__") {
					OnExit(j.value("code", 0));
					break; //Kill() dropped whatever else was queued
				}
				else if (type == "__text__") {
					LOG_DEBUG("claude stdout: %s", j.value("text", "").c_str());
				}
			}
			if (status == Status::Stopping && std::chrono::steady_clock::now() > stop_deadline) {
				Kill();
				EndTurn();
				AddItem(Item::Kind::Info, "Stopped. The conversation continues with your next message.");
				status = Status::Idle;
			}
		}

		void Shutdown()
		{
			Kill();
			EndTurn();
		}

		Status GetStatus() { return status; }

		const char* StatusName(Status s)
		{
			switch (s) {
			case Status::Idle: return "idle";
			case Status::Busy: return "busy";
			case Status::Ready: return "ready";
			case Status::Stopping: return "stopping";
			case Status::Failed: return "failed";
			}
			return "?";
		}

		void SetClaudeOverride(const std::string& path) { claude_override = path; }
		std::string LastError() { return last_error; }
		std::string SessionId() { return session_id; }
		std::string ActiveModel() { return active_model; }
		double TotalCostUsd() { return cost_before_process + process_cost; }
		std::string Model()
		{
			LoadModel();
			return model;
		}

		void SetModel(const std::string& m, bool remember)
		{
			LoadModel();
			model = m;
			if (remember) {
				std::error_code ec;
				fs::create_directories(SettingsFile().parent_path(), ec);
				std::ofstream out(SettingsFile(), std::ios::trunc);
				out << json{ { "model", model } }.dump(2);
			}
		}
		const std::vector<Item>& Transcript() { return transcript; }
		std::string LiveText() { return live_text; }
		uint64_t Revision() { return revision; }
	}
}
