// SimpleMessage — 宿主进程的"主函数"
//
// 真正的入口符号由平台层提供（src/platform/<平台>/entry_<平台>.cpp）；
// 这里只登记一个函数，避免在 Windows 上出现第二个 main()。

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
