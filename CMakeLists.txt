cmake_minimum_required(VERSION 3.24)

project(ft_vox LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FT_VOX_FETCH_DEPS "Fetch third-party dependencies into the build tree" ON)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

include(FetchContent)

find_package(OpenGL REQUIRED)

if(FT_VOX_FETCH_DEPS)
  FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
  )

  FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
  )

  FetchContent_Declare(
    fastnoiselite
    GIT_REPOSITORY https://github.com/Auburn/FastNoiseLite.git
    GIT_TAG v1.1.1
    GIT_SHALLOW TRUE
  )

  FetchContent_MakeAvailable(glfw glm)

  FetchContent_GetProperties(fastnoiselite)
  if(NOT fastnoiselite_POPULATED)
    FetchContent_Populate(fastnoiselite)
  endif()

  add_library(ft_vox_fastnoise INTERFACE)
  target_include_directories(
    ft_vox_fastnoise
    INTERFACE
      ${fastnoiselite_SOURCE_DIR}/Cpp
  )
else()
  find_package(glfw3 REQUIRED)
  find_package(glm CONFIG REQUIRED)
  message(FATAL_ERROR "FT_VOX_FETCH_DEPS=OFF is not fully wired yet. Reconfigure with -DFT_VOX_FETCH_DEPS=ON.")
endif()

add_executable(
  ft_vox
  srcs/main.cpp
  srcs/player/input_handler.cpp
  srcs/player/camera.cpp
  srcs/player/player.cpp
  srcs/streaming/chunk_manager.cpp
  srcs/world/noise.cpp
  srcs/world/terrain_gen.cpp
  srcs/world/mesh_builder.cpp
  srcs/world/world.cpp
  srcs/core/engine.cpp
  srcs/renderer/shader.cpp
  srcs/renderer/frustum.cpp
  srcs/renderer/texture_atlas.cpp
  srcs/renderer/skybox.cpp
  srcs/renderer/renderer.cpp
)

target_include_directories(
  ft_vox
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/srcs
    ${glfw_SOURCE_DIR}/deps
)

target_compile_definitions(ft_vox PRIVATE GLFW_INCLUDE_NONE)

target_link_libraries(
  ft_vox
  PRIVATE
    OpenGL::GL
    glfw
    glm::glm
    ft_vox_fastnoise
)

set_target_properties(
  ft_vox
  PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}
)
