find_package(Qt6 REQUIRED COMPONENTS Test)

get_target_property(player_sources VideoPlayer SOURCES)
list(REMOVE_ITEM player_sources main.cpp)
list(TRANSFORM player_sources PREPEND "${PROJECT_SOURCE_DIR}/")

add_executable(player_tests player_tests.cpp ${player_sources})
target_include_directories(player_tests PRIVATE "${PROJECT_SOURCE_DIR}")
foreach(property COMPILE_OPTIONS INCLUDE_DIRECTORIES LINK_DIRECTORIES LINK_LIBRARIES LINK_OPTIONS)
    get_target_property(value VideoPlayer ${property})
    if(value)
        set_property(TARGET player_tests APPEND PROPERTY ${property} "${value}")
    endif()
endforeach()
target_link_libraries(player_tests PRIVATE Qt6::Test)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(player_tests PRIVATE TEST_FFMPEG_FAILURES)
    target_link_options(player_tests PRIVATE
        -Wl,--wrap=avformat_find_stream_info
        -Wl,--wrap=avformat_close_input
        -Wl,--wrap=avcodec_open2
        -Wl,--wrap=avformat_seek_file
        -Wl,--wrap=SDL_OpenAudioDevice
    )
endif()

add_test(NAME player_tests COMMAND player_tests)
set_tests_properties(player_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy"
    TIMEOUT 60
)
