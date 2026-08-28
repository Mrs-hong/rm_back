/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_REDIS_REDIS_H
#define QIFENG_FRAMEWORK_INCLUDE_REDIS_REDIS_H
#include <cstring>
#include <netdb.h>
#include <string>
#include <vector>

#include "workflow/RedisMessage.h"
#include "workflow/WFTaskFactory.h"
struct RedisDemoConfig {
    std::string redisServer;
    std::string redisUrl;
    int redisRetryTimes = 3;
    int redisPort = 6379;
};

class RedisService {
public:
    static RedisService& Instance();

    static void PrintTaskError(int state, int error);
    static void PrintRedisResult(const std::string& command, const protocol::RedisValue& value);
    static void RedisTaskCallback(WFRedisTask* task);
    WFRedisTask* CreateRedisTask(const RedisDemoConfig& config, const std::string& command,
                                 const std::vector<std::string>& params);
};

#endif