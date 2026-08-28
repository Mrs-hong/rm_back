/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_COMMON_CONFIG_MANAGER_H
#define QIFENG_FRAMEWORK_INCLUDE_COMMON_CONFIG_MANAGER_H

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <yaml-cpp/yaml.h>

/**
 * @brief 配置项数值约束
 */
template <typename T>
struct ValueConstraint {
    T minValue;
    T maxValue;
    T defaultValue;

    ValueConstraint(T min, T max, T def) : minValue(min), maxValue(max), defaultValue(def) {};

    ValueConstraint() {};
};

/**
 * @brief 配置管理器类
 */
class ConfigManager {
public:
    /**
     * @brief 获取配置管理器单例实例
     */
    static ConfigManager& GetInstance();

    /**
     * @brief 初始化配置管理器
     * @param serviceName 服务名称
     * @param configPath 配置文件路径
     * @param reloadIntervalMs 重新加载间隔（毫秒）
     * @return 是否初始化成功
     */
    bool Initialize(const std::string& serviceName, const std::string& configPath, uint32_t reloadIntervalMs = 300000);

    /**
     * @brief 快速获取配置值
     */
    template <typename T>
    T GetConfig(const std::string& section, const std::string& key) const;

    /**
     * @brief 获取字符串配置值
     */
    std::string GetString(const std::string& section, const std::string& key,
                          const std::string& defaultValue = "") const;

    /**
     * @brief 获取整数配置值
     */
    int GetInt(const std::string& section, const std::string& key, int defaultValue = 0) const;
    /**
     * @brief 获取整数配置值（带约束）
     */
    int GetIntWithConstraint(const std::string& section, const std::string& key) const;

    /**
     * @brief 获取浮点数配置值
     */
    double GetDouble(const std::string& section, const std::string& key, double defaultValue = 0.0) const;
    /**
     * @brief 获取浮点数配置值（带约束）
     */
    double GetDoubleWithConstraint(const std::string& section, const std::string& key) const;

    /**
     * @brief 获取布尔配置值
     */
    bool GetBool(const std::string& section, const std::string& key, bool defaultValue = false) const;
    /**
     * @brief 获取布尔配置值（带约束）
     */
    bool GetBoolWithConstraint(const std::string& section, const std::string& key) const;

    /**
     * @brief 强制重新加载配置
     */
    bool Reload();

private:
    ConfigManager() : mReloadIntervalMs(0), mStopReloadThread(false) {};
    ~ConfigManager();
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    ConfigManager(ConfigManager&&) = delete;
    ConfigManager& operator=(ConfigManager&&) = delete;

    // 配置缓存类型
    using ConfigCache = std::unordered_map<std::string, std::unordered_map<std::string, std::string>>;

    // 内部方法
    bool LoadConfig();
    void ReloadThreadFunc();
    bool ParseConfigFile(const std::string& filePath, ConfigCache& cache);
    bool ParseIniFile(const std::string& filePath, ConfigCache& cache);
    void ParseYamlNode(const YAML::Node& node, const std::string& currentPath, ConfigCache& cache);
    void ParseLine(const std::string& line, std::string& currentSection, ConfigCache& cache);
    void Trim(std::string& str);

    // 配置值处理
    std::string GetRawConfig(const std::string& section, const std::string& key) const;
    // 解析 ${ENV_VAR} 环境变量占位符；非占位符原样返回；环境变量未设置时返回空串
    std::string ResolveEnvPlaceholder(const std::string& value) const;

    // 全局注册所有默认值，所有默认值在initialize时注册, 后续通过getConfig获取时会进行约束校验
    void RegisterAllDefaults();
    void RegisterNumericConfig(const std::string_view& section, const std::string_view& key,
                               const ValueConstraint<int>& constraint);
    void RegisterNumericConfig(const std::string_view& section, const std::string_view& key,
                               const ValueConstraint<double>& constraint);
    void RegisterStringConfig(const std::string_view& section, const std::string_view& key,
                              const std::string_view& defaultVal = "");
    void RegisterBoolConfig(const std::string_view& section, const std::string_view& key, bool defaultVal = false);

    // 特殊：更新日志等级
    void UpdateLogLevel(ConfigCache& newCache);
    const std::string GetServiceName();

private:
    std::string mGlobalConfigFile;
    std::string mServiceName;

    // 合并后的配置缓存
    ConfigCache mConfigCache;

    // 配置约束
    std::unordered_map<std::string, ValueConstraint<int>> mIntConstraints;
    std::unordered_map<std::string, ValueConstraint<double>> mDoubleConstraints;
    std::unordered_map<std::string, bool> mBoolDefaults;
    std::unordered_map<std::string, std::string> mStringDefaults;

    std::chrono::system_clock::time_point mLastLoadTime;
    uint32_t mReloadIntervalMs;

    std::unique_ptr<std::thread> mReloadThread;
    std::atomic<bool> mStopReloadThread;
    mutable std::mutex mCacheMutex;

    // 保证RegisterAllDefaults只调用一次
    mutable std::once_flag mRegisterDefaultsFlag;
};

#define CONFIG_MANAGER ConfigManager::GetInstance()

#endif  // QIFENG_FRAMEWORK_INCLUDE_COMMON_CONFIG_MANAGER_H
