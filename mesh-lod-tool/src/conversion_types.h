#ifndef MLOD_CONVERSION_TYPES_H
#define MLOD_CONVERSION_TYPES_H

#include "diagnostics.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace mlod {

// Identifies one glTF primitive by its zero-based mesh/primitive index in
// document order. Host-independent counterpart of the native adapter's
// SelectedPrimitive (input.h) -- the converter core never depends on
// adapter-only headers (architecture section 7.9).
struct PrimitiveIdentity {
    std::uint32_t meshIndex = 0;
    std::uint32_t primitiveIndex = 0;
};

inline bool operator==(const PrimitiveIdentity& a, const PrimitiveIdentity& b) {
    return a.meshIndex == b.meshIndex && a.primitiveIndex == b.primitiveIndex;
}
inline bool operator!=(const PrimitiveIdentity& a, const PrimitiveIdentity& b) {
    return !(a == b);
}

// Selects every supported primitive, every primitive of one mesh, or exactly
// one mesh/primitive. A single-primitive selection always carries its owning
// mesh index as part of the same value, so "a primitive without a mesh"
// cannot be represented (architecture sections 7.9 and 7.12).
enum class PrimitiveSelectionMode : int {
    kAllPrimitives = 0,
    kWholeMesh = 1,
    kSinglePrimitive = 2,
};

struct PrimitiveSelection {
    PrimitiveSelectionMode mode = PrimitiveSelectionMode::kAllPrimitives;
    std::uint32_t meshIndex = 0;      // used by kWholeMesh and kSinglePrimitive
    std::uint32_t primitiveIndex = 0; // used only by kSinglePrimitive

    static PrimitiveSelection allPrimitives() {
        return PrimitiveSelection{};
    }
    static PrimitiveSelection wholeMesh(std::uint32_t mesh) {
        PrimitiveSelection selection;
        selection.mode = PrimitiveSelectionMode::kWholeMesh;
        selection.meshIndex = mesh;
        return selection;
    }
    static PrimitiveSelection singlePrimitive(std::uint32_t mesh, std::uint32_t primitive) {
        PrimitiveSelection selection;
        selection.mode = PrimitiveSelectionMode::kSinglePrimitive;
        selection.meshIndex = mesh;
        selection.primitiveIndex = primitive;
        return selection;
    }
};

inline bool operator==(const PrimitiveSelection& a, const PrimitiveSelection& b) {
    return a.mode == b.mode && a.meshIndex == b.meshIndex && a.primitiveIndex == b.primitiveIndex;
}
inline bool operator!=(const PrimitiveSelection& a, const PrimitiveSelection& b) {
    return !(a == b);
}

// Output-affecting conversion knobs plus primitive selection. Deliberately
// excludes input/output/stats paths, validate-only, preset display names, and
// compiler target: none of those affect emitted `.mlod` bytes (architecture
// section 7.9). Defaults mirror the native CLI's canonical defaults exactly.
struct ConversionSettings {
    PrimitiveSelection selection;

    std::uint32_t meshletMaxVertices = 64;
    std::uint32_t meshletMinTriangles = 40;
    std::uint32_t meshletMaxTriangles = 124;
    std::uint32_t partitionSize = 8;
    float simplifyRatio = 0.5f;
    float simplifyThreshold = 0.85f;

    std::uint32_t pageMinKiB = 64;
    std::uint32_t pageTargetKiB = 128;
    std::uint32_t pageMaxKiB = 256;
};

namespace detail {
inline void appendCanonicalUint(std::string& target, std::uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%u", value);
    target += buffer;
}
inline void appendCanonicalFloat(std::string& target, float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.6g", static_cast<double>(value));
    target += buffer;
}
} // namespace detail

// Locale-independent, path-free, lexicographically key-ordered serialization
// of every output-affecting setting (mirrors cli.cpp's canonicalConversionOptions
// exactly, minus the path-derived fields it never had). Selection is
// intentionally excluded: it changes *which* primitives are emitted, not the
// bytes of any one emitted primitive, so two requests differing only in
// selection still canonicalize identically.
inline std::string canonicalConversionSettings(const ConversionSettings& settings) {
    std::string canonical;
    canonical += "meshlet_max_triangles=";
    detail::appendCanonicalUint(canonical, settings.meshletMaxTriangles);
    canonical += "\nmeshlet_max_vertices=";
    detail::appendCanonicalUint(canonical, settings.meshletMaxVertices);
    canonical += "\nmeshlet_min_triangles=";
    detail::appendCanonicalUint(canonical, settings.meshletMinTriangles);
    canonical += "\npage_max_kib=";
    detail::appendCanonicalUint(canonical, settings.pageMaxKiB);
    canonical += "\npage_min_kib=";
    detail::appendCanonicalUint(canonical, settings.pageMinKiB);
    canonical += "\npage_target_kib=";
    detail::appendCanonicalUint(canonical, settings.pageTargetKiB);
    canonical += "\npartition_size=";
    detail::appendCanonicalUint(canonical, settings.partitionSize);
    canonical += "\nsimplify_ratio=";
    detail::appendCanonicalFloat(canonical, settings.simplifyRatio);
    canonical += "\nsimplify_threshold=";
    detail::appendCanonicalFloat(canonical, settings.simplifyThreshold);
    canonical += "\n";
    return canonical;
}

// Validates settings exactly as the native CLI parser does (cli.cpp), but
// host-independently and without any argv/option-name text: page sizes must
// be multiples of 64 KiB with min<=target<=max, and minTriangles<=maxTriangles.
// Emits one Diagnostic (kCliOption family) through `sink` on the first
// violation found, matching the native parser's check order, and returns
// false; returns true when every rule passes.
inline bool validateConversionSettings(const ConversionSettings& settings, const DiagnosticSink& sink) {
    const std::pair<const char*, std::uint32_t> pageSizes[] = {
        {"pageMinKiB", settings.pageMinKiB},
        {"pageTargetKiB", settings.pageTargetKiB},
        {"pageMaxKiB", settings.pageMaxKiB},
    };
    for (const auto& entry : pageSizes) {
        if (entry.second % 64 != 0) {
            DiagnosticContext context;
            context.optionName = entry.first;
            emitDiagnostic(sink, makeDiagnostic(diag_code::kCliOption, kExitCli, DiagnosticSeverity::kError,
                                                std::string(entry.first) + " must be a multiple of 64 KiB",
                                                context));
            return false;
        }
    }
    if (!(settings.pageMinKiB <= settings.pageTargetKiB && settings.pageTargetKiB <= settings.pageMaxKiB)) {
        DiagnosticContext context;
        context.optionName = "pageTargetKiB";
        emitDiagnostic(sink, makeDiagnostic(diag_code::kCliOption, kExitCli, DiagnosticSeverity::kError,
                                            "page sizes must satisfy pageMinKiB <= pageTargetKiB <= pageMaxKiB",
                                            context));
        return false;
    }
    if (settings.meshletMinTriangles > settings.meshletMaxTriangles) {
        DiagnosticContext context;
        context.optionName = "meshletMinTriangles";
        emitDiagnostic(sink, makeDiagnostic(diag_code::kCliOption, kExitCli, DiagnosticSeverity::kError,
                                            "meshletMinTriangles must not exceed meshletMaxTriangles",
                                            context));
        return false;
    }
    return true;
}

// ---- Input / resource resolution ----

// Which glTF property kind a resolved resource satisfies. Buffers feed
// geometry (and the source digest); images are validated for presence only --
// their bytes never enter normalized geometry or the digest (architecture
// section 7.11).
enum class ResourceKind : int {
    kBuffer = 0,
    kImage = 1,
};

// Describes what a resolver's virtual root permits, purely for
// diagnostics/preflight presentation. Enforcement of scheme/escape rules is
// each resolver's own responsibility: all resolvers satisfy the
// same ResourceResolver contract but apply their own host policy rather than
// branching inside the converter core (architecture section 7.11).
struct ResourceResolverCapabilities {
    bool allowsRemoteUri = false;   // http(s):, protocol-relative
    bool allowsAbsoluteUri = false; // drive-letter, UNC, leading '/', file:
    std::string rootDescription;    // e.g. "native filesystem (entry directory)"
};

// One resolved external resource: owned bytes for exactly one buffer or image
// reference.
struct ResolvedResource {
    std::vector<unsigned char> bytes;
};

// Resolves one externally referenced glTF resource (a buffer or image URI
// that is neither the GLB BIN chunk nor an embedded data URI) into owned
// bytes. `uri` is the exact value read from the glTF document (not yet
// normalized); `gltfProperty` identifies the referencing property (e.g.
// "buffers[2].uri") for diagnostics. Returns true and fills `outResource` on
// success; returns false and fills `outError` on failure. Never invoked for
// GLB BIN chunks or data URIs, which the core resolves internally without
// host I/O.
using ResourceResolver = std::function<bool(const std::string& uri, const std::string& gltfProperty,
                                            ResourceKind kind, ResolvedResource& outResource,
                                            Diagnostic& outError)>;

// Owns the entry document bytes, a display name for diagnostics, and the
// resolver used to materialize every externally referenced buffer/image, for
// the full inspection/conversion lifetime. Filesystem-independent: the native
// adapter constructs one of these over
// their own storage (architecture sections 7.9 and 7.11).
struct InputBundle {
    std::string entryVirtualPath;  // normalized virtual path/name of the entry .glb/.gltf
    std::vector<unsigned char> entryBytes;
    std::string sourceDisplayName; // shown in diagnostics
    ResourceResolver resolver;
    ResourceResolverCapabilities capabilities;
};

// ---- Progress ----

// Weighted conversion stages (architecture section 7.14). Values are ordinal
// and monotonic across one conversion attempt.
enum class ConversionStage : int {
    kValidateResources = 0,
    kNormalizeGeometry = 1,
    kBuildHierarchy = 2,
    kPackPages = 3,
    kValidateOutputs = 4,
    kPrepareDownload = 5,
};

struct ProgressEvent {
    ConversionStage stage = ConversionStage::kValidateResources;
    std::string activityCode;
    std::uint64_t completedUnits = 0;
    std::uint64_t totalUnits = 0;
    float overallFraction = 0.0f; // [0, 1], monotonically non-decreasing overall
    std::uint64_t trackedBytes = 0;
    std::uint64_t estimatedPeakBytes = 0;
    std::string context;
};

// Callback-style progress sink with the same "absent means no-op" contract as
// DiagnosticSink.
using ProgressSink = std::function<void(const ProgressEvent&)>;

inline void emitProgress(const ProgressSink& sink, const ProgressEvent& event) {
    if (sink) {
        sink(event);
    }
}

// ---- Memory ledger ----

// Tracks current/peak tracked allocation bytes against a policy limit. This
// ledger only accounts for allocations the core explicitly reserves through it
// (source/normalized/hierarchy/page/output/report buffers); it does not and
// cannot observe every third-party allocation, so it must never be presented
// as a guarantee against actual out-of-memory failures (architecture section
// 7.16).
class MemoryLedger {
public:
    explicit MemoryLedger(std::uint64_t policyLimitBytes = 0) : policyLimitBytes_(policyLimitBytes) {}

    // Attempts to reserve `bytes` more tracked memory. Returns false and
    // leaves the ledger unchanged when doing so would exceed the policy limit
    // (a limit of 0 means "unbounded") or would overflow; returns true and
    // updates current/peak otherwise.
    bool reserve(std::uint64_t bytes) {
        const std::uint64_t next = currentBytes_ + bytes;
        if (next < currentBytes_) {
            return false; // overflow
        }
        if (policyLimitBytes_ != 0 && next > policyLimitBytes_) {
            return false;
        }
        currentBytes_ = next;
        if (currentBytes_ > peakBytes_) {
            peakBytes_ = currentBytes_;
        }
        return true;
    }

    // Releases previously reserved bytes. Clamps to zero rather than
    // underflowing if asked to release more than is currently reserved.
    void release(std::uint64_t bytes) {
        currentBytes_ = (bytes > currentBytes_) ? 0 : (currentBytes_ - bytes);
    }

    std::uint64_t currentBytes() const {
        return currentBytes_;
    }
    std::uint64_t peakBytes() const {
        return peakBytes_;
    }
    std::uint64_t policyLimitBytes() const {
        return policyLimitBytes_;
    }

private:
    std::uint64_t currentBytes_ = 0;
    std::uint64_t peakBytes_ = 0;
    std::uint64_t policyLimitBytes_ = 0;
};

// ---- Results ----

// Deterministic memory-preflight terms (architecture section 7.12, symbols
// F/B/V/I/T/G/L/M/H/O/P). Populated by inspection; all-zero/true is the
// scaffold default until inspection computes real values.
struct PreflightTerms {
    std::uint64_t selectedBytes = 0;           // F
    std::uint64_t copiedBytes = 0;             // B
    std::uint64_t vertexCount = 0;             // V
    std::uint64_t indexCount = 0;              // I
    std::uint64_t triangleCount = 0;           // T
    std::uint64_t normalizedGeometryBytes = 0; // G
    std::uint32_t lodFactor = 0;               // L
    std::uint64_t estimatedMeshlets = 0;       // M
    std::uint64_t hierarchyReserveBytes = 0;   // H
    std::uint64_t outputReserveBytes = 0;      // O
    std::uint64_t packagingReserveBytes = 0;   // P
    std::uint64_t estimatedPeakBytes = 0;
    std::uint64_t policyLimitBytes = 0;
    bool withinPolicyLimit = true;
};

struct InspectedPrimitive {
    PrimitiveIdentity identity;
    bool supported = true;
    std::uint64_t sourceVertexCount = 0;
    std::uint64_t sourceTriangleCount = 0;
};

// Path-free inspection result: everything needed to show the user what will
// be converted and whether it can proceed, without producing any `.mlod`
// bytes (architecture section 7.9 / 7.12).
struct InspectionResult {
    std::string entryType; // "glb" or "gltf"
    std::vector<std::string> resolvedResourcePaths;
    std::vector<InspectedPrimitive> primitives;
    std::vector<PrimitiveIdentity> supportedSelection;
    std::uint64_t sourceVertexCount = 0;
    std::uint64_t sourceTriangleCount = 0;
    std::uint64_t sourceIndexCount = 0;
    PreflightTerms preflight;
    std::vector<Diagnostic> warnings;
};

// One converted primitive's result. `bytes` is an owned value whose lifetime
// is independent of any input callback, resolver, or resource lifetime
// (architecture section 7.9).
struct PrimitiveOutput {
    PrimitiveIdentity identity;
    std::vector<unsigned char> bytes;
    std::uint64_t sourceVertexCount = 0;
    std::uint64_t sourceTriangleCount = 0;
    std::uint64_t outputVertexCount = 0;
    std::uint64_t outputTriangleCount = 0;
    std::uint32_t meshletCount = 0;
    std::uint32_t hierarchyDepth = 0;
    std::uint32_t pageCount = 0;
    std::uint32_t pinnedPageCount = 0;
    std::uint64_t validatedByteSize = 0;
};

// Whole-conversion result. `success` is true only when every selected
// primitive converted and independently validated -- there is no partial
// success. `elapsedHostSeconds` is host wall-clock timing supplied purely for
// diagnostics/UI and never enters deterministic output bytes or
// `canonicalMetadataJson` (architecture section 7.9).
struct ConversionResult {
    bool success = false;
    std::array<std::uint8_t, 32> sourceDigest{};
    std::vector<PrimitiveOutput> outputs; // source mesh/primitive order
    std::string canonicalMetadataJson;
    double elapsedHostSeconds = 0.0;
    std::vector<Diagnostic> diagnostics;
};

} // namespace mlod

#endif // MLOD_CONVERSION_TYPES_H
