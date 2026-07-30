#ifndef MLOD_NATIVE_FILESYSTEM_H
#define MLOD_NATIVE_FILESYSTEM_H

#include "cli.h"
#include "conversion_types.h"
#include "input.h"

#include <array>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace mlod {

// Builds an InputBundle by reading `path` into memory and installing a
// resolver that reads sibling files relative to `path`'s parent directory --
// the exact local-resource behavior the native CLI has always had (previously
// implemented via cgltf_load_buffers/cgltf_load_buffer_file). The path itself
// is stored exactly as supplied and never absolutized or otherwise rewritten.
// Returns kExitSuccess, or kExitIo with a diagnostic on err.
int loadInputBundleFromNativePath(const std::string& path, InputBundle& bundle, std::ostream& err);

// Native-adapter convenience wrapper (moved here in task 11.2 so the
// host-independent mesh-lod-converter-core never depends on native
// filesystem code): loads `options.inputPath` from the native filesystem into
// an InputBundle, maps options.hasMesh/hasPrimitive to a PrimitiveSelection,
// and delegates to loadSourcePrimitivesFromBundle (input.h). Behaviorally
// identical to the tool's original direct-filesystem ingestion. Returns
// kExitSuccess, or an ExitCode (2 selection, 3 I/O, 4 malformed, 5
// unsupported).
int loadSourcePrimitives(const ConversionOptions& options, std::vector<SourcePrimitive>& out, std::ostream& err,
                         std::array<std::uint8_t, 32>* sourceDigest = nullptr);

// Native publication: derives filesystem output paths from
// `result.outputs`' source mesh/primitive identities (a single output keeps
// `options.outputPath` exactly; multiple outputs each get a deterministic
// `.meshNNN.primNNN` sibling name -- architecture section 7.4), writes
// `options.statsJsonPath` with the shared core's canonical metadata JSON when
// requested, prints a human-readable summary to `out`, and -- unless
// `options.validateOnly` -- stages every `.mlod` as a sibling `.tmp` file
// before renaming all of them into place, so a write/publish failure never
// leaves a successful-looking partial output set. Returns kExitSuccess,
// kExitIo (stats write failure), or kExitWrite (staging/rename failure).
int publishConversionResult(const ConversionOptions& options, const ConversionResult& result, std::ostream& out,
                            std::ostream& err);

} // namespace mlod

#endif // MLOD_NATIVE_FILESYSTEM_H
