//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_EXTERNAL_SERVICES_SERVICE_FACTORY_H
#define QIFENG_CA_EXTERNAL_SERVICES_SERVICE_FACTORY_H

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "external_services/converters/provider_converter.h"
#include "external_services/external_service.h"

namespace qifeng_ca::external_services {

    // 服务注册
    // 非线程安全, 不允许drogon启动后再注册
    class ServiceFactory {
    public:
        using TranscribeCreator = std::function<TranscribeServicePtr(const ProviderConfig &)>;
        using SummaryCreator = std::function<SummaryServicePtr(const ProviderConfig &)>;

        static ServiceFactory &GetInstance();

        // 注册转写服务商创建器
        void RegisterTranscribe(const std::string &name, TranscribeCreator creator);
        // 注册总结服务商创建器
        void RegisterSummary(const std::string &name, SummaryCreator creator);

        // 转写模板注册
        template <typename T>
        void RegisterTranscribe(const std::string &name) {
            RegisterTranscribe(
                name, [](const ProviderConfig &cfg) -> TranscribeServicePtr { return std::make_shared<T>(cfg); });
        }

        // 总结模板注册
        template <typename T>
        void RegisterSummary(const std::string &name) {
            RegisterSummary(name,
                            [](const ProviderConfig &cfg) -> SummaryServicePtr { return std::make_shared<T>(cfg); });
        }

        // 按名字创建转写服务(若未注册返回 nullptr)
        TranscribeServicePtr CreateTranscribe(const std::string &name, const ProviderConfig &cfg);
        // 按名字创建总结服务(若未注册返回 nullptr)
        SummaryServicePtr CreateSummary(const std::string &name, const ProviderConfig &cfg);

        // 内置注册
        void RegisterBuiltins();

    private:
        ServiceFactory();
        ~ServiceFactory() = default;
        ServiceFactory(const ServiceFactory &) = delete;
        ServiceFactory &operator=(const ServiceFactory &) = delete;
        ServiceFactory(ServiceFactory &&) = delete;
        ServiceFactory &operator=(ServiceFactory &&) = delete;

        std::map<std::string, TranscribeCreator> mTranscribeCreators;
        std::map<std::string, SummaryCreator> mSummaryCreators;
        bool mBuiltinsRegistered {false};
    };

}  // namespace qifeng_ca::external_services

#endif  // QIFENG_CA_EXTERNAL_SERVICES_SERVICE_FACTORY_H
