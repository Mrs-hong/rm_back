#
# Copyright (C) 2025-2025 Qifeng Shunshi Co., Ltd. All rights reserved.
#

# 收集库源文件（排除main.cpp）
file(GLOB_RECURSE LIB_SRC_FILES
    "common/*.cpp"
    "http/*.cpp"
    "rpc/*.cpp"
    "websocket/*.cpp"
    "dao/*.cpp"
    "redis/*.cpp"
    "openai/*.cpp"
    "middleware/*.cpp"
    "curl/*.cpp"
    "dag/*.cpp"
    "hal/*.cpp"
)

# 若是aarch64 加入models下的所有源文件 (TCIM 版本: 使用 models_hm/ 替代 models/)
if(COMPILER_AARCH64)
    file(GLOB_RECURSE MODEL_SRC_FILES 
    "models_hm/worker/*.cpp"
    "models_hm/manager/*.cpp"
    "aas/*.cpp"
    "lms/*.cpp"
    "lms_hm/*.cpp"
    "asr/*.cpp"
    )
    list(APPEND LIB_SRC_FILES ${MODEL_SRC_FILES})
endif()

# 创建共享库
add_library(qifeng_framework SHARED ${LIB_SRC_FILES})

# 设置库的版本信息
set_target_properties(qifeng_framework PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME "qifeng"
)

# 设置位置无关代码
# RPATH: 所有 .so 统一放在 lib/ 目录, 运行时从 $ORIGIN 查找
# tcim 库在 lib/houmo/tcim/ 子目录, 需额外加入 $ORIGIN/../houmo/tcim 供运行时解析
set_target_properties(qifeng_framework PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    BUILD_RPATH "$ORIGIN;$ORIGIN/../houmo/tcim"
    INSTALL_RPATH "$ORIGIN;$ORIGIN/../houmo/tcim"
    INSTALL_RPATH_USE_LINK_PATH FALSE   # 禁止将链接时的绝对路径写入 RPATH
    INTERFACE_LINK_LIBRARIES "" # 防止导出时携带 grpc / absl 等依赖
)

set_target_properties(qifeng_framework PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/lib"
)

# 设置库的包含目录
# 注意: include_directories(SYSTEM ...) 仅对其后定义的目标生效，而 qifeng_framework
# 在下方 include_directories 之前创建，故须在此显式补上 third_part/include 根目录，
# 否则 include/ 平铺头文件（spdlog、yaml-cpp 等）无法解析。
target_include_directories(qifeng_framework PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${THIRD_PARTY_DIR}/include>
    $<INSTALL_INTERFACE:include/qifeng_framework>
)

target_include_directories(qifeng_framework PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
# target_include_directories(qifeng_framework PUBLIC
#     $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
#     $<INSTALL_INTERFACE:include>
# )

# 将第三方库的包含目录设置为系统目录，以过滤掉它们的告警
# 1.0.7 结构: include/ 下既有子目录 (grpcpp/, drogon/ 等), 又有平铺的 .h 文件 (ares.h, zlib.h 等)
# file(GLOB ... /*/) 无法可靠区分文件与目录, 改用 IS_DIRECTORY 显式过滤, 避免 .h 文件被误加入 -I
# 1.0.6 及之前: include/ 下仅子目录, GLOB /*/ 可工作; 1.0.7 平铺 .h 后该写法失效
file(GLOB _tp_include_candidates "${CMAKE_SOURCE_DIR}/third_part/include/*")
set(THIRD_PARTY_INCLUDE_SUBDIRS "")
foreach(_dir ${_tp_include_candidates})
    if(IS_DIRECTORY "${_dir}")
        list(APPEND THIRD_PARTY_INCLUDE_SUBDIRS "${_dir}")
    endif()
endforeach()
include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/third_part/include ${THIRD_PARTY_INCLUDE_SUBDIRS})
unset(_tp_include_candidates)
unset(_dir)
# 若是aarch64 加入sophon、onnx、numcpp库;同上
if(COMPILER_AARCH64)
    # 扁平化结构: ARM 特有库的头文件也在 third_part/include/<libname>/ 下, 已被上方 GLOB 包含
    message(STATUS "qifeng_framework: ARM64 architecture, third_part headers in subdirs")
endif()

# 确保spdlog使用bundled的fmt(PUBLIC 确保所有使用该库都用同一套fmt)
target_compile_definitions(qifeng_framework PUBLIC
    FMT_HEADER_ONLY=1
)

# 三方库依赖配置(子目录结构: 库在 third_part/lib/<name>/, 头文件在 third_part/include/<name>/)
# 1.0.5 策略: 优先使用静态库(.a), 回退到动态库(.so); 头文件按库分子目录
function(add_third_party name libname)
    set(lib_root "${CMAKE_SOURCE_DIR}/third_part/lib")
    set(include_root "${CMAKE_SOURCE_DIR}/third_part/include")

    # 子目录名映射: 默认与 libname 一致, 少数不一致在此显式指定
    if(name STREQUAL "openssl")
        set(subdir "grpc")  # libssl.so / libcrypto.so 在 grpc 子目录
    elseif(name STREQUAL "absl")
        set(subdir "grpc")  # 1.0.6: libabsl_*.so 统一放在 grpc 子目录 (gRPC 1.80 依赖)
    elseif(name STREQUAL "sophon")
        set(subdir "sophon")
    elseif(name STREQUAL "tokenizers_c_binding")
        set(subdir "tokenizers_c_binding")  # 目录名与 name 一致, 非 libname
    elseif(name STREQUAL "yamlcpp")
        set(subdir "yamlcpp")  # 目录名 yamlcpp, libname yaml-cpp
    elseif(name STREQUAL "libwebsockets")
        set(subdir "libwebsockets")  # 目录名 libwebsockets, libname websockets
    elseif(name STREQUAL "kaldi-native-fbank")
        set(subdir "kaldi-native-fbank")  # 目录名 kaldi-native-fbank, libname kaldi-native-fbank-core
    elseif(name STREQUAL "tcim")
        set(subdir "houmo/tcim")  # tcim 库位于 lib/houmo/tcim/
    else()
        set(subdir "${libname}")
    endif()

    set(lib_dir "${lib_root}/${subdir}")
    # 头文件按库分子目录: include/<subdir>/
    set(include_path "${include_root}/${subdir}")

    # soci: 多个子库, 动态库 libsoci_<sub>.so
    if(name STREQUAL "soci")
        set(soci_libs "core" "empty" "mysql" "sqlite3")
        foreach(lib ${soci_libs})
            set(soci_lib_path "${lib_dir}/libsoci_${lib}.so")
            add_library(${name}::${lib} SHARED IMPORTED)
            set_target_properties(${name}::${lib} PROPERTIES
                IMPORTED_LOCATION "${soci_lib_path}"
                INTERFACE_INCLUDE_DIRECTORIES "${include_path}"
            )
        endforeach()
    elseif(name STREQUAL "drogon")
        # drogon + trantor 都在 drogon 子目录, 动态库
        foreach(lib drogon trantor)
            set(drogon_lib_path "${lib_dir}/lib${lib}.so")
            add_library(${name}::${lib} SHARED IMPORTED)
            set_target_properties(${name}::${lib} PROPERTIES
                IMPORTED_LOCATION "${drogon_lib_path}"
                INTERFACE_INCLUDE_DIRECTORIES "${include_path}"
            )
        endforeach()
    elseif(name STREQUAL "sophon")
        # 批量添加 sophon 子目录下所有共享库
        file(GLOB sophon_shared_libs "${lib_dir}/lib*.so")
        foreach(lib_file ${sophon_shared_libs})
            get_filename_component(lib_name "${lib_file}" NAME_WE)
            string(REGEX REPLACE "^lib" "" lib_name "${lib_name}")
            add_library(${name}::${lib_name} SHARED IMPORTED)
            set_target_properties(${name}::${lib_name} PROPERTIES
                IMPORTED_LOCATION "${lib_file}"
                INTERFACE_INCLUDE_DIRECTORIES "${include_path}"
            )
        endforeach()
        # bmodel: 静态库 libbmodel.a (1.0.5 已恢复)
        set(bmodel_lib_path "${lib_dir}/libbmodel.a")
        if(EXISTS "${bmodel_lib_path}")
            add_library(sophon::bmodel STATIC IMPORTED)
            set_target_properties(sophon::bmodel PROPERTIES
                IMPORTED_LOCATION "${bmodel_lib_path}"
                INTERFACE_INCLUDE_DIRECTORIES "${include_path}"
            )
        else()
            message(WARNING "sophon::bmodel (libbmodel.a) not found at ${bmodel_lib_path}")
        endif()
    elseif(name STREQUAL "tcim")
        file(GLOB tcim_shared_libs "${lib_dir}/lib*.so")
        foreach(lib_file ${tcim_shared_libs})
            get_filename_component(lib_name "${lib_file}" NAME_WE)
            string(REGEX REPLACE "^lib" "" lib_name "${lib_name}")
            add_library(houmo::tcim::${lib_name} SHARED IMPORTED)
            set_target_properties(houmo::tcim::${lib_name} PROPERTIES
                IMPORTED_LOCATION "${lib_file}"
                INTERFACE_INCLUDE_DIRECTORIES "${include_root}"
            )
        endforeach()
    elseif(name STREQUAL "absl")
        # absl: 批量创建 grpc 子目录下所有 libabsl_*.so IMPORTED 目标, 聚合为 absl::all
        file(GLOB absl_shared_libs "${lib_dir}/libabsl_*.so")
        set(absl_targets "")
        foreach(lib_file ${absl_shared_libs})
            get_filename_component(lib_name "${lib_file}" NAME_WE)
            string(REGEX REPLACE "^lib" "" lib_name "${lib_name}")
            add_library(absl::${lib_name} SHARED IMPORTED)
            set_target_properties(absl::${lib_name} PROPERTIES
                IMPORTED_LOCATION "${lib_file}"
                INTERFACE_INCLUDE_DIRECTORIES "${include_path}"
            )
            list(APPEND absl_targets "absl::${lib_name}")
        endforeach()
        add_library(absl::all INTERFACE IMPORTED)
        target_link_libraries(absl::all INTERFACE ${absl_targets})
    elseif(name STREQUAL "onnx")
        # onnx: 实际文件名为 libonnxruntime.so (非 libonnx.so)
        set(lib_file "${lib_dir}/libonnxruntime.so")
        if(NOT EXISTS "${lib_file}")
            message(WARNING "${name}::${libname} library not found at ${lib_file}")
        endif()
        add_library(${name}::${libname} SHARED IMPORTED)
        set_target_properties(${name}::${libname} PROPERTIES
            IMPORTED_LOCATION "${lib_file}"
            INTERFACE_INCLUDE_DIRECTORIES "${include_path}"
        )
    else()
        # 通用: 优先静态库 lib<libname>.a, 回退动态库 lib<libname>.so
        # casbin 特殊: 文件名为 casbin.a 而非 libcasbin.a
        if(name STREQUAL "casbin")
            set(static_lib "${lib_dir}/casbin.a")
        else()
            set(static_lib "${lib_dir}/lib${libname}.a")
        endif()
        set(shared_lib "${lib_dir}/lib${libname}.so")

        if(EXISTS "${static_lib}")
            add_library(${name}::${libname} STATIC IMPORTED)
            set_target_properties(${name}::${libname} PROPERTIES
                IMPORTED_LOCATION "${static_lib}"
                INTERFACE_INCLUDE_DIRECTORIES "${include_path}"
            )
        elseif(EXISTS "${shared_lib}")
            add_library(${name}::${libname} SHARED IMPORTED)
            set_target_properties(${name}::${libname} PROPERTIES
                IMPORTED_LOCATION "${shared_lib}"
                INTERFACE_INCLUDE_DIRECTORIES "${include_path}"
            )
        else()
            message(WARNING "${name}::${libname} library not found at ${lib_dir}/ (checked .a and .so)")
        endif()
    endif()
endfunction()

# 注意：添加三方库除了下面的链接以外还要添加两个处：
# 1. 项目根目录 CMakeLists.txt 的 THIRD_PARTY_LIST 添加install安装库、so
# 2. cmake/qifeng_framework-config.cmake.in 中使用 add_imported_qifeng_dep 添加外部可依赖的库

# 1.0.5: 恢复全部静态库链接 (1.0.5 包已保留所有 .a 静态库)
add_third_party(spdlog spdlog)
add_third_party(workflow workflow)
add_third_party(yamlcpp yaml-cpp)
add_third_party(libwebsockets websockets)
add_third_party(soci soci)
add_third_party(jsoncpp jsoncpp)
add_third_party(drogon drogon)
add_third_party(hdbscan hdbscan)
add_third_party(casbin casbin)
add_third_party(minidocx minidocx)
add_third_party(xlnt xlnt)
add_third_party(re2 re2)
# absl: protobuf 6.x / gRPC 依赖, 批量创建所有 absl IMPORTED 目标并聚合为 absl::all
add_third_party(absl absl)
# openssl: 优先使用 third_part 预编译的 libssl.so / libcrypto.so, 避免链接系统 OpenSSL 3.x
add_third_party(openssl ssl)
add_third_party(openssl crypto)
add_third_party(pugixml pugixml)
add_third_party(minizip minizip)

# 若是aarch64 加入sophon、onnx、numcpp库;同上
if(COMPILER_AARCH64)
    add_third_party(sophon bmlib)
    add_third_party(tcim tcim)
    add_third_party(tokenizers_c_binding tokenizers_c)
    add_third_party(onnx onnx)
    add_third_party(numcpp numcpp)
    add_third_party(kaldi-native-fbank kaldi-native-fbank-core)
endif()

# 链接库
target_link_libraries(qifeng_framework PUBLIC
    workflow::workflow
    spdlog::spdlog
    yamlcpp::yaml-cpp
    libwebsockets::websockets
    soci::core
    soci::empty
    soci::mysql
    soci::sqlite3
    jsoncpp::jsoncpp
    drogon::drogon
    drogon::trantor
    hdbscan::hdbscan
    casbin::casbin
    xlnt::xlnt
    minidocx::minidocx
    re2::re2
    pugixml::pugixml
    minizip::minizip

    curl
    gpiod
    asound
    pthread
    openssl::ssl
    openssl::crypto
    dl
    m
    z
    # 注意：gRPC 必须在后面（解决 undefined reference）！！！必须在后面
    gRPC::grpc++
    gRPC::grpc
    gRPC::gpr
    protobuf::libprotobuf
    # absl: gRPC / protobuf 6.x 依赖的 absl 库集合 (log_internal / strings / sync 等)
    absl::all
)
# 若是aarch64 加入sophon、onnx、numcpp库;同上
if(COMPILER_AARCH64)
    find_package(OpenMP REQUIRED)
    if(OpenMP_CXX_FOUND)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    endif()
    target_link_libraries(qifeng_framework PUBLIC
        OpenMP::OpenMP_CXX
        sophon::bmlib
        sophon::bmrt
        sophon::bmodel
        sophon::bmcv
        sophon::avcodec
        sophon::avformat
        sophon::avutil
        sophon::swresample
        houmo::tcim::tcim_runtime_lite
        houmo::tcim::tcim_dev_ctrl
        houmo::tcim::tcimblas
        tokenizers_c_binding::tokenizers_c
        onnx::onnx
        numcpp::numcpp
        kaldi-native-fbank::kaldi-native-fbank-core
    )
endif()



# 创建可执行文件（仅包含main.cpp）
add_executable(qifeng_example main.cpp)

# 设置可执行文件的包含目录
target_include_directories(qifeng_example PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

set_target_properties(qifeng_example PROPERTIES
    BUILD_RPATH "$ORIGIN/../lib"
    INSTALL_RPATH "$ORIGIN/../lib"
    INSTALL_RPATH_USE_LINK_PATH FALSE # 禁止将链接时的绝对路径写入 RPATH
)

# 链接公共库做测试
target_link_libraries(qifeng_example PRIVATE
    qifeng_framework
    proto_lib  # proto测试库
    # 1.0.6 升级: gRPC 1.80 头文件重度依赖 absl (log_internal 等),
    # main.cpp 通过 gRPC 头文件间接使用 absl 符号, 需显式链接 absl::all
    # (qifeng_framework 设 INTERFACE_LINK_LIBRARIES="" 阻止依赖传播)
    absl::all
)
