// SimpleMessage — 平台抽象层（宿主内部）
#pragma once

#include <cstddef>
#include <cstdint>

namespace sm {
namespace platform {

class IModule {
 public:
  virtual ~IModule() = default;

  virtual void* symbol(const char* name) = 0;

  virtual const char* last_error() const = 0;

  virtual const char* path() const = 0;
};

IModule* load_module(const char* utf8_path, const char** out_error);

void unload_module(IModule* module);

bool guarded_call(void (*fn)(void*), void* user, std::uint32_t* out_code);

class IWaiter {
 public:
  virtual ~IWaiter() = default;

  virtual bool wait(std::int64_t timeout_ms) = 0;

  virtual void signal() = 0;
};

IWaiter* create_waiter();
void destroy_waiter(IWaiter* waiter);

std::int64_t monotonic_ms();

void emit_debug_output(const char* utf8_line);

class ISingleInstanceLock {
 public:
  virtual ~ISingleInstanceLock() = default;
};

ISingleInstanceLock* acquire_single_instance_lock(const char* utf8_name);

bool notify_existing_instance(const char* utf8_name);

}  // namespace platform
}  // namespace sm
