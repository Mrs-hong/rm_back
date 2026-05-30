/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "qifeng_framework/common/logger.h"
#include "service_manger/dbus_manager.h"

#include <systemd/sd-bus.h>

#include <cstring>
#include <iostream>

namespace {
    // systemd DBus 常量定义
    constexpr const char* SystemdDestination = "org.freedesktop.systemd1";
    constexpr const char* SystemdManagerPath = "/org/freedesktop/systemd1";
    constexpr const char* SystemdManagerInterface = "org.freedesktop.systemd1.Manager";
    constexpr const char* SystemdUnitInterface = "org.freedesktop.systemd1.Unit";
    constexpr const char* SystemdServiceInterface = "org.freedesktop.systemd1.Service";
    constexpr const char* ServiceSuffix = ".service";

    // 将服务名转换为systemd单元名（追加.service后缀）
    std::string ToUnitName(const std::string &serviceName) {
        if (serviceName.size() > std::strlen(ServiceSuffix) &&
            serviceName.compare(serviceName.size() - std::strlen(ServiceSuffix), std::strlen(ServiceSuffix),
                                ServiceSuffix) == 0) {
            return serviceName;
        }
        return serviceName + ServiceSuffix;
    }
}  // namespace

namespace qifeng::scm {
    DBusManager::DBusManager() : DBusManager(BusType::System) {
    }

    DBusManager::DBusManager(BusType busType) : mBus(nullptr), mConnected(false) {
        int ret = 0;
        if (busType == BusType::User) {
            // 打开用户会话总线连接
            ret = sd_bus_open_user(&mBus);
        } else {
            // 打开系统总线连接
            ret = sd_bus_open_system(&mBus);
        }

        if (ret < 0) {
            SLOG_ERROR << "Failed to open " << (busType == BusType::User ? "user" : "system")
                       << " bus: " << std::strerror(-ret);
            mBus = nullptr;
            mConnected = false;
        } else {
            mConnected = true;
        }
    }

    DBusManager::~DBusManager() {
        if (mBus) {
            sd_bus_unref(mBus);
            mBus = nullptr;
        }
        mConnected = false;
    }

    bool DBusManager::IsConnected() const {
        return mConnected && mBus != nullptr;
    }

    ResultMsg DBusManager::StartUnit(const std::string &serviceName) {
        return CallManagerMethod("StartUnit", serviceName, "replace");
    }

    ResultMsg DBusManager::StopUnit(const std::string &serviceName) {
        return CallManagerMethod("StopUnit", serviceName, "replace");
    }

    ResultMsg DBusManager::RestartUnit(const std::string &serviceName) {
        return CallManagerMethod("RestartUnit", serviceName, "replace");
    }

    ResultMsg DBusManager::GetUnitActiveState(const std::string &serviceName) {
        if (!IsConnected()) {
            return MakeError("DBus is not connected");
        }

        // 获取服务单元的DBus对象路径
        std::string unitPath = GetUnitPath(serviceName);
        if (unitPath.empty()) {
            return MakeError("Failed to get unit path for: " + serviceName);
        }

        // 读取ActiveState属性
        return GetStringProperty(unitPath, SystemdUnitInterface, "ActiveState");
    }

    ResultMsg DBusManager::GetUnitSubState(const std::string &serviceName) {
        if (!IsConnected()) {
            return MakeError("DBus is not connected");
        }

        // 获取服务单元的DBus对象路径
        std::string unitPath = GetUnitPath(serviceName);
        if (unitPath.empty()) {
            return MakeError("Failed to get unit path for: " + serviceName);
        }

        // 读取SubState属性
        return GetStringProperty(unitPath, SystemdUnitInterface, "SubState");
    }

    ResultMsg DBusManager::GetServiceMainPID(const std::string &serviceName) {
        if (!IsConnected()) {
            return MakeError("DBus is not connected");
        }

        // 获取服务单元的DBus对象路径
        std::string unitPath = GetUnitPath(serviceName);
        if (unitPath.empty()) {
            return MakeError("Failed to get unit path for: " + serviceName);
        }

        // 读取MainPID属性（Service接口）
        return GetUint32Property(unitPath, SystemdServiceInterface, "MainPID");
    }

    ResultMsg DBusManager::EnableUnit(const std::string &serviceName) {
        if (!IsConnected()) {
            return MakeError("DBus is not connected");
        }

        std::string unitName = ToUnitName(serviceName);
        sd_bus_error error = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;

        // 调用EnableUnitFiles方法：参数为单元名数组、是否运行时、是否强制
        int ret = sd_bus_call_method(mBus, SystemdDestination, SystemdManagerPath, SystemdManagerInterface,
                                     "EnableUnitFiles", &error, &reply, "asbb",
                                     1,  // 数组长度
                                     unitName.c_str(),
                                     0,  // runtime=false：持久化修改
                                     1   // force=true：强制替换已有符号链接
        );

        if (ret < 0) {
            std::string errMsg = error.message ? error.message : std::strerror(-ret);
            sd_bus_error_free(&error);
            return MakeError("Failed to enable unit " + unitName + ": " + errMsg);
        }

        sd_bus_message_unref(reply);
        sd_bus_error_free(&error);

        // 启用后重载守护进程以使变更生效
        return ReloadDaemon();
    }

    ResultMsg DBusManager::DisableUnit(const std::string &serviceName) {
        if (!IsConnected()) {
            return MakeError("DBus is not connected");
        }

        std::string unitName = ToUnitName(serviceName);
        sd_bus_error error = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;

        // 调用DisableUnitFiles方法：参数为单元名数组、是否运行时
        int ret = sd_bus_call_method(mBus, SystemdDestination, SystemdManagerPath, SystemdManagerInterface,
                                     "DisableUnitFiles", &error, &reply, "asb",
                                     1,  // 数组长度
                                     unitName.c_str(),
                                     0  // runtime=false：持久化修改
        );

        if (ret < 0) {
            std::string errMsg = error.message ? error.message : std::strerror(-ret);
            sd_bus_error_free(&error);
            return MakeError("Failed to disable unit " + unitName + ": " + errMsg);
        }

        sd_bus_message_unref(reply);
        sd_bus_error_free(&error);

        // 禁用后重载守护进程以使变更生效
        return ReloadDaemon();
    }

    ResultMsg DBusManager::ReloadDaemon() {
        if (!IsConnected()) {
            return MakeError("DBus is not connected");
        }

        sd_bus_error error = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;

        // 调用Reload方法：无参数
        int ret = sd_bus_call_method(mBus, SystemdDestination, SystemdManagerPath, SystemdManagerInterface, "Reload",
                                     &error, &reply, "");

        if (ret < 0) {
            std::string errMsg = error.message ? error.message : std::strerror(-ret);
            sd_bus_error_free(&error);
            return MakeError("Failed to reload systemd daemon: " + errMsg);
        }

        sd_bus_message_unref(reply);
        sd_bus_error_free(&error);
        return MakeSuccess();
    }

    ResultMsg DBusManager::CallManagerMethod(const std::string &method, const std::string &serviceName,
                                             const std::string &mode) {
        if (!IsConnected()) {
            return MakeError("DBus is not connected");
        }

        std::string unitName = ToUnitName(serviceName);
        sd_bus_error error = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;

        // 调用systemd Manager方法（StartUnit/StopUnit/RestartUnit）
        // 参数：单元名（string）、模式（string）
        int ret = sd_bus_call_method(mBus, SystemdDestination, SystemdManagerPath, SystemdManagerInterface,
                                     method.c_str(), &error, &reply, "ss", unitName.c_str(), mode.c_str());

        if (ret < 0) {
            std::string errMsg = error.message ? error.message : std::strerror(-ret);
            sd_bus_error_free(&error);
            return MakeError("Failed to " + method + " unit " + unitName + ": " + errMsg);
        }

        sd_bus_message_unref(reply);
        sd_bus_error_free(&error);
        return MakeSuccess();
    }

    std::string DBusManager::GetUnitPath(const std::string &serviceName) {
        std::string unitName = ToUnitName(serviceName);
        sd_bus_error error = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;

        // 调用LoadUnit方法获取服务单元的DBus对象路径
        // LoadUnit与GetUnit的区别：当单元未加载时，LoadUnit会自动加载单元文件；
        // GetUnit仅返回已加载的单元，停止后的服务可能已被卸载导致GetUnit失败
        int ret = sd_bus_call_method(mBus, SystemdDestination, SystemdManagerPath, SystemdManagerInterface, "LoadUnit",
                                     &error, &reply, "s", unitName.c_str());

        if (ret < 0) {
            std::string errMsg = error.message ? error.message : std::strerror(-ret);
            SLOG_ERROR << "Failed to load unit " << unitName << ": " << errMsg;
            sd_bus_error_free(&error);
            return "";
        }

        // 读取返回的对象路径
        const char* path = nullptr;
        ret = sd_bus_message_read(reply, "o", &path);
        if (ret < 0) {
            SLOG_ERROR << "Failed to read unit path: " << std::strerror(-ret);
            sd_bus_message_unref(reply);
            sd_bus_error_free(&error);
            return "";
        }

        std::string result(path);
        sd_bus_message_unref(reply);
        sd_bus_error_free(&error);
        return result;
    }

    ResultMsg DBusManager::GetStringProperty(const std::string &objectPath, const std::string &interface,
                                             const std::string &property) {
        sd_bus_error error = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;

        // 通过sd_bus_get_property读取字符串属性
        int ret = sd_bus_get_property(mBus, SystemdDestination, objectPath.c_str(), interface.c_str(), property.c_str(),
                                      &error, &reply, "s");

        if (ret < 0) {
            std::string errMsg = error.message ? error.message : std::strerror(-ret);
            sd_bus_error_free(&error);
            return MakeError("Failed to get property " + property + ": " + errMsg);
        }

        // 读取属性值
        const char* value = nullptr;
        ret = sd_bus_message_read(reply, "s", &value);
        if (ret < 0) {
            sd_bus_message_unref(reply);
            sd_bus_error_free(&error);
            return MakeError("Failed to read property " + property + ": " + std::strerror(-ret));
        }

        ResultMsg result = ResultMsg {0, std::string(value)};
        sd_bus_message_unref(reply);
        sd_bus_error_free(&error);
        return result;
    }

    ResultMsg DBusManager::GetUint32Property(const std::string &objectPath, const std::string &interface,
                                             const std::string &property) {
        sd_bus_error error = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;

        // 通过sd_bus_get_property读取uint32属性
        int ret = sd_bus_get_property(mBus, SystemdDestination, objectPath.c_str(), interface.c_str(), property.c_str(),
                                      &error, &reply, "u");

        if (ret < 0) {
            std::string errMsg = error.message ? error.message : std::strerror(-ret);
            sd_bus_error_free(&error);
            return MakeError("Failed to get property " + property + ": " + errMsg);
        }

        // 读取属性值
        uint32_t value = 0;
        ret = sd_bus_message_read(reply, "u", &value);
        if (ret < 0) {
            sd_bus_message_unref(reply);
            sd_bus_error_free(&error);
            return MakeError("Failed to read property " + property + ": " + std::strerror(-ret));
        }

        ResultMsg result = ResultMsg {0, std::to_string(value)};
        sd_bus_message_unref(reply);
        sd_bus_error_free(&error);
        return result;
    }
}  // namespace qifeng::scm
