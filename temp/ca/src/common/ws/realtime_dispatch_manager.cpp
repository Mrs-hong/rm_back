//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <HttpAppFramework.h>
#include <cctype>
#include <common/utils/time.h>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "drogon/drogon.h"
#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"

#include "common/common.h"
#include "common/config/ws_config.h"
#include "common/ws/realtime_dispatch_manager.h"
#include "common/ws/system_message_notifier.h"
#include "dao_managers/user_dao_manager.h"

namespace qifeng_ca {

    uint64_t ParseAccountIdFromClientId(const std::string &clientId) {
        auto pos = clientId.find('_');
        if (pos == std::string::npos || pos == 0) {
            return 0;
        }
        std::string numPart = clientId.substr(0, pos);
        for (char c : numPart) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                return 0;
            }
        }
        try {
            return std::stoull(numPart);
        } catch (...) {
            return 0;
        }
    }

    AccountInfo ResolveAccountInfo(const std::string &clientId) {
        uint64_t accountId = ParseAccountIdFromClientId(clientId);
        if (accountId == 0) {
            return {};
        }
        auto user = UserDaoManager::GetInstance().GetByAccountId(accountId);
        if (user.mAccountId == 0) {
            return {};
        }
        return {user.mAccountId, user.mGroupId};
    }

    RealtimeDispatchManager &RealtimeDispatchManager::GetInstance() {
        static RealtimeDispatchManager Instance;
        return Instance;
    }

    std::vector<drogon::WebSocketConnectionPtr>
    RealtimeDispatchManager::EvictExcessLocked(const AccountInfo &accountInfo, EClientType clientType) {
        std::vector<drogon::WebSocketConnectionPtr> toShutdown;
        if (accountInfo.mAccountId == 0) {
            return toShutdown;
        }
        int maxConns = (accountInfo.mGroupId == Authority::GUEST)
                           ? WsConfig::GetInstance().GetGuestMaxConnections()
                           : WsConfig::GetInstance().GetNormalUserMaxConnections();
        if (maxConns <= 0) {
            return toShutdown;
        }
        int count = 0;
        for (const auto &[id, item] : mClients) {
            if (item.mAccount.mAccountId == accountInfo.mAccountId && item.mClientType == clientType) {
                count += static_cast<int>(item.mConns.size());
            }
        }
        while (count >= maxConns) {
            auto evicted = EvictOldestLocked(accountInfo.mAccountId, clientType);
            if (!evicted) {
                break;
            }
            toShutdown.push_back(evicted);
            --count;
        }
        return toShutdown;
    }

    bool RealtimeDispatchManager::AddClient(const std::string &clientId, EClientType clientType,
                                            const drogon::WebSocketConnectionPtr &conn,
                                            const AccountInfo &accountInfo) {
        std::vector<drogon::WebSocketConnectionPtr> toShutdown;
        {
            std::unique_lock lock(mMutex);
            // 在同一锁内执行淘汰+添加, 避免并发连接绕过限制
            toShutdown = EvictExcessLocked(accountInfo, clientType);
            auto it = mClients.find(clientId);
            if (it == mClients.end()) {
                ClientItem item;
                item.mClientType = clientType;
                item.mAccount = accountInfo;
                item.mConns.push_back({conn, GetTimeMs()});
                mClients[clientId] = std::move(item);
            } else {
                it->second.mConns.push_back({conn, GetTimeMs()});
            }
            SLOG_INFO << "AddClient: " << clientId << " type=" << static_cast<int>(clientType)
                      << " account=" << accountInfo.mAccountId << " group=" << accountInfo.mGroupId
                      << " conns=" << mClients[clientId].mConns.size() << " evicted=" << toShutdown.size();
        }
        // 在锁外关闭被淘汰的连接, 避免 handleConnectionClosed 回调死锁
        for (auto &c : toShutdown) {
            if (c) {
                SLOG_DEBUG << "RealtimeDispatchManager: closing old connection";
                // 针对访客: 先向被淘汰的旧连接发送重登录通知, 再关闭
                if (accountInfo.mGroupId == Authority::GUEST) {
                    SystemMessageNotifier::GetInstance().SendUserReloginToConn(c);
                }
                c->forceClose();
                c->shutdown();
            }
        }
        return true;
    }

    drogon::WebSocketConnectionPtr RealtimeDispatchManager::EvictOldestLocked(uint64_t accountId,
                                                                              EClientType clientType) {
        auto oldestMapIt = mClients.end();
        size_t oldestIdx = 0;
        uint64_t oldestTime = std::numeric_limits<uint64_t>::max();
        for (auto it = mClients.begin(); it != mClients.end(); ++it) {
            if (it->second.mAccount.mAccountId != accountId || it->second.mClientType != clientType) {
                continue;
            }
            for (size_t i = 0; i < it->second.mConns.size(); ++i) {
                if (it->second.mConns[i].mConnectTime < oldestTime) {
                    oldestTime = it->second.mConns[i].mConnectTime;
                    oldestMapIt = it;
                    oldestIdx = i;
                }
            }
        }
        if (oldestMapIt == mClients.end()) {
            return {};
        }
        auto conn = oldestMapIt->second.mConns[oldestIdx].mConn;
        oldestMapIt->second.mConns.erase(oldestMapIt->second.mConns.begin() + static_cast<ptrdiff_t>(oldestIdx));
        if (oldestMapIt->second.mConns.empty()) {
            mClients.erase(oldestMapIt);
        }
        SLOG_INFO << "EvictOldestLocked: evict for account " << accountId << " type=" << static_cast<int>(clientType);
        return conn;
    }

    std::vector<drogon::WebSocketConnectionPtr> RealtimeDispatchManager::DelClient(const std::string &clientId) {
        std::vector<drogon::WebSocketConnectionPtr> conns;
        std::unique_lock lock(mMutex);
        auto it = mClients.find(clientId);
        if (it == mClients.end()) {
            return conns;
        }
        for (auto &entry : it->second.mConns) {
            conns.push_back(entry.mConn);
        }
        mClients.erase(it);
        SLOG_INFO << "DelClient: " << clientId << " conns=" << conns.size();
        return conns;
    }

    bool RealtimeDispatchManager::DelClientByConn(const drogon::WebSocketConnectionPtr &conn) {
        std::unique_lock lock(mMutex);
        for (auto mapIt = mClients.begin(); mapIt != mClients.end(); ++mapIt) {
            auto &conns = mapIt->second.mConns;
            auto connIt = std::find_if(conns.begin(), conns.end(),
                                       [&](const ConnEntry &e) { return e.mConn.get() == conn.get(); });
            if (connIt == conns.end()) {
                continue;
            }
            std::string clientId = mapIt->first;
            conns.erase(connIt);
            if (conns.empty()) {
                mClients.erase(mapIt);
            }
            SLOG_INFO << "DelClientByConn: removed connection for client: " << clientId;
            return true;
        }
        return false;
    }

    ClientItem RealtimeDispatchManager::GetClient(const std::string &clientId) const {
        std::shared_lock lock(mMutex);
        auto it = mClients.find(clientId);
        if (it != mClients.end()) {
            return it->second;
        }
        return {};
    }

    bool RealtimeDispatchManager::SendTo(const std::string &clientId, const std::string &message) {
        std::shared_lock lock(mMutex);
        auto it = mClients.find(clientId);
        if (it == mClients.end() || it->second.mConns.empty()) {
            SLOG_WARN << "SendTo: client not found: " << clientId;
            return false;
        }
        for (auto &entry : it->second.mConns) {
            entry.mConn->send(message);
        }
        return true;
    }

    bool RealtimeDispatchManager::Distribute(const std::string &message, EClientType clientType) const {
        std::shared_lock lock(mMutex);
        for (const auto &[id, item] : mClients) {
            if (item.mClientType == clientType) {
                for (const auto &entry : item.mConns) {
                    entry.mConn->send(message);
                }
            }
        }
        return true;
    }

    bool RealtimeDispatchManager::DistributeToGroup(const std::string &message, EClientType clientType,
                                                    uint64_t groupId) const {
        std::shared_lock lock(mMutex);
        for (const auto &[id, item] : mClients) {
            // 双重过滤: clientType + groupId(角色)
            if (item.mClientType == clientType && item.mAccount.mGroupId == groupId) {
                for (const auto &entry : item.mConns) {
                    entry.mConn->send(message);
                }
            }
        }
        return true;
    }

    bool RealtimeDispatchManager::DistributeAll(const std::string &message) const {
        std::shared_lock lock(mMutex);
        for (const auto &[id, item] : mClients) {
            for (const auto &entry : item.mConns) {
                entry.mConn->send(message);
            }
        }
        return true;
    }

    size_t RealtimeDispatchManager::CountByGroup(EClientType clientType, uint64_t groupId) const {
        std::shared_lock lock(mMutex);
        size_t count = 0;
        for (const auto &[id, item] : mClients) {
            if (item.mClientType == clientType && item.mAccount.mGroupId == groupId) {
                count += item.mConns.size();
            }
        }
        return count;
    }

    void RealtimeDispatchManager::OnHeartbeat() {
        if (!mHeartbeatRunning) {
            return;
        }
        DistributeAll(R"({"heart":true})");
    }

    void RealtimeDispatchManager::StartHeartbeat() {
        if (mHeartbeatRunning) {
            return;
        }
        mHeartbeatRunning = true;
        int32_t interval = WsConfig::GetInstance().GetHeartbeatIntervalSeconds();
        auto loop = drogon::app().getLoop();
        loop->queueInLoop([this, loop, interval]() {
            if (!mHeartbeatRunning) {
                return;  // 启动过程中被停止
            }
            // 创建周期性定时器
            mTimerId = loop->runEvery(interval, [this]() { OnHeartbeat(); });
            SLOG_INFO << "Heartbeat started with interval " << interval << "s";
        });
    }

    void RealtimeDispatchManager::StopHeartbeat() {
        mHeartbeatRunning = false;
        auto loop = drogon::app().getLoop();
        loop->queueInLoop([this]() {
            if (mTimerId) {
                drogon::app().getLoop()->invalidateTimer(mTimerId);  // 取消定时器
            }
            SLOG_INFO << "Heartbeat stopped";
        });
    }

}  // namespace qifeng_ca
