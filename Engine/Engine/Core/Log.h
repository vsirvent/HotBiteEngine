#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace HotBite {
	namespace Engine {
		namespace Core {

			enum class LogLevel : int {
				Trace = 0,
				Debug,
				Info,
				Warning,
				Error,
				Fatal,
				//Not a level anything logs at - the value Init/SetLevel take to mean
				//"nothing reaches the file or buffer", so a binary that never calls
				//Init pays nothing for a LOG_* call left in the code (Enabled() is the
				//one check before the varargs format work happens).
				Off
			};

			//One formatted line, kept in the in-memory ring buffer so a UI (the Scene
			//Editor's Log panel) can render the log without re-reading the file. `file`
			//is the caller's __FILE__ reduced to a basename and `line` its __LINE__ -
			//captured automatically by the LOG_* macros below, never passed by hand -
			//and `text` is the message alone, so a renderer can columnize instead of
			//re-parsing a formatted string.
			struct LogEntry {
				LogLevel level = LogLevel::Info;
				std::string time;   //"HH:MM:SS.mmm"
				std::string file;
				int line = 0;
				std::string text;
			};

			//Process-wide file logger. One process writes one log: Init truncates the
			//file, because the point of reading it is "what did this run do", not an
			//ever-growing history to scroll past. Every Write() call locks and flushes
			//immediately - the usual reason to reach for this is a crash a moment later,
			//and a buffered line a crash never gets to flush is worse than useless, it is
			//actively misleading about where the run got to.
			class Log {
			public:
				//Opens (truncating) the file at `path`, sets the minimum level that
				//reaches it, and clears the ring buffer. Safe to call again to repoint
				//the file or change the level.
				static void Init(const std::string& path, LogLevel min_level = LogLevel::Info);
				static void SetLevel(LogLevel min_level);
				static LogLevel GetLevel();

				//True if a call at `level` would actually write. Checked by the LOG_*
				//macros before they touch the varargs, so a filtered-out Trace costs one
				//comparison rather than a vsnprintf nobody will read - and so a LOG_*
				//left in a hot loop is free until someone opts into that level.
				static bool Enabled(LogLevel level);

				//Formats and appends one line, to the file and to the ring buffer.
				//`file`/`line` are the call site - always the LOG_* macro's __FILE__/
				//__LINE__, never passed by hand. Call through the macros, not this
				//directly - they carry the Enabled() short-circuit.
				static void Write(LogLevel level, const char* file, int line, const char* fmt, ...);

				//A copy of the in-memory ring buffer, oldest first, capped small enough
				//that a full copy once a UI frame is not worth avoiding.
				static std::vector<LogEntry> Snapshot();
				static void ClearBuffer();

				static void Shutdown();
			};

		}
	}
}

#define HB_LOG(level, ...) \
	do { if (::HotBite::Engine::Core::Log::Enabled(level)) { \
		::HotBite::Engine::Core::Log::Write(level, __FILE__, __LINE__, __VA_ARGS__); } } while (0)

#define LOG_TRACE(...) HB_LOG(::HotBite::Engine::Core::LogLevel::Trace,   __VA_ARGS__)
#define LOG_DEBUG(...) HB_LOG(::HotBite::Engine::Core::LogLevel::Debug,   __VA_ARGS__)
#define LOG_INFO(...)  HB_LOG(::HotBite::Engine::Core::LogLevel::Info,    __VA_ARGS__)
#define LOG_WARN(...)  HB_LOG(::HotBite::Engine::Core::LogLevel::Warning, __VA_ARGS__)
#define LOG_ERROR(...) HB_LOG(::HotBite::Engine::Core::LogLevel::Error,   __VA_ARGS__)
#define LOG_FATAL(...) HB_LOG(::HotBite::Engine::Core::LogLevel::Fatal,   __VA_ARGS__)
