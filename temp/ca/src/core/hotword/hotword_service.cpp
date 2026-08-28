//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <regex>
#include <string>

#include "qifeng_framework/common/logger.h"
#include "utf8/checked.h"
#include "xlnt/xlnt.hpp"

#include "common/audio/audio_utils.h"
#include "common/common.h"
#include "common/config/hotword_config.h"
#include "common/config/tmp_path_config.h"
#include "common/utils/file_name_generator.h"
#include "common/utils/file_opt.h"
#include "core/hotword/hotword_service.h"
#include "dao/models/bms_hotword.h"

namespace qifeng_ca {

    HotwordDao &HotwordService::Dao() {
        static HotwordDao Instance;
        return Instance;
    }

    // 规范化热词文本: 去除换行制表符, 去除前后空格, 可选去除中间空格
    static Status NormalizeWord(const std::string &input, std::string &output, bool trimMiddleSpaces = false) {
        try {
            std::regex pattern(R"([\n\r\t])");
            output = std::regex_replace(input, pattern, "");
            if (trimMiddleSpaces) {
                output.erase(std::remove(output.begin(), output.end(), ' '), output.end());
            } else {
                auto start = output.find_first_not_of(' ');
                if (start == std::string::npos) {
                    output.clear();
                    return Status {};
                }
                auto end = output.find_last_not_of(' ');
                output = output.substr(start, end - start + 1);
            }
            return Status {};
        } catch (const std::exception &e) {
            SLOG_ERROR << "NormalizeWord failed: " << e.what();
            return Status {-1, "输入内容格式异常"};
        }
    }

    static Status ValidateAddRequest(const int status) {
        if (status != 1 && status != 2) {
            return Status {-1, "状态参数无效"};
        }
        return Status {};
    }

    static Status ValidateHotwordContent(const std::string &word, const std::string &remark) {
        auto &cfg = HotWordConfig::GetInstance();
        if (word.empty()) {
            return Status {-1, "热词内容不能为空"};
        }
        if (static_cast<size_t>(utf8::distance(word.begin(), word.end())) >
            static_cast<size_t>(cfg.GetHotwordMaxWordLen())) {
            return Status {-1, "热词长度不能超过" + std::to_string(cfg.GetHotwordMaxWordLen()) + "个字符"};
        }
        if (static_cast<size_t>(utf8::distance(remark.begin(), remark.end())) >
            static_cast<size_t>(cfg.GetHotwordMaxRemarkLen())) {
            return Status {-1, "备注长度不能超过" + std::to_string(cfg.GetHotwordMaxRemarkLen()) + "个字符"};
        }
        return Status {};
    }

    [[maybe_unused]] static bool IsValidExcelFilename(const std::string &filename) {
        if (filename.size() >= 5 && filename.substr(filename.size() - 5) == ".xlsx") {
            return true;
        }
        if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".xls") {
            return true;
        }
        return false;
    }

    static bool ValidateExcelColumns(const xlnt::worksheet &ws) {
        auto colA = ws.cell(xlnt::cell_reference("A1"));
        auto colB = ws.cell(xlnt::cell_reference("B1"));
        auto colC = ws.cell(xlnt::cell_reference("C1"));

        if (!colA.has_value() || !colB.has_value() || !colC.has_value()) {
            return false;
        }

        std::string headerA = colA.value<std::string>();
        std::string headerB = colB.value<std::string>();
        std::string headerC = colC.value<std::string>();

        if (headerA.find("热词") == std::string::npos) {
            return false;
        }
        if (headerB.find("描述") == std::string::npos) {
            return false;
        }
        if (headerC.find("状态") == std::string::npos) {
            return false;
        }
        return true;
    }

    static int32_t ParseStatusText(const std::string &statusText) {
        if (statusText == "启用") {
            return 1;
        }
        if (statusText == "停用") {
            return 2;
        }
        return 0;
    }

    struct HotwordRowError {
        int32_t mRowNumber = 0;
        std::string mWord;
        std::string mRemark;
        std::string mStatusText;
        std::string mErrorReason;
    };

    struct HotwordImportContext {
        uint64_t mAccountId = 0;
        int32_t mSuccessCount = 0;
        std::vector<HotwordRowError> mErrors;
    };

    static std::string GetCellString(const xlnt::worksheet &ws, const std::string &cellRef) {
        try {
            auto cell = ws.cell(xlnt::cell_reference(cellRef));
            if (!cell.has_value()) {
                return {};
            }
            return cell.to_string();
        } catch (const std::exception &e) {
            SLOG_WARN << "GetCellString failed, cellRef=" << cellRef << ", err=" << e.what();
            return {};
        }
    }

    static bool ValidateRowData(const std::string &word, const std::string &remark, const std::string &statusText,
                                std::string &errorReason) {
        if (word.empty()) {
            errorReason = "热词不能为空";
            return false;
        }
        auto &cfg = HotWordConfig::GetInstance();
        if (static_cast<size_t>(utf8::distance(word.begin(), word.end())) >
            static_cast<size_t>(cfg.GetHotwordMaxWordLen())) {
            errorReason = "热词长度不能超过" + std::to_string(cfg.GetHotwordMaxWordLen()) + "个字符";
            return false;
        }
        if (static_cast<size_t>(utf8::distance(remark.begin(), remark.end())) >
            static_cast<size_t>(cfg.GetHotwordMaxRemarkLen())) {
            errorReason = "描述长度不能超过" + std::to_string(cfg.GetHotwordMaxRemarkLen()) + "个字符";
            return false;
        }
        if (!statusText.empty() && statusText != "启用" && statusText != "停用") {
            errorReason = "状态必须是\"启用\"或\"停用\"";
            return false;
        }
        return true;
    }

    static int32_t CountValidExcelRows(const xlnt::worksheet &ws, int32_t totalRows) {
        int32_t validCount = 0;
        try {
            for (int32_t row = 2; row <= totalRows; ++row) {
                std::string rawWord = GetCellString(ws, "A" + std::to_string(row));
                std::string statusText = GetCellString(ws, "C" + std::to_string(row));

                SLOG_DEBUG << "row=" << row << ", word=" << rawWord << ", statusText=" << statusText;

                if (rawWord.empty()) {
                    continue;
                }
                // 与 ProcessExcelRow 保持一致的规范化逻辑, 确保预检查计数与实际导入一致
                std::string word;
                if (!NormalizeWord(rawWord, word, true).IsSuccess()) {
                    continue;
                }
                std::string remark = GetCellString(ws, "B" + std::to_string(row));
                if (statusText.empty()) {
                    statusText = "启用";
                }
                // 与 ProcessExcelRow 共用 ValidateRowData, 保证预检查通过的热词必定能导入
                std::string errorReason;
                if (ValidateRowData(word, remark, statusText, errorReason)) {
                    ++validCount;
                }
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "CountValidExcelRows failed, totalRows=" << totalRows << ", err=" << e.what();
            throw;
        }
        return validCount;
    }

    static Status CheckTotalCapacity(uint64_t accountId, int32_t validExcelCount) {
        auto &cfg = HotWordConfig::GetInstance();
        int64_t existingCount = HotwordDao().Count(accountId);
        int64_t totalCount = existingCount + validExcelCount;
        int64_t maxLimit = cfg.GetHotwordMaxLimit();

        if (totalCount > maxLimit) {
            int64_t remaining = maxLimit - existingCount;
            std::string msg = "热词总数超过限制。数据库剩余可容纳" + std::to_string(remaining) +
                              "条，热词模板包含热词" + std::to_string(validExcelCount) + "条";
            return Status {-1, msg};
        }
        return Status {};
    }

    static void ProcessExcelRow(const xlnt::worksheet &ws, int32_t row, HotwordImportContext &ctx) {
        std::string rawWord = GetCellString(ws, "A" + std::to_string(row));
        std::string remark = GetCellString(ws, "B" + std::to_string(row));
        std::string statusText = GetCellString(ws, "C" + std::to_string(row));
        if (rawWord.empty() && remark.empty() && statusText.empty()) {  // 全空则跳过
            return;
        }
        if (rawWord.empty()) {
            ctx.mErrors.push_back({row, "", remark, statusText, "热词不能为空"});
            return;
        }
        // 规范化热词: 去除换行制表符, 去除所有空格(与 AddHotword 保持一致)
        std::string word;
        if (!NormalizeWord(rawWord, word, true).IsSuccess() || word.empty()) {
            ctx.mErrors.push_back({row, rawWord, remark, statusText, "热词内容格式异常"});
            return;
        }
        if (statusText.empty()) {
            statusText = "启用";
        }

        std::string errorReason;
        if (!ValidateRowData(word, remark, statusText, errorReason)) {
            if (row == 2 && (word.find("最大支持输入15个字符") != std::string::npos &&
                             word.find("例子：智会宝") != std::string::npos)) {
                return;
            }
            ctx.mErrors.push_back({row, word, remark, statusText, errorReason});
            return;
        }

        int32_t status = statusText.empty() ? 1 : ParseStatusText(statusText);

        models::HotWord existing = HotwordDao().GetByWord(ctx.mAccountId, word);
        if (existing.mId != 0) {
            ctx.mErrors.push_back({row, word, remark, statusText, "热词已存在"});
            return;
        }

        models::HotWord hw;
        hw.mAccountId = ctx.mAccountId;
        hw.mWord = word;
        hw.mRemark = remark;
        hw.mStatus = status;
        hw.mCreateTime = static_cast<int64_t>(GetTimeMs());
        hw.mUpdateTime = hw.mCreateTime;

        if (!HotwordDao().Insert(hw)) {
            ctx.mErrors.push_back({row, word, remark, statusText, "添加热词失败"});
            return;
        }
        ++ctx.mSuccessCount;
    }

    static Status LoadAndValidateExcel(const HotwordImportRequest &req, xlnt::workbook &wb, xlnt::worksheet &ws,
                                       int32_t &totalRows) {
        if (req.file_paths_size() == 0) {
            return Status {-1, "未上传文件"};
        }
        // 默认只处理一个
        if (req.file_paths_size() > 1) {
            return Status {-1, "上传文件过多"};
        }

        try {
            // 从临时文件路径加载Excel(由BmsPreUploadHotwordFileReq保存)
            wb.load(req.file_paths(0));

            ws = wb.active_sheet();
            if (!ValidateExcelColumns(ws)) {
                return Status {-1, "模板格式错误：必须包含\"热词\"、\"描述\"、\"状态\"三列"};
            }

        } catch (const std::exception &e) {
            SLOG_ERROR << "ImportHotword: failed to load Excel: " << e.what();
            return Status {-1, "请上传正确模板文件"};
        }
        // 使用xlnt::cell_reference解析dimension,避免统计到空行/默认填充行
        totalRows = 0;
        try {
            auto dimension = ws.calculate_dimension();
            std::string dimStr = dimension.to_string();
            if (!dimStr.empty()) {
                auto ref = xlnt::range_reference(dimStr);
                totalRows = static_cast<int32_t>(ref.bottom_right().row());
                SLOG_INFO << "ImportHotword: Excel dimension=" << dimStr << " totalRows=" << totalRows;
            }
        } catch (const std::exception &e) {
            SLOG_WARN << "ImportHotword: calculate_dimension failed, fallback to rows(), err=" << e.what();
            // ws.rows(false) 遍历到的行数即最后一行行号, 与消费侧语义一致, 不再扣减表头/说明行
            for (auto row : ws.rows(false)) {
                (void)row;
                ++totalRows;
            }
        }
        return Status {};
    }

    [[maybe_unused]] static Status CheckImportFileSize(const std::string &filePath) {
        std::error_code ec;
        auto fileSize = static_cast<int64_t>(std::filesystem::file_size(filePath, ec));
        if (ec) {
            SLOG_ERROR << "ImportHotword: get file size failed, path=" << filePath << ", err=" << ec.message();
            return Status {-1, "读取上传文件失败"};
        }
        constexpr int64_t importMaxFileSize = static_cast<int64_t>(512) * 1024;  // 512KB
        if (fileSize > importMaxFileSize) {
            SLOG_WARN << "ImportHotword: file too large, size=" << fileSize << ", max=" << importMaxFileSize;
            return Status {-1, "上传文件超过512KB，请减少热词数量后重试"};
        }
        return Status {};
    }

    static void ImportAllExcelRows(const xlnt::worksheet &ws, int32_t totalRows, HotwordImportContext &ctx) {
        // 空行由 ProcessExcelRow 内部跳过, 不可 break 终止(原 break 致中间空行后剩余行全部丢失)
        for (int32_t row = 2; row <= totalRows; ++row) {
            ProcessExcelRow(ws, row, ctx);
        }
    }

    static std::string CreateExcel(const HotwordImportContext &ctx) {
        // 如果有失败条目，生成错误Excel文件
        if (ctx.mErrors.empty()) {
            return "";
        }

        auto &tmpCfg = TmpPathConfig::GetInstance();
        std::string tempDir = tmpCfg.GetDrogonTmpPath() + "/" + tmpCfg.GetHotwordUploadTmpPath();

        // 确保目录存在
        if (!FileOpt::CreateDstDirectory(tempDir)) {
            SLOG_ERROR << "ImportHotword: create dst directory failed, path=" << tempDir;
            return "";
        }

        std::string fileName = FileNameGenerator::GenFile("hotword_import_error", ".xlsx");
        std::string filePath = tempDir + fileName;

        xlnt::workbook errorWb;
        xlnt::worksheet errorWs = errorWb.active_sheet();

        // 写入表头
        errorWs.cell("A1").value("行号");
        errorWs.cell("B1").value("热词");
        errorWs.cell("C1").value("描述");
        errorWs.cell("D1").value("状态");
        errorWs.cell("E1").value("失败原因");

        // 写入失败数据行
        for (size_t i = 0; i < ctx.mErrors.size(); ++i) {
            const auto &err = ctx.mErrors[i];
            std::string rowStr = std::to_string(i + 2);  // 从第2行开始
            errorWs.cell("A" + rowStr).value(err.mRowNumber);
            errorWs.cell("B" + rowStr).value(err.mWord);
            errorWs.cell("C" + rowStr).value(err.mRemark);
            errorWs.cell("D" + rowStr).value(err.mStatusText);
            errorWs.cell("E" + rowStr).value(err.mErrorReason);
        }

        errorWb.save(filePath);

        SLOG_INFO << "ImportHotword: error file saved to " << filePath;
        return filePath;
    }

    Status HotwordService::SearchHotword(const HotwordSearchRequest &req, HotwordSearchResponse* resp) {
        if (req.page_size() > 100 || req.page_size() <= 0) {
            return Status {-1, "page_size 参数异常，范围1-100"};
        }
        if (req.current() <= 0) {
            return Status {-1, "current 参数异常"};
        }

        HotwordSearchFilter filter;
        filter.mAccountId = req.account_id();
        filter.mCurrent = req.current();
        filter.mPageSize = req.page_size();
        if (req.has_keyword()) {
            filter.mKeyword = req.keyword();
        }
        if (req.has_status()) {
            filter.mStatus = req.status();
        }
        if (req.has_start_time()) {
            filter.mStartTime = req.start_time();
        }
        if (req.has_end_time()) {
            filter.mEndTime = req.end_time();
        }
        if (filter.mEndTime < filter.mStartTime) {
            return Status {-1, "时间范围异常"};
        }

        HotwordSearchResult result = Dao().Search(filter);
        FillSearchResponse(result, resp);
        return Status {};
    }

    void HotwordService::FillSearchResponse(const HotwordSearchResult &result, HotwordSearchResponse* resp) {
        resp->set_total(result.mTotal);
        for (size_t i = 0; i < result.mRecords.size(); ++i) {
            const auto &r = result.mRecords[i];
            auto* item = resp->add_records();
            item->set_id(r.mId);
            item->set_word(r.mWord);
            if (!r.mRemark.empty()) {
                item->set_remark(r.mRemark);
            }
            item->set_status(r.mStatus);
            item->set_create_time(static_cast<uint64_t>(r.mCreateTime));
            item->set_update_time(static_cast<uint64_t>(r.mUpdateTime));
        }
    }

    Status HotwordService::AddHotword(const HotwordAddRequest &req, Empty* resp) {
        (void)resp;
        uint64_t accountId = req.account_id();

        Status status = ValidateAddRequest(req.status());
        if (status.GetCode() != 0) {
            return status;
        }

        std::string word;
        status = NormalizeWord(req.word(), word, true);
        if (status.GetCode() != 0) {
            return status;
        }

        std::string remark;
        if (req.has_remark()) {
            status = NormalizeWord(req.remark(), remark);
            if (status.GetCode() != 0) {
                return status;
            }
        }

        status = ValidateHotwordContent(word, remark);
        if (status.GetCode() != 0) {
            return status;
        }

        auto &cfg = HotWordConfig::GetInstance();
        int64_t totalCount = Dao().Count(accountId);
        if (totalCount >= cfg.GetHotwordMaxLimit()) {
            return Status {-1, "热词数量已达上限"};
        }
        models::HotWord existing = Dao().GetByWord(accountId, word);
        if (existing.mId != 0) {
            return Status {-1, "热词已存在"};
        }

        models::HotWord hw;
        hw.mAccountId = accountId;
        hw.mWord = word;
        hw.mRemark = remark;
        hw.mStatus = req.status();
        hw.mCreateTime = static_cast<int64_t>(GetTimeMs());
        hw.mUpdateTime = hw.mCreateTime;

        if (!Dao().Insert(hw)) {
            return Status {-1, "添加热词失败"};
        }
        return Status {};
    }

    Status HotwordService::EditHotword(const HotwordEditRequest &req, Empty* resp) {
        (void)resp;
        uint64_t accountId = req.account_id();
        Status status = ValidateAddRequest(req.status());
        if (status.GetCode() != 0) {
            return status;
        }

        auto &cfg = HotWordConfig::GetInstance();
        std::string remark;
        if (req.has_remark()) {
            Status normStatus = NormalizeWord(req.remark(), remark);
            if (normStatus.GetCode() != 0) {
                return normStatus;
            }
        }
        if (static_cast<size_t>(utf8::distance(remark.begin(), remark.end())) >
            static_cast<size_t>(cfg.GetHotwordMaxRemarkLen())) {
            return Status {-1, "备注长度不能超过" + std::to_string(cfg.GetHotwordMaxRemarkLen()) + "个字符"};
        }

        models::HotWord hw = Dao().GetById(accountId, req.id());
        if (hw.mId == 0) {
            return Status {-1, "热词不存在"};
        }

        hw.mStatus = req.status();
        hw.mRemark = remark;
        hw.mUpdateTime = static_cast<int64_t>(GetTimeMs());

        if (!Dao().Update(accountId, hw)) {
            return Status {-1, "编辑热词失败"};
        }
        return status;
    }

    Status HotwordService::DeleteHotword(const HotwordDelRequest &req, Empty* resp) {
        (void)resp;
        uint64_t accountId = req.account_id();
        if (req.ids_size() == 0) {
            return Status {-1, "请选择要删除的热词"};
        }

        std::vector<uint64_t> ids;
        for (int i = 0; i < req.ids_size(); ++i) {
            uint64_t id = req.ids(i);
            if (id <= 0) {
                return Status {-1, "热词ID必须大于0"};
            }
            ids.push_back(id);
        }

        if (!Dao().DeleteByIds(accountId, ids)) {
            return Status {-1, "删除热词失败"};
        }
        return Status {};
    }

    Status HotwordService::ImportHotword(const HotwordImportRequest &req, HotwordImportResponse* resp) {
        if (CheckImportFileSize(req.file_paths(0)).GetCode() != 0) {
            return Status {-1, "上传文件超过512KB，请减少热词数量后重试"};
        }
        try {
            uint64_t accountId = req.account_id();
            xlnt::workbook wb;
            xlnt::worksheet ws;
            int32_t totalRows = 0;
            Status loadStatus = LoadAndValidateExcel(req, wb, ws, totalRows);
            if (loadStatus.GetCode() != 0) {
                SLOG_ERROR << "ImportHotword: LoadAndValidateExcel failed, status=" << loadStatus.ToString();
                return loadStatus;
            }
            auto &cfg = HotWordConfig::GetInstance();
            if (totalRows - 2 > cfg.GetHotwordMaxLimit()) {
                return Status {-1, "文件中热词数量超过" + std::to_string(cfg.GetHotwordMaxLimit()) + "条"};
            }
            SLOG_INFO << "ImportHotword: LoadAndValidateExcel success, totalRows=" << totalRows;
            int32_t validExcelCount = CountValidExcelRows(ws, totalRows);
            SLOG_INFO << "ImportHotword: validExcelCount=" << validExcelCount;
            // 有效热词为0时跳过容量检查, 但仍走导入流程以生成错误Excel展示无效行
            bool noValidRows = (validExcelCount == 0);
            if (!noValidRows) {
                Status capacityStatus = CheckTotalCapacity(accountId, validExcelCount);
                if (!capacityStatus.IsSuccess()) {
                    SLOG_ERROR << "ImportHotword: CheckTotalCapacity failed, status=" << capacityStatus.ToString();
                    return capacityStatus;
                }
            }

            HotwordImportContext ctx;
            ctx.mAccountId = accountId;
            ImportAllExcelRows(ws, totalRows, ctx);
            auto filePath = CreateExcel(ctx);

            resp->set_success_count(ctx.mSuccessCount);
            resp->set_fail_count(static_cast<int32_t>(ctx.mErrors.size()));
            if (!filePath.empty()) {
                resp->set_fail_url(filePath);
            }
            SLOG_INFO << "ImportHotword completed: success=" << ctx.mSuccessCount << ", fail=" << ctx.mErrors.size();
            if (noValidRows) {
                return Status {-1, "文件中有效热词数量为0"};
            }
            return Status {};
        } catch (const std::exception &e) {
            SLOG_ERROR << "ImportHotword failed with exception: " << e.what();
            return Status {-1, "导入热词失败"};
        }
    }

}  // namespace qifeng_ca
