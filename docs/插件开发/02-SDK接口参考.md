# 插件开发 02 — SDK 接口参考

本文是 `include/sm/*.h` 的**逐符号参考**。头文件与本文必须保持一一对应：
改了头文件就要改本文，反之亦然。

**阅读顺序建议**：先看 §1（版本体系）与 §2（约定），再从 §4 开始按需查阅。
要写插件的话，最常用的三节是 §7（`IPlugin`）、§6.3（`IHostContext` 的调度/设置/日志）、
§5（`Notification`）。

| 头文件 | 内容 | 本文小节 |
|---|---|---|
| `sm/types.h` | `Span`、`limits` | §3 |
| `sm/version.h` | 版本宏 | §1 |
| `sm/notification.h` | `Level`、`Action`、`Notification` | §5 |
| `sm/host_context.h` | `LogLevel`、`IHostContext` | §6 |
| `sm/plugin.h` | `PluginKind`、`StartResult`、设置 schema、`PluginMeta`、`IPlugin`、`PluginBase` | §7 |
| `sm/export.h` | `SM_DEFINE_PLUGIN` 与四个导出符号 | §8 |
| `sm/version_range.h` | 版本号与范围解析 | §9 |
| `sm/summary.h` | UTF-8 工具与 `make_summary` | §10 |

---

## 1. 版本体系

**四个不同的版本概念，混淆它们是本 SDK 最容易犯的错。**

| # | 概念 | 载体 | 取值示例 | 谁检查 |
|---|---|---|---|---|
| 1 | **宿主版本** | `SM_VERSION_STRING`、`IHostContext::host_version()` | `"0.1.0"` | 人看 |
| 2 | **API / ABI 版本** | `SM_API_VERSION`、`sm_abi_version()`、`PluginMeta::api_version` | `2` | 宿主加载时**精确匹配** |
| 3 | **插件自身版本** | `PluginMeta::version` | `"1.3.0"` | 宿主不解释 |
| 4 | **兼容范围** | `PluginMeta::host_range` | `">=0.1.0,<1.0.0"` | 宿主用 §9 的规则判定 |

### 1.1 API 版本（`SM_API_VERSION`）的递增规则

`SM_API_VERSION` 是**二进制接口**的版本。以下任何一项发生，它**必须**递增，
且**不提供任何向后兼容**：

- 增删 `IPlugin` / `IHostContext` 的虚函数；
- 改变任何虚函数的参数、返回类型或 const 限定；
- 增删或重排 `PluginMeta`、`SettingDef`、`Notification`、`Action`、
  `StartOutput`、`SettingOption` 的任何字段；
- 改变任何枚举的底层类型或已有枚举值；
- 改变 `sm::limits` 中任何常量的值（它会改变内存布局与截断行为）。

> **为什么"改变 `limits` 常量"也算不兼容**：一个按 `kMaxContentBytes = 65536`
> 编译的插件，其 `Notification` 里的字符串长度假设可能与宿主不同。
> 精确匹配是最省事的正确答案。

> **为什么不提供兼容**：跨 ABI 的兼容层意味着两级间接与一堆版本分支，
> 而收益仅仅是"不必重新编译"。插件的分发形态本来就是"一个 DLL"，
> 重新编译一次的成本远低于兼容层的长期维护成本。

### 1.2 宿主版本的升级规则

宿主版本是 SemVer，含义按 SemVer 的约定：

- **主版本号**变化：对插件作者而言是破坏性变化（通常伴随 `SM_API_VERSION` 递增）。
- **次版本号**变化：新增能力，老插件应继续可用。
- **修订号**变化：修 bug。

`PluginMeta::host_range` 写的是**宿主版本**的范围，不是 API 版本。

### 1.3 版本号文字语法（§9 的输入）

```
"1"      → 1.0.0
"1.2"    → 1.2.0
"1.2.3"  → 1.2.3
```

允许前后空白。**不接受**前导 `v`、预发布标签（`-beta`）、构建元数据（`+sha`）。
超过三段非法。每个数字段上限 1000000（防溢出，实际不会碰到）。

### 1.4 版本范围语法（§9 的输入）

```
比较符：>=  >  <=  <  =         （裸版本号等价于 "="）
与：    ,                       ">=0.1.0,<1.0.0"
或：    |                       ">=1.0.0 | <0.5.0"
优先级：先按 | 分组取或，组内按 , 取与
空串或全空白：不限（不推荐）
```

**明确不支持**：`^`、`~`、通配符（`*`/`x`）、区间语法（`1.0 - 2.0`）、
预发布标签比较。

> **为什么不做 `^` 与 `~`**：它们的语义在不同生态里不一致（npm 的 `^0.1.2`
> 与很多人以为的行为不同），而引入它们只是为了让 `host_range` 短几个字符。
> 写 `">=0.1.0,<1.0.0"` 多打 12 个字符，换来确定无歧义。

### 1.5 插件作者该怎么写 `host_range`

```cpp
">=0.1.0,<1.0.0"     // 推荐：本项目 0.x 阶段，宿主可能随时破坏兼容
">=1.2.0,<2.0.0"     // 宿主进入 1.x 之后
">=0.1.0"            // 宽松：知道自己只用了稳定能力时才这么写
""                   // 不限 —— 不推荐，等于放弃所有保护
```

---

## 2. 全局约定

### 2.1 字符串

所有字符串一律是 **UTF-8、NUL 结尾的 `const char*`**。

- 空字符串写 `""`；可选字段"未提供"写 `nullptr`。**两者对必填字段都算缺失。**
- 长度上限一律按**字节数**计（见 §3.2）。一个中文字符通常 3 字节，
  所以 `kMaxTitleBytes = 256` 大约是 85 个汉字。
- 不提供长度参数 —— NUL 结尾就够用，也避免了"长度与实际不符"的经典 bug。

### 2.2 跨边界规则（重复一遍，因为它是本 SDK 的地基）

1. 只传：POD 结构体、定长整型/浮点、`const char*`、`sm::Span<>`、不透明 `void*`。
2. **禁止 STL 类型**跨边界（`std::string` / `std::vector` / `std::function` /
   `std::span` / `std::string_view`）。
3. **禁止跨模块释放内存**、**禁止异常越过边界**。
4. **禁止跨 DLL `dynamic_cast` / `typeid`**。

细节与理由见 [03](01-入门.md)。

### 2.3 线程

两条线程与插件相关：**主线程**（一切 UI 与除 `start()` 外的所有回调）与
**调度线程**（只跑 `start()`）。每个方法都标注了允许的线程；
**在错误的线程调用是未定义行为**，宿主不做检测（检测本身要加锁，会破坏
"空闲时零开销"的设计目标）。

速查表见 [01-入门 §4](01-入门.md)。

### 2.4 超限的处理

宿主对超限数据的处理一律是**截断 + 记一条 Warn 日志**，而不是拒绝整条数据。

> **为什么**：一条内容 70 KiB 的通知，用户更可能希望"看到前面 64 KiB"
> 而不是"什么都没有，而且插件报错"。截断是降级，拒绝是失败。

---

## 3. `sm/types.h`

### 3.1 `sm::Span<T>`

```cpp
template <typename T>
struct Span {
  const T* data = nullptr;
  std::size_t size = 0;

  constexpr Span() = default;
  constexpr Span(const T* d, std::size_t n);
  constexpr const T* begin() const;
  constexpr const T* end() const;
  constexpr bool empty() const;
  constexpr std::size_t length() const;
  constexpr const T& operator[](std::size_t i) const;
};
```

跨边界的只读数组视图，语义是"指向一段连续数据的 (指针, 长度)"。**不拥有数据**。

| 项 | 说明 |
|---|---|
| 为什么不用 `std::span` | 它是 STL 类型（违反 §2.2 规则 2），且其 ABI 随标准库实现变化 |
| 生存期 | 由被指向的 arena 或静态存储决定，见每个使用点的注释 |
| 空视图 | `data == nullptr && size == 0`，由默认构造给出 |
| 越界 | `operator[]` **不做**边界检查；`Span` 是零开销视图，不是安全容器 |

> **注意**：`Span<T>` 的 `T` 是 `const` 修饰过的（`const T*`）。
> 但 `Notification::actions` 声明为 `Span<Action>` 而非 `Span<const Action>`
> —— 因为插件在 `start()` 里构造它，需要写入。宿主读它时只当只读用。

### 3.2 `sm::span_of(const T (&arr)[N])`

从静态数组直接构造 `Span`，专为**元信息里的静态表**设计：

```cpp
static const char* kSubs[] = { "mail.*", "sm.host.log" };
static constexpr auto kSubsSpan = sm::span_of(kSubs);
```

**只能用于真正的数组**（`T arr[N]`）。传指针进去会编译失败 —— 这正是想要的：
它让"忘记写数组长度"变成编译错误。

**元素类型怎么定**：值类型（如 `SettingOption`）在视图里是只读的，
所以加了一层 `const`；指针类型（`const char*`，即订阅模式数组）保持原样。
这条规则是为了让下面这四种"插件作者自然会写"的声明都能直接用，
而不必去猜哪种写法能过：

| 声明 | 得到的类型 |
|---|---|
| `constexpr SettingOption kOpts[]` | `Span<const SettingOption>` |
| `constexpr SettingDef kSettings[]` | `Span<const SettingDef>` |
| `static const char* kSubs[]` | `Span<const char*>` |
| `constexpr const char* kSubs[]` | `Span<const char*>` |

> 注意最后两行的结果是 `Span<const char*>`，而**不是** `Span<const char* const>`。
> 后者是"常量指针的视图"，与 `PluginMeta::subscriptions` 的声明不兼容 ——
> 直接写 `Span<const T>` 就会掉进这个坑，而且是那种"看起来很对、
> 编译不过又想不通为什么"的错误。

### 3.3 `sm::limits`

**宿主实现不得放宽这些值**（放宽会让基于它们的插件缓冲区溢出）。

#### 通知字段

| 常量 | 值 | 约束的对象 |
|---|---|---|
| `kMaxIdBytes` | 64 | `Notification::id` |
| `kMaxTitleBytes` | 256 | `Notification::title` |
| `kMaxContentBytes` | 64 * 1024 | `Notification::content` |
| `kMaxUrlBytes` | 2048 | `Notification::url` |
| `kMaxIconBytes` | 2048 | `Notification::icon` |
| `kMaxTopicBytes` | 128 | `Notification::topic` |
| `kSummaryCodepoints` | 20 | 自动生成的 `summary` 长度（**码点**，不是字节） |
| `kMaxActionsPerNotification` | 4 | `Notification::actions.size` |

#### Action

| 常量 | 值 |
|---|---|
| `kMaxActionIdBytes` | 64 |
| `kMaxActionLabelBytes` | 64 |

#### 插件元信息

| 常量 | 值 |
|---|---|
| `kMaxPluginIdBytes` | 64 |
| `kMaxPluginNameBytes` | 128 |
| `kMaxPluginAuthorBytes` | 128 |
| `kMaxPluginVersionBytes` | 32 |
| `kMaxPluginDescriptionBytes` | 1024 |
| `kMaxHostRangeBytes` | 256 |
| `kMaxSubscriptions` | 16 |

#### 设置 schema

| 常量 | 值 |
|---|---|
| `kMaxSettingItems` | 64 |
| `kMaxSettingNameBytes` | 64 |
| `kMaxSettingLabelBytes` | 128 |
| `kMaxSettingDescBytes` | 512 |
| `kMaxStringSettingBytes` | 1024 |
| `kMaxEnumOptions` | 16 |

#### 宿主运行时上限

| 常量 | 值 | 说明 |
|---|---|---|
| `kNotificationQueueCapacity` | 1000 | 待处理队列。满时丢弃**最旧**的并计数 |
| `kNotificationHistoryCapacity` | 200 | 内存历史，即 `draw_ui(history)` 能看到的条数 |

> **为什么把宿主的运行时上限也放进 SDK**：插件需要据此决定行为。
> 一个 Notifier 若想"显示最近 500 条"，得知道 `draw_ui` 最多只会给它 200 条。

### 3.4 `sm::summary.h` 里的 `utf8_*` 与 `make_summary`

见 §10。

---

## 4. `sm/version.h`

```cpp
#define SM_VERSION_MAJOR 0
#define SM_VERSION_MINOR 1
#define SM_VERSION_PATCH 0
#define SM_VERSION_STRING "0.1.0"
#define SM_VERSION_DETAIL "v0.1.0"   // git describe 的完整结果
#define SM_API_VERSION 2

namespace sm { inline constexpr std::int32_t kApiVersion = SM_API_VERSION; }
```

| 符号 | 类型 | 说明 |
|---|---|---|
| `SM_VERSION_MAJOR/MINOR/PATCH` | 整数宏 | 宿主版本的三个分量。构建时由 `cmake/SmVersion.cmake` 从 git tag 推导，经 `sm::sdk` 的接口编译定义注入；**不要手改** |
| `SM_VERSION_STRING` | 字符串宏 | 严格的 `major.minor.patch`，用于日志、插件版本范围判定与控制面板"关于"页 |
| `SM_VERSION_DETAIL` | 字符串宏 | `git describe --tags --dirty` 的完整结果（如 `v0.1.0-3-gabc1234-dirty`），只用于展示与排障，不能拿去做版本比较 |
| `SM_API_VERSION` | 整数宏 | ABI 版本。**插件元信息里必须写这个宏**，不要写字面量 |
| `sm::kApiVersion` | `constexpr std::int32_t` | 上面那个宏的类型安全同义写法 |

**版本号只有一个来源：git tag。** 发布时给提交打 `vX.Y.Z` 标签，CMake 配置阶段用
`git describe --tags --match "v[0-9]*"` 找到最近的标签，取数字部分作为宿主版本；
标签之后的提交数、短哈希与 dirty 标记只进入 `SM_VERSION_DETAIL`。
没有 git 或没有匹配标签时，才退回 `include/sm/version.h` 里的兜底宏并给出警告。

**为什么 `SM_API_VERSION` 是宏**：`PluginMeta` 是 `constexpr` 静态数据，
插件需要在 `constexpr` 上下文里使用它。

---

## 5. `sm/notification.h`

### 5.1 `sm::Level`

```cpp
enum class Level : std::int32_t {
  Debug = 0, Info = 1, Warning = 2, Error = 3, Critical = 4,
};
```

**这是通知的级别，与日志级别（`LogLevel`）是两套独立概念。**二者唯一的交汇点
是"宿主把日志事件转为通知"这一处，映射为：

| `LogLevel` | → `Level` |
|---|---|
| `Error` | `Error` |
| `Warn` | `Warning` |
| `Info` | `Info` |
| `Debug` | 永不转换 |
| `Trace` | 永不转换 |

> **为什么不让两者共用一个枚举**：它们会独立演化。日志级别关心的是
> "这段话有多重要"，通知级别关心的是"要不要打扰用户"。现在统一，
> 以后一定会出现"日志想加 `Fatal`、通知想加 `Sticky`"的岔路。

取值顺序即严重程度顺序，可以比较：`level >= Level::Warning`。

### 5.2 `sm::Action`

```cpp
struct Action {
  const char* id = nullptr;     // 上限 kMaxActionIdBytes，同一通知内唯一
  const char* label = nullptr;  // 上限 kMaxActionLabelBytes，可空 → Notifier 用 id 兜底
};
```

Action 只描述**"有什么按钮"**，不描述**"按钮长什么样"** —— 外观由具体的
Notifier 决定（imgui 浮层画成按钮，控制台 Notifier 可能直接忽略）。

**点击链路**（完整时序见 [02](../01-架构总览.md)）：

```
用户点击 Notifier 界面上的按钮
  → 宿主在主线程调用该 Notifier 的 on_action(notification, action_id)
  → Notifier 只记录状态，并调用 request_ui_surface() / set_view_title()
  → 宿主在后续帧调用该 Notifier 的 draw_view()
```

**宿主把"点了某个 Action"当成"用户看过这条通知了"**（[02-宿主设计 §8.5](../02-宿主设计.md)）：
宿主在主线程把这个动作分发出去时（**在路由之前**），就把这条通知在内存历史里
标为**已读** —— 包括
"没有 Notifier 订阅这个 topic、于是谁都没收到"的那种点击。因此 **Notifier
不需要自己维护已读状态**，也没有为此存在的接口。"已读"是宿主独占的字段，
不出现在 `Notification` 里，也不进导出的 `.jsonl`（[03-控制面板 §4.4](../03-控制面板.md)）。

**两个约定 id**（不是强制，但 Source 声明、Notifier 响应时按这个来，
彼此才能接上）：

| `id` | 含义 | 典型响应 |
|---|---|---|
| `details` | 打开详情视图，显示完整 `content` | Notifier：拷贝一份通知 → `set_view_title()` → `request_ui_surface(0)` |
| `dismiss` | 请求收起/关掉这张通知卡片 | Notifier：把它从自己的浮层队列里移除 |

随仓库发布的 `notifier.imgui` 会把这两个动作作为**浮层上的内置按钮**画出来
（见 [04-示例插件.md](04-示例插件.md)）：浮层只放得下摘要，完整内容必须有 `details`
这条入口；`dismiss` 与"在浮层上点右键"同义。通知自己已经声明了同 id 的
Action 时，它不会再画一个重复的按钮。

### 5.3 `sm::Notification`

所有 `const char*` 字段都是 UTF-8、NUL 结尾、位于**调用方 arena 内**。

#### 内容字段

| 字段 | 必填 | 上限 | 说明 |
|---|---|---|---|
| `title` | ✔ | `kMaxTitleBytes` | 标题 |
| `content` | ✔ | `kMaxContentBytes` | 完整内容。通知卡片上**不应**直接铺开它，而应展示 `summary` |
| `summary` | ✘ | — | 可留空 → 宿主用 `sm::make_summary()` 生成（§10）。插件填了则原样采用（仍做长度与空白归一化） |

#### 分类字段

| 字段 | 必填 | 说明 |
|---|---|---|
| `topic` | ✔ | 点分小写标识，如 `"mail.inbox"`。支持父级前缀匹配。`"sm."` 前缀为宿主保留 |
| `level` | ✔ | 默认 `Level::Info` |
| `time_unix_ms` | ✔ | Unix 毫秒（UTC）。宿主**不做校正**，插件必须填真实时间 |

#### 可选字段

| 字段 | 说明 |
|---|---|
| `id` | 去重键，可空。**作用域是全局的**（跨插件也是），所以建议 `"插件id:业务id"`，如 `"source.mail:unread"`。队列中已存在同 id → **原地更新**而非新增 |
| `url` | 关联链接，可空。宿主不校验合法性（但 Notifier 打开它时会走 `open_url()`，见 §6.3） |
| `icon` | 图标，可空，上限 `kMaxIconBytes`。可以是 http(s) 网络图片 URL、`file://` URL、绝对路径，或相对 `data_dir()` 的路径。宿主**不校验、不阻塞投递**；Notifier 通过 `IHostContext::icon_texture()` 异步取纹理，加载失败只是不显示（见 §6.3） |
| `actions` | 可点击项，可空。超出 `kMaxActionsPerNotification` 的部分被截断 |

#### 宿主持有字段（插件必须留空）

| 字段 | 宿主填入 |
|---|---|
| `source_plugin` | Source 的插件 id；宿主自身的通知填保留 id `"sm.host"` |
| `seq` | 单调递增序号（**从 1 开始**），进程内不重复、不倒退。Notifier 用它判断"这条是不是新到的" |

> **为什么 `seq` 从 1 开始而不是 0**：`0` 是"未初始化"的哨兵值。
> 从 1 开始，`n.seq == 0` 就明确表示"这条数据没经过宿主"（例如插件在别处
> 构造了一个 `Notification` 自用），便于排错。

> **`id` 的更新语义**：`id` 非空且队列中已有同 id 的通知时，宿主把新数据
> **覆盖**到那条上（保留其 `seq` 不变），而**不是**新增一条也不改变其位置。
> 因此"未读邮件数"这类通知不会刷屏。若希望每次都新增，就留 `id` 为空。

---

## 6. `sm/host_context.h`

### 6.1 `sm::LogLevel`

```cpp
enum class LogLevel : std::int32_t { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4 };
```

日志写入内存环形缓冲并由后台线程落盘。日志的完整格式（时间/级别/插件/线程
前缀）见 [06](../02-宿主设计.md)。

### 6.2 `sm::MainThreadTaskFn`

```cpp
using MainThreadTaskFn = void (*)(void* user);
```

`post_to_main_thread()` 会把 `(fn, user)` 排队，主线程在下一帧调用 `fn(user)`。

**硬规则**：`fn` 必须尽快返回 —— 不得阻塞、不得抛异常、不得再投递长任务。
宿主对 `fn` 的调用**包在 SEH 屏障内**，因此它崩溃会连累**投递它的那个插件**
被禁用（与直接在 `on_notify` 里崩溃等价）。

### 6.3 `sm::IHostContext`

这个对象由**宿主拥有**，在 `sm_create_plugin()` 时交给插件，生存期到
`exit()` 返回后由宿主销毁。

**插件禁止**：把 `IHostContext*` 保存到 `exit()` 之后使用；在自己的线程里
调用除 `post_to_main_thread` / `log` / `logf` / `now_unix_ms` 之外的任何方法。

#### 一、宿主信息（任意线程）

| 方法 | 返回 | 说明 |
|---|---|---|
| `const char* host_version() const` | `"0.1.0"` | 宿主版本 |
| `std::int32_t host_api_version() const` | `SM_API_VERSION` | 宿主加载时已校验过，仅供日志与排障 |
| `const char* config_dir() const` | 绝对路径 | `config.toml` 与 `plugins/` 下的插件配置在此 |
| `const char* data_dir() const` | 绝对路径 | **插件自己的状态文件放这里**，不要写进配置目录 |
| `const char* plugin_dir() const` | 绝对路径 | 插件所在目录 |
| `const char* own_config_path() const` | 绝对路径 | `config_dir/plugins/<id>.toml`。**插件不应直接读写它** —— 设置走 `setting_*()` |
| `std::int64_t now_unix_ms() const` | Unix 毫秒 | 等价于通知里的 `time_unix_ms` |
| `const char* locale() const` | `"zh-CN"` | BCP-47。当前版本恒定返回 `"zh-CN"` |

**返回的字符串在整个会话期间稳定有效**（宿主把它们放在长期存储里），
插件可以放心保存指针 —— 但**不要**保存 `IHostContext*` 本身到 `exit()` 之后。

> **为什么 `own_config_path()` 存在却又说"不要用"**：插件写日志或错误信息时
> 需要引用"你的配置在哪"，主机不可能替它拼这个路径。但**读写**必须走
> `setting_*()`，否则宿主不知道设置被改了，界面会显示过期的值。

> **`data_dir()` 与 `config_dir()` 为什么分开**：配置目录的内容会被用户
> 手工编辑、被宿主的"备份并重置"逻辑处理；数据目录不会。插件的缓存、
> 索引、上次运行状态应当放数据目录，否则用户重置配置时会莫名其妙丢掉它们。

#### 二、arena 分配器

```cpp
void* allocate(std::size_t bytes);

template <typename T> T* allocate_array(std::size_t n);
const char* allocate_string(const char* src, std::size_t max_bytes);
```

`allocate()` 返回的内存绑定到**当前这次插件回调**：

- 在 `start()` 期间分配的，`start()` 返回后宿主即回收；
- 在 `on_notify` / `on_action` / `draw_ui` / `draw_view` 期间分配的，
  该回调返回后即回收。

**分配失败返回 `nullptr`，调用方必须检查。**宿主给每次回调设了上限
（默认 4 MiB，见 [06](../02-宿主设计.md)）。

| 便捷函数 | 语义 |
|---|---|
| `allocate_array<T>(n)` | 分配 `n` 个 `T` 的连续数组。**不做构造**，`T` 必须是平凡类型。`n == 0` 返回 `nullptr` |
| `allocate_string(src, max_bytes)` | 拷贝 NUL 结尾字符串进 arena。`src == nullptr` 返回 `nullptr`。超长则**按字节截断** |

> **`allocate_string` 的截断是按字节的**，可能把一个多字节 UTF-8 序列切成
> 两半。中文内容请改用 `sm::make_summary()`（按码点安全截断），
> 或自行在码点边界截断。这是本 SDK 里唯一一处"按字节截断"可能造成
> 显示乱码的地方，因此单独标出来。

> **`allocate_string` 为什么是 inline 在头文件里**：它只依赖手写的 `strlen`
> 循环，不涉及任何 STL 容器，因此跨模块调用没有 ABI 问题；同时让 SDK 不必
> 提供导入库（`sm::sdk` 是 `INTERFACE` 目标，没有 `.lib`）。

#### 三、调度（**仅允许在 `start()` 期间调用**）

```cpp
void schedule_next(std::int64_t delay_ms);
void schedule_at(std::int64_t unix_ms);
```

| 方法 | 语义 |
|---|---|
| `schedule_next(delay_ms)` | `delay_ms` 毫秒后再调用一次 `start()` |
| `schedule_at(unix_ms)` | 在 `unix_ms` 这个时刻调用一次 `start()`。已过去则等价于 `schedule_next(0)` |

- 每次 `start()` 中重复调用，**以最后一次为准**。
- 传入非正数会被忽略并记 Warn（`schedule_at` 的"已过去"是例外，见上）。
- **宿主在每次调用 `start()` 之前会清空该插件的下次调度时刻。**
  因此 `start()` 里若不调用这两个方法中的任何一个，插件就进入**休眠**，
  直到用户在控制面板里再次启用它。

> **"不注册即休眠"是刻意的设计**：它不需要"取消调度"接口，也不需要插件在
> 退出路径上记得清理调度。插件的"我还想再跑"是一个显式的、每次都要重新
> 表达的决定 —— 漏写就是休眠（可见），而不是永远以某个频率空转（不可见）。

> **`schedule_at` 的时间基准是 UTC 毫秒**，而内部堆排序用的是单调时钟
> （见 [02](../01-架构总览.md)）。宿主在入堆时做一次换算，因此
> **系统时间被修改会影响等待中的绝对调度**（相对调度不受影响）。

#### 四、插件设置（任意线程）

```cpp
bool         setting_bool(const char* name, bool fallback) const;
std::int64_t setting_int(const char* name, std::int64_t fallback) const;
double       setting_double(const char* name, double fallback) const;
const char*  setting_string(const char* name, const char* fallback) const;
const char*  setting_enum(const char* name, const char* fallback) const;
```

按 schema 中声明的 `name` 读取**当前生效值**。

- 用户在控制面板里改动设置**立即生效**（无"应用"按钮）。因此插件应当
  **在使用时按需读取**，而**不要**在 `initialize()` 里读一次就缓存。
- `schema` 中不存在的 `name`，或类型不匹配：返回 `fallback` 并记一条 Warn。
- 返回的 `const char*` 生存期：**直到下一次同类调用或设置被修改为止**。
  需要留存请自行拷走。

> **`setting_enum` 返回的是选项的 `value` 字符串，不是索引。**
> 用它与自己的常量比较：
> ```cpp
> const char* v = host()->setting_enum("on_parse_error", "log");
> if (std::strcmp(v, "skip") == 0) { /* … */ }
> ```
> 返回索引会让"往枚举中间插一个选项"变成静默的行为改变 —— 老配置里的
> `2` 会指向另一个选项。字符串则要么匹配要么落到 fallback，不会悄悄换意思。

#### 五、日志（任意线程，线程安全）

```cpp
void log(LogLevel level, const char* message);
void logf(LogLevel level, const char* fmt, ...);
```

宿主自动为每条日志加上 `[时间][级别][插件id][线程]` 前缀与换行。
`message` **不需要也不应带**换行（内部换行会被替换为空格）。

**性能约定：这两个调用永不阻塞调用方。**日志进入有界环形缓冲，由后台写线程
批量落盘；缓冲满时丢弃最旧的记录并计数（会在日志里报告"丢弃了 N 条"）。

`logf` 是 printf 风格的，**格式串上限 1024 字节**，超出截断。

> **为什么不用 `std::format` 或 `std::string`**：那会把 STL 类型引入跨边界
> 调用（违反 §2.2 规则 2）。printf 风格是本 SDK 里唯一零 ABI 风险的
> 格式化方案 —— 所有参数都是 POD。

> **`logf` 的常见陷阱**：`%s` 传 `nullptr` 在 MSVC 上输出 `(null)`，
> 但在其他实现上可能崩溃。宿主实现应当把 `nullptr` 参数统一替换为 `"(null)"`。

#### 六、错误上报

```cpp
void set_error(const char* message);   // 仅 initialize/start/stop/exit/on_action 期间
```

供 `StartOutput::error` 之外的场合使用（例如 `initialize()` 失败、看门狗
中断后的收尾）。宿主会把它写进日志并显示在控制面板的插件状态里。
传 `nullptr` 或 `""` 表示**清除**当前错误。

#### 七、取消 / 超时（**仅允许在 `start()` 期间调用**）

```cpp
bool is_cancelled() const;
```

返回 `true` 表示宿主希望插件尽快放弃本次工作：可能是"该插件已超过看门狗
阈值"，也可能是"宿主正在退出"。

**宿主不会强制终止卡死的插件代码**（那是做不到的，只会让进程更不稳定）。
插件应当在自己的耗时循环里定期检查本函数，尽快返回。

**看到 `is_cancelled()` 为 `true` 时应当返回 `StartResult::Cancelled` 而不是
`Error`** —— 否则会被计入失败退避，拖慢恢复。

#### 八、UI 能力（**仅 Notifier；仅允许在 `on_notify` / `on_action` / `draw_ui` / `draw_view` 期间调用**）

在调度线程（`start()` 期间）调用它们是未定义行为。

```cpp
void* imgui_context();
void  request_ui_surface(std::int64_t duration_ms);
void  set_view_title(const char* title);
void  close_view();
std::uint64_t icon_texture(const char* icon, int* out_width, int* out_height,
                           bool* out_failed);
bool  invoke_action(std::int64_t notification_seq, const char* action_id);
bool  open_url(const char* url);
```

| 方法 | 说明 |
|---|---|
| `imgui_context()` | **当前这一帧画在哪张表面上的**那个 imgui 上下文，实际类型是 `ImGuiContext*`；无 UI 会话时返回 `nullptr`。宿主有**三类表面**（控制面板、通知表面、每个插件一扇的详情窗口），各有一个上下文 —— 同一个插件的 `draw_ui()`（画在通知表面上）与 `draw_view()`（画在该插件的详情窗口上）拿到的指针**不同**，所以每次回调都要重新取。**返回 `void*` 是刻意的** —— SDK 头不引入 imgui，以免强制非 UI 插件依赖它 |
| `request_ui_surface(duration_ms)` | 请求宿主为**本插件**维持一个存活 `duration_ms` 毫秒的 UI 会话。`> 0`：从现在起至少维持这么久，重复调用刷新为"从现在起"。`<= 0`：我持续需要 UI，直到显式关闭（用于详情视图）。多次调用**取最长的那个** |
| `set_view_title(title)` | 打开（或更新）本插件的详情窗口，并设置窗口标题。`nullptr`/`""` 时用"插件名" |
| `close_view()` | 关闭本插件的详情窗口并释放它持有的"持续"UI 引用。**只影响本插件** |
| `icon_texture(icon, &w, &h, &failed)` | 取通知图标的纹理（即 `ImTextureID`），供 `ImGui::Image()` 直接使用。`icon` 接受 `Notification::icon` 的任何形式。**加载是异步的**：首帧返回 0 并开始下载/解码，之后每帧重取；就绪后返回非 0 句柄，并通过 `out_width` / `out_height` 写入像素尺寸（最长边缩到 256 px）。失败返回 0 且 `*out_failed = true`，失败结果会被缓存、不会反复重试。空指针或空字符串返回 0。支持 PNG / JPEG / BMP / ICO（系统 WIC 解码）。**只在 `draw_ui()` / `draw_view()` 期间有意义** —— 它取的是当前表面的绘制资源，不是通知数据 |
| `invoke_action(seq, action_id)` | 请求宿主在**本帧绘制结束后**回调本插件的 `on_action()`。返回 `false` 表示该通知已不在内存历史里，或主线程队列已满 —— 本次点击应被安静忽略 |
| `open_url(url)` | 用系统默认程序打开链接。**只接受 http / https / mailto**，其余拒绝并记 Warn |

`imgui_context()` 的三条硬规则（先判空、不自己创建/销毁上下文、不缓存 ——
"不缓存"包括**不跨表面缓存**）见 [01-入门 §7.3](01-入门.md)。

> **为什么是"这一帧所在表面"而不是"那个会话"**：界面有三类表面（控制面板、
> 通知表面、每个插件一扇的详情窗口），各有自己的 imgui 上下文
> （[01-架构总览 §8.2](../01-架构总览.md)）。插件的回调本来就在某一帧、某一张
> 表面上被调用 —— 让接口回答这件事，插件就不需要知道"宿主的界面分几张表面"，
> 只需要在每次回调里问一次。返回"那个会话的上下文"这种说法在多表面下不再有
> 唯一的答案。

> **`invoke_action` 为什么需要存在（而不是让插件直接调自己的 `on_action`）**：
> 两个理由。① `on_action` 里可能开详情视图、改设置，那是会改变本帧正在
> 遍历的状态，属于重入；推迟到帧末可以彻底避免这一类问题。② 让"点击 Action"
> 只有一条路径 —— 无论是宿主画的通知卡片还是插件画的浮层，最终都走到同一个
> `on_action`，插件不必写两套处理。

> **`invoke_action` 为什么用 `seq` 而不是 `id`**：`id` 允许为空，
> 而 `seq` 一定存在且在进程内唯一。

> **`open_url` 为什么只允许三种协议**：一个能执行任意字符串的接口，
> 会让任何一个被安装的插件都具备执行本地命令的能力 —— `file:///C:/...exe`
> 或自定义协议的处理器都能变成任意代码执行。这不是"少写几个分支"，
> 而是把一个"装插件 = 授权本地命令执行"的设计摘出去。
> 需要打开本地文件/目录的插件，本版本不提供支持（见 [01-架构总览](../01-架构总览.md) 的“明确不做的事”一节）。

#### 九、主线程任务投递（任意线程）

```cpp
bool post_to_main_thread(MainThreadTaskFn fn, void* user);
```

把 `fn(user)` 排进主线程队列，主线程在下一帧调用。

- 返回 `false` 表示**队列已满或宿主正在退出，任务不会被执行**。
  调用方应当尊重这个返回值（例如记一条日志并丢弃结果），**而不是假定成功**。
- 队列为有界队列，宿主不提供优先级。
- 这是**自定义 UI 插件**（自己开原生窗口、自己起线程）唯一被允许从非主线程
  触碰宿主的方式，见 [01-入门 §8](01-入门.md)。
- `user` 指针的生存期**由插件负责**：宿主只负责传递，不负责释放。

---

## 7. `sm/plugin.h`

### 7.1 `sm::PluginKind`

```cpp
enum class PluginKind : std::int32_t { Source = 0, Notifier = 1 };
```

| 值 | 含义 |
|---|---|
| `Source` | 调度子项插件：数据的来源。宿主按调度循环调用它的 `start()` |
| `Notifier` | UI/通知插件：通知的出口。宿主把匹配它订阅的通知交给它 |

两类插件**必须继承同一个基类 `IPlugin`**，靠 `kind` 区分行为。宿主对两类
插件的加载、版本校验、隔离、启停、异常处理走**完全相同**的代码路径。

### 7.2 `sm::StartResult`

```cpp
enum class StartResult : std::int32_t { Ok = 0, Empty = 1, Error = 2, Abort = 3, Cancelled = 4 };
```

| 值 | 含义 | 宿主行为 |
|---|---|---|
| `Ok` | 本次成功**且有数据**。`items` 必须非空 | 处理数据，按插件注册的时间继续调度 |
| `Empty` | 本次成功但**没有新数据**。这是常态 | 按插件注册的时间正常继续调度 |
| `Error` | 本次失败（网络不可达、文件被占用……）。`error` 应写明原因 | **退避重试**，见下 |
| `Abort` | 插件自报"我处于不可恢复的错误状态" | **立即**停止调度并禁用该插件（持久化到配置） |
| `Cancelled` | 宿主取消了本次执行（看门狗超时，或宿主正在退出） | 不计入失败 |

**关键规则：`Error` 不等于"禁用"。**宿主对 `Error` 的处理是**指数退避重试**
（本次间隔 ×2，上限 30 分钟），只有**连续**失败达到阈值（默认 5 次）
或收到 `Abort` 时才禁用该插件。

> **为什么这条要写死**：不这样规定，实现者很容易把"一次网络抖动"变成
> "插件被永久禁用"。用户第二天打开电脑，发现邮件插件不工作了，
> 而原因是昨晚路由器重启了 3 秒。退避 + 阈值让偶发失败无害、持续失败
> 才升级为"需要人介入"。

退避表见 [02](../01-架构总览.md) 的"调度循环"一节。

### 7.3 设置项 schema

```cpp
enum class SettingType : std::int32_t { Bool = 0, Int = 1, Double = 2, String = 3, Enum = 4 };

struct SettingOption {
  const char* value = nullptr;  // 存进配置的值，建议小写短标识，如 "log"
  const char* label = nullptr;  // 界面显示，UTF-8，如 "写入日志"
};

struct SettingDef { /* 见下 */ };
```

设计要点：插件**声明**设置项，宿主自动生成 imgui 表单、自动读写 TOML、
自动校验。插件因此**完全不需要碰 UI 代码与文件 IO**。

**恰好支持五种类型。**刻意不支持 string-list / 多行文本 / 路径选择器 / 颜色：
每多一种类型，宿主的表单生成、TOML 读写、校验都要多一条分支，
而插件设置几乎用不到它们。

`SettingDef` 实例**必须是静态存储期**（推荐 `constexpr` 数组），
宿主持有它们的指针直到插件被卸载。

#### `SettingDef` 字段

| 字段 | 类型 | 说明 |
|---|---|---|
| `name` | `const char*` | TOML 中的键名。只允许 `[a-z0-9_]`，≤ `kMaxSettingNameBytes`。**不得重名**（重名会被宿主拒绝加载，不是静默覆盖）。**不得为 `schema_version`** |
| `label` | `const char*` | 界面显示名，可空（为空用 `name`） |
| `description` | `const char*` | 说明，可空。界面上以 tooltip 展示 |
| `type` | `SettingType` | 决定下面哪些字段生效 |
| `requires_restart` | `bool` | `true` 时控制面板会在该项后标注"（重启后生效）"。**这只是给用户的提示：宿主不会因为设置变化而重启插件** |
| `default_bool` | `bool` | `Bool` 的默认值 |
| `default_int` | `std::int64_t` | `Int` 的默认值 |
| `default_double` | `double` | `Double` 的默认值 |
| `default_string` | `const char*` | `String` / `Enum` 的默认值 |
| `min_int` / `max_int` | `std::int64_t` | `Int` 的范围 |
| `min_double` / `max_double` | `double` | `Double` 的范围 |
| `max_string_bytes` | `std::size_t` | `String` 的最长字节数。`0` 表示用 `kMaxStringSettingBytes` |
| `options` | `Span<const SettingOption>` | **仅 Enum**。为空或 `size == 0` 的枚举项会被宿主**拒绝加载** |

> **`requires_restart` 为什么只是提示**：如果宿主真的"因为某个设置变了
> 就重启插件"，那插件作者就必须保证"重启是安全的"（不能丢数据、
> 不能重复发通知），而这个保证很难做。改成提示之后，插件作者自己决定
> 何时重新初始化内部状态，宿主只负责把这件事告诉用户。

#### `sm::setting` 工厂函数

让 schema 声明保持紧凑（全部 `constexpr`）：

```cpp
namespace sm::setting {

constexpr SettingDef boolean(const char* name, bool def, const char* label,
                             const char* description = nullptr,
                             bool requires_restart = false);

constexpr SettingDef integer(const char* name, std::int64_t def, std::int64_t min,
                             std::int64_t max, const char* label,
                             const char* description = nullptr,
                             bool requires_restart = false);

constexpr SettingDef number(const char* name, double def, double min, double max,
                            const char* label, const char* description = nullptr,
                            bool requires_restart = false);

constexpr SettingDef text(const char* name, const char* def, std::size_t max_bytes,
                          const char* label, const char* description = nullptr,
                          bool requires_restart = false);

constexpr SettingDef enumeration(const char* name, const char* def,
                                 Span<const SettingOption> options, const char* label,
                                 const char* description = nullptr,
                                 bool requires_restart = false);
}
```

完整示例：

```cpp
static constexpr sm::SettingOption kModes[] = {
  { "log",   "写入日志" },
  { "toast", "弹出浮层" },
};

static constexpr sm::SettingDef kSettings[] = {
  sm::setting::integer("interval_sec", 300, 10, 86400, "轮询间隔（秒）"),
  sm::setting::enumeration("render_mode", "toast", sm::span_of(kModes), "呈现方式"),
  sm::setting::boolean("include_subdirs", false, "包含子目录"),
};
```

### 7.4 `sm::PluginMeta`

```cpp
struct PluginMeta {
  const char* id = nullptr;
  const char* name = nullptr;
  const char* author = nullptr;
  const char* version = nullptr;
  const char* description = nullptr;
  PluginKind kind = PluginKind::Source;
  const char* host_range = nullptr;
  Span<const char*> subscriptions{};
  Span<const SettingDef> settings{};
  std::int32_t api_version = 0;
};
```

**硬规则**：`PluginMeta` 实例必须有**静态存储期**（推荐 `constexpr`），
并且在 DLL 被加载的整个期间保持不变。宿主通过 `sm_plugin_meta()` 在
**创建插件实例之前**读取它。

| 字段 | 必填 | 说明 |
|---|---|---|
| `id` | ✔ | 全局唯一标识。只允许 `[a-z0-9_.-]`，须以类型前缀开头：`"source.mail"`、`"notifier.imgui"`。≤ `kMaxPluginIdBytes`。**不得以 `"sm."` 开头**（宿主保留命名空间）。它同时是日志前缀、配置文件名（`<id>.toml`）与路由键，**一旦发布不得更改** |
| `name` | ✘ | 显示名，为空用 `id` |
| `author` | ✘ | 作者 |
| `version` | ✔ | 插件自身版本，如 `"1.2.0"` |
| `description` | ✘ | 描述 |
| `kind` | ✔ | 插件类别 |
| `host_range` | ✔ | 要求的宿主版本范围。语法见 §1.4。空串视为"不限"（不推荐） |
| `subscriptions` | ✘ | 订阅的 topic 模式。**仅 Notifier 有意义**。见下 |
| `settings` | ✘ | 设置项 schema。`size == 0` 表示无可配置项 |
| `api_version` | ✔ | **必须写 `SM_API_VERSION`**。宿主会与 `sm_abi_version()` 交叉校验 |

#### `subscriptions` 的匹配规则

- `"mail.inbox"`（精确）与 `"mail.*"`（父级前缀，匹配 `mail` 下的**任意层级**）两种形式。
- `size == 0`（空）表示**订阅全部** —— 这是刻意的默认值，让简单的 Notifier
  （如归档插件）不必枚举 topic。
- 个数 ≤ `kMaxSubscriptions`。超出被截断并记 Warn。
- 匹配按**最长前缀优先**。同一通知**可以命中多个 Notifier** —— 这是特性：
  既弹浮层又归档，是正常用法。

> **为什么"空 = 全部"而不是"空 = 什么都不订阅"**：一个订阅为空的 Notifier
> 是彻底无用且难以排查的状态（它存在、已加载、已启用，但永远收不到东西）。
> 让空等于"全部"，就把这个"看似配置好了其实没配"的陷阱消掉了。

### 7.5 `sm::StartOutput`

```cpp
struct StartOutput {
  Span<const Notification> items{};
  const char* error = nullptr;
};
```

| 字段 | 说明 |
|---|---|
| `items` | 本次产生的通知数组，**必须在 `IHostContext` 的 arena 里**。返回 `Ok` 时 `size` 必须 ≥ 1；其他返回值时宿主忽略它 |
| `error` | 失败原因（UTF-8，arena 内）。`Error` 时应当填写，会写进日志并显示在插件状态里 |

### 7.6 `sm::IPlugin`

两类插件共同的基类。**线程标注是硬约束**，在错误的线程调用是未定义行为。

宿主对每一次跨边界的插件调用都包了 **SEH 屏障**。插件内部**禁止**使用 SEH
自行吞掉访问违例 —— 那会让宿主误以为一切正常。允许使用 C++ 异常并在方法
内部处理完，但**异常禁止越过边界**。

| 方法 | 线程 | 必填覆盖情况 |
|---|---|---|
| `const PluginMeta& meta() const` | 任意 | `PluginBase` 已实现 |
| `bool initialize()` | **主线程** | 可选 |
| `StartResult start(StartOutput*)` | **调度线程** | Source 必填 |
| `void stop()` | 主线程 | 可选 |
| `void exit()` | 主线程 | 可选 |
| `void on_notify(const Notification&)` | 主线程 | Notifier 必填 |
| `void on_action(const Notification&, const char* action_id)` | 主线程 | 可选 |
| `void draw_ui(Span<const Notification> history)` | 主线程 | 可选 |
| `void draw_view()` | 主线程 | 可选 |

#### `virtual ~IPlugin()`

虚析构函数是**必需的**：宿主通过 `sm_destroy_plugin()` 在**插件自己的模块内**
执行 `delete`，从而保证 `new`/`delete` 落在同一个 CRT 里。

#### `meta()`

必须返回**静态存储期**的引用。任意线程可调用，但实际只在加载阶段调用一次。

#### `initialize()`

在**主线程**调用。时机：插件被加载之后、**第一次 `start()` 之前**。
也用于"从被禁用状态重新启用"的场景 —— 宿主会先调用 `initialize()`。

| | |
|---|---|
| **适合**做的事 | 申请长期资源、读取并校验设置、建立内部数据结构 |
| **不适合**做的事 | 任何可能长时间阻塞的 IO（会卡住主线程与界面）。需要预热的网络请求请放到第一次 `start()` 里 |

返回 `false` 表示初始化失败。宿主会读 `IHostContext::set_error()` 设置的原因
写进日志，并**不调用 `start()`**、直接进入"初始化失败"状态。

**`initialize` 失败不自动禁用插件**：用户可以改完设置后手动重试。

#### `start(StartOutput*)`

一次调度执行（**仅 Source 有意义**）。在**调度线程**调用。

- 宿主在每次调用前会**清空**该插件的下次调度时刻。因此不调用
  `schedule_next()` / `schedule_at()` 就会进入**休眠**，
  直到用户手动重新启用。
- **禁止阻塞式等待**，禁止做没有超时的网络/磁盘 IO。宿主无法强制终止卡死的
  代码，只会超过看门狗阈值后**标记该插件无响应并停止调度它**。
  请在耗时循环里定期检查 `is_cancelled()`。
- 返回 `Ok` 时 `out->items.size >= 1` 必须成立；否则宿主记一条 Warn 并按
  `Empty` 处理。

#### `stop()`

在**主线程**调用。时机："插件被禁用"或"宿主即将退出（`exit()` 之前）"。

应当停止所有内部活动、释放长期资源，但**不需要摧毁插件对象本身** ——
之后可能还会被重新 `initialize()` 并启用。

**保证最多被调用一次**（对每一次启用而言），且必定在 `exit()` 之前。
因此实现上应当容忍"没 `start()` 过就 `stop()`"（用标志位判断）。

#### `exit()`

在**主线程**调用，且在插件被卸载（`FreeLibrary`）之前**保证**调用。

> **硬规则**：本方法返回之前，插件必须销毁它创建的一切 —— 线程、窗口、
> 定时器、句柄、其它 DLL 的引用、以及所有**非 arena** 的内存。宿主在
> `exit()` 返回后就会 `FreeLibrary` 本模块；任何存活的线程或窗口过程都会在
> 卸载后指向已释放的代码段，导致**必然的崩溃**。这是本 SDK 最容易踩、
> 后果最严重的一条规则。

本方法中**允许**使用 `IHostContext`（例如写最后一条日志），但宿主可能在它
返回后立即销毁上下文。

#### `on_notify(const Notification& n)`

收到一条通知（**仅 Notifier**）。在**主线程**调用，时机为通知进入管线并
匹配到本插件之后。

- **禁止阻塞**：这里是 UI 线程。
- 想弹出浮层的话，在这里调用 `request_ui_surface(5000)`；之后宿主会在
  浮层存活期间每帧调用 `draw_ui()`。
- `n` 的生存期**仅限本次调用**，需要留存请拷贝（见 [01-入门 §5.2](01-入门.md)）。

#### `on_action(const Notification& n, const char* action_id)`

用户点击了某条通知上的某个 Action。在**主线程**调用。

典型实现：

```cpp
void MyNotifier::on_action(const sm::Notification& n, const char* action_id) {
  if (std::strcmp(action_id, "details") == 0) {
    m_detail = CopyOf(n);              // 自己拷一份，见 01-入门 §5.2
    host()->set_view_title(n.title);
    host()->request_ui_surface(0);     // 持续持有，直到 close_view()
  }
}
```

宿主随后会在每一帧调用 `draw_view()`。

- **禁止阻塞、禁止同步 IO**。需要取完整内容的话，应当在 `start()` 时就把完整
  `content` 放进通知里；确实需要按需拉取的，请投递到自己的线程并立刻返回，
  取到结果后再 `post_to_main_thread()`。
- `n` 的生存期**仅限本次调用**；`action_id` 是通知里声明的 Action `id`。

#### `draw_ui(Span<const Notification> history)`

绘制本插件的**常驻界面**。在**主线程**调用，且**仅当**本插件持有活跃的
UI 表面时（也就是它调用过 `request_ui_surface()` 且尚未到期）才会被调用。

- 用 `imgui_context()` 拿到上下文，创建自己的 imgui 窗口
  （典型用法是一个右下角的无边框浮层）。
- **什么都不画也是合法的** —— 例如只想要"延长 UI 会话"但没有可绘制的界面。
- `history` 是宿主内存历史的**只读视图**（最近在前，**已按本插件的订阅过滤**），
  生存期**仅限本次调用**。用 `n.seq` 判断哪些是"新到的"。

#### `draw_view()`

绘制本插件的**详情视图**。在**主线程**调用，且**仅当**本插件调用了
`set_view_title()` 且尚未 `close_view()` 时才会被调用。

视图的承载形式**由宿主决定**：宿主为该插件新建一扇独立窗口（有标题栏、
可缩放），窗口里只画本插件的 `draw_view()`。打开详情**不会**打开控制面板，
关掉控制面板也**不会**关掉详情窗口。插件只管从这块表面的左上角开始画内容，
**不要创建自己的顶层窗口**。

### 7.7 `sm::PluginBase`

可选基类，为所有方法提供安全的空实现：

```cpp
class PluginBase : public IPlugin {
 public:
  PluginBase(IHostContext* host, const PluginMeta& meta);
  // 全部虚函数已实现（initialize 返回 true，start 返回 Empty，其余为空）
 protected:
  IHostContext* host() const;
  const PluginMeta& plugin_meta() const;
};
```

**强烈建议继承它而不是直接实现 `IPlugin`** —— 插件只需覆盖自己关心的那几个
方法。`PluginBase` 不做任何超出"空实现"的事情，也不持有任何资源，
因此不覆盖它的生命周期方法也是安全的。

注意 `PluginBase` 的默认 `start()` 会把 `out->items` 置空并返回 `Empty`，
**不会**调用 `schedule_next()` —— 所以一个只继承 `PluginBase` 的 Source
只跑一次就休眠。这是正确且安全的默认行为。

---

## 8. `sm/export.h`

### 8.1 四个导出符号

| 符号 | 签名 | 用途 |
|---|---|---|
| `sm_abi_version` | `std::int32_t()` | ABI 版本。不匹配 → 立刻拒绝，不再调用其它 |
| `sm_plugin_meta` | `const sm::PluginMeta*()` | 元信息。校验 id / host_range / schema / 订阅 |
| `sm_create_plugin` | `sm::IPlugin*(sm::IHostContext*)` | 创建实例 |
| `sm_destroy_plugin` | `void(sm::IPlugin*)` | 销毁实例（**在插件自己的模块内 `delete`**） |

**前两个符号是无状态的**：宿主可以在不创建插件对象的前提下拿到元信息，
因此"版本不符的插件"从不曾被实例化过。

### 8.2 `SM_PLUGIN_EXPORT`

```cpp
#if defined(_WIN32) || defined(_WIN64)
#define SM_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define SM_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif
```

`extern "C"` 是必需的 —— 否则符号名会被 C++ 的名字修饰（name mangling）
改变，宿主就按名字找不到它们，而且修饰规则恰好是跨工具集最不稳定的东西。

### 8.3 `SM_DECLARE_PLUGIN_EXPORTS()`

声明四个符号。插件不直接用它，用下面的宏。

### 8.4 `SM_DEFINE_PLUGIN(MetaSymbol, PluginClass)`

```cpp
#define SM_DEFINE_PLUGIN(MetaSymbol, PluginClass)                             \
  SM_PLUGIN_EXPORT std::int32_t sm_abi_version() { return SM_API_VERSION; }   \
  SM_PLUGIN_EXPORT const sm::PluginMeta* sm_plugin_meta() { return &(MetaSymbol); } \
  SM_PLUGIN_EXPORT sm::IPlugin* sm_create_plugin(sm::IHostContext* host) {    \
    return new PluginClass(host);                                             \
  }                                                                           \
  SM_PLUGIN_EXPORT void sm_destroy_plugin(sm::IPlugin* plugin) { delete plugin; }
```

用法：

```cpp
namespace {
constexpr sm::PluginMeta kMeta = { /* … */ };
class MySource final : public sm::PluginBase { /* … */ };
}

SM_DEFINE_PLUGIN(kMeta, MySource)   // 两个参数：元信息符号 + 插件类
```

在**命名空间作用域**使用，**不要放在 `namespace` 里**（`extern "C"` 符号必须
在全局作用域）。

**为什么元信息要显式传两遍**：`PluginBase` 构造时会接收 `kMeta` 供实例使用，
而 `sm_plugin_meta()` 是**静态**导出函数，它在没有实例的情况下就要能返回
元信息（这正是 §1.1 "ABI 不匹配的插件从不被实例化"的前提）。因此元信息符号
必须再作为宏参数传一次。写成宏参数而不是让宏去猜，是为了让元信息可以放在
匿名命名空间里而不会产生链接冲突。

---

## 9. `sm/version_range.h`

全部为 **header-only 的 inline 函数**：无需链接额外的库，也不引入任何 STL
类型跨边界。

### 9.1 `sm::Version`

```cpp
struct Version { std::int32_t major = 0; std::int32_t minor = 0; std::int32_t patch = 0; };
```

### 9.2 `parse_version`

```cpp
inline bool parse_version(const char* text, Version* out);
```

解析版本号字符串。成功返回 `true` 并写入 `*out`；失败返回 `false`
（**`*out` 不变**）。语法见 §1.3。

### 9.3 `compare_version`

```cpp
inline int compare_version(const Version& a, const Version& b);
```

三路比较：`a < b` 返回 `-1`，`a == b` 返回 `0`，`a > b` 返回 `1`。

### 9.4 `is_valid_range`

```cpp
inline bool is_valid_range(const char* range);
```

判断范围表达式是否合法。`nullptr` 返回 `false`；空串或全空白返回 `true`（不限）。

控制面板与加载校验都会用它，以便在插件写错语法时给出**"范围表达式非法"**
这种明确原因，而不是含糊的"版本不兼容"。

### 9.5 `version_satisfies`

```cpp
inline bool version_satisfies(const char* version, const char* range,
                              const char** error_out = nullptr);
```

| 参数 | 说明 |
|---|---|
| `version` | 待检查的版本号字符串（如宿主版本 `"0.1.0"`） |
| `range` | 范围表达式 |
| `error_out` | 可选。失败时写入一条**静态存储期**的原因字符串（**无需释放**）。可传 `nullptr` |

返回 `false` 的三类原因，`error_out` 会区分它们：

| `*error_out` | 含义 |
|---|---|
| `"版本号格式非法"` | `version` 解析失败 |
| `"范围表达式为空指针"` | `range == nullptr` |
| `"范围表达式语法非法"` | `range` 语法错误 |
| `"宿主版本不在插件声明的范围内"` | 解析都成功但确实不满足 |

**实现细节（可能影响边界行为，实现者需知）**：

- 空组视为不限。`"1.0|"` 与 `"1.0"` 行为相同（都满足）。这是宽容处理，
  不是 bug —— 一个尾部多写的 `|` 不该让插件加载失败。
- 版本段的数字上限为 1000000，超过即被截断到该值（防溢出）。
- 比较项的版本号缓冲为 64 字节，超长的项被判为语法错误。

---

## 10. `sm/summary.h`

### 10.1 UTF-8 工具

```cpp
inline std::size_t utf8_sequence_length(unsigned char lead);
inline std::size_t utf8_codepoint_count(const char* s);
inline std::size_t utf8_next(const char* s, std::size_t i);
```

| 函数 | 语义 |
|---|---|
| `utf8_sequence_length(lead)` | 返回 lead 字节开头的 UTF-8 序列长度（1..4）。**非法首字节按 1 处理** —— 目的是"永不越界"，而不是"校验合法性" |
| `utf8_codepoint_count(s)` | 统计码点个数（不含结尾 NUL）。**不校验合法性**。`nullptr` 返回 0 |
| `utf8_next(s, i)` | 把 `i` 前进到下一个码点边界。输入被截断时停在 NUL 上 |

**这三个函数都不做合法性校验**，只保证不越界。这是刻意的：SDK 的这一层
不该变成 UTF-8 验证器，否则每个调用点都要处理"输入非法怎么办"这条分支。

### 10.2 `make_summary`

```cpp
inline std::size_t make_summary(const char* src, std::size_t max_codepoints,
                                char* dst, std::size_t dst_capacity);
```

**规则**（与 `Notification::summary` 的文档必须一致）：

1. **归一化空白**：把连续的空白字符（空格/制表/换行/回车/换页/垂直制表）
   折叠成一个半角空格；去掉首尾空白。**只处理 ASCII 空白**，
   全角空格等按普通字符保留。
2. 取前 `max_codepoints` 个 Unicode 码点。**折叠出来的空格不计入配额**
   —— 否则"前 20 个字"会被空白偷走几个。
3. **绝不切断 UTF-8 字节序列。**
4. 若后续仍有非空白内容，在末尾追加 `U+2026`（`…`，3 字节）。
5. 结果总长不超过 `dst_capacity - 1` 字节，并以 NUL 结尾。

| 参数 | 说明 |
|---|---|
| `src` | 源文本（UTF-8）。`nullptr` 视为空串 |
| `max_codepoints` | 码点配额。传 `kSummaryCodepoints` 得到默认行为 |
| `dst` | 目标缓冲。**不得为 `nullptr`** |
| `dst_capacity` | 目标缓冲容量（**字节，含结尾 NUL**）。为 0 时不做任何事 |

**返回**：写入 `dst` 的字节数（**不含**结尾 NUL）。

**为什么它是 SDK 的公开函数而不是宿主内部实现**："简略内容怎么生成"是一个
会被反复问到的问题 —— 插件作者会想知道为什么自己的中文标题被截成了 6 个
字符。把它做成公开函数，答案就只有一处，且可被测试。

**为什么不做"首行优先"**：曾经考虑过"有换行就取首行"，但那会让同一个插件在
不同内容下产生风格不一致的摘要（有时是标题、有时是半句话），而用户对通知的
第一印象正是这行字。**规则简单可预测，比"聪明"更重要。**

#### 示例

```cpp
char buf[128];
sm::make_summary("  你好\n\n世界   这是很长的内容…", sm::limits::kSummaryCodepoints,
                 buf, sizeof(buf));
// buf == "你好 世界 这是很长的内容…"（若未超出 20 个码点则不含 "…"）
```

---

## 11. 符号索引

一页速查。括号内是本文小节。

### 类型

`sm::Span<T>` (§3.1) · `sm::Version` (§9.1) · `sm::Level` (§5.1) ·
`sm::Action` (§5.2) · `sm::Notification` (§5.3) · `sm::LogLevel` (§6.1) ·
`sm::MainThreadTaskFn` (§6.2) · `sm::IHostContext` (§6.3) ·
`sm::PluginKind` (§7.1) · `sm::StartResult` (§7.2) · `sm::SettingType` (§7.3) ·
`sm::SettingOption` (§7.3) · `sm::SettingDef` (§7.3) · `sm::PluginMeta` (§7.4) ·
`sm::StartOutput` (§7.5) · `sm::IPlugin` (§7.6) · `sm::PluginBase` (§7.7)

### 函数

**`sm/types.h`**：`span_of` (§3.2)

**`sm/version_range.h`**：`parse_version` (§9.2) · `compare_version` (§9.3) ·
`is_valid_range` (§9.4) · `version_satisfies` (§9.5)

**`sm/summary.h`**：`utf8_sequence_length` (§10.1) · `utf8_codepoint_count` (§10.1) ·
`utf8_next` (§10.1) · `make_summary` (§10.2)

**`sm/plugin.h` 的 `sm::setting::`**：`boolean` · `integer` · `number` ·
`text` · `enumeration` (§7.3)

### 常量

`SM_VERSION_MAJOR` / `SM_VERSION_MINOR` / `SM_VERSION_PATCH` /
`SM_VERSION_STRING` / `SM_VERSION_DETAIL` / `SM_API_VERSION` / `sm::kApiVersion` (§4)

`sm::limits::*` (§3.3)

### 宏

`SM_PLUGIN_EXPORT` (§8.2) · `SM_DECLARE_PLUGIN_EXPORTS()` (§8.3) ·
`SM_DEFINE_PLUGIN(MetaSymbol, PluginClass)` (§8.4)

### 导出符号

`sm_abi_version` · `sm_plugin_meta` · `sm_create_plugin` · `sm_destroy_plugin` (§8.1)
