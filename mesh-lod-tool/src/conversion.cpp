#include "conversion.h"

#include "hierarchy.h"
#include "input.h"
#include "mlod_writer.h"
#include "normalize.h"
#include "page_packer.h"
#include "statistics.h"
#include "validator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <utility>

namespace mlod {
namespace {

constexpr std::uint64_t kKiB = 1024ull;
constexpr std::uint64_t kMiB = 1024ull * kKiB;
constexpr std::uint64_t kPolicyLimitBytes = 512ull * kMiB;
constexpr std::uint64_t kFixedOverheadBytes = 32ull * kMiB;

std::uint64_t align64KiB(std::uint64_t bytes) {
    constexpr std::uint64_t kAlign = 64ull * kKiB;
    return ((bytes + kAlign - 1) / kAlign) * kAlign;
}

// Resolves `selection` against per-mesh primitive counts, using the exact
// same mesh/primitive-index rules as the native CLI/converter core
// (input.cpp's resolveSelection). Kept cgltf-free so conversion.cpp never
// needs to touch cgltf types directly -- input.cpp already validated every
// count while building the inventory.
bool resolveInventorySelection(const std::vector<std::uint32_t>& meshPrimitiveCounts,
                               const PrimitiveSelection& selection, std::vector<PrimitiveIdentity>& out,
                               const DiagnosticSink& diagnostics) {
    out.clear();
    auto meshOutOfRange = [&](std::uint32_t meshIndex) {
        DiagnosticContext context;
        context.hasMesh = true;
        context.meshIndex = meshIndex;
        emitDiagnostic(diagnostics, makeDiagnostic(diag_code::kCliSelection, kExitCli, DiagnosticSeverity::kError,
                                                   "mesh index is out of range", context));
    };
    auto primitiveOutOfRange = [&](std::uint32_t meshIndex, std::uint32_t primitiveIndex) {
        DiagnosticContext context;
        context.hasMesh = true;
        context.meshIndex = meshIndex;
        context.hasPrimitive = true;
        context.primitiveIndex = primitiveIndex;
        emitDiagnostic(diagnostics, makeDiagnostic(diag_code::kCliSelection, kExitCli, DiagnosticSeverity::kError,
                                                   "primitive index is out of range", context));
    };

    switch (selection.mode) {
    case PrimitiveSelectionMode::kSinglePrimitive: {
        if (selection.meshIndex >= meshPrimitiveCounts.size()) {
            meshOutOfRange(selection.meshIndex);
            return false;
        }
        if (selection.primitiveIndex >= meshPrimitiveCounts[selection.meshIndex]) {
            primitiveOutOfRange(selection.meshIndex, selection.primitiveIndex);
            return false;
        }
        out.push_back({selection.meshIndex, selection.primitiveIndex});
        return true;
    }
    case PrimitiveSelectionMode::kWholeMesh: {
        if (selection.meshIndex >= meshPrimitiveCounts.size()) {
            meshOutOfRange(selection.meshIndex);
            return false;
        }
        for (std::uint32_t p = 0; p < meshPrimitiveCounts[selection.meshIndex]; ++p) {
            out.push_back({selection.meshIndex, p});
        }
        return true;
    }
    case PrimitiveSelectionMode::kAllPrimitives:
    default:
        for (std::uint32_t m = 0; m < meshPrimitiveCounts.size(); ++m) {
            for (std::uint32_t p = 0; p < meshPrimitiveCounts[m]; ++p) {
                out.push_back({m, p});
            }
        }
        return true;
    }
}

// Maps a stage ExitCode to its stable diagnostic code family (architecture
// section 7.18).
const char* diagCodeForExitCode(ExitCode code) {
    switch (code) {
    case kExitCli:
        return diag_code::kCliOption;
    case kExitIo:
        return diag_code::kIoRead;
    case kExitMalformed:
        return diag_code::kMalformed;
    case kExitUnsupported:
        return diag_code::kUnsupported;
    case kExitHierarchy:
        return diag_code::kHierarchy;
    case kExitValidation:
        return diag_code::kOutputValidation;
    case kExitWrite:
        return diag_code::kWrite;
    case kExitSuccess:
    default:
        return diag_code::kMalformed;
    }
}

void reportStageFailure(const DiagnosticSink& diagnostics, int rc, const std::string& message) {
    const ExitCode code = static_cast<ExitCode>(rc);
    emitDiagnostic(diagnostics, makeDiagnostic(diagCodeForExitCode(code), code, DiagnosticSeverity::kError, message));
}

} // namespace

bool inspectConversion(const InputBundle& bundle, const ConversionSettings& settings, InspectionResult& out,
                       const DiagnosticSink& diagnostics, const ProgressSink& progress,
                       const CancellationProbe& cancel) {
    out = InspectionResult{};

    ProgressEvent startEvent;
    startEvent.stage = ConversionStage::kValidateResources;
    startEvent.activityCode = "validate-settings";
    emitProgress(progress, startEvent);

    if (!validateConversionSettings(settings, diagnostics)) {
        return false;
    }

    if (cancel && cancel()) {
        return false;
    }

    DocumentInventory inventory;
    const int rc = inspectDocument(bundle, inventory, diagnostics, cancel);
    if (rc != kExitSuccess) {
        return false;
    }

    out.entryType = inventory.entryType;
    out.resolvedResourcePaths = inventory.resolvedResourcePaths;
    out.primitives = inventory.primitives;
    out.warnings = inventory.warnings;

    std::vector<PrimitiveIdentity> resolvedSelection;
    if (!resolveInventorySelection(inventory.meshPrimitiveCounts, settings.selection, resolvedSelection,
                                   diagnostics)) {
        return false;
    }

    if (cancel && cancel()) {
        return false;
    }

    std::uint64_t totalVertices = 0;
    std::uint64_t totalTriangles = 0;
    for (const PrimitiveIdentity& identity : resolvedSelection) {
        const auto it = std::find_if(inventory.primitives.begin(), inventory.primitives.end(),
                                     [&](const InspectedPrimitive& primitive) { return primitive.identity == identity; });
        if (it == inventory.primitives.end() || !it->supported) {
            continue; // unsupported primitives inside the selection are excluded, not fatal
        }
        out.supportedSelection.push_back(identity);
        totalVertices += it->sourceVertexCount;
        totalTriangles += it->sourceTriangleCount;
    }
    const std::uint64_t totalIndices = totalTriangles * 3;

    out.sourceVertexCount = totalVertices;
    out.sourceTriangleCount = totalTriangles;
    out.sourceIndexCount = totalIndices;

    // Architecture section 7.12 preflight formula, computed only over the
    // supported subset of the requested selection (what will actually run
    // through convert()).
    PreflightTerms& terms = out.preflight;
    terms.selectedBytes = inventory.selectedBytes;     // F
    terms.copiedBytes = inventory.copiedResourceBytes; // B
    terms.vertexCount = totalVertices;                 // V
    terms.indexCount = totalIndices;                   // I
    terms.triangleCount = totalTriangles;               // T
    terms.normalizedGeometryBytes = 24ull * totalVertices + 4ull * totalIndices; // G

    const float clampedRatio = std::min(settings.simplifyRatio, 0.875f);
    std::uint32_t lodFactor = static_cast<std::uint32_t>(std::ceil(1.0f / (1.0f - clampedRatio)));
    lodFactor = std::max<std::uint32_t>(2, std::min<std::uint32_t>(8, lodFactor));
    terms.lodFactor = lodFactor; // L

    const std::uint32_t minTriangles = std::max<std::uint32_t>(settings.meshletMinTriangles, 1);
    terms.estimatedMeshlets =
        static_cast<std::uint64_t>(lodFactor) * ((totalTriangles + minTriangles - 1) / minTriangles); // M

    terms.hierarchyReserveBytes =
        3ull * terms.normalizedGeometryBytes + 96ull * totalTriangles + 256ull * terms.estimatedMeshlets; // H

    const std::uint64_t primitiveCount = out.supportedSelection.size();
    const std::uint64_t pageMaxBytes = static_cast<std::uint64_t>(settings.pageMaxKiB) * kKiB;
    terms.outputReserveBytes =
        align64KiB(static_cast<std::uint64_t>(lodFactor) * (24ull * totalVertices + 2ull * totalIndices) +
                  160ull * terms.estimatedMeshlets + primitiveCount * pageMaxBytes); // O

    // Packaging reserve: O for a single downloadable output, O plus ZIP
    // central-directory/metadata overhead when there is more than one output
    // (architecture section 7.12, symbol P).
    terms.packagingReserveBytes = (primitiveCount <= 1)
                                     ? terms.outputReserveBytes
                                     : terms.outputReserveBytes + 128ull * (primitiveCount + 1) + 64ull * kKiB; // P

    terms.estimatedPeakBytes = align64KiB(terms.selectedBytes + terms.copiedBytes + terms.hierarchyReserveBytes +
                                         terms.outputReserveBytes + terms.packagingReserveBytes +
                                         kFixedOverheadBytes);
    terms.policyLimitBytes = kPolicyLimitBytes;
    terms.withinPolicyLimit = terms.estimatedPeakBytes <= kPolicyLimitBytes;

    ProgressEvent doneEvent;
    doneEvent.stage = ConversionStage::kValidateResources;
    doneEvent.activityCode = "inspected";
    doneEvent.completedUnits = 1;
    doneEvent.totalUnits = 1;
    doneEvent.overallFraction = 0.1f;
    doneEvent.trackedBytes = terms.selectedBytes + terms.copiedBytes;
    doneEvent.estimatedPeakBytes = terms.estimatedPeakBytes;
    emitProgress(progress, doneEvent);

    return true;
}

bool convert(const InputBundle& bundle, const ConversionSettings& settings, ConversionResult& out,
            const DiagnosticSink& diagnostics, const ProgressSink& progress, const CancellationProbe& cancel) {
    out = ConversionResult{};

    InspectionResult inspection;
    if (!inspectConversion(bundle, settings, inspection, diagnostics, progress, cancel)) {
        return false; // inspectConversion already emitted the failing diagnostic
    }
    if (inspection.supportedSelection.empty()) {
        emitDiagnostic(diagnostics, makeDiagnostic(diag_code::kUnsupported, kExitUnsupported,
                                                   DiagnosticSeverity::kError,
                                                   "no supported primitive is selected for conversion"));
        return false;
    }
    if (cancel && cancel()) {
        return false;
    }

    std::vector<PrimitiveOutput> outputs;
    std::vector<PrimitiveHierarchy> hierarchies;
    std::vector<PackedGeometry> packedGeometries;
    outputs.reserve(inspection.supportedSelection.size());
    hierarchies.reserve(inspection.supportedSelection.size());
    packedGeometries.reserve(inspection.supportedSelection.size());

    bool digestCaptured = false;
    std::array<std::uint8_t, 32> sourceDigest{};

    const std::uint64_t totalSourceTriangles = std::max<std::uint64_t>(inspection.sourceTriangleCount, 1);
    std::uint64_t processedTriangles = 0;

    // Every progress message must include a monotonically non-decreasing
    // overall fraction that respects the architecture 7.14 stage weights
    // (Validate resources 0-10%, Normalize geometry 10-25%, Build hierarchy
    // 25-65%, Pack streaming pages 65-82%, Validate outputs 82-94%, Prepare
    // download 94-100%). Primitives run through the four middle stages
    // sequentially (not in cross-primitive batches), so each primitive is
    // given an equal slice of the shared 10%-94% band and the four stage
    // weights are applied *within* that slice, keeping progress strictly
    // non-decreasing across both primitives and stages.
    const std::size_t primitiveCount = inspection.supportedSelection.size();
    const float primitiveBand = 0.84f / static_cast<float>(std::max<std::size_t>(primitiveCount, 1));
    // Cumulative stage-start weights inside one primitive's band, derived
    // from the global 15%/40%/17%/12% (normalize/hierarchy/pack/validate)
    // proportions of the 84-point 10%-94% range.
    constexpr float kNormalizeStartRatio = 0.0f;
    constexpr float kHierarchyStartRatio = 15.0f / 84.0f;
    constexpr float kPackStartRatio = 55.0f / 84.0f;
    constexpr float kValidateStartRatio = 72.0f / 84.0f;

    // Tracked/estimated-peak memory reported alongside every event: the
    // preflight's ceiling estimate stays constant for the whole conversion,
    // while "tracked" grows by each primitive's already-produced output
    // bytes (the only conversion-phase allocation this layer can account for
    // without a JS-side heap probe).
    const std::uint64_t estimatedPeakBytes = inspection.preflight.estimatedPeakBytes;
    std::uint64_t trackedBytes = inspection.preflight.selectedBytes + inspection.preflight.copiedBytes;

    auto emitStage = [&](ConversionStage stage, const char* activityCode, const std::string& context,
                         std::uint64_t completedUnits, std::uint64_t totalUnitsArg, float overallFraction) {
        ProgressEvent event;
        event.stage = stage;
        event.activityCode = activityCode;
        event.context = context;
        event.completedUnits = completedUnits;
        event.totalUnits = totalUnitsArg;
        event.overallFraction = overallFraction;
        event.trackedBytes = trackedBytes;
        event.estimatedPeakBytes = estimatedPeakBytes;
        emitProgress(progress, event);
    };

    for (std::size_t primitiveIndex = 0; primitiveIndex < primitiveCount; ++primitiveIndex) {
        const PrimitiveIdentity& identity = inspection.supportedSelection[primitiveIndex];
        if (cancel && cancel()) {
            return false; // `out` was reset above and never populated -- no partial success
        }

        const std::string context =
            "mesh " + std::to_string(identity.meshIndex) + " primitive " + std::to_string(identity.primitiveIndex);
        const float primitiveBase = 0.10f + primitiveBand * static_cast<float>(primitiveIndex);
        const float primitiveEnd = std::min(0.94f, primitiveBase + primitiveBand);

        emitStage(ConversionStage::kNormalizeGeometry, "normalize-geometry", context, primitiveIndex, primitiveCount,
                 primitiveBase + primitiveBand * kNormalizeStartRatio);

        // Reload just this one primitive: resolveBuffers/validateImages walk
        // the whole document's buffers/images every call regardless of
        // selection, so the resulting sourceDigest is identical no matter
        // which supported identity we ask for first.
        std::vector<SourcePrimitive> sourcePrimitives;
        std::ostringstream loadErr;
        std::array<std::uint8_t, 32> digest{};
        const int loadRc = loadSourcePrimitivesFromBundle(
            bundle, PrimitiveSelection::singlePrimitive(identity.meshIndex, identity.primitiveIndex),
            sourcePrimitives, loadErr, &digest);
        if (loadRc != kExitSuccess || sourcePrimitives.size() != 1) {
            reportStageFailure(diagnostics, loadRc == kExitSuccess ? kExitMalformed : loadRc, loadErr.str());
            return false;
        }
        if (!digestCaptured) {
            sourceDigest = digest;
            digestCaptured = true;
        }

        NormalizedPrimitive normalized;
        std::ostringstream normalizeErr;
        const int normalizeRc = normalizePrimitive(sourcePrimitives.front(), normalized, normalizeErr);
        if (normalizeRc != kExitSuccess) {
            reportStageFailure(diagnostics, normalizeRc, normalizeErr.str());
            return false;
        }

        if (cancel && cancel()) {
            return false;
        }

        emitStage(ConversionStage::kBuildHierarchy, "build-hierarchy", context, primitiveIndex, primitiveCount,
                 primitiveBase + primitiveBand * kHierarchyStartRatio);

        PrimitiveHierarchy hierarchy;
        std::ostringstream hierarchyErr;
        const int hierarchyRc = buildHierarchy(normalized, settings, hierarchy, hierarchyErr);
        if (hierarchyRc != kExitSuccess) {
            reportStageFailure(diagnostics, hierarchyRc, hierarchyErr.str());
            return false;
        }

        if (cancel && cancel()) {
            return false;
        }

        emitStage(ConversionStage::kPackPages, "pack-pages", context, primitiveIndex, primitiveCount,
                 primitiveBase + primitiveBand * kPackStartRatio);

        PackedGeometry packed;
        std::ostringstream packErr;
        const int packRc = packPages(hierarchy, normalized, settings, packed, packErr);
        if (packRc != kExitSuccess) {
            reportStageFailure(diagnostics, packRc, packErr.str());
            return false;
        }

        std::vector<unsigned char> bytes;
        std::ostringstream writeErr;
        const int writeRc = writeContainer(hierarchy, packed, normalized, settings, sourceDigest, bytes, writeErr);
        if (writeRc != kExitSuccess) {
            reportStageFailure(diagnostics, writeRc, writeErr.str());
            return false;
        }

        if (cancel && cancel()) {
            return false;
        }

        emitStage(ConversionStage::kValidateOutputs, "validate-outputs", context, primitiveIndex, primitiveCount,
                 primitiveBase + primitiveBand * kValidateStartRatio);

        std::ostringstream validateErr;
        const int validateRc = validateContainer(bytes.data(), bytes.size(), validateErr);
        if (validateRc != kExitSuccess) {
            reportStageFailure(diagnostics, validateRc, validateErr.str());
            return false;
        }

        std::uint64_t outputTriangles = 0;
        std::uint64_t outputVertices = 0;
        for (const HierarchyCluster& cluster : hierarchy.clusters) {
            outputTriangles += cluster.triangleCount;
        }
        for (const PackedPage& page : packed.pages) {
            outputVertices += page.vertexCount;
        }

        PrimitiveOutput output;
        output.identity = identity;
        output.sourceVertexCount = normalized.vertexCount();
        output.sourceTriangleCount = hierarchy.sourceTriangleCount;
        output.outputVertexCount = outputVertices;
        output.outputTriangleCount = outputTriangles;
        output.meshletCount = static_cast<std::uint32_t>(hierarchy.clusters.size());
        output.hierarchyDepth = hierarchy.levelCount;
        output.pageCount = static_cast<std::uint32_t>(packed.pages.size());
        output.pinnedPageCount = packed.pinnedPageCount;
        output.validatedByteSize = bytes.size();
        trackedBytes += output.validatedByteSize;
        output.bytes = std::move(bytes);

        processedTriangles += std::max<std::uint32_t>(hierarchy.sourceTriangleCount, 1);
        hierarchies.push_back(std::move(hierarchy));
        packedGeometries.push_back(std::move(packed));
        outputs.push_back(std::move(output));

        emitStage(ConversionStage::kValidateOutputs, "primitive-complete", context, processedTriangles,
                 totalSourceTriangles, primitiveEnd);
    }

    if (cancel && cancel()) {
        return false;
    }

    emitStage(ConversionStage::kPrepareDownload, "package-outputs", std::string(), primitiveCount, primitiveCount,
             0.94f);

    out.sourceDigest = sourceDigest;
    out.outputs = std::move(outputs);
    out.canonicalMetadataJson = buildStatisticsJson(hierarchies, packedGeometries);
    out.success = true;

    emitStage(ConversionStage::kPrepareDownload, "complete", std::string(), primitiveCount, primitiveCount, 1.0f);

    return true;
}

} // namespace mlod
