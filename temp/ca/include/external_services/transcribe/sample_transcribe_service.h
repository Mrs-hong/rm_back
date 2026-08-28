//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_EXTERNAL_SERVICES_TRANSCRIBE_SAMPLE_TRANSCRIBE_SERVICE_H
#define QIFENG_CA_EXTERNAL_SERVICES_TRANSCRIBE_SAMPLE_TRANSCRIBE_SERVICE_H

#include <string>
#include <string_view>

#include "json/value.h"

#include "external_services/base_external_service.h"
#include "external_services/converters/audio_payload.h"
#include "external_services/converters/json_helpers.h"
#include "external_services/converters/provider_converter.h"
#include "external_services/http/drogon_http_client.h"
#include "external_services/http/endpoint_parser.h"
#include "external_services/service_types.h"

namespace qifeng_ca::external_services {

    // 转写服务实例
    class SampleTranscribeService
        : public BaseExternalService<SampleTranscribeService, ServiceRequest, ServiceResponse> {
    public:
        explicit SampleTranscribeService(const ProviderConfig &cfg)
            : SampleTranscribeService(cfg, "sample", "/v1/asr/transcribe") {}

        std::string_view GetProviderName() const override { return mProviderName; }
        ServiceType GetServiceType() const override { return ServiceType::Transcribe; }

        // 构建HTTP请求(POST + JSON): 含音频数据与补充的时间信息
        drogon::HttpRequestPtr BuildRequest(const ServiceRequest &req) {
            auto httpReq = mHttpClient.NewRequest();
            httpReq->setPath(mPath);
            ApplyAuth(httpReq);
            DrogonHttpClient::SetJsonBody(httpReq, BuildRequestBody(req));
            return httpReq;
        }

        // 解析响应: segments 缺少时间戳时, 按 audioStartMs..audioEndMs 比例填充
        ServiceResponse ParseResponse(const drogon::HttpResponsePtr &resp, const ServiceRequest &req) {
            ServiceResponse out;
            out.mRawResponse = std::string(resp->body());
            Json::Value root;
            if (!ParseJson(out.mRawResponse, root) || !root.isObject()) {
                out.mSuccess = false;
                out.mError.mCode = static_cast<int>(drogon::ReqResult::Ok);
                out.mError.mMessage = "transcribe response is not valid json";
                return out;
            }
            if (root.isMember("code") && root["code"].asInt() != 0) {
                out.mSuccess = false;
                out.mError.mCode = root["code"].asInt();
                out.mError.mMessage = root.get("message", "transcribe failed").asString();
                return out;
            }
            out.mSuccess = true;
            FillSegments(root["segments"], req, out);
            return out;
        }

    protected:
        SampleTranscribeService(const ProviderConfig &cfg, std::string_view name, std::string_view path)
            : mConfig(cfg), mProviderName(name), mPath(path) {
            InitHttpClient(cfg.mEndpoint, 60);
        }

        ProviderConfig mConfig;

    private:
        std::string mProviderName;
        std::string mPath;

        void InitHttpClient(const std::string &endpoint, int timeoutSec) {
            DrogonHttpClient::Config httpCfg;
            httpCfg.mHost = ParseHost(endpoint);
            httpCfg.mPort = ParsePort(endpoint);
            httpCfg.mUseSsl = UseSsl(endpoint);
            httpCfg.mTimeoutSec = timeoutSec;
            mHttpClient = DrogonHttpClient(httpCfg);
        }

        void ApplyAuth(const drogon::HttpRequestPtr &req) const {
            if (!mConfig.mAppId.empty()) {
                DrogonHttpClient::SetHeader(req, "X-App-Id", mConfig.mAppId);
            }
            if (!mConfig.mSecretKey.empty()) {
                DrogonHttpClient::SetHeader(req, "X-Secret-Key", mConfig.mSecretKey);
            }
            if (!mConfig.mModel.empty()) {
                DrogonHttpClient::SetHeader(req, "X-Model", mConfig.mModel);
            }
        }

        std::string BuildRequestBody(const ServiceRequest &req) const {
            Json::Value body(Json::objectValue);
            body["provider"] = std::string(mProviderName);
            body["audio"] = Build(req.mAudio);
            body["format"] = req.mAudioFormat;
            body["sampleRate"] = req.mAudioConfig.mSampleRate;
            body["channels"] = req.mAudioConfig.mChannels;
            body["bitDepth"] = req.mAudioConfig.mBitDepth;
            body["language"] = req.mLanguage;
            body["enableDiarization"] = req.mEnableDiarization;
            if (!req.mModel.empty()) {
                body["model"] = req.mModel;
            }
            // 补充时间信息: 样例HTTP协议不返回逐句时间, 响应解析时据此填充Segment时间
            body["audioStartMs"] = static_cast<Json::Int64>(req.mAudioStartMs);
            body["audioEndMs"] = static_cast<Json::Int64>(req.mAudioEndMs);
            return ToJsonString(body);
        }

        // 样例协议不返回逐句时间: 按段数均分 [startMs, endMs]; 若响应含时间则用响应值
        void FillSegments(const Json::Value &segs, const ServiceRequest &req, ServiceResponse &out) const {
            if (!segs.isArray()) {
                return;
            }
            const int n = static_cast<int>(segs.size());
            const int64_t startMs = req.mAudioStartMs;
            const int64_t endMs = (req.mAudioEndMs > startMs) ? req.mAudioEndMs : startMs;
            const int64_t span = endMs - startMs;
            for (int i = 0; i < n; ++i) {
                const Json::Value &s = segs[i];
                Segment seg;
                seg.mOrder = i + 1;
                seg.mText = s.get("text", "").asString();
                seg.mSpeakerId = s.get("speakerId", "").asString();
                seg.mSpeakerName = s.get("speakerName", "").asString();
                if (s.isMember("startMs") && s.isMember("endMs")) {
                    seg.mStartMs = s["startMs"].asInt64();
                    seg.mEndMs = s["endMs"].asInt64();
                } else if (n > 0) {
                    seg.mStartMs = startMs + span * i / n;
                    seg.mEndMs = startMs + span * (i + 1) / n;
                }
                out.mSegments.push_back(std::move(seg));
            }
        }
    };

}  // namespace qifeng_ca::external_services

#endif  // QIFENG_CA_EXTERNAL_SERVICES_TRANSCRIBE_SAMPLE_TRANSCRIBE_SERVICE_H
