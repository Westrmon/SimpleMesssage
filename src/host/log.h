// SimpleMessage — 日志子系统
#pragma once

#include <cstddef>
#include <cstdint>

#include "sm/host_context.h"

namespace sm {
namespace log {

inline constexpr std::size_t kRingCapacity = 1024;
inline constexpr std::size_t kSourceCap    = 32;
inline constexpr std::size_t kMessageCap   = 1024;

struct Entry {
  std::int64_t time_unix_ms;
  sm::LogLevel level;
  char         source[kSourceCap];
  char         message[kMessageCap];
};

void Init();
void Shutdown();

void Configure(const char* utf8_log_dir, int retention_days, int max_total_mb);

void SetMinLevel(sm::LogLevel level);
sm::LogLevel MinLevel();

void Write(sm::LogLevel level, const char* source, const char* message);
void Writef(sm::LogLevel level, const char* source, const char* fmt, ...);

void WriteCritical(sm::LogLevel level, const char* source, const char* message);
void WriteCriticalf(sm::LogLevel level, const char* source, const char* fmt, ...);

using EchoFn = void (*)(const char* utf8_line);
void SetEchoSink(EchoFn fn);

using ObserverFn = void (*)(void* user, const Entry& entry);
void SetObserver(ObserverFn fn, void* user);

void ForceFlush();

std::uint64_t Version();

std::size_t Snapshot(Entry* out, std::size_t capacity);

std::uint64_t DroppedCount();

std::size_t CurrentFilePath(char* out, std::size_t capacity);

}  // namespace log
}  // namespace sm
