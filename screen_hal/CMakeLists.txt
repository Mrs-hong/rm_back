#
# screen_hal 独立构建工程
#
# 从 qifeng_framework 抽离的迪文 T5L DGUSII 串口屏 HAL 模块，可脱离原框架单独编译。
# 目录结构：
#   include/                   公共头文件（hal/screen、hal/bounded_queue.h、common 日志）
#   src/                       实现源文件（hal/screen、common/logger）
#   test/screen_test.cpp       屏幕功能交互测试程序
#
# 依赖：系统 spdlog（Ubuntu 的 spdlog 使用外部 fmt，需一并链接 fmt）、pthread
#   sudo apt install libspdlog-dev libfmt-dev
#
cmake_minimum_required(VERSION 3.15)
project(screen_hal LANGUAGES CXX)

# ── 编译标准与类型 ──
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build" FORCE)
endif()

# 源文件含中文注释与字符串字面量，统一按 UTF-8 处理（与原框架保持一致）
add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)

# ── 日志依赖：系统 spdlog（SPDLOG_FMT_EXTERNAL=1，需同时链接 fmt）──
find_package(PkgConfig REQUIRED)
pkg_check_modules(SPDLOG REQUIRED IMPORTED_TARGET spdlog)
pkg_check_modules(FMT REQUIRED IMPORTED_TARGET fmt)

# ── screen HAL 静态库 ──
# src 下仅含 hal/screen 与 common/logger 两部分，全部纳入即可
file(GLOB_RECURSE SCREEN_HAL_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(screen_hal STATIC ${SCREEN_HAL_SRC})

target_include_directories(screen_hal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(screen_hal PUBLIC
    PkgConfig::SPDLOG
    PkgConfig::FMT
    pthread
)

# ── 测试程序（默认构建，可用 -DBUILD_TESTING=OFF 关闭）──
option(BUILD_TESTING "Build screen test program" ON)
if(BUILD_TESTING)
    add_executable(screen_test test/screen_test.cpp)
    target_link_libraries(screen_test PRIVATE screen_hal)

    set_target_properties(screen_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
endif()
