/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include "common/scmd_types.h"
#include <string>

struct sd_bus;

namespace qifeng::scm {
    /**
     * @brief DBus总线类型枚举
     */
    enum class BusType {
        System,  // 系统总线（默认，需要root权限管理systemd系统服务）
        User     // 用户会话总线（管理systemd用户服务，无需root权限）
    };

    /**
     * @brief systemd DBus管理类
     * sd_bus RAII cpp wrapper: 对systemd
     * DBus的连接和服务生命周期管理常用接口（如启动、状态查询、停止、重启服务等）封装；目的是安全、方便地管理systemd服务的生命周期；
     * @note 接口类统一返回值为ResultMsg，方便调用者处理不同场景下的结果；
     */
    class DBusManager {
    public:
        /**
         * @brief 默认构造函数，使用系统总线
         */
        DBusManager();

        /**
         * @brief 指定总线类型的构造函数
         * @param busType 总线类型（System/User）
         */
        explicit DBusManager(BusType busType);

        ~DBusManager();

        DBusManager(const DBusManager &) = delete;
        DBusManager &operator=(const DBusManager &) = delete;
        DBusManager(DBusManager &&) = delete;
        DBusManager &operator=(DBusManager &&) = delete;

        /**
         * @brief 启动systemd服务单元
         * @param serviceName 服务名称（不含.service后缀）
         * @return ResultMsg 操作结果
         */
        ResultMsg StartUnit(const std::string &serviceName);

        /**
         * @brief 停止systemd服务单元
         * @param serviceName 服务名称（不含.service后缀）
         * @return ResultMsg 操作结果
         */
        ResultMsg StopUnit(const std::string &serviceName);

        /**
         * @brief 重启systemd服务单元
         * @param serviceName 服务名称（不含.service后缀）
         * @return ResultMsg 操作结果
         */
        ResultMsg RestartUnit(const std::string &serviceName);

        /**
         * @brief 获取服务活跃状态（active/inactive/failed等）
         * @param serviceName 服务名称（不含.service后缀）
         * @return ResultMsg 成功时msg为ActiveState值
         */
        ResultMsg GetUnitActiveState(const std::string &serviceName);

        /**
         * @brief 获取服务子状态（running/dead/exited等）
         * @param serviceName 服务名称（不含.service后缀）
         * @return ResultMsg 成功时msg为SubState值
         */
        ResultMsg GetUnitSubState(const std::string &serviceName);

        /**
         * @brief 获取服务主进程PID
         * @param serviceName 服务名称（不含.service后缀）
         * @return ResultMsg 成功时msg为PID字符串
         */
        ResultMsg GetServiceMainPID(const std::string &serviceName);

        /**
         * @brief 获取服务最后一次进入active状态的时间戳（微秒）
         * @param serviceName 服务名称（不含.service后缀）
         * @return ResultMsg 成功时msg为时间戳字符串
         */
        ResultMsg GetUnitActiveEnterTimestamp(const std::string &serviceName);

        /**
         * @brief 获取服务当前内存使用量（字节）
         * @param serviceName 服务名称（不含.service后缀）
         * @return ResultMsg 成功时msg为内存字节数字符串
         */
        ResultMsg GetServiceMemoryCurrent(const std::string &serviceName);

        /**
         * @brief 获取服务CPU累计使用时间（纳秒）
         * @param serviceName 服务名称（不含.service后缀）
         * @return ResultMsg 成功时msg为纳秒数字符串
         */
        ResultMsg GetServiceCPUUsageNSec(const std::string &serviceName);

        /**
         * @brief 获取服务重启次数
         * @param serviceName 服务名称（不含.service后缀）
         * @return ResultMsg 成功时msg为重启次数字符串
         */
        ResultMsg GetServiceNRestarts(const std::string &serviceName);

        /**
         * @brief 启用服务开机自启
         * @param serviceName 服务名称（不含.service后缀）
         * @return ResultMsg 操作结果
         */
        ResultMsg EnableUnit(const std::string &serviceName);

        /**
         * @brief 禁用服务开机自启
         * @param serviceName 服务名称（不含.service后缀）
         * @return ResultMsg 操作结果
         */
        ResultMsg DisableUnit(const std::string &serviceName);

        /**
         * @brief 重新加载systemd守护进程配置
         * @return ResultMsg 操作结果
         */
        ResultMsg ReloadDaemon();

        /**
         * @brief 检查DBus连接是否正常
         * @return bool 是否已连接
         */
        bool IsConnected() const;

    private:
        /**
         * @brief 统一调用systemd Manager的StartUnit/StopUnit/RestartUnit方法
         * @param method DBus方法名（如"StartUnit"）
         * @param serviceName 服务名称（不含.service后缀）
         * @param mode 启停模式（replace/fail/isolate等）
         * @return ResultMsg 操作结果
         */
        ResultMsg CallManagerMethod(const std::string &method, const std::string &serviceName, const std::string &mode);

        /**
         * @brief 获取服务单元的DBus对象路径
         * @param serviceName 服务名称（不含.service后缀）
         * @return std::string 对象路径，失败返回空字符串
         */
        std::string GetUnitPath(const std::string &serviceName);

        /**
         * @brief 读取字符串类型的DBus属性
         * @param objectPath DBus对象路径
         * @param interface DBus接口名
         * @param property 属性名
         * @return ResultMsg 成功时msg为属性值
         */
        ResultMsg GetStringProperty(const std::string &objectPath, const std::string &interface,
                                    const std::string &property);

        /**
         * @brief 读取uint32类型的DBus属性
         * @param objectPath DBus对象路径
         * @param interface DBus接口名
         * @param property 属性名
         * @return ResultMsg 成功时msg为属性值字符串
         */
        ResultMsg GetUint32Property(const std::string &objectPath, const std::string &interface,
                                    const std::string &property);

        /**
         * @brief 读取uint64类型的DBus属性
         * @param objectPath DBus对象路径
         * @param interface DBus接口名
         * @param property 属性名
         * @return ResultMsg 成功时msg为属性值字符串
         */
        ResultMsg GetUint64Property(const std::string &objectPath, const std::string &interface,
                                    const std::string &property);

    private:
        sd_bus* mBus;     // sd_bus连接指针
        bool mConnected;  // 连接状态
    };
}  // namespace qifeng::scm
