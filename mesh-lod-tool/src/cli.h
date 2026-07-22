#ifndef MLOD_CLI_H
#define MLOD_CLI_H

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace mlod {

// Process exit codes shared by the CLI and later conversion stages
// (architecture section 7.7). Keep this the single definition so diagnostics
// map failure classes to stable codes.
enum ExitCode : int {
    kExitSuccess = 0,
    kExitCli = 2,         // CLI argument error
    kExitIo = 3,          // input/output I/O error
    kExitMalformed = 4,   // malformed glTF/GLB/accessor
    kExitUnsupported = 5, // unsupported source feature/layout/material
    kExitHierarchy = 6,   // hierarchy generation failure
    kExitValidation = 7,  // output validation/integrity failure
    kExitWrite = 8,       // final write/rename failure
};

// Fully resolved conversion request. All conversion-affecting knobs carry their
// architecture-defined defaults; file paths are stored exactly as supplied and
// are never absolutized or otherwise rewritten.
struct ConversionOptions {
    std::string inputPath;
    std::string outputPath;

    bool hasMesh = false;
    std::uint32_t meshIndex = 0;
    bool hasPrimitive = false;
    std::uint32_t primitiveIndex = 0;

    std::uint32_t meshletMaxVertices = 64;
    std::uint32_t meshletMinTriangles = 40;
    std::uint32_t meshletMaxTriangles = 124;
    std::uint32_t partitionSize = 8;
    float simplifyRatio = 0.5f;
    float simplifyThreshold = 0.85f;

    std::uint32_t pageMinKiB = 64;
    std::uint32_t pageTargetKiB = 128;
    std::uint32_t pageMaxKiB = 256;

    std::string statsJsonPath; // empty means "not requested"
    bool validateOnly = false;
};

// Parses convert-mode arguments (help/version are handled by runCli). On success
// returns kExitSuccess and populates options; on any argument error returns
// kExitCli and writes a diagnostic identifying the offending option to err.
int parseConversionOptions(const std::vector<std::string>& args, ConversionOptions& options,
                           std::ostream& err);

// Serializes the output-affecting conversion knobs into a deterministic,
// locale-independent, path-free string with lexicographically ordered keys.
// Used later for provenance and reproducibility hashing.
std::string canonicalConversionOptions(const ConversionOptions& options);

// Runs the full conversion pipeline for already-validated options: load and hash
// the source, then for every selected primitive normalize, build the hierarchy,
// pack pages, and write + independently validate the container in memory. Emits
// statistics and, unless --validate-only, atomically publishes all outputs so
// nothing is written unless every selected primitive validates. Returns an
// ExitCode.
int runConversion(const ConversionOptions& options, std::ostream& out, std::ostream& err);

// Full command-line entry: resolves --help/--version, parses and validates
// convert-mode options, and dispatches conversion. Returns an ExitCode.
int runCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

} // namespace mlod

#endif // MLOD_CLI_H
