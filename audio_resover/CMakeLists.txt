# audio_resover - C++17 audio processing library
# Top-level CMake configuration.
cmake_minimum_required(VERSION 3.16)
project(audio_resover
    VERSION 1.0.0
    DESCRIPTION "C++17 audio processing library (decode / resample / streaming)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release for standalone builds.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- Options ---
option(AUDIO_RESOVER_BUILD_TESTS "Build the audio_resover test suite" ON)
option(AUDIO_RESOVER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# --- Library target ---
add_library(audio_resover STATIC
    src/AudioReader.cpp
    src/MiniaudioDecoder.cpp
    src/MiniaudioResampler.cpp
    src/miniaudio_impl.cpp
)
add_library(audio_resover::audio_resover ALIAS audio_resover)

target_include_directories(audio_resover
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(audio_resover PUBLIC cxx_std_17)

# Strict warnings on the library + miniaudio translation unit.
target_compile_options(audio_resover PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<BOOL:${AUDIO_RESOVER_WARNINGS_AS_ERRORS}>:-Werror>
)

# On Linux, miniaudio only needs libpthread and libm. No apt packages required.
target_link_libraries(audio_resover PRIVATE
    $<$<PLATFORM_ID:Linux>:pthread>
    $<$<PLATFORM_ID:Linux>:m>
)

# --- Tests ---
if(AUDIO_RESOVER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
