#ifndef MLOD_INPUT_H
#define MLOD_INPUT_H

#include <cstdint>
#include <string>
#include <vector>

namespace mlod {

// Identifies a source glTF primitive by its zero-based mesh and primitive index
// in document order. Used both for selection and deterministic output naming.
struct SelectedPrimitive {
    std::uint32_t meshIndex = 0;
    std::uint32_t primitiveIndex = 0;
};

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
