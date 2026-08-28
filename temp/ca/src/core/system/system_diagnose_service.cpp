//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"

#include "common/common.h"
#include "common/config/device_config.h"
#include "common/config/diagnose_config.h"
#include "common/config/tmp_path_config.h"
#include "common/status.h"
#include "common/utils/docx/zip_utils.h"
#include "common/utils/file_opt.h"
#include "core/system/system_diagnose_service.h"
#include "dao/audit_dao.h"
#include "dao/device_dao.h"

namespace qifeng_ca {

    namespace {

        // 生成时间戳目录名: diagnose_YYYYMMDD_HHMMSS
        std::string MakeTimestampDirName() {
            std::tm tm = GetLocalTime();
            std::array<char, 32> buf {};
            std::strftime(buf.data(), buf.size(), "%Y%m%d_%H%M%S", &tm);
            return std::string("diagnose_") + buf.data();
        }

        // 创建诊断工作目录, 返回绝对路径; 失败返回空串
        std::string CreateTimestampDir(const std::string &diagDir) {
            std::string timestampDir = diagDir + MakeTimestampDirName() + "/";
            bool ret = FileOpt::CreateDstDirectory(timestampDir);
            if (!ret) {
                SLOG_ERROR << "Diagnose: create timestamp dir failed";
                return {};
            }
            return timestampDir;
        }
        std::string CreateWorkDir() {
            const auto &tmp = TmpPathConfig::GetInstance();
            std::string workDir = tmp.GetDrogonTmpPath() + "/" + tmp.GetDiagonseTmpPath();
            bool ret = FileOpt::CreateDstDirectory(workDir);
            if (!ret) {
                SLOG_ERROR << "Diagnose: create work dir failed";
                return {};
            }

            return workDir;
        }

        // 收集 logs 目录下最近更新的最多 max_log_files 个文件
        Status CopyRecentLogFiles(const std::string &destLogDir) {
            std::error_code ec;
            std::filesystem::create_directories(destLogDir, ec);

            std::string logSrcDir = DiagnoseConfig::GetInstance().GetLogSrcDir();
            std::vector<std::filesystem::path> logFiles;
            if (!std::filesystem::exists(logSrcDir, ec)) {
                SLOG_WARN << "Diagnose: log src dir not found: " << logSrcDir;
                return Status {};
            }
            for (const auto &entry : std::filesystem::directory_iterator(logSrcDir, ec)) {
                if (entry.is_regular_file()) {
                    logFiles.push_back(entry.path());
                }
            }
            // 按最后修改时间倒序
            std::sort(logFiles.begin(), logFiles.end(),
                      [](const std::filesystem::path &a, const std::filesystem::path &b) {
                          return std::filesystem::last_write_time(a) > std::filesystem::last_write_time(b);
                      });

            size_t count = std::min(DiagnoseConfig::GetInstance().GetMaxLogFiles(), logFiles.size());
            for (size_t i = 0; i < count; ++i) {
                auto dest = destLogDir + logFiles[i].filename().string();
                std::filesystem::copy_file(logFiles[i], dest, std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) {
                    SLOG_WARN << "Diagnose: copy log failed, src=" << logFiles[i] << " ec=" << ec.message();
                }
            }
            SLOG_INFO << "Diagnose: copied " << count << " log files";
            return Status {};
        }

        // 拷贝系统版本配置文件
        Status CopyVersionConfig(const std::string &destDir) {
            std::string versionPath = DeviceConfig::GetInstance().GetVersionConfigPath();
            std::error_code ec;
            if (!std::filesystem::exists(versionPath, ec)) {
                SLOG_WARN << "Diagnose: version config not found: " << versionPath;
                return Status {};
            }
            std::string dest = destDir + "version.txt";
            std::filesystem::copy_file(versionPath, dest, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                SLOG_WARN << "Diagnose: copy version failed, ec=" << ec.message();
                return Status {-1, "拷贝版本配置失败"};
            }
            SLOG_INFO << "Diagnose: copied version config from " << versionPath;
            return Status {};
        }

        // 收集OEM配置(产品序列号)和bm_version固件信息
        void CollectOemInfo(const std::string &destDir) {
            // 拷贝/factory/OEMconfig.ini
            std::error_code ec;
            if (std::filesystem::exists("/factory/OEMconfig.ini", ec)) {
                std::string oemDest = destDir + "OEMconfig.ini";
                std::filesystem::copy_file("/factory/OEMconfig.ini", oemDest,
                                           std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) {
                    SLOG_INFO << "Diagnose: copied OEMconfig.ini";
                }
            } else {
                SLOG_WARN << "Diagnose: /factory/OEMconfig.ini not found";
            }

            // 收集bm_version完整输出
            {
                std::string bmDest = destDir + "bm_version.txt";
                std::string cmd = "bm_version > " + bmDest + " 2>&1";
                int ret = std::system(cmd.c_str());
                if (ret == 0) {
                    SLOG_INFO << "Diagnose: saved bm_version output";
                } else {
                    SLOG_WARN << "Diagnose: bm_version command failed, ret=" << ret;
                    // bm_version可能不存在, 写空文件
                    std::ofstream ofs(bmDest);
                    ofs << "bm_version command failed, ret=" << ret << std::endl;
                }
            }
        }

        // 查询最近3天设备历史, 按天查询并直接逐条写入文本文件
        Status WriteDeviceHistory(const std::string &destDir) {
            std::string dest = destDir + "device_history.txt";
            std::ofstream ofs(dest);
            if (!ofs) {
                return Status {-1, "写入设备历史文件失败"};
            }
            int64_t historyDays = DiagnoseConfig::GetInstance().GetHistoryDays();
            ofs << "# device history (recent " << historyDays << " days)\n";
            ofs << "# fields: device_id, timestamp, connect, version, cpu, memory, disk, accelerator, battery, audio\n";

            int64_t endTime = static_cast<int64_t>(GetTimeMs());
            int64_t dayMs = 24 * 60 * 60 * 1000LL;
            int64_t startTime = endTime - historyDays * dayMs;

            DeviceDao deviceDao;
            std::string deviceId;  // 空表示查询所有设备
            int32_t totalCollected = 0;

            // 按天查询, 每天单独查询并逐条写入, 避免一次性加载过多数据
            for (int64_t dayStart = startTime; dayStart < endTime; dayStart += dayMs) {
                int64_t dayEnd = std::min(dayStart + dayMs, endTime);
                auto startTimeCost = GetTimeMs();
                auto records = deviceDao.QueryByTimeRange(deviceId, dayStart, dayEnd);
                SLOG_INFO << "Diagnose: query device history, dayStart=" << dayStart << ", dayEnd=" << dayEnd
                          << ", count=" << records.size() << ", time=" << (GetTimeMs() - startTimeCost) << "ms";

                startTimeCost = GetTimeMs();
                for (const auto &r : records) {
                    ofs << "---\n";
                    ofs << "device_id=" << r.mDeviceId << "\n";
                    ofs << "timestamp=" << r.mTimestamp << "\n";
                    ofs << "connect=" << r.mConnect << "\n";
                    ofs << "version=" << r.mVersion << "\n";
                    ofs << "cpu=" << r.mCpu << "\n";
                    ofs << "memory=" << r.mMemory << "\n";
                    ofs << "disk=" << r.mDisk << "\n";
                    ofs << "accelerator=" << r.mAccelerator << "\n";
                    ofs << "battery=" << r.mBattery << "\n";
                    ofs << "audio=" << r.mAudio << "\n";
                    ++totalCollected;
                }
                SLOG_INFO << "Diagnose: write device history, dayStart=" << dayStart << ", dayEnd=" << dayEnd
                          << ", time=" << (GetTimeMs() - startTimeCost) << "ms";
            }
            ofs.close();
            SLOG_INFO << "Diagnose: write device history, count=" << totalCollected;
            return Status {};
        }

        // 查询近30天审计日志, 按天分页查询并逐条写入文本文件(避开 proto 转换, 直接走 DAO)
        Status WriteAuditLogs(const std::string &destDir) {
            std::string dest = destDir + "audit_logs.txt";
            std::ofstream ofs(dest);
            if (!ofs) {
                return Status {-1, "写入审计日志文件失败"};
            }
            int64_t retentionDays = DiagnoseConfig::GetInstance().GetAuditLogRetentionDays();
            int32_t pageSize = DiagnoseConfig::GetInstance().GetAuditLogPageSize();
            int32_t maxRecords = DiagnoseConfig::GetInstance().GetAuditLogMaxRecords();
            ofs << "# audit logs (recent " << retentionDays << " days)\n";
            ofs << "# fields: id, user_id, account, user_name, group_name, action, action_desc, "
                   "api_path, http_method, success, error_message, client_ip, request_data, create_time\n";

            int64_t endTime = static_cast<int64_t>(GetTimeMs());
            int64_t dayMs = 24 * 60 * 60 * 1000LL;
            int64_t startTime = endTime - retentionDays * dayMs;
            AuditDao auditDao;
            int32_t totalCollected = 0;

            // 按天查询, 每天单独分页查询并逐条写入, 避免一次性加载过多数据
            for (int64_t dayStart = startTime; dayStart < endTime; dayStart += dayMs) {
                int64_t dayEnd = std::min(dayStart + dayMs, endTime);
                int32_t current = 1;
                while (totalCollected < maxRecords) {
                    auto startTimeCost = GetTimeMs();
                    AuditSearchFilter filter;
                    filter.mStartTime = dayStart;
                    filter.mEndTime = dayEnd;
                    filter.mCurrent = current;
                    filter.mPageSize = pageSize;
                    filter.mSuccessFilter = -1;
                    AuditSearchResult result = auditDao.Search(filter);
                    SLOG_INFO << "Diagnose: query audit logs, dayStart=" << dayStart << ", dayEnd=" << dayEnd
                              << ", page=" << current << ", count=" << result.mRecords.size()
                              << ", time=" << (GetTimeMs() - startTimeCost) << "ms";

                    if (result.mRecords.empty()) {
                        break;
                    }
                    startTimeCost = GetTimeMs();
                    for (const auto &r : result.mRecords) {
                        ofs << "---\n";
                        ofs << "id=" << r.mId << "\n";
                        ofs << "user_id=" << r.mUserId << "\n";
                        ofs << "account=" << r.mUserAccount << "\n";
                        ofs << "user_name=" << r.mUserName << "\n";
                        ofs << "group_name=" << GetGroupName(r.mUserGroupId) << "\n";
                        ofs << "action=" << r.mAction << "\n";
                        ofs << "action_desc=" << r.mActionDesc << "\n";
                        ofs << "api_path=" << r.mApiPath << "\n";
                        ofs << "http_method=" << r.mHttpMethod << "\n";
                        ofs << "success=" << (r.mSuccess ? 1 : 0) << "\n";
                        ofs << "error_message=" << r.mErrorMessage << "\n";
                        ofs << "client_ip=" << r.mClientIp << "\n";
                        ofs << "request_data=" << r.mRequestData << "\n";
                        ofs << "create_time=" << r.mCreateTime << "\n";
                        ++totalCollected;
                        if (totalCollected >= maxRecords) {
                            break;
                        }
                    }
                    SLOG_INFO << "Diagnose: write audit logs, dayStart=" << dayStart << ", dayEnd=" << dayEnd
                              << ", page=" << current << ", time=" << (GetTimeMs() - startTimeCost) << "ms";
                    if (static_cast<int32_t>(result.mRecords.size()) < pageSize) {
                        break;
                    }
                    ++current;
                }
                if (totalCollected >= maxRecords) {
                    break;
                }
            }
            ofs.close();
            SLOG_INFO << "Diagnose: write audit logs, count=" << totalCollected;
            return Status {};
        }

        // 打包工作目录为zip, 返回zip路径
        Status ZipWorkDir(const std::string &workDir, std::string &zipPath) {
            std::string baseDir = CreateWorkDir();
            if (baseDir.empty()) {
                return Status {-1, "创建诊断目录失败"};
            }
            // workDir 去掉末尾的'/'
            std::string dirName = workDir;
            if (!dirName.empty() && dirName.back() == '/') {
                dirName.pop_back();
            }
            std::string name = std::filesystem::path(dirName).filename().string();
            zipPath = baseDir + name + ".zip";

            if (!docx::ZipDir(dirName, zipPath, false)) {
                return Status {-1, "打包诊断日志失败"};
            }
            SLOG_INFO << "Diagnose: zip created, path=" << zipPath;
            return Status {};
        }

    }  // namespace

    Status SystemDiagnoseService::GenerateDiagnoseLog(DiagnoseLogResponse* resp) {
        SLOG_INFO << "Diagnose: generate diagnose log";
        std::string workDir = CreateWorkDir();
        if (workDir.empty()) {
            return Status {-1, "创建诊断临时目录失败"};
        }
        std::string timestampDir = CreateTimestampDir(workDir);
        if (timestampDir.empty()) {
            return Status {-1, "创建诊断时间戳目录失败"};
        }

        Status st = CollectLogs(timestampDir);
        if (!st.IsSuccess()) {
            std::filesystem::remove_all(timestampDir);
            return st;
        }
        st = CollectVersionFile(timestampDir);
        if (!st.IsSuccess()) {
            std::filesystem::remove_all(timestampDir);
            return st;
        }
        st = CollectDeviceHistory(timestampDir);
        if (!st.IsSuccess()) {
            std::filesystem::remove_all(timestampDir);
            return st;
        }
        st = CollectAuditLogs(timestampDir);
        if (!st.IsSuccess()) {
            std::filesystem::remove_all(timestampDir);
            return st;
        }

        // 收集OEM配置和bm_version信息(非关键, 失败不阻断)
        CollectOemInfo(timestampDir);

        std::string zipPath;
        st = ZipWorkDir(timestampDir, zipPath);
        // 无论打包成功与否都清理临时目录(zip已包含数据)
        std::error_code ec;
        std::filesystem::remove_all(timestampDir, ec);

        resp->set_file_url(zipPath);
        return st;
    }

    Status SystemDiagnoseService::CollectLogs(const std::string &destDir) {
        return CopyRecentLogFiles(destDir + "logs/");
    }

    Status SystemDiagnoseService::CollectVersionFile(const std::string &destDir) {
        return CopyVersionConfig(destDir);
    }

    Status SystemDiagnoseService::CollectDeviceHistory(const std::string &destDir) {
        return WriteDeviceHistory(destDir);
    }

    Status SystemDiagnoseService::CollectAuditLogs(const std::string &destDir) {
        return WriteAuditLogs(destDir);
    }

}  // namespace qifeng_ca
