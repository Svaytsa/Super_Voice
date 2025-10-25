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
    src/middleware/request_id.cpp
    src/middleware/idempotency.cpp
    src/core/Metrics.cpp
    src/core/Tracing.cpp
    src/http/HttpServer.cpp
    src/http/ChatWebSocket.cpp
    src/http/CompanyNamespace.cpp
    src/http/companies/AgentRouter.cpp
    src/http/companies/Anphropic.cpp
    src/http/companies/DeepSeek.cpp
    src/http/companies/Gemini.cpp
    src/http/companies/HuggingFace.cpp
    src/http/companies/LaMA.cpp
    src/http/companies/MiniMax.cpp
    src/http/companies/OpenAI.cpp
    src/http/companies/OpenRouter.cpp
    src/http/companies/Perplexety.cpp
    src/http/companies/Qwen.cpp
    src/http/companies/Vertex.cpp
    src/http/companies/XAI.cpp
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
