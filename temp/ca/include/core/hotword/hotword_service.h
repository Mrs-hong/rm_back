//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_HOTWORD_HOTWORD_SERVICE_H
#define QIFENG_CA_INCLUDE_CORE_HOTWORD_HOTWORD_SERVICE_H

#include "qifeng_ca/common.pb.h"
#include "qifeng_ca/hotword.pb.h"

#include "common/status.h"
#include "dao/hotword_dao.h"

namespace qifeng_ca {

    class HotwordService {
    public:
        HotwordService() = default;
        ~HotwordService() = default;

        HotwordService(const HotwordService &) = delete;
        HotwordService &operator=(const HotwordService &) = delete;
        HotwordService(HotwordService &&) noexcept = delete;
        HotwordService &operator=(HotwordService &&) = delete;

        Status SearchHotword(const HotwordSearchRequest &req, HotwordSearchResponse* resp);

        Status AddHotword(const HotwordAddRequest &req, Empty* resp);

        Status EditHotword(const HotwordEditRequest &req, Empty* resp);

        Status DeleteHotword(const HotwordDelRequest &req, Empty* resp);

        Status ImportHotword(const HotwordImportRequest &req, HotwordImportResponse* resp);

    private:
        static HotwordDao &Dao();
        void FillSearchResponse(const HotwordSearchResult &result, HotwordSearchResponse* resp);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_HOTWORD_HOTWORD_SERVICE_H
