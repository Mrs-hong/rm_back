//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <filesystem>
#include <string>
#include <vector>

#include "qifeng_framework/common/logger.h"

#include "common/config/meeting_config.h"
#include "common/config/tmp_path_config.h"
#include "common/config/voiceprint_config.h"
#include "common/status.h"
#include "core/system/reset_task.h"

namespace qifeng_ca {

    namespace {

        // 删除目录及其所有内容(目录不存在视为成功)
        void RemoveDir(const std::string &path) {
            if (path.empty()) {
                return;
            }
            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) {
                return;
            }
            auto count = std::filesystem::remove_all(path, ec);
            if (ec) {
                SLOG_WARN << "Reset: remove dir failed, path=" << path << " ec=" << ec.message();
            } else {
                SLOG_INFO << "Reset: removed dir, path=" << path << " entries=" << count;
            }
        }

        // 重新创建空目录(保证后续业务可正常写入)
        void RecreateDir(const std::string &path) {
            if (path.empty()) {
                return;
            }
            std::error_code ec;
            std::filesystem::create_directories(path, ec);
            if (ec) {
                SLOG_WARN << "Reset: recreate dir failed, path=" << path << " ec=" << ec.message();
            }
        }

        // 收集需要清理的目录路径
        std::vector<std::string> CollectCleanupPaths() {
            std::vector<std::string> paths;
            paths.push_back(MeetingConfig::GetInstance().GetAudioPath());
            paths.push_back(MeetingConfig::GetInstance().GetSymlinkAudioPath());
            paths.push_back(VoiceprintConfig::GetInstance().GetVoiceprintPath());
            paths.push_back(VoiceprintConfig::GetInstance().GetVoiceprintTmpPath());
            paths.push_back(TmpPathConfig::GetInstance().GetDrogonTmpPath() + "/" +
                            TmpPathConfig::GetInstance().GetAudioTmpPath());
            paths.push_back(TmpPathConfig::GetInstance().GetDrogonTmpPath() + "/" +
                            TmpPathConfig::GetInstance().GetAudioUploadTmpPath());
            paths.push_back(TmpPathConfig::GetInstance().GetDrogonTmpPath() + "/" +
                            TmpPathConfig::GetInstance().GetHotwordUploadTmpPath());
            paths.push_back(TmpPathConfig::GetInstance().GetDrogonTmpPath() + "/" +
                            TmpPathConfig::GetInstance().GetHotwordTmpPath());
            paths.push_back(TmpPathConfig::GetInstance().GetDrogonTmpPath() + "/" +
                            TmpPathConfig::GetInstance().GetDocxTmpPath());
            paths.push_back(TmpPathConfig::GetInstance().GetDrogonTmpPath() + "/" +
                            TmpPathConfig::GetInstance().GetDiagonseTmpPath());

            return paths;
        }

    }  // namespace

    // 清理磁盘文件: 录音目录、临时目录、声纹目录
    class CleanupFilesResetTask final : public ResetTask {
    public:
        std::string_view Name() const override { return "CleanupFiles"; }

        Status Execute(uint64_t operatorAccountId) override {
            (void)operatorAccountId;
            auto paths = CollectCleanupPaths();
            for (const auto &path : paths) {
                RemoveDir(path);
                RecreateDir(path);
            }
            SLOG_INFO << "Reset: cleaned " << paths.size() << " directories";
            return Status {};
        }
    };

    std::unique_ptr<ResetTask> CreateCleanupFilesResetTask() {
        return std::make_unique<CleanupFilesResetTask>();
    }

}  // namespace qifeng_ca
