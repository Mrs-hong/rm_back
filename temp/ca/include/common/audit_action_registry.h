/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_CA_INCLUDE_COMMON_AUDIT_ACTION_REGISTRY_H
#define QIFENG_CA_INCLUDE_COMMON_AUDIT_ACTION_REGISTRY_H

#include <string>
#include <unordered_map>

namespace qifeng_ca {

    struct OperationDesc {
        bool mIsAudit;
        std::string mAction;
        std::string mActionDesc;
    };

    struct ActionDesc {
        std::string mActionDesc;
        bool mIsAudit = true;
        ActionDesc() = default;
        // 允许隐式
        ActionDesc(const std::string &actionDesc, bool isAudit = true)  // NOLINT(google-explicit-constructor)
            : mActionDesc(actionDesc), mIsAudit(isAudit) {}
    };

    // 【初始化后只读】用于主线程调用的全局初始化流程，不需要加锁
    class ListenActionRegistry {
    public:
        static ListenActionRegistry &GetInstance() {
            static ListenActionRegistry Instance;
            return Instance;
        }

        // 暂未使用
        void Register(const std::string &path, const std::string &action, const ActionDesc &actionDesc) {
            mRegistry[path] = {actionDesc.mIsAudit, action, actionDesc.mActionDesc};
        }

        void Register(const std::string &path, const std::string &action, const std::string &actionDesc) {
            mRegistry[path] = {!actionDesc.empty(), action, actionDesc};
        }

        bool Lookup(const std::string &path, OperationDesc &desc) const {
            auto iter = mRegistry.find(path);
            if (iter != mRegistry.end()) {
                desc = iter->second;
                return true;
            }
            return false;
        }

        // 校验action是否为已注册的合法操作类型(防止注入)
        bool IsValidAction(const std::string &action) const {
            if (action.empty()) {
                return false;
            }
            for (const auto &pair : mRegistry) {
                if (pair.second.mAction == action) {
                    return true;
                }
            }
            return false;
        }

    private:
        ListenActionRegistry() = default;
        ~ListenActionRegistry() = default;
        ListenActionRegistry(const ListenActionRegistry &) = delete;
        ListenActionRegistry &operator=(const ListenActionRegistry &) = delete;
        ListenActionRegistry(ListenActionRegistry &&) = delete;
        ListenActionRegistry &operator=(ListenActionRegistry &&) = delete;

        std::unordered_map<std::string, OperationDesc> mRegistry;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_AUDIT_ACTION_REGISTRY_H
