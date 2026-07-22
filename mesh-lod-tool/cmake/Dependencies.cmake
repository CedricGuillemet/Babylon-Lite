include(FetchContent)

# Immutable full-commit pins (REQ-TOOL-2). These are the single source of truth
# for both the FetchContent revisions and the provenance reported by --version.
set(MLOD_MESHOPTIMIZER_REV "f843aae0b3070306bd2aeef43ffcf09509fee526"
    CACHE STRING "Pinned meshoptimizer commit" FORCE)
set(MLOD_CGLTF_REV "85cd62382dfea638278962690cf515023f33ed00"
    CACHE STRING "Pinned cgltf commit" FORCE)

# Allow the pinned upstream projects, which predate CMake 4's removal of
# pre-3.5 compatibility, to configure under a modern CMake.
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

FetchContent_Declare(meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG ${MLOD_MESHOPTIMIZER_REV}
    GIT_SHALLOW FALSE)

# cgltf is a single-header library. Point SOURCE_SUBDIR at a directory with no
# CMakeLists.txt so FetchContent populates the source without adding cgltf's own
# CMake project (which would build its unrelated test targets).
FetchContent_Declare(cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG ${MLOD_CGLTF_REV}
    GIT_SHALLOW FALSE
    SOURCE_SUBDIR do-not-configure)

FetchContent_MakeAvailable(meshoptimizer cgltf)

# clusterlod.h ships in meshoptimizer's demo/ directory. Expose it through our
# own interface target rather than mutating meshoptimizer's exported target
# (whose install/export rules reject raw build-tree include paths). The
# BUILD_INTERFACE guard keeps the build-tree path out of any export.
add_library(mlod_clusterlod INTERFACE)
target_include_directories(mlod_clusterlod INTERFACE
    "$<BUILD_INTERFACE:${meshoptimizer_SOURCE_DIR}/demo>")
target_link_libraries(mlod_clusterlod INTERFACE meshoptimizer)

add_library(mlod_cgltf INTERFACE)
target_include_directories(mlod_cgltf INTERFACE
    "$<BUILD_INTERFACE:${cgltf_SOURCE_DIR}>")
