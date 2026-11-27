# Header-only libraries of the MCP / REST servers (server/): nlohmann/json and cpp-httplib (MIT).
# Nothing of them is in the repository:
#   * installed ones are used if CMake finds them (apt: nlohmann-json3-dev, libcpp-httplib-dev),
#     unless FR_BUNDLED_DEPS is on (Windows builds) or FR_FETCH_SERVER_DEPS forces the download;
#   * otherwise the pinned release headers are downloaded once into <build>/deps/include, checked
#     by SHA-256 (like GMP and GEOS in cmake/BundledDeps.cmake).
# Defines the INTERFACE targets fr_json and fr_httplib.
option(FR_FETCH_SERVER_DEPS "Download the pinned nlohmann/json and cpp-httplib headers even if installed" OFF)

set(FR_JSON_VERSION 3.11.3)
set(FR_JSON_URL https://github.com/nlohmann/json/releases/download/v${FR_JSON_VERSION}/json.hpp)
set(FR_JSON_SHA256 9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6)
set(FR_HTTPLIB_VERSION 0.18.7)
set(FR_HTTPLIB_URL https://raw.githubusercontent.com/yhirose/cpp-httplib/v${FR_HTTPLIB_VERSION}/httplib.h)
set(FR_HTTPLIB_SHA256 4770f8ea3d3fcd27b0b713c783d9b53bce3baddbd5adad13714011df96142526)

set(FR_SERVER_DEPS_INCLUDE ${CMAKE_BINARY_DIR}/deps/include)

# Downloads one header (once; again if the file does not match its hash).
function(fr_fetch_header url sha256 dest)
  if(EXISTS ${dest})
    file(SHA256 ${dest} have)
    if(have STREQUAL sha256)
      return()
    endif()
  endif()
  message(STATUS "Downloading ${url}")
  file(DOWNLOAD ${url} ${dest} EXPECTED_HASH SHA256=${sha256} TLS_VERIFY ON STATUS status)
  list(GET status 0 code)
  if(NOT code EQUAL 0)
    list(GET status 1 reason)
    file(REMOVE ${dest})
    message(FATAL_ERROR "Cannot download ${url}: ${reason}")
  endif()
endfunction()

set(FR_USE_SYSTEM_SERVER_DEPS ON)
if(FR_BUNDLED_DEPS OR FR_FETCH_SERVER_DEPS)
  set(FR_USE_SYSTEM_SERVER_DEPS OFF)
endif()

add_library(fr_json INTERFACE)
if(FR_USE_SYSTEM_SERVER_DEPS)
  find_package(nlohmann_json 3.11 CONFIG QUIET)
endif()
if(nlohmann_json_FOUND)
  target_link_libraries(fr_json INTERFACE nlohmann_json::nlohmann_json)
  message(STATUS "nlohmann/json: system ${nlohmann_json_VERSION}")
else()
  fr_fetch_header(${FR_JSON_URL} ${FR_JSON_SHA256} ${FR_SERVER_DEPS_INCLUDE}/nlohmann/json.hpp)
  target_include_directories(fr_json SYSTEM INTERFACE ${FR_SERVER_DEPS_INCLUDE})
  message(STATUS "nlohmann/json: downloaded ${FR_JSON_VERSION}")
endif()

add_library(fr_httplib INTERFACE)
if(FR_USE_SYSTEM_SERVER_DEPS)
  find_package(httplib 0.18 CONFIG QUIET)
endif()
if(httplib_FOUND)
  target_link_libraries(fr_httplib INTERFACE httplib::httplib)
  message(STATUS "cpp-httplib: system ${httplib_VERSION}")
else()
  fr_fetch_header(${FR_HTTPLIB_URL} ${FR_HTTPLIB_SHA256} ${FR_SERVER_DEPS_INCLUDE}/httplib.h)
  target_include_directories(fr_httplib SYSTEM INTERFACE ${FR_SERVER_DEPS_INCLUDE})
  target_link_libraries(fr_httplib INTERFACE Threads::Threads)
  message(STATUS "cpp-httplib: downloaded ${FR_HTTPLIB_VERSION}")
endif()
# A JSON body sent without "Content-Type: application/json" (curl -d) arrives as form data, which
# cpp-httplib limits to 8 KB by default: the same limit as any other body (set for every user of
# httplib.h, so that all translation units see the same inline code).
target_compile_definitions(fr_httplib INTERFACE "CPPHTTPLIB_FORM_URL_ENCODED_PAYLOAD_MAX_LENGTH=(140u*1024*1024)")
if(WIN32)
  # Winsock (and the crypto API cpp-httplib uses for its random numbers)
  target_link_libraries(fr_httplib INTERFACE ws2_32 crypt32)
endif()
