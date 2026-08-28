/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#ifndef HAL_BUTTON_BUTTON_DRIVER_H
#define HAL_BUTTON_BUTTON_DRIVER_H

namespace qifeng {
    /**
     * @brief 按键驱动抽象接口
     */
    class ButtonDriver {
    public:
        virtual ~ButtonDriver() = default;

        ButtonDriver(const ButtonDriver&) = delete;
        ButtonDriver& operator=(const ButtonDriver&) = delete;
        ButtonDriver(ButtonDriver&&) = delete;
        ButtonDriver& operator=(ButtonDriver&&) = delete;

        /**
         * @brief 初始化按键驱动
         * @return 初始化成功返回true
         */
        virtual bool Init() = 0;

        /**
         * @brief 读取按键电平
         * @param level 输出当前电平状态
         * @return 读取成功返回true
         */
        virtual bool ReadLevel(bool& level) = 0;

    protected:
        ButtonDriver() = default;
    };

}  // namespace qifeng
#endif  // HAL_BUTTON_BUTTON_DRIVER_H
