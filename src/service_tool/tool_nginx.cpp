/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "service_tool/tool_nginx.h"

#include "common/types.h"
#include "qifeng_framework/common/logger.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
    // 执行 shell 命令并捕获输出，合并 stderr 到 stdout
    int ExecCommand(const std::string &cmd, std::string &output) {
        std::string fullCmd = cmd + " 2>&1";
        FILE* pipe = popen(fullCmd.c_str(), "r");
        if (pipe == nullptr) {
            output = "popen failed";
            return -1;
        }

        std::array<char, 4096> buffer {};
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output += buffer.data();
        }

        int status = pclose(pipe);
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        }
        return -1;
    }

    // 对字符串进行 shell 单引号转义：用单引号包裹，内部单引号替换为 '\''
    std::string EscapeShellSingleQuote(const std::string &s) {
        std::string result = "'";
        for (char c : s) {
            if (c == '\'') {
                result += "'\\''";
            } else {
                result += c;
            }
        }
        result += "'";
        return result;
    }

    // 去除字符串末尾的换行符与回车符
    void TrimTrailingNewlines(std::string &s) {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
            s.pop_back();
        }
    }

    // nginx 可执行文件路径
    constexpr const char* NginxBinary = "/usr/sbin/nginx";
    // nginx pid 文件路径
    constexpr const char* NginxPidFile = "/var/run/nginx.pid";
}  // namespace

namespace qifeng::scm::tool {

    bool Nginx::IsInstalled() const {
        bool exists = fs::exists(NginxBinary);
        SLOG_INFO << "nginx installed check: " << NginxBinary << " exists=" << exists;
        return exists;
    }

    bool Nginx::IsRunning() const {
        // 读取 pid 文件
        std::ifstream ifs(NginxPidFile);
        if (!ifs) {
            SLOG_INFO << "nginx pid file not readable: " << NginxPidFile;
            return false;
        }

        std::string pidStr;
        std::getline(ifs, pidStr);
        ifs.close();

        // 去除空白字符
        while (!pidStr.empty() &&
               (pidStr.back() == '\n' || pidStr.back() == '\r' || pidStr.back() == ' ' || pidStr.back() == '\t')) {
            pidStr.pop_back();
        }
        if (pidStr.empty()) {
            SLOG_WARN << "nginx pid file is empty: " << NginxPidFile;
            return false;
        }

        // 转换为 pid 数字
        char* endPtr = nullptr;
        long pidVal = std::strtol(pidStr.c_str(), &endPtr, 10);
        if (endPtr == pidStr.c_str() || pidVal <= 0) {
            SLOG_WARN << "invalid nginx pid: " << pidStr;
            return false;
        }

        // 使用 kill(pid, 0) 检测进程是否存在（信号 0 不实际发送信号）
        pid_t pid = static_cast<pid_t>(pidVal);
        if (kill(pid, 0) != 0) {
            SLOG_INFO << "nginx process not alive, pid=" << pid;
            return false;
        }
        SLOG_INFO << "nginx is running, pid=" << pid;
        return true;
    }

    ResultMsg Nginx::Start(const std::string &confPath) const {
        if (confPath.empty()) {
            return MakeError("nginx config path is empty");
        }
        if (!fs::exists(confPath)) {
            return MakeError("nginx config file does not exist: " + confPath);
        }
        if (!IsInstalled()) {
            return MakeError("nginx is not installed");
        }

        // 若已在运行，避免重复启动
        if (IsRunning()) {
            SLOG_INFO << "nginx already running, skip start";
            return MakeSuccess();
        }

        std::string cmd = std::string(NginxBinary) + " -c " + EscapeShellSingleQuote(confPath);
        std::string output;
        int ret = ExecCommand(cmd, output);
        TrimTrailingNewlines(output);

        if (ret != 0) {
            SLOG_ERROR << "nginx start failed, ret=" << ret << " output=" << output;
            return MakeError("nginx start failed: " + output);
        }
        SLOG_INFO << "nginx started with config: " << confPath;
        return MakeSuccess();
    }

    ResultMsg Nginx::StartSystem() const {
        if (!IsInstalled()) {
            return MakeError("nginx is not installed");
        }

        // 若已在运行，使用 reload 应用新配置
        if (IsRunning()) {
            SLOG_INFO << "nginx already running, do reload";
            return Reload();
        }

        // 使用系统默认配置启动（不指定 -c）
        std::string cmd = std::string(NginxBinary);
        std::string output;
        int ret = ExecCommand(cmd, output);
        TrimTrailingNewlines(output);

        if (ret != 0) {
            SLOG_ERROR << "nginx system start failed, ret=" << ret << " output=" << output;
            return MakeError("nginx system start failed: " + output);
        }
        SLOG_INFO << "nginx started with system default config";
        return MakeSuccess();
    }

    ResultMsg Nginx::Stop() const {
        if (!IsInstalled()) {
            return MakeError("nginx is not installed");
        }

        if (!IsRunning()) {
            SLOG_INFO << "nginx not running, skip stop";
            return MakeSuccess();
        }

        std::string cmd = std::string(NginxBinary) + " -s stop";
        std::string output;
        int ret = ExecCommand(cmd, output);
        TrimTrailingNewlines(output);

        if (ret != 0) {
            SLOG_ERROR << "nginx stop failed, ret=" << ret << " output=" << output;
            return MakeError("nginx stop failed: " + output);
        }
        SLOG_INFO << "nginx stopped";
        return MakeSuccess();
    }

    ResultMsg Nginx::Reload() const {
        if (!IsInstalled()) {
            return MakeError("nginx is not installed");
        }

        if (!IsRunning()) {
            SLOG_WARN << "nginx not running, reload skipped";
            return MakeError("nginx is not running, cannot reload");
        }

        std::string cmd = std::string(NginxBinary) + " -s reload";
        std::string output;
        int ret = ExecCommand(cmd, output);
        TrimTrailingNewlines(output);

        if (ret != 0) {
            SLOG_ERROR << "nginx reload failed, ret=" << ret << " output=" << output;
            return MakeError("nginx reload failed: " + output);
        }
        SLOG_INFO << "nginx reloaded";
        return MakeSuccess();
    }

    ResultMsg Nginx::TestConfig(const std::string &confPath) const {
        if (confPath.empty()) {
            return MakeError("nginx config path is empty");
        }
        if (!fs::exists(confPath)) {
            return MakeError("nginx config file does not exist: " + confPath);
        }
        if (!IsInstalled()) {
            return MakeError("nginx is not installed");
        }

        std::string cmd = std::string(NginxBinary) + " -t -c " + EscapeShellSingleQuote(confPath);
        std::string output;
        int ret = ExecCommand(cmd, output);
        TrimTrailingNewlines(output);

        if (ret != 0) {
            SLOG_ERROR << "nginx config test failed, ret=" << ret << " output=" << output;
            return MakeError("nginx config test failed: " + output);
        }
        SLOG_INFO << "nginx config test passed: " << confPath;
        return MakeSuccess();
    }

    ResultMsg Nginx::TestSystemConfig() const {
        if (!IsInstalled()) {
            return MakeError("nginx is not installed");
        }

        // 不指定 -c，测试系统默认配置 /etc/nginx/nginx.conf
        std::string cmd = std::string(NginxBinary) + " -t";
        std::string output;
        int ret = ExecCommand(cmd, output);
        TrimTrailingNewlines(output);

        if (ret != 0) {
            SLOG_ERROR << "nginx system config test failed, ret=" << ret << " output=" << output;
            return MakeError("nginx system config test failed: " + output);
        }
        SLOG_INFO << "nginx system config test passed";
        return MakeSuccess();
    }

    ResultMsg Nginx::GetVersion() const {
        if (!IsInstalled()) {
            return MakeError("nginx is not installed");
        }

        std::string cmd = std::string(NginxBinary) + " -v";
        std::string output;
        int ret = ExecCommand(cmd, output);

        if (ret != 0) {
            SLOG_ERROR << "nginx get version failed, ret=" << ret << " output=" << output;
            return MakeError("failed to get nginx version: " + output);
        }

        // 去除末尾换行符
        TrimTrailingNewlines(output);

        SLOG_INFO << "nginx version: " << output;
        return {0, output};
    }

}  // namespace qifeng::scm::tool
