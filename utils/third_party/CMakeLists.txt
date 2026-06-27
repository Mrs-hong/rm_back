# utils/third_party - 集中管理第三方依赖
#
# 说明:
#   - miniaudio: header-only,无需 add_library,仅暴露 include 路径
#   - minizip: 由 build.sh 从源码编译为静态库(-fPIC 已启用)
#   - pugixml: 由 build.sh 从源码编译为静态库(-fPIC 已启用)
#
# 编译前需先执行:
#   cd utils/third_party/minizip && ./build.sh
#   cd utils/third_party/pugixml && ./build.sh
#
# -fPIC 编译选项使三方库静态库既能链接到 STATIC 也能链接到 SHARED 主库

# ── miniaudio: header-only (MIT) ──
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio
)

# ── minizip: 预编译静态库 (zlib License) ──
# 许可证:zlib License(允许免费闭源商用)
# IMPORTED GLOBAL:使 IMPORTED 目标在所有子目录可见,
#   否则下游 target 链接时只能拿到目标名,导致 -l<name> 而非完整路径
add_library(minizip STATIC IMPORTED GLOBAL)
set_target_properties(minizip PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/minizip/lib/libminizip.a
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/minizip/include
)
# minizip 依赖系统 zlib
find_package(ZLIB REQUIRED)
target_link_libraries(minizip INTERFACE ZLIB::ZLIB)

# ── pugixml: 预编译静态库 (MIT License) ──
# 许可证:MIT License(允许免费闭源商用)
add_library(pugixml STATIC IMPORTED GLOBAL)
set_target_properties(pugixml PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/pugixml/lib/libpugixml.a
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/pugixml/include
)
