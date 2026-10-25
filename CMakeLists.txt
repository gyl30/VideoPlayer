cmake_minimum_required(VERSION 3.16)

project(VideoPlayer LANGUAGES CXX)

include(GNUInstallDirs)
include(CheckCXXCompilerFlag)

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
    -fstack-protector-strong
)

if(UNIX AND NOT APPLE)
    list(APPEND HARDENING_FLAGS_COMMON
        -fstack-clash-protection
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
)

check_cxx_compiler_flag("-ftrivial-auto-var-init=zero" HAS_FLAG_TRIVIAL_AUTO_VAR_INIT_ZERO)
if(HAS_FLAG_TRIVIAL_AUTO_VAR_INIT_ZERO)
    list(APPEND HARDENING_FLAGS_PRODUCTION -ftrivial-auto-var-init=zero)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets OpenGL Svg)
find_package(SDL2 REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat 
    libavcodec 
    libavutil 
    libavfilter
    libswscale
)

add_executable(VideoPlayer
    log.cpp
    main.cpp
    demuxer.cpp
    decoder.cpp
    av_clock.cpp
    video_widget.cpp
    video_scaler.cpp
    playlist_store.cpp
    playlist_name_dialog.cpp
    playlist_management_dialog.cpp
    style_loader.cpp
    main_window.cpp 
    volumemeter.cpp
    sdl_audio_backend.cpp
    video_sync_thread.cpp
    resources.qrc
)

target_compile_options(VideoPlayer PRIVATE
    ${HARDENING_FLAGS_COMMON}
    $<$<CONFIG:Release,RelWithDebInfo>:${HARDENING_FLAGS_PRODUCTION}>
    -isystem /usr/local/include
)

target_link_options(VideoPlayer PRIVATE
    ${HARDENING_LINKER_FLAGS}
)

if(SANITIZER_COMPILE_FLAGS)
    target_compile_options(VideoPlayer PRIVATE ${SANITIZER_COMPILE_FLAGS})
    target_link_options(VideoPlayer PRIVATE ${SANITIZER_LINK_FLAGS})
endif()

target_include_directories(VideoPlayer SYSTEM PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    third/spdlog/include
)

target_link_directories(VideoPlayer PRIVATE
    ${FFMPEG_LIBRARY_DIRS}
)

target_link_libraries(VideoPlayer PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::OpenGL
    Qt6::Svg
    SDL2::SDL2
    PkgConfig::FFMPEG
)

if(WIN32)
    set_target_properties(VideoPlayer PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
if(APPLE)
    set_target_properties(VideoPlayer PROPERTIES MACOSX_BUNDLE TRUE)
endif()

install(TARGETS VideoPlayer
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
)

install(FILES packaging/INTERNAL-NOTICE.txt packaging/THIRD-PARTY-NOTICES.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/VideoPlayer
)
install(FILES third/spdlog/LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/VideoPlayer/licenses
    RENAME spdlog-MIT.txt
)
install(FILES third/spdlog/include/spdlog/fmt/bundled/fmt.license.rst
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/VideoPlayer/licenses
    RENAME fmt-MIT.txt
)

if(UNIX AND NOT APPLE)
    install(FILES packaging/VideoPlayer.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(FILES icons/app_icon.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME VideoPlayer.svg
    )
endif()

set(PACKAGE_VERSION_DEFAULT "0.1.0")
if(DEFINED ENV{PACKAGE_VERSION} AND NOT "$ENV{PACKAGE_VERSION}" STREQUAL "")
    set(PACKAGE_VERSION "$ENV{PACKAGE_VERSION}")
else()
    set(PACKAGE_VERSION "${PACKAGE_VERSION_DEFAULT}")
endif()

set(CPACK_PACKAGE_NAME "VideoPlayer")
set(CPACK_PACKAGE_VENDOR "gyl30")
set(CPACK_PACKAGE_CONTACT "gyl30")
set(CPACK_PACKAGE_VERSION "${PACKAGE_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "VideoPlayer")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A local video player built with Qt, FFmpeg and SDL")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/INTERNAL-NOTICE.txt")

if(UNIX AND NOT APPLE)
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    if(EXISTS "/etc/debian_version")
        set(CPACK_GENERATOR "DEB")
        set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
        set(CPACK_DEBIAN_PACKAGE_SECTION "video")
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

        # SVG 图标和平台插件由 Qt 动态加载，不会出现在 ELF 的依赖列表中。
        execute_process(
            COMMAND dpkg-query -S "*/qt6/plugins/iconengines/libqsvgicon.so"
            OUTPUT_VARIABLE svg_plugin_owner
            RESULT_VARIABLE svg_plugin_status
            ERROR_QUIET
        )
        if(NOT svg_plugin_status EQUAL 0)
            message(FATAL_ERROR "缺少 Qt SVG 图标插件，请安装发行版提供的插件包")
        endif()
        string(REGEX MATCH "^[^: \n]+" svg_plugin_package "${svg_plugin_owner}")
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-qpa-plugins, ${svg_plugin_package}, fonts-noto-cjk")

        file(READ "/etc/os-release" package_os_release)
        string(REGEX MATCH "(^|\n)ID=\"?([a-z0-9_-]+)" unused "${package_os_release}")
        set(package_distribution "${CMAKE_MATCH_2}")
        string(REGEX MATCH "(^|\n)VERSION_ID=\"?([0-9.]+)" unused "${package_os_release}")
        string(APPEND package_distribution "${CMAKE_MATCH_2}")
        set(CPACK_DEBIAN_PACKAGE_RELEASE "1~${package_distribution}")
    elseif(EXISTS "/etc/redhat-release")
        set(CPACK_GENERATOR "RPM")
        set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
        set(CPACK_RPM_PACKAGE_LICENSE "NOASSERTION")
        set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
        set(CPACK_RPM_PACKAGE_RELEASE_DIST ON)
        set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtsvg, font(notosanscjksc)")
        set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
            /usr/share/applications
            /usr/share/icons
            /usr/share/icons/hicolor
            /usr/share/icons/hicolor/scalable
            /usr/share/icons/hicolor/scalable/apps
        )
    endif()
endif()

include(CPack)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
