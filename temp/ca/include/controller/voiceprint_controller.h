//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CONTROLLER_VOICEPRINT_CONTROLLER_H
#define QIFENG_CA_INCLUDE_CONTROLLER_VOICEPRINT_CONTROLLER_H

#include "drogon/DrObject.h"
#include "qifeng_ca/common.pb.h"
#include "qifeng_ca/voiceprint.pb.h"

#include "common/http/http.h"
#include "common/status.h"
#include "core/voiceprint/voiceprint_service.h"

namespace qifeng_ca {

    class VoiceprintController final : public drogon::DrObject<VoiceprintController> {
    public:
        QIFENG_CA_METHOD_LIST_BEGIN(VoiceprintController);

        QIFENG_CA_METHOD_PREREQ_ADD("录制声纹", RecordVoiceprint, BmsPreAccountIdReq<RecordVoiceprintRequest>,
                                    "/web/voiceprint/recordVoiceprint", drogon::Post, "CADiskFilter");

        QIFENG_CA_METHOD_PREREQ_ADD("重置声纹录制", RollbackVoiceprint, BmsPreAccountIdReq<RollbackVoiceprintRequest>,
                                    "/web/voiceprint/rollbackVoiceprint", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("停止声纹录制", StopVoiceprint, BmsPreAccountIdReq<StopVoiceprintRequest>,
                                    "/web/voiceprint/stopVoiceprint", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD(ActionDesc("校验声纹信息", false), CheckVoiceprintInfo,
                                    BmsPreAccountIdReq<CheckVoiceprintInfoRequest>,
                                    "/web/voiceprint/checkVoiceprintInfo", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("添加声纹", AddVoiceprint, BmsPreAccountIdReq<AddVoiceprintRequest>,
                                    "/web/voiceprint/addVoiceprint", drogon::Post, "CADiskFilter");

        QIFENG_CA_METHOD_PREREQ_ADD(ActionDesc("查询声纹列表", false), GetVoiceprintList,
                                    BmsPreAccountIdReq<VoiceprintSearchRequest>, "/web/voiceprint/getVoiceprintList",
                                    drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("编辑声纹", EditVoiceprint, BmsPreAccountIdReq<EditVoiceprintRequest>,
                                    "/web/voiceprint/editVoiceprint", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("删除声纹", DeleteVoiceprint, BmsPreAccountIdReq<DeleteVoiceprintRequest>,
                                    "/web/voiceprint/delVoiceprint", drogon::Post);

        QIFENG_CA_METHOD_LIST_END;

        Status RecordVoiceprint(const RecordVoiceprintRequest &req, RecordVoiceprintResponse &resp);

        Status RollbackVoiceprint(const RollbackVoiceprintRequest &req, Empty &resp);

        Status StopVoiceprint(const StopVoiceprintRequest &req, StopVoiceprintResponse &resp);

        Status CheckVoiceprintInfo(const CheckVoiceprintInfoRequest &req, Empty &resp);

        Status AddVoiceprint(const AddVoiceprintRequest &req, Empty &resp);

        Status GetVoiceprintList(const VoiceprintSearchRequest &req, VoiceprintSearchResponse &resp);

        Status EditVoiceprint(const EditVoiceprintRequest &req, Empty &resp);

        Status DeleteVoiceprint(const DeleteVoiceprintRequest &req, Empty &resp);

    private:
        VoiceprintService mVoiceprintService;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CONTROLLER_VOICEPRINT_CONTROLLER_H
