/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include <string>

struct gpiod_chip;
struct gpiod_line;

namespace qifeng::scm {

/**
 * @brief libgpiod GPIO 封装（RAII）
 * @details 构造时打开指定 GPIO 引脚并设为输出模式；提供 Set()/Get()。
 * 析构时释放引脚并关闭芯片（仅当芯片由本对象打开时）。
 * 支持两种引脚指定格式：
 *   - 全局编号: "488"（自动查找所属 gpiochip）
 *   - 芯片:偏移: "0:16"（直接打开 gpiochip0 的第 16 线）
 * 无 libgpiod 或引脚不可用时构造失败，由调用方决定 Skipped。
 */
class Gpio {
public:
    Gpio() = default;

    /**
     * @brief 打开引脚并设为 out；失败则 IsOpen()==false
     * @param number 引脚编号
     */
    explicit Gpio(const std::string &number);
    ~Gpio();

    Gpio(const Gpio &) = delete;
    Gpio &operator=(const Gpio &) = delete;

    /**
     * @brief 检查引脚是否已打开
     */
    bool IsOpen() const { return mOk; }

    /**
     * @brief 设置电平
     * @param high 电平值
     * @return 成功返回 true
     */
    bool Set(bool high);

    /**
     * @brief 回读当前电平
     * @param high 输出电平值
     * @return 成功返回 true
     */
    bool Get(bool &high);

private:
    gpiod_chip *mChip = nullptr;
    gpiod_line *mLine = nullptr;
    bool mOwnsChip = false;
    bool mOk = false;
};

}  // namespace qifeng::scm