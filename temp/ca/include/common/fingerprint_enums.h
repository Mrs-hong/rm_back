//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_FINGERPRINT_ENUMS_H
#define QIFENG_CA_INCLUDE_COMMON_FINGERPRINT_ENUMS_H

#include <string_view>

namespace qifeng_ca {

    // 指纹录入提示文案(录入流程中显示屏/WS推送统一引用此处)
    // 设计目的: 避免散落在fingerprint_enroller.cpp中的字符串字面量, 便于统一维护和国际化
    namespace FingerprintMsg {
        // 采集质量错误(单次采集可恢复)
        static constexpr std::string_view SmallContactArea = "请将手指完整覆盖指纹采集区";
        static constexpr std::string_view PoorImageQuality = "请擦拭手指或指纹采集区后重新录入";

        // 采集结果错误(不可恢复, 终止录入)
        static constexpr std::string_view Timeout = "超时录入，重新采集指纹";
        static constexpr std::string_view StorageFull = "指纹存储已满，请删除不需要的用户指纹并重新注册";
        static constexpr std::string_view EnrollFailed = "指纹录入失败，建议更换手指重试";
        static constexpr std::string_view CaptureFail = "采集失败，请重新采集";
        static constexpr std::string_view IdentifyFailed = "识别失败，请重新录入";
        static constexpr std::string_view NotFound = "未找到匹配指纹，请重新录入";

        // 通用错误兜底
        static constexpr std::string_view SystemBusy = "系统繁忙，请重试";

        // 重复指纹(后缀需拼接已存在用户名, 使用DuplicateFingerPrefix)
        static constexpr std::string_view DuplicateFinger = "已有指纹,请勿重复录入";
        static constexpr std::string_view DuplicateFingerPrefix = "已有指纹";  // 例如："已有指纹张三,请勿重复录入"
        static constexpr std::string_view DuplicateFingerSuffix = ",请勿重复录入";

        // 录入流程状态提示
        static constexpr std::string_view CaptureSuccess = "采集成功，请继续录入";                 // 单次采集成功
        static constexpr std::string_view EnrollComplete = "指纹录入完成";                         // 全部采集完成
        static constexpr std::string_view EnrollCancelled = "指纹录入已取消";                      // 用户主动取消
        static constexpr std::string_view MaxFailuresExceeded = "指纹录入失败，建议更换手指重试";  // 失败次数达上限
        static constexpr std::string_view FingerprintOffline = "指纹掉线";
    }  // namespace FingerprintMsg

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_FINGERPRINT_ENUMS_H
