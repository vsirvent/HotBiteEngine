#include "CrashHandler.h"

#include <Windows.h>
#include <DbgHelp.h>
#include <cstdio>

#pragma comment(lib, "dbghelp.lib")

namespace HotBiteEditor {
	namespace CrashHandler {

		//Fixed buffers, filled at Install() time: the exception filter runs in a
		//process that just crashed, so it avoids the heap where it reasonably can.
		static char report_path[MAX_PATH] = {};
		static char dump_path[MAX_PATH] = {};

		static void WriteStackTrace(FILE* out, CONTEXT* context)
		{
			HANDLE process = GetCurrentProcess();
			HANDLE thread = GetCurrentThread();

			SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
			//nullptr search path: dbghelp defaults to the exe's directory (plus
			//_NT_SYMBOL_PATH if set), which is where the build leaves the PDB.
			SymInitialize(process, nullptr, TRUE);

			//StackWalk64 modifies the context it walks; keep the caller's intact
			//for the minidump.
			CONTEXT walk_context = *context;
			STACKFRAME64 frame = {};
			frame.AddrPC.Offset = walk_context.Rip;
			frame.AddrPC.Mode = AddrModeFlat;
			frame.AddrStack.Offset = walk_context.Rsp;
			frame.AddrStack.Mode = AddrModeFlat;
			frame.AddrFrame.Offset = walk_context.Rbp;
			frame.AddrFrame.Mode = AddrModeFlat;

			char symbol_buffer[sizeof(SYMBOL_INFO) + 256] = {};
			SYMBOL_INFO* symbol = (SYMBOL_INFO*)symbol_buffer;
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			symbol->MaxNameLen = 255;

			for (int i = 0; i < 64; ++i) {
				if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame,
					&walk_context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
					break;
				}
				if (frame.AddrPC.Offset == 0) {
					break;
				}
				fprintf(out, "  %02d 0x%016llX", i, (unsigned long long)frame.AddrPC.Offset);

				DWORD64 displacement = 0;
				if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
					fprintf(out, " %s+0x%llX", symbol->Name, (unsigned long long)displacement);
				}
				IMAGEHLP_LINE64 line = {};
				line.SizeOfStruct = sizeof(line);
				DWORD line_displacement = 0;
				if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &line_displacement, &line)) {
					fprintf(out, " [%s:%lu]", line.FileName, line.LineNumber);
				}
				fprintf(out, "\n");
			}
			SymCleanup(process);
		}

		static LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* info)
		{
			//Text report first: it's the most robust artifact and the one an agent
			//or user reads directly.
			FILE* out = nullptr;
			fopen_s(&out, report_path, "w");
			for (FILE* f : { out, stderr }) {
				if (f == nullptr) {
					continue;
				}
				fprintf(f, "=== SceneEditor crash ===\n");
				fprintf(f, "Exception 0x%08X at 0x%016llX\n",
					(unsigned)info->ExceptionRecord->ExceptionCode,
					(unsigned long long)(uintptr_t)info->ExceptionRecord->ExceptionAddress);
				if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
					info->ExceptionRecord->NumberParameters >= 2) {
					fprintf(f, "Access violation: %s address 0x%016llX\n",
						info->ExceptionRecord->ExceptionInformation[0] == 0 ? "reading" :
						info->ExceptionRecord->ExceptionInformation[0] == 1 ? "writing" : "executing (DEP)",
						(unsigned long long)info->ExceptionRecord->ExceptionInformation[1]);
				}
				fprintf(f, "Stack:\n");
				WriteStackTrace(f, info->ContextRecord);
				fprintf(f, "Minidump: %s\n", dump_path);
				fflush(f);
			}
			if (out != nullptr) {
				fclose(out);
			}

			HANDLE dump_file = CreateFileA(dump_path, GENERIC_WRITE, 0, nullptr,
				CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (dump_file != INVALID_HANDLE_VALUE) {
				MINIDUMP_EXCEPTION_INFORMATION mei = {};
				mei.ThreadId = GetCurrentThreadId();
				mei.ExceptionPointers = info;
				mei.ClientPointers = FALSE;
				MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump_file,
					(MINIDUMP_TYPE)(MiniDumpWithDataSegs | MiniDumpWithThreadInfo), &mei, nullptr, nullptr);
				CloseHandle(dump_file);
			}
			return EXCEPTION_EXECUTE_HANDLER; //terminate without the WER dialog
		}

		void Install(const std::string& output_dir)
		{
			snprintf(report_path, sizeof(report_path), "%s\\crash.txt", output_dir.c_str());
			snprintf(dump_path, sizeof(dump_path), "%s\\crash.dmp", output_dir.c_str());
			SetUnhandledExceptionFilter(OnUnhandledException);
		}

	}
}
