# Test executable. Uses a tiny home-grown assertion framework (no GoogleTest
# dependency) to keep the project edge-friendly and zero-dependency.
add_executable(test_audio_reader test_audio_reader.cpp)

target_link_libraries(test_audio_reader PRIVATE audio_resover)

target_compile_options(test_audio_reader PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<BOOL:${AUDIO_RESOVER_WARNINGS_AS_ERRORS}>:-Werror>
)

# Path to the test audio file (sibling of the project root by default).
# Override with -DAUDIO_TEST_FILE=/path/to/file at configure time.
if(DEFINED AUDIO_TEST_FILE)
    set(AUDIO_TEST_FILE_VALUE "${AUDIO_TEST_FILE}")
else()
    set(AUDIO_TEST_FILE_VALUE "${CMAKE_SOURCE_DIR}/../一小时空音频.wav")
endif()
target_compile_definitions(test_audio_reader PRIVATE
    AUDIO_TEST_FILE="${AUDIO_TEST_FILE_VALUE}"
)

# Allow the test to be invoked with an argv[1] override too.
add_test(NAME audio_reader_tests COMMAND test_audio_reader)
