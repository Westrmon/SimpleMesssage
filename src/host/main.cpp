// SimpleMessage — 宿主进程入口

#include <windows.h>
#include <shellapi.h>

#include "host/app.h"
#include "host/log.h"
#include "host/util.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  sm::log::Init();
  sm::util::set_thread_tag("main");

  int exit_code = 0;
  {
    sm::host::App app;

    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    exit_code = app.Run(argc, argv);
    if (argv != nullptr) ::LocalFree(argv);
  }

  sm::log::Shutdown();
  return exit_code;
}
