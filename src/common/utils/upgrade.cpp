/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/utils/upgrade.h"
#include "common/utils/file.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace qifeng::scm::utils {
    /**
     * @brief 获取当前本地时间的格式化字符串
     * @return std::string 格式 "YYYY-MM-DD HH:MM:SS"
     */
    std::string GetCurrentTimeString() {
        auto now = std::chrono::system_clock::now();
        std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
        struct tm tmStruct {};
        localtime_r(&nowTime, &tmStruct);
        std::ostringstream oss;
        oss << std::put_time(&tmStruct, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    /**
     * @brief 写入升级结果 JSON 文件
     * @details 手动拼接 JSON（避免引入 JsonCpp 依赖到 utils），
     *          自动创建父目录，覆盖写。格式对齐 upgrade_result.json 模板。
     */
    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg WriteUpgradeResult(const std::string &resultPath, bool success, const std::string &upgradeTime,
                                const std::string &upgradeVersion, const std::string &defeatReason) {
        if (resultPath.empty()) {
            return MakeError("Result path is empty");
        }

        // 确保父目录存在
        namespace fs = std::filesystem;
        fs::path parentPath = fs::path(resultPath).parent_path();
        if (!parentPath.empty()) {
            auto dirRet = CreateDirectory(parentPath.string());
            if (!dirRet.IsDefaultSuccess()) {
                return MakeError("Failed to create result directory: " + parentPath.string() + " : " + dirRet.msg);
            }
        }

        // 拼接 JSON 内容（defeatReason 中可能含特殊字符，做基本转义）
        std::string escapedReason;
        escapedReason.reserve(defeatReason.size());
        for (char ch : defeatReason) {
            switch (ch) {
                case '"':  escapedReason += "\\\""; break;
                case '\\': escapedReason += "\\\\"; break;
                case '\n': escapedReason += "\\n";  break;
                case '\r': escapedReason += "\\r";  break;
                case '\t': escapedReason += "\\t";  break;
                default:   escapedReason += ch;     break;
            }
        }

        std::ostringstream oss;
        oss << "{\n";
        oss << "    \"upgrade_success\": " << (success ? "true" : "false") << ",\n";
        oss << "    \"upgrade_time\": \"" << upgradeTime << "\",\n";
        oss << "    \"upgrade_version\": \"" << upgradeVersion << "\",\n";
        oss << "    \"defeat_reason\": \"" << escapedReason << "\"\n";
        oss << "}\n";

        std::ofstream ofs(resultPath, std::ios::trunc);
        if (!ofs.is_open()) {
            return MakeError("Failed to open result file: " + resultPath);
        }
        ofs << oss.str();
        ofs.close();
        return MakeSuccess();
    }
}  // namespace qifeng::scm::utils
