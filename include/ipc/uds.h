/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <string>

namespace qifeng::scm {

    /**
     * @brief UDS工作模式枚举
     */
    enum class UdsMode {
        SERVER,  // scmd服务端模式
        CLIENT   // scmctl客户端模式
    };

    /**
     * @brief unix domain socket wrapper class
     * 通过对本地unix domain socket的RAII封装，提供统一的接口用于与服务器进行通信
     * - 0、创建时候分为(客户端)scmctl和（服务端）scmd模式
     * - 1. 通过创建.sock文件建立通信(scmd负责管理、维护scmctl负责使用)、文件路径由外部传入
     * - 2. 支持通信文件创建删除
     * - 3. 支持流式数据发送接收、提供阻塞和非阻塞模式的接口
     * - 4. scmd可以设置支持连接数量、默认为单连接
     * - 5. 提供详细的错误信息
     */
    class UdsWrapper {
    public:
        /**
         * @brief 构造函数
         * @param mode SERVER或CLIENT模式
         * @param socketPath .sock文件路径
         * @param maxConnections 服务端最大连接数（仅SERVER模式有效，默认1）
         */
        explicit UdsWrapper(UdsMode mode, const std::string &socketPath, int maxConnections = 1);

        ~UdsWrapper();

        UdsWrapper(const UdsWrapper &) = delete;
        UdsWrapper &operator=(const UdsWrapper &) = delete;
        UdsWrapper(UdsWrapper &&) = delete;
        UdsWrapper &operator=(UdsWrapper &&) = delete;

        /**
         * @brief 初始化
         * SERVER模式：创建socket/bind/listen
         * CLIENT模式：创建socket
         * 重复调用会先关闭之前的资源再重新初始化
         * @return 是否初始化成功
         */
        bool Initialize();

        /**
         * @brief 关闭连接并清理资源
         */
        void Close();

        /**
         * @brief 等待客户端连接（阻塞），仅SERVER模式
         * @return 客户端fd，调用方负责关闭该fd，失败返回-1
         */
        int AcceptClient();

        /**
         * @brief 连接到服务端（阻塞），仅CLIENT模式
         * @return 是否连接成功
         */
        bool Connect();

        /**
         * @brief 发送数据（阻塞模式，自动处理部分写入和EINTR）
         * @param fd 目标fd（服务端使用AcceptClient返回的fd，客户端使用GetSocketFd()返回的fd）
         * @param data 发送的数据
         * @return 发送的字节数，-1表示错误
         */
        ssize_t Send(int fd, const std::string &data);

        /**
         * @brief 接收数据（阻塞模式，自动重试EINTR）
         * @param fd 来源fd
         * @param buf 接收缓冲区
         * @param bufSize 缓冲区大小
         * @return 接收的字节数，0表示连接关闭，-1表示错误
         */
        ssize_t Receive(int fd, char* buf, size_t bufSize);

        /**
         * @brief 设置fd为非阻塞模式
         * @param fd 文件描述符
         * @return 是否设置成功
         */
        bool SetNonBlocking(int fd);

        /**
         * @brief 获取socket路径
         */
        const std::string &GetSocketPath() const;

        /**
         * @brief 获取socket fd
         * SERVER模式返回监听fd，CLIENT模式返回连接fd
         */
        int GetSocketFd() const;

    private:
        /**
         * @brief 准备socket路径（检查长度、清理旧文件）
         */
        bool PrepareSocketPath();

        /**
         * @brief 删除.sock文件
         */
        void RemoveSocketFile();

    private:
        UdsMode mMode;
        std::string mSocketPath;
        int mMaxConnections;
        int mSocketFd {-1};
    };

}  // namespace qifeng::scm
