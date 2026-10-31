cmake_minimum_required(VERSION 3.20)   # Debian bookworm ships 3.25
project(arof_stream VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# Target CPU. This Pi is a Pi 4 (Cortex-A72); use cortex-a76 for a Pi 5.
# Empty on other hosts (x86 laptop builds for the unit tests).
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  set(AROF_MCPU_DEFAULT "cortex-a72")
else()
  set(AROF_MCPU_DEFAULT "")
endif()
set(AROF_MCPU "${AROF_MCPU_DEFAULT}" CACHE STRING "-mcpu value (cortex-a72: Pi 4, cortex-a76: Pi 5, empty: none)")
if(AROF_MCPU)
  add_compile_options(-mcpu=${AROF_MCPU})
endif()
add_compile_options(-Wall -Wextra)
# No -ffast-math: the port must keep IEEE semantics (inf switches, nearbyint ties to even).

# Dependencies: FFTW (double) and nlohmann/json.
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3 REQUIRED IMPORTED_TARGET fftw3)

find_package(nlohmann_json 3.10 QUIET)
if(NOT nlohmann_json_FOUND)
  message(STATUS "nlohmann_json not found, fetching v3.11.3")
  include(FetchContent)
  FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(json)
endif()

find_package(Threads REQUIRED)

add_executable(arof_stream src/main.cpp)
target_include_directories(arof_stream PRIVATE include)
target_link_libraries(arof_stream PRIVATE PkgConfig::FFTW3 nlohmann_json::nlohmann_json Threads::Threads)

include(CTest)
if(BUILD_TESTING AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
  add_subdirectory(tests)
endif()

install(TARGETS arof_stream RUNTIME DESTINATION bin)
