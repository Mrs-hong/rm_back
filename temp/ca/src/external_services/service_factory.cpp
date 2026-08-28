//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "qifeng_framework/common/logger.h"

#include "external_services/service_factory.h"

namespace qifeng_ca::external_services {

    ServiceFactory &ServiceFactory::GetInstance() {
        static ServiceFactory Instance;
        return Instance;
    }

    ServiceFactory::ServiceFactory() {
        RegisterBuiltins();
    }

    // 注册对应的转写、总结服务实例
    void ServiceFactory::RegisterBuiltins() {
        if (mBuiltinsRegistered) {
            return;
        }
        // TODO(yf): 后续可加入不同的http客户端
        mBuiltinsRegistered = true;
    }

    void ServiceFactory::RegisterTranscribe(const std::string &name, TranscribeCreator creator) {
        mTranscribeCreators[name] = std::move(creator);
    }

    void ServiceFactory::RegisterSummary(const std::string &name, SummaryCreator creator) {
        mSummaryCreators[name] = std::move(creator);
    }

    TranscribeServicePtr ServiceFactory::CreateTranscribe(const std::string &name, const ProviderConfig &cfg) {
        auto it = mTranscribeCreators.find(name);
        if (it == mTranscribeCreators.end()) {
            SLOG_ERROR << "ServiceFactory: transcribe provider not registered, name=" << name;
            return nullptr;
        }
        return it->second(cfg);
    }

    SummaryServicePtr ServiceFactory::CreateSummary(const std::string &name, const ProviderConfig &cfg) {
        auto it = mSummaryCreators.find(name);
        if (it == mSummaryCreators.end()) {
            SLOG_ERROR << "ServiceFactory: summary provider not registered, name=" << name;
            return nullptr;
        }
        return it->second(cfg);
    }

}  // namespace qifeng_ca::external_services
