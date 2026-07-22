#include "validator.h"

#include "cli.h"
#include "crc32c.h"
#include "mlod_format.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <utility>
#include <vector>

namespace mlod {
namespace {

struct DirectoryEntry {
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

bool allZero(const unsigned char* p, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (p[i] != 0) {
            return false;
        }
    }
    return true;
}

} // namespace

int validateContainer(const unsigned char* bytes, std::size_t size, std::ostream& err) {
    const auto fail = [&](const char* message) {
        err << "error: .mlod validation: " << message << "\n";
        return kExitValidation;
    };

    if (size < kHeaderSize) {
        return fail("file smaller than the header");
    }
    if (std::memcmp(bytes + header::kMagic, kContainerMagic, sizeof(kContainerMagic)) != 0) {
        return fail("bad container magic");
    }
    if (le::readU16(bytes + header::kFormatMajor) != kFormatMajor) {
        return fail("unsupported format major");
    }
    if (le::readU16(bytes + header::kFormatMinor) != kFormatMinor) {
        return fail("unsupported format minor");
    }
    if (le::readU16(bytes + header::kMinReaderMajor) != kMinReaderMajor ||
        le::readU16(bytes + header::kMinReaderMinor) != kMinReaderMinor) {
        return fail("incompatible minimum reader");
    }
    if (le::readU32(bytes + header::kEndianTag) != kEndianTag) {
        return fail("bad endian tag");
    }
    if (le::readU32(bytes + header::kHeaderBytes) != kHeaderSize) {
        return fail("bad header size");
    }
    if (le::readU64(bytes + header::kTotalFileBytes) != size) {
        return fail("total file bytes disagree with actual size");
    }
    if (!allZero(bytes + header::kReserved, header::kReservedSize)) {
        return fail("header reserved bytes are not zero");
    }

    // Header CRC computed with its own field zeroed.
    std::vector<unsigned char> headerCopy(bytes, bytes + kHeaderSize);
    const std::uint32_t storedHeaderCrc = le::readU32(headerCopy.data() + header::kHeaderCrc);
    le::writeU32(headerCopy.data() + header::kHeaderCrc, 0);
    if (crc32c(headerCopy.data(), kHeaderSize) != storedHeaderCrc) {
        return fail("header CRC mismatch");
    }

    const std::uint32_t sectionCount = le::readU32(bytes + header::kSectionCount);
    const std::uint64_t directoryOffset = le::readU64(bytes + header::kDirectoryOffset);
    const std::uint64_t directoryBytes = le::readU64(bytes + header::kDirectoryBytes);
    if (sectionCount != kRequiredSectionCount) {
        return fail("unexpected section count");
    }
    if (directoryBytes != static_cast<std::uint64_t>(sectionCount) * kSectionEntrySize) {
        return fail("directory size disagrees with section count");
    }
    if (!rangeWithin(directoryOffset, directoryBytes, size) ||
        directoryOffset + directoryBytes > kPageAlignment) {
        return fail("directory is out of the first 64 KiB");
    }
    if (le::readU32(bytes + header::kDirectoryCrc) != crc32c(bytes + directoryOffset, directoryBytes)) {
        return fail("directory CRC mismatch");
    }

    // Parse and validate directory entries.
    std::vector<DirectoryEntry> entries(sectionCount);
    std::uint32_t previousType = 0;
    for (std::uint32_t i = 0; i < sectionCount; ++i) {
        const unsigned char* e = bytes + directoryOffset + i * kSectionEntrySize;
        DirectoryEntry& entry = entries[i];
        entry.type = le::readU32(e + section_entry::kType);
        entry.flags = le::readU32(e + section_entry::kFlags);
        entry.offset = le::readU64(e + section_entry::kOffset);
        entry.storedBytes = le::readU64(e + section_entry::kStoredBytes);
        entry.decodedBytes = le::readU64(e + section_entry::kDecodedBytes);
        entry.elementCount = le::readU32(e + section_entry::kElementCount);
        entry.elementStride = le::readU32(e + section_entry::kElementStride);
        entry.crc = le::readU32(e + section_entry::kCrc);
        entry.alignment = le::readU32(e + section_entry::kAlignment);
        if (!allZero(e + section_entry::kReserved, section_entry::kReservedSize)) {
            return fail("section entry reserved bytes are not zero");
        }
        if (i > 0 && entry.type <= previousType) {
            return fail("directory is not sorted by section type");
        }
        previousType = entry.type;
        if (entry.type != i + 1u) {
            return fail("missing or misordered required section");
        }
        if (!rangeWithin(entry.offset, entry.storedBytes, size)) {
            return fail("section range is out of bounds");
        }
        if (entry.alignment == 0 || entry.offset % entry.alignment != 0) {
            return fail("section is not aligned");
        }
        if (entry.type != kSectionPageData) {
            if (entry.decodedBytes != entry.storedBytes) {
                return fail("metadata decoded size disagrees with stored size");
            }
            if (crc32c(bytes + entry.offset, entry.storedBytes) != entry.crc) {
                return fail("section CRC mismatch");
            }
        } else if (entry.crc != 0) {
            return fail("page-data section must have zero section CRC");
        }
    }

    // Sections must not overlap (touching is allowed); pages live inside the
    // page-data section so they are covered by that single range here.
    for (std::uint32_t i = 0; i < sectionCount; ++i) {
        if (entries[i].offset < directoryOffset + directoryBytes && entries[i].storedBytes > 0 &&
            !rangesDisjoint(entries[i].offset, entries[i].storedBytes, directoryOffset,
                            directoryBytes)) {
            return fail("a section overlaps the directory");
        }
        for (std::uint32_t j = i + 1; j < sectionCount; ++j) {
            if (!rangesDisjoint(entries[i].offset, entries[i].storedBytes, entries[j].offset,
                                entries[j].storedBytes)) {
                return fail("two sections overlap");
            }
        }
    }

    const DirectoryEntry& groups = entries[1];
    const DirectoryEntry& clusters = entries[2];
    const DirectoryEntry& nodes = entries[3];
    const DirectoryEntry& pageRefs = entries[4];
    const DirectoryEntry& pageTable = entries[5];
    const DirectoryEntry& pageData = entries[6];

    const std::uint32_t groupCount = le::readU32(bytes + header::kGroupCount);
    const std::uint32_t clusterCount = le::readU32(bytes + header::kClusterCount);
    const std::uint32_t nodeCount = le::readU32(bytes + header::kNodeCount);
    const std::uint32_t pageCount = le::readU32(bytes + header::kPageCount);
    const std::uint32_t pinnedPageCount = le::readU32(bytes + header::kPinnedPageCount);
    const std::uint32_t levelCount = le::readU32(bytes + header::kLevelCount);
    const std::uint32_t attributeMask = le::readU32(bytes + header::kAttributeMask);

    if ((attributeMask & (kAttributePosition | kAttributeNormal)) !=
        (kAttributePosition | kAttributeNormal)) {
        return fail("attribute mask is missing position or normal");
    }
    if (le::readU32(bytes + header::kVertexStride) != kDecodedVertexStride) {
        return fail("unexpected decoded vertex stride");
    }
    if (groups.elementCount != groupCount || groups.elementStride != kGroupRecordSize ||
        clusters.elementCount != clusterCount || clusters.elementStride != kClusterRecordSize ||
        nodes.elementCount != nodeCount || nodes.elementStride != kHierarchyNodeSize ||
        pageTable.elementCount != pageCount || pageTable.elementStride != kPageTableRecordSize ||
        pageData.elementCount != pageCount) {
        return fail("directory counts disagree with the header");
    }

    // DAG references.
    for (std::uint32_t g = 0; g < groupCount; ++g) {
        const unsigned char* rec = bytes + groups.offset + g * kGroupRecordSize;
        const std::uint32_t depth = le::readU32(rec + group_record::kDepth);
        const std::uint32_t firstCluster = le::readU32(rec + group_record::kFirstCluster);
        const std::uint32_t count = le::readU32(rec + group_record::kClusterCount);
        const std::uint32_t firstRef = le::readU32(rec + group_record::kFirstPageRef);
        const std::uint16_t refCount = le::readU16(rec + group_record::kPageRefCount);
        if (depth >= levelCount) {
            return fail("group depth exceeds level count");
        }
        if (static_cast<std::uint64_t>(firstCluster) + count > clusterCount) {
            return fail("group cluster range is out of bounds");
        }
        if (static_cast<std::uint64_t>(firstRef) + refCount > pageRefs.elementCount) {
            return fail("group page-ref range is out of bounds");
        }
    }
    for (std::uint32_t c = 0; c < clusterCount; ++c) {
        const unsigned char* rec = bytes + clusters.offset + c * kClusterRecordSize;
        const std::uint32_t groupId = le::readU32(rec + cluster_record::kGroupId);
        const std::int32_t refined = le::readI32(rec + cluster_record::kRefinedGroupId);
        const std::uint32_t pageId = le::readU32(rec + cluster_record::kPageId);
        if (groupId >= groupCount) {
            return fail("cluster owning group is out of range");
        }
        if (refined != -1 && (refined < 0 || static_cast<std::uint32_t>(refined) >= groupId)) {
            return fail("cluster refined group is invalid");
        }
        if (pageId >= pageCount) {
            return fail("cluster page id is out of range");
        }
    }
    for (std::uint32_t n = 0; n < nodeCount; ++n) {
        const unsigned char* rec = bytes + nodes.offset + n * kHierarchyNodeSize;
        const std::int32_t group = le::readI32(rec + node_record::kGroup);
        const std::uint32_t firstChild = le::readU32(rec + node_record::kFirstChild);
        const std::uint32_t childCount = le::readU32(rec + node_record::kChildCount);
        if (group == -1) {
            if (childCount == 0 || childCount > 8 ||
                static_cast<std::uint64_t>(firstChild) + childCount > nodeCount) {
                return fail("internal node children are invalid");
            }
        } else if (group < 0 || static_cast<std::uint32_t>(group) >= groupCount || childCount != 0) {
            return fail("leaf node is invalid");
        }
    }
    for (std::uint32_t r = 0; r < pageRefs.elementCount; ++r) {
        if (le::readU32(bytes + pageRefs.offset + r * 4) >= pageCount) {
            return fail("group page ref is out of range");
        }
    }

    // Page table and per-page integrity.
    std::uint64_t previousPageEnd = pageData.offset;
    std::uint64_t pinnedEnd = pageData.offset;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> clusterRanges;
    for (std::uint32_t p = 0; p < pageCount; ++p) {
        const unsigned char* rec = bytes + pageTable.offset + p * kPageTableRecordSize;
        const std::uint64_t offset = le::readU64(rec + page_table::kOffset);
        const std::uint32_t stored = le::readU32(rec + page_table::kStoredBytes);
        const std::uint32_t crc = le::readU32(rec + page_table::kCrc);
        const std::uint32_t flags = le::readU32(rec + page_table::kFlags);
        const std::uint32_t firstCluster = le::readU32(rec + page_table::kFirstCluster);
        const std::uint32_t count = le::readU32(rec + page_table::kClusterCount);
        if (offset % kPageAlignment != 0 || stored % kPageAlignment != 0 || stored < kPageAlignment ||
            stored > 256u * 1024u) {
            return fail("page violates 64-256 KiB alignment");
        }
        if (!rangeWithin(offset, stored, size) || offset != previousPageEnd) {
            return fail("page data is not contiguous");
        }
        previousPageEnd = offset + stored;
        if (crc32c(bytes + offset, stored) != crc) {
            return fail("page CRC mismatch");
        }
        if (std::memcmp(bytes + offset + stored_page::kMagic, kStoredPageMagic,
                        sizeof(kStoredPageMagic)) != 0) {
            return fail("bad stored page magic");
        }
        if (le::readU32(bytes + offset + stored_page::kPageId) != p) {
            return fail("stored page id mismatch");
        }
        const bool pinned = (flags & kPageFlagPinned) != 0;
        if (pinned != (p < pinnedPageCount)) {
            return fail("pinned pages are not a contiguous prefix");
        }
        if (pinned) {
            pinnedEnd = offset + stored;
        }
        if (static_cast<std::uint64_t>(firstCluster) + count > clusterCount) {
            return fail("page cluster range is out of bounds");
        }
        clusterRanges.emplace_back(firstCluster, count);
    }
    // Page ids are ordered pinned-first, so cluster ranges are validated after
    // sorting rather than in page-id order.
    std::sort(clusterRanges.begin(), clusterRanges.end());
    std::uint32_t clusterTiling = 0;
    for (const auto& range : clusterRanges) {
        if (range.first != clusterTiling) {
            return fail("page cluster ranges do not tile");
        }
        clusterTiling += range.second;
    }
    if (clusterTiling != clusterCount) {
        return fail("pages do not cover every cluster");
    }
    if (previousPageEnd != size) {
        return fail("page data does not reach end of file");
    }
    if (le::readU64(bytes + header::kBootstrapBytes) != pinnedEnd) {
        return fail("bootstrap bytes do not match the pinned page prefix");
    }

    return kExitSuccess;
}

} // namespace mlod
