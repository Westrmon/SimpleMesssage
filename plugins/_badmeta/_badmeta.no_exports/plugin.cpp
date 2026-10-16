// _badmeta.no_exports —— 一个普通 DLL：有导出，但没有一个 sm_* 符号。

#include <cstdint>

extern "C" __declspec(dllexport) std::int32_t not_a_plugin_marker() {
  return 0;
}
