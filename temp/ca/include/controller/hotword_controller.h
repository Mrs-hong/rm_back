//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CONTROLLER_HOTWORD_CONTROLLER_H
#define QIFENG_CA_INCLUDE_CONTROLLER_HOTWORD_CONTROLLER_H

#include "drogon/DrObject.h"
#include "qifeng_ca/hotword.pb.h"

#include "common/http/http.h"
#include "common/status.h"
#include "core/hotword/hotword_service.h"

namespace qifeng_ca {

    class HotwordController final : public drogon::DrObject<HotwordController> {
    public:
        QIFENG_CA_METHOD_LIST_BEGIN(HotwordController);

        QIFENG_CA_METHOD_PREREQ_ADD(ActionDesc("查询热词列表", false), SearchHotword,
                                    BmsPreAccountIdReq<HotwordSearchRequest>, "/web/hotword/getHotwordList",
                                    drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("添加热词", AddHotword, BmsPreAccountIdReq<HotwordAddRequest>,
                                    "/web/hotword/addHotword", drogon::Post, "CADiskFilter");

        QIFENG_CA_METHOD_PREREQ_ADD("编辑热词", EditHotword, BmsPreAccountIdReq<HotwordEditRequest>,
                                    "/web/hotword/editHotword", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("删除热词", DeleteHotword, BmsPreAccountIdReq<HotwordDelRequest>,
                                    "/web/hotword/delHotword", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("批量导入热词", ImportHotword, BmsPreUploadHotwordFileReq,
                                    "/web/hotword/importHotword", drogon::Post, "CADiskFilter");

        QIFENG_CA_METHOD_LIST_END;

        Status SearchHotword(const HotwordSearchRequest &req, HotwordSearchResponse &resp);

        Status AddHotword(const HotwordAddRequest &req, Empty &resp);

        Status EditHotword(const HotwordEditRequest &req, Empty &resp);

        Status DeleteHotword(const HotwordDelRequest &req, Empty &resp);

        Status ImportHotword(const HotwordImportRequest &req, HotwordImportResponse &resp);

    private:
        HotwordService mHotwordService;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CONTROLLER_HOTWORD_CONTROLLER_H
