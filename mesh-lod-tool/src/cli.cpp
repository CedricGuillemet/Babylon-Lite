#include "cli.h"

#include "mlod_version.h"

#include <ostream>
#include <string>
#include <vector>

namespace mlod {
namespace {

void printHelp(std::ostream& out) {
    out << "mesh-lod-tool - MeshLoD offline converter (glTF/GLB -> .mlod)\n"
        << "\n"
        << "Usage:\n"
        << "  mesh-lod-tool --input <path> --output <path> [options]\n"
        << "  mesh-lod-tool --version\n"
        << "  mesh-lod-tool --help\n"
        << "\n"
        << "Required:\n"
        << "  --input <path>                    Source .gltf or .glb file\n"
        << "  --output <path>                   Destination .mlod file\n"
        << "\n"
        << "Primitive selection:\n"
        << "  --mesh <index>                    Zero-based mesh index\n"
        << "  --primitive <index>               Zero-based primitive index (requires --mesh)\n"
        << "\n"
        << "Meshlet and hierarchy options:\n"
        << "  --meshlet-max-vertices <4..256>   Default 64\n"
        << "  --meshlet-min-triangles <4..256>  Default 40\n"
        << "  --meshlet-max-triangles <4..256>  Default 124\n"
        << "  --partition-size <2..32>          Default 8\n"
        << "  --simplify-ratio <0..1>           Default 0.5\n"
        << "  --simplify-threshold <0..1>       Default 0.85\n"
        << "\n"
        << "Page options (KiB, multiples of 64):\n"
        << "  --page-min-kib <64..256>          Default 64\n"
        << "  --page-target-kib <64..256>       Default 128\n"
        << "  --page-max-kib <64..256>          Default 256\n"
        << "\n"
        << "Other:\n"
        << "  --stats-json <path>               Write canonical statistics JSON\n"
        << "  --validate-only                   Validate without writing output\n"
        << "  --help                            Show this help and exit\n"
        << "  --version                         Show version provenance and exit\n";
}

void printVersion(std::ostream& out) {
    out << "tool_version=" << kToolVersion << "\n"
        << "format_version=" << kFormatVersionMajor << "." << kFormatVersionMinor << "\n"
        << "meshoptimizer_revision=" << kMeshoptimizerRev << "\n"
        << "cgltf_revision=" << kCgltfRev << "\n"
        << "compiler_target=" << kCompilerTarget << "\n";
}

bool isKnownOption(const std::string& arg) {
    static constexpr const char* kKnownOptions[] = {
        "--input",
        "--output",
        "--mesh",
        "--primitive",
        "--meshlet-max-vertices",
        "--meshlet-min-triangles",
        "--meshlet-max-triangles",
        "--partition-size",
        "--simplify-ratio",
        "--simplify-threshold",
        "--page-min-kib",
        "--page-target-kib",
        "--page-max-kib",
        "--stats-json",
        "--validate-only",
        "--help",
        "--version",
    };
    for (const char* option : kKnownOptions) {
        if (arg == option) {
            return true;
        }
    }
    return false;
}

} // namespace

int runCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    // --help and --version are handled first and independently of position so
    // they work without input files (REQ-TOOL-5, REQ-TOOL-6).
    for (const std::string& arg : args) {
        if (arg == "--help") {
            printHelp(out);
            return kExitSuccess;
        }
        if (arg == "--version") {
            printVersion(out);
            return kExitSuccess;
        }
    }

    if (args.empty()) {
        err << "error: no arguments provided; use --help for usage\n";
        return kExitCli;
    }

    // Scaffold stage: reject unknown options up front. Full argument parsing,
    // primitive selection, and conversion are implemented by later tasks.
    for (const std::string& arg : args) {
        if (arg.size() >= 2 && arg[0] == '-' && arg[1] == '-' && !isKnownOption(arg)) {
            err << "error: unknown argument '" << arg << "'\n";
            return kExitCli;
        }
    }

    err << "error: conversion is not implemented in this build\n";
    return kExitCli;
}

} // namespace mlod
