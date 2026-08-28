/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_CA_INCLUDE_DAO_BASE_DAO_H
#define QIFENG_CA_INCLUDE_DAO_BASE_DAO_H

#include "qifeng_framework/dao/db_pool.h"
#include "soci/soci.h"

namespace qifeng_ca {
    class BmsBaseDao {
    public:
        BmsBaseDao() {}
        virtual ~BmsBaseDao() {}

        // 禁止拷贝和赋值
        BmsBaseDao(const BmsBaseDao &) = delete;
        BmsBaseDao &operator=(const BmsBaseDao &) = delete;
        BmsBaseDao(BmsBaseDao &&) = delete;
        BmsBaseDao &operator=(BmsBaseDao &&) = delete;

    protected:
        // 获取数据库会话，并显式设置连接字符集为 utf8mb4
        // 避免因客户端连接字符集与服务端不匹配导致中文乱码
        soci::session GetSession() { return DBPool::GetInstance().Get(); }
    };
}  // namespace qifeng_ca
#endif  // QIFENG_CA_INCLUDE_DAO_BASE_DAO_H
