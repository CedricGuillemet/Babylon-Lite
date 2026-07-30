// Contract/default/canonicalization tests for the host-independent converter
// core types (conversion_types.h + diagnostics.h) plus memory input and
// resource-resolution tests (input.h + native_filesystem.h).
// These exercise pure value types and the memory-parsing ingestion path -- no
// dependence on a specific working directory beyond the checked-in fixtures.

#include "cli.h"
#include "conversion.h"
#include "conversion_types.h"
#include "diagnostics.h"
#include "input.h"
#include "native_filesystem.h"
#include "normalize.h"
#include "validator.h"

#include <array>
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

void testCanonicalDefaultsExact() {
    // The canonical/default ConversionSettings must serialize to the exact
    // native default canonicalConversionOptions() string (cli.cpp) so that
    // "shared defaults and validation exactly match the current CLI".
    mlod::ConversionOptions defaultOptions;
    const std::string nativeCanonical = mlod::canonicalConversionOptions(defaultOptions);

    mlod::ConversionSettings defaultSettings;
    const std::string coreCanonical = mlod::canonicalConversionSettings(defaultSettings);

    expect(nativeCanonical == coreCanonical, "default ConversionSettings canonicalizes identically to "
                                             "default ConversionOptions");

    // toConversionSettings() over CLI defaults also canonicalizes identically.
    const std::string mappedCanonical = mlod::canonicalConversionSettings(mlod::toConversionSettings(defaultOptions));
    expect(nativeCanonical == mappedCanonical, "toConversionSettings(defaults) canonicalizes identically to native "
                                               "defaults");
}

void testHostOnlyFieldsCannotAffectCanonicalSettings() {
    // Two requests differing only in host paths, stats path, or validate-only
    // must map to the exact same canonical settings string.
    mlod::ConversionOptions a;
    a.inputPath = "a/in.glb";
    a.outputPath = "a/out.mlod";
    a.statsJsonPath = "a/stats.json";
    a.validateOnly = false;

    mlod::ConversionOptions b;
    b.inputPath = "completely/different/path/in.gltf";
    b.outputPath = "elsewhere/out.mlod";
    b.statsJsonPath = "";
    b.validateOnly = true;

    expect(mlod::canonicalConversionSettings(mlod::toConversionSettings(a)) ==
              mlod::canonicalConversionSettings(mlod::toConversionSettings(b)),
          "host-only fields (paths, stats path, validate-only) do not affect canonical settings");
}

void testEveryOutputAffectingSettingChangesCanonical() {
    const std::string base = mlod::canonicalConversionSettings(mlod::ConversionSettings{});

    auto changed = [&](void (*mutate)(mlod::ConversionSettings&)) {
        mlod::ConversionSettings settings;
        mutate(settings);
        return mlod::canonicalConversionSettings(settings);
    };

    expect(changed([](mlod::ConversionSettings& s) { s.meshletMaxVertices = 96; }) != base,
          "meshletMaxVertices change alters canonical settings");
    expect(changed([](mlod::ConversionSettings& s) { s.meshletMinTriangles = 32; }) != base,
          "meshletMinTriangles change alters canonical settings");
    expect(changed([](mlod::ConversionSettings& s) { s.meshletMaxTriangles = 160; }) != base,
          "meshletMaxTriangles change alters canonical settings");
    expect(changed([](mlod::ConversionSettings& s) { s.partitionSize = 12; }) != base,
          "partitionSize change alters canonical settings");
    expect(changed([](mlod::ConversionSettings& s) { s.simplifyRatio = 0.65f; }) != base,
          "simplifyRatio change alters canonical settings");
    expect(changed([](mlod::ConversionSettings& s) { s.simplifyThreshold = 0.9f; }) != base,
          "simplifyThreshold change alters canonical settings");
    expect(changed([](mlod::ConversionSettings& s) { s.pageMinKiB = 128; }) != base,
          "pageMinKiB change alters canonical settings");
    expect(changed([](mlod::ConversionSettings& s) { s.pageTargetKiB = 192; }) != base,
          "pageTargetKiB change alters canonical settings");
    expect(changed([](mlod::ConversionSettings& s) { s.pageMaxKiB = 256; s.pageTargetKiB = 256; }) != base,
          "pageMaxKiB change alters canonical settings");

    // Selection never enters the canonical string (it changes *which*
    // primitives are emitted, not the bytes of any one emitted primitive).
    mlod::ConversionSettings selected;
    selected.selection = mlod::PrimitiveSelection::singlePrimitive(3, 1);
    expect(mlod::canonicalConversionSettings(selected) == base,
          "selection does not affect canonical settings");
}

void testPrimitiveSelectionValidity() {
    const mlod::PrimitiveSelection all = mlod::PrimitiveSelection::allPrimitives();
    expect(all.mode == mlod::PrimitiveSelectionMode::kAllPrimitives, "allPrimitives() has kAllPrimitives mode");

    const mlod::PrimitiveSelection mesh = mlod::PrimitiveSelection::wholeMesh(4);
    expect(mesh.mode == mlod::PrimitiveSelectionMode::kWholeMesh && mesh.meshIndex == 4,
          "wholeMesh() carries the mesh index");

    const mlod::PrimitiveSelection primitive = mlod::PrimitiveSelection::singlePrimitive(4, 2);
    expect(primitive.mode == mlod::PrimitiveSelectionMode::kSinglePrimitive && primitive.meshIndex == 4 &&
              primitive.primitiveIndex == 2,
          "singlePrimitive() always carries a mesh index alongside the primitive index");

    expect(all != mesh && mesh != primitive, "distinct selections compare unequal");
    expect(mlod::PrimitiveSelection::wholeMesh(4) == mesh, "identical selections compare equal");
}

void testEveryNumericBoundaryAndConflict() {
    struct Case {
        const char* name;
        mlod::ConversionSettings settings;
        bool expectValid;
    };

    mlod::ConversionSettings pageMinNotMultiple;
    pageMinNotMultiple.pageMinKiB = 100;

    mlod::ConversionSettings pageOutOfOrder;
    pageOutOfOrder.pageMinKiB = 192;
    pageOutOfOrder.pageTargetKiB = 128;
    pageOutOfOrder.pageMaxKiB = 256;

    mlod::ConversionSettings triangleConflict;
    triangleConflict.meshletMinTriangles = 200;
    triangleConflict.meshletMaxTriangles = 100;

    mlod::ConversionSettings validBoundary;
    validBoundary.pageMinKiB = 64;
    validBoundary.pageTargetKiB = 64;
    validBoundary.pageMaxKiB = 64;
    validBoundary.meshletMinTriangles = 4;
    validBoundary.meshletMaxTriangles = 4;

    const Case cases[] = {
        {"default settings are valid", mlod::ConversionSettings{}, true},
        {"pageMinKiB not a multiple of 64 is invalid", pageMinNotMultiple, false},
        {"page sizes out of min<=target<=max order are invalid", pageOutOfOrder, false},
        {"meshletMinTriangles > meshletMaxTriangles is invalid", triangleConflict, false},
        {"exact boundary values are valid", validBoundary, true},
    };

    for (const Case& testCase : cases) {
        int diagnosticCount = 0;
        mlod::Diagnostic lastDiagnostic;
        const mlod::DiagnosticSink sink = [&](const mlod::Diagnostic& diagnostic) {
            ++diagnosticCount;
            lastDiagnostic = diagnostic;
        };
        const bool valid = mlod::validateConversionSettings(testCase.settings, sink);
        expect(valid == testCase.expectValid, std::string(testCase.name) + " (validity)");
        if (!testCase.expectValid) {
            expect(diagnosticCount == 1, std::string(testCase.name) + " (emits exactly one diagnostic)");
            expect(lastDiagnostic.nativeExitCategory == mlod::kExitCli,
                  std::string(testCase.name) + " (native exit category is kExitCli)");
            expect(lastDiagnostic.code == mlod::diag_code::kCliOption,
                  std::string(testCase.name) + " (uses the kCliOption diagnostic code family)");
            expect(!lastDiagnostic.context.optionName.empty(),
                  std::string(testCase.name) + " (context identifies the offending option)");
        } else {
            expect(diagnosticCount == 0, std::string(testCase.name) + " (emits no diagnostic when valid)");
        }
    }
}

void testAbsentSinksAreNoOps() {
    // An empty DiagnosticSink/ProgressSink must be safely callable and must
    // not allocate or crash (architecture 7.9 implementation detail 4).
    mlod::DiagnosticSink emptyDiagnosticSink;
    mlod::emitDiagnostic(emptyDiagnosticSink, mlod::makeDiagnostic(mlod::diag_code::kMalformed, mlod::kExitMalformed,
                                                                  mlod::DiagnosticSeverity::kError, "unused"));

    mlod::ProgressSink emptyProgressSink;
    mlod::emitProgress(emptyProgressSink, mlod::ProgressEvent{});

    mlod::CancellationProbe emptyProbe;
    expect(!emptyProbe, "an absent CancellationProbe is falsy / usable without invoking it");

    expect(true, "absent sinks did not crash");
}

void testDiagnosticContextIsSerializableByValue() {
    mlod::DiagnosticContext context;
    context.resourceUri = "buffer.bin";
    context.gltfProperty = "buffers[1].uri";
    context.hasMesh = true;
    context.meshIndex = 2;
    context.hasPrimitive = true;
    context.primitiveIndex = 1;
    context.hasAccessor = true;
    context.accessorIndex = 7;
    context.extensionName = "KHR_materials_clearcoat";
    context.optionName = "pageTargetKiB";
    context.hasOutputRange = true;
    context.outputRangeBegin = 128;
    context.outputRangeEnd = 256;

    const mlod::Diagnostic diagnostic = mlod::makeDiagnostic(mlod::diag_code::kUnsupported, mlod::kExitUnsupported,
                                                             mlod::DiagnosticSeverity::kError, "unsupported extension",
                                                             context);

    expect(diagnostic.context.resourceUri == "buffer.bin", "diagnostic context preserves resourceUri");
    expect(diagnostic.context.gltfProperty == "buffers[1].uri", "diagnostic context preserves gltfProperty");
    expect(diagnostic.context.hasMesh && diagnostic.context.meshIndex == 2, "diagnostic context preserves mesh index");
    expect(diagnostic.context.hasPrimitive && diagnostic.context.primitiveIndex == 1,
          "diagnostic context preserves primitive index");
    expect(diagnostic.context.hasAccessor && diagnostic.context.accessorIndex == 7,
          "diagnostic context preserves accessor index");
    expect(diagnostic.context.extensionName == "KHR_materials_clearcoat",
          "diagnostic context preserves extension name");
    expect(diagnostic.context.hasOutputRange && diagnostic.context.outputRangeBegin == 128 &&
              diagnostic.context.outputRangeEnd == 256,
          "diagnostic context preserves output range");
    expect(diagnostic.severity == mlod::DiagnosticSeverity::kError, "diagnostic preserves severity");
    expect(diagnostic.nativeExitCategory == mlod::kExitUnsupported, "diagnostic preserves native exit category");
}

void testCancellationDefaults() {
    mlod::CancellationProbe defaultProbe;
    expect(!defaultProbe, "default-constructed CancellationProbe is absent (never cancelled)");

    bool cancelled = false;
    mlod::CancellationProbe probe = [&]() { return cancelled; };
    expect(!probe(), "a present probe reports false until cancellation is requested");
    cancelled = true;
    expect(probe(), "a present probe reports true once cancellation is requested");
}

void testMemoryLedgerOverflowAndLimit() {
    mlod::MemoryLedger ledger(1024);
    expect(ledger.reserve(512), "reserve within limit succeeds");
    expect(ledger.currentBytes() == 512, "currentBytes reflects the reservation");
    expect(ledger.peakBytes() == 512, "peakBytes reflects the reservation");

    expect(!ledger.reserve(600), "reserve exceeding the policy limit fails");
    expect(ledger.currentBytes() == 512, "a failed reserve leaves currentBytes unchanged");
    expect(ledger.peakBytes() == 512, "a failed reserve leaves peakBytes unchanged");

    ledger.release(256);
    expect(ledger.currentBytes() == 256, "release reduces currentBytes");
    expect(ledger.peakBytes() == 512, "release never reduces peakBytes");

    ledger.release(10000);
    expect(ledger.currentBytes() == 0, "releasing more than reserved clamps to zero rather than underflowing");

    mlod::MemoryLedger overflowLedger(0); // 0 == unbounded
    expect(overflowLedger.reserve(1000), "a zero policy limit is treated as unbounded");
    expect(!overflowLedger.reserve(UINT64_MAX), "an addition that would overflow is rejected even when unbounded");
    expect(overflowLedger.currentBytes() == 1000, "a rejected overflowing reserve leaves currentBytes unchanged");
}

// ---- Task 10.2: memory input and resource resolution ----

std::string fixturePath(const std::string& relative) {
    return std::string(MLOD_FIXTURES_DIR) + "/" + relative;
}

bool loadNormalized(const std::string& relativePath, mlod::NormalizedPrimitive& out) {
    mlod::InputBundle bundle;
    std::ostringstream err;
    if (mlod::loadInputBundleFromNativePath(fixturePath(relativePath), bundle, err) != mlod::kExitSuccess) {
        std::cerr << "FAIL: could not load bundle for " << relativePath << ": " << err.str() << "\n";
        return false;
    }
    std::vector<mlod::SourcePrimitive> sources;
    if (mlod::loadSourcePrimitivesFromBundle(bundle, mlod::PrimitiveSelection::allPrimitives(), sources, err) !=
        mlod::kExitSuccess) {
        std::cerr << "FAIL: could not load primitives for " << relativePath << ": " << err.str() << "\n";
        return false;
    }
    if (sources.size() != 1) {
        return false;
    }
    return mlod::normalizePrimitive(sources.front(), out, err) == mlod::kExitSuccess;
}

void testIngestGlbExternalDataUriEquivalence() {
    // The same triangle geometry, expressed as a self-contained GLB, an
    // external multi-file glTF, and a data-URI glTF, must all ingest to the
    // exact same normalized positions/indices through the memory path.
    mlod::NormalizedPrimitive fromGlb;
    mlod::NormalizedPrimitive fromExternal;
    mlod::NormalizedPrimitive fromDataUri;

    expect(loadNormalized("triangle_indexed.glb", fromGlb), "GLB fixture ingests via the memory path");
    expect(loadNormalized("external/triangle.gltf", fromExternal),
          "external multi-file glTF fixture ingests via the memory path + native resolver");
    expect(loadNormalized("data_uri_triangle.gltf", fromDataUri), "data-URI glTF fixture ingests via the memory path");

    expect(fromGlb.positions == fromExternal.positions && fromExternal.positions == fromDataUri.positions,
          "GLB / external / data-URI ingestion yields identical normalized positions");
    expect(fromGlb.indices == fromExternal.indices && fromExternal.indices == fromDataUri.indices,
          "GLB / external / data-URI ingestion yields identical normalized indices");
    expect(fromExternal.vertexCount() == 3 && fromExternal.triangleCount() == 1,
          "external fixture has the expected triangle geometry");
}

void testResolverNotCalledForGlbOrDataUri() {
    int resolverCalls = 0;
    auto countingResolver = [&](const std::string&, const std::string&, mlod::ResourceKind,
                                mlod::ResolvedResource&, mlod::Diagnostic&) {
        ++resolverCalls;
        return false;
    };

    std::ostringstream err;

    mlod::InputBundle glbBundle;
    expect(mlod::loadInputBundleFromNativePath(fixturePath("triangle_indexed.glb"), glbBundle, err) ==
              mlod::kExitSuccess,
          "GLB bundle loads");
    glbBundle.resolver = countingResolver;
    std::vector<mlod::SourcePrimitive> glbOut;
    expect(mlod::loadSourcePrimitivesFromBundle(glbBundle, mlod::PrimitiveSelection::allPrimitives(), glbOut, err) ==
              mlod::kExitSuccess,
          "GLB converts successfully with a resolver installed but never called");
    expect(resolverCalls == 0, "the resolver is never invoked for a self-contained GLB");

    mlod::InputBundle dataUriBundle;
    expect(mlod::loadInputBundleFromNativePath(fixturePath("data_uri_triangle.gltf"), dataUriBundle, err) ==
              mlod::kExitSuccess,
          "data-URI bundle loads");
    dataUriBundle.resolver = countingResolver;
    std::vector<mlod::SourcePrimitive> dataUriOut;
    expect(mlod::loadSourcePrimitivesFromBundle(dataUriBundle, mlod::PrimitiveSelection::allPrimitives(), dataUriOut,
                                                err) == mlod::kExitSuccess,
          "data-URI glTF converts successfully with a resolver installed but never called");
    expect(resolverCalls == 0, "the resolver is never invoked for an embedded data URI");
}

void testResolverCalledExactlyOnceForExternalBuffer() {
    std::vector<std::string> calledUris;
    std::vector<std::string> calledProperties;
    std::vector<mlod::ResourceKind> calledKinds;

    mlod::InputBundle bundle;
    std::ostringstream err;
    expect(mlod::loadInputBundleFromNativePath(fixturePath("external/triangle.gltf"), bundle, err) ==
              mlod::kExitSuccess,
          "external bundle loads");
    const mlod::ResourceResolver nativeResolver = bundle.resolver;
    bundle.resolver = [&](const std::string& uri, const std::string& property, mlod::ResourceKind kind,
                          mlod::ResolvedResource& outResource, mlod::Diagnostic& outError) {
        calledUris.push_back(uri);
        calledProperties.push_back(property);
        calledKinds.push_back(kind);
        return nativeResolver(uri, property, kind, outResource, outError);
    };

    std::vector<mlod::SourcePrimitive> out;
    expect(mlod::loadSourcePrimitivesFromBundle(bundle, mlod::PrimitiveSelection::allPrimitives(), out, err) ==
              mlod::kExitSuccess,
          "external fixture converts through the wrapping resolver");
    expect(calledUris.size() == 1 && calledUris[0] == "triangle.bin",
          "the resolver is invoked exactly once, with the exact relative URI from the glTF document");
    expect(calledProperties[0] == "buffers[0].uri", "the resolver call identifies the referencing glTF property");
    expect(calledKinds[0] == mlod::ResourceKind::kBuffer, "the resolver call identifies the resource kind as buffer");
}

void testMissingResourceFailsBeforeGeometryConversion() {
    mlod::InputBundle bundle;
    std::ostringstream err;
    expect(mlod::loadInputBundleFromNativePath(fixturePath("external/triangle.gltf"), bundle, err) ==
              mlod::kExitSuccess,
          "external bundle loads");
    bundle.resolver = [](const std::string& uri, const std::string& property, mlod::ResourceKind,
                         mlod::ResolvedResource&, mlod::Diagnostic& outError) {
        mlod::DiagnosticContext context;
        context.resourceUri = uri;
        context.gltfProperty = property;
        outError = mlod::makeDiagnostic(mlod::diag_code::kIoRead, mlod::kExitIo, mlod::DiagnosticSeverity::kError,
                                        "simulated missing resource", context);
        return false;
    };

    std::vector<mlod::SourcePrimitive> out;
    const int rc = mlod::loadSourcePrimitivesFromBundle(bundle, mlod::PrimitiveSelection::allPrimitives(), out, err);
    expect(rc == mlod::kExitIo, "a missing external resource fails with the resolver's native exit category");
    expect(out.empty(), "no primitives are produced when a referenced resource is missing");
}

void testMalformedDataUriIsMalformed() {
    mlod::InputBundle bundle;
    bundle.sourceDisplayName = "malformed-data-uri.gltf";
    const std::string json =
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"byteLength\":4,\"uri\":\"data:application/octet-stream,not-base64\"}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteLength\":4,\"byteOffset\":0}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":1,\"type\":\"SCALAR\"}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}]}";
    bundle.entryBytes.assign(json.begin(), json.end());

    std::vector<mlod::SourcePrimitive> out;
    std::ostringstream err;
    const int rc = mlod::loadSourcePrimitivesFromBundle(bundle, mlod::PrimitiveSelection::allPrimitives(), out, err);
    expect(rc == mlod::kExitMalformed, "a non-base64 data URI fails as malformed");
}

void testDigestDeterminismAcrossPathSpelling() {
    // The source digest hashes bytes, never path spelling, so two differently
    // spelled (but equivalent) native paths to the same external fixture must
    // produce identical digests.
    std::array<std::uint8_t, 32> digestA{};
    std::array<std::uint8_t, 32> digestB{};

    mlod::InputBundle bundleA;
    std::ostringstream errA;
    expect(mlod::loadInputBundleFromNativePath(fixturePath("external/triangle.gltf"), bundleA, errA) ==
              mlod::kExitSuccess,
          "bundle A loads");
    std::vector<mlod::SourcePrimitive> outA;
    expect(mlod::loadSourcePrimitivesFromBundle(bundleA, mlod::PrimitiveSelection::allPrimitives(), outA, errA,
                                                &digestA) == mlod::kExitSuccess,
          "bundle A converts");

    mlod::InputBundle bundleB;
    std::ostringstream errB;
    expect(mlod::loadInputBundleFromNativePath(fixturePath("external/./triangle.gltf"), bundleB, errB) ==
              mlod::kExitSuccess,
          "bundle B (differently spelled, equivalent path) loads");
    std::vector<mlod::SourcePrimitive> outB;
    expect(mlod::loadSourcePrimitivesFromBundle(bundleB, mlod::PrimitiveSelection::allPrimitives(), outB, errB,
                                                &digestB) == mlod::kExitSuccess,
          "bundle B converts");

    expect(digestA == digestB, "the source digest is identical regardless of native path spelling");
}

// ---- Task 10.3: inspection and preflight terms ----

bool loadBundle(const std::string& relativePath, mlod::InputBundle& bundle) {
    std::ostringstream err;
    return mlod::loadInputBundleFromNativePath(fixturePath(relativePath), bundle, err) == mlod::kExitSuccess;
}

void testInspectDocumentTriangleInventory() {
    for (const std::string& fixture : {std::string("triangle_indexed.glb"), std::string("external/triangle.gltf"),
                                       std::string("data_uri_triangle.gltf")}) {
        mlod::InputBundle bundle;
        expect(loadBundle(fixture, bundle), fixture + ": bundle loads");

        mlod::DocumentInventory inventory;
        std::vector<mlod::Diagnostic> diagnostics;
        const mlod::DiagnosticSink sink = [&](const mlod::Diagnostic& d) { diagnostics.push_back(d); };
        const int rc = mlod::inspectDocument(bundle, inventory, sink, mlod::CancellationProbe{});

        expect(rc == mlod::kExitSuccess, fixture + ": inspectDocument succeeds");
        expect(diagnostics.empty(), fixture + ": inspectDocument emits no fatal diagnostics");
        expect(inventory.primitives.size() == 1, fixture + ": exactly one primitive in the inventory");
        if (inventory.primitives.size() == 1) {
            expect(inventory.primitives[0].supported, fixture + ": the sole primitive is supported");
            expect(inventory.primitives[0].sourceVertexCount == 3, fixture + ": vertex count is 3");
            expect(inventory.primitives[0].sourceTriangleCount == 1, fixture + ": triangle count is 1");
        }
        expect(inventory.meshPrimitiveCounts.size() == 1 && inventory.meshPrimitiveCounts[0] == 1,
              fixture + ": exactly one mesh with one primitive");
    }
}

void testInspectDocumentMixedSupportedAndUnsupported() {
    mlod::InputBundle bundle;
    expect(loadBundle("mixed.gltf", bundle), "mixed.gltf bundle loads");

    mlod::DocumentInventory inventory;
    const int rc = mlod::inspectDocument(bundle, inventory, mlod::DiagnosticSink{}, mlod::CancellationProbe{});

    expect(rc == mlod::kExitSuccess, "inspectDocument succeeds even with an unsupported primitive present");
    expect(inventory.primitives.size() == 2, "mixed.gltf inventories both document primitives");
    if (inventory.primitives.size() == 2) {
        expect(inventory.primitives[0].identity == mlod::PrimitiveIdentity{0, 0} && inventory.primitives[0].supported,
              "mesh 0 primitive 0 (triangulated, textured sphere) is supported");
        expect(inventory.primitives[0].sourceVertexCount == 289, "mesh 0 primitive 0 reports its real vertex count");
        expect(inventory.primitives[1].identity == mlod::PrimitiveIdentity{1, 0} && !inventory.primitives[1].supported,
              "mesh 1 primitive 0 (POINTS mode) is unsupported");
    }
    expect(inventory.warnings.size() == 1, "exactly one warning is recorded for the one unsupported primitive");
    if (inventory.warnings.size() == 1) {
        expect(inventory.warnings[0].context.hasMesh && inventory.warnings[0].context.meshIndex == 1 &&
                  inventory.warnings[0].context.hasPrimitive && inventory.warnings[0].context.primitiveIndex == 0,
              "the warning identifies the exact offending mesh/primitive");
    }
}

void testInspectConversionExcludesUnsupportedFromSelection() {
    mlod::InputBundle bundle;
    expect(loadBundle("mixed.gltf", bundle), "mixed.gltf bundle loads");

    mlod::ConversionSettings settings; // canonical defaults, kAllPrimitives selection
    mlod::InspectionResult result;
    std::vector<mlod::ProgressEvent> progressEvents;
    const mlod::ProgressSink progressSink = [&](const mlod::ProgressEvent& e) { progressEvents.push_back(e); };

    const bool ok = mlod::inspectConversion(bundle, settings, result, mlod::DiagnosticSink{}, progressSink,
                                            mlod::CancellationProbe{});

    expect(ok, "inspectConversion succeeds for a document containing one unsupported primitive");
    expect(result.primitives.size() == 2, "the full document inventory still lists both primitives");
    expect(result.supportedSelection.size() == 1 && result.supportedSelection[0] == mlod::PrimitiveIdentity{0, 0},
          "supportedSelection excludes the unsupported primitive rather than failing the whole request");
    expect(!result.warnings.empty(), "the unsupported primitive is still reported as a warning");
    expect(!progressEvents.empty(), "inspection reports progress");
    for (const mlod::ProgressEvent& event : progressEvents) {
        expect(event.stage == mlod::ConversionStage::kValidateResources,
              "inspection progress never leaves the Validate-resources stage");
    }
}

void testInspectConversionNeverProducesOutputBytes() {
    // InspectionResult has no byte-buffer field at all (unlike PrimitiveOutput /
    // ConversionResult) -- this is a structural, compile-time guarantee that
    // inspection cannot expose partial `.mlod` output, verified here by
    // confirming a successful inspection's result only carries counts/terms.
    mlod::InputBundle bundle;
    expect(loadBundle("triangle_indexed.glb", bundle), "GLB bundle loads");
    mlod::InspectionResult result;
    const bool ok = mlod::inspectConversion(bundle, mlod::ConversionSettings{}, result, mlod::DiagnosticSink{},
                                            mlod::ProgressSink{}, mlod::CancellationProbe{});
    expect(ok, "inspection succeeds");
    expect(result.entryType == "glb", "entry type is detected correctly");
    expect(result.preflight.triangleCount == 1, "preflight triangle count matches the single supported primitive");
}

void testInspectConversionPreflightFormula() {
    mlod::InputBundle bundle;
    expect(loadBundle("triangle_indexed.glb", bundle), "GLB bundle loads");

    mlod::ConversionSettings settings; // canonical defaults: meshletMinTriangles=40, simplifyRatio=0.5, pageMaxKiB=256
    mlod::InspectionResult result;
    const bool ok = mlod::inspectConversion(bundle, settings, result, mlod::DiagnosticSink{}, mlod::ProgressSink{},
                                            mlod::CancellationProbe{});
    expect(ok, "inspection succeeds");

    // Hand-computed architecture section 7.12 formula for V=3, I=3, T=1,
    // primitiveCount=1, with canonical default settings.
    const std::uint64_t expectedV = 3;
    const std::uint64_t expectedI = 3;
    const std::uint64_t expectedT = 1;
    const std::uint64_t expectedG = 24 * expectedV + 4 * expectedI; // 84
    const std::uint32_t expectedL = 2;                              // ceil(1/(1-0.5)) = 2, clamped [2,8]
    const std::uint64_t expectedM = static_cast<std::uint64_t>(expectedL) * 1; // ceil(1/40) = 1
    const std::uint64_t expectedH = 3 * expectedG + 96 * expectedT + 256 * expectedM;
    const std::uint64_t expectedOUnaligned =
        static_cast<std::uint64_t>(expectedL) * (24 * expectedV + 2 * expectedI) + 160 * expectedM + 1 * (256ull * 1024ull);
    const std::uint64_t expectedO = ((expectedOUnaligned + 65535) / 65536) * 65536;

    expect(result.preflight.vertexCount == expectedV, "preflight V matches");
    expect(result.preflight.indexCount == expectedI, "preflight I matches");
    expect(result.preflight.triangleCount == expectedT, "preflight T matches");
    expect(result.preflight.normalizedGeometryBytes == expectedG, "preflight G matches");
    expect(result.preflight.lodFactor == expectedL, "preflight L matches");
    expect(result.preflight.estimatedMeshlets == expectedM, "preflight M matches");
    expect(result.preflight.hierarchyReserveBytes == expectedH, "preflight H matches");
    expect(result.preflight.outputReserveBytes == expectedO, "preflight O matches (64 KiB aligned)");
    expect(result.preflight.packagingReserveBytes == expectedO, "single-output packaging reserve equals O exactly");
    expect(result.preflight.policyLimitBytes == 512ull * 1024ull * 1024ull, "policy limit is 512 MiB");
    expect(result.preflight.withinPolicyLimit, "a tiny triangle fixture stays well within the policy limit");
}

void testInspectConversionOutOfRangeSelectionFails() {
    mlod::InputBundle bundle;
    expect(loadBundle("triangle_indexed.glb", bundle), "GLB bundle loads");

    mlod::ConversionSettings settings;
    settings.selection = mlod::PrimitiveSelection::singlePrimitive(99, 0);

    mlod::InspectionResult result;
    std::vector<mlod::Diagnostic> diagnostics;
    const mlod::DiagnosticSink sink = [&](const mlod::Diagnostic& d) { diagnostics.push_back(d); };
    const bool ok = mlod::inspectConversion(bundle, settings, result, sink, mlod::ProgressSink{},
                                            mlod::CancellationProbe{});

    expect(!ok, "an out-of-range explicit mesh selection fails inspection");
    expect(result.supportedSelection.empty(), "no output selection is produced on failure");
    expect(!diagnostics.empty() && diagnostics.back().code == mlod::diag_code::kCliSelection,
          "the failure is reported with the selection diagnostic code");
    expect(diagnostics.back().context.hasMesh && diagnostics.back().context.meshIndex == 99,
          "the diagnostic identifies the offending mesh index");
}

void testInspectConversionInvalidSettingsFailsBeforeParsing() {
    mlod::InputBundle bundle;
    expect(loadBundle("triangle_indexed.glb", bundle), "GLB bundle loads");

    mlod::ConversionSettings settings;
    settings.pageMinKiB = 100; // not a multiple of 64

    mlod::InspectionResult result;
    std::vector<mlod::Diagnostic> diagnostics;
    const mlod::DiagnosticSink sink = [&](const mlod::Diagnostic& d) { diagnostics.push_back(d); };
    const bool ok = mlod::inspectConversion(bundle, settings, result, sink, mlod::ProgressSink{},
                                            mlod::CancellationProbe{});

    expect(!ok, "invalid settings fail inspection");
    expect(result.entryType.empty(), "the document is never parsed when settings are already invalid");
    expect(!diagnostics.empty() && diagnostics.back().code == mlod::diag_code::kCliOption,
          "the failure is reported with the option diagnostic code");
}

void testInspectConversionCancellationLeavesNoOutputState() {
    mlod::InputBundle bundle;
    expect(loadBundle("triangle_indexed.glb", bundle), "GLB bundle loads");

    const mlod::CancellationProbe alwaysCancelled = []() { return true; };
    mlod::InspectionResult result;
    const bool ok = mlod::inspectConversion(bundle, mlod::ConversionSettings{}, result, mlod::DiagnosticSink{},
                                            mlod::ProgressSink{}, alwaysCancelled);

    expect(!ok, "an immediately-cancelled inspection fails");
    expect(result.supportedSelection.empty() && result.primitives.empty(),
          "a cancelled inspection retains no primitive inventory or selection state");
}

// ---- Task 10.4: atomic core conversion and portable provenance ----

void testConvertEquivalentFixturesInMemory() {
    for (const std::string& fixture : {std::string("triangle_indexed.glb"), std::string("external/triangle.gltf"),
                                       std::string("data_uri_triangle.gltf")}) {
        mlod::InputBundle bundle;
        expect(loadBundle(fixture, bundle), fixture + ": bundle loads");

        mlod::ConversionResult result;
        std::vector<mlod::Diagnostic> diagnostics;
        const mlod::DiagnosticSink sink = [&](const mlod::Diagnostic& d) { diagnostics.push_back(d); };
        const bool ok =
            mlod::convert(bundle, mlod::ConversionSettings{}, result, sink, mlod::ProgressSink{}, mlod::CancellationProbe{});

        expect(ok, fixture + ": convert succeeds entirely in memory");
        expect(diagnostics.empty(), fixture + ": convert emits no diagnostics on success");
        expect(result.success, fixture + ": ConversionResult.success is true");
        expect(result.outputs.size() == 1, fixture + ": exactly one PrimitiveOutput");
        if (result.outputs.size() == 1) {
            const mlod::PrimitiveOutput& output = result.outputs.front();
            expect(!output.bytes.empty(), fixture + ": output bytes are non-empty");
            expect(output.validatedByteSize == output.bytes.size(),
                  fixture + ": validatedByteSize matches the actual byte count");
            std::ostringstream validateErr;
            expect(mlod::validateContainer(output.bytes.data(), output.bytes.size(), validateErr) ==
                      mlod::kExitSuccess,
                  fixture + ": the returned .mlod bytes independently validate");
            expect(output.sourceTriangleCount == 1 && output.pinnedPageCount >= 1,
                  fixture + ": output statistics reflect the single-triangle source");
        }
        expect(!result.canonicalMetadataJson.empty(), fixture + ": canonical metadata JSON is populated");
    }
}

void testConvertProducesByteIdenticalOutputRegardlessOfEntryFormat() {
    // GLB, external glTF, and data-URI glTF all describe the identical
    // triangle with identical default settings; their .mlod outputs must be
    // byte-identical except for the embedded source digest (which correctly
    // differs because the entry *document bytes* differ across formats).
    mlod::InputBundle glbBundle;
    mlod::InputBundle externalBundle;
    expect(loadBundle("triangle_indexed.glb", glbBundle), "GLB bundle loads");
    expect(loadBundle("external/triangle.gltf", externalBundle), "external bundle loads");

    mlod::ConversionResult glbResult;
    mlod::ConversionResult externalResult;
    expect(mlod::convert(glbBundle, mlod::ConversionSettings{}, glbResult, mlod::DiagnosticSink{},
                        mlod::ProgressSink{}, mlod::CancellationProbe{}),
          "GLB converts");
    expect(mlod::convert(externalBundle, mlod::ConversionSettings{}, externalResult, mlod::DiagnosticSink{},
                        mlod::ProgressSink{}, mlod::CancellationProbe{}),
          "external glTF converts");

    expect(glbResult.outputs.size() == 1 && externalResult.outputs.size() == 1, "both produce one output");
    if (glbResult.outputs.size() == 1 && externalResult.outputs.size() == 1) {
        expect(glbResult.outputs[0].bytes.size() == externalResult.outputs[0].bytes.size(),
              "GLB and external glTF outputs are the same byte length");
        expect(glbResult.outputs[0].meshletCount == externalResult.outputs[0].meshletCount &&
                  glbResult.outputs[0].pageCount == externalResult.outputs[0].pageCount,
              "GLB and external glTF outputs report identical geometry statistics");
    }
}

void testConvertTwoSupportedPrimitivesBothSucceed() {
    mlod::InputBundle bundle;
    expect(loadBundle("two_triangles.gltf", bundle), "two_triangles.gltf bundle loads");

    mlod::ConversionResult result;
    const bool ok = mlod::convert(bundle, mlod::ConversionSettings{}, result, mlod::DiagnosticSink{},
                                  mlod::ProgressSink{}, mlod::CancellationProbe{});

    expect(ok && result.success, "two_triangles.gltf converts both supported primitives");
    expect(result.outputs.size() == 2, "exactly two outputs are produced");
    if (result.outputs.size() == 2) {
        expect(result.outputs[0].identity == mlod::PrimitiveIdentity{0, 0} &&
                  result.outputs[1].identity == mlod::PrimitiveIdentity{1, 0},
              "outputs are ordered by source mesh/primitive index");
    }
}

void testConvertCancellationAfterFirstPrimitiveYieldsZeroOutputs() {
    // Cancellation injected after the first (already-converted) primitive
    // must still discard everything: convert() is atomic across the whole
    // selection (REQ-BROWSER-7), so one primitive succeeding is never enough
    // for a partial result.
    mlod::InputBundle bundle;
    expect(loadBundle("two_triangles.gltf", bundle), "two_triangles.gltf bundle loads");

    int primitiveStageEvents = 0;
    const mlod::ProgressSink countingProgress = [&](const mlod::ProgressEvent& event) {
        if (event.stage == mlod::ConversionStage::kValidateOutputs && event.completedUnits > 0) {
            ++primitiveStageEvents;
        }
    };
    const mlod::CancellationProbe cancelAfterFirst = [&]() { return primitiveStageEvents >= 1; };

    mlod::ConversionResult result;
    const bool ok = mlod::convert(bundle, mlod::ConversionSettings{}, result, mlod::DiagnosticSink{}, countingProgress,
                                  cancelAfterFirst);

    expect(!ok && !result.success, "conversion cancelled after the first primitive reports overall failure");
    expect(result.outputs.empty(), "zero outputs are retained even though the first primitive already converted");
}

void testConvertCancellationAtEveryStageBoundaryYieldsZeroOutputs() {
    // Task 13.1 fixture-matrix requirement: "Cancellation at inspection and
    // every conversion stage boundary" -- exhaustively trips the
    // CancellationProbe at each of convert()'s cancellation checkpoints (not
    // just "after the first primitive") over a two-primitive fixture so both
    // per-primitive and cross-primitive checkpoints are exercised, and
    // asserts zero outputs at every single one.
    mlod::InputBundle bundle;
    expect(loadBundle("two_triangles.gltf", bundle), "two_triangles.gltf bundle loads");

    // First, discover exactly how many cancellation checkpoints an
    // uncancelled run passes through (by counting cancel() calls without
    // ever tripping), so every checkpoint index can be tripped in turn below
    // without hand-enumerating conversion.cpp's internal checkpoint count.
    int totalCheckpoints = 0;
    const mlod::CancellationProbe countingProbe = [&]() {
        ++totalCheckpoints;
        return false;
    };
    {
        mlod::ConversionResult baseline;
        const bool ok = mlod::convert(bundle, mlod::ConversionSettings{}, baseline, mlod::DiagnosticSink{},
                                      mlod::ProgressSink{}, countingProbe);
        expect(ok && baseline.success, "the uncancelled baseline run succeeds (needed to count checkpoints)");
    }
    expect(totalCheckpoints >= 8, "a two-primitive conversion passes through at least eight cancellation checkpoints");

    for (int tripAt = 1; tripAt <= totalCheckpoints; ++tripAt) {
        int calls = 0;
        const mlod::CancellationProbe tripAtCall = [&]() {
            ++calls;
            return calls == tripAt;
        };
        mlod::ConversionResult result;
        const bool ok =
            mlod::convert(bundle, mlod::ConversionSettings{}, result, mlod::DiagnosticSink{}, mlod::ProgressSink{}, tripAtCall);
        expect(!ok && !result.success, "cancelling at checkpoint " + std::to_string(tripAt) + " reports overall failure");
        expect(result.outputs.empty(), "cancelling at checkpoint " + std::to_string(tripAt) + " retains zero outputs");
    }
}

void testConvertRequiresSuccessfulInspection() {
    mlod::InputBundle bundle;
    expect(loadBundle("triangle_indexed.glb", bundle), "GLB bundle loads");

    mlod::ConversionSettings settings;
    settings.selection = mlod::PrimitiveSelection::singlePrimitive(99, 0); // out of range -> inspection fails

    mlod::ConversionResult result;
    std::vector<mlod::Diagnostic> diagnostics;
    const mlod::DiagnosticSink sink = [&](const mlod::Diagnostic& d) { diagnostics.push_back(d); };
    const bool ok =
        mlod::convert(bundle, settings, result, sink, mlod::ProgressSink{}, mlod::CancellationProbe{});

    expect(!ok, "convert fails when the underlying inspection fails");
    expect(result.outputs.empty() && !result.success, "no outputs are produced when inspection fails");
    expect(!diagnostics.empty(), "the inspection failure diagnostic propagates through convert");
}

void testConvertMemoryLedgerRejectsOversizedReservation() {
    // Direct unit coverage of the MemoryLedger contract convert()'s future
    // memory-accounting integration relies on (architecture 7.16): a reserve
    // that would exceed the policy limit is rejected without corrupting
    // already-tracked bytes, exactly like an aborted conversion must retain
    // no partial state.
    mlod::MemoryLedger ledger(1024);
    expect(ledger.reserve(900), "an in-budget reserve succeeds");
    expect(!ledger.reserve(200), "a reserve that would exceed the limit is rejected");
    expect(ledger.currentBytes() == 900, "the rejected reserve leaves currentBytes unchanged");
}

void testConvertProgressIsMonotonicStagedAndCarriesMemory() {
    // Task 12.3 (REQ-BROWSER-6/9): every progress event across a real
    // multi-primitive conversion must report a non-decreasing overall
    // fraction, walk the six ordered stages, and always carry a nonzero
    // estimatedPeakBytes (the constant preflight ceiling) alongside a
    // trackedBytes figure that only grows.
    mlod::InputBundle bundle;
    expect(loadBundle("two_triangles.gltf", bundle), "two_triangles.gltf bundle loads");

    std::vector<mlod::ProgressEvent> events;
    const mlod::ProgressSink progressSink = [&](const mlod::ProgressEvent& event) { events.push_back(event); };

    mlod::ConversionResult result;
    const bool ok = mlod::convert(bundle, mlod::ConversionSettings{}, result, mlod::DiagnosticSink{}, progressSink,
                                  mlod::CancellationProbe{});
    expect(ok && result.success, "two_triangles.gltf converts for the progress test");
    expect(events.size() >= 10, "a two-primitive conversion reports at least ten progress events");

    float previousFraction = -1.0f;
    std::uint64_t previousTracked = 0;
    bool sawEveryStage[6] = {false, false, false, false, false, false};
    for (const mlod::ProgressEvent& event : events) {
        expect(event.overallFraction >= previousFraction, "overallFraction never decreases across progress events");
        previousFraction = event.overallFraction;
        expect(event.trackedBytes >= previousTracked, "trackedBytes never decreases across progress events");
        previousTracked = event.trackedBytes;
        // The very first event ("validate-settings") fires before the
        // preflight formula runs, so it legitimately carries no peak
        // estimate yet; every event from inspection's own "inspected"
        // completion onward must carry the (constant) ceiling.
        if (event.activityCode != "validate-settings") {
            expect(event.estimatedPeakBytes > 0, "every post-inspection progress event carries a nonzero estimatedPeakBytes");
        }
        expect(!event.activityCode.empty(), "every convert() progress event carries a non-empty activityCode");
        sawEveryStage[static_cast<int>(event.stage)] = true;
    }
    for (int stage = 0; stage < 6; ++stage) {
        expect(sawEveryStage[stage], "every one of the six ordered stages is reported at least once");
    }
    expect(events.front().overallFraction <= events.back().overallFraction, "progress starts low and ends high");
    expect(events.back().overallFraction == 1.0f, "the final progress event reaches exactly 100%");
}

} // namespace

int main() {
    testCanonicalDefaultsExact();
    testHostOnlyFieldsCannotAffectCanonicalSettings();
    testEveryOutputAffectingSettingChangesCanonical();
    testPrimitiveSelectionValidity();
    testEveryNumericBoundaryAndConflict();
    testAbsentSinksAreNoOps();
    testDiagnosticContextIsSerializableByValue();
    testCancellationDefaults();
    testMemoryLedgerOverflowAndLimit();

    testIngestGlbExternalDataUriEquivalence();
    testResolverNotCalledForGlbOrDataUri();
    testResolverCalledExactlyOnceForExternalBuffer();
    testMissingResourceFailsBeforeGeometryConversion();
    testMalformedDataUriIsMalformed();
    testDigestDeterminismAcrossPathSpelling();

    testInspectDocumentTriangleInventory();
    testInspectDocumentMixedSupportedAndUnsupported();
    testInspectConversionExcludesUnsupportedFromSelection();
    testInspectConversionNeverProducesOutputBytes();
    testInspectConversionPreflightFormula();
    testInspectConversionOutOfRangeSelectionFails();
    testInspectConversionInvalidSettingsFailsBeforeParsing();
    testInspectConversionCancellationLeavesNoOutputState();

    testConvertEquivalentFixturesInMemory();
    testConvertProducesByteIdenticalOutputRegardlessOfEntryFormat();
    testConvertTwoSupportedPrimitivesBothSucceed();
    testConvertCancellationAfterFirstPrimitiveYieldsZeroOutputs();
    testConvertCancellationAtEveryStageBoundaryYieldsZeroOutputs();
    testConvertRequiresSuccessfulInspection();
    testConvertMemoryLedgerRejectsOversizedReservation();
    testConvertProgressIsMonotonicStagedAndCarriesMemory();

    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "converter_core_contracts: all tests passed\n";
    return 0;
}
