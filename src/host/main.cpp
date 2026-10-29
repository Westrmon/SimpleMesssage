// SimpleMessage — 宿主进程的"主函数"

#include <string>
#include <vector>

#include "host/app.h"
#include "host/log.h"
#include "host/util.h"
#include "platform/entry.h"

namespace {

int HostMain(const std::vector<std::string>& args) {
  sm::log::Init();
  sm::util::set_thread_tag("main");

  int exit_code = 0;
  {
    sm::host::App app;
    exit_code = app.Run(args);
  }

  sm::log::Shutdown();
  return exit_code;
}

struct EntryRegistrar {
  EntryRegistrar() { sm::platform::SetEntryFunction(&HostMain); }
};

EntryRegistrar g_entry_registrar;

}  // namespace
