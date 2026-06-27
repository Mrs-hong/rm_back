cmake_minimum_required(VERSION 3.10)
project(docx_temp_helper C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==============================================================================
# 三方库说明
#
# minizip 和 pugixml 通过各自目录下的 build.sh 从源码编译为静态库。
# 编译前需先执行：
#   cd third_party/minizip && ./build.sh
#   cd third_party/pugixml && ./build.sh
#
# 编译产物目录结构：
#   third_party/minizip/include/  (zip.h, unzip.h, ioapi.h)
#   third_party/minizip/lib/      (libminizip.a)
#   third_party/pugixml/include/  (pugixml.hpp, pugiconfig.hpp)
#   third_party/pugixml/lib/      (libpugixml.a)
# ==============================================================================

# ───────── 三方库：minizip（源码编译静态库） ─────────
# 许可证：zlib License（允许免费闭源商用）
add_library(minizip STATIC IMPORTED)
set_target_properties(minizip PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minizip/lib/libminizip.a
)
target_include_directories(minizip INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minizip/include
)

# minizip 依赖系统 zlib
find_package(ZLIB REQUIRED)
target_link_libraries(minizip INTERFACE ZLIB::ZLIB)

# ───────── 三方库：pugixml（源码编译静态库） ─────────
# 许可证：MIT License（允许免费闭源商用）
add_library(pugixml STATIC IMPORTED)
set_target_properties(pugixml PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/third_party/pugixml/lib/libpugixml.a
)
target_include_directories(pugixml INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/pugixml/include
)

# ───────── 主库：docx_temp_helper ─────────
add_library(docx_temp_helper STATIC
    src/docx_document.cpp
    src/streaming_processor.cpp
    src/zip_utils.cpp
    src/xml_utils.cpp
    src/rich_content.cpp
)
target_include_directories(docx_temp_helper PUBLIC
    include
)
target_link_libraries(docx_temp_helper PUBLIC minizip pugixml)

# ───────── CLI 工具：test_docx ─────────
add_executable(test_docx main.cpp)
target_link_libraries(test_docx PRIVATE docx_temp_helper)

# ───────── 测试程序 ─────────
enable_testing()

# 测试数据目录（编译时注入路径，测试程序可据此定位 test/data/ 下的文件）
set(TEST_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test/data)

# 测试程序列表：目标名 => 源文件路径
set(TEST_TARGETS
    test_plain
    test_markdown
    test_html
    test_mixed
    test_streaming
    test_error
    test_generate
    test_zip
)

foreach(target ${TEST_TARGETS})
    add_executable(${target} test/${target}.cpp)
    target_link_libraries(${target} PRIVATE docx_temp_helper)
    # 测试程序需要访问内部头文件（zip_utils.h）用于验证输出
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(${target} PRIVATE TEST_DATA_DIR="${TEST_DATA_DIR}")
    add_test(NAME ${target} COMMAND ${target})
endforeach()
