cmake_minimum_required(VERSION 3.21)
project(Chiply VERSION 0.1.0 LANGUAGES CXX)

# Optimised by default: the built-in simulator should run at full speed in
# the app. Pass -DCMAKE_BUILD_TYPE=Debug for a debug build.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CHIPLY_BUILD_GUI   "Build the Qt desktop application" ON)
option(CHIPLY_BUILD_TESTS "Build the unit tests"             ON)

include(FetchContent)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(src/core)
add_subdirectory(src/sim)
add_subdirectory(src/cli)

if(CHIPLY_BUILD_GUI)
  add_subdirectory(src/app)
endif()

if(CHIPLY_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
