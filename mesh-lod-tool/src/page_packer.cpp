#include "page_packer.h"

#include "cli.h"
#include "crc32c.h"
#include "hierarchy.h"
#include "mlod_format.h"
#include "normalize.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "meshoptimizer.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <vector>

namespace mlod {
namespace {

std::uint32_t roundUp(std::uint32_t value, std::uint32_t alignment) {
    return ((value + alignment - 1) / alignment) * alignment;
}

// Rounds a page's meaningful bytes up to the 64 KiB-multiple allocation, with a
// 64 KiB minimum. The caller enforces the 256 KiB maximum.
std::uint32_t pageAllocation(std::uint32_t content) {
    std::uint32_t allocation = roundUp(content, kPageAlignment);
    if (allocation < kPageAlignment) {
        allocation = kPageAlignment;
    }
    return allocation;
}

void writeVertexRecord(unsigned char* dst, const NormalizedPrimitive& primitive,
                       std::uint32_t vertexId) {
    const std::size_t p = static_cast<std::size_t>(vertexId) * 3;
    le::writeF32(dst + 0, primitive.positions[p + 0]);
    le::writeF32(dst + 4, primitive.positions[p + 1]);
    le::writeF32(dst + 8, primitive.positions[p + 2]);

    std::int16_t e0 = 0;
    std::int16_t e1 = 0;
    octEncodeNormal(primitive.normals[p + 0], primitive.normals[p + 1], primitive.normals[p + 2],
                    e0, e1);
    le::writeU16(dst + 12, static_cast<std::uint16_t>(e0));
    le::writeU16(dst + 14, static_cast<std::uint16_t>(e1));

    std::uint16_t u = 0;
    std::uint16_t v = 0;
    if (primitive.hasUv) {
        const std::size_t t = static_cast<std::size_t>(vertexId) * 2;
        u = meshopt_quantizeHalf(primitive.uvs[t + 0]);
        v = meshopt_quantizeHalf(primitive.uvs[t + 1]);
    }
    le::writeU16(dst + 16, u);
    le::writeU16(dst + 18, v);
    le::writeU16(dst + 20, 0); // reserved
    le::writeU16(dst + 22, 0);
}

// One page under construction, before pinned-first ordering and header/CRC.
struct PageWork {
    std::vector<std::uint32_t> clusterIndices;
    bool pinned = false;
    std::uint16_t minDepth = 0;
    std::uint16_t maxDepth = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t localIndexCount = 0;
    std::uint32_t decodedVertexBytes = 0;
    std::uint32_t decodedIndexBytes = 0;
    std::uint32_t decodedBytes = 0;
    std::uint32_t encVertexOffset = 0;
    std::uint32_t encIndexOffset = 0;
    std::vector<unsigned char> encVertex;
    std::vector<unsigned char> encIndex;
};

} // namespace

void octEncodeNormal(float x, float y, float z, std::int16_t& e0, std::int16_t& e1) {
    const float l1 = std::fabs(x) + std::fabs(y) + std::fabs(z);
    if (l1 <= 0.0f) {
        e0 = 0;
        e1 = 0;
        return;
    }
    float ox = x / l1;
    float oy = y / l1;
    if (z < 0.0f) {
        const float sx = ox >= 0.0f ? 1.0f : -1.0f;
        const float sy = oy >= 0.0f ? 1.0f : -1.0f;
        const float tx = (1.0f - std::fabs(oy)) * sx;
        const float ty = (1.0f - std::fabs(ox)) * sy;
        ox = tx;
        oy = ty;
    }
    e0 = static_cast<std::int16_t>(meshopt_quantizeSnorm(ox, 16));
    e1 = static_cast<std::int16_t>(meshopt_quantizeSnorm(oy, 16));
}

void octDecodeNormal(std::int16_t e0, std::int16_t e1, float& x, float& y, float& z) {
    float nx = static_cast<float>(e0) / 32767.0f;
    float ny = static_cast<float>(e1) / 32767.0f;
    float nz = 1.0f - std::fabs(nx) - std::fabs(ny);
    if (nz < 0.0f) {
        const float sx = nx >= 0.0f ? 1.0f : -1.0f;
        const float sy = ny >= 0.0f ? 1.0f : -1.0f;
        const float tx = (1.0f - std::fabs(ny)) * sx;
        const float ty = (1.0f - std::fabs(nx)) * sy;
        nx = tx;
        ny = ty;
    }
    const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
    x = nx / length;
    y = ny / length;
    z = nz / length;
}

int packPages(PrimitiveHierarchy& hierarchy, const NormalizedPrimitive& primitive,
              const ConversionSettings& options, PackedGeometry& out, std::ostream& err) {
    const auto fail = [&](const std::string& message) {
        err << "error: mesh " << hierarchy.meshIndex << " primitive " << hierarchy.primitiveIndex
            << ": page packing " << message << "\n";
        return kExitValidation;
    };

    std::vector<HierarchyGroup>& groups = hierarchy.groups;
    std::vector<HierarchyCluster>& clusters = hierarchy.clusters;

    const std::uint32_t targetDecoded = options.pageTargetKiB * 1024;
    const std::uint32_t maxDecoded = options.pageMaxKiB * 1024;

    const auto clusterDecoded = [](const HierarchyCluster& c) {
        return static_cast<std::uint32_t>(c.vertexCount) * kDecodedVertexStride +
               static_cast<std::uint32_t>(c.triangleCount) * 3u * kLocalIndexStride;
    };

    // Phase 1: group clusters into pages in source order, never mixing pinned and
    // fine clusters, preferring to keep a whole group on one page.
    std::vector<PageWork> works;
    PageWork current;
    bool haveCurrent = false;
    const auto flush = [&]() {
        if (haveCurrent) {
            works.push_back(std::move(current));
            current = PageWork{};
            haveCurrent = false;
        }
    };

    for (std::size_t gid = 0; gid < groups.size(); ++gid) {
        const HierarchyGroup& g = groups[gid];
        const bool pinned = g.terminal;
        std::uint32_t groupDecoded = 0;
        for (std::uint32_t ci = g.firstCluster; ci < g.firstCluster + g.clusterCount; ++ci) {
            groupDecoded += clusterDecoded(clusters[ci]);
        }
        if (haveCurrent &&
            (current.pinned != pinned || current.decodedVertexBytes + current.decodedIndexBytes +
                                                 groupDecoded > targetDecoded)) {
            flush();
        }
        for (std::uint32_t ci = g.firstCluster; ci < g.firstCluster + g.clusterCount; ++ci) {
            const std::uint32_t cd = clusterDecoded(clusters[ci]);
            if (haveCurrent &&
                current.decodedVertexBytes + current.decodedIndexBytes + cd > maxDecoded) {
                flush();
            }
            if (!haveCurrent) {
                current = PageWork{};
                current.pinned = pinned;
                current.minDepth = static_cast<std::uint16_t>(g.depth);
                current.maxDepth = static_cast<std::uint16_t>(g.depth);
                haveCurrent = true;
            }
            current.clusterIndices.push_back(ci);
            current.decodedVertexBytes += static_cast<std::uint32_t>(clusters[ci].vertexCount) *
                                          kDecodedVertexStride;
            current.decodedIndexBytes +=
                static_cast<std::uint32_t>(clusters[ci].triangleCount) * 3u * kLocalIndexStride;
            current.minDepth = std::min<std::uint16_t>(current.minDepth,
                                                       static_cast<std::uint16_t>(g.depth));
            current.maxDepth = std::max<std::uint16_t>(current.maxDepth,
                                                       static_cast<std::uint16_t>(g.depth));
        }
    }
    flush();

    if (works.empty()) {
        return fail("produced no pages");
    }

    // Phase 2: build decoded arrays, encode streams, and record per-cluster page
    // offsets (independent of final page id).
    meshopt_encodeVertexVersion(0);
    meshopt_encodeIndexVersion(1);
    for (PageWork& work : works) {
        std::vector<unsigned char> vertexData;
        std::vector<std::uint16_t> indexData;
        for (const std::uint32_t ci : work.clusterIndices) {
            HierarchyCluster& c = clusters[ci];
            c.firstVertexInPage = static_cast<std::uint32_t>(vertexData.size() / kDecodedVertexStride);
            c.firstLocalIndexInPage = static_cast<std::uint32_t>(indexData.size());
            for (const std::uint32_t vertexId : c.localVertices) {
                unsigned char record[kDecodedVertexStride];
                writeVertexRecord(record, primitive, vertexId);
                vertexData.insert(vertexData.end(), record, record + kDecodedVertexStride);
            }
            for (const std::uint16_t local : c.localTriangles) {
                indexData.push_back(static_cast<std::uint16_t>(c.firstVertexInPage + local));
            }
        }
        work.vertexCount = static_cast<std::uint32_t>(vertexData.size() / kDecodedVertexStride);
        work.localIndexCount = static_cast<std::uint32_t>(indexData.size());
        work.decodedVertexBytes = static_cast<std::uint32_t>(vertexData.size());
        work.decodedIndexBytes = static_cast<std::uint32_t>(indexData.size() * kLocalIndexStride);
        const std::uint32_t decodedContent = work.decodedVertexBytes + work.decodedIndexBytes;
        work.decodedBytes = pageAllocation(decodedContent);
        if (work.decodedBytes > maxDecoded) {
            return fail("decoded page exceeds the maximum allocation");
        }

        work.encVertex.resize(meshopt_encodeVertexBufferBound(work.vertexCount, kDecodedVertexStride));
        const size_t encV = meshopt_encodeVertexBuffer(work.encVertex.data(), work.encVertex.size(),
                                                       vertexData.data(), work.vertexCount,
                                                       kDecodedVertexStride);
        if (encV == 0) {
            return fail("vertex stream encoding failed");
        }
        work.encVertex.resize(encV);

        std::vector<unsigned int> wide(indexData.begin(), indexData.end());
        work.encIndex.resize(meshopt_encodeIndexBufferBound(work.localIndexCount, work.vertexCount));
        const size_t encI = meshopt_encodeIndexBuffer(work.encIndex.data(), work.encIndex.size(),
                                                      wide.data(), work.localIndexCount);
        if (encI == 0 && work.localIndexCount != 0) {
            return fail("index stream encoding failed");
        }
        work.encIndex.resize(encI);

        work.encVertexOffset = kStoredPageHeaderSize;
        work.encIndexOffset = kStoredPageHeaderSize + roundUp(static_cast<std::uint32_t>(encV), 4);
    }

    // Phase 3: order pinned pages first (stable by original position), assign page
    // ids, assemble stored bytes with header + CRC, and record cluster page ids.
    std::vector<std::size_t> order(works.size());
    for (std::size_t i = 0; i < works.size(); ++i) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return works[a].pinned && !works[b].pinned;
    });

    out = PackedGeometry{};
    out.pages.resize(works.size());
    out.pinnedPageCount = 0;
    for (std::uint32_t pageId = 0; pageId < order.size(); ++pageId) {
        PageWork& work = works[order[pageId]];
        PackedPage& page = out.pages[pageId];
        page.pageId = pageId;
        page.pinned = work.pinned;
        page.firstCluster = work.clusterIndices.front();
        page.clusterCount = static_cast<std::uint32_t>(work.clusterIndices.size());
        page.minDepth = work.minDepth;
        page.maxDepth = work.maxDepth;
        page.vertexCount = work.vertexCount;
        page.localIndexCount = work.localIndexCount;
        page.decodedVertexBytes = work.decodedVertexBytes;
        page.decodedIndexBytes = work.decodedIndexBytes;
        page.decodedBytes = work.decodedBytes;

        const std::uint32_t meaningful =
            work.encIndexOffset + static_cast<std::uint32_t>(work.encIndex.size());
        const std::uint32_t storedAllocation = pageAllocation(meaningful);
        if (storedAllocation > options.pageMaxKiB * 1024u) {
            return fail("stored page exceeds the maximum allocation");
        }
        page.meaningfulBytes = meaningful;
        page.storedBytes.assign(storedAllocation, 0);

        unsigned char* h = page.storedBytes.data();
        std::memcpy(h + stored_page::kMagic, kStoredPageMagic, sizeof(kStoredPageMagic));
        le::writeU16(h + stored_page::kMajor, kStoredPageMajor);
        le::writeU16(h + stored_page::kHeaderBytes, static_cast<std::uint16_t>(kStoredPageHeaderSize));
        le::writeU32(h + stored_page::kPageId, pageId);
        le::writeU32(h + stored_page::kFlags, work.pinned ? (kPageFlagPinned | kPageFlagCoarse) : 0u);
        le::writeU32(h + stored_page::kVertexCount, work.vertexCount);
        le::writeU32(h + stored_page::kLocalIndexCount, work.localIndexCount);
        le::writeU32(h + stored_page::kEncVertexOffset, work.encVertexOffset);
        le::writeU32(h + stored_page::kEncVertexBytes, static_cast<std::uint32_t>(work.encVertex.size()));
        le::writeU32(h + stored_page::kDecVertexBytes, work.decodedVertexBytes);
        le::writeU32(h + stored_page::kEncIndexOffset, work.encIndexOffset);
        le::writeU32(h + stored_page::kEncIndexBytes, static_cast<std::uint32_t>(work.encIndex.size()));
        le::writeU32(h + stored_page::kDecIndexBytes, work.decodedIndexBytes);
        le::writeU32(h + stored_page::kVertexStride, kDecodedVertexStride);
        le::writeU32(h + stored_page::kIndexStride, kLocalIndexStride);
        std::memcpy(h + work.encVertexOffset, work.encVertex.data(), work.encVertex.size());
        std::memcpy(h + work.encIndexOffset, work.encIndex.data(), work.encIndex.size());
        page.crc = crc32c(page.storedBytes.data(), page.storedBytes.size());

        for (const std::uint32_t ci : work.clusterIndices) {
            clusters[ci].pageId = pageId;
        }
        if (work.pinned) {
            ++out.pinnedPageCount;
        }
        out.totalStoredBytes += storedAllocation;
        out.totalDecodedBytes += work.decodedBytes;
    }

    // Phase 4: flatten GROUP_PAGE_REFS and record group page-ref ranges.
    for (HierarchyGroup& g : groups) {
        g.firstPageRef = static_cast<std::uint32_t>(out.groupPageRefs.size());
        std::vector<std::uint32_t> seen;
        for (std::uint32_t ci = g.firstCluster; ci < g.firstCluster + g.clusterCount; ++ci) {
            const std::uint32_t pid = clusters[ci].pageId;
            if (std::find(seen.begin(), seen.end(), pid) == seen.end()) {
                seen.push_back(pid);
                out.groupPageRefs.push_back(pid);
            }
        }
        g.pageRefCount = static_cast<std::uint16_t>(seen.size());
    }

    // Phase 5: validate cluster-range partitioning, alignment, and pinned prefix.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
    for (const PackedPage& page : out.pages) {
        if (page.storedBytes.size() < kPageAlignment ||
            page.storedBytes.size() > options.pageMaxKiB * 1024u ||
            page.storedBytes.size() % kPageAlignment != 0) {
            return fail("a stored page violates size or alignment limits");
        }
        if (page.decodedBytes < kPageAlignment || page.decodedBytes > options.pageMaxKiB * 1024u ||
            page.decodedBytes % kPageAlignment != 0) {
            return fail("a decoded page violates size or alignment limits");
        }
        if (page.clusterCount == 0) {
            return fail("produced an empty page");
        }
        ranges.emplace_back(page.firstCluster, page.clusterCount);
    }
    std::sort(ranges.begin(), ranges.end());
    std::uint32_t expected = 0;
    for (const auto& range : ranges) {
        if (range.first != expected) {
            return fail("page cluster ranges do not tile the cluster array");
        }
        expected += range.second;
    }
    if (expected != clusters.size()) {
        return fail("page cluster ranges do not cover all clusters");
    }
    for (std::uint32_t pageId = 0; pageId < out.pages.size(); ++pageId) {
        const bool pinned = pageId < out.pinnedPageCount;
        if (out.pages[pageId].pinned != pinned) {
            return fail("pinned pages are not a contiguous prefix");
        }
    }
    for (const HierarchyCluster& c : clusters) {
        if (c.pageId >= out.pages.size()) {
            return fail("cluster references an invalid page");
        }
        const PackedPage& page = out.pages[c.pageId];
        if (static_cast<std::uint64_t>(c.firstVertexInPage) + c.vertexCount > page.vertexCount ||
            static_cast<std::uint64_t>(c.firstLocalIndexInPage) +
                    static_cast<std::uint64_t>(c.triangleCount) * 3 >
                page.localIndexCount) {
            return fail("cluster page offsets exceed the page");
        }
    }

    return kExitSuccess;
}

int packPages(PrimitiveHierarchy& hierarchy, const NormalizedPrimitive& primitive, const ConversionOptions& options,
              PackedGeometry& out, std::ostream& err) {
    return packPages(hierarchy, primitive, toConversionSettings(options), out, err);
}

int decodeStoredPage(const unsigned char* stored, std::size_t storedSize,
                     std::vector<unsigned char>& vertices, std::vector<std::uint16_t>& indices,
                     std::ostream& err) {
    const auto fail = [&](const char* message) {
        err << "error: stored page decode " << message << "\n";
        return kExitValidation;
    };

    if (storedSize < kStoredPageHeaderSize) {
        return fail("truncated page header");
    }
    if (std::memcmp(stored + stored_page::kMagic, kStoredPageMagic, sizeof(kStoredPageMagic)) != 0) {
        return fail("bad page magic");
    }
    if (le::readU16(stored + stored_page::kMajor) != kStoredPageMajor ||
        le::readU16(stored + stored_page::kHeaderBytes) != kStoredPageHeaderSize) {
        return fail("unsupported page header");
    }

    const std::uint32_t vertexCount = le::readU32(stored + stored_page::kVertexCount);
    const std::uint32_t indexCount = le::readU32(stored + stored_page::kLocalIndexCount);
    const std::uint32_t encVOffset = le::readU32(stored + stored_page::kEncVertexOffset);
    const std::uint32_t encVBytes = le::readU32(stored + stored_page::kEncVertexBytes);
    const std::uint32_t decVBytes = le::readU32(stored + stored_page::kDecVertexBytes);
    const std::uint32_t encIOffset = le::readU32(stored + stored_page::kEncIndexOffset);
    const std::uint32_t encIBytes = le::readU32(stored + stored_page::kEncIndexBytes);
    const std::uint32_t decIBytes = le::readU32(stored + stored_page::kDecIndexBytes);

    if (le::readU32(stored + stored_page::kVertexStride) != kDecodedVertexStride ||
        le::readU32(stored + stored_page::kIndexStride) != kLocalIndexStride) {
        return fail("unexpected stream strides");
    }
    if (decVBytes != vertexCount * kDecodedVertexStride ||
        decIBytes != indexCount * kLocalIndexStride) {
        return fail("decoded stream sizes disagree with counts");
    }
    if (!rangeWithin(encVOffset, encVBytes, storedSize) ||
        !rangeWithin(encIOffset, encIBytes, storedSize)) {
        return fail("encoded stream is out of bounds");
    }
    if (!rangesDisjoint(encVOffset, encVBytes, encIOffset, encIBytes)) {
        return fail("encoded streams overlap");
    }

    vertices.assign(static_cast<std::size_t>(vertexCount) * kDecodedVertexStride, 0);
    if (meshopt_decodeVertexBuffer(vertices.data(), vertexCount, kDecodedVertexStride,
                                   stored + encVOffset, encVBytes) != 0) {
        return fail("vertex stream decode failed");
    }
    indices.assign(indexCount, 0);
    if (indexCount != 0 &&
        meshopt_decodeIndexBuffer(indices.data(), indexCount, kLocalIndexStride, stored + encIOffset,
                                  encIBytes) != 0) {
        return fail("index stream decode failed");
    }
    return kExitSuccess;
}

} // namespace mlod
