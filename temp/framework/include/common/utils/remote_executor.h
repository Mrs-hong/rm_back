/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_REMOTE_EXECUTOR_H
#define QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_REMOTE_EXECUTOR_H

#include <json/json.h>
#include <string>
#include <vector>

/**
 * @brief 远程执行器，用于通过SSH连接到远程服务器并执行命令
 *
 * 提供SSH连接管理、远程命令执行功能，支持同步和异步执行方式
 */
class RemoteExecutor {
    RemoteExecutor(const RemoteExecutor&) = delete;
    RemoteExecutor& operator=(const RemoteExecutor&) = delete;
    RemoteExecutor(RemoteExecutor&&) noexcept = delete;
    RemoteExecutor& operator=(RemoteExecutor&&) noexcept = delete;

    struct SshCommandOptions {
        std::string mUser;
        std::string mIp;
        std::vector<std::string> mCommands;
        std::string* mResult;
        std::string* mError;
    };

    struct LoadFile {
        std::string mUser;
        std::string mIp;
        std::string mLocalFilePath;
        std::string mRemoteFilePath;
        std::string* mError;
    };

public:
    /**
     * @brief 构造函数
     */
    RemoteExecutor();

    /**
     * @brief 析构函数
     */
    ~RemoteExecutor();

    /**
     * @brief 通过SSH连接到远程服务器并执行命令（使用配置中的默认user和ip）
     * @param command 要执行的命令
     * @param result 执行结果（输出到该参数）
     * @param error 错误信息（输出到该参数）
     * @return bool 执行成功返回true，失败返回false
     */
    bool ExecuteCommand(const std::string& command, std::string& result, std::string& error);

    /**
     * @brief 通过SSH连接到远程服务器并执行命令
     * @param user 远程服务器用户名
     * @param ip 远程服务器IP地址
     * @param command 要执行的命令
     * @param result 执行结果（输出到该参数）
     * @param error 错误信息（输出到该参数）
     * @return bool 执行成功返回true，失败返回false
     */
    bool ExecuteCommand(const SshCommandOptions& options);

    /**
     * @brief 通过SSH连接到远程服务器并执行多条命令（使用配置中的默认user和ip）
     * @param commands 要执行的命令列表
     * @param result 执行结果（输出到该参数）
     * @param error 错误信息（输出到该参数）
     * @return bool 执行成功返回true，失败返回false
     */
    bool ExecuteCommands(const std::vector<std::string>& commands, std::string& result, std::string& error);

    /**
     * @brief 通过SSH连接到远程服务器并执行多条命令
     * @param user 远程服务器用户名
     * @param ip 远程服务器IP地址
     * @param commands 要执行的命令列表
     * @param result 执行结果（输出到该参数）
     * @param error 错误信息（输出到该参数）
     * @return bool 执行成功返回true，失败返回false
     */
    bool ExecuteCommands(const SshCommandOptions& options);

    /**
     * @brief 通过SSH连接到远程服务器并执行命令（使用配置中的默认user和ip，JSON结果）
     * @param command 要执行的命令
     * @return Json::Value 执行结果，包含status、output、error字段
     */
    Json::Value ExecuteCommandJson(const std::string& command);

    /**
     * @brief 通过SSH连接到远程服务器并执行命令（JSON结果）
     * @param user 远程服务器用户名
     * @param ip 远程服务器IP地址
     * @param command 要执行的命令
     * @return Json::Value 执行结果，包含status、output、error字段
     */
    Json::Value ExecuteCommandJson(const std::string& user, const std::string& ip, const std::string& command);

    /**
     * @brief 通过SSH连接到远程服务器并执行多条命令（使用配置中的默认user和ip，JSON结果）
     * @param commands 要执行的命令列表
     * @return Json::Value 执行结果，包含status、output、error字段
     */
    Json::Value ExecuteCommandsJson(const std::vector<std::string>& commands);

    /**
     * @brief 通过SSH连接到远程服务器并执行多条命令（JSON结果）
     * @param user 远程服务器用户名
     * @param ip 远程服务器IP地址
     * @param commands 要执行的命令列表
     * @return Json::Value 执行结果，包含status、output、error字段
     */
    Json::Value ExecuteCommandsJson(const std::string& user, const std::string& ip,
                                    const std::vector<std::string>& commands);

    /**
     * @brief 设置SSH连接超时时间
     * @param timeoutMs 超时时间，单位：毫秒
     */
    void SetConnectionTimeout(uint32_t timeoutMs);

    /**
     * @brief 设置命令执行超时时间
     * @param timeoutMs 超时时间，单位：毫秒
     */
    void SetExecutionTimeout(uint32_t timeoutMs);

    /**
     * @brief 设置SSH私钥文件路径
     * @param privateKeyPath 私钥文件路径
     */
    void SetPrivateKeyPath(const std::string& privateKeyPath);

    /**
     * @brief 将本地文件上传到远程服务器（使用配置中的默认user和ip）
     * @param localFilePath 本地文件路径
     * @param remoteFilePath 远程文件路径
     * @param error 错误信息（输出到该参数）
     * @return bool 上传成功返回true，失败返回false
     */
    bool UploadFile(const std::string& localFilePath, const std::string& remoteFilePath, std::string& error);

    /**
     * @brief 将本地文件上传到远程服务器
     * @param user 远程服务器用户名
     * @param ip 远程服务器IP地址
     * @param localFilePath 本地文件路径
     * @param remoteFilePath 远程文件路径
     * @param error 错误信息（输出到该参数）
     * @return bool 上传成功返回true，失败返回false
     */
    bool UploadFile(const LoadFile& options);

    /**
     * @brief 从远程服务器下载文件到本地（使用配置中的默认user和ip）
     * @param remoteFilePath 远程文件路径
     * @param localFilePath 本地文件路径
     * @param error 错误信息（输出到该参数）
     * @return bool 下载成功返回true，失败返回false
     */
    bool DownloadFile(const std::string& remoteFilePath, const std::string& localFilePath, std::string& error);

    /**
     * @brief 从远程服务器下载文件到本地
     * @param user 远程服务器用户名
     * @param ip 远程服务器IP地址
     * @param remoteFilePath 远程文件路径
     * @param localFilePath 本地文件路径
     * @param error 错误信息（输出到该参数）
     * @return bool 下载成功返回true，失败返回false
     */
    bool DownloadFile(const LoadFile& options);

private:
    // 辅助函数
    bool CheckCommandParams(const std::string& user, const std::string& ip, const std::string& command,
                            std::string& error) const;
    std::string BuildSshCommand(const std::string& user, const std::string& ip, const std::string& command) const;
    bool RunSshCommand(const std::string& sshCommand, std::string& result, int& status, std::string& error) const;
    bool HandleSshStatus(int status, const std::string& result, std::string& error) const;
    /**
     * @brief 初始化SSH会话
     * @param user 远程服务器用户名
     * @param ip 远程服务器IP地址
     * @param session SSH会话指针（输出参数）
     * @return bool 初始化成功返回true，失败返回false
     */
    bool InitSshSession(const std::string& user, const std::string& ip, void*& session);

    /**
     * @brief 关闭SSH会话
     * @param session SSH会话指针
     */
    void CloseSshSession(void* session);

    /**
     * @brief 连接到SSH服务器
     * @param session SSH会话指针
     * @return bool 连接成功返回true，失败返回false
     */
    bool ConnectSshServer(void* session);

    /**
     * @brief 在SSH会话上执行命令
     * @param session SSH会话指针
     * @param command 要执行的命令
     * @param result 执行结果（输出参数）
     * @param error 错误信息（输出参数）
     * @return bool 执行成功返回true，失败返回false
     */
    bool ExecuteOnSession(void* session, const std::string& command, std::string& result, std::string& error);

    uint32_t mConnectionTimeoutMs;  ///< SSH连接超时时间（毫秒）
    uint32_t mExecutionTimeoutMs;   ///< 命令执行超时时间（毫秒）
    std::string mPrivateKeyPath;    ///< SSH私钥文件路径
    std::string mDefaultUser;       ///< 默认远程服务器用户名（从配置中读取）
    std::string mDefaultIp;         ///< 默认远程服务器IP地址（从配置中读取）
};

#endif  // QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_REMOTE_EXECUTOR_H