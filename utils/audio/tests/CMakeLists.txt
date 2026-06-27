# Test executable. Uses a tiny home-grown assertion framework (no GoogleTest
# dependency) to keep the project edge-friendly and zero-dependency.
add_executable(test_audio_reader test_audio_reader.cpp)

target_link_libraries(test_audio_reader PRIVATE utils_audio_static)

target_compile_options(test_audio_reader PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<BOOL:${UTILS_WARNINGS_AS_ERRORS}>:-Werror>
)

# Path to the test audio file (sibling of the project root by default).
# Override with -DUTILS_AUDIO_TEST_FILE=/path/to/file at configure time.
if(DEFINED UTILS_AUDIO_TEST_FILE)
    set(UTILS_AUDIO_TEST_FILE_VALUE "${UTILS_AUDIO_TEST_FILE}")
else()
    set(UTILS_AUDIO_TEST_FILE_VALUE "${CMAKE_SOURCE_DIR}/../一小时空音频.wav")
endif()
target_compile_definitions(test_audio_reader PRIVATE
    UTILS_AUDIO_TEST_FILE="${UTILS_AUDIO_TEST_FILE_VALUE}"
)

# Allow the test to be invoked with an argv[1] override too.
add_test(NAME audio_reader_tests COMMAND test_audio_reader)
