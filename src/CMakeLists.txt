# 设置头文件搜索路径
include_directories(${CMAKE_SOURCE_DIR}/include)

# 获得ipc 的所有.cpp
file(GLOB_RECURSE IPC_SOURCES "ipc/*.cpp")


# 获得version.cpp
# 获取Git commit hash
find_package(Git QUIET)
if(Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
else()
    set(GIT_COMMIT "unknown")
endif()

# 获取编译时间
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S" UTC)

# 配置version.cpp模板
configure_file(
    ${CMAKE_SOURCE_DIR}/src/scmd/version.cpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/version_generated.cpp
    @ONLY
)


# 获得common 的所有.cpp
file(GLOB_RECURSE COMMON_SOURCES "common/*.cpp")

# 获得service_manger 的所有.cpp
file(GLOB_RECURSE SERVICE_MANAGER_SOURCES "service_manger/*.cpp")

# 获得scmd 的所有.cpp
file(GLOB_RECURSE SCMD_SOURCES "scmd/*.cpp")

# 获得scmctl 的所有.cpp
file(GLOB_RECURSE SCMCTL_SOURCES "scmctl/*.cpp")

# 获得service_tool 的所有.cpp
file(GLOB_RECURSE SERVICE_TOOL_SOURCES "service_tool/*.cpp")

# checker 作为独立库构建（静态库 + 动态库），主程序链接静态库
add_subdirectory(checker)

# checker_runner.cpp 是主程序桥接层（qifeng::scm 命名空间），不属于 checker 库
set(CHECKER_RUNNER_SOURCES "checker/checker_runner.cpp")

add_executable(qifeng_scmd ${SCMD_SOURCES} ${COMMON_SOURCES} ${SERVICE_MANAGER_SOURCES} ${IPC_SOURCES} ${SERVICE_TOOL_SOURCES} ${CHECKER_RUNNER_SOURCES} ${CMAKE_CURRENT_BINARY_DIR}/version_generated.cpp)
target_link_libraries(qifeng_scmd PRIVATE qifeng_framework::qifeng_framework systemd mysqlclient qifeng_checker)
set_target_properties(qifeng_scmd PROPERTIES OUTPUT_NAME "qf_scmd")


add_executable(qifeng_scmctl ${SCMCTL_SOURCES} ${IPC_SOURCES} ${COMMON_SOURCES})
target_link_libraries(qifeng_scmctl PRIVATE qifeng_framework::qifeng_framework systemd)
set_target_properties(qifeng_scmctl PROPERTIES OUTPUT_NAME "qf_scmc")