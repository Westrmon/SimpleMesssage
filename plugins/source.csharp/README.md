# source.csharp — C# / Native AOT 示例插件

用 C# 写成的 Source 插件：每隔 `interval_sec` 秒产生一条通知。它与
[`notifier.console`](../notifier.console/plugin.cpp) 等 C++ 示例遵守同一套
[插件开发规则](../../docs/插件开发/01-入门.md)，区别只在于实现语言和构建方式。

````
SourceCsharp.csproj   构建配置（Native AOT、共享库、输出名 source.csharp.dll）
Exports.cs            与 C++ 的边界：ABI 结构体、导出符号、vtable、arena 编组
PluginBase.cs         PluginBase 插件基类 + PluginEntry 注册表
SourcePlugin.cs       SourcePlugin 基类（SourcePluginSharp.Source）
NotifierPlugin.cs     NotifierPlugin 基类（SourcePluginSharp.Notifier）
Plugin.cs             示例数据源 + 自注册
````

分层很直接：

- `Exports.cs` 只关心 ABI，不引用任何具体插件。它导出宿主查找的四个符号，
  把 `IPlugin` 的每个槽位翻译成托管回调，并把 `IHostContext` 的 vtable
  包装成 `PluginHost`。
- `PluginBase.cs` 定义插件生命周期；`SourcePlugin` 与 `NotifierPlugin`
  分别位于不同命名空间，扩展时不修改基类或边界层。
- `Plugin.cs` 只关心业务，不出现 `unsafe`、指针或 vtable。插件通过
  `PluginHost` 读设置、写日志、注册下一次调度。

## 构建

需要 .NET 10 SDK，以及带 C++ 生成工具（`link.exe`）的 Visual Studio。
Native AOT 的链接阶段会用到后者。

```bat
dotnet publish -c Release -r win-x64
```

产物：

```
bin\Release\net10.0\win-x64\publish\source.csharp.dll
```

> 如果在 Git Bash / 普通命令行里链接时报 `vswhere.exe` 找不到，把
> `C:\Program Files (x86)\Microsoft Visual Studio\Installer` 加进 `PATH`
> 再执行，或者改从 “x64 Native Tools Command Prompt for VS” 里执行。

这个项目**没有**接进 `plugins/CMakeLists.txt`：它由 .NET 工具链构建，
`SM_BUILD_SAMPLES` 只负责 C++ 插件。需要随宿主一起分发时，把上面那个
DLL 复制到宿主的 `plugins/` 目录即可。

## 安装与配置

1. 把 `source.csharp.dll` 放进 `<exe 所在目录>/plugins/`。
2. 在 `config/config.toml` 的 `[[instances]]` 里启用它（实例 id 与插件 id
   均为 `source.csharp`）。
3. 首次运行时宿主会按 schema 自动生成 `config/plugins/source.csharp.toml`：

```toml
schema_version = 1

[settings]
interval_sec = 60
title = "标题"
message = "这是来自 C# AOT 插件的定时通知。"
```

| 设置项 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `interval_sec` | Int | `60` | 通知间隔（秒），范围 `5`–`86400` |
| `title` | String | `C# 插件提醒` | 通知标题；空串回落到默认值 |
| `message` | String | `这是来自 C# AOT 插件的定时通知。` | 通知正文；空串回落到默认值 |

设置项在**每次** `start()` 时重新读取，因此改完配置后下一拍即生效，不需要
重启宿主。

## 写自己的插件

1. 数据源插件继承 `SourcePlugin`，通知器插件继承 `NotifierPlugin`。
2. 重写 `Meta`，填入插件 id 与设置项 schema。
3. 数据源插件重写 `Start`，往 `StartOutput` 里 `Add` 通知，并调用
   `host.ScheduleNext(...)`；通知器插件重写 `OnNotify`。
4. 在插件文件里加一行自注册：

```csharp
internal static class PluginRegistration
{
    [ModuleInitializer]
    internal static void Register () => PluginEntry.Register<你的插件>();
}
```

`SourcePlugin` 位于 `SourcePluginSharp.Source`，`NotifierPlugin` 位于
`SourcePluginSharp.Notifier`，两者都继承 `SourcePluginSharp.Base.PluginBase`。
新增插件类型不需要修改基类或 `Exports.cs`。

需要的话还可以重写 `Initialize` / `Stop` / `Exit` / `OnAction`。
`PluginHost` 提供了 `GetSettingInt` / `GetSettingString` / `GetSettingBool` /
`GetSettingDouble` / `GetSettingEnum`、`NowUnixMs`、`IsCancelled`、`Log` 和
`ScheduleNext`，全部是托管方法，不需要碰指针。

`StartOutput.Error` 用于报告业务错误；把它设成非空并返回
`StartResult.Error`，宿主会记录该错误并进入退避。

## 实现要点

- 边界上只有指针、整数和宿主 arena 内存，没有托管对象、委托或字符串跨过
  模块边界。托管 `Notification` / `PluginMetaInfo` 会在边界层被复制成
  arena 内存或静态非托管结构。
- `Exports.cs` 手工构造了与 MSVC x64 布局一致的 `IPlugin` vtable 和 SDK POD
  结构体，并用 `[UnmanagedCallersOnly(EntryPoint = ...)]` 导出四个符号。
- 每个插件实例用一个 `GCHandle` 挂在非托管实例上；`sm_destroy_plugin`
  释放它，因此多个实例互不干扰。
- 每次 `start()` 都调用 `schedule_next()`；arena 不足时返回 `Error`，但**仍然**
  注册下一次调度，让宿主进入退避。
- 所有导出函数和被宿主调用的 thunk 都用 `try/catch` 包住，异常不会越过边界。

### ABI 是手工同步的

这与 C++ 插件不同：编译器不会替这个示例检查 ABI。以下任何一项变化都必须
同步修改 `Exports.cs`：

- `SM_API_VERSION`（`include/sm/version.h`）；
- `sm::IPlugin` / `sm::IHostContext` 增删或重排虚函数（vtable 槽位与
  `Vt*` 常量）；
- `PluginMeta` / `SettingDef` / `Notification` / `StartOutput` 的字段布局。

只支持 Windows x64 + MSVC 布局。C++ 插件会被宿主的 ABI 版本校验拦下，
而手写布局的本插件必须在编译期之外自行保证一致。

### InvariantGlobalization

当前 `SourceCsharp.csproj` 开启了 `<InvariantGlobalization>true</InvariantGlobalization>`。
它会让产物体积更小；此前在部分机器上曾触发杀毒软件（Kaspersky）误报，
表现为 DLL 生成后无法读取或凭空消失。如果遇到这种情况，先关掉它再试。
