//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "qifeng_framework/common/logger.h"

#include "external_services/config/service_config.h"
#include "external_services/service_dispatcher.h"
#include "external_services/service_factory.h"

namespace qifeng_ca::external_services {

    ServiceDispatcher &ServiceDispatcher::GetInstance() {
        static ServiceDispatcher Instance;
        return Instance;
    }

    // 这样实现是方便我后面在转写、总结做切换
    // 转写: 是否走外部 HTTP 方式
    bool ServiceDispatcher::UseExternalTranscribe() const {
        return ExternalServiceConfig::GetInstance().GetTranscribeMode() == CallMode::ExternalHttp;
    }

    // 总结: 是否走外部 HTTP 方式
    bool ServiceDispatcher::UseExternalSummary() const {
        return ExternalServiceConfig::GetInstance().GetSummaryMode() == CallMode::ExternalHttp;
    }

    // 从文件中加载不同配置
    static ProviderConfig LoadProviderConfig(const std::string &provider) {
        auto &cfg = ExternalServiceConfig::GetInstance();
        ProviderConfig pc;
        pc.mAppId = cfg.Get(provider, "app_id", "");
        pc.mSecretKey = cfg.Get(provider, "secret_key", "");
        pc.mRegion = cfg.Get(provider, "region", "");
        pc.mEndpoint = cfg.Get(provider, "endpoint", "");
        pc.mModel = cfg.Get(provider, "model", "");
        return pc;
    }

    // 创建外部转写服务实例
    TranscribeServicePtr ServiceDispatcher::GetTranscribeService() {
        if (mTranscribeService) {
            return mTranscribeService;
        }
        // 根据不同的接口完成不同的配置读取
        auto &cfg = ExternalServiceConfig::GetInstance();
        std::string provider = cfg.GetTranscribeProvider();
        ProviderConfig pc = LoadProviderConfig(provider);
        mTranscribeService = ServiceFactory::GetInstance().CreateTranscribe(provider, pc);
        if (mTranscribeService) {
            SLOG_INFO << "ServiceDispatcher: external transcribe service created, provider=" << provider;
        }
        return mTranscribeService;
    }

    // 创建外部总结服务实例
    SummaryServicePtr ServiceDispatcher::GetSummaryService() {
        if (mSummaryService) {
            return mSummaryService;
        }
        auto &cfg = ExternalServiceConfig::GetInstance();
        std::string provider = cfg.GetSummaryProvider();
        ProviderConfig pc = LoadProviderConfig(provider);
        mSummaryService = ServiceFactory::GetInstance().CreateSummary(provider, pc);
        if (mSummaryService) {
            SLOG_INFO << "ServiceDispatcher: external summary service created, provider=" << provider;
        }
        return mSummaryService;
    }

}  // namespace qifeng_ca::external_services
