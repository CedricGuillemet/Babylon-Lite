cmake_minimum_required(VERSION 3.24)

project(mesh-lod-tool
    VERSION 0.1.0
    DESCRIPTION "MeshLoD offline converter: glTF/GLB -> .mlod"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Single-config generators default to a Release build so the documented
# workflow produces an optimized converter without an explicit build type.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerWarnings)
include(Dependencies)

# Deterministic build provenance surfaced through --version and, later, the
# generated .mlod metadata. Only compiler identity is recorded; no absolute
# path, timestamp, or locale-sensitive value enters this string.
set(MLOD_COMPILER_TARGET
    "${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")

add_library(mesh-lod-tool-core STATIC
    src/cli.cpp
    src/input.cpp
    src/normalize.cpp)
target_include_directories(mesh-lod-tool-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(mesh-lod-tool-core PUBLIC
    MLOD_TOOL_VERSION="${PROJECT_VERSION}"
    MLOD_FORMAT_VERSION_MAJOR=1
    MLOD_FORMAT_VERSION_MINOR=0
    MLOD_MESHOPTIMIZER_REV="${MLOD_MESHOPTIMIZER_REV}"
    MLOD_CGLTF_REV="${MLOD_CGLTF_REV}"
    MLOD_COMPILER_TARGET="${MLOD_COMPILER_TARGET}")
# meshoptimizer (via mlod_clusterlod) and cgltf are linked now to prove the
# pinned dependencies build and are reachable. Their implementation macros are
# intentionally left undefined until later tasks consume them.
target_link_libraries(mesh-lod-tool-core PUBLIC mlod_clusterlod mlod_cgltf)
mlod_set_project_warnings(mesh-lod-tool-core)

add_executable(mesh-lod-tool src/main.cpp)
target_link_libraries(mesh-lod-tool PRIVATE mesh-lod-tool-core)
mlod_set_project_warnings(mesh-lod-tool)

enable_testing()
add_subdirectory(tests)
