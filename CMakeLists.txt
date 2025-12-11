cmake_minimum_required(VERSION 3.16)

project(VideoPlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE ON)

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "AddressSanitizer (ASan) and ThreadSanitizer (TSan) cannot be enabled at the same time.")
endif()

set(SANITIZER_COMPILE_FLAGS "")
set(SANITIZER_LINK_FLAGS "")

if(ENABLE_ASAN)
    list(APPEND SANITIZER_COMPILE_FLAGS -fsanitize=address -fno-omit-frame-pointer -g)
    list(APPEND SANITIZER_LINK_FLAGS -fsanitize=address)
endif()

if(ENABLE_TSAN)
    list(APPEND SANITIZER_COMPILE_FLAGS -fsanitize=thread -g)
    list(APPEND SANITIZER_LINK_FLAGS -fsanitize=thread)
endif()

if(ENABLE_UBSAN)
    list(APPEND SANITIZER_COMPILE_FLAGS -fsanitize=undefined -fno-omit-frame-pointer -g)
    list(APPEND SANITIZER_LINK_FLAGS -fsanitize=undefined)
endif()

set(HARDENING_FLAGS_COMMON
    -Wall -Wformat -Wformat=2 -Wconversion -Wimplicit-fallthrough
    -Werror=format-security
    -fstack-clash-protection -fstack-protector-strong
)

if(UNIX AND NOT APPLE)
    list(APPEND HARDENING_FLAGS_COMMON
        -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3
        -fPIE
    )
endif()

list(APPEND HARDENING_FLAGS_COMMON -D_GLIBCXX_ASSERTIONS)

set(HARDENING_LINKER_FLAGS "")

if(UNIX AND NOT APPLE)
    list(APPEND HARDENING_LINKER_FLAGS
        -pie
        -Wl,-z,noexecstack
        -Wl,-z,relro
        -Wl,-z,now
        -Wl,--as-needed
        -Wl,-z,nodlopen
        -Wl,--no-copy-dt-needed-entries
    )
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    list(APPEND HARDENING_FLAGS_COMMON -Wtrampolines)
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 12.0)
        list(APPEND HARDENING_FLAGS_COMMON -Wbidi-chars=any)
    endif()
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 13.0)
        list(APPEND HARDENING_FLAGS_COMMON -fstrict-flex-arrays=3)
    endif()
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15.0)
        list(APPEND HARDENING_FLAGS_COMMON -fzero-init-padding-bits=all)
    endif()
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    list(APPEND HARDENING_FLAGS_COMMON -fcf-protection=full)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    list(APPEND HARDENING_FLAGS_COMMON -mbranch-protection=standard)
endif()

set(HARDENING_FLAGS_PRODUCTION
    -fno-delete-null-pointer-checks
    -fno-strict-overflow
    -fno-strict-aliasing
    -ftrivial-auto-var-init=zero
)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)

find_package(SDL2 REQUIRED)

find_package(spdlog REQUIRED)

find_package(PkgConfig REQUIRED)

pkg_check_modules(FFMPEG REQUIRED 
    libavformat 
    libavcodec 
    libavutil 
    libswresample 
    libswscale
)

add_executable(VideoPlayer
    log.cpp
    main.cpp
    mainwindow.cpp
    video_decoder.cpp
)

target_compile_options(VideoPlayer PRIVATE
    ${HARDENING_FLAGS_COMMON}
    $<$<CONFIG:Release,RelWithDebInfo>:${HARDENING_FLAGS_PRODUCTION}>
)

target_link_options(VideoPlayer PRIVATE
    ${HARDENING_LINKER_FLAGS}
)

if(SANITIZER_COMPILE_FLAGS)
    target_compile_options(VideoPlayer PRIVATE ${SANITIZER_COMPILE_FLAGS})
    target_link_options(VideoPlayer PRIVATE ${SANITIZER_LINK_FLAGS})
endif()

target_include_directories(VideoPlayer SYSTEM PRIVATE
    ${SDL2_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_directories(VideoPlayer PRIVATE
    ${FFMPEG_LIBRARY_DIRS}
)

target_link_libraries(VideoPlayer PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    SDL2::SDL2
    spdlog::spdlog
    ${FFMPEG_LIBRARIES}
)

if(WIN32)
    set_target_properties(VideoPlayer PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(APPLE)
    set_target_properties(VideoPlayer PROPERTIES MACOSX_BUNDLE TRUE)
endif()
