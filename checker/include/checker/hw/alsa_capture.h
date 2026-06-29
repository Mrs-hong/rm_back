// =============================================================================
// alsa_capture.h — ALSA 录音封装（条件编译）
//
// 有 ALSA (CHECKER_HAS_ALSA=1)：以 16kHz/单声道/S16LE 录指定帧数。
// 无 ALSA：capture() 直接返回 false，由调用方决定 Skipped。
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace checker {

class AlsaCapture {
 public:
  // 打开 PCM 录音设备；成功返回 true
  bool open(const std::string& device, unsigned rate = 16000, unsigned channels = 1);
  // 录制 frames 帧；返回样本数组（交错）。失败返回空。
  std::vector<int16_t> capture(int frames);
  void close();

 private:
  void* pcm_ = nullptr;  // snd_pcm_t*，避免头文件泄漏
};

}  // namespace checker
