#include "MeshySetup.h"

#include "imgui.h"

#include <Windows.h>

#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace HotBiteEditor {
	namespace MeshySetup {

		namespace {

			constexpr const char* POPUP = "Set Meshy API Key";

			// ---------------------------------------------------------- text helpers
			// Own copies, not shared with ClaudeAgent.cpp - its Widen/Narrow/QuoteArg
			// are themselves file-scoped there, not exported.

			std::wstring Widen(const std::string& s) {
				if (s.empty()) return std::wstring();
				int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
				std::wstring w((size_t)n, L'\0');
				MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
				return w;
			}

			bool IsFile(const fs::path& p) {
				std::error_code ec;
				return !p.empty() && fs::is_regular_file(p, ec);
			}

			fs::path ExeDir() {
				wchar_t buf[MAX_PATH * 4];
				DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
				return fs::path(std::wstring(buf, n)).parent_path();
			}

			//One argument quoted for CommandLineToArgvW, exactly ClaudeAgent.cpp's own
			//QuoteArg (backslashes are only special immediately before a quote).
			std::wstring QuoteArg(const std::wstring& a) {
				if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
					return a;
				}
				std::wstring out = L"\"";
				for (auto it = a.begin();; ++it) {
					size_t backslashes = 0;
					while (it != a.end() && *it == L'\\') { ++it; ++backslashes; }
					if (it == a.end()) { out.append(backslashes * 2, L'\\'); break; }
					if (*it == L'"') out.append(backslashes * 2 + 1, L'\\');
					else out.append(backslashes, L'\\');
					out.push_back(*it);
				}
				out.push_back(L'"');
				return out;
			}

			//The script lives in the source tree, not beside the binary - the same
			//walk-up-from-the-exe search ClaudeAgent.cpp's FindServer() does for
			//server.js, since both are found relative to Solution/x64/<Config>/.
			std::string FindScript() {
				wchar_t buf[MAX_PATH * 4];
				DWORD n = GetEnvironmentVariableW(L"HOTBITE_MESHY_SETUP_SCRIPT", buf, (DWORD)std::size(buf));
				if (n > 0 && n < std::size(buf)) {
					fs::path p(std::wstring(buf, n));
					return IsFile(p) ? p.string() : std::string();
				}
				fs::path dir = ExeDir();
				for (int i = 0; i < 8 && !dir.empty(); ++i) {
					fs::path candidate = dir / "Tools" / "SceneEditor" / "mcp" / "setup-meshy-key.ps1";
					if (IsFile(candidate)) return candidate.string();
					if (dir == dir.parent_path()) break;
					dir = dir.parent_path();
				}
				return std::string();
			}

			std::string FindPowerShell() {
				wchar_t sysdir[MAX_PATH] = {};
				GetSystemDirectoryW(sysdir, MAX_PATH);
				fs::path exe = fs::path(sysdir) / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";
				return IsFile(exe) ? exe.string() : std::string();
			}

			// ---------------------------------------------------------- background run

			std::mutex mtx;
			bool busy = false;
			bool has_result = false;
			bool last_ok = false;
			std::string last_message;
			//Tracked by hand rather than asked of ImGui::IsPopupOpen() from IsOpen():
			//the automation channel runs commands *before* ImGui::NewFrame() for that
			//tick (CLAUDE.md - "the automation channel already executes commands
			//pre-frame"), so there is no current window/ID-stack context yet and
			//IsPopupOpen's single-string overload (which scopes the id to
			//g.CurrentWindow) dereferences a null window - a real crash, not a
			//theoretical one (see the fix in Draw() below).
			bool is_open = false;
			std::thread worker;

			//Set by RequestShow(), consumed by Draw() - both run on the main thread,
			//and never concurrently (command processing and the ImGui frame are two
			//phases of one iteration, not two threads), so this one plain bool needs
			//no lock, unlike the worker-thread state above.
			bool open_requested = false;

			void Finish(bool ok, std::string message) {
				std::lock_guard<std::mutex> lock(mtx);
				busy = false;
				has_result = true;
				last_ok = ok;
				last_message = std::move(message);
			}

			//Runs setup-meshy-key.ps1 in a hidden process and writes `key` to its
			//stdin - the only thing that ever sees the plaintext key besides this
			//function's own stack is that one child process. Never touches ctx.channel,
			//the agent, or anything that could put the key in a transcript.
			void RunScript(std::string key, int max_per_task, int max_per_day) {
				const std::string ps1 = FindScript();
				if (ps1.empty()) {
					Finish(false, "setup-meshy-key.ps1 was not found above " + ExeDir().string() +
						" - set HOTBITE_MESHY_SETUP_SCRIPT to its full path.");
					return;
				}
				const std::string pwsh = FindPowerShell();
				if (pwsh.empty()) {
					Finish(false, "powershell.exe was not found under System32\\WindowsPowerShell\\v1.0.");
					return;
				}

				std::wstring cmdline = QuoteArg(Widen(pwsh)) +
					L" -NoProfile -ExecutionPolicy Bypass -File " + QuoteArg(Widen(ps1));
				if (max_per_task > 0) cmdline += L" -MaxCreditsPerTask " + std::to_wstring(max_per_task);
				if (max_per_day > 0) cmdline += L" -MaxCreditsPerDay " + std::to_wstring(max_per_day);

				SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
				HANDLE in_read = nullptr, in_write = nullptr, out_read = nullptr, out_write = nullptr;
				if (!CreatePipe(&in_read, &in_write, &sa, 0) || !CreatePipe(&out_read, &out_write, &sa, 0)) {
					Finish(false, "CreatePipe failed (" + std::to_string(GetLastError()) + ")");
					return;
				}
				//Only the child's ends are inherited.
				SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
				SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);

				STARTUPINFOW si{};
				si.cb = sizeof(si);
				si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
				si.wShowWindow = SW_HIDE;
				si.hStdInput = in_read;
				si.hStdOutput = out_write;
				si.hStdError = out_write;
				PROCESS_INFORMATION pi{};
				std::vector<wchar_t> cmd(cmdline.begin(), cmdline.end());
				cmd.push_back(L'\0');
				BOOL created = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
					CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &si, &pi);
				DWORD create_error = GetLastError();
				CloseHandle(in_read);
				CloseHandle(out_write);
				if (!created) {
					CloseHandle(in_write);
					CloseHandle(out_read);
					Finish(false, "could not start powershell.exe (error " + std::to_string(create_error) + ")");
					return;
				}

				//The key plus a newline, then close the write end: that is what tells
				//the script's [Console]::In.ReadLine() it has the whole line, and closing
				//it is what stops the script waiting for input that is never coming if
				//anything about it goes wrong.
				std::string line = key + "\r\n";
				DWORD written = 0;
				WriteFile(in_write, line.data(), (DWORD)line.size(), &written, nullptr);
				CloseHandle(in_write);

				std::string output;
				char chunk[4096];
				DWORD n = 0;
				while (ReadFile(out_read, chunk, sizeof(chunk), &n, nullptr) && n > 0) {
					output.append(chunk, n);
				}
				CloseHandle(out_read);

				WaitForSingleObject(pi.hProcess, 15000);
				DWORD code = 1;
				GetExitCodeProcess(pi.hProcess, &code);
				CloseHandle(pi.hProcess);
				CloseHandle(pi.hThread);

				//Trim trailing whitespace only, so a multi-line error from the script
				//(Write-Error output lands in the same redirected stream) stays readable.
				while (!output.empty() && (output.back() == '\n' || output.back() == '\r' || output.back() == ' ')) {
					output.pop_back();
				}
				Finish(code == 0, code == 0 ? output : ("setup-meshy-key.ps1 failed (exit " +
					std::to_string(code) + "): " + output));
			}

		} // namespace

		void RequestShow() {
			open_requested = true;
			std::lock_guard<std::mutex> lock(mtx);
			has_result = false;
			last_message.clear();
		}

		bool Busy() {
			std::lock_guard<std::mutex> lock(mtx);
			return busy;
		}

		bool IsOpen() {
			std::lock_guard<std::mutex> lock(mtx);
			return is_open;
		}

		Result LastResult() {
			std::lock_guard<std::mutex> lock(mtx);
			return { has_result, last_ok, last_message };
		}

		void Shutdown() {
			if (worker.joinable()) worker.join();
		}

		void Draw(EditorState& state) {
			static char key_buf[512] = {};
			static int max_per_task = 0;
			static int max_per_day = 0;

			if (open_requested) {
				open_requested = false;
				key_buf[0] = '\0';
				ImGui::OpenPopup(POPUP);
			}

			ImVec2 center = ImGui::GetMainViewport()->GetCenter();
			ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			const bool showing = ImGui::BeginPopupModal(POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize);
			{
				std::lock_guard<std::mutex> lock(mtx);
				is_open = showing;
			}
			if (!showing) {
				return;
			}

			bool currently_busy;
			bool got_result;
			bool ok = false;
			std::string message;
			{
				std::lock_guard<std::mutex> lock(mtx);
				currently_busy = busy;
				got_result = has_result;
				ok = last_ok;
				message = last_message;
			}

			ImGui::PushTextWrapPos(420.0f);
			ImGui::TextUnformatted("Writes only %TEMP%\\HotBiteMeshy\\config.json - never anything under "
				"the project or this repository. The key goes straight to setup-meshy-key.ps1's own process; "
				"it is never seen by Claude or any chat.");
			ImGui::PopTextWrapPos();
			ImGui::Separator();

			ImGui::BeginDisabled(currently_busy);
			ImGui::SetNextItemWidth(360.0f);
			ImGui::InputText("Meshy API key", key_buf, sizeof(key_buf), ImGuiInputTextFlags_Password);
			if (ImGui::TreeNode("Optional spending caps")) {
				ImGui::SetNextItemWidth(120.0f);
				ImGui::InputInt("Max credits per task", &max_per_task);
				ImGui::SetNextItemWidth(120.0f);
				ImGui::InputInt("Max credits per day", &max_per_day);
				ImGui::TextDisabled("0 = no cap. Written to the same config file; meshy_* tools refuse to\n"
					"run above these regardless of what was approved in chat.");
				ImGui::TreePop();
			}
			ImGui::EndDisabled();

			if (currently_busy) {
				ImGui::TextDisabled("Running setup-meshy-key.ps1...");
			}
			else if (got_result) {
				ImGui::PushStyleColor(ImGuiCol_Text, ok ? ImVec4(0.4f, 0.85f, 0.4f, 1.0f) : ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
				ImGui::PushTextWrapPos(420.0f);
				ImGui::TextUnformatted(message.c_str());
				ImGui::PopTextWrapPos();
				ImGui::PopStyleColor();
				state.status_message = std::string("Meshy: ") + message;
			}

			const bool can_save = !currently_busy && key_buf[0] != '\0';
			ImGui::BeginDisabled(!can_save);
			if (ImGui::Button("Save", ImVec2(90.0f, 0.0f))) {
				{
					std::lock_guard<std::mutex> lock(mtx);
					busy = true;
					has_result = false;
				}
				if (worker.joinable()) worker.join(); // the previous run, if any, has already finished
				std::string key(key_buf);
				const int mpt = max_per_task, mpd = max_per_day;
				worker = std::thread([key, mpt, mpd]() { RunScript(key, mpt, mpd); });
				std::memset(key_buf, 0, sizeof(key_buf)); // do not keep the plaintext around once the worker has its own copy
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(currently_busy);
			if (ImGui::Button("Close", ImVec2(90.0f, 0.0f))) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();

			ImGui::EndPopup();
		}

	}
}
