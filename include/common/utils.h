/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

// umbrella 头文件：聚合所有工具函数子头文件，向后兼容
// 调用方仍可 #include "common/utils.h" 获取全部工具函数
// 新代码建议直接 include 需要的子头文件，减少编译依赖

#include "common/utils/dependency.h"
#include "common/utils/file.h"
#include "common/utils/password.h"
#include "common/utils/path.h"
#include "common/utils/string.h"
#include "common/utils/systemd_time.h"
#include "common/utils/upgrade.h"
#include "common/utils/user.h"
#include "common/utils/version.h"
#include "common/utils/yaml_resolve.h"
