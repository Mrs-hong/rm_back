# 单元测试 CMakeLists.txt

# 动态查找 GTest 头文件路径，优先从 third_part 目录查找，回退到系统路径
find_path(GTEST_INCLUDE_DIR
    NAMES gtest/gtest.h
    PATHS
        "${CMAKE_SOURCE_DIR}/third_part/qifeng_framework/install/include"
        /usr/include
        /usr/local/include
    NO_DEFAULT_PATH
)
if(NOT GTEST_INCLUDE_DIR)
    message(FATAL_ERROR "GTest headers not found. Install libgtest-dev or add gtest headers to third_part/qifeng_framework/install/include/")
endif()
message(STATUS "GTest include dir: ${GTEST_INCLUDE_DIR}")

set(GTEST_LIB_DIR "${CMAKE_SOURCE_DIR}/third_part/qifeng_framework/install/lib/gtest")

add_library(gtest STATIC IMPORTED)
set_target_properties(gtest PROPERTIES
    IMPORTED_LOCATION "${GTEST_LIB_DIR}/libgtest.a"
    INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INCLUDE_DIR}"
)

add_library(gtest_main STATIC IMPORTED)
set_target_properties(gtest_main PROPERTIES
    IMPORTED_LOCATION "${GTEST_LIB_DIR}/libgtest_main.a"
    INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INCLUDE_DIR}"
)

# 测试源文件
set(TEST_SOURCES
    test_config.cpp
)

# 被测试的源文件
set(SOURCES_UNDER_TEST
    ${CMAKE_SOURCE_DIR}/src/common/config.cpp
    ${CMAKE_SOURCE_DIR}/src/common/utils.cpp
    ${CMAKE_SOURCE_DIR}/src/common/types.cpp
)

# 构建测试可执行文件
add_executable(test_config ${TEST_SOURCES} ${SOURCES_UNDER_TEST})

# 设置头文件搜索路径
target_include_directories(test_config PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${GTEST_INCLUDE_DIR}
)

# 链接依赖库
target_link_libraries(test_config PRIVATE
    gtest
    gtest_main
    qifeng_framework::qifeng_framework
    pthread
)

# 放宽测试目标的部分编译警告，避免 GTest 头文件触发 -Werror
target_compile_options(test_config PRIVATE
    -Wno-switch-default
    -Wno-switch-enum
    -Wno-unused-parameter
    -Wno-unused-variable
)

# 注册测试
include(GoogleTest)
gtest_discover_tests(test_config)

# ==============================================================================
# DBusManager 单元测试
# ==============================================================================

# 启用 C 语言支持（testa.c 需要）
enable_language(C)

# 编译测试服务可执行文件（testa）
add_executable(testa_dbus ${CMAKE_CURRENT_SOURCE_DIR}/test_service/testa.c)

# 将 testa 可执行文件和服务文件复制到构建目录
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/test_service/testa.service
    ${CMAKE_CURRENT_BINARY_DIR}/test_service/testa.service
    COPYONLY
)

# 测试完成后自定义命令：创建目录并复制 testa 到 test_service 子目录
add_custom_command(TARGET testa_dbus POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/test_service
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:testa_dbus>
        ${CMAKE_CURRENT_BINARY_DIR}/test_service/testa
    COMMENT "Copying testa binary to test_service directory"
)

# DBusManager 测试源文件
set(DBUS_TEST_SOURCES
    test_sd_bus.cpp
)

# DBusManager 被测试的源文件
set(DBUS_SOURCES_UNDER_TEST
    ${CMAKE_SOURCE_DIR}/src/service_manger/dbus_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/common/types.cpp
)

# 构建测试可执行文件
add_executable(test_sd_bus ${DBUS_TEST_SOURCES} ${DBUS_SOURCES_UNDER_TEST})

# 设置头文件搜索路径
target_include_directories(test_sd_bus PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${GTEST_INCLUDE_DIR}
)

# 链接依赖库
target_link_libraries(test_sd_bus PRIVATE
    gtest
    gtest_main
    qifeng_framework::qifeng_framework
    systemd
    pthread
)

# 放宽测试目标的部分编译警告
target_compile_options(test_sd_bus PRIVATE
    -Wno-switch-default
    -Wno-switch-enum
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-old-style-cast
)

# 确保 testa_dbus 在 test_sd_bus 之前构建
add_dependencies(test_sd_bus testa_dbus)

# 注册测试
gtest_discover_tests(test_sd_bus)

# ==============================================================================
# UDS IPC 通信测试
# ==============================================================================

set(UDS_IPC_TEST_SOURCES test_uds_coc.cpp)

set(UDS_IPC_SOURCES_UNDER_TEST
    ${CMAKE_SOURCE_DIR}/src/ipc/protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/uds.cpp
    ${CMAKE_SOURCE_DIR}/src/common/utils.cpp
    ${CMAKE_SOURCE_DIR}/src/common/types.cpp
)

add_executable(test_uds_coc ${UDS_IPC_TEST_SOURCES} ${UDS_IPC_SOURCES_UNDER_TEST})

# ==============================================================================
# CommandDispatcher 单元测试
# ==============================================================================

set(COMMAND_DISPATCHER_TEST_SOURCES test_command_dispatcher.cpp)

set(COMMAND_DISPATCHER_SOURCES_UNDER_TEST
    ${CMAKE_SOURCE_DIR}/src/scmd/command_dispatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/common/types.cpp
)

add_executable(test_command_dispatcher ${COMMAND_DISPATCHER_TEST_SOURCES} ${COMMAND_DISPATCHER_SOURCES_UNDER_TEST})

target_include_directories(test_uds_coc PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${GTEST_INCLUDE_DIR}
)

target_link_libraries(test_uds_coc PRIVATE
    gtest
    gtest_main
    qifeng_framework::qifeng_framework
    pthread
)

target_compile_options(test_uds_coc PRIVATE
    -Wno-switch-default
    -Wno-switch-enum
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-old-style-cast
    -Wno-stack-usage
)

gtest_discover_tests(test_uds_coc)

# CommandDispatcher 测试配置
target_include_directories(test_command_dispatcher PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${GTEST_INCLUDE_DIR}
)

target_link_libraries(test_command_dispatcher PRIVATE
    gtest
    gtest_main
    qifeng_framework::qifeng_framework
    pthread
)

target_compile_options(test_command_dispatcher PRIVATE
    -Wno-switch-default
    -Wno-switch-enum
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-old-style-cast
)

gtest_discover_tests(test_command_dispatcher)

# ==============================================================================
# DatabaseInit 单元测试
# ==============================================================================

set(DATABASE_TEST_SOURCES
    test_database.cpp
)

set(DATABASE_SOURCES_UNDER_TEST
    ${CMAKE_SOURCE_DIR}/src/common/types.cpp
    ${CMAKE_SOURCE_DIR}/src/common/utils.cpp
)


# ==============================================================================
# Mariadb 工具类单元测试
# ==============================================================================

set(MARIADB_TEST_SOURCES
    test_tool_mariadb.cpp
)

set(MARIADB_SOURCES_UNDER_TEST
    ${CMAKE_SOURCE_DIR}/src/service_tool/tool_mariadb.cpp
    ${CMAKE_SOURCE_DIR}/src/common/types.cpp
)

add_executable(test_tool_mariadb ${MARIADB_TEST_SOURCES} ${MARIADB_SOURCES_UNDER_TEST})

target_include_directories(test_tool_mariadb PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${GTEST_INCLUDE_DIR}
)

target_link_libraries(test_tool_mariadb PRIVATE
    gtest
    gtest_main
    qifeng_framework::qifeng_framework
    mysqlclient
    pthread
)

target_compile_options(test_tool_mariadb PRIVATE
    -Wno-switch-default
    -Wno-switch-enum
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-old-style-cast
)

gtest_discover_tests(test_tool_mariadb)

# ==============================================================================
# Checker 框架单元测试
# ==============================================================================

set(CHECKER_TEST_SOURCES test_check.cpp)

set(CHECKER_SOURCES_UNDER_TEST
    ${CMAKE_SOURCE_DIR}/src/common/json_load.cpp
    ${CMAKE_SOURCE_DIR}/src/common/cmd_execute.cpp
    ${CMAKE_SOURCE_DIR}/src/common/types.cpp
    ${CMAKE_SOURCE_DIR}/src/checker/core/registry.cpp
    ${CMAKE_SOURCE_DIR}/src/checker/core/runner.cpp
    ${CMAKE_SOURCE_DIR}/src/checker/checker_runner.cpp
    ${CMAKE_SOURCE_DIR}/src/checker/checkers/register_all.cpp
    ${CMAKE_SOURCE_DIR}/src/checker/checkers/disk_checker.cpp
    ${CMAKE_SOURCE_DIR}/src/checker/checkers/memory_checker.cpp
    ${CMAKE_SOURCE_DIR}/src/checker/checkers/network_checker.cpp
    ${CMAKE_SOURCE_DIR}/src/checker/checkers/tpu_checker.cpp
    ${CMAKE_SOURCE_DIR}/src/checker/checkers/model_inference_checker.cpp
    ${CMAKE_SOURCE_DIR}/src/checker/checkers/fan_checker.cpp
    ${CMAKE_SOURCE_DIR}/src/checker/checkers/display_checker.cpp
    ${CMAKE_SOURCE_DIR}/src/checker/checkers/microphone_checker.cpp
    ${CMAKE_SOURCE_DIR}/src/checker/checkers/light_checker.cpp
    ${CMAKE_SOURCE_DIR}/src/checker/checkers/fingerprint_checker.cpp
    ${CMAKE_SOURCE_DIR}/src/checker/checkers/pcba_checker.cpp
    ${CMAKE_SOURCE_DIR}/src/checker/hw/serial_port.cpp
    ${CMAKE_SOURCE_DIR}/src/checker/hw/gpio.cpp
    ${CMAKE_SOURCE_DIR}/src/checker/hw/drm_device.cpp
    ${CMAKE_SOURCE_DIR}/src/checker/hw/alsa_capture.cpp
)

add_executable(test_check ${CHECKER_TEST_SOURCES} ${CHECKER_SOURCES_UNDER_TEST})

target_include_directories(test_check PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${GTEST_INCLUDE_DIR}
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(test_check PRIVATE
    gtest
    gtest_main
    qifeng_framework::qifeng_framework
    pthread
)

target_compile_options(test_check PRIVATE
    -Wno-switch-default
    -Wno-switch-enum
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-old-style-cast
)

# 使用 PRE_TEST 发现模式：在 ctest 运行时（而非构建后）发现测试用例，
# 便于继承运行时环境（如 LD_LIBRARY_PATH 指向第三方库）
gtest_discover_tests(test_check DISCOVERY_MODE PRE_TEST)
