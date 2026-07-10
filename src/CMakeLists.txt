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

# checker 源文件直接编译进主程序（不再构建独立的 checker 库）
file(GLOB_RECURSE CHECKER_SOURCES "checker/*.cpp")

add_executable(qifeng_scmd ${SCMD_SOURCES} ${COMMON_SOURCES} ${SERVICE_MANAGER_SOURCES} ${IPC_SOURCES} ${SERVICE_TOOL_SOURCES} ${CHECKER_SOURCES} ${CMAKE_CURRENT_BINARY_DIR}/version_generated.cpp)

# 添加 generated 头文件搜索路径（checker/config.h 等）
target_include_directories(qifeng_scmd PRIVATE
  ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(qifeng_scmd PRIVATE
  qifeng_framework::qifeng_framework
  systemd
  mysqlclient
  Threads::Threads
)

# checker 条件编译依赖（直接从原 checker_add_optional_dep 宏内联）
if(CHECKER_HAS_BM_SDK)
  target_include_directories(qifeng_scmd PRIVATE ${BM_SDK_INCLUDE_DIR})
  target_link_libraries(qifeng_scmd PRIVATE ${BM_LIB_BMRT} ${BM_LIB_BMCV} ${BM_LIB_BMLIB})
  target_compile_definitions(qifeng_scmd PRIVATE CHECKER_HAS_BM_SDK=1)
endif()
if(CHECKER_HAS_ALSA)
  target_include_directories(qifeng_scmd PRIVATE ${ALSA_INCLUDE_DIR})
  target_link_libraries(qifeng_scmd PRIVATE ${ALSA_LIB})
  target_compile_definitions(qifeng_scmd PRIVATE CHECKER_HAS_ALSA=1)
endif()
if(CHECKER_HAS_DRM)
  target_include_directories(qifeng_scmd PRIVATE ${DRM_INCLUDE_DIR})
  target_link_libraries(qifeng_scmd PRIVATE ${DRM_LIB})
  target_compile_definitions(qifeng_scmd PRIVATE CHECKER_HAS_DRM=1)
endif()
if(CHECKER_HAS_GPIOD)
  target_include_directories(qifeng_scmd PRIVATE ${GPIOD_INCLUDE_DIR})
  target_link_libraries(qifeng_scmd PRIVATE ${GPIOD_LIB})
  target_compile_definitions(qifeng_scmd PRIVATE CHECKER_HAS_GPIOD=1)
endif()

set_target_properties(qifeng_scmd PROPERTIES OUTPUT_NAME "qf_scmd")


add_executable(qifeng_scmctl ${SCMCTL_SOURCES} ${IPC_SOURCES} ${COMMON_SOURCES})
target_link_libraries(qifeng_scmctl PRIVATE qifeng_framework::qifeng_framework systemd)
set_target_properties(qifeng_scmctl PROPERTIES OUTPUT_NAME "qf_scmc")