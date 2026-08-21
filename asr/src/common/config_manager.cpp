/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string_view>

#include "common/config_define.h"
#include "common/config_manager.h"
#include "common/logger.h"

ConfigManager& ConfigManager::GetInstance() {
    static ConfigManager Instance;
    return Instance;
}

ConfigManager::~ConfigManager() {
    if (mReloadThread && mReloadThread->joinable()) {
        mStopReloadThread = true;
        mReloadThread->join();
    }
}

bool ConfigManager::Initialize(const std::string& serviceName, const std::string& configPath, uint32_t reloadInterval) {
    try {
        // 检查是否已初始化
        if (mReloadThread) {
            SLOG_WARN << "ConfigManager already initialized, skipping";
            return true;
        }

        // 检查参数
        if (serviceName.empty()) {
            SLOG_ERROR << "Error: serviceName is empty";
            return false;
        }

        if (configPath.empty()) {
            SLOG_ERROR << "Error: configPath is empty";
            return false;
        }

        // 初始化日志系统
        if (!Logger::GetInstance().IsInitialized()) {
            if (!Logger::GetInstance().Initialize(serviceName)) {
                SLOG_ERROR << "Error: Failed to initialize logger";
                return false;
            }
        }
        // 只注册一次系统内所有配置的值约束
        std::call_once(mRegisterDefaultsFlag, [this]() { RegisterAllDefaults(); });

        // 保存配置文件路径和重载间隔
        mGlobalConfigFile = configPath;
        mReloadIntervalMs = reloadInterval;

        // 首次加载配置
        if (!LoadConfig()) {
            SLOG_ERROR << "Error: Failed to load config files";
            return false;
        }

        mReloadThread = std::make_unique<std::thread>([this] { ReloadThreadFunc(); });

        return true;
    } catch (const std::exception& e) {
        SLOG_ERROR << "Exception in ConfigManager::Initialize: " << e.what();
        return false;
    } catch (...) {
        SLOG_ERROR << "Unknown exception in ConfigManager::Initialize";
        return false;
    }
}

bool ConfigManager::LoadConfig() {
    ConfigCache newCache;

    // 加载配置
    if (!ParseConfigFile(mGlobalConfigFile, newCache)) {
        SLOG_ERROR << "Failed to parse global config: " << mGlobalConfigFile;
        return false;  // 已有日志
    }

    // 检查是否切换了日志等级，如果切换了，更新日志等级
    UpdateLogLevel(newCache);

    // 原子性地更新缓存
    {
        const std::lock_guard<std::mutex> lock(mCacheMutex);
        mConfigCache.swap(newCache);
    }

    mLastLoadTime = std::chrono::system_clock::now();
    return true;
}

void ConfigManager::ParseYamlNode(const YAML::Node& node, const std::string& currentPath, ConfigCache& cache) {
    try {
        auto storeValue = [&](const std::string& value) {
            const size_t lastDotPos = currentPath.find_last_of('.');
            if (lastDotPos == std::string::npos) {
                cache["default"][currentPath] = value;
                return;
            }

            const std::string section = currentPath.substr(0, lastDotPos);
            const std::string key = currentPath.substr(lastDotPos + 1);
            cache[section][key] = value;
        };

        if (node.IsScalar()) {
            storeValue(node.as<std::string>());
            return;
        }

        if (node.IsSequence()) {
            storeValue("[sequence]");
            return;
        }

        if (!node.IsMap()) {
            SLOG_ERROR << "YAML node is not a map at path: " << currentPath;  // 早返回异常分支
            return;
        }

        for (const auto& kv : node) {
            if (!kv.first.IsScalar()) {
                SLOG_ERROR << "YAML map key is not scalar at path: " << currentPath;  // continue异常分支
                continue;
            }

            const std::string keyStr = kv.first.as<std::string>();
            const std::string newPath = currentPath.empty() ? keyStr : currentPath + "." + keyStr;
            ParseYamlNode(kv.second, newPath, cache);
        }
    } catch (const YAML::Exception& e) {
        SLOG_ERROR << "Error parsing YAML node: " << e.what();
        return;
    } catch (const std::exception& e) {
        SLOG_ERROR << "Error processing YAML node: " << e.what();
        return;
    }
}

bool ConfigManager::ParseConfigFile(const std::string& filePath, ConfigCache& cache) {
    // 检查文件路径是否为空
    if (filePath.empty()) {
        SLOG_ERROR << "Error: Empty config file path";
        return false;
    }

    // 尝试打开文件，确保文件存在且可访问
    std::ifstream testFile(filePath);
    if (!testFile.is_open()) {
        SLOG_ERROR << "Cannot open config file: " << filePath;
        return false;
    }
    testFile.close();

    // 根据文件扩展名选择解析器
    const size_t dotPos = filePath.find_last_of(".");
    if (dotPos == std::string::npos || dotPos == filePath.size() - 1) {
        // 没有扩展名或扩展名为空，使用INI解析器
        SLOG_ERROR << "Warning: No file extension, using INI parser";
        return ParseIniFile(filePath, cache);
    }

    std::string ext = filePath.substr(dotPos + 1);

    // 将扩展名转换为小写
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == "yaml" || ext == "yml") {
        // YAML文件解析
        try {
            const YAML::Node config = YAML::LoadFile(filePath);
            ParseYamlNode(config, "", cache);
            return true;
        } catch (const YAML::Exception& e) {
            SLOG_ERROR << "Error parsing YAML file: " << filePath << ", error: " << e.what();
            return false;
        } catch (const std::exception& e) {
            SLOG_ERROR << "Error loading YAML file: " << filePath << ", error: " << e.what();
            return false;
        }
    } else {
        // 使用INI解析器
        return ParseIniFile(filePath, cache);
    }
}

bool ConfigManager::ParseIniFile(const std::string& filePath, ConfigCache& cache) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        SLOG_ERROR << "Cannot open INI config file: " << filePath;
        return false;
    }

    std::string line;
    std::string currentSection = "default";
    cache[currentSection] = {};

    while (std::getline(file, line)) {
        ParseLine(line, currentSection, cache);
    }

    if (file.bad()) {
        SLOG_ERROR << "Error reading INI config file: " << filePath;
        return false;
    }

    return true;
}

void ConfigManager::ParseLine(const std::string& line, std::string& currentSection, ConfigCache& cache) {
    std::string trimmedLine = line;
    Trim(trimmedLine);

    // 跳过空行和注释
    if (trimmedLine.empty() || trimmedLine[0] == ';' || trimmedLine[0] == '#') {
        return;
    }

    // 处理段
    if (trimmedLine[0] == '[' && trimmedLine.find(']') != std::string::npos) {
        const size_t endPos = trimmedLine.find(']');
        currentSection = trimmedLine.substr(1, endPos - 1);
        Trim(currentSection);
        cache[currentSection];  // 确保段存在
        return;
    }
    if (trimmedLine[0] == '[' && trimmedLine.find(']') == std::string::npos) {
        SLOG_ERROR << "Malformed section header in INI: " << trimmedLine;
        return;
    }

    // 处理键值对
    const size_t pos = trimmedLine.find('=');
    if (pos != std::string::npos) {
        std::string key = trimmedLine.substr(0, pos);
        std::string value = trimmedLine.substr(pos + 1);

        Trim(key);
        Trim(value);

        if (!key.empty()) {
            cache[currentSection][key] = value;
        } else {
            SLOG_ERROR << "Empty key in INI line: " << trimmedLine;
        }
    } else {
        SLOG_ERROR << "Malformed key-value line in INI: " << trimmedLine;
    }
}

void ConfigManager::ReloadThreadFunc() {
    auto lastReloadTime = std::chrono::system_clock::now();

    while (!mStopReloadThread) {
        auto now = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReloadTime).count();

        if (elapsed >= mReloadIntervalMs) {
            // 只在日志系统初始化时使用日志
            if (Logger::GetInstance().IsInitialized()) {
                Reload();
            }
            lastReloadTime = now;
        }

        // 每秒检查一次
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

bool ConfigManager::Reload() {
    return LoadConfig();
}

// 注册所有默认值, 初始硬编码，全局配置，避免出现遗漏、篡改
void ConfigManager::RegisterAllDefaults() {
    // ===================== general =====================
    RegisterNumericConfig(General::Section, General::KeyLogLevel,
                          ValueConstraint<int> {General::ConstraintLogLevelMin, General::ConstraintLogLevelMax, 2});
    // ===================== database =====================
    RegisterBoolConfig(Database::Section, Database::KeySqliteEnable, true);

    RegisterStringConfig(Database::Section, Database::KeySqliteDbPath, Database::DefaultSqliteDbPath);
    // ===================== service =====================
    RegisterStringConfig(Service::Section, Service::KeyName, "");
    RegisterStringConfig(Service::Section, Service::KeyRegisterType, Service::DefaultRegisterType);
    RegisterNumericConfig(Service::Section, Service::KeyPortStart,
                          ValueConstraint<int> {Service::ConstraintPortStartMin, Service::ConstraintPortStartMax,
                                                Service::DefaultPortStart});
    RegisterNumericConfig(
        Service::Section, Service::KeyPortEnd,
        ValueConstraint<int> {Service::ConstraintPortEndMin, Service::ConstraintPortEndMax, Service::DefaultPortEnd});
    RegisterStringConfig(Service::Section, Service::KeyFileUrlPrefix, Service::DefaultFileUrlPrefix);
    RegisterStringConfig(Service::Section, Service::KeyFileStoragePath, Service::DefaultFileStoragePath);
    // ===================== models =====================
    RegisterStringConfig(Models::Section, Models::KeyLlmModel, Models::DefaultLlmModel);
    RegisterStringConfig(Models::Section, Models::KeyLlmApiKey, Models::DefaultLlmApiKey);
    RegisterStringConfig(Models::Section, Models::KeyLlmApiBase, Models::DefaultLlmApiBase);
    // ===================== ssh =====================
    RegisterStringConfig(Ssh::Section, Ssh::KeyUser, Ssh::DefaultUser);
    RegisterStringConfig(Ssh::Section, Ssh::KeyIp, Ssh::DefaultIp);
    RegisterStringConfig(Ssh::Section, Ssh::KeyPrivateKeyPath, Ssh::DefaultPrivateKeyPath);
    RegisterNumericConfig(Ssh::Section, Ssh::KeyConnectionTimeout,
                          ValueConstraint<int> {1000, 3600000, Ssh::DefaultConnectionTimeout});
    RegisterNumericConfig(Ssh::Section, Ssh::KeyExecutionTimeout,
                          ValueConstraint<int> {1000, 3600000, Ssh::DefaultExecutionTimeout});
    // ===================== auth =====================
    RegisterStringConfig(Auth::Section, Auth::KeyAdminKey, Auth::DefaultAdminKey);
    // ===================== dag =====================
    RegisterNumericConfig(Dag::Section, Dag::KeyComputeThreads,
                          ValueConstraint<int> {Dag::ConstraintComputeThreadsMin, Dag::ConstraintComputeThreadsMax,
                                                Dag::DefaultComputeThreads});
    // ===================== aas =====================
    RegisterNumericConfig(
        Aas::Section, Aas::KeySampleRate,
        ValueConstraint<int> {Aas::ConstraintSampleRateMin, Aas::ConstraintSampleRateMax, Aas::DefaultSampleRate});
    RegisterNumericConfig(
        Aas::Section, Aas::KeyChannels,
        ValueConstraint<int> {Aas::ConstraintChannelsMin, Aas::ConstraintChannelsMax, Aas::DefaultChannels});
    RegisterNumericConfig(
        Aas::Section, Aas::KeyBitDepth,
        ValueConstraint<int> {Aas::ConstraintBitDepthMin, Aas::ConstraintBitDepthMax, Aas::DefaultBitDepth});
}

// 配置项注册实现
// 注意: 约束注册必须在初始化前进行，初始话ConfigManager后，约束为只读类，避免并发冲突
// 建议放在匿名 namespace 或 private 静态函数
static inline std::string MakeConfigKey(std::string_view section, std::string_view key) {
    std::string result;
    result.reserve(section.size() + 1 + key.size());  // 避免二次分配
    result.append(section);
    result.push_back('.');
    result.append(key);
    return result;
}

// ===================== Numeric<int> =====================
void ConfigManager::RegisterNumericConfig(const std::string_view& section, const std::string_view& key,
                                          const ValueConstraint<int>& constraint) {
    mIntConstraints.emplace(MakeConfigKey(section, key), constraint);
}

// ===================== Numeric<double> =====================
void ConfigManager::RegisterNumericConfig(const std::string_view& section, const std::string_view& key,
                                          const ValueConstraint<double>& constraint) {
    mDoubleConstraints.emplace(MakeConfigKey(section, key), constraint);
}

// ===================== String =====================
void ConfigManager::RegisterStringConfig(const std::string_view& section, const std::string_view& key,
                                         const std::string_view& defaultVal) {
    mStringDefaults.emplace(MakeConfigKey(section, key),
                            std::string(defaultVal)  // 这里必须落地为 owning string
    );
}

// ===================== Bool =====================
void ConfigManager::RegisterBoolConfig(const std::string_view& section, const std::string_view& key, bool defaultVal) {
    mBoolDefaults.emplace(MakeConfigKey(section, key), defaultVal);
}

// 配置读取实现
std::string ConfigManager::GetRawConfig(const std::string& section, const std::string& key) const {
    const std::lock_guard<std::mutex> lock(mCacheMutex);

    auto sectionIt = mConfigCache.find(section);
    if (sectionIt == mConfigCache.end()) {
        // 打印日志
        SLOG_DEBUG << "Config section not found: " << section;
        return "";
    }

    auto keyIt = sectionIt->second.find(key);
    if (keyIt == sectionIt->second.end()) {
        // 打印日志
        SLOG_WARN << "Config key not found: " << section << "." << key;
        return "";
    }

    // 支持 ${ENV_VAR} 环境变量占位符解析（敏感配置仅留占位符，禁止明文入库）
    return ResolveEnvPlaceholder(keyIt->second);
}

// 解析 ${ENV_VAR} 环境变量占位符；非占位符原样返回
// 约定: 占位符必须整体等于 "${NAME}" 格式；环境变量未设置时返回空串（fail-closed，
// 由调用方回退默认值，禁止静默降级为明文）
std::string ConfigManager::ResolveEnvPlaceholder(const std::string& value) const {
    constexpr std::string_view kPrefix = "${";
    constexpr std::string_view kSuffix = "}";

    const bool isPlaceholder = value.size() > kPrefix.size() + kSuffix.size() &&
                               value.rfind(kPrefix, 0) == 0 && value.back() == kSuffix.back();
    if (!isPlaceholder) {
        return value;
    }

    const std::string envName = value.substr(kPrefix.size(), value.size() - kPrefix.size() - kSuffix.size());
    if (envName.empty()) {
        SLOG_WARN << "Empty environment variable placeholder in config";
        return "";
    }

    const char* envValue = std::getenv(envName.c_str());
    if (envValue == nullptr) {
        SLOG_WARN << "Environment variable not set for config placeholder: " << envName;
        return "";
    }
    return std::string(envValue);
}

int ConfigManager::GetIntWithConstraint(const std::string& section, const std::string& key) const {
    const std::string constraintKey = section + "." + key;
    auto constraintIt = mIntConstraints.find(constraintKey);

    if (constraintIt == mIntConstraints.end()) {
        // 没有约束，使用简单版本
        SLOG_WARN << "No constraint found for config: " << section << "." << key;
        return GetInt(section, key, 0);
    }

    const auto& constraint = constraintIt->second;
    const std::string valueStr = GetRawConfig(section, key);

    if (valueStr.empty()) {
        // 打印日志
        SLOG_WARN << "Config value is empty for: " << section << "." << key
                  << ", using default: " << constraint.defaultValue;
        return constraint.defaultValue;
    }

    try {
        const int value = std::stoi(valueStr);

        // 应用约束
        if (value < constraint.minValue) {
            SLOG_ERROR << "Value " << value << " is below minimum " << constraint.minValue << " for " << section << "."
                       << key;
            return constraint.minValue;
        }

        if (value > constraint.maxValue) {
            SLOG_ERROR << "Value " << value << " is above maximum " << constraint.maxValue << " for " << section << "."
                       << key;
            return constraint.maxValue;
        }

        return value;
    } catch (const std::exception& e) {
        SLOG_ERROR << "Invalid integer value for " << section << "." << key << ": " << valueStr
                   << ", using default: " << constraint.defaultValue;
        return constraint.defaultValue;
    }
}

int ConfigManager::GetIntWithConstraint(const std::string& section, const std::string& key,
                                        int defaultValue) const {
    const std::string constraintKey = section + "." + key;
    auto constraintIt = mIntConstraints.find(constraintKey);

    if (constraintIt == mIntConstraints.end()) {
        // 没有约束项：配置存在则读取，否则返回调用方默认值
        return GetInt(section, key, defaultValue);
    }

    const auto& constraint = constraintIt->second;
    const std::string valueStr = GetRawConfig(section, key);

    if (valueStr.empty()) {
        SLOG_WARN << "Config value is empty for: " << section << "." << key
                  << ", using default: " << constraint.defaultValue;
        return constraint.defaultValue;
    }

    try {
        const int value = std::stoi(valueStr);

        // 应用约束
        if (value < constraint.minValue) {
            SLOG_ERROR << "Value " << value << " is below minimum " << constraint.minValue << " for " << section << "."
                       << key;
            return constraint.minValue;
        }

        if (value > constraint.maxValue) {
            SLOG_ERROR << "Value " << value << " is above maximum " << constraint.maxValue << " for " << section << "."
                       << key;
            return constraint.maxValue;
        }

        return value;
    } catch (const std::exception& e) {
        SLOG_ERROR << "Invalid integer value for " << section << "." << key << ": " << valueStr
                   << ", using default: " << constraint.defaultValue;
        return constraint.defaultValue;
    }
}

double ConfigManager::GetDoubleWithConstraint(const std::string& section, const std::string& key) const {
    const std::string constraintKey = section + "." + key;
    auto constraintIt = mDoubleConstraints.find(constraintKey);

    if (constraintIt == mDoubleConstraints.end()) {
        // 没有约束，使用简单版本
        SLOG_WARN << "No constraint found for config: " << section << "." << key;
        return GetDouble(section, key, 0.0);
    }

    const auto& constraint = constraintIt->second;
    const std::string valueStr = GetRawConfig(section, key);

    if (valueStr.empty()) {
        // 打印日志
        SLOG_WARN << "Config value is empty for: " << section << "." << key
                  << ", using default: " << constraint.defaultValue;
        return constraint.defaultValue;
    }

    try {
        const double value = std::stod(valueStr);

        // 应用约束
        if (value < constraint.minValue) {
            SLOG_ERROR << "Value " << value << " is below minimum " << constraint.minValue << " for " << section << "."
                       << key;
            return constraint.minValue;
        }

        if (value > constraint.maxValue) {
            SLOG_ERROR << "Value " << value << " is above maximum " << constraint.maxValue << " for " << section << "."
                       << key;
            return constraint.maxValue;
        }

        return value;
    } catch (const std::exception& e) {
        SLOG_ERROR << "Invalid double value for " << section << "." << key << ": " << valueStr
                   << ", using default: " << constraint.defaultValue;
        return constraint.defaultValue;
    }
}

bool ConfigManager::GetBoolWithConstraint(const std::string& section, const std::string& key) const {
    const std::string defaultKey = section + "." + key;
    auto defaultIt = mBoolDefaults.find(defaultKey);
    const bool defaultValue = (defaultIt != mBoolDefaults.end()) ? defaultIt->second : false;

    const std::string valueStr = GetRawConfig(section, key);
    if (valueStr.empty()) {
        // 打印日志
        SLOG_WARN << "Config value is empty for: " << section << "." << key << ", using default: " << std::boolalpha
                  << defaultValue;
        return defaultValue;
    }

    // 转换为小写进行比较
    std::string lowerValue = valueStr;
    std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(), ::tolower);

    if (lowerValue == "true" || lowerValue == "1" || lowerValue == "yes" || lowerValue == "on") {
        return true;
    } else if (lowerValue == "false" || lowerValue == "0" || lowerValue == "no" || lowerValue == "off") {
        return false;
    } else {
        SLOG_ERROR << "Invalid boolean value for " << section << "." << key << ": " << valueStr
                   << ", using default: " << std::boolalpha << defaultValue;
        return defaultValue;
    }
}

// 公共读取接口
std::string ConfigManager::GetString(const std::string& section, const std::string& key,
                                     const std::string& defaultValue) const {
    std::string value = GetRawConfig(section, key);
    if (value.empty()) {
        const std::string defaultKey = section + "." + key;
        auto defaultIt = mStringDefaults.find(defaultKey);
        SLOG_WARN << "Config value is empty for: " << section << "." << key
                  << ", using default: " << ((defaultIt != mStringDefaults.end()) ? defaultIt->second : defaultValue);
        return (defaultIt != mStringDefaults.end()) ? defaultIt->second : defaultValue;
    }
    return value;
}

int ConfigManager::GetInt(const std::string& section, const std::string& key, int defaultValue) const {
    const std::string valueStr = GetRawConfig(section, key);
    if (valueStr.empty()) {
        SLOG_DEBUG << "Config value is empty for: " << section << "." << key << ", using default: " << defaultValue;
        return defaultValue;
    }

    try {
        return std::stoi(valueStr);
    } catch (const std::exception& e) {
        SLOG_ERROR << "Invalid integer value for " << section << "." << key << ": " << valueStr
                   << ", using default: " << defaultValue;
        return defaultValue;
    }
}

double ConfigManager::GetDouble(const std::string& section, const std::string& key, double defaultValue) const {
    const std::string valueStr = GetRawConfig(section, key);
    if (valueStr.empty()) {
        SLOG_WARN << "Config value is empty for: " << section << "." << key << ", using default: " << defaultValue;
        return defaultValue;
    }

    try {
        return std::stod(valueStr);
    } catch (const std::exception& e) {
        SLOG_ERROR << "Invalid double value for " << section << "." << key << ": " << valueStr
                   << ", using default: " << defaultValue;
        return defaultValue;
    }
}

bool ConfigManager::GetBool(const std::string& section, const std::string& key, bool defaultValue) const {
    const std::string valueStr = GetRawConfig(section, key);
    if (valueStr.empty()) {
        SLOG_WARN << "Config value is empty for: " << section << "." << key << ", using default: " << std::boolalpha
                  << defaultValue;
        return defaultValue;
    }

    // 转换为小写进行比较
    std::string lowerValue = valueStr;
    std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(), ::tolower);

    if (lowerValue == "true" || lowerValue == "1" || lowerValue == "yes" || lowerValue == "on") {
        return true;
    } else if (lowerValue == "false" || lowerValue == "0" || lowerValue == "no" || lowerValue == "off") {
        return false;
    } else {
        SLOG_ERROR << "Invalid boolean value for " << section << "." << key << ": " << valueStr
                   << ", using default: " << std::boolalpha << defaultValue;
        return defaultValue;
    }
}

void ConfigManager::Trim(std::string& str) {
    str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](unsigned char ch) { return !std::isspace(ch); }));

    str.erase(std::find_if(str.rbegin(), str.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(),
              str.end());
}

void ConfigManager::UpdateLogLevel(ConfigCache& newCache) {
    const std::string newLogLvl = newCache[std::string(General::Section)][std::string(General::KeyLogLevel)];
    const std::string oldLogLvl = mConfigCache[std::string(General::Section)][std::string(General::KeyLogLevel)];
    if (newLogLvl != oldLogLvl) {
        try {
            const int value = std::stoi(newLogLvl);
            Logger::GetInstance().SetLevel(static_cast<Logger::Level>(value));
        } catch (const std::exception& e) {
            SLOG_ERROR << "Invalid log level value for " << General::Section << "." << General::KeyLogLevel << ": "
                       << newLogLvl << ", using default: " << oldLogLvl;
        }
    }
}

const std::string ConfigManager::GetServiceName() {
    return mServiceName;
}
