# =============================================================================
# BM1684 边缘设备开机自检工程 (bm1684-selftest)
# 独立 C++17 CMake 工程，与上层 leveldb 仓库无依赖关系。
# 目标平台: ARM Linux (BM1684)；本机 x86 WSL2 仅用于框架与可运行项验证。
# =============================================================================
cmake_minimum_required(VERSION 3.14)
project(bm1684_selftest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 默认构建类型为 Release，兼顾自检性能
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# 严格告警
add_compile_options(-Wall -Wextra -Wpedantic)

# ----------------------------------------------------------------------------
# 选项：
#   WITH_BM1684_SDK : 是否启用 Sophon SDK (TPU/模型推理)，默认 AUTO 探测
#   WITH_ALSA       : 是否启用 ALSA (麦克风)，默认 AUTO 探测
#   WITH_DRM        : 是否启用 libdrm (显示器)，默认 AUTO 探测
# 全部探测失败时对应检查器在运行期返回 Skipped，不影响整体构建。
# ----------------------------------------------------------------------------
option(WITH_BM1684_SDK "Enable Sophon SDK (bmlib/bmrt)" ON)
option(WITH_ALSA "Enable ALSA microphone capture" ON)
option(WITH_DRM "Enable libdrm display probing" ON)

set(BM_SDK_ROOT "/opt/sophon/libsophon-current" CACHE PATH "Sophon SDK root")

find_package(Threads REQUIRED)

# --- 第三方库 (vendored, header-only) ----------------------------------------
# nlohmann/json  : third_party/nlohmann/json.hpp  (include 根 = third_party)
# spdlog         : third_party/spdlog/spdlog/     (include 根 = third_party/spdlog)
# 均为 header-only，无需链接；spdlog 使用内置 bundled fmt。
add_library(checker_third_party INTERFACE)
target_include_directories(checker_third_party INTERFACE
  ${CMAKE_SOURCE_DIR}/third_party
  ${CMAKE_SOURCE_DIR}/third_party/spdlog
)

# --- Sophon SDK 探测 ---------------------------------------------------------
set(CHECKER_HAS_BM_SDK 0)
if(WITH_BM1684_SDK)
  find_path(BM_SDK_INCLUDE_DIR
    NAMES bmlib_runtime.h
    PATHS ${BM_SDK_ROOT}/include /opt/sophon/libsophon-current/include
    NO_DEFAULT_PATH)
  find_library(BM_LIB_BMLIB NAMES bmlib
    PATHS ${BM_SDK_ROOT}/lib /opt/sophon/libsophon-current/lib NO_DEFAULT_PATH)
  find_library(BM_LIB_BMRT NAMES bmrt
    PATHS ${BM_SDK_ROOT}/lib /opt/sophon/libsophon-current/lib NO_DEFAULT_PATH)
  find_library(BM_LIB_BMCV NAMES bmcv
    PATHS ${BM_SDK_ROOT}/lib /opt/sophon/libsophon-current/lib NO_DEFAULT_PATH)
  if(BM_SDK_INCLUDE_DIR AND BM_LIB_BMLIB AND BM_LIB_BMRT AND BM_LIB_BMCV)
    set(CHECKER_HAS_BM_SDK 1)
    message(STATUS "Sophon SDK: found (${BM_SDK_ROOT})")
  else()
    message(WARNING "Sophon SDK not found, TPU/model checkers will be Skipped at runtime")
  endif()
endif()

# --- ALSA 探测 ---------------------------------------------------------------
set(CHECKER_HAS_ALSA 0)
if(WITH_ALSA)
  find_path(ALSA_INCLUDE_DIR NAMES asoundlib.h PATH_SUFFIXES alsa)
  find_library(ALSA_LIB NAMES asound)
  if(ALSA_INCLUDE_DIR AND ALSA_LIB)
    set(CHECKER_HAS_ALSA 1)
    message(STATUS "ALSA: found")
  else()
    message(STATUS "ALSA not found, microphone checker will be Skipped at runtime")
  endif()
endif()

# --- libdrm 探测 -------------------------------------------------------------
set(CHECKER_HAS_DRM 0)
if(WITH_DRM)
  find_path(DRM_INCLUDE_DIR NAMES xf86drm.h PATH_SUFFIXES libdrm)
  find_library(DRM_LIB NAMES drm)
  if(DRM_INCLUDE_DIR AND DRM_LIB)
    set(CHECKER_HAS_DRM 1)
    message(STATUS "libdrm: found")
  else()
    message(STATUS "libdrm not found, display checker falls back to sysfs probing")
  endif()
endif()

# --- 生成编译期配置头 --------------------------------------------------------
configure_file(
  ${CMAKE_SOURCE_DIR}/include/checker/generated/config.h.in
  ${CMAKE_BINARY_DIR}/generated/checker/config.h
  @ONLY)

# --- 主可执行文件 ------------------------------------------------------------
add_executable(bm1684-selftest
  src/main.cpp
  src/core/registry.cpp
  src/core/runner.cpp
  src/core/context.cpp
  src/checkers/disk_checker.cpp
  src/checkers/memory_checker.cpp
  src/checkers/display_checker.cpp
  src/checkers/fingerprint_checker.cpp
  src/checkers/microphone_checker.cpp
  src/checkers/light_checker.cpp
  src/checkers/network_checker.cpp
  src/checkers/tpu_checker.cpp
  src/checkers/model_inference_checker.cpp
  src/checkers/script_checker.cpp
  src/checkers/register_all.cpp
  src/hw/serial_port.cpp
  src/hw/gpio.cpp
  src/hw/drm_device.cpp
  src/hw/alsa_capture.cpp
  src/util/time_util.cpp
  src/util/subprocess.cpp
  src/log/logger.cpp
)

target_include_directories(bm1684-selftest PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(bm1684-selftest PRIVATE Threads::Threads checker_third_party)

# 条件链接可选系统库
if(CHECKER_HAS_BM_SDK)
  target_include_directories(bm1684-selftest PRIVATE ${BM_SDK_INCLUDE_DIR})
  target_link_libraries(bm1684-selftest PRIVATE ${BM_LIB_BMRT} ${BM_LIB_BMCV} ${BM_LIB_BMLIB})
  target_compile_definitions(bm1684-selftest PRIVATE CHECKER_HAS_BM_SDK=1)
endif()
if(CHECKER_HAS_ALSA)
  target_include_directories(bm1684-selftest PRIVATE ${ALSA_INCLUDE_DIR})
  target_link_libraries(bm1684-selftest PRIVATE ${ALSA_LIB})
  target_compile_definitions(bm1684-selftest PRIVATE CHECKER_HAS_ALSA=1)
endif()
if(CHECKER_HAS_DRM)
  target_include_directories(bm1684-selftest PRIVATE ${DRM_INCLUDE_DIR})
  target_link_libraries(bm1684-selftest PRIVATE ${DRM_LIB})
  target_compile_definitions(bm1684-selftest PRIVATE CHECKER_HAS_DRM=1)
endif()

# 安装
install(TARGETS bm1684-selftest DESTINATION /usr/local/bin)
install(FILES config/selftest.json DESTINATION /etc/bm1684-selftest)
install(PROGRAMS scripts/install.sh DESTINATION /usr/local/share/bm1684-selftest)
install(FILES scripts/bm1684-selftest.service DESTINATION /usr/local/share/bm1684-selftest)

# --- 可选单元测试 (仅 core/util 逻辑，不依赖硬件) ---------------------------
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
  enable_testing()
  add_executable(test_json tests/test_json.cpp src/core/context.cpp src/log/logger.cpp)
  target_include_directories(test_json PRIVATE
    ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
  target_link_libraries(test_json PRIVATE checker_third_party Threads::Threads)
  target_compile_options(test_json PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME json_test COMMAND test_json)
endif()
