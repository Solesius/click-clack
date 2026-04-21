cmake_minimum_required(VERSION 3.25)
project(click-clack
    VERSION   0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ──────────────────────────────────────────────────
option(CC_BUILD_TESTS    "Build GoogleTest suites"  ON)
option(CC_BUILD_FRONTEND "Build Angular frontend"   OFF)

# ── Dependencies ─────────────────────────────────────────────
include(FetchContent)

# Composite-tree storage engine
set(CELER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CELER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    celer
    GIT_REPOSITORY https://github.com/Solesius/celer-mem.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(celer)

# Drogon (HTTP/WS framework)
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

# nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# ── Backend library (header-only core + compiled main) ───────
add_library(cc_core INTERFACE)
target_include_directories(cc_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/backend/include
)
target_link_libraries(cc_core INTERFACE
    celer
    nlohmann_json::nlohmann_json
)

# ── Server executable ────────────────────────────────────────
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

# ── MCP stdio bridge (what VS Code spawns) ───────────────────
add_executable(click-clack-mcp
    backend/src/mcp_bridge_main.cpp
)
target_compile_options(click-clack-mcp PRIVATE
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
    )
    target_link_libraries(cc_tests PRIVATE
        cc_core
        GTest::gtest_main
        drogon
    )
    target_compile_options(cc_tests PRIVATE -Wall -Wextra)

    include(GoogleTest)
    gtest_discover_tests(cc_tests)
endif()
