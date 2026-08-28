/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "common/config_define.h"
#include "common/config_manager.h"
#include "common/logger.h"
#include "common/utils/remote_executor.h"

RemoteExecutor::RemoteExecutor()
    : mConnectionTimeoutMs(Ssh::DefaultConnectionTimeout), mExecutionTimeoutMs(Ssh::DefaultExecutionTimeout) {
    // 从配置文件中读取SSH相关配置
    try {
        mDefaultUser = ConfigManager::GetInstance().GetString(std::string(Ssh::Section), std::string(Ssh::KeyUser),
                                                              std::string(Ssh::DefaultUser));
        mDefaultIp = ConfigManager::GetInstance().GetString(std::string(Ssh::Section), std::string(Ssh::KeyIp),
                                                            std::string(Ssh::DefaultIp));
        mPrivateKeyPath = ConfigManager::GetInstance().GetString(
            std::string(Ssh::Section), std::string(Ssh::KeyPrivateKeyPath), std::string(Ssh::DefaultPrivateKeyPath));
        mConnectionTimeoutMs = static_cast<uint32_t>(ConfigManager::GetInstance().GetInt(
            std::string(Ssh::Section), std::string(Ssh::KeyConnectionTimeout), Ssh::DefaultConnectionTimeout));
        mExecutionTimeoutMs = static_cast<uint32_t>(ConfigManager::GetInstance().GetInt(
            std::string(Ssh::Section), std::string(Ssh::KeyExecutionTimeout), Ssh::DefaultExecutionTimeout));
    } catch (const std::exception& e) {
        SLOG_WARN << "Failed to read SSH configuration from config: " << e.what();
        mDefaultUser = Ssh::DefaultUser;
        mDefaultIp = Ssh::DefaultIp;
        mPrivateKeyPath = Ssh::DefaultPrivateKeyPath;
        mConnectionTimeoutMs = Ssh::DefaultConnectionTimeout;
        mExecutionTimeoutMs = Ssh::DefaultExecutionTimeout;
    }
}

RemoteExecutor::~RemoteExecutor() {
    // 析构函数
}

void RemoteExecutor::SetConnectionTimeout(uint32_t timeoutMs) {
    mConnectionTimeoutMs = timeoutMs;
}

void RemoteExecutor::SetExecutionTimeout(uint32_t timeoutMs) {
    mExecutionTimeoutMs = timeoutMs;
}

void RemoteExecutor::SetPrivateKeyPath(const std::string& privateKeyPath) {
    mPrivateKeyPath = privateKeyPath;
}

bool RemoteExecutor::ExecuteCommand(const std::string& command, std::string& result, std::string& error) {
    std::vector<std::string> commands {command};
    SshCommandOptions options {mDefaultUser, mDefaultIp, commands, &result, &error};
    return ExecuteCommand(options);
}

// 新结构体参数重载实现
bool RemoteExecutor::ExecuteCommand(const SshCommandOptions& options) {
    if (!options.mResult || !options.mError) {
        return false;
    }
    if (!CheckCommandParams(options.mUser, options.mIp, options.mCommands.front(), *options.mError)) {
        SLOG_ERROR << *options.mError;
        return false;
    }
    std::string sshCommand = BuildSshCommand(options.mUser, options.mIp, options.mCommands.front());
    SLOG_INFO << "Executing SSH command: " << sshCommand;
    int status = 0;
    if (!RunSshCommand(sshCommand, *options.mResult, status, *options.mError)) {
        SLOG_ERROR << *options.mError;
        return false;
    }
    if (!HandleSshStatus(status, *options.mResult, *options.mError)) {
        SLOG_ERROR << *options.mError;
        return false;
    }
    SLOG_INFO << "SSH command executed successfully, output: " << *options.mResult;
    return true;
}

bool RemoteExecutor::CheckCommandParams(const std::string& user, const std::string& ip, const std::string& command,
                                        std::string& error) const {
    if (user.empty() || ip.empty() || command.empty()) {
        error = "Invalid parameters: user, ip, and command cannot be empty";
        return false;
    }
    return true;
}

std::string RemoteExecutor::BuildSshCommand(const std::string& user, const std::string& ip,
                                            const std::string& command) const {
    std::string sshCommand = "ssh -t -o ConnectTimeout=" + std::to_string(mConnectionTimeoutMs / 1000) +
                             " -o StrictHostKeyChecking=no" + " -o UserKnownHostsFile=/dev/null";
    if (!mPrivateKeyPath.empty()) {
        sshCommand += " -i " + mPrivateKeyPath;
    }
    sshCommand += " " + user + "@" + ip + " \"" + command + "\"";
    return sshCommand;
}

bool RemoteExecutor::RunSshCommand(const std::string& sshCommand, std::string& result, int& status,
                                   std::string& error) const {
    FILE* pipe = popen(sshCommand.c_str(), "r");
    if (!pipe) {
        error = "Failed to create pipe for SSH command";
        return false;
    }
    std::array<char, 4096> buffer {};
    result.clear();
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    status = pclose(pipe);
    if (status == -1) {
        error = "Failed to close pipe for SSH command";
        return false;
    }
    return true;
}

bool RemoteExecutor::HandleSshStatus(int status, const std::string& result, std::string& error) const {
    if (WIFEXITED(status)) {
        int exitCode = WEXITSTATUS(status);
        if (exitCode != 0) {
            error = "SSH command execution failed with exit code " + std::to_string(exitCode) + ": " + result;
            return false;
        }
    } else if (WIFSIGNALED(status)) {
        int signal = WTERMSIG(status);
        error = "SSH command was terminated by signal " + std::to_string(signal);
        return false;
    } else {
        error = "SSH command execution failed with unknown status";
        return false;
    }
    return true;
}

bool RemoteExecutor::ExecuteCommands(const std::vector<std::string>& commands, std::string& result,
                                     std::string& error) {
    SshCommandOptions options {mDefaultUser, mDefaultIp, commands, &result, &error};
    return ExecuteCommands(options);
}

bool RemoteExecutor::ExecuteCommands(const SshCommandOptions& options) {
    if (options.mUser.empty() || options.mIp.empty() || options.mCommands.empty()) {
        *options.mError = "Invalid parameters: user, ip, and commands cannot be empty";
        SLOG_ERROR << *options.mError;
        return false;
    }

    // 组合多条命令，每条命令使用 bash -i -c 包装，使用分号分隔
    std::string combinedCommand;
    for (size_t i = 0; i < options.mCommands.size(); ++i) {
        if (!options.mCommands[i].empty()) {
            // 对命令中的双引号进行转义
            std::string escapedCommand = options.mCommands[i];
            size_t pos = 0;
            while ((pos = escapedCommand.find('"', pos)) != std::string::npos) {
                escapedCommand.insert(pos, "\\");
                pos += 2;
            }

            combinedCommand += "bash -i -c \"" + escapedCommand + "\"";
            if (i < options.mCommands.size() - 1) {
                combinedCommand += "; ";
            }
        }
    }

    // 调用已有的 ExecuteCommand 方法执行组合后的命令
    return ExecuteCommand(options);
}

Json::Value RemoteExecutor::ExecuteCommandJson(const std::string& command) {
    return ExecuteCommandJson(mDefaultUser, mDefaultIp, command);
}

Json::Value RemoteExecutor::ExecuteCommandJson(const std::string& user, const std::string& ip,
                                               const std::string& command) {
    Json::Value resultJson;
    std::string output;
    std::string error;

    SshCommandOptions options {user, ip, {command}, &output, &error};

    bool success = ExecuteCommand(options);

    resultJson["status"] = success;
    resultJson["output"] = output;
    resultJson["error"] = error;

    return resultJson;
}

Json::Value RemoteExecutor::ExecuteCommandsJson(const std::vector<std::string>& commands) {
    return ExecuteCommandsJson(mDefaultUser, mDefaultIp, commands);
}

Json::Value RemoteExecutor::ExecuteCommandsJson(const std::string& user, const std::string& ip,
                                                const std::vector<std::string>& commands) {
    Json::Value resultJson;
    std::string output;
    std::string error;

    SshCommandOptions options {user, ip, commands, &output, &error};

    bool success = ExecuteCommands(options);

    resultJson["status"] = success;
    resultJson["output"] = output;
    resultJson["error"] = error;

    return resultJson;
}

bool RemoteExecutor::UploadFile(const std::string& localFilePath, const std::string& remoteFilePath,
                                std::string& error) {
    LoadFile options {mDefaultUser, mDefaultIp, localFilePath, remoteFilePath, &error};
    return UploadFile(options);
}

bool RemoteExecutor::UploadFile(const LoadFile& options) {
    if (options.mUser.empty() || options.mIp.empty() || options.mLocalFilePath.empty() ||
        options.mRemoteFilePath.empty()) {
        *options.mError = "Invalid parameters: user, ip, localFilePath, and remoteFilePath cannot be empty";
        SLOG_ERROR << *options.mError;
        return false;
    }
    std::string scpCommand = "scp -o ConnectTimeout=" + std::to_string(mConnectionTimeoutMs / 1000) +
                             " -o StrictHostKeyChecking=no" + " -o UserKnownHostsFile=/dev/null";
    if (!mPrivateKeyPath.empty()) {
        scpCommand += " -i " + mPrivateKeyPath;
    }
    scpCommand +=
        " " + options.mLocalFilePath + " " + options.mUser + "@" + options.mIp + ":" + options.mRemoteFilePath;
    FILE* pipe = popen(scpCommand.c_str(), "r");
    if (!pipe) {
        *options.mError = "Failed to create pipe for SCP upload command";
        SLOG_ERROR << *options.mError;
        return false;
    }
    std::array<char, 4096> buffer = {};
    std::string result;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    int status = pclose(pipe);
    if (status == -1) {
        *options.mError = "Failed to close pipe for SCP upload command";
        SLOG_ERROR << *options.mError;
        return false;
    }
    if (WIFEXITED(status)) {
        int exitCode = WEXITSTATUS(status);
        if (exitCode != 0) {
            *options.mError = "SCP upload failed with exit code " + std::to_string(exitCode) + ": " + result;
            SLOG_ERROR << *options.mError;
            return false;
        }
    } else if (WIFSIGNALED(status)) {
        int signal = WTERMSIG(status);
        *options.mError = "SCP upload was terminated by signal " + std::to_string(signal);
        SLOG_ERROR << *options.mError;
        return false;
    } else {
        *options.mError = "SCP upload failed with unknown status";
        SLOG_ERROR << *options.mError;
        return false;
    }
    SLOG_INFO << "SCP upload completed successfully";
    return true;
}

bool RemoteExecutor::DownloadFile(const std::string& remoteFilePath, const std::string& localFilePath,
                                  std::string& error) {
    LoadFile options {mDefaultUser, mDefaultIp, localFilePath, remoteFilePath, &error};
    return DownloadFile(options);
}

bool RemoteExecutor::DownloadFile(const LoadFile& options) {
    if (options.mUser.empty() || options.mIp.empty() || options.mRemoteFilePath.empty() ||
        options.mLocalFilePath.empty()) {
        *options.mError = "Invalid parameters: user, ip, remoteFilePath, and localFilePath cannot be empty";
        SLOG_ERROR << *options.mError;
        return false;
    }
    std::string scpCommand = "scp -o ConnectTimeout=" + std::to_string(mConnectionTimeoutMs / 1000) +
                             " -o StrictHostKeyChecking=no" + " -o UserKnownHostsFile=/dev/null";
    if (!mPrivateKeyPath.empty()) {
        scpCommand += " -i " + mPrivateKeyPath;
    }
    scpCommand +=
        " " + options.mUser + "@" + options.mIp + ":" + options.mRemoteFilePath + " " + options.mLocalFilePath;
    FILE* pipe = popen(scpCommand.c_str(), "r");
    if (!pipe) {
        *options.mError = "Failed to create pipe for SCP download command";
        SLOG_ERROR << *options.mError;
        return false;
    }
    std::array<char, 4096> buffer = {};
    std::string result;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    int status = pclose(pipe);
    if (status == -1) {
        *options.mError = "Failed to close pipe for SCP download command";
        SLOG_ERROR << *options.mError;
        return false;
    }
    if (WIFEXITED(status)) {
        int exitCode = WEXITSTATUS(status);
        if (exitCode != 0) {
            *options.mError = "SCP download failed with exit code " + std::to_string(exitCode) + ": " + result;
            SLOG_ERROR << *options.mError;
            return false;
        }
    } else if (WIFSIGNALED(status)) {
        int signal = WTERMSIG(status);
        *options.mError = "SCP download was terminated by signal " + std::to_string(signal);
        SLOG_ERROR << *options.mError;
        return false;
    } else {
        *options.mError = "SCP download failed with unknown status";
        SLOG_ERROR << *options.mError;
        return false;
    }
    SLOG_INFO << "SCP download completed successfully";
    return true;
}

// 以下是使用libssh库的实现模板，目前未启用
bool RemoteExecutor::InitSshSession(const std::string& user, const std::string& ip, void*& session) {
    // 预留libssh实现
    // 使用一下传入的变量，确保编译器不会警告未使用变量
    (void)user;
    (void)ip;
    (void)session;
    return false;
}

void RemoteExecutor::CloseSshSession(void* session) {
    // 预留libssh实现
}

bool RemoteExecutor::ConnectSshServer(void* session) {
    // 预留libssh实现
    // 使用一下传入的变量，确保编译器不会警告未使用变量
    (void)session;
    return false;
}

bool RemoteExecutor::ExecuteOnSession(void* session, const std::string& command, std::string& result,
                                      std::string& error) {
    // 预留libssh实现
    // 使用一下传入的变量，确保编译器不会警告未使用变量
    (void)session;
    (void)command;
    (void)result;
    (void)error;
    return false;
}