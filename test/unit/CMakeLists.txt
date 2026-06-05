# 单元测试 CMakeLists.txt

set(GTEST_INCLUDE_DIR "/usr/include")
set(GTEST_LIB_DIR "${CMAKE_SOURCE_DIR}/third_part/qifeng_framework/install/lib")

# 导入 GTest 静态库
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
    ${CMAKE_SOURCE_DIR}/third_part/cli11/install/include
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
    ${CMAKE_SOURCE_DIR}/src/ipc/dbus_manager.cpp
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
    ${CMAKE_SOURCE_DIR}/src/ipc/protocil.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/uds.cpp
)

add_executable(test_uds_coc ${UDS_IPC_TEST_SOURCES} ${UDS_IPC_SOURCES_UNDER_TEST})

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

# ==============================================================================
# DatabaseInit 单元测试
# ==============================================================================

set(DATABASE_TEST_SOURCES
    test_database.cpp
)

set(DATABASE_SOURCES_UNDER_TEST
    ${CMAKE_SOURCE_DIR}/src/dbinit/database_init.cpp
    ${CMAKE_SOURCE_DIR}/src/dbinit/mysql_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/dbinit/opengauss_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/common/types.cpp
    ${CMAKE_SOURCE_DIR}/src/common/utils.cpp
)

add_executable(test_database ${DATABASE_TEST_SOURCES} ${DATABASE_SOURCES_UNDER_TEST})

target_include_directories(test_database PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${GTEST_INCLUDE_DIR}
)

target_link_libraries(test_database PRIVATE
    gtest
    gtest_main
    qifeng_framework::qifeng_framework
    pthread
)

target_compile_options(test_database PRIVATE
    -Wno-switch-default
    -Wno-switch-enum
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-old-style-cast
)

gtest_discover_tests(test_database)
