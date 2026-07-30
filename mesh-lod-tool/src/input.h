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

// Host-independent core ingestion (architecture sections 7.9, 7.11). Parses
// `bundle.entryBytes` as a glTF/GLB document in memory: GLB BIN chunks and
// glTF data URIs are resolved internally without host I/O; every other
// externally referenced buffer or image is materialized through
// `bundle.resolver`. Applies `selection`, then reads every selected
// primitive's supported geometry/material facts, rejecting every unsupported
// source feature/layout with a diagnostic identifying the offending
// mesh/primitive/accessor/extension. Every referenced image is validated for
// presence (but its bytes are never copied into geometry or the digest). When
// sourceDigest is non-null it receives the length-prefixed SHA-256 over the
// entry bytes followed by each referenced external geometry buffer in glTF
// buffer-index order. Returns kExitSuccess, or an ExitCode (2 selection, 3
// I/O, 4 malformed, 5 unsupported).
int loadSourcePrimitivesFromBundle(const InputBundle& bundle, const PrimitiveSelection& selection,
                                   std::vector<SourcePrimitive>& out, std::ostream& err,
                                   std::array<std::uint8_t, 32>* sourceDigest = nullptr);

// Per-document primitive inventory produced by inspection.
// `identity`/`supported`/counts mirror InspectedPrimitive (conversion_types.h);
// this module fills them in for every document primitive, independent of any
// requested selection.
struct DocumentInventory {
    std::string entryType; // "glb" or "gltf", detected from the entry bytes
    std::vector<std::string> resolvedResourcePaths; // external buffer/image URIs, glTF order
    std::vector<InspectedPrimitive> primitives;      // every document primitive, mesh/primitive order
    std::vector<std::uint32_t> meshPrimitiveCounts;  // primitives_count per mesh, for selection-range checks
    std::uint64_t copiedResourceBytes = 0; // B: external buffer + decoded data-URI bytes (excludes GLB BIN)
    std::uint64_t selectedBytes = 0;       // F: entry bytes + every externally resolved buffer/image's bytes
    std::vector<Diagnostic> warnings;      // one per unsupported primitive
};

// Parses `bundle.entryBytes`, resolves every buffer/image exactly as
// loadSourcePrimitivesFromBundle does, and inventories every document
// primitive's support and counts by reusing the very same per-primitive
// validation (readPrimitive) that conversion uses -- no independent copy of
// the rejection rules -- without building a hierarchy or normalized geometry
// for any primitive. Polls
// `cancel` once per referenced buffer/image resolution and once per
// mesh/primitive. Returns kExitSuccess even when individual primitives are
// unsupported (see `out.primitives[].supported`, each with a warning
// Diagnostic appended to `out.warnings`); returns an ExitCode only for a
// fatal document/resource failure (parse, resource resolution, or
// required-extension failure).
int inspectDocument(const InputBundle& bundle, DocumentInventory& out, const DiagnosticSink& diagnostics,
                   const CancellationProbe& cancel);

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
