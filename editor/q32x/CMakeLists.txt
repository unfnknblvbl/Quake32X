# Quake 32X: the converter library (libq32x), the command-line converter
# (q32x-convert) and, inside the editor build, the editor's 32X module.
#
# Builds on its own (no Qt needed) for the game's build:
#   cmake -S editor/q32x -B build-q32x && cmake --build build-q32x
cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(q32x CXX C)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
  endif()
  set(Q32X_STANDALONE ON)
else()
  set(Q32X_STANDALONE OFF)
endif()

option(Q32X_BUILD_TESTS "Build the libq32x tests" ON)

file(GLOB Q32X_LIB_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/*.cpp)
add_library(q32x STATIC ${Q32X_LIB_SOURCES})
target_include_directories(q32x PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/include)
target_compile_features(q32x PUBLIC cxx_std_20)
# The conversions must give the same bytes on every compiler and machine:
# no fused multiply-adds, no reassociation, IEEE double everywhere.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(q32x PRIVATE -ffp-contract=off -fno-fast-math -Wall -Wextra -Wno-unused-parameter)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "86")
    target_compile_options(q32x PRIVATE -msse2 -mfpmath=sse)
  endif()
elseif(MSVC)
  target_compile_options(q32x PRIVATE /fp:precise)
endif()
if(NOT Q32X_STANDALONE)
  # inside the TrenchBroom build: its warnings-as-errors settings are for its own code
  set_target_properties(q32x PROPERTIES CXX_VISIBILITY_PRESET default)
endif()

add_executable(q32x-convert ${CMAKE_CURRENT_SOURCE_DIR}/cli/main.cpp)
target_link_libraries(q32x-convert PRIVATE q32x)
if(MINGW)
  target_link_options(q32x-convert PRIVATE -static -static-libgcc -static-libstdc++)
endif()

if(Q32X_BUILD_TESTS AND Q32X_STANDALONE)
  enable_testing()
  add_executable(q32x-tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/tests.cpp)
  target_link_libraries(q32x-tests PRIVATE q32x)
  add_test(NAME q32x-tests COMMAND q32x-tests)
endif()

if(NOT Q32X_STANDALONE)
  add_subdirectory(ui)
endif()

install(TARGETS q32x-convert RUNTIME DESTINATION bin)
