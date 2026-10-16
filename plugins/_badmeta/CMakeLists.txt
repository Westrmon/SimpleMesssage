# SimpleMessage — 元信息坏插件
#
# 每个 DLL 只把元信息（或导出符号表）里的一项写坏，用来逐条证明宿主确实在创建
# 插件实例之前查了它（校验清单见 docs/插件开发/01-入门.md §2.2）。校验没通过就
# 不会创建实例，因此这些文件里没有任何行为实现 —— 也只有这样，"校验漏了一条"
# 才会表现为"这个插件竟然出现在列表里"，一眼可见。
#
# 与 _test/ 一样建到 plugins_test/，同样由 SM_BUILD_TEST_PLUGINS 控制。

# 一个目录一条校验项，名字直接说明坏在哪里。
set(_sm_badmeta_plugins
    _badmeta.abi
    _badmeta.meta_abi
    _badmeta.namespace
    _badmeta.filename
    _badmeta.badid
    _badmeta.range_syntax
    _badmeta.range_miss
    _badmeta.dup_setting
    _badmeta.reserved_key
    _badmeta.empty_enum
    _badmeta.too_many_subs
    _badmeta.no_exports)

foreach(_plugin IN LISTS _sm_badmeta_plugins)
    sm_add_test_plugin(${_plugin})
endforeach()
