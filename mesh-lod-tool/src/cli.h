#ifndef MLOD_CLI_H
#define MLOD_CLI_H

#include "exit_code.h"

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace mlod {

// NATIVE-ADAPTER STATE (architecture section 7.9 / 7.8 "mesh-lod-native-adapter").
// ConversionOptions is CLI-shaped host state: it combines the host-independent
// canonical settings (see ConversionSettings in conversion_types.h) with
// filesystem-only concerns -- input/output paths, a stats-JSON path, and
// validate-only -- that exist only because the native adapter talks to a
// filesystem. It is not the canonical schema: `toConversionSettings` below
// maps the output-affecting subset into the portable contract. New
// output-affecting knobs are added to ConversionSettings first and mapped
// here, never defined only on this native-only struct.
//
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

// Included after ConversionOptions is fully defined above: conversion_types.h
// only depends on exit_code.h (via diagnostics.h) and never back on cli.h, so
// this stays a one-directional native-adapter -> core-contracts include.
#include "conversion_types.h"

namespace mlod {

// Maps native selection flags (--mesh/--primitive) into the host-independent
// PrimitiveSelection contract (conversion_types.h).
inline PrimitiveSelection toPrimitiveSelection(const ConversionOptions& options) {
    if (options.hasPrimitive) {
        return PrimitiveSelection::singlePrimitive(options.meshIndex, options.primitiveIndex);
    }
    if (options.hasMesh) {
        return PrimitiveSelection::wholeMesh(options.meshIndex);
    }
    return PrimitiveSelection::allPrimitives();
}

// Maps native ConversionOptions into the host-independent, output-affecting
// ConversionSettings contract (conversion_types.h). Native-only fields --
// input/output/stats paths and validateOnly -- have no counterpart in
// ConversionSettings and are dropped by this mapping; they never affect
// canonicalConversionSettings/canonicalConversionOptions.
inline ConversionSettings toConversionSettings(const ConversionOptions& options) {
    ConversionSettings settings;
    settings.selection = toPrimitiveSelection(options);
    settings.meshletMaxVertices = options.meshletMaxVertices;
    settings.meshletMinTriangles = options.meshletMinTriangles;
    settings.meshletMaxTriangles = options.meshletMaxTriangles;
    settings.partitionSize = options.partitionSize;
    settings.simplifyRatio = options.simplifyRatio;
    settings.simplifyThreshold = options.simplifyThreshold;
    settings.pageMinKiB = options.pageMinKiB;
    settings.pageTargetKiB = options.pageTargetKiB;
    settings.pageMaxKiB = options.pageMaxKiB;
    return settings;
}

} // namespace mlod

#endif // MLOD_CLI_H
