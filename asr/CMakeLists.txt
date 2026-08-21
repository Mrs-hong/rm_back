# ============================================================================
# AAS (实时转写) 独立 CMake 项目
#
# 构建：  cd /home/lyh/data2/code/asr && mkdir -p build && cd build && cmake .. && make -j
# 运行：  cd /home/lyh/data2/code/asr && ./build/bin/flow_transcribe_test
#         （须在项目根目录运行，因测试相对路径为 ./data ./config）
#
# 依赖：tcim(houmo) / onnxruntime / openssl(系统3.0.2) / ffmpeg(剥离自sophon) /
#       workflow / kaldi-native-fbank / hdbscan / yaml-cpp / jsoncpp / spdlog /
#       eigen / numcpp / nlohmann / sanisizer / knncolle / umappp
# 已移除：sophon BMNN SDK、drogon、grpc、lms 等无关库
# ============================================================================

cmake_minimum_required(VERSION 3.15)
project(asr_standalone LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 与原项目一致：deprecation warning 视为 error（OpenSSL 3.0.2 EVP API 要求）
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=deprecated-declarations -fPIC -O2 -g")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# 为 clangd 生成 compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- 三方库根 ----
set(TP_DIR ${CMAKE_SOURCE_DIR}/third_party)
include_directories(SYSTEM ${TP_DIR}/include)
# 部分库的头嵌套一层目录：eigen/<Eigen/..>、numcpp/<NumCpp..>、jsoncpp/<json/..>、yamlcpp/<yaml-cpp/..>、houmo/<tcim/..,half/..>、onnx/<onnxruntime_*.h>
include_directories(SYSTEM
    ${TP_DIR}/include/eigen
    ${TP_DIR}/include/numcpp
    ${TP_DIR}/include/jsoncpp
    ${TP_DIR}/include/yamlcpp
    ${TP_DIR}/include/houmo
    ${TP_DIR}/include/onnx
)
link_directories(${TP_DIR}/lib)
# 运行时让可执行直接找到 third_party/lib 下的 .so（绝对路径，避免 $ORIGIN 相对计算）
set(CMAKE_BUILD_RPATH "${TP_DIR}/lib")
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")

# ---- 系统依赖 ----
find_package(OpenMP REQUIRED)
find_package(Threads REQUIRED)
# OpenSSL 使用系统 3.0.2（/usr/include/openssl），不拷贝

# ---- 主静态库：只编 AAS 链路源码 ----
add_library(asr_core STATIC
    # aas 转写主链路
    src/aas/aas_api.cpp
    src/aas/aas_tasks.cpp
    src/aas/audio_load.cpp
    src/aas/SessionSpeakerManager.cpp
    # dag 异步图执行
    src/dag/dag.cpp
    src/dag/node.cpp
    src/dag/workflow_dag.cpp
    # common 基础设施（仅子集）
    src/common/config_manager.cpp
    src/common/logger.cpp
    src/common/utils/batch_executor.cpp
    src/common/utils/file_utils.cpp
    # models_hm：tcim 版模型管理 + 各 worker
    src/models_hm/manager/models_manager.cpp
    src/models_hm/worker/asr_worker.cpp
    src/models_hm/worker/audio_feature.cpp
    src/models_hm/worker/hotwords_worker.cpp
    src/models_hm/worker/punc_worker.cpp
    src/models_hm/worker/qwen3_asr_worker.cpp
    src/models_hm/worker/qwen3_forced_aligner_worker.cpp
    src/models_hm/worker/sv_worker.cpp
    src/models_hm/worker/vad_worker.cpp
    src/models_hm/worker/worker.cpp
)
target_include_directories(asr_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
# spdlog 内嵌 fmt 头模式（避免链接外部 fmt）
target_compile_definitions(asr_core PUBLIC FMT_HEADER_ONLY=1)

# ---- 链接三方库 ----
target_link_libraries(asr_core PUBLIC
    # 线程 / OpenMP
    Threads::Threads OpenMP::OpenMP_CXX
    # HM runtime（tcim）
    tcim_runtime_lite tcim_dev_ctrl tcimblas
    # paraformer predictor
    onnxruntime onnxruntime_providers_shared
    # MD5（EVP）
    ssl crypto
    # AudioLoad 重采样（剥离自 sophon）
    avcodec avformat avutil swresample swscale
    # dag 异步图执行
    workflow
    # Fbank/Mel 特征
    kaldi-native-fbank-core
    # 说话人聚类
    hdbscan
    # config / json
    yaml-cpp jsoncpp
    # 日志
    spdlog
    # 系统库
    dl m z pthread
)

# ---- 测试可执行 ----
add_executable(flow_transcribe_test test/flow_transcribe_test.cpp)
target_link_libraries(flow_transcribe_test PRIVATE asr_core)
set_target_properties(flow_transcribe_test PROPERTIES
    BUILD_RPATH "${TP_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
