#include "cli.h"

#include "mlod_version.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

int run(const std::vector<std::string>& args, std::string& out, std::string& err) {
    std::ostringstream outStream;
    std::ostringstream errStream;
    const int code = mlod::runCli(args, outStream, errStream);
    out = outStream.str();
    err = errStream.str();
    return code;
}

} // namespace

int main() {
    std::string out;
    std::string err;

    int code = run({"--help"}, out, err);
    expect(code == mlod::kExitSuccess, "--help exits 0");
    expect(contains(out, "--input"), "--help lists --input");
    expect(contains(out, "--output"), "--help lists --output");
    expect(contains(out, "--version"), "--help lists --version");
    expect(contains(out, "--page-target-kib"), "--help lists page options");

    code = run({"--version"}, out, err);
    expect(code == mlod::kExitSuccess, "--version exits 0");
    expect(contains(out, "tool_version="), "--version prints tool_version");
    expect(contains(out, "format_version=1.0"), "--version prints format 1.0");
    expect(contains(out, std::string("meshoptimizer_revision=") + mlod::kMeshoptimizerRev),
           "--version prints meshoptimizer revision");
    expect(contains(out, std::string("cgltf_revision=") + mlod::kCgltfRev),
           "--version prints cgltf revision");
    expect(contains(out, "compiler_target="), "--version prints compiler target");

    // The pinned revisions are contractually fixed by the architecture.
    expect(std::string(mlod::kMeshoptimizerRev) == "f843aae0b3070306bd2aeef43ffcf09509fee526",
           "meshoptimizer pin matches architecture");
    expect(std::string(mlod::kCgltfRev) == "85cd62382dfea638278962690cf515023f33ed00",
           "cgltf pin matches architecture");

    code = run({"--nope"}, out, err);
    expect(code == mlod::kExitCli, "unknown argument exits with CLI error code");
    expect(!err.empty(), "unknown argument writes a diagnostic");

    code = run({}, out, err);
    expect(code == mlod::kExitCli, "no arguments exits with CLI error code");

    if (g_failures == 0) {
        std::cout << "all mesh-lod-tool smoke tests passed\n";
        return 0;
    }
    std::cerr << g_failures << " mesh-lod-tool smoke test(s) failed\n";
    return 1;
}
