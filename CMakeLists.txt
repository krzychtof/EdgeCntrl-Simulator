cmake_minimum_required(VERSION 3.21)
project(EdgeCntrlSimulator VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_library(edgecontrol_core
    src/ipv4.cpp
    src/ipv6.cpp
    src/metrics_server.cpp
    src/network.cpp
    src/scenarios.cpp
    src/srv6.cpp
)
target_include_directories(edgecontrol_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
if(WIN32)
    target_link_libraries(edgecontrol_core PUBLIC ws2_32)
endif()

add_executable(edgecontrol src/main.cpp)
target_link_libraries(edgecontrol PRIVATE edgecontrol_core)

include(CTest)
include(FetchContent)
option(EDGECONTROL_BUILD_TESTS "Build unit and integration tests with GoogleTest" ON)

if(BUILD_TESTING AND EDGECONTROL_BUILD_TESTS)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.16.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(googletest)

    add_executable(edgecontrol_tests tests/gtest_tests.cpp)
    target_link_libraries(edgecontrol_tests PRIVATE edgecontrol_core GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(edgecontrol_tests)
endif()

message(STATUS "EdgeCntrl Simulator ${PROJECT_VERSION}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
if(BUILD_TESTING AND EDGECONTROL_BUILD_TESTS)
    message(STATUS "Tests enabled: ON")
else()
    message(STATUS "Tests enabled: OFF")
endif()
