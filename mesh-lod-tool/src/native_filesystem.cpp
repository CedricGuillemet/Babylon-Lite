#include "native_filesystem.h"

#include "input.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

namespace mlod {
namespace {

// Returns the parent directory of `path` using the same separator handling as
// the previous cgltf_load_buffer_file-based behavior (both '/' and '\\' are
// accepted; an empty result means "current directory").
std::string parentDirectory(const std::string& path) {
    const std::size_t separator = path.find_last_of("/\\");
    return separator == std::string::npos ? std::string() : path.substr(0, separator);
}

bool readFileBytes(const std::string& path, std::vector<unsigned char>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return static_cast<bool>(file) || file.eof();
}

} // namespace

int loadInputBundleFromNativePath(const std::string& path, InputBundle& bundle, std::ostream& err) {
    std::vector<unsigned char> entryBytes;
    if (!readFileBytes(path, entryBytes)) {
        err << "error: " << path << ": could not open input file\n";
        return kExitIo;
    }

    InputBundle result;
    result.entryVirtualPath = path;
    result.entryBytes = std::move(entryBytes);
    result.sourceDisplayName = path;
    result.capabilities.allowsRemoteUri = false;
    result.capabilities.allowsAbsoluteUri = true; // native paths may be absolute, matching prior behavior
    result.capabilities.rootDescription = "native filesystem (entry file's parent directory)";

    const std::string directory = parentDirectory(path);
    result.resolver = [directory](const std::string& uri, const std::string& gltfProperty, ResourceKind kind,
                                  ResolvedResource& outResource, Diagnostic& outError) -> bool {
        (void)kind;
        const std::string resolvedPath = directory.empty() ? uri : directory + "/" + uri;
        if (!readFileBytes(resolvedPath, outResource.bytes)) {
            DiagnosticContext context;
            context.resourceUri = uri;
            context.gltfProperty = gltfProperty;
            outError = makeDiagnostic(diag_code::kIoRead, kExitIo, DiagnosticSeverity::kError,
                                      "could not read referenced resource '" + uri + "'", context);
            return false;
        }
        return true;
    };

    bundle = std::move(result);
    return kExitSuccess;
}

int loadSourcePrimitives(const ConversionOptions& options, std::vector<SourcePrimitive>& out, std::ostream& err,
                         std::array<std::uint8_t, 32>* sourceDigest) {
    InputBundle bundle;
    const int loadResult = loadInputBundleFromNativePath(options.inputPath, bundle, err);
    if (loadResult != kExitSuccess) {
        return loadResult;
    }

    const PrimitiveSelection selection = toPrimitiveSelection(options);
    return loadSourcePrimitivesFromBundle(bundle, selection, out, err, sourceDigest);
}

int publishConversionResult(const ConversionOptions& options, const ConversionResult& result, std::ostream& out,
                            std::ostream& err) {
    namespace fs = std::filesystem;

    std::vector<SelectedPrimitive> selection;
    selection.reserve(result.outputs.size());
    for (const PrimitiveOutput& output : result.outputs) {
        selection.push_back({output.identity.meshIndex, output.identity.primitiveIndex});
    }
    const std::vector<std::string> outputPaths = deriveOutputPaths(options.outputPath, selection);

    // Human-readable per-primitive summary (stdout). Not a documented/tested
    // contract -- purely informational, mirroring the tool's original style.
    for (std::size_t i = 0; i < result.outputs.size(); ++i) {
        const PrimitiveOutput& output = result.outputs[i];
        out << "mesh " << output.identity.meshIndex << " primitive " << output.identity.primitiveIndex << ":\n"
            << "  meshlets=" << output.meshletCount << " hierarchyDepth=" << output.hierarchyDepth << "\n"
            << "  sourceTriangles=" << output.sourceTriangleCount << " outputTriangles=" << output.outputTriangleCount
            << "\n"
            << "  pages=" << output.pageCount << " (pinned " << output.pinnedPageCount
            << ") bytes=" << output.validatedByteSize << "\n";
    }

    if (!options.statsJsonPath.empty()) {
        std::ofstream statsFile(options.statsJsonPath, std::ios::binary | std::ios::trunc);
        if (!statsFile) {
            err << "error: could not write statistics to " << options.statsJsonPath << "\n";
            return kExitIo;
        }
        statsFile.write(result.canonicalMetadataJson.data(),
                        static_cast<std::streamsize>(result.canonicalMetadataJson.size()));
        if (!statsFile) {
            err << "error: could not write statistics to " << options.statsJsonPath << "\n";
            return kExitIo;
        }
    }

    if (options.validateOnly) {
        return kExitSuccess;
    }

    // Atomic publish: write every output to a sibling temporary first, then
    // rename each into place. A failure before all temporaries are written
    // leaves no final files (matching the tool's original publication
    // contract exactly).
    std::vector<std::string> temporaries(outputPaths.size());
    const auto cleanup = [&]() {
        for (const std::string& temporary : temporaries) {
            if (!temporary.empty()) {
                std::error_code ec;
                fs::remove(temporary, ec);
            }
        }
    };

    for (std::size_t i = 0; i < outputPaths.size(); ++i) {
        temporaries[i] = outputPaths[i] + ".tmp";
        std::ofstream file(temporaries[i], std::ios::binary | std::ios::trunc);
        if (!file) {
            err << "error: could not create " << temporaries[i] << "\n";
            cleanup();
            return kExitWrite;
        }
        const std::vector<unsigned char>& bytes = result.outputs[i].bytes;
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        file.close();
        if (!file) {
            err << "error: could not write " << temporaries[i] << "\n";
            cleanup();
            return kExitWrite;
        }
    }

    for (std::size_t i = 0; i < outputPaths.size(); ++i) {
        std::error_code ec;
        fs::remove(outputPaths[i], ec);
        fs::rename(temporaries[i], outputPaths[i], ec);
        if (ec) {
            err << "error: could not publish " << outputPaths[i] << "\n";
            cleanup();
            return kExitWrite;
        }
        temporaries[i].clear();
        out << "wrote " << outputPaths[i] << "\n";
    }

    return kExitSuccess;
}

} // namespace mlod
