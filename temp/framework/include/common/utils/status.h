//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_FRAMEWORK_INCLUDE_COMMON_STATUS_H
#define QIFENG_FRAMEWORK_INCLUDE_COMMON_STATUS_H

#include <memory>
#include <string>

#include "common/logger.h"

namespace qifeng {

    struct ExDataInfo {
        uint64_t mAccountId;
        ExDataInfo() = default;
        explicit ExDataInfo(uint64_t accountId) : mAccountId(accountId) {
        }
    };

    class Status {
    public:
        Status() = default;

        explicit Status(int code) : mCode(code) {
        }

        Status(int code, std::string msg) : mCode(code), mMsg(std::move(msg)) {
        }

        Status(const Status&) = default;
        Status(Status&&) noexcept = default;
        Status& operator=(const Status&) = default;
        Status& operator=(Status&&) noexcept = default;
        ~Status() = default;

        Status& SetResult(int code, std::string msg = "") {
            mCode = code;
            mMsg = std::move(msg);
            return *this;
        }

        void SetCode(int code) {
            mCode = code;
        }

        void SetMsg(std::string msg) {
            mMsg = std::move(msg);
        }

        void SetExData(std::shared_ptr<ExDataInfo>& data) {
            mData = std::move(data);
        }

        bool IsSuccess() const {
            return mCode == 0;
        }

        explicit operator bool() const {
            return IsSuccess();
        }

        int GetCode() const {
            return mCode;
        }

        const std::string& GetMsg() const {
            return mMsg;
        }

        std::shared_ptr<ExDataInfo> GetExData() const {
            return mData;
        }

        std::string ToString() const {
            return "Status{code=" + std::to_string(mCode) + ", msg=" + mMsg + "}";
        }

    private:
        void DoLog() const {
            if (mCode == 0) {
                if (!mMsg.empty()) {
                    FLOG_DEBUG("Status success: " + mMsg);
                }
            } else {
                std::string logMsg = "Status error: code=" + std::to_string(mCode);
                if (!mMsg.empty()) {
                    logMsg += ", msg=" + mMsg;
                }
                FLOG_ERROR(logMsg);
            }
        }

    private:
        int mCode = 0;
        std::string mMsg;
        // 作为额外的数据传输
        std::shared_ptr<ExDataInfo> mData;
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_COMMON_STATUS_H
