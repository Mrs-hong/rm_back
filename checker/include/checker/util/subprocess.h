// =============================================================================
// subprocess.h — 极简子进程封装（popen）
//
// 仅用于运行 ping 等诊断命令并捕获 stdout/exit code。
// 不提供输入管道与超时（调用方用 ping 自带 -W 控制）。
// =============================================================================
#pragma once

#include <string>

namespace checker {

struct SubprocessResult {
  int exit_code = -1;       // 进程退出码
  std::string out;          // 合并的 stdout/stderr
};

// 运行命令，返回退出码与输出。command 为 shell 字符串。
SubprocessResult run_subprocess(const std::string& command);

}  // namespace checker
