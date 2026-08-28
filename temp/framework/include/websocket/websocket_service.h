/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_COMMON_WEBSOCKET_SERVICE_H
#define QIFENG_FRAMEWORK_COMMON_WEBSOCKET_SERVICE_H

#include <atomic>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>

#include "libwebsockets.h"

class WebSocketService {
public:
    static WebSocketService& Instance();

    WebSocketService(const WebSocketService&) = delete;
    WebSocketService& operator=(const WebSocketService&) = delete;
    WebSocketService(WebSocketService&&) = delete;
    WebSocketService& operator=(WebSocketService&&) = delete;

    bool Start(const int& port);
    bool PushMessage(const std::string& message);
    void Stop();

private:
    WebSocketService();
    ~WebSocketService();

    bool CreateContext(const int& port);
    bool StartEventThread();
    void EventLoop();
    int HandleFilterProtocolConnection(struct lws* wsi);
    void HandleEstablished(struct lws* wsi);
    void HandleClosed(struct lws* wsi);
    int HandleReceiveMessage(struct lws* wsi, const void* inputData, size_t length);
    int HandleServerWriteable(struct lws* wsi);
    int HandleCallbackReason(struct lws* wsi, lws_callback_reasons reason, const void* inputData, size_t length);

    static int CallbackWebSocket(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);

    bool PopMessage(std::string& message);

private:
    struct lws_context* mContext;
    std::atomic<bool> mIsRunning;
    std::thread mEventThread;

    std::set<lws*> mClients;
    std::mutex mClientsMutex;

    std::queue<std::string> mSendQueue;
    std::mutex mQueueMutex;

    // WebSocket 子协议名（Sec-WebSocket-Protocol）
    static constexpr const char* ProcName = "ws-transcriptions";
    // 握手 URL 路径
    static constexpr const char* ProcPath = "/ws/transcriptions";
    // 接收缓冲区大小
    static constexpr size_t ProcBufferSize = 8192;
    // 最多阻塞等待事件的毫秒数
    static constexpr size_t ProcMaxWaitMs = 10;
};
#endif  // QIFENG_FRAMWORK_COMMON_WEBSOCKET_SERVICE_H
