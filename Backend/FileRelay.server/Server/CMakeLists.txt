add_executable(server_app
    src/main.cpp
)

target_compile_features(server_app PRIVATE cxx_std_20)

target_link_libraries(server_app
    PRIVATE
        asio
        ZSTD::ZSTD
)

if(MSVC)
    target_compile_options(server_app PRIVATE /permissive- /W4)
else()
    target_compile_options(server_app PRIVATE -Wall -Wextra -pedantic)
endif()
