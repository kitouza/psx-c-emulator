cmake_minimum_required(VERSION 3.24)

project(psx_emulator LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(FetchContent)

# Keep SDL local to this build and pin it so builds remain reproducible.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.10
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(SDL3)

find_package(OpenGL REQUIRED)

# Keep shader source as normal GLSL files and copy them beside the executable.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vertex.glsl"
    "${CMAKE_CURRENT_BINARY_DIR}/shaders/vertex.glsl"
    COPYONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/fragment.glsl"
    "${CMAKE_CURRENT_BINARY_DIR}/shaders/fragment.glsl"
    COPYONLY
)

add_executable(psx-emulator
    src/bios.c
    src/cpu.c
    src/dma.c
    src/gpu.c
    src/instruction.c
    src/interconnect.c
    src/main.c
    src/ram.c
    src/renderer.c
)

target_compile_options(psx-emulator PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(psx-emulator PRIVATE
    SDL3::SDL3-static
    OpenGL::GL
)
