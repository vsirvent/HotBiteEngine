#pragma once

#include <string>

namespace HotBiteEditor {
	// Optional RenderDoc in-application integration (renderdoc_app.h SDK), enabled
	// with the --renderdoc command-line switch. Load() must run BEFORE the D3D11
	// device is created: renderdoc.dll installs its API hooks at load time, so a
	// device created earlier would be invisible to it. Captures are triggered
	// programmatically (automation command `rdoc_capture`), no RenderDoc UI or
	// hotkey involved; .rdc files land in the directory given to Load() and are
	// analyzed offline with qrenderdoc's Python API.
	namespace RenderDocIntegration {
		// Loads renderdoc.dll (empty dll_path = the default install,
		// C:\Program Files\RenderDoc\renderdoc.dll) and resolves the API. Capture
		// files are written under capture_dir with the given file prefix.
		bool Load(const std::string& dll_path, const std::string& capture_dir);
		bool Available();

		// Queues a capture of the next rendered frame.
		bool TriggerCapture();

		// Number of completed captures this session, and the path of the newest
		// one (false if none yet).
		uint32_t GetNumCaptures();
		bool GetLastCapturePath(std::string& path);
	}
}
