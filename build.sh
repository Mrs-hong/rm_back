#!/bin/bash

# ============================================================================
# qifeng-scm 编译脚本
# 用法:
#   ./build              - 编译主项目 (Release)
#   ./build -d           - 编译主项目 (Debug)
#   ./build -r           - 编译主项目 (Release)
#   ./build all -d       - 编译所有目标 (Debug)
#   ./build all -r       - 编译所有目标 (Release)
#   ./build test -d      - 编译测试目标 (Debug)
#   ./build test -r      - 编译测试目标 (Release)
#   ./build clean        - 清除 build 目录
#   ./build -r -i /opt/qifeng  - 编译并安装到指定目录
#   ./build pack         - 打包部署产物到 dist/ 目录
#
# checker 可选库参数（默认全部 ON，WSL2 交叉编译时可关闭）:
#   --with-all=ON|OFF      全部开启或关闭（快捷方式，可被单项覆盖）
#   --with-bm-sdk=ON|OFF   Sophon SDK (TPU/模型推理)
#   --with-alsa=ON|OFF     ALSA (麦克风)
#   --with-drm=ON|OFF      libdrm (显示器)
#   --with-gpiod=ON|OFF    libgpiod (GPIO 指示灯)
# ============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
JOBS=5

# 解析全局参数
INSTALL_PREFIX=""
PARSED_ARGS=()
WITH_BM_SDK=""
WITH_ALSA=""
WITH_DRM=""
WITH_GPIOD=""

i=1
while [ $i -le $# ]; do
    arg="${!i}"
    case "$arg" in
        -i)
            i=$((i + 1))
            if [ $i -le $# ]; then
                INSTALL_PREFIX="${!i}"
            else
                echo "错误: -i 参数需要指定安装目录"
                exit 1
            fi
            ;;
        --with-all=*)
            all_val="${arg#--with-all=}"
            WITH_BM_SDK="${all_val}"
            WITH_ALSA="${all_val}"
            WITH_DRM="${all_val}"
            WITH_GPIOD="${all_val}"
            ;;
        --with-bm-sdk=*)
            WITH_BM_SDK="${arg#--with-bm-sdk=}"
            ;;
        --with-alsa=*)
            WITH_ALSA="${arg#--with-alsa=}"
            ;;
        --with-drm=*)
            WITH_DRM="${arg#--with-drm=}"
            ;;
        --with-gpiod=*)
            WITH_GPIOD="${arg#--with-gpiod=}"
            ;;
        *)
            PARSED_ARGS+=("$arg")
            ;;
    esac
    i=$((i + 1))
done

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
    local cmake_args=(
        -DCMAKE_BUILD_TYPE="${build_type}"
        -DBUILD_TESTING="${testing}"
    )

    # checker 可选库参数（仅显式指定时才传入，否则使用 CMake 默认值）
    [ -n "${WITH_BM_SDK}" ]  && cmake_args+=(-DWITH_BM1684_SDK="${WITH_BM_SDK}")
    [ -n "${WITH_ALSA}" ]    && cmake_args+=(-DWITH_ALSA="${WITH_ALSA}")
    [ -n "${WITH_DRM}" ]     && cmake_args+=(-DWITH_DRM="${WITH_DRM}")
    [ -n "${WITH_GPIOD}" ]   && cmake_args+=(-DWITH_GPIOD="${WITH_GPIOD}")

    cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" "${cmake_args[@]}"
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

# 执行安装
run_install() {
    if [ -n "${INSTALL_PREFIX}" ]; then
        echo "=== 安装到 ${INSTALL_PREFIX} ==="
        cmake --install "${BUILD_DIR}" --prefix "${INSTALL_PREFIX}"
    fi
}

# 打包部署产物
run_pack() {
    local dist_dir="${SCRIPT_DIR}/dist"

    echo "=== 打包部署产物 ==="

    # 检查构建产物是否存在
    if [ ! -f "${BUILD_DIR}/bin/qf_scmd" ]; then
        echo "错误: 未找到构建产物，请先执行编译"
        exit 1
    fi

    # 清理旧的打包目录
    rm -rf "${dist_dir}"

    # --- bin/ ---
    echo "  收集 bin/"
    mkdir -p "${dist_dir}/bin"
    cp -p "${BUILD_DIR}/bin/qf_scmd"  "${dist_dir}/bin/"
    cp -p "${BUILD_DIR}/bin/qf_scmc"  "${dist_dir}/bin/"

    # --- lib/ ---
    echo "  收集 lib/"
    mkdir -p "${dist_dir}/lib"
    # checker 库
    if [ -d "${BUILD_DIR}/src/checker" ]; then
        cp -p "${BUILD_DIR}/src/checker"/libqifeng_checker.so* "${dist_dir}/lib/" 2>/dev/null || true
        cp -p "${BUILD_DIR}/src/checker"/libqifeng_checker.a   "${dist_dir}/lib/" 2>/dev/null || true
    fi
    # 第三方库（qifeng_framework 等，平铺安装到 lib/）
    # libsophon 系列库：ARM 架构使用 qifeng_framework 自带的版本；x86 使用运行系统安装的版本
    local arch
    arch=$(uname -m)
    local include_bundled_sophon=false
    if [ "${arch}" = "aarch64" ] || [ "${arch}" = "arm64" ]; then
        include_bundled_sophon=true
    fi

    local tp_dir="${SCRIPT_DIR}/third_part"
    local sophon_libs=(
        libbmlib.so
        libbmrt.so
        libbmcv.so
        libbmcv_cpu_func.so
        libbmvpuapi.so
        libbmvpulite.so
        libbmvideo.so
        libbmjpuapi.so
        libbmjpulite.so
        libbmion.so
        libbmvppapi.so
    )
    if [ -d "${tp_dir}" ]; then
        for lib in $(find "${tp_dir}" \( -path "*/install/lib/*.so*" -o -path "*/install/lib/*.a" \) 2>/dev/null); do
            local basename_lib
            basename_lib=$(basename "${lib}")
            # 如果 basename 以任意 libsophon 库名开头，则默认跳过
            local skip=false
            for prefix in "${sophon_libs[@]}"; do
                if [[ "${basename_lib}" == "${prefix}"* ]]; then
                    skip=true
                    break
                fi
            done
            if [ "${skip}" = true ] && [ "${include_bundled_sophon}" != true ]; then
                echo "  跳过 libsophon 库: ${basename_lib}"
                continue
            fi
            if [ "${skip}" = true ]; then
                echo "  包含 qifeng_framework 携带的 libsophon 库: ${basename_lib}"
            fi
            cp -p "${lib}" "${dist_dir}/lib/"
        done
    fi

    # --- .config/ ---
    echo "  收集 .config/"
    mkdir -p "${dist_dir}/.config"
    cp -p "${SCRIPT_DIR}/.config/scmd.yaml"     "${dist_dir}/.config/"
    cp -p "${SCRIPT_DIR}/.config/selftest.json"  "${dist_dir}/.config/"

    # --- scripts/ (自检脚本) ---
    echo "  收集 scripts/"
    mkdir -p "${dist_dir}/scripts"
    if [ -f "${SCRIPT_DIR}/checker_script/dist/self-check" ]; then
        cp -p "${SCRIPT_DIR}/checker_script/dist/self-check"  "${dist_dir}/scripts/"
    fi
    if [ -f "${SCRIPT_DIR}/checker_script/dist/config.ini" ]; then
        cp -p "${SCRIPT_DIR}/checker_script/dist/config.ini"  "${dist_dir}/scripts/"
    fi

    # --- model/ (推理模型) ---
    echo "  收集 model/"
    mkdir -p "${dist_dir}/model"
    if [ -f "${SCRIPT_DIR}/model/fsmn_fp32_.bmodel" ]; then
        cp -p "${SCRIPT_DIR}/model/fsmn_fp32_.bmodel" "${dist_dir}/model/"
    fi

    # --- systemd/ ---
    echo "  收集 systemd/"
    mkdir -p "${dist_dir}/systemd"
    if [ -f "${SCRIPT_DIR}/debian/qifeng-scmd.service" ]; then
        cp -p "${SCRIPT_DIR}/debian/qifeng-scmd.service" "${dist_dir}/systemd/"
    fi

    # --- ld.so.conf.d/ ---
    echo "  收集 ld.so.conf.d/"
    mkdir -p "${dist_dir}/ld.so.conf.d"
    if [ -f "${SCRIPT_DIR}/debian/ld.so.conf.d/qifeng-scm.conf" ]; then
        cp -p "${SCRIPT_DIR}/debian/ld.so.conf.d/qifeng-scm.conf" "${dist_dir}/ld.so.conf.d/"
    fi

    # --- debian/ (deb 包维护脚本) ---
    echo "  收集 debian/"
    mkdir -p "${dist_dir}/debian"
    for f in postinst prerm postrm; do
        if [ -f "${SCRIPT_DIR}/debian/${f}" ]; then
            cp -p "${SCRIPT_DIR}/debian/${f}" "${dist_dir}/debian/"
        fi
    done

    # --- 部署脚本 ---
    echo "  收集 dist_depoly.sh"
    if [ -f "${SCRIPT_DIR}/dist_depoly.sh" ]; then
        cp -p "${SCRIPT_DIR}/dist_depoly.sh" "${dist_dir}/"
        chmod +x "${dist_dir}/dist_depoly.sh"
    fi

    # 输出汇总
    echo ""
    echo "=== 打包完成 ==="
    echo "  目录: ${dist_dir}/"
    echo ""
    echo "  目录结构:"
    find "${dist_dir}" -type f | sort | sed "s|${dist_dir}/|    |"
    echo ""
    echo "  对应系统路径:"
    echo "    bin/              → /usr/bin/"
    echo "    lib/              → /usr/lib/qifeng-scm/"
    echo "    .config/          → /etc/qifeng-scm/"
    echo "    scripts/          → /usr/lib/qifeng-scm/"
    echo "    model/            → /opt/sophon/selftest/"
    echo "    systemd/          → /lib/systemd/system/"
    echo "    ld.so.conf.d/     → /etc/ld.so.conf.d/"
    echo "    debian/           → (deb 包维护脚本)"
}

# 主逻辑
set -- "${PARSED_ARGS[@]}"

case "$1" in
    clean|-clean)
        if [ -d "${BUILD_DIR}" ]; then
            rm -rf "${BUILD_DIR}"
            echo "已完全清除 build 目录"
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
        run_install
        ;;
    test)
        BUILD_TYPE=$(parse_build_type "$2")
        echo "=== 编译测试目标 | ${BUILD_TYPE} | -j${JOBS} ==="
        run_cmake "${BUILD_TYPE}" ON
        run_make test_config
        run_install
        ;;
    -d|-r|"")
        BUILD_TYPE=$(parse_build_type "$1")
        echo "=== 编译主项目 | ${BUILD_TYPE} | -j${JOBS} ==="
        run_cmake "${BUILD_TYPE}" OFF
        run_make
        run_install
        ;;
    pack)
        run_pack
        ;;
    *)
        echo "用法: $0 [all|test|clean|pack] [-d|-r] [-i <install_prefix>] [--with-*=ON|OFF]"
        echo "  $0                          编译主项目 (Release)"
        echo "  $0 -d                       编译主项目 (Debug)"
        echo "  $0 -r                       编译主项目 (Release)"
        echo "  $0 all -d                   编译所有目标 (Debug)"
        echo "  $0 all -r                   编译所有目标 (Release)"
        echo "  $0 test -d                  编译测试目标 (Debug)"
        echo "  $0 test -r                  编译测试目标 (Release)"
        echo "  $0 -r -i /opt/qifeng        编译并安装到指定目录"
        echo "  $0 clean                    清除 build 目录"
        echo "  $0 pack                     打包部署产物到 dist/ 目录"
        echo ""
        echo "checker 可选库参数:"
        echo "  --with-all=ON|OFF           全部开启或关闭（快捷方式，可被单项覆盖）"
        echo "  --with-bm-sdk=ON|OFF        Sophon SDK (TPU/模型推理, 默认ON)"
        echo "  --with-alsa=ON|OFF          ALSA (麦克风, 默认ON)"
        echo "  --with-drm=ON|OFF           libdrm (显示器, 默认ON)"
        echo "  --with-gpiod=ON|OFF         libgpiod (GPIO指示灯, 默认ON)"
        echo ""
        echo "示例:"
        echo "  $0 -r --with-all=OFF                    WSL2交叉编译(关闭全部可选库)"
        echo "  $0 -r --with-all=OFF --with-gpiod=ON    关闭全部但仅启用GPIO"
        exit 1
        ;;
esac
