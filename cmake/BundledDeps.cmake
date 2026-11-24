# GMP and GEOS built from source as static libraries (FR_BUNDLED_DEPS=ON; the default when
# building for Windows). The sources are downloaded once (SHA-256 checked) and built with the
# project's compiler (also when cross-compiling, e.g. MinGW-w64 in WSL), installed into
# <build>/deps. Defines the same targets as the system-library path in CMakeLists.txt:
#   fr_gmp_exe, fr_gmp_shared  - GMP for executables / the Lua module
#   fr_geos                    - the GEOS C API
include(ExternalProject)

set(FR_DEPS_PREFIX ${CMAKE_BINARY_DIR}/deps)
file(MAKE_DIRECTORY ${FR_DEPS_PREFIX}/include ${FR_DEPS_PREFIX}/lib)

set(FR_GMP_VERSION 6.3.0)
set(FR_GMP_URL https://ftp.gnu.org/gnu/gmp/gmp-${FR_GMP_VERSION}.tar.xz)
set(FR_GMP_SHA256 a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898)
set(FR_GEOS_VERSION 3.14.1)
set(FR_GEOS_URL https://download.osgeo.org/geos/geos-${FR_GEOS_VERSION}.tar.bz2)
set(FR_GEOS_SHA256 3c20919cda9a505db07b5216baa980bacdaa0702da715b43f176fb07eff7e716)

# GMP uses autotools: its C compiler is the C++ compiler's sibling; when cross-compiling it also
# needs the host triple and a native compiler for its build-time helper programs.
string(REGEX REPLACE "g\\+\\+([^/]*)$" "gcc\\1" FR_DEPS_CC "${CMAKE_CXX_COMPILER}")
set(FR_GMP_CONFIGURE <SOURCE_DIR>/configure --prefix=${FR_DEPS_PREFIX} --enable-static --disable-shared
    --with-pic CC=${FR_DEPS_CC})
if(CMAKE_CROSSCOMPILING)
  execute_process(COMMAND ${FR_DEPS_CC} -dumpmachine OUTPUT_VARIABLE FR_DEPS_HOST OUTPUT_STRIP_TRAILING_WHITESPACE)
  list(APPEND FR_GMP_CONFIGURE --host=${FR_DEPS_HOST} CC_FOR_BUILD=cc)
endif()
include(ProcessorCount)
ProcessorCount(FR_DEPS_JOBS)
if(FR_DEPS_JOBS EQUAL 0)
  set(FR_DEPS_JOBS 4)
endif()

ExternalProject_Add(gmp_ext
  URL ${FR_GMP_URL}
  URL_HASH SHA256=${FR_GMP_SHA256}
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  CONFIGURE_COMMAND ${FR_GMP_CONFIGURE}
  BUILD_COMMAND make -j${FR_DEPS_JOBS}
  INSTALL_COMMAND make install
  BUILD_BYPRODUCTS ${FR_DEPS_PREFIX}/lib/libgmp.a
  LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON LOG_OUTPUT_ON_FAILURE ON)

set(FR_GEOS_ARGS
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX=${FR_DEPS_PREFIX}
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DBUILD_BENCHMARKS=OFF
    -DBUILD_DOCUMENTATION=OFF -DBUILD_GEOSOP=OFF)
if(CMAKE_TOOLCHAIN_FILE)
  list(APPEND FR_GEOS_ARGS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
endif()
ExternalProject_Add(geos_ext
  URL ${FR_GEOS_URL}
  URL_HASH SHA256=${FR_GEOS_SHA256}
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  CMAKE_ARGS ${FR_GEOS_ARGS}
  BUILD_BYPRODUCTS ${FR_DEPS_PREFIX}/lib/libgeos_c.a ${FR_DEPS_PREFIX}/lib/libgeos.a
  LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON LOG_OUTPUT_ON_FAILURE ON)

# gmp.h for code that does not link GMP itself (the runtime object library)
add_library(fr_gmp_headers INTERFACE)
target_include_directories(fr_gmp_headers INTERFACE ${FR_DEPS_PREFIX}/include)
add_dependencies(fr_gmp_headers gmp_ext)
add_library(fr_gmp_exe INTERFACE)
target_include_directories(fr_gmp_exe INTERFACE ${FR_DEPS_PREFIX}/include)
target_link_libraries(fr_gmp_exe INTERFACE ${FR_DEPS_PREFIX}/lib/libgmp.a Threads::Threads)
add_dependencies(fr_gmp_exe gmp_ext)
add_library(fr_gmp_shared INTERFACE)
target_link_libraries(fr_gmp_shared INTERFACE fr_gmp_exe)

add_library(fr_geos INTERFACE)
target_include_directories(fr_geos INTERFACE ${FR_DEPS_PREFIX}/include)
# GEOS_DLL_EXPORT unset + static library: geos_c.h must not mark its functions dllimport.
target_compile_definitions(fr_geos INTERFACE GEOS_STATIC)
target_link_libraries(fr_geos INTERFACE ${FR_DEPS_PREFIX}/lib/libgeos_c.a ${FR_DEPS_PREFIX}/lib/libgeos.a)
add_dependencies(fr_geos geos_ext)
