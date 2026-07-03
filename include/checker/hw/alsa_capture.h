/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace qifeng::scm {

/**
 * @brief ALSA 录音封装（条件编译）
 * @details 有 ALSA (CHECKER_HAS_ALSA=1)：以 16kHz/单声道/S16LE 录指定帧数。
 * 无 ALSA：Capture() 直接返回 false，由调用方决定 Skipped。
 */
class AlsaCapture {
public:
    /**
     * @brief 打开 PCM 录音设备
     * @param device 设备名
     * @param rate 采样率
     * @param channels 声道数
     * @return 成功返回 true
     */
    bool Open(const std::string &device, unsigned rate = 16000, unsigned channels = 1);

    /**
     * @brief 录制 frames 帧
     * @param frames 帧数
     * @return 样本数组（交错）。失败返回空。
     */
    std::vector<int16_t> Capture(int frames);

    /**
     * @brief 关闭录音设备
     */
    void Close();

private:
    void *mPcm = nullptr;  // snd_pcm_t*，避免头文件泄漏
};

}  // namespace qifeng::scm