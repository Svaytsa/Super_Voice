# Build System Work Breakdown

## Directory Structure Layout
- Establish top-level `cmake/` module directory for reusable scripts (toolchain, Find modules).
- Organize source under `src/client/` and `src/server/` with shared utilities in `src/common/`.
- Maintain `include/` mirror hierarchy for public headers; add `include/client/`, `include/server/`, `include/common/`.
- Provide `third_party/` staging area for vendored or generated dependency helpers (e.g., downloaded license files).
- Add `cmake/presets/` or `.cmake` files for toolchain configuration (Windows MSVC, Linux GCC/Clang).

## CMakeLists.txt Authoring
- Root `CMakeLists.txt` initializes project, sets C++20 standard, configures FetchContent modules, and defines global options (e.g., `SUPER_VOICE_ENABLE_TESTS`).
- `client/CMakeLists.txt` creates `client_app` executable, specifies source glob/explicit lists, include directories, and links against shared libs/dependencies.
- `server/CMakeLists.txt` defines `server_app` executable with analogous structure, plus server-specific libraries or definitions.
- Introduce interface/static library targets for reusable code: `common_core` (core utilities) consumed by both executables.

## Dependency Acquisition (Asio, Zstd)
- Implement `FetchContent` blocks in root CMake for header-only Asio (standalone) and libzstd when network fetch is acceptable.
- Provide fallback `find_package` logic: create `cmake/FindASIO.cmake` and `cmake/FindZSTD.cmake` to discover system installations.
- Cache downloaded content under build tree (`_deps`) and document offline/air-gapped workflows.
- Ensure include/link properties propagate via imported targets (`asio::asio`, `ZSTD::ZSTD`).

## Compiler Standards and Flags
- Force C++20 via `set(CMAKE_CXX_STANDARD 20)` and `CMAKE_CXX_STANDARD_REQUIRED ON` in root.
- Configure warning levels: `/W4` + `/permissive-` for MSVC, `-Wall -Wextra -Wpedantic` for GCC/Clang.
- Gate optional sanitizers (`-fsanitize=address,undefined`) behind `SUPER_VOICE_ENABLE_SANITIZERS` option.
- Add position-independent code flags for shared libs (`CMAKE_POSITION_INDEPENDENT_CODE ON`).

## Platform Support (Windows & Linux)
- Provide toolchain presets for MSVC (x64) and GCC/Clang.
- Abstract platform-specific definitions via generator expressions (e.g., `_WIN32` vs POSIX sockets).
- Manage runtime library linkage options (`/MD` vs `/MT`) with configurable cache entries.
- Validate zstd linking on Windows (import libs) and Linux (shared/static) via conditional linking logic.

## Target Definitions (client_app & server_app)
- `client_app`: depend on `common_core`, set entry point sources, add necessary compile definitions (e.g., `CLIENT_BUILD`).
- `server_app`: similar structure with `SERVER_BUILD` define and server-specific libraries (asio networking, zstd compression).
- Configure install rules to place binaries under `bin/` and shared resources under `share/`.
- Add packaging rules (CPack) placeholder for future installers.

## Linking and Build Options
- Offer `SUPER_VOICE_USE_STATIC_LINKING` toggle to choose static vs shared linking on supported toolchains.
- Ensure link-time optimization option (`SUPER_VOICE_ENABLE_LTO`) toggles `INTERPROCEDURAL_OPTIMIZATION` property.
- Manage dependency linkage order for zstd/asio, including `ws2_32` on Windows and `pthread` on Linux.
- Add custom command to embed version info generated from Git metadata.

## Local CI Validation
- Provide `cmake --preset <preset>` + `cmake --build --preset <preset>` instructions for Linux and Windows.
- Integrate `ctest --preset` or explicit `ctest -C Debug` for test invocation.
- Document `clang-tidy`/`cppcheck` hooks triggered via `SUPER_VOICE_ENABLE_LINT` option.
- Supply `scripts/ci/build_and_test.sh` for developers to run the CI pipeline locally.
