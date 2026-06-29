// =============================================================================
// subprocess.cpp — popen 封装
// =============================================================================
#include "checker/util/subprocess.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace checker {

SubprocessResult run_subprocess(const std::string& command) {
  SubprocessResult r;
  // 2>&1 合并 stderr，便于诊断
  std::string cmd = command + " 2>&1";
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    r.exit_code = -1;
    return r;
  }
  std::array<char, 4096> buf{};
  while (true) {
    size_t n = std::fread(buf.data(), 1, buf.size(), pipe);
    if (n == 0) break;
    r.out.append(buf.data(), n);
    if (r.out.size() > 1 << 16) break;  // 防止异常输出占用过多内存
  }
  int status = pclose(pipe);
  r.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return r;
}

}  // namespace checker
