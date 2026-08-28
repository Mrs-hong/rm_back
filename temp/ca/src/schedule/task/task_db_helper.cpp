//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "schedule/task/task_db_helper.h"

#include "dao_managers/meeting_dao_manager.h"

namespace qifeng_ca {

    bool TaskDbHelper::UpdateAudioStatus(uint64_t accountId, const std::string &audioId, AudioStatus status,
                                         std::string_view message) {
        auto &daoMg = MeetingDaoManager::GetInstance();
        return daoMg.UpdateStatus(accountId, audioId, static_cast<int>(status), std::string(message));
    }

}  // namespace qifeng_ca
