cmake_minimum_required(VERSION 3.16)
project(utils
    VERSION 1.0.0
    DESCRIPTION "C++17 utility libraries: audio processing + docx template"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ── 全局选项 ──
option(UTILS_BUILD_TESTS "Build test suites" ON)
option(UTILS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# 公共编译选项:位置无关代码
# 静态库源码也用 -fPIC 编译,使其既能链接到可执行文件也能链接到动态库
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fPIC>)

enable_testing()

# 子模块
add_subdirectory(third_party)
add_subdirectory(common)
add_subdirectory(audio)
add_subdirectory(docx)
