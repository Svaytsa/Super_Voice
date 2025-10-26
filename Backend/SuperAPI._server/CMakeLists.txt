cmake_minimum_required(VERSION 3.20)

project(SuperAPI_server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(project_options INTERFACE)
target_compile_features(project_options INTERFACE cxx_std_20)

add_library(project_warnings INTERFACE)
target_compile_options(project_warnings INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(third_party)

file(GLOB_RECURSE PROJECT_HEADERS CONFIGURE_DEPENDS include/*.h include/*.hpp)

add_executable(superapi_server
    src/main.cpp
    src/app_config.cpp
    src/environment.cpp
    src/logging.cpp
    src/providers.cpp
    src/providers/HttpProviderBase.cpp
    src/providers/OpenAIProvider.cpp
    src/providers/AnthropicProvider.cpp
    src/providers/XAIProvider.cpp
    src/providers/PerplexityProvider.cpp
    src/providers/LamaProvider.cpp
    src/providers/VertexProvider.cpp
    src/providers/GeminiProvider.cpp
    src/providers/HuggingFaceProvider.cpp
    src/providers/OpenRouterProvider.cpp
    src/providers/AgentRouterProvider.cpp
    src/providers/DeepSeekProvider.cpp
    src/providers/QwenProvider.cpp
    src/providers/ZhipuProvider.cpp
    src/providers/MiniMaxProvider.cpp
    src/middleware/request_id.cpp
    src/core/Metrics.cpp
    src/core/Tracing.cpp
    ${PROJECT_HEADERS}
)

target_include_directories(superapi_server
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(superapi_server
    PRIVATE
        project_options
        project_warnings
        superapi_dependencies
)

target_compile_definitions(superapi_server
    PRIVATE
        DROGON_NO_WARN_DEPRECATED
)

install(TARGETS superapi_server RUNTIME DESTINATION bin)

add_subdirectory(tests)
