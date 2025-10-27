cmake_minimum_required(VERSION 3.20)

include(FetchContent)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(googletest)

include(GoogleTest)

set(TEST_SOURCES
    environment_test.cpp
    providers/http_provider_base_test.cpp
)

add_executable(superapi_tests ${TEST_SOURCES})

target_sources(superapi_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/environment.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/providers/HttpProviderBase.cpp
)

target_include_directories(superapi_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(superapi_tests
    PRIVATE
        project_options
        project_warnings
        superapi_dependencies
        GTest::gtest_main
)

gtest_discover_tests(superapi_tests)

add_test(NAME superapi_tests COMMAND superapi_tests)
