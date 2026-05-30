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

# 获得scmd 的所有.cpp
file(GLOB_RECURSE SCMD_SOURCES "scmd/*.cpp")

# 获得scmctl 的所有.cpp
file(GLOB_RECURSE SCMCTL_SOURCES "scmctl/*.cpp")




add_executable(qifeng_scmd ${SCMD_SOURCES} ${IPC_SOURCES} ${CMAKE_CURRENT_BINARY_DIR}/version_generated.cpp)
target_include_directories(qifeng_scmd PRIVATE ${CLI11_INCLUDE_DIR})
target_link_libraries(qifeng_scmd PRIVATE qifeng_framework::qifeng_framework systemd)


add_executable(qifeng_scmctl ${SCMCTL_SOURCES} ${IPC_SOURCES})
target_include_directories(qifeng_scmctl PRIVATE ${CLI11_INCLUDE_DIR})
target_link_libraries(qifeng_scmctl PRIVATE qifeng_framework::qifeng_framework)