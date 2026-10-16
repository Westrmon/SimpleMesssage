#include "host/log.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <share.h>
#endif

#include "host/util.h"

namespace sm {
namespace log {
namespace {

constexpr std::size_t kThreadCap      = 8;
constexpr std::size_t kLineCapacity   = kSourceCap + kMessageCap + 64;
constexpr int         kWriteIntervalMs = 200;
constexpr int         kWriteRetryCapMs = 5000;
constexpr const char* kFilePrefix     = "simplemessage-";
constexpr std::size_t kFilePrefixLen  = 14;
constexpr std::size_t kFileDayLen     = 10;
constexpr const char* kFileSuffix     = ".log";

constexpr const char* kLevelNames[5] = {"TRACE", "DEBUG", "INFO ", "WARN ", "ERROR"};

std::size_t level_index(sm::LogLevel level) {
  const int v = static_cast<int>(level);
  if (v < 0) return 0;
  if (v > 4) return 4;
  return static_cast<std::size_t>(v);
}

bool local_time(std::time_t t, std::tm* out) {
#ifdef _WIN32
  return ::localtime_s(out, &t) == 0;
#else
  return ::localtime_r(&t, out) != nullptr;
#endif
}

struct Record {
  std::int64_t time_unix_ms;
  sm::LogLevel level;
  char         thread[kThreadCap];
  char         source[kSourceCap];
  char         message[kMessageCap];
};

std::mutex              g_ring_mutex;
std::condition_variable g_ring_cv;
std::vector<Record>     g_ring;
std::size_t             g_head  = 0;
std::size_t             g_count = 0;
std::uint64_t           g_seq_first = 0;
std::uint64_t           g_seq_next  = 0;
std::uint64_t           g_written_seq = 0;
std::uint64_t           g_dropped = 0;
bool                    g_stop = false;
std::atomic<std::uint64_t> g_version{0};
std::atomic<bool>       g_write_failed{false};

std::mutex        g_file_mutex;
std::string       g_log_dir;
std::atomic<bool> g_file_ready{false};

std::atomic<sm::LogLevel> g_min_level{sm::LogLevel::Info};
std::atomic<EchoFn>       g_echo{nullptr};

ObserverFn g_observer = nullptr;
void*      g_observer_user = nullptr;

std::thread g_writer;
bool        g_initialized = false;

void copy_sanitized(char* dst, std::size_t cap, const char* src, bool mark_truncated) {
  if (cap == 0) return;
  const char* s = (src != nullptr) ? src : "";
  std::size_t i = 0;
  for (; s[i] != '\0' && i + 1 < cap; ++i) {
    const char c = s[i];
    dst[i] = (c == '\n' || c == '\r') ? ' ' : c;
  }
  if (s[i] != '\0') {
    while (i > 0 && (static_cast<unsigned char>(dst[i - 1]) & 0xC0u) == 0x80u) {
      --i;
    }
    if (mark_truncated && i >= 3) {
      dst[i - 3] = '.';
      dst[i - 2] = '.';
      dst[i - 1] = '.';
    }
  }
  dst[i] = '\0';
}

std::int64_t day_key_of(std::time_t t) {
  std::tm tmv{};
  if (!local_time(t, &tmv)) return 0;
  return (static_cast<std::int64_t>(tmv.tm_year + 1900) * 10000) +
         ((tmv.tm_mon + 1) * 100) + tmv.tm_mday;
}

std::int64_t today_key() { return day_key_of(std::time(nullptr)); }

std::string file_name_for_day_key(std::int64_t key) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%s%04d-%02d-%02d%s", kFilePrefix,
                static_cast<int>(key / 10000),
                static_cast<int>((key / 100) % 100),
                static_cast<int>(key % 100), kFileSuffix);
  return buf;
}

void format_line(const Record& r, char* out, std::size_t cap) {
  const std::time_t secs = static_cast<std::time_t>(r.time_unix_ms / 1000);
  std::tm tmv{};
  char stamp[32];
  if (local_time(secs, &tmv)) {
    std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour,
                  tmv.tm_min, tmv.tm_sec,
                  static_cast<int>(r.time_unix_ms % 1000));
  } else {
    std::snprintf(stamp, sizeof(stamp), "0000-00-00 00:00:00.000");
  }

  const int n = std::snprintf(out, cap, "[%s][%s][%s][%s] %s\n", stamp,
                              kLevelNames[level_index(r.level)], r.source,
                              r.thread, r.message);
  if (n < 0) {
    out[0] = '\0';
  } else if (static_cast<std::size_t>(n) >= cap) {
    out[cap - 2] = '\n';
    out[cap - 1] = '\0';
  }
}

void echo_line(const char* line) {
  if (EchoFn fn = g_echo.load(std::memory_order_relaxed)) {
    fn(line);
  }
}

void cleanup_old_logs_locked(int retention_days, int max_total_mb) {
  if (g_log_dir.empty()) return;

  struct Item {
    std::string   name;
    std::string   day;
    std::uint64_t size;
  };

  std::vector<Item> ours;
  for (const util::DirEntry& f : util::list_files(g_log_dir, kFileSuffix)) {
    if (f.name.size() != kFilePrefixLen + kFileDayLen + std::strlen(kFileSuffix)) {
      continue;
    }
    if (f.name.compare(0, kFilePrefixLen, kFilePrefix) != 0) continue;
    const std::string day = f.name.substr(kFilePrefixLen, kFileDayLen);
    if (day[4] != '-' || day[7] != '-') continue;
    ours.push_back(Item{f.name, day, f.size});
  }

  const std::time_t now = std::time(nullptr);
  const std::time_t cutoff_time =
      now - static_cast<std::time_t>(retention_days) * 24 * 3600;
  const std::int64_t cutoff_key = day_key_of(cutoff_time);
  const std::string cutoff = file_name_for_day_key(cutoff_key)
                                 .substr(kFilePrefixLen, kFileDayLen);

  std::vector<Item> kept;
  kept.reserve(ours.size());
  for (const Item& it : ours) {
    if (it.day < cutoff) {
      util::remove_file(util::path_join(g_log_dir, it.name));
    } else {
      kept.push_back(it);
    }
  }

  std::sort(kept.begin(), kept.end(),
            [](const Item& a, const Item& b) { return a.day < b.day; });
  std::uint64_t total = 0;
  for (const Item& it : kept) total += it.size;

  const std::uint64_t limit =
      static_cast<std::uint64_t>(max_total_mb) * 1024ull * 1024ull;
  for (const Item& it : kept) {
    if (total <= limit) break;
    if (util::remove_file(util::path_join(g_log_dir, it.name))) total -= it.size;
  }
}

std::FILE* open_today_locked() {
#ifdef _WIN32
  const std::wstring wpath =
      util::widen(util::path_join(g_log_dir, file_name_for_day_key(today_key())).c_str());
  return ::_wfsopen(wpath.c_str(), L"ab", _SH_DENYNO);
#else
  return std::fopen(util::path_join(g_log_dir, file_name_for_day_key(today_key())).c_str(),
                    "ab");
#endif
}

bool append_locked(const char* data, std::size_t len) {
  if (g_log_dir.empty()) return false;

  std::FILE* file = open_today_locked();
  if (file == nullptr) {
    if (!util::ensure_directory(g_log_dir)) return false;
    file = open_today_locked();
    if (file == nullptr) return false;
  }

  std::fwrite(data, 1, len, file);
  std::fflush(file);
  std::fclose(file);
  return true;
}

bool write_batch(const std::vector<Record>& batch) {
  std::string blob;
  blob.reserve(batch.size() * 128);

  char line[kLineCapacity];
  for (const Record& r : batch) {
    format_line(r, line, sizeof(line));
    echo_line(line);
    blob.append(line);
  }

  std::lock_guard<std::mutex> lock(g_file_mutex);
  return append_locked(blob.data(), blob.size());
}

std::uint64_t pending_from_locked() {
  return (g_written_seq > g_seq_first) ? g_written_seq : g_seq_first;
}

bool has_pending_locked() {
  return g_seq_next > g_written_seq && g_file_ready.load(std::memory_order_relaxed);
}

bool pending_locked() {
  return !g_write_failed.load(std::memory_order_relaxed) && has_pending_locked();
}

void writer_main() {
  util::set_thread_tag("log");
  int interval_ms = kWriteIntervalMs;

  for (;;) {
    std::vector<Record> batch;
    std::uint64_t       batch_upto = 0;
    bool                stopping = false;

    {
      std::unique_lock<std::mutex> lock(g_ring_mutex);
      g_ring_cv.wait_for(lock, std::chrono::milliseconds(interval_ms),
                         [] { return g_stop || pending_locked(); });

      if (g_stop || has_pending_locked()) {
        const std::uint64_t from = pending_from_locked();
        batch_upto = g_seq_next;
        batch.reserve(static_cast<std::size_t>(g_seq_next - from));
        for (std::uint64_t s = from; s < g_seq_next; ++s) {
          batch.push_back(g_ring[(g_head + (s - g_seq_first)) % kRingCapacity]);
        }
      }
      stopping = g_stop;
    }

    if (!batch.empty()) {
      const bool ok = write_batch(batch);
      {
        std::lock_guard<std::mutex> lock(g_ring_mutex);
        if (ok) g_written_seq = batch_upto;
      }
      g_write_failed.store(!ok, std::memory_order_relaxed);
      interval_ms = ok ? kWriteIntervalMs
                       : std::min(interval_ms * 2, kWriteRetryCapMs);
    }
    if (stopping) break;
  }
}

}  // namespace

void Init() {
  if (g_initialized) return;

  g_ring.resize(kRingCapacity);
  g_head = 0;
  g_count = 0;
  g_seq_first = 0;
  g_seq_next = 0;
  g_written_seq = 0;
  g_dropped = 0;
  g_stop = false;
  g_write_failed.store(false, std::memory_order_relaxed);
  g_initialized = true;

  g_writer = std::thread(writer_main);
}

void Shutdown() {
  if (!g_initialized) return;

  {
    std::lock_guard<std::mutex> lock(g_ring_mutex);
    g_stop = true;
  }
  g_ring_cv.notify_all();

  if (g_writer.joinable()) g_writer.join();

  g_file_ready.store(false, std::memory_order_relaxed);
  g_initialized = false;
}

void Configure(const char* utf8_log_dir, int retention_days, int max_total_mb) {
  if (!g_initialized || utf8_log_dir == nullptr) return;

  std::lock_guard<std::mutex> lock(g_file_mutex);

  g_log_dir = utf8_log_dir;
  if (util::ensure_directory(g_log_dir)) {
    cleanup_old_logs_locked(retention_days, max_total_mb);
  }

  g_file_ready.store(true, std::memory_order_relaxed);
  g_ring_cv.notify_all();
}

void SetMinLevel(sm::LogLevel level) {
  g_min_level.store(level, std::memory_order_relaxed);
}

sm::LogLevel MinLevel() { return g_min_level.load(std::memory_order_relaxed); }

void SetEchoSink(EchoFn fn) { g_echo.store(fn, std::memory_order_relaxed); }

void SetObserver(ObserverFn fn, void* user) {
  std::lock_guard<std::mutex> lock(g_ring_mutex);
  g_observer = fn;
  g_observer_user = user;
}

namespace {

void write_record(sm::LogLevel level, const char* source, const char* message) {
  if (!g_initialized) return;

  Record r{};
  r.time_unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  r.level = level;
  copy_sanitized(r.thread, sizeof(r.thread), util::thread_tag(), false);
  copy_sanitized(r.source, sizeof(r.source), (source != nullptr) ? source : "-", true);
  copy_sanitized(r.message, sizeof(r.message), message, true);

  ObserverFn observer = nullptr;
  void*      observer_user = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_ring_mutex);
    if (g_count < kRingCapacity) {
      g_ring[(g_head + g_count) % kRingCapacity] = r;
      ++g_count;
    } else {
      g_ring[g_head] = r;
      g_head = (g_head + 1) % kRingCapacity;
      ++g_dropped;
      ++g_seq_first;
    }
    ++g_seq_next;
    observer = g_observer;
    observer_user = g_observer_user;
  }

  g_version.fetch_add(1, std::memory_order_relaxed);

  if (static_cast<int>(level) >= static_cast<int>(sm::LogLevel::Warn)) {
    g_ring_cv.notify_one();
  }

  if (observer != nullptr) {
    Entry entry{};
    entry.time_unix_ms = r.time_unix_ms;
    entry.level = r.level;
    std::memcpy(entry.source, r.source, sizeof(entry.source));
    std::memcpy(entry.message, r.message, sizeof(entry.message));
    observer(observer_user, entry);
  }
}

}  // namespace

void Write(sm::LogLevel level, const char* source, const char* message) {
  if (static_cast<int>(level) <
      static_cast<int>(g_min_level.load(std::memory_order_relaxed))) {
    return;
  }
  write_record(level, source, message);
}

void Writef(sm::LogLevel level, const char* source, const char* fmt, ...) {
  if (static_cast<int>(level) <
      static_cast<int>(g_min_level.load(std::memory_order_relaxed))) {
    return;
  }

  char buf[kMessageCap];
  va_list args;
  va_start(args, fmt);
  const int n = std::vsnprintf(buf, sizeof(buf), (fmt != nullptr) ? fmt : "", args);
  va_end(args);
  if (n < 0) buf[0] = '\0';

  write_record(level, source, buf);
}

void WriteCritical(sm::LogLevel level, const char* source, const char* message) {
  write_record(level, source, message);
}

void WriteCriticalf(sm::LogLevel level, const char* source, const char* fmt, ...) {
  char buf[kMessageCap];
  va_list args;
  va_start(args, fmt);
  const int n = std::vsnprintf(buf, sizeof(buf), (fmt != nullptr) ? fmt : "", args);
  va_end(args);
  if (n < 0) buf[0] = '\0';

  write_record(level, source, buf);
}

void ForceFlush() {
  std::unique_lock<std::mutex> ring_lock(g_ring_mutex, std::try_to_lock);
  if (!ring_lock.owns_lock()) return;

  std::unique_lock<std::mutex> file_lock(g_file_mutex, std::try_to_lock);
  if (!file_lock.owns_lock()) return;

  const std::uint64_t from = pending_from_locked();
  const std::uint64_t upto = g_seq_next;

  char line[kLineCapacity];
  std::string blob;
  for (std::uint64_t s = from; s < upto; ++s) {
    const Record& r = g_ring[(g_head + (s - g_seq_first)) % kRingCapacity];
    format_line(r, line, sizeof(line));
    echo_line(line);
    blob.append(line);
  }

  if (!blob.empty() && append_locked(blob.data(), blob.size())) {
    g_written_seq = upto;
  }
}

std::uint64_t Version() { return g_version.load(std::memory_order_relaxed); }

std::size_t Snapshot(Entry* out, std::size_t capacity) {
  if (out == nullptr || capacity == 0) return 0;

  std::lock_guard<std::mutex> lock(g_ring_mutex);
  const std::size_t n = (g_count < capacity) ? g_count : capacity;
  const std::size_t skip = g_count - n;

  for (std::size_t i = 0; i < n; ++i) {
    const Record& r = g_ring[(g_head + skip + i) % kRingCapacity];
    out[i].time_unix_ms = r.time_unix_ms;
    out[i].level = r.level;
    std::memcpy(out[i].source, r.source, sizeof(out[i].source));
    std::memcpy(out[i].message, r.message, sizeof(out[i].message));
  }
  return n;
}

std::uint64_t DroppedCount() {
  std::lock_guard<std::mutex> lock(g_ring_mutex);
  return g_dropped;
}

std::size_t CurrentFilePath(char* out, std::size_t capacity) {
  if (out == nullptr || capacity == 0) return 0;
  std::string path;
  {
    std::lock_guard<std::mutex> lock(g_file_mutex);
    if (!g_log_dir.empty()) {
      path = util::path_join(g_log_dir, file_name_for_day_key(today_key()));
    }
  }
  const std::size_t n = std::min(path.size(), capacity - 1);
  std::memcpy(out, path.data(), n);
  out[n] = '\0';
  return n;
}

}  // namespace log
}  // namespace sm
