#include "cli.h"

#include "hierarchy.h"
#include "input.h"
#include "mlod_version.h"
#include "validator.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
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

std::string fixture(const std::string& name) {
    return std::string(MLOD_FIXTURES_DIR) + "/" + name;
}

int loadFixture(const std::string& name, std::vector<mlod::SourcePrimitive>& out, std::string& err) {
    mlod::ConversionOptions options;
    options.inputPath = fixture(name);
    options.outputPath = "out.mlod";
    std::ostringstream errStream;
    const int code = mlod::loadSourcePrimitives(options, out, errStream);
    err = errStream.str();
    return code;
}

// Loads then normalizes the first primitive, returning the first non-success code.
int loadAndNormalize(const std::string& name, mlod::NormalizedPrimitive& out, std::string& err) {
    std::vector<mlod::SourcePrimitive> primitives;
    const int loadCode = loadFixture(name, primitives, err);
    if (loadCode != mlod::kExitSuccess) {
        return loadCode;
    }
    std::ostringstream errStream;
    const int code = mlod::normalizePrimitive(primitives[0], out, errStream);
    err = errStream.str();
    return code;
}

void expectRejected(const std::string& name, const std::string& keyword) {
    std::string err;
    mlod::NormalizedPrimitive out;
    const int code = loadAndNormalize(name, out, err);
    expect(code == mlod::kExitMalformed || code == mlod::kExitUnsupported,
           name + " is rejected with exit 4 or 5");
    if (!keyword.empty()) {
        expect(contains(err, keyword), name + " diagnostic mentions '" + keyword + "'");
    }
}

void testIngestion() {
    std::string err;

    // Supported: indexed triangle, POSITION only, no material.
    std::vector<mlod::SourcePrimitive> triangle;
    expect(loadFixture("triangle_indexed.gltf", triangle, err) == mlod::kExitSuccess,
           "indexed triangle loads");
    expect(triangle.size() == 1, "one primitive selected");
    expect(triangle[0].vertexCount == 3, "triangle has three vertices");
    expect(triangle[0].normals.empty(), "source triangle has no normals");
    expect(triangle[0].indices.size() == 3, "triangle indices read");

    mlod::NormalizedPrimitive triNorm;
    expect(loadAndNormalize("triangle_indexed.gltf", triNorm, err) == mlod::kExitSuccess,
           "indexed triangle normalizes");
    expect(triNorm.normals.size() == 9, "normals generated for all vertices");
    expect(triNorm.normals[2] > 0.99f, "generated normal points +Z");
    expect(triNorm.triangleCount() == 1, "one triangle after normalization");

    // GLB ingestion must be equivalent to the glTF form.
    std::vector<mlod::SourcePrimitive> triangleGlb;
    expect(loadFixture("triangle_indexed.glb", triangleGlb, err) == mlod::kExitSuccess,
           "GLB triangle loads");
    expect(triangleGlb.size() == 1 && triangleGlb[0].positions == triangle[0].positions,
           "GLB and glTF positions are equivalent");
    expect(triangleGlb[0].indices == triangle[0].indices, "GLB and glTF indices are equivalent");

    // Unindexed input becomes valid indexed geometry.
    std::vector<mlod::SourcePrimitive> unindexed;
    expect(loadFixture("triangle_unindexed.gltf", unindexed, err) == mlod::kExitSuccess,
           "unindexed triangle loads");
    expect(unindexed[0].indices.empty(), "unindexed source has no indices");
    mlod::NormalizedPrimitive unindexedNorm;
    expect(loadAndNormalize("triangle_unindexed.gltf", unindexedNorm, err) == mlod::kExitSuccess,
           "unindexed triangle normalizes");
    expect(unindexedNorm.indices.size() == 3, "sequential indices synthesized");
    expect(unindexedNorm.indices[0] == 0 && unindexedNorm.indices[1] == 1 &&
               unindexedNorm.indices[2] == 2,
           "synthesized indices are sequential");

    // Full attributes with an opaque, double-sided, untextured material.
    std::vector<mlod::SourcePrimitive> quad;
    expect(loadFixture("quad_textured.gltf", quad, err) == mlod::kExitSuccess, "quad loads");
    expect(!quad[0].normals.empty() && !quad[0].uvs.empty(), "quad has normals and UVs");
    expect(quad[0].material.hasMaterial && quad[0].material.doubleSided,
           "quad material facts captured");
    expect(!quad[0].material.requiresUv, "untextured material does not require UV");
    mlod::NormalizedPrimitive quadNorm;
    expect(loadAndNormalize("quad_textured.gltf", quadNorm, err) == mlod::kExitSuccess,
           "quad normalizes");
    expect(quadNorm.hasUv && quadNorm.triangleCount() == 2, "quad has UVs and two triangles");

    // Missing file surfaces an I/O error.
    std::vector<mlod::SourcePrimitive> missing;
    expect(loadFixture("does_not_exist.gltf", missing, err) == mlod::kExitIo,
           "missing input is an I/O error");

    // Every required rejection class fails with exit 4 or 5 and context.
    expectRejected("points.gltf", "TRIANGLES");
    expectRejected("skinned.gltf", "");
    expectRejected("morph.gltf", "morph");
    expectRejected("alpha_blend.gltf", "alpha");
    expectRejected("transmission.gltf", "transmission");
    expectRejected("draco.gltf", "draco");
    expectRejected("sparse.gltf", "sparse");
    expectRejected("textured_no_uv.gltf", "TEXCOORD_0");
    expectRejected("bad_index.gltf", "index");
    expectRejected("malformed.gltf", "");
}

int buildFromFixture(const std::string& name, std::size_t primitiveIndex,
                     mlod::PrimitiveHierarchy& out, std::string& err) {
    std::vector<mlod::SourcePrimitive> primitives;
    const int loadCode = loadFixture(name, primitives, err);
    if (loadCode != mlod::kExitSuccess) {
        return loadCode;
    }
    if (primitiveIndex >= primitives.size()) {
        return -1;
    }
    mlod::NormalizedPrimitive normalized;
    std::ostringstream normErr;
    const int normCode = mlod::normalizePrimitive(primitives[primitiveIndex], normalized, normErr);
    if (normCode != mlod::kExitSuccess) {
        err = normErr.str();
        return normCode;
    }
    const mlod::ConversionOptions options;
    std::ostringstream buildErr;
    const int buildCode = mlod::buildHierarchy(normalized, options, out, buildErr);
    err = buildErr.str();
    return buildCode;
}

std::uint64_t terminalCoverage(const mlod::PrimitiveHierarchy& h) {
    std::uint64_t total = 0;
    for (const mlod::HierarchyGroup& g : h.groups) {
        if (g.terminal) {
            total += g.sourceTriangles;
        }
    }
    return total;
}

void testHierarchy() {
    const mlod::ConversionOptions options;

    // Single triangle: one terminal group, one cluster, one level.
    mlod::PrimitiveHierarchy triangle;
    std::string err;
    expect(buildFromFixture("triangle_indexed.gltf", 0, triangle, err) == mlod::kExitSuccess,
           "single triangle builds a hierarchy");
    expect(triangle.groups.size() == 1 && triangle.clusters.size() == 1,
           "single triangle yields one group and cluster");
    expect(!triangle.nodes.empty() && triangle.levelCount == 1, "single triangle is one level");
    expect(triangle.groups[0].terminal, "the single group is terminal");
    expect(terminalCoverage(triangle) == triangle.sourceTriangleCount,
           "single triangle terminal coverage is exact");

    // Displaced grid: multi-level hierarchy with complete coarse coverage.
    mlod::PrimitiveHierarchy grid;
    expect(buildFromFixture("grid.gltf", 0, grid, err) == mlod::kExitSuccess,
           "grid builds a hierarchy");
    expect(grid.groups.size() > 1 && grid.clusters.size() > 1, "grid emits multiple groups");
    expect(grid.levelCount >= 2, "grid produces a multi-level hierarchy");
    expect(!grid.nodes.empty(), "grid emits hierarchy nodes");
    expect(terminalCoverage(grid) == grid.sourceTriangleCount, "grid terminal coverage is exact");
    std::ostringstream revalidateErr;
    expect(mlod::validateHierarchy(grid, options, revalidateErr) == mlod::kExitSuccess,
           "grid hierarchy re-validates");
    for (const mlod::HierarchyCluster& c : grid.clusters) {
        expect(c.triangleCount <= options.meshletMaxTriangles &&
                   c.vertexCount <= options.meshletMaxVertices,
               "grid clusters respect meshlet limits");
    }

    // Two primitives with distinct materials build as independent hierarchies.
    mlod::PrimitiveHierarchy first;
    mlod::PrimitiveHierarchy second;
    expect(buildFromFixture("two_primitives.gltf", 0, first, err) == mlod::kExitSuccess,
           "first primitive builds");
    expect(buildFromFixture("two_primitives.gltf", 1, second, err) == mlod::kExitSuccess,
           "second primitive builds");
    expect(first.meshIndex == 0 && first.primitiveIndex == 0, "first primitive indices");
    expect(second.meshIndex == 1 && second.primitiveIndex == 0, "second primitive indices");
    expect(first.material.hasMaterial && second.material.hasMaterial, "both carry material facts");
    expect(terminalCoverage(first) == first.sourceTriangleCount &&
               terminalCoverage(second) == second.sourceTriangleCount,
           "both primitives have exact terminal coverage");

    // Validation rejects a hierarchy whose coarse coverage is broken.
    mlod::PrimitiveHierarchy corrupted = grid;
    corrupted.sourceTriangleCount += 1;
    std::ostringstream rejectErr;
    expect(mlod::validateHierarchy(corrupted, options, rejectErr) == mlod::kExitHierarchy,
           "validation rejects broken terminal coverage");

    mlod::PrimitiveHierarchy oversized = grid;
    oversized.clusters[0].triangleCount = static_cast<std::uint16_t>(options.meshletMaxTriangles + 1);
    std::ostringstream oversizedErr;
    expect(mlod::validateHierarchy(oversized, options, oversizedErr) == mlod::kExitHierarchy,
           "validation rejects an over-limit cluster");
}

std::vector<unsigned char> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
}

int convert(const std::string& fixtureName, const std::string& outputPath, bool validateOnly,
            const std::string& statsPath = std::string()) {
    mlod::ConversionOptions options;
    options.inputPath = fixture(fixtureName);
    options.outputPath = outputPath;
    options.validateOnly = validateOnly;
    options.statsJsonPath = statsPath;
    std::ostringstream outStream;
    std::ostringstream errStream;
    return mlod::runConversion(options, outStream, errStream);
}

void testEndToEnd() {
    namespace fs = std::filesystem;

    // Single-primitive conversion writes exactly one container that revalidates,
    // and emits canonical statistics with the required counts.
    const std::string gridOut = "e2e_grid.mlod";
    const std::string gridStats = "e2e_grid.stats.json";
    fs::remove(gridOut);
    fs::remove(gridStats);
    expect(convert("grid.gltf", gridOut, false, gridStats) == mlod::kExitSuccess,
           "grid converts end to end");
    expect(fs::exists(gridOut), "grid output is written");
    std::vector<unsigned char> gridBytes = readFile(gridOut);
    std::ostringstream errStream;
    expect(mlod::validateContainer(gridBytes.data(), gridBytes.size(), errStream) ==
               mlod::kExitSuccess,
           "written grid container validates from disk");
    const std::vector<unsigned char> statsBytes = readFile(gridStats);
    const std::string statsText(statsBytes.begin(), statsBytes.end());
    expect(contains(statsText, "\"sourceTriangleCount\":1152"), "stats report source triangles");
    expect(contains(statsText, "\"pageCount\":") && contains(statsText, "\"groupCount\":") &&
               contains(statsText, "\"pinnedPageCount\":"),
           "stats report layout counts");

    // Determinism: a second conversion is byte-identical on disk.
    const std::string gridOut2 = "e2e_grid2.mlod";
    fs::remove(gridOut2);
    expect(convert("grid.gltf", gridOut2, false) == mlod::kExitSuccess, "grid converts again");
    expect(readFile(gridOut2) == gridBytes, "two conversions are byte-identical on disk");

    // --validate-only writes nothing.
    const std::string validateOut = "e2e_validate.mlod";
    fs::remove(validateOut);
    expect(convert("grid.gltf", validateOut, true) == mlod::kExitSuccess, "validate-only succeeds");
    expect(!fs::exists(validateOut), "validate-only writes no output");

    // Multi-primitive conversion publishes one sibling container per primitive.
    const std::string multiOut = "e2e_multi.mlod";
    const std::string multi0 = "e2e_multi.mesh000.prim000.mlod";
    const std::string multi1 = "e2e_multi.mesh001.prim000.mlod";
    fs::remove(multi0);
    fs::remove(multi1);
    expect(convert("two_primitives.gltf", multiOut, false) == mlod::kExitSuccess,
           "two-primitive file converts");
    expect(fs::exists(multi0) && fs::exists(multi1), "both sibling outputs are published");
    expect(!fs::exists(multiOut), "the base name is not written for multi-output");

    // A whole-file conversion with an unsupported primitive fails and leaves no
    // output files (atomic behavior).
    const std::string mixedOut = "e2e_mixed.mlod";
    const std::string mixed0 = "e2e_mixed.mesh000.prim000.mlod";
    const std::string mixed1 = "e2e_mixed.mesh001.prim000.mlod";
    for (const std::string& path : {mixedOut, mixed0, mixed1}) {
        fs::remove(path);
    }
    expect(convert("mixed.gltf", mixedOut, false) == mlod::kExitUnsupported,
           "mixed file conversion fails as unsupported");
    expect(!fs::exists(mixedOut) && !fs::exists(mixed0) && !fs::exists(mixed1),
           "failed multi-output leaves no files");

    // Clean up test artifacts and any stray temporaries.
    for (const std::string& path :
         {gridOut, gridOut2, gridStats, multi0, multi1, gridOut + ".tmp", multi0 + ".tmp",
          multi1 + ".tmp"}) {
        fs::remove(path);
    }
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
    testIngestion();
    testHierarchy();
    testEndToEnd();

    if (g_failures == 0) {
        std::cout << "all mesh-lod-tool CLI tests passed\n";
        return 0;
    }
    std::cerr << g_failures << " mesh-lod-tool CLI test(s) failed\n";
    return 1;
}
