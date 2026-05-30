#!/bin/bash

# ============================================================================
# qifeng-scm 编译脚本
# 用法:
#   ./build          - 编译主项目 (Release)
#   ./build -d       - 编译主项目 (Debug)
#   ./build -r       - 编译主项目 (Release)
#   ./build all -d   - 编译所有目标 (Debug)
#   ./build all -r   - 编译所有目标 (Release)
#   ./build test -d  - 编译测试目标 (Debug)
#   ./build test -r  - 编译测试目标 (Release)
#   ./build clean    - 清除 build 目录
# ============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
JOBS=5

# 解析构建类型: -d=Debug, -r=Release
parse_build_type() {
    case "$1" in
        -d) echo "Debug" ;;
        -r) echo "Release" ;;
        *)  echo "Release" ;;
    esac
}

# 执行 cmake 配置
run_cmake() {
    local build_type="$1"
    local testing="$2"

    mkdir -p "${BUILD_DIR}"
    cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE="${build_type}" \
        -DBUILD_TESTING="${testing}"
}

# 执行 make 编译
run_make() {
    local target="$1"
    if [ -z "${target}" ]; then
        cmake --build "${BUILD_DIR}" -j${JOBS}
    else
        cmake --build "${BUILD_DIR}" --target "${target}" -j${JOBS}
    fi
}

# 主逻辑
case "$1" in
    clean)
        if [ -d "${BUILD_DIR}" ]; then
            # 第一步: 清除编译产物 (.o, 可执行文件等)
            cmake --build "${BUILD_DIR}" --target clean 2>/dev/null
            # 第二步: 清除 CMake 配置文件和生成的目录结构
            rm -f "${BUILD_DIR}/CMakeCache.txt"
            rm -f "${BUILD_DIR}/compile_commands.json"
            rm -f "${BUILD_DIR}/Makefile"
            rm -f "${BUILD_DIR}/cmake_install.cmake"
            rm -f "${BUILD_DIR}/CPackConfig.cmake"
            rm -f "${BUILD_DIR}/CPackSourceConfig.cmake"
            rm -rf "${BUILD_DIR}/CMakeFiles"
            rm -rf "${BUILD_DIR}/.cmake"
            rm -rf "${BUILD_DIR}/src"
            rm -rf "${BUILD_DIR}/test"
            rm -rf "${BUILD_DIR}/bin"
            rm -rf "${BUILD_DIR}/lib"
            echo "已清除编译产物和 CMake 配置文件"
        else
            echo "build 目录不存在，无需清除"
        fi
        exit 0
        ;;
    all)
        BUILD_TYPE=$(parse_build_type "$2")
        echo "=== 编译所有目标 | ${BUILD_TYPE} | -j${JOBS} ==="
        run_cmake "${BUILD_TYPE}" ON
        run_make
        ;;
    test)
        BUILD_TYPE=$(parse_build_type "$2")
        echo "=== 编译测试目标 | ${BUILD_TYPE} | -j${JOBS} ==="
        run_cmake "${BUILD_TYPE}" ON
        run_make test_config
        ;;
    -d|-r|"")
        BUILD_TYPE=$(parse_build_type "$1")
        echo "=== 编译主项目 | ${BUILD_TYPE} | -j${JOBS} ==="
        run_cmake "${BUILD_TYPE}" OFF
        run_make
        ;;
    *)
        echo "用法: $0 [all|test|clean] [-d|-r]"
        echo "  $0            编译主项目 (Release)"
        echo "  $0 -d         编译主项目 (Debug)"
        echo "  $0 -r         编译主项目 (Release)"
        echo "  $0 all -d     编译所有目标 (Debug)"
        echo "  $0 all -r     编译所有目标 (Release)"
        echo "  $0 test -d    编译测试目标 (Debug)"
        echo "  $0 test -r    编译测试目标 (Release)"
        echo "  $0 clean      清除 build 目录"
        exit 1
        ;;
esac
