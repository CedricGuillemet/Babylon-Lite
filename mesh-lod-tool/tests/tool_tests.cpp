#include "cli.h"

#include "input.h"
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

int runCliCapture(const std::vector<std::string>& args, std::string& out, std::string& err) {
    std::ostringstream outStream;
    std::ostringstream errStream;
    const int code = mlod::runCli(args, outStream, errStream);
    out = outStream.str();
    err = errStream.str();
    return code;
}

int parse(const std::vector<std::string>& args, mlod::ConversionOptions& options) {
    std::ostringstream errStream;
    return mlod::parseConversionOptions(args, options, errStream);
}

// Convenience: full-featured base argument list a case can extend.
std::vector<std::string> withIo(std::vector<std::string> extra) {
    std::vector<std::string> args = {"--input", "in.glb", "--output", "out.mlod"};
    for (auto& token : extra) {
        args.push_back(std::move(token));
    }
    return args;
}

void testVersionAndHelp() {
    std::string out;
    std::string err;

    int code = runCliCapture({"--help"}, out, err);
    expect(code == mlod::kExitSuccess, "--help exits 0");
    expect(contains(out, "--input"), "--help lists --input");
    expect(contains(out, "--output"), "--help lists --output");
    expect(contains(out, "--meshlet-max-vertices"), "--help lists meshlet options");
    expect(contains(out, "--page-target-kib"), "--help lists page options");
    expect(contains(out, "--validate-only"), "--help lists --validate-only");
    expect(contains(out, "--version"), "--help lists --version");

    code = runCliCapture({"--version"}, out, err);
    expect(code == mlod::kExitSuccess, "--version exits 0");
    expect(contains(out, "tool_version="), "--version prints tool_version");
    expect(contains(out, "format_version=1.0"), "--version prints format 1.0");
    expect(contains(out, std::string("meshoptimizer_revision=") + mlod::kMeshoptimizerRev),
           "--version prints meshoptimizer revision");
    expect(contains(out, std::string("cgltf_revision=") + mlod::kCgltfRev),
           "--version prints cgltf revision");
    expect(std::string(mlod::kMeshoptimizerRev) == "f843aae0b3070306bd2aeef43ffcf09509fee526",
           "meshoptimizer pin matches architecture");
    expect(std::string(mlod::kCgltfRev) == "85cd62382dfea638278962690cf515023f33ed00",
           "cgltf pin matches architecture");

    // --help wins over other arguments and never begins conversion.
    code = runCliCapture({"--input", "x", "--help"}, out, err);
    expect(code == mlod::kExitSuccess, "--help takes precedence");

    code = runCliCapture({}, out, err);
    expect(code == mlod::kExitCli, "no arguments exits with CLI error code");
}

void testDefaults() {
    mlod::ConversionOptions options;
    const int code = parse({"--input", "in.glb", "--output", "out.mlod"}, options);
    expect(code == mlod::kExitSuccess, "minimal valid args parse");
    expect(options.inputPath == "in.glb", "input path stored verbatim");
    expect(options.outputPath == "out.mlod", "output path stored verbatim");
    expect(!options.hasMesh && !options.hasPrimitive, "no selection by default");
    expect(options.meshletMaxVertices == 64, "default meshlet max vertices");
    expect(options.meshletMinTriangles == 40, "default meshlet min triangles");
    expect(options.meshletMaxTriangles == 124, "default meshlet max triangles");
    expect(options.partitionSize == 8, "default partition size");
    expect(options.simplifyRatio == 0.5f, "default simplify ratio");
    expect(options.simplifyThreshold == 0.85f, "default simplify threshold");
    expect(options.pageMinKiB == 64, "default page min");
    expect(options.pageTargetKiB == 128, "default page target");
    expect(options.pageMaxKiB == 256, "default page max");
    expect(!options.validateOnly, "validate-only off by default");
    expect(options.statsJsonPath.empty(), "no stats path by default");
}

void testValidValues() {
    mlod::ConversionOptions options;
    const int code = parse(withIo({"--mesh", "2", "--primitive", "1", "--meshlet-max-vertices",
                                    "128", "--meshlet-min-triangles", "32", "--meshlet-max-triangles",
                                    "96", "--partition-size", "16", "--simplify-ratio", "0.25",
                                    "--simplify-threshold", "0.9", "--page-min-kib", "64",
                                    "--page-target-kib", "192", "--page-max-kib", "256",
                                    "--stats-json", "stats.json", "--validate-only"}),
                           options);
    expect(code == mlod::kExitSuccess, "full option set parses");
    expect(options.hasMesh && options.meshIndex == 2, "mesh index parsed");
    expect(options.hasPrimitive && options.primitiveIndex == 1, "primitive index parsed");
    expect(options.meshletMaxVertices == 128, "meshlet max vertices parsed");
    expect(options.partitionSize == 16, "partition size parsed");
    expect(options.simplifyRatio == 0.25f, "simplify ratio parsed");
    expect(options.pageTargetKiB == 192, "page target parsed");
    expect(options.statsJsonPath == "stats.json", "stats path parsed");
    expect(options.validateOnly, "validate-only parsed");
}

void testBoundaries() {
    mlod::ConversionOptions options;

    expect(parse(withIo({"--meshlet-max-vertices", "4"}), options) == mlod::kExitSuccess,
           "meshlet vertices lower bound accepted");
    expect(parse(withIo({"--meshlet-max-vertices", "256"}), options) == mlod::kExitSuccess,
           "meshlet vertices upper bound accepted");
    expect(parse(withIo({"--meshlet-max-vertices", "3"}), options) == mlod::kExitCli,
           "meshlet vertices below range rejected");
    expect(parse(withIo({"--meshlet-max-vertices", "257"}), options) == mlod::kExitCli,
           "meshlet vertices above range rejected");

    expect(parse(withIo({"--partition-size", "2"}), options) == mlod::kExitSuccess,
           "partition lower bound accepted");
    expect(parse(withIo({"--partition-size", "32"}), options) == mlod::kExitSuccess,
           "partition upper bound accepted");
    expect(parse(withIo({"--partition-size", "1"}), options) == mlod::kExitCli,
           "partition below range rejected");
    expect(parse(withIo({"--partition-size", "33"}), options) == mlod::kExitCli,
           "partition above range rejected");

    expect(parse(withIo({"--simplify-ratio", "0"}), options) == mlod::kExitSuccess,
           "simplify ratio 0 accepted");
    expect(parse(withIo({"--simplify-ratio", "1"}), options) == mlod::kExitSuccess,
           "simplify ratio 1 accepted");
    expect(parse(withIo({"--simplify-ratio", "-0.1"}), options) == mlod::kExitCli,
           "simplify ratio below range rejected");
    expect(parse(withIo({"--simplify-ratio", "1.1"}), options) == mlod::kExitCli,
           "simplify ratio above range rejected");

    // Page sizes: range, multiple-of-64, and ordering.
    expect(parse(withIo({"--page-target-kib", "100"}), options) == mlod::kExitCli,
           "page size not multiple of 64 rejected");
    expect(parse(withIo({"--page-min-kib", "32"}), options) == mlod::kExitCli,
           "page size below range rejected");
    expect(parse(withIo({"--page-max-kib", "320"}), options) == mlod::kExitCli,
           "page size above range rejected");
    expect(parse(withIo({"--page-min-kib", "192", "--page-target-kib", "128"}), options) ==
               mlod::kExitCli,
           "page ordering violation rejected");
    expect(parse(withIo({"--meshlet-min-triangles", "200", "--meshlet-max-triangles", "100"}),
                 options) == mlod::kExitCli,
           "meshlet triangle ordering violation rejected");
}

void testErrors() {
    mlod::ConversionOptions options;

    expect(parse({"--output", "out.mlod"}, options) == mlod::kExitCli, "missing --input rejected");
    expect(parse({"--input", "in.glb"}, options) == mlod::kExitCli, "missing --output rejected");
    expect(parse(withIo({"--primitive", "0"}), options) == mlod::kExitCli,
           "--primitive without --mesh rejected");
    expect(parse(withIo({"--mesh", "abc"}), options) == mlod::kExitCli, "non-numeric mesh rejected");
    expect(parse(withIo({"--simplify-ratio", "nan"}), options) == mlod::kExitCli,
           "NaN simplify ratio rejected");
    expect(parse(withIo({"--simplify-ratio", "inf"}), options) == mlod::kExitCli,
           "infinite simplify ratio rejected");
    expect(parse(withIo({"--mesh"}), options) == mlod::kExitCli, "missing value rejected");
    expect(parse({"--input", "a", "--input", "b", "--output", "o.mlod"}, options) == mlod::kExitCli,
           "duplicate option rejected");
    expect(parse(withIo({"--bogus"}), options) == mlod::kExitCli, "unknown option rejected");

    // A parse failure must never leave a build believing it can convert.
    std::string out;
    std::string err;
    const int code = runCliCapture({"--bogus"}, out, err);
    expect(code == mlod::kExitCli, "runCli surfaces parse errors");
    expect(!err.empty(), "runCli writes a diagnostic on parse error");
}

void testNaming() {
    // A single selected primitive uses the requested path exactly.
    std::vector<mlod::SelectedPrimitive> single = {{0, 0}};
    auto singlePaths = mlod::deriveOutputPaths("statue.mlod", single);
    expect(singlePaths.size() == 1 && singlePaths[0] == "statue.mlod",
           "single primitive keeps exact output");

    // Multiple selected primitives receive deterministic sibling names.
    std::vector<mlod::SelectedPrimitive> many = {{0, 0}, {1, 0}, {2, 0}};
    auto manyPaths = mlod::deriveOutputPaths("statue.mlod", many);
    expect(manyPaths.size() == 3, "three primitives produce three paths");
    expect(manyPaths[0] == "statue.mesh000.prim000.mlod", "mesh000 name");
    expect(manyPaths[1] == "statue.mesh001.prim000.mlod", "mesh001 name");
    expect(manyPaths[2] == "statue.mesh002.prim000.mlod", "mesh002 name");

    // Directory prefixes and multi-primitive meshes are preserved and padded.
    expect(mlod::derivePrimitiveOutputPath("out/statue.mlod", 12, 3) ==
               "out/statue.mesh012.prim003.mlod",
           "directory prefix preserved with zero padding");
    expect(mlod::derivePrimitiveOutputPath("a\\b\\model.mlod", 0, 5) ==
               "a\\b\\model.mesh000.prim005.mlod",
           "windows separators preserved");
    // Names are collision-free across distinct (mesh, primitive) pairs.
    expect(mlod::derivePrimitiveOutputPath("m.mlod", 0, 1) !=
               mlod::derivePrimitiveOutputPath("m.mlod", 1, 0),
           "distinct selections yield distinct names");
}

void testCanonicalOptions() {
    mlod::ConversionOptions a;
    mlod::ConversionOptions b;
    a.inputPath = "one.glb";
    a.outputPath = "one.mlod";
    b.inputPath = "two.glb"; // different paths must not change the canonical form
    b.outputPath = "two.mlod";
    expect(mlod::canonicalConversionOptions(a) == mlod::canonicalConversionOptions(b),
           "canonical options ignore file paths");
    expect(!contains(mlod::canonicalConversionOptions(a), "one.glb"),
           "canonical options contain no path");

    b.partitionSize = 16;
    expect(mlod::canonicalConversionOptions(a) != mlod::canonicalConversionOptions(b),
           "canonical options reflect knob changes");
}

} // namespace

int main() {
    testVersionAndHelp();
    testDefaults();
    testValidValues();
    testBoundaries();
    testErrors();
    testNaming();
    testCanonicalOptions();

    if (g_failures == 0) {
        std::cout << "all mesh-lod-tool CLI tests passed\n";
        return 0;
    }
    std::cerr << g_failures << " mesh-lod-tool CLI test(s) failed\n";
    return 1;
}
