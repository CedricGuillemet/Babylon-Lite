#include "cli.h"

#include "hierarchy.h"
#include "input.h"
#include "mlod_version.h"
#include "mlod_writer.h"
#include "normalize.h"
#include "page_packer.h"
#include "statistics.h"
#include "validator.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <set>
#include <string>
#include <system_error>
#include <utility>
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
        << "  --version                         Show version provenance and exit\n"
        << "\n"
        << "Constraints: --primitive requires --mesh; page sizes must be multiples of\n"
        << "64 KiB with --page-min-kib <= --page-target-kib <= --page-max-kib.\n";
}

void printVersion(std::ostream& out) {
    out << "tool_version=" << kToolVersion << "\n"
        << "format_version=" << kFormatVersionMajor << "." << kFormatVersionMinor << "\n"
        << "meshoptimizer_revision=" << kMeshoptimizerRev << "\n"
        << "cgltf_revision=" << kCgltfRev << "\n"
        << "compiler_target=" << kCompilerTarget << "\n";
}

// Locale-independent unsigned integer parse. Requires the whole token to be
// consumed and rejects signs, whitespace, and non-digits.
bool parseUint(const std::string& text, std::uint32_t& value) {
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    std::uint32_t parsed = 0;
    const auto result = std::from_chars(begin, end, parsed, 10);
    if (result.ec != std::errc() || result.ptr != end) {
        return false;
    }
    value = parsed;
    return true;
}

// Locale-independent finite float parse. Requires the whole token to be consumed
// and rejects NaN and infinities.
bool parseFloat(const std::string& text, float& value) {
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    float parsed = 0.0f;
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc() || result.ptr != end) {
        return false;
    }
    if (!std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

void appendCanonicalFloat(std::string& target, float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.6g", static_cast<double>(value));
    target += buffer;
}

void appendCanonicalUint(std::string& target, std::uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%u", value);
    target += buffer;
}

} // namespace

int parseConversionOptions(const std::vector<std::string>& args, ConversionOptions& options,
                           std::ostream& err) {
    ConversionOptions parsed;
    std::set<std::string> seen;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];

        // Every option and flag is single-use; reject repeats up front.
        if (arg.rfind("--", 0) == 0) {
            if (!seen.insert(arg).second) {
                err << "error: duplicate option '" << arg << "'\n";
                return kExitCli;
            }
        }

        auto takeValue = [&](std::string& value) -> bool {
            if (i + 1 >= args.size()) {
                err << "error: option '" << arg << "' requires a value\n";
                return false;
            }
            value = args[++i];
            return true;
        };

        auto takeUint = [&](std::uint32_t& value, std::uint32_t low, std::uint32_t high) -> bool {
            std::string token;
            if (!takeValue(token)) {
                return false;
            }
            if (!parseUint(token, value) || value < low || value > high) {
                err << "error: option '" << arg << "' expects an integer in [" << low << ", "
                    << high << "]\n";
                return false;
            }
            return true;
        };

        auto takeFloat = [&](float& value, float low, float high) -> bool {
            std::string token;
            if (!takeValue(token)) {
                return false;
            }
            if (!parseFloat(token, value) || value < low || value > high) {
                err << "error: option '" << arg << "' expects a number in [" << low << ", " << high
                    << "]\n";
                return false;
            }
            return true;
        };

        if (arg == "--input") {
            if (!takeValue(parsed.inputPath)) {
                return kExitCli;
            }
        } else if (arg == "--output") {
            if (!takeValue(parsed.outputPath)) {
                return kExitCli;
            }
        } else if (arg == "--mesh") {
            if (!takeUint(parsed.meshIndex, 0, UINT32_MAX)) {
                return kExitCli;
            }
            parsed.hasMesh = true;
        } else if (arg == "--primitive") {
            if (!takeUint(parsed.primitiveIndex, 0, UINT32_MAX)) {
                return kExitCli;
            }
            parsed.hasPrimitive = true;
        } else if (arg == "--meshlet-max-vertices") {
            if (!takeUint(parsed.meshletMaxVertices, 4, 256)) {
                return kExitCli;
            }
        } else if (arg == "--meshlet-min-triangles") {
            if (!takeUint(parsed.meshletMinTriangles, 4, 256)) {
                return kExitCli;
            }
        } else if (arg == "--meshlet-max-triangles") {
            if (!takeUint(parsed.meshletMaxTriangles, 4, 256)) {
                return kExitCli;
            }
        } else if (arg == "--partition-size") {
            if (!takeUint(parsed.partitionSize, 2, 32)) {
                return kExitCli;
            }
        } else if (arg == "--simplify-ratio") {
            if (!takeFloat(parsed.simplifyRatio, 0.0f, 1.0f)) {
                return kExitCli;
            }
        } else if (arg == "--simplify-threshold") {
            if (!takeFloat(parsed.simplifyThreshold, 0.0f, 1.0f)) {
                return kExitCli;
            }
        } else if (arg == "--page-min-kib") {
            if (!takeUint(parsed.pageMinKiB, 64, 256)) {
                return kExitCli;
            }
        } else if (arg == "--page-target-kib") {
            if (!takeUint(parsed.pageTargetKiB, 64, 256)) {
                return kExitCli;
            }
        } else if (arg == "--page-max-kib") {
            if (!takeUint(parsed.pageMaxKiB, 64, 256)) {
                return kExitCli;
            }
        } else if (arg == "--stats-json") {
            if (!takeValue(parsed.statsJsonPath)) {
                return kExitCli;
            }
        } else if (arg == "--validate-only") {
            parsed.validateOnly = true;
        } else {
            err << "error: unknown argument '" << arg << "'\n";
            return kExitCli;
        }
    }

    if (parsed.inputPath.empty()) {
        err << "error: --input is required\n";
        return kExitCli;
    }
    if (parsed.outputPath.empty()) {
        err << "error: --output is required\n";
        return kExitCli;
    }
    if (parsed.hasPrimitive && !parsed.hasMesh) {
        err << "error: --primitive requires --mesh\n";
        return kExitCli;
    }

    // v1 requires page sizes to be whole multiples of 64 KiB.
    const std::pair<const char*, std::uint32_t> pageSizes[] = {
        {"--page-min-kib", parsed.pageMinKiB},
        {"--page-target-kib", parsed.pageTargetKiB},
        {"--page-max-kib", parsed.pageMaxKiB},
    };
    for (const auto& [name, value] : pageSizes) {
        if (value % 64 != 0) {
            err << "error: option '" << name << "' must be a multiple of 64 KiB\n";
            return kExitCli;
        }
    }
    if (!(parsed.pageMinKiB <= parsed.pageTargetKiB && parsed.pageTargetKiB <= parsed.pageMaxKiB)) {
        err << "error: page sizes must satisfy --page-min-kib <= --page-target-kib <= "
               "--page-max-kib\n";
        return kExitCli;
    }
    if (parsed.meshletMinTriangles > parsed.meshletMaxTriangles) {
        err << "error: --meshlet-min-triangles must not exceed --meshlet-max-triangles\n";
        return kExitCli;
    }

    options = parsed;
    return kExitSuccess;
}

std::string canonicalConversionOptions(const ConversionOptions& options) {
    // Path-free, lexicographically ordered, locale-independent. Captures only
    // the knobs that influence output bytes (not selection or file paths).
    std::string canonical;
    canonical += "meshlet_max_triangles=";
    appendCanonicalUint(canonical, options.meshletMaxTriangles);
    canonical += "\nmeshlet_max_vertices=";
    appendCanonicalUint(canonical, options.meshletMaxVertices);
    canonical += "\nmeshlet_min_triangles=";
    appendCanonicalUint(canonical, options.meshletMinTriangles);
    canonical += "\npage_max_kib=";
    appendCanonicalUint(canonical, options.pageMaxKiB);
    canonical += "\npage_min_kib=";
    appendCanonicalUint(canonical, options.pageMinKiB);
    canonical += "\npage_target_kib=";
    appendCanonicalUint(canonical, options.pageTargetKiB);
    canonical += "\npartition_size=";
    appendCanonicalUint(canonical, options.partitionSize);
    canonical += "\nsimplify_ratio=";
    appendCanonicalFloat(canonical, options.simplifyRatio);
    canonical += "\nsimplify_threshold=";
    appendCanonicalFloat(canonical, options.simplifyThreshold);
    canonical += "\n";
    return canonical;
}

std::string derivePrimitiveOutputPath(const std::string& baseOutput, std::uint32_t meshIndex,
                                      std::uint32_t primitiveIndex) {
    const std::size_t separator = baseOutput.find_last_of("/\\");
    const std::size_t nameStart = (separator == std::string::npos) ? 0 : separator + 1;
    const std::size_t dot = baseOutput.find_last_of('.');

    std::string stem;
    std::string extension;
    if (dot != std::string::npos && dot > nameStart) {
        stem = baseOutput.substr(0, dot);
        extension = baseOutput.substr(dot);
    } else {
        stem = baseOutput;
    }

    char suffix[32];
    std::snprintf(suffix, sizeof(suffix), ".mesh%03u.prim%03u", meshIndex, primitiveIndex);
    return stem + suffix + extension;
}

std::vector<std::string> deriveOutputPaths(const std::string& baseOutput,
                                           const std::vector<SelectedPrimitive>& selection) {
    std::vector<std::string> paths;
    paths.reserve(selection.size());
    if (selection.size() == 1) {
        paths.push_back(baseOutput);
        return paths;
    }
    for (const SelectedPrimitive& primitive : selection) {
        paths.push_back(derivePrimitiveOutputPath(baseOutput, primitive.meshIndex, primitive.primitiveIndex));
    }
    return paths;
}

int runConversion(const ConversionOptions& options, std::ostream& out, std::ostream& err) {
    std::vector<SourcePrimitive> primitives;
    std::array<std::uint8_t, 32> sourceDigest{};
    int rc = loadSourcePrimitives(options, primitives, err, &sourceDigest);
    if (rc != kExitSuccess) {
        return rc;
    }

    std::vector<SelectedPrimitive> selection;
    selection.reserve(primitives.size());
    for (const SourcePrimitive& primitive : primitives) {
        selection.push_back({primitive.meshIndex, primitive.primitiveIndex});
    }
    const std::vector<std::string> outputs = deriveOutputPaths(options.outputPath, selection);

    std::vector<PrimitiveHierarchy> hierarchies(primitives.size());
    std::vector<PackedGeometry> packs(primitives.size());
    std::vector<std::vector<unsigned char>> images(primitives.size());
    for (std::size_t i = 0; i < primitives.size(); ++i) {
        NormalizedPrimitive normalized;
        rc = normalizePrimitive(primitives[i], normalized, err);
        if (rc != kExitSuccess) {
            return rc;
        }
        rc = buildHierarchy(normalized, options, hierarchies[i], err);
        if (rc != kExitSuccess) {
            return rc;
        }
        rc = packPages(hierarchies[i], normalized, options, packs[i], err);
        if (rc != kExitSuccess) {
            return rc;
        }
        rc = writeContainer(hierarchies[i], packs[i], normalized, options, sourceDigest, images[i],
                            err);
        if (rc != kExitSuccess) {
            return rc;
        }
        rc = validateContainer(images[i].data(), images[i].size(), err);
        if (rc != kExitSuccess) {
            return rc;
        }
    }

    if (!options.statsJsonPath.empty()) {
        const std::string json = buildStatisticsJson(hierarchies, packs);
        std::ofstream statsFile(options.statsJsonPath, std::ios::binary | std::ios::trunc);
        if (!statsFile) {
            err << "error: could not write statistics to " << options.statsJsonPath << "\n";
            return kExitIo;
        }
        statsFile.write(json.data(), static_cast<std::streamsize>(json.size()));
        if (!statsFile) {
            err << "error: could not write statistics to " << options.statsJsonPath << "\n";
            return kExitIo;
        }
    }
    writeStatisticsText(hierarchies, packs, out);

    if (options.validateOnly) {
        return kExitSuccess;
    }

    // Atomic publish: write every output to a sibling temporary first, then
    // rename each into place. A failure before all temporaries are written leaves
    // no final files.
    namespace fs = std::filesystem;
    std::vector<std::string> temporaries(outputs.size());
    const auto cleanup = [&]() {
        for (const std::string& temporary : temporaries) {
            if (!temporary.empty()) {
                std::error_code ec;
                fs::remove(temporary, ec);
            }
        }
    };

    for (std::size_t i = 0; i < outputs.size(); ++i) {
        temporaries[i] = outputs[i] + ".tmp";
        std::ofstream file(temporaries[i], std::ios::binary | std::ios::trunc);
        if (!file) {
            err << "error: could not create " << temporaries[i] << "\n";
            cleanup();
            return kExitWrite;
        }
        file.write(reinterpret_cast<const char*>(images[i].data()),
                   static_cast<std::streamsize>(images[i].size()));
        file.close();
        if (!file) {
            err << "error: could not write " << temporaries[i] << "\n";
            cleanup();
            return kExitWrite;
        }
    }

    for (std::size_t i = 0; i < outputs.size(); ++i) {
        std::error_code ec;
        fs::remove(outputs[i], ec);
        fs::rename(temporaries[i], outputs[i], ec);
        if (ec) {
            err << "error: could not publish " << outputs[i] << "\n";
            cleanup();
            return kExitWrite;
        }
        temporaries[i].clear();
        out << "wrote " << outputs[i] << "\n";
    }

    return kExitSuccess;
}

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

    ConversionOptions options;
    const int parseResult = parseConversionOptions(args, options, err);
    if (parseResult != kExitSuccess) {
        return parseResult;
    }

    return runConversion(options, out, err);
}

} // namespace mlod
