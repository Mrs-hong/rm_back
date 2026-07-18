cmake_minimum_required(VERSION 3.15)
project(qifeng_scm VERSION 0.0.2 LANGUAGES CXX)

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

# 自动检测目标架构（允许通过 -DARCH_ARM=ON / -DARCH_X86=ON 覆盖）
if(NOT ARCH_ARM AND NOT ARCH_X86)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _CMAKE_SYSTEM_PROCESSOR_LOWER)
    if(_CMAKE_SYSTEM_PROCESSOR_LOWER MATCHES "aarch64|arm64")
        set(ARCH_ARM TRUE)
    elseif(_CMAKE_SYSTEM_PROCESSOR_LOWER MATCHES "x86_64|amd64")
        set(ARCH_X86 TRUE)
    endif()
    unset(_CMAKE_SYSTEM_PROCESSOR_LOWER)
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
        -Wl,--no-as-needed        # 保留所有间接 .so 依赖（框架 DT_NEEDED 传递链需要）
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
        $<$<VERSION_GREATER_EQUAL:${CMAKE_CXX_COMPILER_VERSION},10>:-Warith-conversion>  # 警告算术运算中的隐式类型转换（GCC 10+）
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

# qifeng_framework 安装目录，同时携带 Sophon SDK 头文件/库
set(QIFENG_FRAMEWORK_INSTALL_DIR "${CMAKE_SOURCE_DIR}/third_part/qifeng_framework/install")

# 设置库搜索路径优先级：优先使用 third_part 目录下的库
set(CMAKE_LIBRARY_PATH "${QIFENG_FRAMEWORK_INSTALL_DIR}/lib" ${CMAKE_LIBRARY_PATH})

# 设置链接器搜索路径
link_directories("${QIFENG_FRAMEWORK_INSTALL_DIR}/lib")

# GCC < 10 时 std::filesystem 需要额外链接 stdc++fs
if(COMPILER_GCC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
    link_libraries(stdc++fs)
    message(STATUS "GCC ${CMAKE_CXX_COMPILER_VERSION} < 10: linking stdc++fs for std::filesystem support")
endif()

# 依赖公共库
find_package(OpenMP REQUIRED)
find_package(Threads REQUIRED)

# 手动配置 qifeng_framework 导入目标（临时替代 find_package，因 arm 包缺少 cmake 配置文件）
# 收集 include/ 下所有一级子目录作为 include 路径，确保 jsoncpp、drogon 等子模块头文件可被找到
set(QIFENG_FRAMEWORK_INCLUDE_DIRS "${QIFENG_FRAMEWORK_INSTALL_DIR}/include")
file(GLOB FW_INC_SUBDIRS LIST_DIRECTORIES true "${QIFENG_FRAMEWORK_INSTALL_DIR}/include/*")
foreach(D ${FW_INC_SUBDIRS})
    if(IS_DIRECTORY "${D}")
        list(APPEND QIFENG_FRAMEWORK_INCLUDE_DIRS "${D}")
    endif()
endforeach()

add_library(qifeng_framework::qifeng_framework UNKNOWN IMPORTED)
# libqifeng.so 的 DT_NEEDED 传递依赖（CMake IMPORTED 目标默认不传递，需显式列出）
set(_FW_DT_NEEDED_LIBS
    "${QIFENG_FRAMEWORK_INSTALL_DIR}/lib/yamlcpp/libyaml-cpp.so"
    "${QIFENG_FRAMEWORK_INSTALL_DIR}/lib/qifeng_framework/libgpiod.so.2"
    "${QIFENG_FRAMEWORK_INSTALL_DIR}/lib/qifeng_framework/libasound.so.2"
    "${QIFENG_FRAMEWORK_INSTALL_DIR}/lib/qifeng_framework/libcurl.so.4"
    "${QIFENG_FRAMEWORK_INSTALL_DIR}/lib/soci/libsoci_core.so"
)
set_target_properties(qifeng_framework::qifeng_framework PROPERTIES
    IMPORTED_LOCATION "${QIFENG_FRAMEWORK_INSTALL_DIR}/lib/qifeng_framework/libqifeng.so"
    INTERFACE_INCLUDE_DIRECTORIES "${QIFENG_FRAMEWORK_INCLUDE_DIRS}"
    INTERFACE_LINK_LIBRARIES "${_FW_DT_NEEDED_LIBS}"
)

# 设置 RPATH：构建时优先查找 qifeng_framework 自带的库，再查找 build/lib/；
# 安装后: 系统 Sophon SDK 优先 → 框架自带回退
set(CMAKE_BUILD_RPATH "$ORIGIN/../lib;${QIFENG_FRAMEWORK_INSTALL_DIR}/lib")
set(CMAKE_INSTALL_RPATH "/opt/sophon/libsophon-current/lib;/usr/lib/qifeng-scm")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)

# --- checker 可选系统库探测（必须在 add_subdirectory(src) 之前）----------------
option(WITH_BM1684_SDK "Enable Sophon SDK (bmlib/bmrt)" ON)
option(WITH_ALSA "Enable ALSA microphone capture" ON)
option(WITH_DRM "Enable libdrm display probing" ON)
option(WITH_GPIOD "Enable libgpiod GPIO control" ON)

set(BM_SDK_ROOT "/opt/sophon/libsophon-current" CACHE PATH "Sophon SDK system fallback root")

# Sophon SDK
set(CHECKER_HAS_BM_SDK 0)
set(SOPHON_USE_BUNDLED FALSE)
if(WITH_BM1684_SDK)
  # ARM: 框架自带 Sophon SDK 优先（libqifeng.so 预编译时使用此版本，ABI 必须一致）
  # x86: 框架不自带 Sophon，直接使用系统版本
  set(_BM_SDK_INCLUDE_SEARCH_PATHS "")
  set(_BM_SDK_LIB_SEARCH_PATHS "")
  if(ARCH_ARM)
    list(APPEND _BM_SDK_INCLUDE_SEARCH_PATHS "${QIFENG_FRAMEWORK_INSTALL_DIR}/include/sophon")
    list(APPEND _BM_SDK_LIB_SEARCH_PATHS "${QIFENG_FRAMEWORK_INSTALL_DIR}/lib/sophon")
  endif()
  list(APPEND _BM_SDK_INCLUDE_SEARCH_PATHS "${BM_SDK_ROOT}/include" "/opt/sophon/libsophon-current/include")
  list(APPEND _BM_SDK_LIB_SEARCH_PATHS "${BM_SDK_ROOT}/lib" "/opt/sophon/libsophon-current/lib")

  find_path(BM_SDK_INCLUDE_DIR
    NAMES bmlib_runtime.h
    PATHS ${_BM_SDK_INCLUDE_SEARCH_PATHS}
    NO_DEFAULT_PATH)
  find_library(BM_LIB_BMLIB NAMES bmlib
    PATHS ${_BM_SDK_LIB_SEARCH_PATHS} NO_DEFAULT_PATH)
  find_library(BM_LIB_BMRT NAMES bmrt
    PATHS ${_BM_SDK_LIB_SEARCH_PATHS} NO_DEFAULT_PATH)
  find_library(BM_LIB_BMCV NAMES bmcv
    PATHS ${_BM_SDK_LIB_SEARCH_PATHS} NO_DEFAULT_PATH)

  if(BM_SDK_INCLUDE_DIR AND BM_LIB_BMLIB AND BM_LIB_BMRT AND BM_LIB_BMCV)
    set(CHECKER_HAS_BM_SDK 1)
    message(STATUS "Sophon SDK: found (include=${BM_SDK_INCLUDE_DIR}, bmlib=${BM_LIB_BMLIB})")

    # 判断是否使用了 qifeng_framework 自带的库
    string(FIND "${BM_SDK_INCLUDE_DIR}" "${QIFENG_FRAMEWORK_INSTALL_DIR}/include/sophon" _BUNDLED_INCLUDE_POS)
    if(_BUNDLED_INCLUDE_POS EQUAL 0)
      set(SOPHON_USE_BUNDLED TRUE)
      message(STATUS "Sophon SDK: using bundled SDK from qifeng_framework")
    endif()
  else()
    message(WARNING "Sophon SDK not found, TPU/model checkers will be skipped at runtime")
  endif()

  # libqifeng.so 内部依赖 Sophon SDK 库，追加到框架传递链接
  if(CHECKER_HAS_BM_SDK)
      target_link_libraries(qifeng_framework::qifeng_framework INTERFACE
          "${BM_LIB_BMLIB}" "${BM_LIB_BMRT}" "${BM_LIB_BMCV}")
  endif()

  unset(_BM_SDK_INCLUDE_SEARCH_PATHS)
  unset(_BM_SDK_LIB_SEARCH_PATHS)
  unset(_BUNDLED_INCLUDE_POS)
endif()

# ALSA
set(CHECKER_HAS_ALSA 0)
if(WITH_ALSA)
  find_path(ALSA_INCLUDE_DIR NAMES asoundlib.h PATH_SUFFIXES alsa)
  find_library(ALSA_LIB NAMES asound)
  if(ALSA_INCLUDE_DIR AND ALSA_LIB)
    set(CHECKER_HAS_ALSA 1)
    message(STATUS "ALSA: found")
  else()
    message(STATUS "ALSA not found, microphone checker will be Skipped at runtime")
  endif()
endif()

# libdrm
set(CHECKER_HAS_DRM 0)
if(WITH_DRM)
  find_path(DRM_INCLUDE_DIR NAMES xf86drm.h PATH_SUFFIXES libdrm)
  find_library(DRM_LIB NAMES drm)
  if(DRM_INCLUDE_DIR AND DRM_LIB)
    set(CHECKER_HAS_DRM 1)
    message(STATUS "libdrm: found")
  else()
    message(STATUS "libdrm not found, display checker falls back to sysfs probing")
  endif()
endif()

# libgpiod
set(CHECKER_HAS_GPIOD 0)
if(WITH_GPIOD)
  find_path(GPIOD_INCLUDE_DIR NAMES gpiod.h PATH_SUFFIXES gpiod)
  find_library(GPIOD_LIB NAMES gpiod)
  if(GPIOD_INCLUDE_DIR AND GPIOD_LIB)
    set(CHECKER_HAS_GPIOD 1)
    message(STATUS "libgpiod: found")
  else()
    message(STATUS "libgpiod not found, light checker will be Skipped at runtime")
  endif()
endif()

# 生成 checker 编译期配置头
configure_file(
  ${CMAKE_SOURCE_DIR}/include/checker/generated/config.h.in
  ${CMAKE_BINARY_DIR}/generated/checker/config.h
  @ONLY)

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

# 安装参考配置文件到系统配置目录
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/config/scmd.yaml
    DESTINATION /etc/qifeng-scm
)

# 安装自检配置文件
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/config/selftest.json
    DESTINATION /etc/qifeng-scm
)

# 安装自检模型文件（随包安装到 /etc/qifeng-scm，卸载时与其它配置一并移除）
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/model/fsmn_fp32_.bmodel
    DESTINATION /etc/qifeng-scm
)

# 安装 systemd 服务单元
install(FILES ${CMAKE_SOURCE_DIR}/debian/qifeng-scmd.service
    DESTINATION /lib/systemd/system
)

# 新需求3.1：安装 logrotate 轮转配置（5M 截断 + rotate 1 + copytruncate）
install(FILES ${CMAKE_SOURCE_DIR}/debian/logrotate.d/qifeng-scm
    DESTINATION /etc/logrotate.d
)

# 安装 ld.so.conf.d 配置
install(FILES ${CMAKE_SOURCE_DIR}/debian/ld.so.conf.d/qifeng-scm.conf
    DESTINATION /etc/ld.so.conf.d
)

set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third_part)
file(GLOB THIRD_PARTY_SUBDIRS LIST_DIRECTORIES true RELATIVE ${THIRD_PARTY_DIR} ${THIRD_PARTY_DIR}/*)

# 平铺安装所有 .so 文件到私有库目录
# libsophon 系列库：ARM 架构使用 qifeng_framework 自带的版本并随包发布；
# x86 环境使用运行系统安装的版本，不打包安装。
# 仅安装 .so 动态库，.a 静态库已在编译时链接进可执行文件，运行时不需要
file(GLOB_RECURSE THIRD_PARTY_LIBS
    ${THIRD_PARTY_DIR}/*/install/lib/*.so*
)
set(SOPHON_LIB_PREFIXES
    libbmlib.so
    libbmrt.so
    libbmcv.so
    libbmcv_cpu_func.so
    libbmvpuapi.so
    libbmvpulite.so
    libbmvideo.so
    libbmjpuapi.so
    libbmjpulite.so
    libbmion.so
    libbmvppapi.so
)
foreach(LIB ${THIRD_PARTY_LIBS})
    get_filename_component(LIB_BASENAME ${LIB} NAME)
    set(IS_SOPHON_LIB FALSE)
    foreach(PREFIX ${SOPHON_LIB_PREFIXES})
        if(LIB_BASENAME MATCHES "^${PREFIX}")
            set(IS_SOPHON_LIB TRUE)
            break()
        endif()
    endforeach()
    if(NOT IS_SOPHON_LIB OR SOPHON_USE_BUNDLED)
        install(FILES ${LIB} DESTINATION /usr/lib/qifeng-scm)
    else()
        message(STATUS "Skipping system libsophon library from install: ${LIB_BASENAME}")
    endif()
endforeach()

# 自动检测系统架构和操作系统
execute_process(
    COMMAND dpkg --print-architecture
    OUTPUT_VARIABLE CPACK_DEBIAN_PACKAGE_ARCHITECTURE
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
    COMMAND lsb_release -si
    OUTPUT_VARIABLE OS_NAME
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(TOLOWER "${OS_NAME}" OS_NAME)

# 包信息
set(CPACK_PACKAGE_NAME "qifeng-scm")
set(CPACK_PACKAGE_VENDOR "Qifeng Shunshi Co., Ltd.")
set(CPACK_PACKAGE_CONTACT "support@qifengscm.com")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")

# 显式设置 CPACK_PACKAGE_VERSION（必须在 CPACK_DEBIAN_FILE_NAME 之前）
# 修复：原代码此处 CPACK_PACKAGE_VERSION 尚未被 include(CPack) 计算，值为空，
#       导致文件名出现双横杠 "qifeng-scm--ubuntu-arm64.deb"
# CI 可通过环境变量 CI_PACKAGE_VERSION 注入完整版本号（如 0.0.2.B0716.12345）
if(DEFINED ENV{CI_PACKAGE_VERSION})
    set(CPACK_PACKAGE_VERSION "$ENV{CI_PACKAGE_VERSION}")
else()
    set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
endif()

set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "qifeng-scm")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.28), libstdc++6 (>= 9), libgcc-s1 (>= 9), libsystemd0 (>= 244)")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${OS_NAME}-${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}.deb")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_SOURCE_DIR}/debian/preinst"
    "${CMAKE_SOURCE_DIR}/debian/postinst"
    "${CMAKE_SOURCE_DIR}/debian/prerm"
    "${CMAKE_SOURCE_DIR}/debian/postrm"
)

include(CPack)
