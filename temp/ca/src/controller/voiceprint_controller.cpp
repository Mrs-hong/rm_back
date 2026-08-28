//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "qifeng_framework/common/logger.h"

#include "controller/voiceprint_controller.h"

namespace qifeng_ca {

    Status VoiceprintController::CheckVoiceprintInfo(const qifeng_ca::CheckVoiceprintInfoRequest &req,
                                                     qifeng_ca::Empty &resp) {
        SLOG_DEBUG << "CheckVoiceprintInfo - number: " << req.number() << ", speaker: " << req.speaker();

        Status status = mVoiceprintService.CheckVoiceprintInfo(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status VoiceprintController::AddVoiceprint(const qifeng_ca::AddVoiceprintRequest &req, qifeng_ca::Empty &resp) {
        SLOG_DEBUG << "AddVoiceprint - number: " << req.number() << ", speaker: " << req.speaker();

        Status status = mVoiceprintService.AddVoiceprint(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status VoiceprintController::GetVoiceprintList(const qifeng_ca::VoiceprintSearchRequest &req,
                                                   qifeng_ca::VoiceprintSearchResponse &resp) {
        SLOG_DEBUG << "GetVoiceprintList - current: " << req.current() << ", page_size: " << req.page_size();

        Status status = mVoiceprintService.GetVoiceprintList(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status VoiceprintController::EditVoiceprint(const qifeng_ca::EditVoiceprintRequest &req, qifeng_ca::Empty &resp) {
        SLOG_DEBUG << "EditVoiceprint - id: " << req.id();

        Status status = mVoiceprintService.EditVoiceprint(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status VoiceprintController::DeleteVoiceprint(const qifeng_ca::DeleteVoiceprintRequest &req,
                                                  qifeng_ca::Empty &resp) {
        SLOG_DEBUG << "DeleteVoiceprint - id: " << req.id();

        Status status = mVoiceprintService.DeleteVoiceprint(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status VoiceprintController::RecordVoiceprint(const RecordVoiceprintRequest &req, RecordVoiceprintResponse &resp) {
        SLOG_DEBUG << "RecordVoiceprint";

        Status status = mVoiceprintService.RecordVoiceprint(req, &resp);
        if (status.GetCode()) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status VoiceprintController::RollbackVoiceprint(const RollbackVoiceprintRequest &req, Empty &resp) {
        (void)resp;
        SLOG_DEBUG << "RollbackVoiceprint";

        Status status = mVoiceprintService.RollbackVoiceprint(req, &resp);
        if (status.GetCode()) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status VoiceprintController::StopVoiceprint(const StopVoiceprintRequest &req, StopVoiceprintResponse &resp) {
        SLOG_DEBUG << "StopVoiceprint";

        Status status = mVoiceprintService.StopVoiceprint(req, &resp);
        if (status.GetCode()) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

}  // namespace qifeng_ca

QIFENG_CA_HTTP_METHOD_REGISTRY(qifeng_ca::VoiceprintController);
