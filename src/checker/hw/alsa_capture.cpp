/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/hw/alsa_capture.h"

#if defined(CHECKER_HAS_ALSA) && CHECKER_HAS_ALSA
#include <alsa/asoundlib.h>
#endif

#include "qifeng_framework/common/logger.h"

namespace qifeng::scm {

#if defined(CHECKER_HAS_ALSA) && CHECKER_HAS_ALSA

// NOLINTNEXTLINE(readability-function-size)
// ALSA 的 snd_pcm_hw_params_alloca 使用 VLA，GCC 会产生 stack-protector/stack-usage 警告
// Clang 不认识 -Wstack-usage=，需用 __GNUC__ 保护
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstack-protector"
#pragma GCC diagnostic ignored "-Wstack-usage="
#endif
bool AlsaCapture::Open(const std::string &device, unsigned rate, unsigned channels) {
    snd_pcm_t *pcm = nullptr;
    int rc = snd_pcm_open(&pcm, device.c_str(), SND_PCM_STREAM_CAPTURE, 0);
    if (rc < 0) {
        SLOG_DEBUG << "alsa open " << device << " failed: " << snd_strerror(rc);
        return false;
    }
    snd_pcm_hw_params_t *hp = nullptr;
    snd_pcm_hw_params_alloca(&hp);
    snd_pcm_hw_params_any(pcm, hp);
    snd_pcm_hw_params_set_access(pcm, hp, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, hp, SND_PCM_FORMAT_S16_LE);
    unsigned r = rate;
    snd_pcm_hw_params_set_rate_near(pcm, hp, &r, nullptr);
    unsigned c = channels;
    snd_pcm_hw_params_set_channels_near(pcm, hp, &c);
    rc = snd_pcm_hw_params(pcm, hp);
    if (rc < 0) {
        SLOG_DEBUG << "alsa hw_params failed: " << snd_strerror(rc);
        snd_pcm_close(pcm);
        return false;
    }
    snd_pcm_prepare(pcm);
    mPcm = pcm;
    return true;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

std::vector<int16_t> AlsaCapture::Capture(int frames) {
    std::vector<int16_t> buf(static_cast<size_t>(frames), 0);
    if (!mPcm) { return {}; }
    snd_pcm_t *pcm = static_cast<snd_pcm_t *>(mPcm);
    auto n = snd_pcm_readi(pcm, buf.data(), static_cast<snd_pcm_uframes_t>(frames));
    if (n < 0) {
        snd_pcm_recover(pcm, static_cast<int>(n), 0);
        return {};
    }
    buf.resize(n > 0 ? static_cast<size_t>(n) : 0);
    return buf;
}

void AlsaCapture::Close() {
    if (mPcm) {
        snd_pcm_close(static_cast<snd_pcm_t *>(mPcm));
        mPcm = nullptr;
    }
}

#else  // 无 ALSA 桩实现

bool AlsaCapture::Open(const std::string &, unsigned, unsigned) { return false; }
std::vector<int16_t> AlsaCapture::Capture(int) { return {}; }
void AlsaCapture::Close() {}

#endif

}  // namespace qifeng::scm