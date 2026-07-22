#ifndef MLOD_VERSION_H
#define MLOD_VERSION_H

// Single header of build/version provenance consumed by the CLI and, in later
// tasks, by the .mlod metadata writer. Values originate from the CMake build
// (see mesh-lod-tool/CMakeLists.txt and cmake/Dependencies.cmake). The fallbacks
// only apply to out-of-build tooling (e.g. an IDE indexer); a real build always
// supplies the definitions and the smoke tests assert the pinned revisions.

#ifndef MLOD_TOOL_VERSION
#define MLOD_TOOL_VERSION "0.0.0-unconfigured"
#endif

#ifndef MLOD_FORMAT_VERSION_MAJOR
#define MLOD_FORMAT_VERSION_MAJOR 1
#endif

#ifndef MLOD_FORMAT_VERSION_MINOR
#define MLOD_FORMAT_VERSION_MINOR 0
#endif

#ifndef MLOD_MESHOPTIMIZER_REV
#define MLOD_MESHOPTIMIZER_REV "unconfigured"
#endif

#ifndef MLOD_CGLTF_REV
#define MLOD_CGLTF_REV "unconfigured"
#endif

#ifndef MLOD_COMPILER_TARGET
#define MLOD_COMPILER_TARGET "unconfigured"
#endif

namespace mlod {

inline constexpr const char* kToolVersion = MLOD_TOOL_VERSION;
inline constexpr int kFormatVersionMajor = MLOD_FORMAT_VERSION_MAJOR;
inline constexpr int kFormatVersionMinor = MLOD_FORMAT_VERSION_MINOR;
inline constexpr const char* kMeshoptimizerRev = MLOD_MESHOPTIMIZER_REV;
inline constexpr const char* kCgltfRev = MLOD_CGLTF_REV;
inline constexpr const char* kCompilerTarget = MLOD_COMPILER_TARGET;

} // namespace mlod

#endif // MLOD_VERSION_H
