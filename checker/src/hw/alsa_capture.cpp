// =============================================================================
// alsa_capture.cpp — ALSA 录音封装
//   有 ALSA：真实录音
//   无 ALSA：open()/capture() 返回失败，由调用方决定 Skipped
// =============================================================================
#include "checker/hw/alsa_capture.h"

#if defined(CHECKER_HAS_ALSA) && CHECKER_HAS_ALSA
#include <alsa/asoundlib.h>
#endif

#include "checker/log/logger.hpp"

namespace checker {

#if defined(CHECKER_HAS_ALSA) && CHECKER_HAS_ALSA

bool AlsaCapture::open(const std::string& device, unsigned rate, unsigned channels) {
  snd_pcm_t* pcm = nullptr;
  int rc = snd_pcm_open(&pcm, device.c_str(), SND_PCM_STREAM_CAPTURE, 0);
  if (rc < 0) {
    LOG_DEBUG("alsa open %s failed: %s", device.c_str(), snd_strerror(rc));
    return false;
  }
  snd_pcm_hw_params_t* hp;
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
    LOG_DEBUG("alsa hw_params failed: %s", snd_strerror(rc));
    snd_pcm_close(pcm);
    return false;
  }
  snd_pcm_prepare(pcm);
  pcm_ = pcm;
  return true;
}

std::vector<int16_t> AlsaCapture::capture(int frames) {
  std::vector<int16_t> buf(frames, 0);
  if (!pcm_) return {};
  snd_pcm_t* pcm = static_cast<snd_pcm_t*>(pcm_);
  int n = snd_pcm_readi(pcm, buf.data(), frames);
  if (n < 0) {
    snd_pcm_recover(pcm, n, 0);
    return {};
  }
  buf.resize(n > 0 ? n : 0);
  return buf;
}

void AlsaCapture::close() {
  if (pcm_) {
    snd_pcm_close(static_cast<snd_pcm_t*>(pcm_));
    pcm_ = nullptr;
  }
}

#else  // 无 ALSA 桩实现

bool AlsaCapture::open(const std::string&, unsigned, unsigned) { return false; }
std::vector<int16_t> AlsaCapture::capture(int) { return {}; }
void AlsaCapture::close() {}

#endif

}  // namespace checker
