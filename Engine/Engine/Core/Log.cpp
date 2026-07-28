#include "Log.h"

#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <chrono>
#include <ctime>
#include <thread>
#include <sstream>
#include <deque>

namespace HotBite {
	namespace Engine {
		namespace Core {

			namespace {
				std::mutex log_mutex;
				FILE* log_file = nullptr;
				LogLevel min_level = LogLevel::Off;

				//Bounded so a UI can copy the whole thing once a frame without having to
				//think about it; a session that needs more history has the file.
				constexpr size_t RING_CAPACITY = 4096;
				std::deque<LogEntry> ring;

				const char* LevelName(LogLevel level) {
					switch (level) {
					case LogLevel::Trace:   return "TRACE";
					case LogLevel::Debug:   return "DEBUG";
					case LogLevel::Info:    return "INFO";
					case LogLevel::Warning: return "WARN";
					case LogLevel::Error:   return "ERROR";
					case LogLevel::Fatal:   return "FATAL";
					default:                return "?";
					}
				}

				std::string Basename(const char* path) {
					std::string s(path);
					const size_t slash = s.find_last_of("/\\");
					return (slash == std::string::npos) ? s : s.substr(slash + 1);
				}
			}

			void Log::Init(const std::string& path, LogLevel level) {
				std::lock_guard<std::mutex> lock(log_mutex);
				if (log_file != nullptr) {
					fclose(log_file);
					log_file = nullptr;
				}
				log_file = fopen(path.c_str(), "w");
				min_level = level;
				ring.clear();
			}

			void Log::SetLevel(LogLevel level) {
				min_level = level;
			}

			LogLevel Log::GetLevel() {
				return min_level;
			}

			bool Log::Enabled(LogLevel level) {
				return log_file != nullptr && min_level != LogLevel::Off && level >= min_level;
			}

			void Log::Write(LogLevel level, const char* file, int line, const char* fmt, ...) {
				char msg[2048];
				va_list args;
				va_start(args, fmt);
				vsnprintf(msg, sizeof(msg), fmt, args);
				va_end(args);

				const std::string basename = Basename(file);

				const auto now = std::chrono::system_clock::now();
				const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
					now.time_since_epoch()) % 1000;
				const std::time_t t = std::chrono::system_clock::to_time_t(now);
				std::tm tm_buf{};
				localtime_s(&tm_buf, &t);

				char time_buf[16];
				snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d.%03lld",
					tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, (long long)ms.count());

				std::ostringstream tid;
				tid << std::this_thread::get_id();

				std::lock_guard<std::mutex> lock(log_mutex);
				if (log_file != nullptr) {
					fprintf(log_file, "%s [%-5s] [t%s] %s:%d: %s\n",
						time_buf, LevelName(level), tid.str().c_str(), basename.c_str(), line, msg);
					//Flushed on every line: a crash a moment after the line that explains
					//it must not lose it to an unflushed stdio buffer.
					fflush(log_file);
				}

				LogEntry entry;
				entry.level = level;
				entry.time = time_buf;
				entry.file = basename;
				entry.line = line;
				entry.text = msg;
				ring.push_back(std::move(entry));
				if (ring.size() > RING_CAPACITY) {
					ring.pop_front();
				}
			}

			std::vector<LogEntry> Log::Snapshot() {
				std::lock_guard<std::mutex> lock(log_mutex);
				return std::vector<LogEntry>(ring.begin(), ring.end());
			}

			void Log::ClearBuffer() {
				std::lock_guard<std::mutex> lock(log_mutex);
				ring.clear();
			}

			void Log::Shutdown() {
				std::lock_guard<std::mutex> lock(log_mutex);
				if (log_file != nullptr) {
					fclose(log_file);
					log_file = nullptr;
				}
			}

		}
	}
}
