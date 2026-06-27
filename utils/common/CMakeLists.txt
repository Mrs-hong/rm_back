# utils::common - header-only 公共模块
# 提供 Result<T> 等公共类型,各子库通过 utils_common target 复用
add_library(utils_common INTERFACE)
add_library(utils::common ALIAS utils_common)

target_include_directories(utils_common INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(utils_common INTERFACE cxx_std_17)
