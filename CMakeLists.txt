cmake_minimum_required(VERSION 3.25)
project(freerouting_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(BDWGC REQUIRED IMPORTED_TARGET bdw-gc)
find_library(GMP_LIB gmp REQUIRED)
# GEOS = C++ port of JTS (used by geometry/planar/PlanarArea, like JTS in Java).
find_library(GEOS_LIB geos_c REQUIRED)

# Java semantics: signed overflow wraps; no strict-aliasing surprises.
set(FR_COMMON_FLAGS -fwrapv -fno-strict-aliasing -Wall -Wextra -Wno-unused-parameter
    -Wno-unused-variable -Wno-unused-but-set-variable -Wno-sign-compare)

# --- Java runtime emulation (OBJECT library so the global operator new override always links)
file(GLOB JRT_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/runtime/jrt/*.cpp)
add_library(jrt OBJECT ${JRT_SOURCES})
target_include_directories(jrt PUBLIC ${CMAKE_SOURCE_DIR}/runtime)
target_compile_options(jrt PRIVATE ${FR_COMMON_FLAGS})
target_link_libraries(jrt PUBLIC PkgConfig::BDWGC ${GMP_LIB})

# --- Ported freerouting core
file(GLOB_RECURSE FR_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
list(FILTER FR_SOURCES EXCLUDE REGEX ".*/src/main\\.cpp$")
if(FR_SOURCES)
  add_library(frcore STATIC ${FR_SOURCES})
  target_include_directories(frcore PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/runtime)
  target_compile_options(frcore PRIVATE ${FR_COMMON_FLAGS})
  target_link_libraries(frcore PUBLIC jrt ${GEOS_LIB})
  if(EXISTS ${CMAKE_SOURCE_DIR}/src/main.cpp)
    add_executable(freerouting-cpp ${CMAKE_SOURCE_DIR}/src/main.cpp)
    target_compile_options(freerouting-cpp PRIVATE ${FR_COMMON_FLAGS})
    target_link_libraries(freerouting-cpp PRIVATE frcore jrt)
  endif()
endif()

# --- Tests
enable_testing()
add_executable(jrt_test ${CMAKE_SOURCE_DIR}/tests/jrt_test.cpp)
target_compile_options(jrt_test PRIVATE ${FR_COMMON_FLAGS})
target_link_libraries(jrt_test PRIVATE jrt)
add_test(NAME jrt_test COMMAND jrt_test)
