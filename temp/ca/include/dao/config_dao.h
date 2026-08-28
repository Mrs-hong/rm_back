//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_DAO_CONFIG_DAO_H
#define QIFENG_CA_INCLUDE_DAO_CONFIG_DAO_H

#include <string>
#include <vector>

#include "dao/bms_base_dao.h"
#include "dao/models/bms_config.h"

namespace qifeng_ca {

    class ConfigDao : public BmsBaseDao {
    public:
        ConfigDao() = default;
        ~ConfigDao() override = default;

        ConfigDao(const ConfigDao &) = delete;
        ConfigDao &operator=(const ConfigDao &) = delete;
        ConfigDao(ConfigDao &&) = delete;
        ConfigDao &operator=(ConfigDao &&) = delete;

        models::SystemConfig GetByKey(const std::string &key);

        std::vector<models::SystemConfig> GetAll();

        bool Upsert(const models::SystemConfig &config);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_CONFIG_DAO_H
