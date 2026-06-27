# 音频库:decode / resample / streaming
# 同时构建静态库(.a)和动态库(.so),输出文件名均为 libutils_audio.{a,so}
set(AUDIO_SOURCES
    src/AudioReader.cpp
    src/ChannelMixer.cpp
    src/MiniaudioDecoder.cpp
    src/MiniaudioResampler.cpp
    src/miniaudio_impl.cpp
)

# ── 静态库 ──
add_library(utils_audio_static STATIC ${AUDIO_SOURCES})
add_library(utils::audio ALIAS utils_audio_static)   # 默认别名指向静态库

# ── 动态库 ──
add_library(utils_audio_shared SHARED ${AUDIO_SOURCES})
add_library(utils::audio_shared ALIAS utils_audio_shared)

# 两个 target 共用相同的 OUTPUT_NAME,产物为 libutils_audio.a / libutils_audio.so
set_target_properties(utils_audio_static utils_audio_shared
    PROPERTIES OUTPUT_NAME utils_audio
)

# ── 公共属性(include / link / compile 特性)──
foreach(_t utils_audio_static utils_audio_shared)
    target_include_directories(${_t}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(${_t}
        PUBLIC
            utils_common       # Result<T> 模板
        PRIVATE
            miniaudio          # header-only 三方库
            $<$<PLATFORM_ID:Linux>:pthread>
            $<$<PLATFORM_ID:Linux>:m>
    )

    target_compile_features(${_t} PUBLIC cxx_std_17)
    target_compile_options(${_t} PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<BOOL:${UTILS_WARNINGS_AS_ERRORS}>:-Werror>
    )
endforeach()

if(UTILS_BUILD_TESTS)
    add_subdirectory(tests)
endif()
