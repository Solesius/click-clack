cmake_minimum_required(VERSION 3.25)
project(click-clack
    VERSION   0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── Options ──────────────────────────────────────────────────
option(CC_BUILD_TESTS      "Build GoogleTest suites"  ON)
option(CC_BUILD_FRONTEND   "Build Angular frontend"   OFF)
option(CC_BUILD_SERVER     "Build Drogon HTTP server (click-clack-hub); disable for FFI-only builds" ON)
option(CC_WITH_AWS         "Build celer with AWS S3 backend (requires AWS SDK fetch)" OFF)
option(CC_ENABLE_COVERAGE  "Instrument backend for gcov/lcov coverage"   OFF)

if(CC_ENABLE_COVERAGE)
    # -fprofile-update=atomic needed for multithreaded tests (Epoch ConcurrentMonotonic)
    add_compile_options(--coverage -fprofile-update=atomic -O0 -g)
    add_link_options(--coverage)
endif()

# ── Dependencies ─────────────────────────────────────────────
include(FetchContent)

find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
    pkg_check_modules(CC_ROCKSDB QUIET rocksdb)
endif()

set(_cc_rocksdb_probe_includes)
if(CC_ROCKSDB_FOUND)
    list(APPEND _cc_rocksdb_probe_includes ${CC_ROCKSDB_INCLUDE_DIRS})
endif()
if(CMAKE_INCLUDE_PATH)
    list(APPEND _cc_rocksdb_probe_includes ${CMAKE_INCLUDE_PATH})
endif()
if(DEFINED ENV{CMAKE_INCLUDE_PATH} AND NOT "$ENV{CMAKE_INCLUDE_PATH}" STREQUAL "")
    string(REPLACE ":" ";" _cc_env_include_path "$ENV{CMAKE_INCLUDE_PATH}")
    list(APPEND _cc_rocksdb_probe_includes ${_cc_env_include_path})
endif()
if(APPLE AND EXISTS "/opt/homebrew/include")
    list(APPEND _cc_rocksdb_probe_includes "/opt/homebrew/include")
endif()
if(_cc_rocksdb_probe_includes)
    list(REMOVE_DUPLICATES _cc_rocksdb_probe_includes)
    set(CMAKE_REQUIRED_INCLUDES ${_cc_rocksdb_probe_includes})
endif()

# Composite-tree storage engine
set(CELER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CELER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CELER_BUILD_ROCKSDB ON CACHE BOOL "" FORCE)
if(CC_WITH_AWS)
    set(CELER_BUILD_S3 ON CACHE BOOL "" FORCE)
else()
    set(CELER_BUILD_S3 OFF CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(
    celer
    GIT_REPOSITORY https://github.com/Solesius/celer-mem.git
    GIT_TAG        df62387aa36fa2e0af2c81c4f87a8c711cda35d9  # fix(rocksdb): RocksDB 9+ API compat
)
FetchContent_MakeAvailable(celer)

# Drogon (HTTP/WS framework) — only needed by click-clack-hub.
# The FFI shared library and click-clack-mcp stdio bridge do NOT require it,
# so CI jobs that only need libclickclack.{so,dylib} can pass
# -DCC_BUILD_SERVER=OFF and skip this heavy fetch entirely.
if(CC_BUILD_SERVER)
find_package(Drogon QUIET)
if(NOT Drogon_FOUND)
    # jsoncpp — required by Drogon, fetch if not installed
    find_package(Jsoncpp QUIET)
    if(NOT Jsoncpp_FOUND)
        FetchContent_Declare(
            jsoncpp
            GIT_REPOSITORY https://github.com/open-source-parsers/jsoncpp.git
            GIT_TAG        1.9.6
        )
        set(JSONCPP_WITH_TESTS OFF CACHE BOOL "" FORCE)
        set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
        set(BUILD_OBJECT_LIBS OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(jsoncpp)

        # Tell Drogon's FindJsoncpp where to look
        FetchContent_GetProperties(jsoncpp SOURCE_DIR jsoncpp_SOURCE_DIR)
        set(JSONCPP_INCLUDE_DIRS "${jsoncpp_SOURCE_DIR}/include" CACHE PATH "" FORCE)
        set(JSONCPP_LIBRARIES jsoncpp_static CACHE STRING "" FORCE)
    endif()

    set(BUILD_CTL        OFF CACHE BOOL "" FORCE)
    set(BUILD_ORM        OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING    OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        drogon
        GIT_REPOSITORY https://github.com/drogonframework/drogon.git
        GIT_TAG        v1.9.8
    )
    FetchContent_MakeAvailable(drogon)
endif()
endif() # CC_BUILD_SERVER

# nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# google/crc32c — hardware-accelerated CRC-32C (Castagnoli).
# Used by the WAL codec trailer. Picks SSE4.2 / ARMv8-CRC / software
# fallback at build time.
#
# NOTE: upstream 1.1.2 declares cmake_minimum_required(VERSION 3.1).
# Modern CMake (≥ 4.x, shipped by macos-14) refuses that. Configure with
# -DCMAKE_POLICY_VERSION_MINIMUM=3.5 on those hosts (see
# .github/workflows/publish-npm.yml).
set(CRC32C_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
set(CRC32C_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(CRC32C_USE_GLOG         OFF CACHE BOOL "" FORCE)
set(CRC32C_INSTALL          OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    crc32c
    GIT_REPOSITORY https://github.com/google/crc32c.git
    GIT_TAG        1.1.2
)
FetchContent_MakeAvailable(crc32c)

# ── Backend library (header-only core + compiled main) ───────
add_library(cc_core INTERFACE)
target_include_directories(cc_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/backend/include
)
target_link_libraries(cc_core INTERFACE
    celer
    nlohmann_json::nlohmann_json
    crc32c
)

# ── Server executable ────────────────────────────────────────
if(CC_BUILD_SERVER)
add_executable(click-clack-hub
    backend/src/main.cpp
)
target_link_libraries(click-clack-hub PRIVATE
    cc_core
    drogon
)
target_compile_options(click-clack-hub PRIVATE
    -Wall -Wextra -Wpedantic -Werror
)
endif()

# ── MCP stdio bridge (what VS Code spawns) ───────────────────
add_executable(click-clack-mcp
    backend/src/mcp_bridge_main.cpp
)
target_compile_options(click-clack-mcp PRIVATE
    -Wall -Wextra -Wpedantic -Werror
)

# ── FFI shared library (Bun / Node / Python / Rust / ...) ────
#
# Exposes a tiny C ABI (backend/include/click_clack/ffi/capi.h)
# that wraps Wal + Materializer + McpServer. Consumers dlopen
# this and drive the hub via JSON strings. See bindings/bun/.
add_library(click_clack_ffi SHARED
    backend/src/capi.cpp
)
set_target_properties(click_clack_ffi PROPERTIES
    OUTPUT_NAME clickclack
)
target_link_libraries(click_clack_ffi PRIVATE
    cc_core
    celer
)
target_compile_options(click_clack_ffi PRIVATE
    -Wall -Wextra -Wpedantic -Werror
)

# ── Tests ────────────────────────────────────────────────────
if(CC_BUILD_TESTS)
    enable_testing()

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
    )
    FetchContent_MakeAvailable(googletest)

    add_executable(cc_tests
        backend/tests/test_types.cpp
        backend/tests/test_codec.cpp
        backend/tests/test_epoch.cpp
        backend/tests/test_wal.cpp
        backend/tests/test_materializer.cpp
        backend/tests/test_mcp.cpp
        backend/tests/test_security.cpp
    )
    target_link_libraries(cc_tests PRIVATE
        cc_core
        click_clack_ffi
        GTest::gtest_main
        GTest::gmock
    )
    target_compile_options(cc_tests PRIVATE -Wall -Wextra)

    include(GoogleTest)
    gtest_discover_tests(cc_tests)
endif()
