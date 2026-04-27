cmake_minimum_required(VERSION 3.20)
project(ft_vox LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    add_compile_definitions(GL_SILENCE_DEPRECATION)
endif()

# ── OpenGL (must come before target_link_libraries) ──────────────────────────
find_package(OpenGL REQUIRED)

include(FetchContent)

# ── GLFW ─────────────────────────────────────────────────────────────────────
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    ON)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# ── GLAD v2 ──────────────────────────────────────────────────────────────────
# glad2 generates at configure time — no Python required
FetchContent_Declare(glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG        v2.0.8
    GIT_SHALLOW    ON)
FetchContent_GetProperties(glad)
if(NOT glad_POPULATED)
    FetchContent_Populate(glad)
    # glad2 cmake generator lives in <source>/cmake/
    add_subdirectory("${glad_SOURCE_DIR}/cmake" glad_cmake)
    glad_add_library(glad_gl STATIC REPRODUCIBLE
        LANGUAGE C
        API      gl:core=4.1)
endif()

# ── GLM ──────────────────────────────────────────────────────────────────────
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(glm)

# ── stb (single header) ──────────────────────────────────────────────────────
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(stb)

# ── FastNoiseLite ─────────────────────────────────────────────────────────────
FetchContent_Declare(fastnoiselite
    GIT_REPOSITORY https://github.com/Auburn/FastNoiseLite.git
    GIT_TAG        master
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(fastnoiselite)

# ── Sources ───────────────────────────────────────────────────────────────────
file(GLOB_RECURSE SOURCES
    "${CMAKE_SOURCE_DIR}/srcs/*.cpp"
    "${CMAKE_SOURCE_DIR}/srcs/*.c")

add_executable(ft_vox ${SOURCES})

target_include_directories(ft_vox PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/srcs"     # allows #include "renderer/shader.hpp" etc.
    "${stb_SOURCE_DIR}"
    "${fastnoiselite_SOURCE_DIR}/Cpp")

# ── Link ─────────────────────────────────────────────────────────────────────
target_link_libraries(ft_vox PRIVATE glfw glad_gl glm::glm)

if(APPLE)
    # macOS: frameworks only (OpenGL::GL == -framework OpenGL; avoid duplicate)
    target_link_libraries(ft_vox PRIVATE
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo")
elseif(UNIX)
    target_link_libraries(ft_vox PRIVATE OpenGL::GL dl pthread)
elseif(WIN32)
    target_link_libraries(ft_vox PRIVATE OpenGL::GL)
endif()

# ── Assets: copy to build output dir ─────────────────────────────────────────
add_custom_command(TARGET ft_vox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:ft_vox>/assets"
    COMMENT "Copying assets")

# ── Warnings ─────────────────────────────────────────────────────────────────
target_compile_options(ft_vox PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wno-unused-parameter>)
