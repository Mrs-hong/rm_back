cmake_minimum_required(VERSION 3.15)
project(qifeng_scm VERSION 0.0.1 LANGUAGES CXX)

# 设置默认构建类型为 Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build" FORCE)
endif()

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 平台检测
if(WIN32)
    set(PLATFORM_WINDOWS TRUE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /source-charset:utf-8 /execution-charset:utf-8")
    message(STATUS "Platform: Windows")
elif(UNIX AND NOT APPLE)
    set(PLATFORM_LINUX TRUE)
    add_compile_options(-finput-charset=UTF-8)
    add_compile_options(-fexec-charset=UTF-8)
    message(STATUS "Platform: Linux")
    # 检测是否为 Debian
    if(EXISTS "/etc/debian_version")
        set(PLATFORM_DEBIAN TRUE)
        message(STATUS "Distribution: Debian")
    endif()
endif()

# 编译器检测
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(COMPILER_GCC TRUE)
    message(STATUS "Compiler: GCC ${CMAKE_CXX_COMPILER_VERSION}")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(COMPILER_CLANG TRUE)
    message(STATUS "Compiler: Clang ${CMAKE_CXX_COMPILER_VERSION}")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(COMPILER_MSVC TRUE)
    message(STATUS "Compiler: MSVC ${CMAKE_CXX_COMPILER_VERSION}")
endif()

# 设置输出目录
set(LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(EXECUTABLE_OUTPUT_PATH "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# 为所有配置类型设置一致的输出目录
foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} "${CMAKE_BINARY_DIR}/lib")
endforeach()

# 全局编译选项 - 根据架构区分
if(ARCH_ARM)
    add_compile_options(
        "$<$<CONFIG:RELEASE>:-O2;-march=armv8-a>"   # ARM架构: 降低优化级别到 O2，指定 ARMv8-A 架构
        "$<$<CONFIG:DEBUG>:-O0;-g>"                  # ARM架构: 优化级别 O0，-g: 生成调试信息
    )
elseif(ARCH_X86)
    add_compile_options(
        "$<$<CONFIG:RELEASE>:-O2;-march=x86-64>"    # x86架构: 降低优化级别到 O2，指定 x86-64 架构
        "$<$<CONFIG:DEBUG>:-O0;-g>"                  # x86架构: 优化级别 O0，-g: 生成调试信息
    )
else()
    add_compile_options(
        "$<$<CONFIG:RELEASE>:-O2>"                   # 未知架构: 仅设置优化级别 O2
        "$<$<CONFIG:DEBUG>:-O0;-g>"                  # 未知架构: 优化级别 O0，-g: 生成调试信息
    )
endif()

# 平台特定的编译选项
if(PLATFORM_WINDOWS)
    # Windows 特定选项
    if(COMPILER_MSVC)
        # MSVC 编译选项
        add_compile_options(
            "$<$<CONFIG:RELEASE>:/O2;/GL>"
            "$<$<CONFIG:DEBUG>:/Od;/Zi;/RTC1>"
        )
        
        # MSVC 安全选项
        add_compile_options(
            "/GS"           # 缓冲区安全检查
            "/sdl"          # 启用附加安全检查
            "/guard:cf"     # 控制流防护
        )
        
        # MSVC 链接选项
        add_link_options(
            "$<$<CONFIG:RELEASE>:/LTCG;/DEBUG:NONE>"
            "$<$<CONFIG:DEBUG>:/DEBUG:FULL>"
        )
        
        # 设置运行时库
        if(CMAKE_BUILD_TYPE STREQUAL "Release")
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
        else()
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDebug")
        endif()
        
    elseif(COMPILER_GCC)
        # MinGW GCC 编译选项
        add_compile_options(
            -march=native
            -mtune=native
        )
    endif()
    
elseif(PLATFORM_LINUX)
    # Linux 安全编译选项 (GCC/Clang)
    add_compile_options(
        -fstack-protector-strong
        -fstack-clash-protection
        -fcf-protection=full
        -D_FORTIFY_SOURCE=2
        -fPIC
    )
    
    # 链接时安全选项
    add_link_options(
        -Wl,-z,now
        -Wl,-z,relro
        -Wl,-z,noexecstack
    )
    
    # 架构优化 - 使用通用的x86-64架构确保兼容性
    if(COMPILER_GCC)
        add_compile_options(-mtune=native)  # 只保留mtune=native，移除march=native以确保兼容性
    endif()
endif()

# GCC/Clang 通用安全选项
if(COMPILER_GCC OR COMPILER_CLANG)
    add_compile_options(
        -Wall                        # 启用所有常见的警告信息
        #-Wextra                      # 启用额外的警告信息（-W的扩展）
        -Wpedantic                   # 严格执行ISO C/C++标准，拒绝扩展代码
        -Wformat=2                   # 加强printf/scanf等格式字符串的检查
        -Wformat-overflow=2          # 检测格式字符串可能导致的缓冲区溢出（最高级别）
        -Wformat-truncation=2        # 检测格式字符串可能导致的截断问题（最高级别）
        -Wformat-security            # 检测可能危险的格式字符串用法
        -Wnull-dereference           # 检测可能解引用空指针的代码
        -Wstack-protector            # 检测栈溢出保护相关的问题
        -Wtrampolines                # 警告关于trampoline（蹦床）代码的生成
        -Warray-bounds=2             # 加强数组边界检查（最高级别）
        -Wimplicit-fallthrough=3     # 检测switch语句中遗漏break语句的情况（最高级别）
        -Wshift-overflow=2           # 检测位移操作溢出（最高级别）
        -Wcast-qual                  # 检测丢弃const/volatile限定符的转换
        -Wstringop-overflow=4        # 检测字符串操作溢出（最高级别）
        -Wconversion                 # 警告可能改变值的隐式类型转换
        -Warith-conversion           # 警告算术运算中的隐式类型转换
        -Wlogical-op                 # 检测逻辑运算符使用中的可疑模式
        -Wduplicated-cond            # 检测重复的条件判断
        -Wduplicated-branches        # 检测重复的代码分支
        -Wno-restrict                # 检测违反restrict关键字的用法，drogon引入的超大告警太多，暂时屏蔽
        -Wshadow                     # 警告局部变量遮蔽其他变量
        # -Wstrict-overflow=4          # 加强有符号整数溢出检查（最高级别）
        -Wno-strict-overflow         # 禁用严格溢出检查的告警
        -Wundef                      # 警告使用未定义的预处理器标识符
        -Wswitch-default             # 要求switch语句包含default分支
        -Wswitch-enum                # 要求switch语句处理所有enum值
        -Wstack-usage=5120           # 警告栈使用超过2048字节的函数,嵌入式或内存受限的环境需要开启
        -Wcast-align                 # 警告可能影响对齐的指针类型转换
        -Wno-error=null-dereference  # 空指针警告不视为错误
        -Werror                      # 将所有警告视为错误
    )
    
    # Release 特定选项
    add_compile_options(
        "$<$<CONFIG:RELEASE>:-fomit-frame-pointer;-fno-plt;-fno-asynchronous-unwind-tables>"
    )
endif()

# 设置库搜索路径优先级：优先使用 third_part 目录下的库
set(CMAKE_LIBRARY_PATH "${CMAKE_SOURCE_DIR}/third_part/qifeng_framework/install/lib" ${CMAKE_LIBRARY_PATH})
set(CMAKE_LIBRARY_PATH "${CMAKE_SOURCE_DIR}/third_part/cli11/install/lib" ${CMAKE_LIBRARY_PATH})

# 设置链接器搜索路径
link_directories("${CMAKE_SOURCE_DIR}/third_part/qifeng_framework/install/lib")
link_directories("${CMAKE_SOURCE_DIR}/third_part/cli11/install/lib")

# 依赖公共库
set(qifeng_framework_DIR "${CMAKE_SOURCE_DIR}/third_part/qifeng_framework/install/lib/cmake/qifeng_framework")
find_package(qifeng_framework REQUIRED)

# CLI11 头文件库 (header-only)
set(CLI11_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_part/cli11/install/include")

# 创建可执行文件
add_subdirectory(src)

# 测试支持（通过 -DBUILD_TESTING=ON 开启）
option(BUILD_TESTING "Build tests" OFF)
if(BUILD_TESTING)
    add_subdirectory(test)
endif()

# 确保lib目录存在（Linux平台）
if(PLATFORM_LINUX)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
endif()

install(TARGETS qifeng_scmd qifeng_scmctl
    EXPORT qifeng_scmd-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# 安装配置
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/config/
    DESTINATION config
    USE_SOURCE_PERMISSIONS
)

set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_part)
file(GLOB THIRD_PARTY_SUBDIRS LIST_DIRECTORIES true RELATIVE ${THIRD_PARTY_DIR} ${THIRD_PARTY_DIR}/*)

# 平铺安装所有 .so 文件
file(GLOB_RECURSE THIRD_PARTY_LIBS
    ${THIRD_PARTY_DIR}/*/install/lib/*.so*
    ${THIRD_PARTY_DIR}/*/install/lib/*.a
)
foreach(LIB ${THIRD_PARTY_LIBS})
    install(FILES ${LIB} DESTINATION lib)
endforeach()

# 包信息
set(CPACK_PACKAGE_NAME "qifeng_scm")
set(CPACK_PACKAGE_VENDOR "Qifeng Shunshi Co., Ltd.")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "qifeng_scm")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.28), libstdc++6 (>= 9), libgcc-s1 (>= 9), libsystemd-dev (>= 244)")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")

include(CPack)
