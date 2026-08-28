/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_FINGERPRINT_PROTOCOL_HLK_CODE_H
#define HAL_FINGERPRINT_PROTOCOL_HLK_CODE_H

#include <cstdint>

namespace qifeng {
    namespace HlkCmd {
        // 注册流程
        inline constexpr uint16_t Enroll = 0x0111;
        inline constexpr uint16_t EnrollQuery = 0x0112;
        inline constexpr uint16_t Save = 0x0113;
        inline constexpr uint16_t SaveQuery = 0x0114;
        inline constexpr uint16_t Cancel = 0x0115;

        // 自学习
        inline constexpr uint16_t SelfLearn = 0x0116;

        // 匹配流程
        inline constexpr uint16_t FingerStatus = 0x0135;
        inline constexpr uint16_t Verify = 0x0121;
        inline constexpr uint16_t VerifyQuery = 0x0122;

        // 管理
        inline constexpr uint16_t DeleteFinger = 0x0131;
        inline constexpr uint16_t DeleteFingerQuery = 0x0132;

        // LED 控制
        inline constexpr uint16_t LedControl = 0x020F;

        inline const char* Name(uint16_t cmd) {
            switch (cmd) {
                case Enroll:            return "Enroll";
                case EnrollQuery:       return "EnrollQuery";
                case Save:              return "Save";
                case SaveQuery:         return "SaveQuery";
                case Cancel:            return "Cancel";
                case SelfLearn:         return "SelfLearn";
                case FingerStatus:      return "FingerStatus";
                case Verify:            return "Verify";
                case VerifyQuery:       return "VerifyQuery";
                case DeleteFinger:      return "DeleteFinger";
                case DeleteFingerQuery: return "DeleteFingerQuery";
                case LedControl:        return "LedControl";
                default:                return "Unknown";
            }
        }
    }  // namespace HlkCmd

    namespace HlkResp {
        inline constexpr uint32_t Ok = 0x00000000;
        inline constexpr uint32_t UnknownCmd = 0x00000001;
        inline constexpr uint32_t InvalidDataLen = 0x00000002;
        inline constexpr uint32_t InvalidData = 0x00000003;
        inline constexpr uint32_t Busy = 0x00000004;
        inline constexpr uint32_t NoRequest = 0x00000005;
        inline constexpr uint32_t SoftwareError = 0x00000006;
        inline constexpr uint32_t HardwareError = 0x00000007;
        inline constexpr uint32_t FingerTimeout = 0x00000008;
        inline constexpr uint32_t ExtractFail = 0x00000009;
        inline constexpr uint32_t MatchFail = 0x0000000A;
        inline constexpr uint32_t StorageFull = 0x0000000B;
        inline constexpr uint32_t StorageWriteFail = 0x0000000C;
        inline constexpr uint32_t StorageReadFail = 0x0000000D;
        inline constexpr uint32_t PoorImageQuality = 0x0000000E;
        inline constexpr uint32_t DuplicateFinger = 0x0000000F;
        inline constexpr uint32_t SmallContactArea = 0x00000010;
        inline constexpr uint32_t MoveTooMuch = 0x00000011;
        inline constexpr uint32_t MoveTooLittle = 0x00000012;
        inline constexpr uint32_t IdOccupied = 0x00000013;
        inline constexpr uint32_t CaptureFail = 0x00000014;
        inline constexpr uint32_t Aborted = 0x00000015;
        inline constexpr uint32_t NoUpdateNeeded = 0x00000016;
        inline constexpr uint32_t InvalidId = 0x00000017;
        inline constexpr uint32_t GainAdjustFail = 0x00000018;
        inline constexpr uint32_t BufferOverflow = 0x00000019;
        inline constexpr uint32_t SensorSleeping = 0x0000001A;
        inline constexpr uint32_t ChecksumError = 0x0000001C;
        inline constexpr uint32_t FlashWriteFail = 0x00000022;
        inline constexpr uint32_t Other = 0x000000FF;
    }  // namespace HlkResp
}  // namespace qifeng

#endif  // HAL_FINGERPRINT_PROTOCOL_HLK_CODE_H
