//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_WS_REALTIME_DISPATCH_MANAGER_H
#define QIFENG_CA_INCLUDE_COMMON_WS_REALTIME_DISPATCH_MANAGER_H

#include <chrono>
#include <cstdint>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "drogon/WebSocketConnection.h"
#include "trantor/net/EventLoop.h"

#include "common/config/ws_config.h"

namespace qifeng_ca {

    enum class EClientType : uint8_t {
        kTranscribe = 0,
        kMessage = 1,
        kFingerprint = 2,
    };

    // 用户身份信息, 用于连接数限制管理
    struct AccountInfo {
        uint64_t mAccountId {0};
        uint64_t mGroupId {0};
    };

    // 单个 WebSocket 连接条目, 含连接时间戳用于 FIFO 淘汰
    struct ConnEntry {
        drogon::WebSocketConnectionPtr mConn;
        uint64_t mConnectTime {0};  // steady_clock 纳秒, 用于 FIFO 淘汰排序
    };

    // 同 clientId 的连接集合(访客可多个, 非访客通常1个)
    struct ClientItem {
        EClientType mClientType {EClientType::kTranscribe};
        std::vector<ConnEntry> mConns;
        AccountInfo mAccount;
    };

    // 从 client_id 中解析 accountId, 格式示例: "2_msg" -> 2
    // 解析失败返回 0
    uint64_t ParseAccountIdFromClientId(const std::string &clientId);

    // 从 client_id 解析 accountId 并查询用户组, 返回 AccountInfo
    // 解析或查询失败时 mAccountId 为 0
    AccountInfo ResolveAccountInfo(const std::string &clientId);

    class RealtimeDispatchManager {
    public:
        static RealtimeDispatchManager &GetInstance();

        ~RealtimeDispatchManager() = default;

        RealtimeDispatchManager(const RealtimeDispatchManager &) = delete;
        RealtimeDispatchManager &operator=(const RealtimeDispatchManager &) = delete;
        RealtimeDispatchManager(RealtimeDispatchManager &&) = delete;
        RealtimeDispatchManager &operator=(RealtimeDispatchManager &&) = delete;

        // 添加客户端连接(原子操作: 先按 FIFO 淘汰超限旧连接, 再追加新连接)
        // 连接数上限由 groupId + clientType 决定, 超限时按连接时间 FIFO 淘汰最早连接
        bool AddClient(const std::string &clientId, EClientType clientType, const drogon::WebSocketConnectionPtr &conn,
                       const AccountInfo &accountInfo);

        // 删除 clientId 的所有连接, 返回被删连接用于锁外关闭
        std::vector<drogon::WebSocketConnectionPtr> DelClient(const std::string &clientId);

        // 按连接指针删除单个连接(用于连接关闭回调), 返回是否删除成功
        bool DelClientByConn(const drogon::WebSocketConnectionPtr &conn);

        ClientItem GetClient(const std::string &clientId) const;

        // 发送给 clientId 的所有连接
        bool SendTo(const std::string &clientId, const std::string &message);

        bool Distribute(const std::string &message, EClientType clientType) const;

        // 按 clientType + groupId 双重过滤分发, 用于管理员专属通知等场景
        // (每条 ClientItem 已绑定 AccountInfo.mGroupId, 无需查库即可识别角色)
        bool DistributeToGroup(const std::string &message, EClientType clientType, uint64_t groupId) const;

        bool DistributeAll(const std::string &message) const;

        // 统计指定 clientType + groupId 的当前在线连接数, 用于判断某角色是否仍有连接
        size_t CountByGroup(EClientType clientType, uint64_t groupId) const;

        void StartHeartbeat();

        void StopHeartbeat();

    private:
        RealtimeDispatchManager() = default;

        void OnHeartbeat();

        // 在已持锁前提下, 查找并移除指定 accountId + clientType 的最早连接
        // 返回被移除的连接指针, 若无连接可移除则返回空
        drogon::WebSocketConnectionPtr EvictOldestLocked(uint64_t accountId, EClientType clientType);

        // 在已持锁前提下, 按 accountId + clientType 统计当前连接数并淘汰超限旧连接
        // 返回需关闭的连接列表(调用者在锁外执行 shutdown)
        std::vector<drogon::WebSocketConnectionPtr> EvictExcessLocked(const AccountInfo &accountInfo,
                                                                      EClientType clientType);

        mutable std::shared_mutex mMutex;
        std::unordered_map<std::string, ClientItem> mClients;
        bool mHeartbeatRunning {false};
        trantor::TimerId mTimerId = 0;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_WS_REALTIME_DISPATCH_MANAGER_H
