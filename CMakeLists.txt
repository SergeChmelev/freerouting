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

find_package(PkgConfig REQUIRED)
pkg_check_modules(BDWGC REQUIRED IMPORTED_TARGET bdw-gc)
# Static GC/GMP by default: the collector's thread-local free lists are then reached through the
# fast initial-exec TLS model instead of __tls_get_addr on every allocation.
option(FR_STATIC_GC "Link Boehm GC and GMP statically into executables" ON)
# MEMORY_MODEL.md stage E: operator new = calloc, delete / jrt::dispose = free, no collector.
# Objects without an owner leak (FR_MEM_STATS prints the live bytes at exit).
option(FR_NO_GC "Build without the garbage collector (manual memory management only)" OFF)
if(FR_NO_GC)
  add_compile_definitions(JRT_NO_GC=1)
endif()
# AddressSanitizer + LeakSanitizer (tools/mem/asan_run.sh). Only without the collector: Boehm
# and ASan both replace the allocator. Arena memory is poisoned while not handed out.
option(FR_SANITIZE "Build with AddressSanitizer/LeakSanitizer (requires FR_NO_GC)" OFF)
if(FR_SANITIZE)
  if(NOT FR_NO_GC)
    message(FATAL_ERROR "FR_SANITIZE requires FR_NO_GC=ON")
  endif()
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address)
endif()
find_library(GMP_SHARED_LIB gmp REQUIRED)
# Shared GC/GMP: always used by the Lua module (Ubuntu's libgc.a uses local-exec TLS, which a
# shared object cannot contain).
add_library(fr_gc_shared INTERFACE)
target_link_libraries(fr_gc_shared INTERFACE PkgConfig::BDWGC ${GMP_SHARED_LIB})
if(FR_STATIC_GC)
  find_library(GC_STATIC_LIB libgc.a REQUIRED)
  find_library(GMP_STATIC_LIB libgmp.a REQUIRED)
  add_library(fr_gc_exe INTERFACE)
  target_link_libraries(fr_gc_exe INTERFACE ${GC_STATIC_LIB} ${GMP_STATIC_LIB} pthread dl)
else()
  add_library(fr_gc_exe INTERFACE)
  target_link_libraries(fr_gc_exe INTERFACE fr_gc_shared)
endif()
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
target_include_directories(jrt PUBLIC ${BDWGC_INCLUDE_DIRS})

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
    target_link_libraries(freerouting-cpp PRIVATE frcore jrt fr_gc_exe)
  endif()
  # Differential-testing helpers (see tools/parity/)
  add_executable(board_dump ${CMAKE_SOURCE_DIR}/tools/parity/board_dump.cpp)
  target_compile_options(board_dump PRIVATE ${FR_COMMON_FLAGS})
  target_link_libraries(board_dump PRIVATE frcore jrt fr_gc_exe)
  add_executable(trace_route ${CMAKE_SOURCE_DIR}/tools/parity/trace_route.cpp)
  target_compile_options(trace_route PRIVATE ${FR_COMMON_FLAGS})
  target_link_libraries(trace_route PRIVATE frcore jrt fr_gc_exe)
endif()

# --- Tests
enable_testing()
add_executable(jrt_test ${CMAKE_SOURCE_DIR}/tests/jrt_test.cpp)
target_compile_options(jrt_test PRIVATE ${FR_COMMON_FLAGS})
target_link_libraries(jrt_test PRIVATE jrt fr_gc_exe)
add_test(NAME jrt_test COMMAND jrt_test)
# jrt::BigInteger int64 fast paths vs plain GMP (MEMORY_MODEL.md stage D)
add_executable(bigint_fuzz ${CMAKE_SOURCE_DIR}/tests/bigint_fuzz.cpp)
target_compile_options(bigint_fuzz PRIVATE ${FR_COMMON_FLAGS})
target_link_libraries(bigint_fuzz PRIVATE jrt fr_gc_exe)
add_test(NAME bigint_fuzz COMMAND bigint_fuzz)

add_executable(dispose_check ${CMAKE_SOURCE_DIR}/tests/dispose_check.cpp)
target_compile_options(dispose_check PRIVATE ${FR_COMMON_FLAGS})
target_link_libraries(dispose_check PRIVATE jrt fr_gc_exe)
add_test(NAME dispose_check COMMAND dispose_check)
set_tests_properties(dispose_check PROPERTIES ENVIRONMENT "FR_DISPOSE_CHECK=1"
                     PASS_REGULAR_EXPRESSION "dispose check: 1001 disposed, 1 reachable")

# --- Lua 5.3 module "frcore" for the Nuklear/MoonGL front end (ui/)
pkg_check_modules(LUA53 IMPORTED_TARGET lua5.3)
if(LUA53_FOUND AND TARGET frcore)
  add_library(frcore_lua MODULE
              ${CMAKE_SOURCE_DIR}/bindings/lua/lfrcore.cpp
              ${CMAKE_SOURCE_DIR}/bindings/lua/engine.cpp)
  target_include_directories(frcore_lua PRIVATE ${LUA53_INCLUDE_DIRS})
  target_compile_options(frcore_lua PRIVATE ${FR_COMMON_FLAGS} -fvisibility=hidden)
  target_link_libraries(frcore_lua PRIVATE frcore jrt fr_gc_shared)
  # Self-contained: own libstdc++ (so every C++ allocation inside the module goes through the
  # GC-backed operator new), symbols bound locally, only luaopen_frcore exported.
  target_link_options(frcore_lua PRIVATE
                      -static-libstdc++ -static-libgcc -Wl,-Bsymbolic
                      -Wl,--version-script=${CMAKE_SOURCE_DIR}/bindings/lua/frcore.map)
  set_target_properties(frcore_lua PROPERTIES
                        PREFIX "" OUTPUT_NAME "frcore"
                        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/ui)
endif()
