#
# Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
#

# 定义 framework lib 所有包含 .so 的子目录根路径（用于 RPATH 和 -rpath-link）
# 这些子目录是 libqifeng.so DT_NEEDED 传递依赖所在位置
set(FW_LIB_ROOT "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib")
set(FW_LIB_SUBDIRS
    "aarand" "casbin" "drogon" "eigen" "grpc" "grpc_plugin" "gtest"
    "hdbscan" "houmo/tcim" "irlba" "jsoncpp" "kaldi-native-fbank" "knncolle"
    "libwebsockets" "minidocx" "minizip" "minja" "nlohmann" "numcpp"
    "onnx" "pugixml" "qifeng_framework" "re2" "soci" "sophon" "spdlog"
    "subpar" "tokenizers_c_binding" "umappp" "utfcpp" "workflow" "xlnt" "yamlcpp"
)

# 构建 $ORIGIN 相对路径 RPATH
# 将系统 Sophon 库路径放在首位，确保运行时优先加载系统安装的版本（与当前驱动匹配）
set(RPATH_ORIGIN "$ORIGIN;$ORIGIN/lib;/opt/sophon/libsophon-current/lib;$ORIGIN/../lib;$ORIGIN/../../deps/qifeng_api/lib")
foreach(D ${FW_LIB_SUBDIRS})
    set(RPATH_ORIGIN "${RPATH_ORIGIN};$ORIGIN/../../deps/qifeng_framework/lib/${D}")
endforeach()

# RPATH 使用全局变量（同 qf_scmd 已验证的方案），兼容 cmake 3.15+
# 构建时：包含 deps 路径 + 系统 Sophon 路径，方便本地开发调试
# 安装时：系统 Sophon 路径优先，deps 路径作为回退
# 注意：必须在 add_executable 之前设置，cmake 在创建目标时读取这些变量
set(CMAKE_BUILD_RPATH "${RPATH_ORIGIN}")
set(CMAKE_INSTALL_RPATH "${RPATH_ORIGIN}")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)

# 收集库源文件
file(GLOB_RECURSE SRC_FILES
    "*.cpp"
    "dao/*.cpp"
    "controller/ws/*.cpp"
    "common/utils/*.cpp"
)

# 创建可执行文件
add_executable(qifeng_ca ${SRC_FILES})

# 设置 RPATH 属性（必须在 add_executable 之后，install 之前）
# 构建时：使用 deps 路径 + 系统 Sophon 路径，方便本地开发调试
# 安装时：系统 Sophon 路径优先，deps 路径作为回退
set_target_properties(qifeng_ca PROPERTIES
    BUILD_RPATH "${RPATH_ORIGIN}"
    INSTALL_RPATH "${RPATH_ORIGIN}"
)

target_include_directories(qifeng_ca PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# 将依赖库的包含目录设置为系统目录（过滤警告）
target_include_directories(qifeng_ca SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/deps/qifeng_framework/include
    ${CMAKE_SOURCE_DIR}/deps/qifeng_api/include
)

# 使用 -rpath-link 替代 target_link_directories, 链接时解析 DT_NEEDED 但不写入产物 RPATH
# 系统 Sophon 库路径优先，确保链接时优先解析系统版本
target_link_options(qifeng_ca PRIVATE "SHELL:-Wl,-rpath-link,/opt/sophon/libsophon-current/lib")
foreach(D ${FW_LIB_SUBDIRS})
    target_link_options(qifeng_ca PRIVATE "SHELL:-Wl,-rpath-link,${FW_LIB_ROOT}/${D}")
endforeach()
target_link_options(qifeng_ca PRIVATE "SHELL:-Wl,-rpath-link,${CMAKE_SOURCE_DIR}/deps/qifeng_api/lib")

# 强制使用 RPATH 而非 RUNPATH（RPATH 优先级高于 ldconfig 缓存）
# --disable-new-dtags 告诉链接器生成 DT_RPATH 而非 DT_RUNPATH
target_link_options(qifeng_ca PRIVATE "SHELL:-Wl,--disable-new-dtags")

# 链接库
# libqifeng.so 的 DT_NEEDED 传递依赖以完整 .so 路径显式列出,
# 应对 CMake 对预编译 IMPORTED 目标传递依赖链接支持不足的问题
target_link_libraries(qifeng_ca PRIVATE
    qifeng_framework::qifeng_framework

    # -- 核心框架库 --
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/qifeng_framework/libqifeng.so"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/houmo/tcim/libtcim_dev_ctrl.so"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_api/lib/libproto_qifeng_ca.so"

    # -- libqifeng.so 传递依赖 (按 readelf -d DT_NEEDED 列出) --
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/yamlcpp/libyaml-cpp.so"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/soci/libsoci_core.so"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/drogon/libdrogon.so"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/drogon/libtrantor.so"
    #"${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/sophon/libbmlib.so"
    #"${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/sophon/libbmrt.so"
    #"${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/sophon/libbmcv.so"
    #"${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/sophon/libavcodec.so"
    #"${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/sophon/libavformat.so"
    #"${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/sophon/libavutil.so"
    #"${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/sophon/libswresample.so"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/grpc/libgrpc++.so"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/grpc/libgrpc.so"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/grpc/libgpr.so"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/grpc_plugin/libprotobuf.so"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/grpc_plugin/libabsl_status.so"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/qifeng_framework/libgpiod.so.2"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/qifeng_framework/libasound.so.2"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/kaldi-native-fbank/libkaldi-native-fbank-core.so"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/onnx/libonnxruntime.so"

    # -- 源码直接引用的框架静态库 (.a) --
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/casbin/casbin.a"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/minizip/libminizip.a"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/minidocx/libminidocx.a"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/jsoncpp/libjsoncpp.a"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/hdbscan/libhdbscan.a"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/pugixml/libpugixml.a"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/re2/libre2.a"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/numcpp/libnumcpp.a"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/workflow/libworkflow.a"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/libwebsockets/libwebsockets.a"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/tokenizers_c_binding/libtokenizers_c.a"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/gtest/libgtest.a"

    # -- 源码直接引用的框架动态库 (.so) --
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/jsoncpp/libjsoncpp.so"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/spdlog/libspdlog.so"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/xlnt/libxlnt.so"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/re2/libre2.so"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/numcpp/libnumcpp.so"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/workflow/libworkflow.so"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/libwebsockets/libwebsockets.so"
    "${CMAKE_SOURCE_DIR}/deps/qifeng_framework/lib/tokenizers_c_binding/libtokenizers_c.so"

    # -- 系统库 --
    uuid
    pthread
    ssl
    crypto
    dl
    m
    z
)
