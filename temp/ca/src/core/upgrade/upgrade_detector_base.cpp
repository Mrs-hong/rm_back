//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "core/upgrade/upgrade_detector_base.h"

#include "common/utils/verify.h"

namespace qifeng_ca {

    // 将版本号按 '.' 分割为数字分量
    static std::vector<long long> SplitVersion(const std::string &version) {
        std::vector<long long> parts;
        std::string num;
        for (char ch : version) {
            if (ch == '.') {
                try {
                    parts.push_back(std::stoll(num));
                } catch (...) {
                    parts.push_back(0);
                }
                num.clear();
            } else if (std::isdigit(static_cast<unsigned char>(ch))) {
                num.push_back(ch);
            } else {
                // 遇到非数字非点字符, 终止解析(版本段结束)
                break;
            }
        }
        if (!num.empty()) {
            try {
                parts.push_back(std::stoll(num));
            } catch (...) {
                parts.push_back(0);
            }
        }
        if (parts.empty()) {
            parts.push_back(0);
        }
        return parts;
    }

    int CompareVersion(const std::string &a, const std::string &b) {
        auto pa = SplitVersion(a);
        auto pb = SplitVersion(b);
        size_t len = std::max(pa.size(), pb.size());
        for (size_t i = 0; i < len; ++i) {
            long long va = (i < pa.size()) ? pa[i] : 0;
            long long vb = (i < pb.size()) ? pb[i] : 0;
            if (va < vb) {
                return -1;
            }
            if (va > vb) {
                return 1;
            }
        }
        return 0;
    }

    bool IsNewerVersion(const std::string &candidate, const std::string &current) {
        return CompareVersion(candidate, current) > 0;
    }

    std::string ParseVersionFromFileName(const std::string &fileName) {
        // 匹配最后一个 "_upgrade_" 之后、扩展名之前的版本段
        // 形如 "qifeng_xxx_upgrade_1.0.7.tar.gz" → "1.0.7"
        std::string marker = "_upgrade_";
        auto pos = fileName.rfind(marker);
        if (pos == std::string::npos) {
            return "";
        }
        pos += marker.size();
        // 去除已知扩展名(.tar.gz/.tgz/.tar, 大小写不敏感), 保留完整点分版本号 "1.0.7"
        auto stripExt = [](const std::string &name) {
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.size() > 7 && lower.compare(lower.size() - 7, 7, ".tar.gz") == 0) {
                return name.substr(0, name.size() - 7);
            }
            if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".tgz") == 0) {
                return name.substr(0, name.size() - 4);
            }
            if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".tar") == 0) {
                return name.substr(0, name.size() - 4);
            }
            return name;
        };
        std::string version = stripExt(fileName.substr(pos));
        // 校验版本段至少包含一个数字
        if (std::find_if(version.begin(), version.end(),
                         [](unsigned char c) { return std::isdigit(c); }) == version.end()) {
            return "";
        }
        return version;
    }

    const PackageInfo *SelectLatestCandidate(const std::vector<PackageInfo> &candidates,
                                             const std::string &currentVersion) {
        const PackageInfo *latest = nullptr;
        for (const auto &cand : candidates) {
            // 取版本号不低于当前的候选: 相等亦接受(同主版本重升级场景, 由子版本号 U_VERSION 区分)
            if (CompareVersion(cand.package_version, currentVersion) < 0) {
                continue;
            }
            if (latest == nullptr || CompareVersion(cand.package_version, latest->package_version) > 0) {
                latest = &cand;
            }
        }
        return latest;
    }

    Status UpgradeDetectorBase::VerifyUpgradePackage(const std::string &packagePath, const std::string &sha256Path) {
        return Verify::VerifyUpgradePackage(packagePath, sha256Path);
    }

}  // namespace qifeng_ca
