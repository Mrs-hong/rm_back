//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_CONFIG_TMP_PATH_CONFIG_H
#define QIFENG_CA_INCLUDE_COMMON_CONFIG_TMP_PATH_CONFIG_H

#include <string>

#include "qifeng_framework/common/config_manager.h"

namespace qifeng_ca {

    // 统一管理所有临时文件路径及子目录
    class TmpPathConfig {
    public:
        static TmpPathConfig &GetInstance() {
            static TmpPathConfig Instance;
            return Instance;
        }

        // 上传文件的路径都是相对Drogon的临时目录，所以访问时需要拼接GetDrogonTmpPath
        // 默认使用 data/src/tmp(软连接指向 /data2/qifeng_ca/tmp), 利用大容量SSD存储
        std::string GetDrogonTmpPath() const { return CONFIG_MANAGER.GetString("tmp_path", "drogon_tmp", "data/src/tmp"); }

        // 录音上传临时根目录
        std::string GetAudioTmpPath() const { return CONFIG_MANAGER.GetString("tmp_path", "audio_tmp", "audio/"); }

        // 录音上传临时子目录
        std::string GetAudioUploadTmpPath() const { return GetAudioTmpPath() + "upload_temp/"; }

        // 热词导入临时根目录
        std::string GetHotwordTmpPath() const {
            return CONFIG_MANAGER.GetString("tmp_path", "hotword_tmp", "hotword/");
        }

        // 热词导入临时子目录
        std::string GetHotwordUploadTmpPath() const { return GetHotwordTmpPath() + "upload_temp/"; }

        // 文档预览临时根目录
        std::string GetDocxTmpPath() const { return CONFIG_MANAGER.GetString("tmp_path", "docx_tmp", "docx/"); }

        // 诊断临时根目录
        std::string GetDiagonseTmpPath() const {
            return CONFIG_MANAGER.GetString("tmp_path", "diagonse_tmp", "diagonse/");
        }

        // 会议纪要临时根目录
        std::string GetSummaryTmpPath() const {
            return CONFIG_MANAGER.GetString("tmp_path", "summary_tmp", "summary/");
        }

    private:
        TmpPathConfig() = default;
        ~TmpPathConfig() = default;
        TmpPathConfig(const TmpPathConfig &) = delete;
        TmpPathConfig &operator=(const TmpPathConfig &) = delete;
        TmpPathConfig(TmpPathConfig &&) = delete;
        TmpPathConfig &operator=(TmpPathConfig &&) = delete;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_CONFIG_TMP_PATH_CONFIG_H
