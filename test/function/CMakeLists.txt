set(GTEST_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_part/qifeng_framework/install/include")
set(GTEST_LIB_DIR "${CMAKE_SOURCE_DIR}/third_part/qifeng_framework/install/lib")
set(YAMLCPP_LIB_DIR "${CMAKE_SOURCE_DIR}/third_part/qifeng_framework/install/lib")
set(YAMLCPP_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_part/qifeng_framework/install/include")

add_library(gtest_func STATIC IMPORTED)
set_target_properties(gtest_func PROPERTIES
    IMPORTED_LOCATION "${GTEST_LIB_DIR}/libgtest.a"
    INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INCLUDE_DIR}"
)

add_library(gtest_main_func STATIC IMPORTED)
set_target_properties(gtest_main_func PROPERTIES
    IMPORTED_LOCATION "${GTEST_LIB_DIR}/libgtest_main.a"
    INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INCLUDE_DIR}"
)

set(FUNCTION_TEST_SOURCES
    test_mariadb.cpp
)

set(FUNCTION_SOURCES_UNDER_TEST
    ${CMAKE_SOURCE_DIR}/src/common/config.cpp
    ${CMAKE_SOURCE_DIR}/src/common/utils.cpp
    ${CMAKE_SOURCE_DIR}/src/common/scmd_types.cpp
    ${CMAKE_SOURCE_DIR}/src/dbinit/database_init.cpp
    ${CMAKE_SOURCE_DIR}/src/dbinit/mysql_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/dbinit/opengauss_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/service_manger/service_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/service_manger/file_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/service_manger/service_generator.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/dbus_manager.cpp
)

add_executable(test_mariadb ${FUNCTION_TEST_SOURCES} ${FUNCTION_SOURCES_UNDER_TEST})

target_include_directories(test_mariadb PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_part/cli11/install/include
    ${GTEST_INCLUDE_DIR}
    ${YAMLCPP_INCLUDE_DIR}
)

target_link_libraries(test_mariadb PRIVATE
    gtest_func
    gtest_main_func
    qifeng_framework::qifeng_framework
    "${YAMLCPP_LIB_DIR}/libyaml-cpp.so"
    systemd
    pthread
)

target_compile_options(test_mariadb PRIVATE
    -Wno-switch-default
    -Wno-switch-enum
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-old-style-cast
    -Wno-stack-usage
)

gtest_discover_tests(test_mariadb)
