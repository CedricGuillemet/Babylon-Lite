#include "mlod_writer.h"

#include "cli.h"
#include "crc32c.h"
#include "hierarchy.h"
#include "mlod_format.h"
#include "mlod_version.h"
#include "normalize.h"
#include "page_packer.h"
#include "sha256.h"

#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ostream>
#include <string>
#include <vector>

namespace mlod {
namespace {

std::uint64_t alignUp(std::uint64_t value, std::uint64_t alignment) {
    return ((value + alignment - 1) / alignment) * alignment;
}

void appendJsonFloat(std::string& target, float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
    target += buffer;
}

void appendJsonUint(std::string& target, std::uint64_t value) {
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    target += buffer;
}

void appendJsonString(std::string& target, const char* value) {
    target += '"';
    for (const char* p = value; *p != '\0'; ++p) {
        if (*p == '"' || *p == '\\') {
            target += '\\';
        }
        target += *p;
    }
    target += '"';
}

void writeGroupRecord(unsigned char* d, const HierarchyGroup& g) {
    le::writeF32(d + group_record::kSphere + 0, g.center[0]);
    le::writeF32(d + group_record::kSphere + 4, g.center[1]);
    le::writeF32(d + group_record::kSphere + 8, g.center[2]);
    le::writeF32(d + group_record::kSphere + 12, g.radius);
    le::writeF32(d + group_record::kError, g.simplifiedError);
    le::writeU32(d + group_record::kDepth, static_cast<std::uint32_t>(g.depth));
    le::writeU32(d + group_record::kFirstCluster, g.firstCluster);
    le::writeU32(d + group_record::kClusterCount, g.clusterCount);
    le::writeU32(d + group_record::kFirstPageRef, g.firstPageRef);
    le::writeU16(d + group_record::kPageRefCount, g.pageRefCount);
    std::uint16_t flags = 0;
    if (g.terminal) {
        flags |= kGroupFlagTerminal;
    }
    if (g.pinned) {
        flags |= kGroupFlagPinnedCoarse;
    }
    le::writeU16(d + group_record::kFlags, flags);
    le::writeU32(d + group_record::kSourceTriangles, g.sourceTriangles);
    le::writeU32(d + group_record::kEmittedTriangles, g.emittedTriangles);
}

void writeClusterRecord(unsigned char* d, const HierarchyCluster& c) {
    le::writeF32(d + cluster_record::kSphere + 0, c.center[0]);
    le::writeF32(d + cluster_record::kSphere + 4, c.center[1]);
    le::writeF32(d + cluster_record::kSphere + 8, c.center[2]);
    le::writeF32(d + cluster_record::kSphere + 12, c.radius);
    le::writeF32(d + cluster_record::kError, c.error);
    le::writeU32(d + cluster_record::kGroupId, c.groupId);
    le::writeI32(d + cluster_record::kRefinedGroupId, c.refinedGroupId);
    le::writeU32(d + cluster_record::kPageId, c.pageId);
    le::writeU32(d + cluster_record::kFirstVertex, c.firstVertexInPage);
    le::writeU32(d + cluster_record::kFirstLocalIndex, c.firstLocalIndexInPage);
    le::writeU16(d + cluster_record::kVertexCount, c.vertexCount);
    le::writeU16(d + cluster_record::kTriangleCount, c.triangleCount);
    le::writeU32(d + cluster_record::kSourceTriangles, c.sourceTriangles);
}

void writeNodeRecord(unsigned char* d, const HierarchyNode& n) {
    le::writeF32(d + node_record::kSphere + 0, n.center[0]);
    le::writeF32(d + node_record::kSphere + 4, n.center[1]);
    le::writeF32(d + node_record::kSphere + 8, n.center[2]);
    le::writeF32(d + node_record::kSphere + 12, n.radius);
    le::writeF32(d + node_record::kError, n.error);
    le::writeI32(d + node_record::kGroup, n.group);
    le::writeU32(d + node_record::kFirstChild, n.firstChild);
    le::writeU32(d + node_record::kChildCount, n.childCount);
}

void writePageTableRecord(unsigned char* d, const PackedPage& page, std::uint64_t fileOffset) {
    le::writeU64(d + page_table::kOffset, fileOffset);
    le::writeU32(d + page_table::kStoredBytes, static_cast<std::uint32_t>(page.storedBytes.size()));
    le::writeU32(d + page_table::kMeaningfulBytes, page.meaningfulBytes);
    le::writeU32(d + page_table::kDecodedBytes, page.decodedBytes);
    le::writeU32(d + page_table::kCrc, page.crc);
    le::writeU32(d + page_table::kVertexCount, page.vertexCount);
    le::writeU32(d + page_table::kLocalIndexCount, page.localIndexCount);
    le::writeU32(d + page_table::kVertexByteOffset, 0);
    le::writeU32(d + page_table::kIndexByteOffset, page.decodedVertexBytes);
    le::writeU32(d + page_table::kFirstCluster, page.firstCluster);
    le::writeU32(d + page_table::kClusterCount, page.clusterCount);
    le::writeU32(d + page_table::kFlags, page.pinned ? (kPageFlagPinned | kPageFlagCoarse) : 0u);
    le::writeU16(d + page_table::kMinDepth, page.minDepth);
    le::writeU16(d + page_table::kMaxDepth, page.maxDepth);
}

struct SectionPlan {
    std::uint32_t type = 0;
    std::uint32_t flags = 0;
    std::uint64_t offset = 0;
    std::uint64_t storedBytes = 0;
    std::uint64_t decodedBytes = 0;
    std::uint32_t elementCount = 0;
    std::uint32_t elementStride = 0;
    std::uint32_t crc = 0;
    std::uint32_t alignment = 0;
};

} // namespace

std::string buildProvenanceJson(const PrimitiveHierarchy& hierarchy, const PackedGeometry& packed,
                                const NormalizedPrimitive& primitive,
                                const ConversionOptions& options) {
    std::string json;
    json += "{";
    json += "\"boundsMax\":[";
    appendJsonFloat(json, hierarchy.boundsMax[0]);
    json += ",";
    appendJsonFloat(json, hierarchy.boundsMax[1]);
    json += ",";
    appendJsonFloat(json, hierarchy.boundsMax[2]);
    json += "],\"boundsMin\":[";
    appendJsonFloat(json, hierarchy.boundsMin[0]);
    json += ",";
    appendJsonFloat(json, hierarchy.boundsMin[1]);
    json += ",";
    appendJsonFloat(json, hierarchy.boundsMin[2]);
    json += "],\"cgltfRevision\":";
    appendJsonString(json, kCgltfRev);
    json += ",\"clusterCount\":";
    appendJsonUint(json, hierarchy.clusters.size());
    json += ",\"compilerTarget\":";
    appendJsonString(json, kCompilerTarget);
    json += ",\"formatVersion\":\"";
    appendJsonUint(json, kFormatMajor);
    json += ".";
    appendJsonUint(json, kFormatMinor);
    json += "\",\"groupCount\":";
    appendJsonUint(json, hierarchy.groups.size());
    json += ",\"hierarchyLevels\":";
    appendJsonUint(json, hierarchy.levelCount);
    json += ",\"meshletMaxTriangles\":";
    appendJsonUint(json, options.meshletMaxTriangles);
    json += ",\"meshletMaxVertices\":";
    appendJsonUint(json, options.meshletMaxVertices);
    json += ",\"meshletMinTriangles\":";
    appendJsonUint(json, options.meshletMinTriangles);
    json += ",\"meshoptimizerRevision\":";
    appendJsonString(json, kMeshoptimizerRev);
    json += ",\"nodeCount\":";
    appendJsonUint(json, hierarchy.nodes.size());
    json += ",\"pageCount\":";
    appendJsonUint(json, packed.pages.size());
    json += ",\"pageMaxKiB\":";
    appendJsonUint(json, options.pageMaxKiB);
    json += ",\"pageMinKiB\":";
    appendJsonUint(json, options.pageMinKiB);
    json += ",\"pageTargetKiB\":";
    appendJsonUint(json, options.pageTargetKiB);
    json += ",\"partitionSize\":";
    appendJsonUint(json, options.partitionSize);
    json += ",\"pinnedPageCount\":";
    appendJsonUint(json, packed.pinnedPageCount);
    json += ",\"simplifyRatio\":";
    appendJsonFloat(json, options.simplifyRatio);
    json += ",\"simplifyThreshold\":";
    appendJsonFloat(json, options.simplifyThreshold);
    json += ",\"sourceMeshIndex\":";
    appendJsonUint(json, hierarchy.meshIndex);
    json += ",\"sourcePrimitiveIndex\":";
    appendJsonUint(json, hierarchy.primitiveIndex);
    json += ",\"sourceTriangleCount\":";
    appendJsonUint(json, hierarchy.sourceTriangleCount);
    json += ",\"toolVersion\":";
    appendJsonString(json, kToolVersion);
    json += ",\"totalClusterTriangles\":";
    std::uint64_t totalClusterTriangles = 0;
    for (const HierarchyCluster& c : hierarchy.clusters) {
        totalClusterTriangles += c.triangleCount;
    }
    appendJsonUint(json, totalClusterTriangles);
    json += "}";
    (void)primitive;
    return json;
}

int writeContainer(const PrimitiveHierarchy& hierarchy, const PackedGeometry& packed,
                   const NormalizedPrimitive& primitive, const ConversionOptions& options,
                   const std::array<std::uint8_t, 32>& sourceDigest,
                   std::vector<unsigned char>& out, std::ostream& err) {
    const auto fail = [&](const char* message) {
        err << "error: mesh " << hierarchy.meshIndex << " primitive " << hierarchy.primitiveIndex
            << ": writer " << message << "\n";
        return kExitWrite;
    };

    // Build the byte payload of every metadata section.
    const std::string provenance = buildProvenanceJson(hierarchy, packed, primitive, options);
    std::vector<unsigned char> provBytes(provenance.begin(), provenance.end());

    std::vector<unsigned char> groupBytes(hierarchy.groups.size() * kGroupRecordSize, 0);
    for (std::size_t i = 0; i < hierarchy.groups.size(); ++i) {
        writeGroupRecord(groupBytes.data() + i * kGroupRecordSize, hierarchy.groups[i]);
    }
    std::vector<unsigned char> clusterBytes(hierarchy.clusters.size() * kClusterRecordSize, 0);
    for (std::size_t i = 0; i < hierarchy.clusters.size(); ++i) {
        writeClusterRecord(clusterBytes.data() + i * kClusterRecordSize, hierarchy.clusters[i]);
    }
    std::vector<unsigned char> nodeBytes(hierarchy.nodes.size() * kHierarchyNodeSize, 0);
    for (std::size_t i = 0; i < hierarchy.nodes.size(); ++i) {
        writeNodeRecord(nodeBytes.data() + i * kHierarchyNodeSize, hierarchy.nodes[i]);
    }
    std::vector<unsigned char> refBytes(packed.groupPageRefs.size() * 4, 0);
    for (std::size_t i = 0; i < packed.groupPageRefs.size(); ++i) {
        le::writeU32(refBytes.data() + i * 4, packed.groupPageRefs[i]);
    }

    // Lay out sections; page data starts on a 64 KiB boundary with pinned pages
    // first (page ids already order pinned pages before fine pages).
    std::uint64_t cursor = alignUp(kHeaderSize, kSectionAlignment); // directory
    const std::uint64_t directoryOffset = cursor;
    const std::uint64_t directoryBytes = kRequiredSectionCount * kSectionEntrySize;
    cursor += directoryBytes;

    const std::uint64_t provOffset = alignUp(cursor, kSectionAlignment);
    cursor = provOffset + provBytes.size();
    const std::uint64_t groupOffset = alignUp(cursor, kSectionAlignment);
    cursor = groupOffset + groupBytes.size();
    const std::uint64_t clusterOffset = alignUp(cursor, kSectionAlignment);
    cursor = clusterOffset + clusterBytes.size();
    const std::uint64_t nodeOffset = alignUp(cursor, kSectionAlignment);
    cursor = nodeOffset + nodeBytes.size();
    const std::uint64_t refOffset = alignUp(cursor, kSectionAlignment);
    cursor = refOffset + refBytes.size();
    const std::uint64_t pageTableOffset = alignUp(cursor, kSectionAlignment);
    const std::uint64_t pageTableBytes = packed.pages.size() * kPageTableRecordSize;
    cursor = pageTableOffset + pageTableBytes;

    const std::uint64_t pageDataOffset = alignUp(cursor, kPageAlignment);
    std::vector<std::uint64_t> pageOffsets(packed.pages.size());
    std::uint64_t running = pageDataOffset;
    std::uint64_t bootstrapBytes = pageDataOffset;
    for (std::size_t i = 0; i < packed.pages.size(); ++i) {
        pageOffsets[i] = running;
        running += packed.pages[i].storedBytes.size();
        if (packed.pages[i].pinned) {
            bootstrapBytes = running;
        }
    }
    const std::uint64_t totalBytes = running;
    const std::uint64_t pageDataBytes = totalBytes - pageDataOffset;

    if (directoryOffset + directoryBytes > kPageAlignment) {
        return fail("header and directory exceed the first 64 KiB");
    }
    if (bootstrapBytes > totalBytes || pageDataOffset > bootstrapBytes) {
        return fail("bootstrap layout is inconsistent");
    }

    // Serialize the page table now that page file offsets are known.
    std::vector<unsigned char> pageTableRecords(pageTableBytes, 0);
    std::uint64_t totalDecodedPageBytes = 0;
    for (std::size_t i = 0; i < packed.pages.size(); ++i) {
        writePageTableRecord(pageTableRecords.data() + i * kPageTableRecordSize, packed.pages[i],
                             pageOffsets[i]);
        totalDecodedPageBytes += packed.pages[i].decodedBytes;
    }

    // Section plans in type order (directory is sorted by type).
    SectionPlan plans[kRequiredSectionCount];
    plans[0] = {kSectionProvenanceJson,
                kSectionFlagRequired,
                provOffset,
                provBytes.size(),
                provBytes.size(),
                static_cast<std::uint32_t>(provBytes.size()),
                0,
                crc32c(provBytes.data(), provBytes.size()),
                kSectionAlignment};
    plans[1] = {kSectionGroups,
                kSectionFlagRequired,
                groupOffset,
                groupBytes.size(),
                groupBytes.size(),
                static_cast<std::uint32_t>(hierarchy.groups.size()),
                kGroupRecordSize,
                crc32c(groupBytes.data(), groupBytes.size()),
                kSectionAlignment};
    plans[2] = {kSectionClusters,
                kSectionFlagRequired,
                clusterOffset,
                clusterBytes.size(),
                clusterBytes.size(),
                static_cast<std::uint32_t>(hierarchy.clusters.size()),
                kClusterRecordSize,
                crc32c(clusterBytes.data(), clusterBytes.size()),
                kSectionAlignment};
    plans[3] = {kSectionHierarchyNodes,
                kSectionFlagRequired,
                nodeOffset,
                nodeBytes.size(),
                nodeBytes.size(),
                static_cast<std::uint32_t>(hierarchy.nodes.size()),
                kHierarchyNodeSize,
                crc32c(nodeBytes.data(), nodeBytes.size()),
                kSectionAlignment};
    plans[4] = {kSectionGroupPageRefs,
                kSectionFlagRequired,
                refOffset,
                refBytes.size(),
                refBytes.size(),
                static_cast<std::uint32_t>(packed.groupPageRefs.size()),
                4,
                crc32c(refBytes.data(), refBytes.size()),
                kSectionAlignment};
    plans[5] = {kSectionPageTable,
                kSectionFlagRequired,
                pageTableOffset,
                pageTableBytes,
                pageTableBytes,
                static_cast<std::uint32_t>(packed.pages.size()),
                kPageTableRecordSize,
                crc32c(pageTableRecords.data(), pageTableRecords.size()),
                kSectionAlignment};
    plans[6] = {kSectionPageData,
                kSectionFlagRequired | kSectionFlagPageData,
                pageDataOffset,
                pageDataBytes,
                totalDecodedPageBytes,
                static_cast<std::uint32_t>(packed.pages.size()),
                0,
                0, // per-page CRCs
                kPageAlignment};

    // Assemble the full image.
    out.assign(static_cast<std::size_t>(totalBytes), 0);
    std::memcpy(out.data() + provOffset, provBytes.data(), provBytes.size());
    std::memcpy(out.data() + groupOffset, groupBytes.data(), groupBytes.size());
    std::memcpy(out.data() + clusterOffset, clusterBytes.data(), clusterBytes.size());
    std::memcpy(out.data() + nodeOffset, nodeBytes.data(), nodeBytes.size());
    if (!refBytes.empty()) {
        std::memcpy(out.data() + refOffset, refBytes.data(), refBytes.size());
    }
    std::memcpy(out.data() + pageTableOffset, pageTableRecords.data(), pageTableRecords.size());
    for (std::size_t i = 0; i < packed.pages.size(); ++i) {
        std::memcpy(out.data() + pageOffsets[i], packed.pages[i].storedBytes.data(),
                    packed.pages[i].storedBytes.size());
    }

    // Directory entries (sorted by type == plan order).
    unsigned char* dir = out.data() + directoryOffset;
    for (std::uint32_t i = 0; i < kRequiredSectionCount; ++i) {
        unsigned char* e = dir + i * kSectionEntrySize;
        const SectionPlan& plan = plans[i];
        le::writeU32(e + section_entry::kType, plan.type);
        le::writeU32(e + section_entry::kFlags, plan.flags);
        le::writeU64(e + section_entry::kOffset, plan.offset);
        le::writeU64(e + section_entry::kStoredBytes, plan.storedBytes);
        le::writeU64(e + section_entry::kDecodedBytes, plan.decodedBytes);
        le::writeU32(e + section_entry::kElementCount, plan.elementCount);
        le::writeU32(e + section_entry::kElementStride, plan.elementStride);
        le::writeU32(e + section_entry::kCrc, plan.crc);
        le::writeU32(e + section_entry::kAlignment, plan.alignment);
    }
    const std::uint32_t directoryCrc = crc32c(dir, directoryBytes);

    // Deterministic hierarchy id.
    Sha256 idHash;
    idHash.update(sourceDigest.data(), sourceDigest.size());
    unsigned char indexBytes[8];
    le::writeU32(indexBytes + 0, hierarchy.meshIndex);
    le::writeU32(indexBytes + 4, hierarchy.primitiveIndex);
    idHash.update(indexBytes, sizeof(indexBytes));
    const std::string canonical = canonicalConversionOptions(options);
    idHash.update(canonical.data(), canonical.size());
    const std::array<std::uint8_t, 32> hierarchyId = idHash.finalize();

    float maxNonterminalError = 0.0f;
    for (const HierarchyGroup& g : hierarchy.groups) {
        if (!g.terminal && std::isfinite(g.simplifiedError)) {
            maxNonterminalError = std::fmax(maxNonterminalError, g.simplifiedError);
        }
    }
    std::uint32_t attributeMask = kAttributePosition | kAttributeNormal;
    if (primitive.hasUv) {
        attributeMask |= kAttributeUv0;
    }
    std::uint64_t totalClusterTriangles = 0;
    for (const HierarchyCluster& c : hierarchy.clusters) {
        totalClusterTriangles += c.triangleCount;
    }

    // Header.
    unsigned char* h = out.data();
    std::memcpy(h + header::kMagic, kContainerMagic, sizeof(kContainerMagic));
    le::writeU16(h + header::kFormatMajor, kFormatMajor);
    le::writeU16(h + header::kFormatMinor, kFormatMinor);
    le::writeU16(h + header::kMinReaderMajor, kMinReaderMajor);
    le::writeU16(h + header::kMinReaderMinor, kMinReaderMinor);
    le::writeU32(h + header::kEndianTag, kEndianTag);
    le::writeU32(h + header::kHeaderBytes, kHeaderSize);
    le::writeU32(h + header::kContainerFlags, 0);
    le::writeU32(h + header::kSectionCount, kRequiredSectionCount);
    le::writeU64(h + header::kDirectoryOffset, directoryOffset);
    le::writeU64(h + header::kDirectoryBytes, directoryBytes);
    le::writeU64(h + header::kBootstrapBytes, bootstrapBytes);
    le::writeU64(h + header::kTotalFileBytes, totalBytes);
    std::memcpy(h + header::kSourceDigest, sourceDigest.data(), sourceDigest.size());
    const std::array<std::uint8_t, 32> buildFingerprint = computeBuildFingerprint(options);
    std::memcpy(h + header::kBuildFingerprint, buildFingerprint.data(), buildFingerprint.size());
    std::memcpy(h + header::kHierarchyId, hierarchyId.data(), 16);
    le::writeU32(h + header::kSourceMeshIndex, hierarchy.meshIndex);
    le::writeU32(h + header::kSourcePrimitiveIndex, hierarchy.primitiveIndex);
    le::writeU64(h + header::kSourceTriangleCount, hierarchy.sourceTriangleCount);
    le::writeU64(h + header::kTotalClusterTriangles, totalClusterTriangles);
    le::writeU32(h + header::kClusterCount, static_cast<std::uint32_t>(hierarchy.clusters.size()));
    le::writeU32(h + header::kGroupCount, static_cast<std::uint32_t>(hierarchy.groups.size()));
    le::writeU32(h + header::kNodeCount, static_cast<std::uint32_t>(hierarchy.nodes.size()));
    le::writeU32(h + header::kPageCount, static_cast<std::uint32_t>(packed.pages.size()));
    le::writeU32(h + header::kPinnedPageCount, packed.pinnedPageCount);
    le::writeU32(h + header::kLevelCount, hierarchy.levelCount);
    le::writeU32(h + header::kAttributeMask, attributeMask);
    le::writeU32(h + header::kVertexStride, kDecodedVertexStride);
    le::writeF32(h + header::kBoundsMin + 0, hierarchy.boundsMin[0]);
    le::writeF32(h + header::kBoundsMin + 4, hierarchy.boundsMin[1]);
    le::writeF32(h + header::kBoundsMin + 8, hierarchy.boundsMin[2]);
    le::writeF32(h + header::kBoundsMax + 0, hierarchy.boundsMax[0]);
    le::writeF32(h + header::kBoundsMax + 4, hierarchy.boundsMax[1]);
    le::writeF32(h + header::kBoundsMax + 8, hierarchy.boundsMax[2]);
    le::writeF32(h + header::kMaxNonterminalError, maxNonterminalError);
    le::writeU32(h + header::kHeaderCrc, 0);
    le::writeU32(h + header::kDirectoryCrc, directoryCrc);
    const std::uint32_t headerCrc = crc32c(h, kHeaderSize);
    le::writeU32(h + header::kHeaderCrc, headerCrc);

    return kExitSuccess;
}

} // namespace mlod
