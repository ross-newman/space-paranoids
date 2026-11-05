cmake_minimum_required(VERSION 3.16)

# The version is in the VERSION file in the root
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SPACE_PARANOIDS_VERSION_FROM_FILE LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
project(space-paranoids VERSION ${SPACE_PARANOIDS_VERSION_FROM_FILE} LANGUAGES CXX)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# The renderer draws in software, so an unoptimised build is unusably slow
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL2 REQUIRED)
# On Windows SDL supplies the real entry point of a program
set(SDL2_FOR_PROGRAMS SDL2::SDL2)
if(TARGET SDL2::SDL2main)
    set(SDL2_FOR_PROGRAMS SDL2::SDL2main SDL2::SDL2)
endif()

# 1. The engine
# The generic rendering engine, built as a static library: build/engine/libsoftrender.a
add_subdirectory(engine)

# The models of the game, and a tool that draws them to a PNG
add_library(vehicles STATIC src/vehicles.cpp src/runner.cpp)
target_include_directories(vehicles PUBLIC src)
target_link_libraries(vehicles PUBLIC softrender::softrender)
add_executable(vehicle_preview tools/vehicle_preview.cpp)
target_link_libraries(vehicle_preview PRIVATE vehicles)

# The AdLib synthesizer and mixer, and the music made with them
add_library(adlib STATIC
    adlib/src/adlib_synth.cpp
    adlib/src/adlib_design.cpp
    adlib/src/music.cpp
    adlib/src/sound.cpp
)
target_include_directories(adlib PUBLIC adlib/include)
add_library(score STATIC src/score.cpp)
target_include_directories(score PUBLIC src)
target_link_libraries(score PUBLIC adlib)
add_executable(score_check tools/score_check.cpp)
target_link_libraries(score_check PRIVATE score)

# The maze game, a tool that checks and draws it, and the program that plays it
add_library(maze STATIC src/maze.cpp src/battle.cpp src/terminal.cpp)
target_include_directories(maze PUBLIC src)
target_link_libraries(maze PUBLIC softrender::softrender vehicles)
add_executable(maze_preview tools/maze_preview.cpp)
target_link_libraries(maze_preview PRIVATE maze vehicles)
add_executable(terminal_preview tools/terminal_preview.cpp)
target_link_libraries(terminal_preview PRIVATE maze)
add_executable(space-paranoids src/maze_main.cpp src/pad.cpp src/audio.cpp)
target_link_libraries(space-paranoids PRIVATE maze vehicles score ${SDL2_FOR_PROGRAMS})
if(WIN32)
    # A window, not a console program, with the icon of the game
    set_target_properties(space-paranoids PROPERTIES WIN32_EXECUTABLE ON)
    target_sources(space-paranoids PRIVATE packaging/space-paranoids.rc)
    target_include_directories(space-paranoids PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/packaging)
    # The runtime of the compiler goes inside the program, or it would need its DLLs beside it
    target_link_options(space-paranoids PRIVATE -static-libgcc -static-libstdc++)
endif()
add_executable(pad_test tools/pad_test.cpp src/pad.cpp)
target_include_directories(pad_test PRIVATE src)
target_link_libraries(pad_test PRIVATE ${SDL2_FOR_PROGRAMS})

# The checks, which need no display, sound device or controller: `ctest --test-dir build`
enable_testing()
add_test(NAME pad_test COMMAND pad_test)
add_test(NAME maze_preview COMMAND maze_preview maze_check.png)
add_test(NAME terminal_preview COMMAND terminal_preview terminal_check.png)
add_test(NAME vehicle_preview COMMAND vehicle_preview vehicle_check.png)
add_test(NAME score_check COMMAND score_check .)
add_test(NAME game_runs COMMAND space-paranoids 30)
set_tests_properties(pad_test game_runs PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
set_tests_properties(maze_preview PROPERTIES TIMEOUT 1800)

# Installing and packaging: `cpack -G DEB` in the build directory makes a .deb on Linux, and `cpack -G NSIS` an installer
# on Windows. SPACE_PARANOIDS_DISTRO names the distribution a .deb was built on, for the file name, and
# SPACE_PARANOIDS_PACKAGE_VERSION overrides the version in the VERSION file (the release workflows set it from the tag).
set(SPACE_PARANOIDS_PACKAGE_VERSION "" CACHE STRING "Version of the package, if not the one in the VERSION file")
if(NOT SPACE_PARANOIDS_PACKAGE_VERSION)
    set(SPACE_PARANOIDS_PACKAGE_VERSION "${PROJECT_VERSION}")
endif()
set(CPACK_PACKAGE_VERSION "${SPACE_PARANOIDS_PACKAGE_VERSION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/ross-newman/space-paranoids")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "First-person tank game in a generated 3D maze")

if(NOT WIN32)
    include(GNUInstallDirs)
    set(SPACE_PARANOIDS_DISTRO "" CACHE STRING "Distribution the package is built for, such as ubuntu24.04")
    execute_process(COMMAND dpkg --print-architecture OUTPUT_VARIABLE SPACE_PARANOIDS_DEB_ARCH
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT SPACE_PARANOIDS_DEB_ARCH)
        set(SPACE_PARANOIDS_DEB_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    install(TARGETS space-paranoids RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/space-paranoids.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/space-paranoids.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
    install(FILES README.md VERSION DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(FILES packaging/copyright.txt DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME copyright)

    set(CPACK_PACKAGE_NAME "space-paranoids")
    set(CPACK_PACKAGE_CONTACT "Ross Newman <37403843+ross-newman@users.noreply.github.com>")
    set(CPACK_PACKAGE_DESCRIPTION "Drive a slow, heavy tank through a generated maze and shoot down the kites flying its corridors.")
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_STRIP_FILES ON)
    set(CPACK_DEBIAN_PACKAGE_SECTION "games")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_RELEASE "1")
    if(SPACE_PARANOIDS_DISTRO)
        set(CPACK_DEBIAN_FILE_NAME
            "space-paranoids_${CPACK_PACKAGE_VERSION}-${CPACK_DEBIAN_PACKAGE_RELEASE}_${SPACE_PARANOIDS_DISTRO}_${SPACE_PARANOIDS_DEB_ARCH}.deb")
    else()
        set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    endif()
    include(CPack)
else()
    if(CMAKE_VERSION VERSION_LESS 3.21)
        message(FATAL_ERROR "The Windows installer needs CMake 3.21 or later (it finds the DLLs the game needs)")
    endif()
    get_filename_component(SPACE_PARANOIDS_COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

    install(TARGETS space-paranoids RUNTIME_DEPENDENCY_SET space_paranoids_runtime RUNTIME DESTINATION bin)
    install(RUNTIME_DEPENDENCY_SET space_paranoids_runtime
            PRE_EXCLUDE_REGEXES "api-ms-" "ext-ms-"
            POST_EXCLUDE_REGEXES ".*[/\\\\][Ss]ystem32[/\\\\].*" ".*[/\\\\][Ww]indows[/\\\\].*"
            DIRECTORIES "${SPACE_PARANOIDS_COMPILER_DIR}"
            DESTINATION bin)
    install(FILES README.md VERSION DESTINATION .)
    install(FILES packaging/copyright.txt DESTINATION . RENAME LICENSE.txt)

    set(CPACK_GENERATOR "NSIS")
    set(CPACK_PACKAGE_NAME "space-paranoids")
    set(CPACK_PACKAGE_VENDOR "Ross Newman")
    set(CPACK_PACKAGE_FILE_NAME "space-paranoids-${CPACK_PACKAGE_VERSION}-windows-x64")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "space-paranoids")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/copyright.txt")
    set(CPACK_PACKAGE_EXECUTABLES "space-paranoids" "space-paranoids")
    set(CPACK_NSIS_DISPLAY_NAME "space-paranoids")
    set(CPACK_NSIS_PACKAGE_NAME "space-paranoids")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/space-paranoids.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/space-paranoids.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\space-paranoids.exe")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/ross-newman/space-paranoids")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$DESKTOP\\\\space-paranoids.lnk' '$INSTDIR\\\\bin\\\\space-paranoids.exe'")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA "Delete '$DESKTOP\\\\space-paranoids.lnk'")
    include(CPack)
endif()
