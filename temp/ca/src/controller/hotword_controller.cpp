//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "qifeng_framework/common/logger.h"

#include "controller/hotword_controller.h"

namespace qifeng_ca {

    Status HotwordController::SearchHotword(const qifeng_ca::HotwordSearchRequest &req,
                                            qifeng_ca::HotwordSearchResponse &resp) {
        SLOG_DEBUG << "Search hotword list, current: " << req.current() << ", page_size: " << req.page_size();

        Status status = mHotwordService.SearchHotword(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status HotwordController::AddHotword(const qifeng_ca::HotwordAddRequest &req, qifeng_ca::Empty &resp) {
        SLOG_DEBUG << "Add hotword: " << req.word();

        Status status = mHotwordService.AddHotword(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status HotwordController::EditHotword(const qifeng_ca::HotwordEditRequest &req, qifeng_ca::Empty &resp) {
        SLOG_DEBUG << "Edit hotword id: " << req.id();

        Status status = mHotwordService.EditHotword(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status HotwordController::DeleteHotword(const qifeng_ca::HotwordDelRequest &req, qifeng_ca::Empty &resp) {
        SLOG_DEBUG << "Delete hotword, count: " << req.ids_size();

        Status status = mHotwordService.DeleteHotword(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status HotwordController::ImportHotword(const qifeng_ca::HotwordImportRequest &req,
                                            qifeng_ca::HotwordImportResponse &resp) {
        SLOG_DEBUG << "Import hotword from Excel";

        Status status = mHotwordService.ImportHotword(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

}  // namespace qifeng_ca

QIFENG_CA_HTTP_METHOD_REGISTRY(qifeng_ca::HotwordController);
