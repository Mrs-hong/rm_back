# =============================================================================
# qifeng_checker - 同时构建静态库与动态库
# BM1684 边缘设备开机自检核心库，源文件位于 src/checker/，头文件位于 include/checker/。
# 依赖: qifeng_framework (日志/JSON等基础设施)
# 可选: Sophon SDK (TPU/模型推理), ALSA (麦克风), libdrm (显示器), libgpiod (GPIO)
# =============================================================================

# --- 源文件 -------------------------------------------------------------------
set(CHECKER_SOURCES
  core/context.cpp
  core/registry.cpp
  core/runner.cpp
  checkers/disk_checker.cpp
  checkers/memory_checker.cpp
  checkers/display_checker.cpp
  checkers/fingerprint_checker.cpp
  checkers/microphone_checker.cpp
  checkers/light_checker.cpp
  checkers/network_checker.cpp
  checkers/tpu_checker.cpp
  checkers/model_inference_checker.cpp
  checkers/fan_checker.cpp
  checkers/register_all.cpp
  checkers/by_script/pcba_cheker.cpp
  hw/serial_port.cpp
  hw/gpio.cpp
  hw/drm_device.cpp
  hw/alsa_capture.cpp
  ${CMAKE_SOURCE_DIR}/src/common/cmd_execute.cpp
)

# --- 静态库 -------------------------------------------------------------------
add_library(qifeng_checker STATIC ${CHECKER_SOURCES})

target_include_directories(qifeng_checker PUBLIC
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(qifeng_checker PUBLIC
  qifeng_framework::qifeng_framework
  Threads::Threads
)

# --- 动态库 -------------------------------------------------------------------
add_library(qifeng_checker_shared SHARED ${CHECKER_SOURCES})

target_include_directories(qifeng_checker_shared PUBLIC
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(qifeng_checker_shared PUBLIC
  qifeng_framework::qifeng_framework
  Threads::Threads
)

# 动态库输出名与静态库一致（libqifeng_checker.so / libqifeng_checker.a）
set_target_properties(qifeng_checker_shared PROPERTIES OUTPUT_NAME "qifeng_checker")
# 动态库版本号
set_target_properties(qifeng_checker_shared PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

# --- 条件编译定义与链接（可选系统库，同时应用于静态库和动态库）-----------------
macro(checker_add_optional_dep target)
  if(CHECKER_HAS_BM_SDK)
    target_include_directories(${target} PRIVATE ${BM_SDK_INCLUDE_DIR})
    target_link_libraries(${target} PRIVATE ${BM_LIB_BMRT} ${BM_LIB_BMCV} ${BM_LIB_BMLIB})
    target_compile_definitions(${target} PRIVATE CHECKER_HAS_BM_SDK=1)
  endif()
  if(CHECKER_HAS_ALSA)
    target_include_directories(${target} PRIVATE ${ALSA_INCLUDE_DIR})
    target_link_libraries(${target} PRIVATE ${ALSA_LIB})
    target_compile_definitions(${target} PRIVATE CHECKER_HAS_ALSA=1)
  endif()
  if(CHECKER_HAS_DRM)
    target_include_directories(${target} PRIVATE ${DRM_INCLUDE_DIR})
    target_link_libraries(${target} PRIVATE ${DRM_LIB})
    target_compile_definitions(${target} PRIVATE CHECKER_HAS_DRM=1)
  endif()
  if(CHECKER_HAS_GPIOD)
    target_include_directories(${target} PRIVATE ${GPIOD_INCLUDE_DIR})
    target_link_libraries(${target} PRIVATE ${GPIOD_LIB})
    target_compile_definitions(${target} PRIVATE CHECKER_HAS_GPIOD=1)
  endif()
endmacro()

checker_add_optional_dep(qifeng_checker)
checker_add_optional_dep(qifeng_checker_shared)
