add_executable(client_app
    src/main.cpp
)

target_compile_features(client_app PRIVATE cxx_std_20)

target_link_libraries(client_app
    PRIVATE
        asio
        ZSTD::ZSTD
)

if(MSVC)
    target_compile_options(client_app PRIVATE /permissive- /W4)
else()
    target_compile_options(client_app PRIVATE -Wall -Wextra -pedantic)
endif()
