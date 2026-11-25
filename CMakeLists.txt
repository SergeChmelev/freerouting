cmake_minimum_required(VERSION 3.25)
project(freerouting_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Everything is PIC so the core can also be linked into the Lua module (bindings/lua).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Link-time optimization: the compiler sees the whole program when linking and can inline the
# port's many small methods across .cpp files (getters, shape double dispatch); 1-2 % faster on
# Linux. Off for Debug and sanitizer builds and for MinGW (its GCC 13 linker reports multiple
# definitions of the collections' template vtables under LTO).
option(FR_LTO "Build with link-time optimization (except Debug, sanitizer and MinGW builds)" ON)
if(FR_LTO AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT FR_SANITIZE AND NOT FR_TSAN AND NOT MINGW)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT FR_IPO_SUPPORTED OUTPUT FR_IPO_ERROR LANGUAGES CXX)
  if(FR_IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      # Parallel LTO with as many jobs as there are cores (GCC's default is serial).
      add_link_options(-flto=auto)
    endif()
    message(STATUS "Link-time optimization: on")
  else()
    message(STATUS "Link-time optimization: not supported (${FR_IPO_ERROR})")
  endif()
endif()

find_package(PkgConfig)
find_package(Threads REQUIRED)
# Memory (MEMORY_MODEL.md): explicit ownership, no garbage collector. operator new = calloc,
# delete / jrt::dispose = free; unowned temporaries live in operation regions or the job heap.
# AddressSanitizer + LeakSanitizer (tools/mem/asan_run.sh). Arena memory is poisoned while not
# handed out.
option(FR_SANITIZE "Build with AddressSanitizer/LeakSanitizer" OFF)
if(FR_SANITIZE)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address)
endif()
# ThreadSanitizer: data races of the optimizer's worker threads (run with -mt N).
option(FR_TSAN "Build with ThreadSanitizer" OFF)
if(FR_TSAN)
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=thread)
endif()
# Dependencies: GMP (big integers of the exact geometry) and GEOS (C++ port of JTS, used by
# geometry/planar/PlanarArea like JTS in Java).
#   FR_BUNDLED_DEPS=ON  downloads and builds static copies (cmake/BundledDeps.cmake); the default
#                       when building for Windows, e.g. cross-compiling in WSL:
#                         cmake -G Ninja -B build-win -DCMAKE_TOOLCHAIN_FILE=tools/windows/mingw-toolchain.cmake
#                         cmake --build build-win --target freerouting-cpp
#   FR_BUNDLED_DEPS=OFF uses the system libraries (Linux: apt install libgmp-dev libgeos-dev).
if(WIN32)
  option(FR_BUNDLED_DEPS "Download and build static GMP and GEOS" ON)
else()
  option(FR_BUNDLED_DEPS "Download and build static GMP and GEOS" OFF)
endif()
if(FR_BUNDLED_DEPS)
  include(${CMAKE_SOURCE_DIR}/cmake/BundledDeps.cmake)
else()
  # Static GMP in executables by default; the Lua module links it shared.
  option(FR_STATIC_GMP "Link GMP statically into executables" ON)
  find_library(GMP_SHARED_LIB gmp REQUIRED)
  add_library(fr_gmp_shared INTERFACE)
  target_link_libraries(fr_gmp_shared INTERFACE ${GMP_SHARED_LIB} Threads::Threads ${CMAKE_DL_LIBS})
  add_library(fr_gmp_exe INTERFACE)
  if(FR_STATIC_GMP)
    find_library(GMP_STATIC_LIB libgmp.a REQUIRED)
    target_link_libraries(fr_gmp_exe INTERFACE ${GMP_STATIC_LIB} Threads::Threads ${CMAKE_DL_LIBS})
  else()
    target_link_libraries(fr_gmp_exe INTERFACE fr_gmp_shared)
  endif()
  add_library(fr_gmp_headers INTERFACE)  # system gmp.h: nothing to add
  find_library(GEOS_LIB geos_c REQUIRED)
  add_library(fr_geos INTERFACE)
  target_link_libraries(fr_geos INTERFACE ${GEOS_LIB})
endif()
# Windows: one self-contained .exe (no libstdc++ / libgcc / winpthread DLLs next to it).
if(WIN32)
  add_link_options(-static)
endif()

# Java semantics: signed overflow wraps; no strict-aliasing surprises.
set(FR_COMMON_FLAGS -fwrapv -fno-strict-aliasing -Wall -Wextra -Wno-unused-parameter
    -Wno-unused-variable -Wno-unused-but-set-variable -Wno-sign-compare)

# --- Java runtime emulation (OBJECT library so the global operator new override always links)
file(GLOB JRT_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/runtime/jrt/*.cpp)
# Operating-system services (runtime/platform/platform.h): one implementation per target system.
if(WIN32)
  list(APPEND JRT_SOURCES ${CMAKE_SOURCE_DIR}/runtime/platform/windows.cpp)
else()
  list(APPEND JRT_SOURCES ${CMAKE_SOURCE_DIR}/runtime/platform/linux.cpp)
endif()
add_library(jrt OBJECT ${JRT_SOURCES})
target_include_directories(jrt PUBLIC ${CMAKE_SOURCE_DIR}/runtime)
target_compile_options(jrt PRIVATE ${FR_COMMON_FLAGS})
# gmp.h (bignum) and its build when bundled
target_link_libraries(jrt PUBLIC fr_gmp_headers)

# --- Ported freerouting core
file(GLOB_RECURSE FR_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
list(FILTER FR_SOURCES EXCLUDE REGEX ".*/src/main\\.cpp$")
if(FR_SOURCES)
  add_library(frcore STATIC ${FR_SOURCES})
  target_include_directories(frcore PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/runtime)
  target_compile_options(frcore PRIVATE ${FR_COMMON_FLAGS})
  target_link_libraries(frcore PUBLIC jrt fr_geos)
  if(EXISTS ${CMAKE_SOURCE_DIR}/src/main.cpp)
    add_executable(freerouting-cpp ${CMAKE_SOURCE_DIR}/src/main.cpp)
    target_compile_options(freerouting-cpp PRIVATE ${FR_COMMON_FLAGS})
    target_link_libraries(freerouting-cpp PRIVATE frcore jrt fr_gmp_exe)
  endif()
  # Differential-testing helpers (see tools/parity/)
  # KiCad project inputs (src/io/kicad), after the Java tests
  add_executable(kicad_test ${CMAKE_SOURCE_DIR}/tests/kicad_test.cpp)
  target_compile_options(kicad_test PRIVATE ${FR_COMMON_FLAGS})
  target_link_libraries(kicad_test PRIVATE frcore jrt fr_gmp_exe)
  add_test(NAME kicad_test COMMAND kicad_test ${CMAKE_SOURCE_DIR}/upstream/fixtures)
  add_executable(board_dump ${CMAKE_SOURCE_DIR}/tools/parity/board_dump.cpp)
  target_compile_options(board_dump PRIVATE ${FR_COMMON_FLAGS})
  target_link_libraries(board_dump PRIVATE frcore jrt fr_gmp_exe)
  add_executable(trace_route ${CMAKE_SOURCE_DIR}/tools/parity/trace_route.cpp)
  target_compile_options(trace_route PRIVATE ${FR_COMMON_FLAGS})
  target_link_libraries(trace_route PRIVATE frcore jrt fr_gmp_exe)
endif()

# --- Tests
enable_testing()
add_executable(jrt_test ${CMAKE_SOURCE_DIR}/tests/jrt_test.cpp)
target_compile_options(jrt_test PRIVATE ${FR_COMMON_FLAGS})
target_link_libraries(jrt_test PRIVATE jrt fr_gmp_exe)
add_test(NAME jrt_test COMMAND jrt_test)
# jrt::BigInteger int64 fast paths vs plain GMP (MEMORY_MODEL.md stage D)
add_executable(bigint_fuzz ${CMAKE_SOURCE_DIR}/tests/bigint_fuzz.cpp)
target_compile_options(bigint_fuzz PRIVATE ${FR_COMMON_FLAGS})
target_link_libraries(bigint_fuzz PRIVATE jrt fr_gmp_exe)
add_test(NAME bigint_fuzz COMMAND bigint_fuzz)

# --- Lua 5.3 module "frcore" for the Nuklear/MoonGL front end (ui/)
if(PkgConfig_FOUND AND NOT WIN32)
  pkg_check_modules(LUA53 IMPORTED_TARGET lua5.3)
endif()
if(LUA53_FOUND AND TARGET frcore)
  add_library(frcore_lua MODULE
              ${CMAKE_SOURCE_DIR}/bindings/lua/lfrcore.cpp
              ${CMAKE_SOURCE_DIR}/bindings/lua/engine.cpp)
  target_include_directories(frcore_lua PRIVATE ${LUA53_INCLUDE_DIRS})
  target_compile_options(frcore_lua PRIVATE ${FR_COMMON_FLAGS} -fvisibility=hidden)
  target_link_libraries(frcore_lua PRIVATE frcore jrt fr_gmp_shared)
  # Self-contained: own libstdc++ (so every C++ allocation inside the module goes through the
  # port's operator new / delete, which know the job heap), symbols bound locally, only
  # luaopen_frcore exported.
  target_link_options(frcore_lua PRIVATE
                      -static-libstdc++ -static-libgcc -Wl,-Bsymbolic
                      -Wl,--version-script=${CMAKE_SOURCE_DIR}/bindings/lua/frcore.map)
  set_target_properties(frcore_lua PROPERTIES
                        PREFIX "" OUTPUT_NAME "frcore"
                        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/ui)
endif()
