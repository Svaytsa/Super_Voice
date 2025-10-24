cmake_minimum_required(VERSION 3.20)

include(FetchContent)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
set(CPR_BUILD_TESTS OFF CACHE BOOL "Disable CPR tests" FORCE)
set(CPR_BUILD_EXAMPLES OFF CACHE BOOL "Disable CPR examples" FORCE)
set(DROGON_BUILD_TEST OFF CACHE BOOL "Disable Drogon tests" FORCE)
set(DROGON_BUILD_BENCH OFF CACHE BOOL "Disable Drogon benchmarks" FORCE)
set(DROGON_BUILD_CTL OFF CACHE BOOL "Disable Drogon ctl" FORCE)
set(DROGON_BUILD_EXAMPLES OFF CACHE BOOL "Disable Drogon examples" FORCE)
set(DROGON_SHARED OFF CACHE BOOL "Disable Drogon shared build" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Disable third-party examples" FORCE)
set(BUILD_CTL OFF CACHE BOOL "Disable drogon ctl" FORCE)

FetchContent_Declare(
    drogon
    GIT_REPOSITORY https://github.com/drogonframework/drogon.git
    GIT_TAG v1.9.5
)

FetchContent_Declare(
    jsoncpp
    GIT_REPOSITORY https://github.com/open-source-parsers/jsoncpp.git
    GIT_TAG 1.9.5
)

FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG yaml-cpp-0.7.0
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)

FetchContent_Declare(
    cpr
    GIT_REPOSITORY https://github.com/libcpr/cpr.git
    GIT_TAG 1.10.4
)

set(JSONCPP_WITH_TESTS OFF CACHE BOOL "Disable jsoncpp tests" FORCE)
set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "Disable jsoncpp post-build tests" FORCE)

FetchContent_MakeAvailable(jsoncpp)

set(JSONCPP_INCLUDE_DIRS ${jsoncpp_SOURCE_DIR}/include CACHE PATH "" FORCE)
set(JSONCPP_LIBRARIES jsoncpp_static CACHE STRING "" FORCE)
file(MAKE_DIRECTORY ${jsoncpp_BINARY_DIR}/include/json)

set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "Disable yaml-cpp tests" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "Build yaml-cpp shared libs" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "Disable yaml-cpp tools" FORCE)

FetchContent_MakeAvailable(yaml-cpp)

FetchContent_MakeAvailable(drogon nlohmann_json cpr)

add_library(superapi_dependencies INTERFACE)
target_link_libraries(superapi_dependencies
    INTERFACE
        drogon
        trantor
        jsoncpp_static
        nlohmann_json::nlohmann_json
        yaml-cpp
        cpr::cpr
)
