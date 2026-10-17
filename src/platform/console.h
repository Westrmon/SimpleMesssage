// SimpleMessage — 平台层：控制台输出
#pragma once

namespace sm {
namespace platform {
namespace console {

// 确保进程有一个可写的控制台：优先沿用父进程的，其次新分配一个。
// 结果会被缓存（成功或失败都一样），因此重复调用不会反复附加。
bool EnsureOutput();

// 控制台是否已经就绪（只读缓存，不会触发附加/分配）。
bool available();

// 写一段 UTF-8 文本到标准输出或标准错误；控制台尚未就绪时会先尝试 EnsureOutput()。
void Write(bool is_error, const char* utf8_text);

}  // namespace console
}  // namespace platform
}  // namespace sm
