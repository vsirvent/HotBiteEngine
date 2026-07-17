#pragma once

#include <string>

namespace HotBiteEditor {
	// Last-chance crash reporter, independent of any attached debugger: on an
	// unhandled SEH exception it writes, into the directory given to Install():
	//   - crash.txt: exception code/address plus a symbolized stack trace of the
	//     crashing thread (function, file:line - PDB must sit next to the exe,
	//     which is where the build puts it), also echoed to stderr.
	//   - crash.dmp: a minidump openable in Visual Studio or WinDbg for full
	//     post-mortem inspection.
	// A debugger attached to the process sees the exception first (second-chance
	// stops before this filter would run), so live cdb sessions are unaffected.
	namespace CrashHandler {
		void Install(const std::string& output_dir);
	}
}
