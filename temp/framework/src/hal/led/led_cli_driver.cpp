/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "hal/led/led_cli_driver.h"

#include "common/logger.h"

#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace qifeng {
    namespace {
        constexpr const char* ActionMute = "mute";
        constexpr const char* ActionUnmute = "unmute";

        bool ColorToAction(LedColor color, std::string& action) {
            MicMuteStatus status;
            if (!LedColorToMuteStatus(color, status)) {
                return false;
            }
            action = (status == MicMuteStatus::Mute) ? ActionMute : ActionUnmute;
            return true;
        }
    }  // namespace

    CliLedDriver::CliLedDriver(const CliLedConfig& config) : mConfig(config) {
    }

    CliLedDriver::~CliLedDriver() {
        Release();
    }

    bool CliLedDriver::Init() {
        if (mInitialized) {
            SLOG_WARN << "CliLedDriver already initialized";
            return true;
        }

        std::error_code ec;
        if (!std::filesystem::exists(mConfig.tool_path, ec)) {
            SLOG_ERROR << "cli tool not found at " << mConfig.tool_path;
            return false;
        }

        using std::filesystem::perms;
        auto status = std::filesystem::status(mConfig.tool_path, ec);
        bool executable = (status.permissions() & perms::owner_exec) != perms::none;
        if (!executable) {
            SLOG_ERROR << "cli tool not executable: " << mConfig.tool_path;
            return false;
        }

        mInitialized = true;
        mCurrentColor = LedColor::Off;
        SLOG_INFO << "CliLedDriver initialized, tool=" << mConfig.tool_path << " timeout=" << mConfig.timeout_ms
                  << "ms";
        return true;
    }

    bool CliLedDriver::SetColor(LedColor color) {
        if (!mInitialized) {
            SLOG_ERROR << "CliLedDriver not initialized";
            return false;
        }

        std::string action;
        if (!ColorToAction(color, action)) {
            SLOG_ERROR << "Unsupported Led color for cli: " << static_cast<int>(color);
            return false;
        }

        std::string out;
        if (!CallTool(action, out)) {
            SLOG_ERROR << "cli tool " << mConfig.tool_path << " " << action << " failed: " << out;
            return false;
        }

        mCurrentColor = color;
        return true;
    }

    LedColor CliLedDriver::GetColor() const {
        return mCurrentColor;
    }

    MicMuteStatus CliLedDriver::QueryMuteStatus() {
        if (!mInitialized) {
            return MicMuteStatus::Unavailable;
        }
        std::string out;
        if (!CallTool("status", out)) {
            return MicMuteStatus::Unavailable;
        }
        if (out.find("unmute") != std::string::npos) {
            return MicMuteStatus::Unmute;
        }
        if (out.find("mute") != std::string::npos) {
            return MicMuteStatus::Mute;
        }
        return MicMuteStatus::Unavailable;
    }

    void CliLedDriver::Release() {
        if (!mInitialized) {
            return;
        }
        mInitialized = false;
        mCurrentColor = LedColor::Off;
    }

    bool CliLedDriver::SpawnChild(const std::string& action, int& read_fd, pid_t& pid, std::string& err) {
        int pipefd[2];
        if (pipe(pipefd) != 0) {
            err = "pipe create failed";
            return false;
        }

        pid = fork();
        if (pid < 0) {
            close(pipefd[0]);
            close(pipefd[1]);
            err = "fork failed";
            return false;
        }

        if (pid == 0) {
            // 子进程：stdout/stderr 重定向到管道写端，再 exec 外部工具
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);
            execl(mConfig.tool_path.c_str(), mConfig.tool_path.c_str(), action.c_str(), nullptr);
            _exit(127);
        }

        // 父进程：关闭写端，否则子进程退出后 read 不会返回 EOF
        close(pipefd[1]);
        read_fd = pipefd[0];
        return true;
    }

    bool CliLedDriver::ReadWithTimeout(int read_fd, std::string& out, bool& timed_out) {
        timed_out = false;
        out.clear();

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(mConfig.timeout_ms);

        while (true) {
            auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())
                    .count();
            if (remaining <= 0) {
                timed_out = true;
                return false;
            }

            struct pollfd pfd;
            pfd.fd = read_fd;
            pfd.events = POLLIN;

            int ret = poll(&pfd, 1, static_cast<int>(remaining));
            if (ret < 0) {
                if (errno == EINTR)
                    continue;
                return false;
            }
            if (ret == 0) {
                timed_out = true;
                return false;
            }

            char buf[256];
            ssize_t n = read(read_fd, buf, sizeof(buf));
            if (n > 0) {
                out.append(buf, static_cast<size_t>(n));
            } else if (n == 0) {
                return true;  // EOF：子进程已关闭写端
            } else if (errno != EINTR) {
                return false;
            }
        }
    }

    bool CliLedDriver::ReapChild(pid_t pid, bool force_kill, std::string& err, int& exit_code) {
        if (force_kill) {
            kill(pid, SIGKILL);
        }

        int status = 0;
        if (waitpid(pid, &status, 0) < 0) {
            err = "waitpid failed: " + std::string(strerror(errno));
            exit_code = -1;
            return false;
        }

        exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }

    bool CliLedDriver::CallTool(const std::string& action, std::string& out) {
        mLastExitCode = 0;
        int read_fd = -1;
        pid_t pid = -1;
        if (!SpawnChild(action, read_fd, pid, out)) {
            mLastExitCode = -1;
            return false;
        }

        bool timed_out = false;
        bool read_ok = ReadWithTimeout(read_fd, out, timed_out);
        close(read_fd);

        // 超时必须 SIGKILL 回收；正常情况下读取已结束（EOF）也要 waitpid 收尸
        bool success = ReapChild(pid, timed_out, out, mLastExitCode);
        if (timed_out) {
            out = "timeout after " + std::to_string(mConfig.timeout_ms) + "ms";
            mLastExitCode = 2;
            return false;
        }
        return read_ok && success;
    }

    int CliLedDriver::LastExitCode() const {
        return mLastExitCode;
    }

}  // namespace qifeng
