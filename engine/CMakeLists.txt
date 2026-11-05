cmake_minimum_required(VERSION 3.16)
project(softrender VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(SOFTRENDER_TOP_LEVEL ON)
else()
  set(SOFTRENDER_TOP_LEVEL OFF)
endif()

option(SOFTRENDER_BUILD_EXAMPLES "Build the examples" ${SOFTRENDER_TOP_LEVEL})
option(SOFTRENDER_BUILD_TESTS "Build the tests" ${SOFTRENDER_TOP_LEVEL})
option(SOFTRENDER_INSTALL "Generate the install target" ${SOFTRENDER_TOP_LEVEL})

# A software rasteriser is useless unoptimised
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

# Always a .a, whatever BUILD_SHARED_LIBS says. Position independent, so it can go into a shared library too.
add_library(softrender STATIC
  src/canvas.cpp
  src/nebula.cpp
  src/obj.cpp
  src/parallel.cpp
  src/png.cpp
  src/post.cpp
  src/renderer.cpp
  src/scene.cpp
  src/stars.cpp
  src/text.cpp
)
add_library(softrender::softrender ALIAS softrender)
set_target_properties(softrender PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(softrender PUBLIC cxx_std_17)
target_include_directories(softrender PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(softrender PUBLIC Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(softrender PRIVATE -Wall -Wextra)
endif()

if(SOFTRENDER_BUILD_EXAMPLES)
  add_executable(softrender_scene examples/scene.cpp)
  target_link_libraries(softrender_scene PRIVATE softrender)
  # The same scene in a window. Only the example needs SDL2, not the library.
  find_package(SDL2 CONFIG QUIET)
  if(TARGET SDL2::SDL2)
    add_executable(softrender_window examples/window.cpp)
    target_link_libraries(softrender_window PRIVATE softrender SDL2::SDL2)
  endif()
endif()

if(SOFTRENDER_BUILD_TESTS)
  enable_testing()
  add_executable(softrender_tests tests/softrender_tests.cpp)
  target_link_libraries(softrender_tests PRIVATE softrender)
  add_test(NAME softrender_tests COMMAND softrender_tests)
  add_test(NAME softrender_tests_one_thread COMMAND softrender_tests)
  set_tests_properties(softrender_tests_one_thread PROPERTIES ENVIRONMENT SOFTRENDER_THREADS=1)
endif()

if(SOFTRENDER_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)
  install(TARGETS softrender EXPORT softrenderTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(DIRECTORY include/softrender DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(EXPORT softrenderTargets NAMESPACE softrender:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/softrender)
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/softrenderConfig.cmake"
    "include(CMakeFindDependencyMacro)\nfind_dependency(Threads)\ninclude(\"\${CMAKE_CURRENT_LIST_DIR}/softrenderTargets.cmake\")\n")
  write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/softrenderConfigVersion.cmake" COMPATIBILITY SameMajorVersion)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/softrenderConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/softrenderConfigVersion.cmake"
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/softrender)
endif()
