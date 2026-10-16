# SimpleMessage — 测试坏插件
#
# 这些 DLL 让宿主的失败路径逐个可复现：崩溃隔离、看门狗、退避、arena 上限、
# 递归中断、UI 引用计数……它们不随发布分发，只由 SM_BUILD_TEST_PLUGINS 打开。
#
# 建到 plugins_test/ 而不是 plugins/：正式产物里不该混进一堆故意会崩溃的插件，
# 想复现某个用例时也只需把要用的那几个拷去 plugins/。一个插件一个目录，目录名、
# 目标名、输出文件名、元信息 id 四者是同一个字符串。

# 每个目录对应一条失败路径，新增用例时在这里加一行即可。
set(_sm_test_plugins
    _test.crash_start
    _test.crash_init
    _test.crash_notify
    _test.crash_exit
    _test.crash_task
    _test.hang
    _test.hang_cancellable
    _test.results
    _test.spam
    _test.arena_hog
    _test.leak_thread
    _test.long_fields
    _test.bad_summary
    _test.no_schedule
    _test.double_schedule
    _test.log_loop
    _test.ui_ref
    _test.slow_draw)

foreach(_plugin IN LISTS _sm_test_plugins)
    sm_add_test_plugin(${_plugin})
endforeach()
