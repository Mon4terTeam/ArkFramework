# Builds with the Clang bundled in Zig, targeting x86_64-windows-gnu.
#   cmake -S . -B build-cmake -G Ninja --toolchain cmake/zig-toolchain.cmake
find_program(ZIG_EXECUTABLE zig REQUIRED HINTS "${CMAKE_CURRENT_LIST_DIR}/../.tools/zig/ziglang")

set(CMAKE_C_COMPILER "${ZIG_EXECUTABLE}")
set(CMAKE_C_COMPILER_ARG1 "cc -target x86_64-windows-gnu")
set(CMAKE_CXX_COMPILER "${ZIG_EXECUTABLE}")
set(CMAKE_CXX_COMPILER_ARG1 "c++ -target x86_64-windows-gnu")

# Archive rules must replace the platform defaults, which run after this file.
set(CMAKE_USER_MAKE_RULES_OVERRIDE "${CMAKE_CURRENT_LIST_DIR}/zig-rules.cmake")
