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

# ---------------------------------------------------------------------------
# mesh-lod-converter-core: the canonical converter implementation. It owns
# memory input, normalization, hierarchy generation, page packing,
# serialization, validation, hashing, statistics, and structured diagnostics.
# ---------------------------------------------------------------------------
add_library(mesh-lod-converter-core STATIC
    src/input.cpp
    src/diagnostics.cpp
    src/conversion.cpp
    src/normalize.cpp
    src/hierarchy.cpp
    src/crc32c.cpp
    src/sha256.cpp
    src/page_packer.cpp
    src/mlod_writer.cpp
    src/validator.cpp
    src/statistics.cpp)
target_include_directories(mesh-lod-converter-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(mesh-lod-converter-core PUBLIC
    MLOD_TOOL_VERSION="${PROJECT_VERSION}"
    MLOD_FORMAT_VERSION_MAJOR=1
    MLOD_FORMAT_VERSION_MINOR=0
    MLOD_MESHOPTIMIZER_REV="${MLOD_MESHOPTIMIZER_REV}"
    MLOD_CGLTF_REV="${MLOD_CGLTF_REV}"
    MLOD_COMPILER_TARGET="${MLOD_COMPILER_TARGET}")
target_link_libraries(mesh-lod-converter-core PUBLIC mlod_clusterlod mlod_cgltf)
mlod_set_project_warnings(mesh-lod-converter-core)
mlod_set_deterministic_floating_point(mesh-lod-converter-core)

# mesh-lod-native-adapter: CLI parsing, native filesystem resolution, text
# formatting, temporary-file publication, and process exit-code mapping.
add_library(mesh-lod-native-adapter STATIC
    src/cli.cpp
    src/native_filesystem.cpp)
target_include_directories(mesh-lod-native-adapter
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(mesh-lod-native-adapter PUBLIC mesh-lod-converter-core)
mlod_set_project_warnings(mesh-lod-native-adapter)
mlod_set_deterministic_floating_point(mesh-lod-native-adapter)

add_executable(mesh-lod-tool src/main.cpp)
target_link_libraries(mesh-lod-tool PRIVATE mesh-lod-native-adapter)
mlod_set_project_warnings(mesh-lod-tool)
mlod_set_deterministic_floating_point(mesh-lod-tool)

enable_testing()
add_subdirectory(tests)
