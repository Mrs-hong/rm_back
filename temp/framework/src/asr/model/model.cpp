/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "asr/model.h"

namespace qifeng {

    namespace asr {

        ModelContext::CanceledException::CanceledException(const char* msg) : std::runtime_error {msg} {
        }

        ModelContext::CanceledException::CanceledException(const std::string& msg) : std::runtime_error {msg} {
        }

        ModelContext::ModelContext(std::shared_ptr<Model> model) : mModel {std::move(model)} {
        }

    }  // namespace asr

}  // namespace qifeng