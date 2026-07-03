/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/service_error_info.h"

#include <csignal>
#include <sstream>

namespace qifeng::scm {

    namespace {
        // 将信号编号转换为信号名称
        const char* SignalName(int sig) {
            switch (sig) {
                case SIGABRT:
                    return "SIGABRT";
                case SIGALRM:
                    return "SIGALRM";
                case SIGBUS:
                    return "SIGBUS";
                case SIGFPE:
                    return "SIGFPE";
                case SIGHUP:
                    return "SIGHUP";
                case SIGILL:
                    return "SIGILL";
                case SIGINT:
                    return "SIGINT";
                case SIGKILL:
                    return "SIGKILL";
                case SIGPIPE:
                    return "SIGPIPE";
                case SIGQUIT:
                    return "SIGQUIT";
                case SIGSEGV:
                    return "SIGSEGV";
                case SIGTERM:
                    return "SIGTERM";
                case SIGUSR1:
                    return "SIGUSR1";
                case SIGUSR2:
                    return "SIGUSR2";
                case SIGCHLD:
                    return "SIGCHLD";
                case SIGCONT:
                    return "SIGCONT";
                case SIGSTOP:
                    return "SIGSTOP";
                case SIGTSTP:
                    return "SIGTSTP";
                case SIGTTIN:
                    return "SIGTTIN";
                case SIGTTOU:
                    return "SIGTTOU";
                case SIGXCPU:
                    return "SIGXCPU";
                case SIGXFSZ:
                    return "SIGXFSZ";
                default:
                    return nullptr;
            }
        }
    }  // namespace

    // NOLINTNEXTLINE(readability-function-size,readability-function-cognitive-complexity)
    std::string ServiceErrorInfo::FormatError(const ServiceErrorInfo &info, const std::string &activeState,
                                              int recoveryCount) {
        // 服务正常运行，无需错误信息
        if (activeState == "active" && (info.subState == "running" || info.subState.empty())) {
            return "";
        }

        std::ostringstream ss;

        // 按 result 分类输出错误信息（保留 systemd 原生风格，不做中文翻译）
        if (info.result == "signal") {
            const char* sigName = SignalName(info.exitStatus);
            if (sigName) {
                ss << "killed by signal " << sigName;
            } else {
                ss << "killed by signal (" << info.exitStatus << ")";
            }
            ss << " (SubState=" << info.subState << ", exitCode=" << info.exitCode << ")";
        } else if (info.result == "exit-code") {
            ss << "exited with code " << info.exitCode;
            ss << " (SubState=" << info.subState << ")";
        } else if (info.result == "timeout") {
            ss << "timed out (SubState=" << info.subState << ")";
        } else if (info.result == "start-limit-hit") {
            ss << "start limit exceeded";
            if (recoveryCount > 0) {
                ss << " (" << recoveryCount << " recent restart attempts failed)";
            }
            ss << " (SubState=" << info.subState << ")";
        } else if (info.result == "resources") {
            ss << "insufficient resources (SubState=" << info.subState << ")";
        } else if (activeState == "inactive" && info.subState == "dead") {
            ss << "service is not running (SubState=dead)";
            if (recoveryCount > 0) {
                ss << ", " << recoveryCount << " recent restart attempt(s)";
            }
        } else if (activeState == "failed") {
            ss << "service startup failed (SubState=" << info.subState;
            if (info.exitCode > 0) {
                ss << ", exitCode=" << info.exitCode;
            }
            if (recoveryCount > 0) {
                ss << ", retries=" << recoveryCount;
            }
            ss << ")";
        } else {
            ss << "abnormal state (ActiveState=" << activeState << ", SubState=" << info.subState;
            if (info.exitCode > 0) {
                ss << ", exitCode=" << info.exitCode;
            }
            if (recoveryCount > 0) {
                ss << ", retries=" << recoveryCount;
            }
            ss << ")";
        }

        // 附加 statusText（如有）
        if (!info.statusText.empty()) {
            ss << " [" << info.statusText << "]";
        }

        return ss.str();
    }

}  // namespace qifeng::scm
