/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/cmd_execute.h"

#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "qifeng_framework/common/logger.h"

namespace qifeng::scm {

    namespace {

        /**
         * @brief 构建 execvp 所需的 argv 数组（以 nullptr 结尾）
         */
        std::vector<char*> BuildArgv(const std::string &program, const std::vector<std::string> &args) {
            std::vector<char*> argv;
            argv.reserve(args.size() + 2);
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
            argv.push_back(const_cast<char*>(program.c_str()));
            for (const auto &a : args) {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
                argv.push_back(const_cast<char*>(a.c_str()));
            }
            argv.push_back(nullptr);
            return argv;
        }

        /**
         * @brief 从管道读取输出，限制最大字节数
         */
        std::string ReadOutput(int fd, size_t max_output) {
            std::string result;
            std::array<char, 4096> buf {};
            while (true) {
                ssize_t n = ::read(fd, buf.data(), buf.size());
                if (n <= 0) {
                    break;
                }
                result.append(buf.data(), static_cast<size_t>(n));
                if (result.size() >= max_output) {
                    result.resize(max_output);
                    break;
                }
            }
            return result;
        }

        /**
         * @brief 默认结果解析器：退出码 0 为成功，超时或非零退出码为失败
         */
        ResultMsg DefaultParser(const CmdResult &result) {
            if (result.timed_out) {
                return MakeError("command timed out");
            }
            if (result.exit_code != 0) {
                return MakeResult(result.exit_code, result.output.empty()
                                                        ? "exit code " + std::to_string(result.exit_code)
                                                        : result.output);
            }
            return MakeResult(0, result.output);
        }

    }  // namespace

    // NOLINTNEXTLINE(readability-function-size,readability-function-cognitive-complexity)
    CmdResult RunCmd(const std::string &program, const std::vector<std::string> &args, const CmdOptions &options) {
        CmdResult result;

        // 创建管道用于捕获子进程 stdout+stderr
        std::array<int, 2> pipefd = {-1, -1};
        if (::pipe2(pipefd.data(), O_CLOEXEC) != 0) {
            SLOG_ERROR << "RunCmd: pipe2() failed: " << std::strerror(errno);
            result.exit_code = -1;
            return result;
        }

        pid_t pid = ::fork();
        if (pid < 0) {
            // fork 失败
            SLOG_ERROR << "RunCmd: fork() failed: " << std::strerror(errno);
            ::close(pipefd[0]);
            ::close(pipefd[1]);
            result.exit_code = -1;
            return result;
        }

        if (pid == 0) {
            // ---- 子进程 ----
            ::close(pipefd[0]);  // 关闭读端

            // 将 stdout 和 stderr 重定向到管道写端
            ::dup2(pipefd[1], STDOUT_FILENO);
            ::dup2(pipefd[1], STDERR_FILENO);
            if (pipefd[1] != STDOUT_FILENO && pipefd[1] != STDERR_FILENO) {
                ::close(pipefd[1]);
            }

            // 创建新进程组，便于超时时 kill 整个组
            ::setpgid(0, 0);

            // 重置信号处理（避免继承父进程的信号屏蔽）
            ::signal(SIGPIPE, SIG_DFL);

            auto argv = BuildArgv(program, args);
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            ::execvp(argv[0], argv.data());

            // execvp 仅在失败时返回
            // 写入错误信息到 stderr（已重定向到管道）
            const char* err = std::strerror(errno);
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
            ::fprintf(stderr, "execvp(%s) failed: %s\n", argv[0], err);
            ::_exit(127);
        }

        // ---- 父进程 ----
        ::close(pipefd[1]);  // 关闭写端

        // 读取子进程输出
        result.output = ReadOutput(pipefd[0], options.max_output);
        ::close(pipefd[0]);

        // 等待子进程退出（支持超时）
        if (options.timeout_ms > 0) {
            // 使用非阻塞 waitpid + 轮询实现超时
            int elapsed = 0;
            const int pollIntervalMs = 50;
            while (true) {
                int status = 0;
                pid_t ret = ::waitpid(pid, &status, WNOHANG);
                if (ret == pid) {
                    // 子进程已退出
                    if (WIFEXITED(status)) {
                        result.exit_code = WEXITSTATUS(status);
                    } else if (WIFSIGNALED(status)) {
                        result.exit_code = -1;
                    }
                    return result;
                }
                if (ret < 0) {
                    SLOG_ERROR << "RunCmd: waitpid() error: " << std::strerror(errno);
                    result.exit_code = -1;
                    return result;
                }

                // ret == 0: 子进程仍在运行
                elapsed += pollIntervalMs;
                if (elapsed >= options.timeout_ms) {
                    // 超时：向整个进程组发送 SIGKILL
                    ::kill(-pid, SIGKILL);
                    // 回收子进程，防止僵尸进程
                    ::waitpid(pid, nullptr, 0);
                    result.timed_out = true;
                    result.exit_code = -1;
                    return result;
                }
                ::usleep(static_cast<useconds_t>(pollIntervalMs) * 1000);
            }
        } else {
            // 无超时：阻塞等待
            int status = 0;
            if (::waitpid(pid, &status, 0) == pid) {
                if (WIFEXITED(status)) {
                    result.exit_code = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    result.exit_code = -1;
                }
            } else {
                SLOG_ERROR << "RunCmd: waitpid() error: " << std::strerror(errno);
                result.exit_code = -1;
            }
        }

        return result;
    }

    ResultMsg ExecuteCmd(const std::string &program, const std::vector<std::string> &args, const CmdOptions &options,
                         const CmdResultParser &parser) {
        CmdResult result = RunCmd(program, args, options);
        return parser ? parser(result) : DefaultParser(result);
    }

}  // namespace qifeng::scm
