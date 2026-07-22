#ifndef MLOD_INPUT_H
#define MLOD_INPUT_H

#include "cli.h"
#include "normalize.h"

#include <array>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace mlod {

// Identifies a source glTF primitive by its zero-based mesh and primitive index
// in document order. Used both for selection and deterministic output naming.
struct SelectedPrimitive {
    std::uint32_t meshIndex = 0;
    std::uint32_t primitiveIndex = 0;
};

// Parses and validates a glTF/GLB document with cgltf, selects primitives
// according to options (a specific primitive, a whole mesh, or every supported
// primitive), and reads each selected primitive into a SourcePrimitive with its
// material facts. Rejects every unsupported source feature/layout with an
// explicit diagnostic that includes mesh/primitive/accessor/extension context.
// When sourceDigest is non-null it receives the length-prefixed SHA-256 over the
// input file bytes plus any external geometry buffers. Returns kExitSuccess, or
// an ExitCode (2 selection, 3 I/O, 4 malformed, 5 unsupported).
int loadSourcePrimitives(const ConversionOptions& options, std::vector<SourcePrimitive>& out,
                         std::ostream& err, std::array<std::uint8_t, 32>* sourceDigest = nullptr);

// Derives the sibling output path for a single primitive by inserting
// ".meshNNN.primNNN" (three-digit, zero-padded source indices) before the
// output file's extension. Path separators and any directory prefix are
// preserved; the input path is never absolutized.
//   "statue.mlod", mesh 0, prim 0 -> "statue.mesh000.prim000.mlod"
//   "out/statue.mlod", mesh 1, prim 2 -> "out/statue.mesh001.prim002.mlod"
std::string derivePrimitiveOutputPath(const std::string& baseOutput, std::uint32_t meshIndex,
                                      std::uint32_t primitiveIndex);

// Maps a set of selected primitives to output paths in source order. A single
// selected primitive writes exactly baseOutput; multiple selected primitives
// each receive a deterministic sibling name (architecture section 7.4).
std::vector<std::string> deriveOutputPaths(const std::string& baseOutput,
                                           const std::vector<SelectedPrimitive>& selection);

} // namespace mlod

#endif // MLOD_INPUT_H
