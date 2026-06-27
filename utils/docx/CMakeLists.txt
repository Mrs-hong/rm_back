# DOCX 库:模板占位符替换 / 文档生成 / ZIP 工具
# 同时构建静态库(.a)和动态库(.so),输出文件名均为 libutils_docx.{a,so}
set(DOCX_SOURCES
    src/docx_document.cpp
    src/streaming_processor.cpp
    src/zip_utils.cpp
    src/xml_utils.cpp
    src/rich_content.cpp
)

# ── 静态库 ──
add_library(utils_docx_static STATIC ${DOCX_SOURCES})
add_library(utils::docx ALIAS utils_docx_static)   # 默认别名指向静态库

# ── 动态库 ──
add_library(utils_docx_shared SHARED ${DOCX_SOURCES})
add_library(utils::docx_shared ALIAS utils_docx_shared)

# 两个 target 共用相同的 OUTPUT_NAME,产物为 libutils_docx.a / libutils_docx.so
set_target_properties(utils_docx_static utils_docx_shared
    PROPERTIES OUTPUT_NAME utils_docx
)

# ── 公共属性(include / link / compile 特性)──
foreach(_t utils_docx_static utils_docx_shared)
    target_include_directories(${_t}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${CMAKE_SOURCE_DIR}/third_party/pugixml/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/third_party/minizip/include
    )

    target_link_libraries(${_t}
        PUBLIC
            utils_common
            pugixml
        PRIVATE
            minizip
    )

    target_compile_features(${_t} PUBLIC cxx_std_17)
    target_compile_options(${_t} PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<BOOL:${UTILS_WARNINGS_AS_ERRORS}>:-Werror>
    )
endforeach()

# CLI 工具(链接静态库)
add_executable(test_docx main.cpp)
target_link_libraries(test_docx PRIVATE utils_docx_static)
target_include_directories(test_docx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# 测试(链接静态库)
if(UTILS_BUILD_TESTS)
    set(TEST_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test/data)
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
        target_link_libraries(${target} PRIVATE utils_docx_static)
        target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_compile_definitions(${target} PRIVATE TEST_DATA_DIR="${TEST_DATA_DIR}")
        add_test(NAME ${target} COMMAND ${target})
    endforeach()
endif()
