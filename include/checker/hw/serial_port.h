/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace qifeng::scm {

/**
 * @brief POSIX 串口封装（RAII）
 * @details 构造时打开 tty 设备并配置波特率/8N1/原始模式；提供 Write/Read(限时)。
 * 析构自动关闭 fd。打开失败时 IsOpen()==false，由调用方决定 Skipped。
 */
class SerialPort {
public:
    SerialPort() = default;

    /**
     * @brief 打开设备并配置波特率；失败不抛异常，置 mFd=-1
     * @param device 设备路径
     * @param baud 波特率
     */
    SerialPort(const std::string &device, int baud);
    ~SerialPort();

    SerialPort(const SerialPort &) = delete;
    SerialPort &operator=(const SerialPort &) = delete;

    /**
     * @brief 检查串口是否已打开
     */
    bool IsOpen() const { return mFd >= 0; }

    /**
     * @brief 写入字节
     * @param data 数据指针
     * @param len 数据长度
     * @return 实际写入字节数；<0 表示错误
     */
    ssize_t Write(const uint8_t *data, size_t len);

    /**
     * @brief 限时读取
     * @param buf 缓冲区
     * @param max_len 最大读取字节数
     * @param timeout_ms 超时毫秒数
     * @return 读取字节数
     */
    ssize_t Read(uint8_t *buf, size_t max_len, int timeout_ms);

private:
    int mFd = -1;
};

}  // namespace qifeng::scm