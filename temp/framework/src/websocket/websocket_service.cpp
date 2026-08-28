/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#include <array>
#include <cstring>
#include <exception>
#include <system_error>
#include <vector>

#include "common/logger.h"
#include "websocket/websocket_service.h"
WebSocketService& WebSocketService::Instance() {
    static WebSocketService Service;
    return Service;
}

WebSocketService::WebSocketService() : mContext(nullptr), mIsRunning(false) {
}

WebSocketService::~WebSocketService() {
    Stop();
}

bool WebSocketService::Start(const int& port) {
    if (mIsRunning) {
        SLOG_WARN << "WebSocket server already running";
        return false;
    }

    const bool isContextCreated = CreateContext(port);
    if (!isContextCreated) {
        return false;
    }

    const bool isEventThreadStarted = StartEventThread();
    if (!isEventThreadStarted) {
        lws_context_destroy(mContext);
        mContext = nullptr;
        return false;
    }

    SLOG_INFO << "WebSocket server listening on ws://127.0.0.1:" << port << ProcPath;
    return true;
}

bool WebSocketService::CreateContext(const int& port) {
    lws_context_creation_info info {};
    std::memset(&info, 0, sizeof(info));

    static std::array<lws_protocols, 2> Protocols = {{
        {
            ProcName,
            CallbackWebSocket,
            0,
            ProcBufferSize,
            0,
            nullptr,
            0,
        },
        LWS_PROTOCOL_LIST_TERM,
    }};

    info.port = port;
    info.protocols = Protocols.data();
    info.user = this;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    mContext = lws_create_context(&info);
    if (mContext == nullptr) {
        SLOG_ERROR << "Failed to create lws context";
        return false;
    }

    return true;
}

bool WebSocketService::StartEventThread() {
    mIsRunning = true;
    try {
        mEventThread = std::thread(&WebSocketService::EventLoop, this);
        return true;
    } catch (const std::system_error& error) {
        SLOG_ERROR << "Failed to create WebSocket event thread: " << error.what();
    } catch (const std::exception& error) {
        SLOG_ERROR << "Failed to create WebSocket event thread: " << error.what();
    }

    mIsRunning = false;
    return false;
}

void WebSocketService::Stop() {
    if (!mIsRunning) {
        return;
    }

    mIsRunning = false;

    if (mEventThread.joinable()) {
        mEventThread.join();
    }

    if (mContext != nullptr) {
        lws_context_destroy(mContext);
        mContext = nullptr;
    }

    std::lock_guard<std::mutex> lock(mClientsMutex);
    mClients.clear();
}

bool WebSocketService::PushMessage(const std::string& message) {
    if (!mIsRunning) {
        return false;
    }

    {
        std::lock_guard<std::mutex> clientsLock(mClientsMutex);
        if (mClients.empty()) {
            return true;
        }
    }

    {
        std::lock_guard<std::mutex> queueLock(mQueueMutex);
        mSendQueue.push(message);
    }

    std::lock_guard<std::mutex> clientsLock(mClientsMutex);
    for (auto* wsi : mClients) {
        lws_callback_on_writable(wsi);
    }

    return true;
}

void WebSocketService::EventLoop() {
    while (mIsRunning) {
        // 底层用的epoll_wait
        lws_service(mContext, ProcMaxWaitMs);
    }
}

bool WebSocketService::PopMessage(std::string& message) {
    std::lock_guard<std::mutex> queueLock(mQueueMutex);
    if (mSendQueue.empty()) {
        return false;
    }

    message = mSendQueue.front();
    mSendQueue.pop();
    return true;
}

// NOLINTNEXTLINE(readability-function-size),回调函数必须有5个参数
int WebSocketService::CallbackWebSocket(struct lws* wsi, lws_callback_reasons reason, void* /*user*/, void* in,
                                        size_t len) {
    auto* self = static_cast<WebSocketService*>(lws_context_user(lws_get_context(wsi)));
    if (self == nullptr) {
        return -1;
    }

    return self->HandleCallbackReason(wsi, reason, in, len);
}

int WebSocketService::HandleCallbackReason(struct lws* wsi, lws_callback_reasons reason, const void* inputData,
                                           size_t length) {
    if (reason == LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION) {
        return HandleFilterProtocolConnection(wsi);
    }

    if (reason == LWS_CALLBACK_ESTABLISHED) {
        HandleEstablished(wsi);
        return 0;
    }

    if (reason == LWS_CALLBACK_CLOSED) {
        HandleClosed(wsi);
        return 0;
    }

    if (reason == LWS_CALLBACK_RECEIVE) {
        return HandleReceiveMessage(wsi, inputData, length);
    }

    if (reason == LWS_CALLBACK_SERVER_WRITEABLE) {
        return HandleServerWriteable(wsi);
    }

    return 0;
}

int WebSocketService::HandleReceiveMessage(struct lws* /*wsi*/, const void* inputData, size_t length) {
    if (inputData == nullptr || length == 0) {
        return 0;
    }

    const auto* messageData = static_cast<const char*>(inputData);
    const std::string clientMessage(messageData, length);
    SLOG_DEBUG << "Received client message: " << clientMessage;
    return 0;
}

int WebSocketService::HandleFilterProtocolConnection(struct lws* wsi) {
    std::array<char, 256> uri {};
    if (lws_hdr_copy(wsi, uri.data(), uri.size(), WSI_TOKEN_GET_URI) <= 0) {
        return -1;
    }

    if (std::string(uri.data()) != ProcPath) {
        return -1;
    }

    return 0;
}

void WebSocketService::HandleEstablished(struct lws* wsi) {
    {
        std::lock_guard<std::mutex> lock(mClientsMutex);
        mClients.insert(wsi);
    }

    bool hasMessage = false;
    {
        std::lock_guard<std::mutex> lock(mQueueMutex);
        hasMessage = !mSendQueue.empty();
    }

    if (hasMessage) {
        lws_callback_on_writable(wsi);
    }

    SLOG_INFO << "Client connected";
}

void WebSocketService::HandleClosed(struct lws* wsi) {
    std::lock_guard<std::mutex> lock(mClientsMutex);
    mClients.erase(wsi);
    SLOG_INFO << "Client disconnected";
}

int WebSocketService::HandleServerWriteable(struct lws* wsi) {
    std::string message;
    const bool hasMessage = PopMessage(message);
    if (!hasMessage) {
        return 0;
    }

    std::vector<unsigned char> buffer(LWS_PRE + message.size());
    std::memcpy(buffer.data() + LWS_PRE, message.data(), message.size());

    const int writeRet = lws_write(wsi, buffer.data() + LWS_PRE, static_cast<size_t>(message.size()), LWS_WRITE_TEXT);
    if (writeRet < 0) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(mQueueMutex);
    if (!mSendQueue.empty()) {
        lws_callback_on_writable(wsi);
    }

    return 0;
}
