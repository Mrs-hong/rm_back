/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 *

 */
#include "common/logger.h"
#include "redis/redis.h"

RedisService& RedisService::Instance() {
    static RedisService RedisService;
    return RedisService;
}

void RedisService::PrintTaskError(int state, int error) {
    switch (state) {
        case WFT_STATE_SYS_ERROR:
            SLOG_ERROR << "[Redis ERROR] system error: " << std::strerror(error);
            break;
        case WFT_STATE_DNS_ERROR:
            SLOG_ERROR << "[Redis ERROR] DNS error: " << gai_strerror(error);
            break;
        case WFT_STATE_SSL_ERROR:
            SLOG_ERROR << "[Redis ERROR] SSL error: " << error;
            break;
        case WFT_STATE_TASK_ERROR:
            SLOG_ERROR << "[Redis ERROR] task error: " << error;
            break;
        default:
            SLOG_ERROR << "[Redis ERROR] unknown task state: " << state << ", error: " << error;
            break;
    }
}

void RedisService::PrintRedisResult(const std::string& command, const protocol::RedisValue& value) {
    if (value.is_error()) {
        const std::string* errorText = value.string_view();
        SLOG_ERROR << "[Redis ERROR] " << command
                   << " command failed: " << (errorText == nullptr ? "unknown redis error" : errorText->c_str());
        return;
    }

    if (value.is_nil()) {
        SLOG_INFO << "[Redis INFO] " << command << " result: (nil)";
        return;
    }

    if (value.is_int()) {
        SLOG_INFO << "[Redis INFO] " << command << " result: " << static_cast<long long>(value.int_value());
        return;
    }

    if (value.is_string()) {
        SLOG_INFO << "[Redis INFO] " << command << " result: " << value.string_value();
        return;
    }

    SLOG_INFO << "[Redis INFO] " << command << " result type: " << value.get_type();
}

void RedisService::RedisTaskCallback(WFRedisTask* task) {
    const int state = task->get_state();
    const int error = task->get_error();

    std::string command;
    task->get_req()->get_command(command);

    if (state != WFT_STATE_SUCCESS) {
        PrintTaskError(state, error);
        series_of(task)->cancel();
        return;
    }

    protocol::RedisValue result;
    task->get_resp()->get_result(result);
    if (result.is_error()) {
        PrintRedisResult(command, result);
        series_of(task)->cancel();
        return;
    }

    PrintRedisResult(command, result);
}

WFRedisTask* RedisService::CreateRedisTask(const RedisDemoConfig& config, const std::string& command,
                                           const std::vector<std::string>& params) {
    WFRedisTask* task = WFTaskFactory::create_redis_task(config.redisUrl, config.redisRetryTimes, RedisTaskCallback);
    task->get_req()->set_request(command, params);
    return task;
}
