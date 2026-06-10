cmake_minimum_required(VERSION 3.22)

project(ft_minecraft LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FT_MINECRAFT_FETCH_DEPS "Fetch third-party dependencies into the build tree" ON)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

include(FetchContent)

find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

if(FT_MINECRAFT_FETCH_DEPS)
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

  add_library(ft_minecraft_fastnoise INTERFACE)
  target_include_directories(
    ft_minecraft_fastnoise
    INTERFACE
      ${fastnoiselite_SOURCE_DIR}/Cpp
  )

  # ── miniaudio (single-header audio engine) ─────────────────────────────────
  FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.22
    GIT_SHALLOW TRUE
  )
  FetchContent_GetProperties(miniaudio)
  if(NOT miniaudio_POPULATED)
    FetchContent_Populate(miniaudio)
  endif()

  # ── stb (provides stb_vorbis.c for OGG decoding) ───────────────────────────
  FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
  )
  FetchContent_GetProperties(stb)
  if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
  endif()

else()
  find_package(glfw3 REQUIRED)
  find_package(glm CONFIG REQUIRED)
  message(FATAL_ERROR "FT_MINECRAFT_FETCH_DEPS=OFF is not fully wired yet. Reconfigure with -DFT_MINECRAFT_FETCH_DEPS=ON.")
endif()

add_executable(
  ft_minecraft
  srcs/main.cpp
  srcs/player/input_handler.cpp
  srcs/player/camera.cpp
  srcs/player/player.cpp
  srcs/streaming/chunk_manager.cpp
  srcs/world/noise.cpp
  srcs/world/terrain_gen.cpp
  srcs/world/mesh_builder.cpp
  srcs/world/world.cpp
  srcs/world/inventory.cpp
  srcs/world/recipe.cpp
  srcs/core/engine.cpp
  srcs/renderer/shader.cpp
  srcs/renderer/frustum.cpp
  srcs/renderer/texture_atlas.cpp
  srcs/renderer/skybox.cpp
  srcs/renderer/cloud.cpp
  srcs/renderer/title_screen.cpp
  srcs/renderer/minimap.cpp
  srcs/renderer/renderer.cpp
  srcs/network/tcp_socket.cpp
  srcs/network/server.cpp
  srcs/network/client.cpp
  srcs/mob/mob_manager.cpp
  srcs/mob/arrow_manager.cpp
  # ── Audio system ──────────────────────────────────────────────────────────
  srcs/audio/audio_impl.cpp
  srcs/audio/audio_manager.cpp
  srcs/audio/biome_audio_system.cpp
  srcs/audio/sound_emitter.cpp
)

target_include_directories(
  ft_minecraft
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/srcs
    ${glfw_SOURCE_DIR}/deps
    ${miniaudio_SOURCE_DIR}
    ${stb_SOURCE_DIR}
)

target_compile_definitions(ft_minecraft PRIVATE GLFW_INCLUDE_NONE)

target_link_libraries(
  ft_minecraft
  PRIVATE
    OpenGL::GL
    glfw
    glm::glm
    ft_minecraft_fastnoise
    Threads::Threads
    ${CMAKE_DL_LIBS}  # miniaudio uses dlopen on Linux for audio backends
    m                 # math library (miniaudio, stb_vorbis)
)

set_target_properties(
  ft_minecraft
  PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}
)
