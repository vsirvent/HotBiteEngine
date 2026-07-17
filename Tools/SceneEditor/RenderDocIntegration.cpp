#include "RenderDocIntegration.h"
#include "renderdoc_app.h"

#include <Windows.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace HotBiteEditor {
	namespace RenderDocIntegration {

		static RENDERDOC_API_1_4_2* rdoc = nullptr;

		bool Load(const std::string& dll_path, const std::string& capture_dir)
		{
			std::string dll = dll_path.empty()
				? "C:\\Program Files\\RenderDoc\\renderdoc.dll"
				: dll_path;

			//Also covers the case of being launched through RenderDoc/renderdoccmd,
			//where the dll is already injected.
			HMODULE mod = GetModuleHandleA("renderdoc.dll");
			if (mod == nullptr) {
				mod = LoadLibraryA(dll.c_str());
			}
			if (mod == nullptr) {
				return false;
			}
			pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
			if (RENDERDOC_GetAPI == nullptr) {
				return false;
			}
			if (RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_4_2, (void**)&rdoc) != 1) {
				rdoc = nullptr;
				return false;
			}

			std::error_code ec;
			fs::create_directories(capture_dir, ec);
			std::string path_template = (fs::path(capture_dir) / "sceneeditor").string();
			rdoc->SetCaptureFilePathTemplate(path_template.c_str());

			//This is a headless/agent-driven workflow: captures come only from the
			//automation channel, never from hotkeys.
			RENDERDOC_InputButton none = eRENDERDOC_Key_Max;
			rdoc->SetCaptureKeys(&none, 0);
			return true;
		}

		bool Available()
		{
			return rdoc != nullptr;
		}

		bool TriggerCapture()
		{
			if (rdoc == nullptr) {
				return false;
			}
			rdoc->TriggerCapture();
			return true;
		}

		uint32_t GetNumCaptures()
		{
			return (rdoc != nullptr) ? rdoc->GetNumCaptures() : 0;
		}

		bool GetLastCapturePath(std::string& path)
		{
			if (rdoc == nullptr) {
				return false;
			}
			uint32_t count = rdoc->GetNumCaptures();
			if (count == 0) {
				return false;
			}
			char filename[512] = {};
			uint32_t len = sizeof(filename);
			uint64_t timestamp = 0;
			if (rdoc->GetCapture(count - 1, filename, &len, &timestamp) == 0) {
				return false;
			}
			path = filename;
			return true;
		}

	}
}
