//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_MEETING_MEETING_ACCESS_H
#define QIFENG_CA_INCLUDE_CORE_MEETING_MEETING_ACCESS_H

#include <cstdint>
#include <string>
#include <utility>

#include "common/common.h"
#include "common/status.h"
#include "dao/models/bms_audio.h"
#include "dao/models/bms_user.h"
#include "dao_managers/meeting_dao_manager.h"
#include "dao_managers/user_dao_manager.h"

namespace qifeng_ca {

    // 全局获取音频并校验访问权限(meeting模块公共功能):
    //   管理员=全部音频, 普通用户=本人+访客, 访客=仅访客
    // 返回 {audio, status}: status 非空表示失败原因, audio 在失败时可能为零值或部分填充
    inline std::pair<models::Audio, Status> GetAccessibleAudio(uint64_t accountId, const std::string &audioId) {
        models::User user = UserDaoManager::GetInstance().GetByAccountId(accountId);
        if (user.mAccountId == 0) {
            return {models::Audio {}, Status {-1, "用户不存在"}};
        }
        models::Audio audio = MeetingDaoManager::GetInstance().GetByAudioIdGlobal(audioId);
        if (audio.mId == 0) {
            return {audio, Status {-1, "录音不存在"}};
        }
        if (!IsAudioAccessible(accountId, user.mGroupId, audio.mAccountId)) {
            return {audio, Status {-1, "无权操作"}};
        }
        return {audio, Status {}};
    }

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_MEETING_MEETING_ACCESS_H
