/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_MODESTYPE_H
#define QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_MODESTYPE_H

namespace qifeng {
    enum class EModelType {
        SV,   // 声纹识别模型
        VAD,  // 语音活动检测模型
        ASR,  // 自动语音识别模型
        PUNC  // 标点预测模型
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_MODESTYPE_H
