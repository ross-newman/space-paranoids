cmake_minimum_required(VERSION 3.10)

project(space-paranoids CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# The renderer draws in software, so an unoptimised build is unusably slow
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Find SDL2 library using pkg-config
find_package(SDL2 REQUIRED)

# 1. The engine
# The generic rendering engine, built as a static library: build/engine/libsoftrender.a
add_subdirectory(engine)

# The models of the game, and a tool that draws them to a PNG
add_library(vehicles STATIC src/vehicles.cpp)
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
target_link_libraries(space-paranoids PRIVATE maze vehicles score SDL2::SDL2)
add_executable(pad_test tools/pad_test.cpp src/pad.cpp)
target_include_directories(pad_test PRIVATE src)
target_link_libraries(pad_test PRIVATE SDL2::SDL2)
