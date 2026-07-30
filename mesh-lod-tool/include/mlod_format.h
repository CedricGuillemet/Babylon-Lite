#ifndef MLOD_FORMAT_H
#define MLOD_FORMAT_H

// Exact .mlod v1 persisted layout (architecture section 8). Every persisted
// value is read/written through the explicit little-endian helpers below; native
// struct packing and host endianness are never serialized. All offsets, strides,
// flags, and reserved regions are named constants so the writer, validator, and
// tests share one source of truth.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mlod {

// ---------------------------------------------------------------------------
// Global constants
// ---------------------------------------------------------------------------

inline constexpr char kContainerMagic[8] = {'M', 'E', 'S', 'H', 'L', 'O', 'D', '\0'};
inline constexpr std::uint16_t kFormatMajor = 1;
inline constexpr std::uint16_t kFormatMinor = 0;
inline constexpr std::uint16_t kMinReaderMajor = 1;
inline constexpr std::uint16_t kMinReaderMinor = 0;
inline constexpr std::uint32_t kEndianTag = 0x01020304u;

inline constexpr std::uint32_t kHeaderSize = 256;
inline constexpr std::uint32_t kSectionEntrySize = 64;
inline constexpr std::uint32_t kGroupRecordSize = 64;
inline constexpr std::uint32_t kClusterRecordSize = 64;
inline constexpr std::uint32_t kHierarchyNodeSize = 32;
inline constexpr std::uint32_t kPageTableRecordSize = 64;
inline constexpr std::uint32_t kStoredPageHeaderSize = 64;

inline constexpr std::uint32_t kSectionAlignment = 64;
inline constexpr std::uint32_t kPageAlignment = 64 * 1024;
inline constexpr std::uint32_t kDecodedVertexStride = 24;
inline constexpr std::uint32_t kLocalIndexStride = 2;

inline constexpr char kStoredPageMagic[4] = {'M', 'L', 'P', 'G'};
inline constexpr std::uint16_t kStoredPageMajor = 1;

// Section type identifiers (directory entries are sorted by this value).
enum SectionType : std::uint32_t {
    kSectionProvenanceJson = 1,
    kSectionGroups = 2,
    kSectionClusters = 3,
    kSectionHierarchyNodes = 4,
    kSectionGroupPageRefs = 5,
    kSectionPageTable = 6,
    kSectionPageData = 7,
};
inline constexpr std::uint32_t kRequiredSectionCount = 7;

// Section-directory entry flag bits.
enum SectionFlag : std::uint32_t {
    kSectionFlagRequired = 1u << 0,
    kSectionFlagOptional = 1u << 1,
    kSectionFlagPerItemCrc = 1u << 2,
    kSectionFlagPageData = 1u << 3,
};

// Header attribute mask bits.
enum AttributeBit : std::uint32_t {
    kAttributePosition = 1u << 0,
    kAttributeNormal = 1u << 1,
    kAttributeUv0 = 1u << 2,
};

// Group record flag bits.
enum GroupFlag : std::uint16_t {
    kGroupFlagTerminal = 1u << 0,
    kGroupFlagPinnedCoarse = 1u << 1,
};

enum ClusterFlag : std::uint32_t {
    kClusterFlagNormalCone = 1u << 0,
};

// Page-table / stored-page flag bits.
enum PageFlag : std::uint32_t {
    kPageFlagPinned = 1u << 0,
    kPageFlagCoarse = 1u << 1,
};

// ---------------------------------------------------------------------------
// Field offsets
// ---------------------------------------------------------------------------

namespace header {
inline constexpr std::size_t kMagic = 0;            // 8 bytes
inline constexpr std::size_t kFormatMajor = 8;      // u16
inline constexpr std::size_t kFormatMinor = 10;     // u16
inline constexpr std::size_t kMinReaderMajor = 12;  // u16
inline constexpr std::size_t kMinReaderMinor = 14;  // u16
inline constexpr std::size_t kEndianTag = 16;       // u32
inline constexpr std::size_t kHeaderBytes = 20;     // u32
inline constexpr std::size_t kContainerFlags = 24;  // u32
inline constexpr std::size_t kSectionCount = 28;    // u32
inline constexpr std::size_t kDirectoryOffset = 32; // u64
inline constexpr std::size_t kDirectoryBytes = 40;  // u64
inline constexpr std::size_t kBootstrapBytes = 48;  // u64
inline constexpr std::size_t kTotalFileBytes = 56;  // u64
inline constexpr std::size_t kSourceDigest = 64;    // 32 bytes
inline constexpr std::size_t kBuildFingerprint = 96; // 32 bytes
inline constexpr std::size_t kHierarchyId = 128;    // 16 bytes
inline constexpr std::size_t kSourceMeshIndex = 144;      // u32
inline constexpr std::size_t kSourcePrimitiveIndex = 148; // u32
inline constexpr std::size_t kSourceTriangleCount = 152;  // u64
inline constexpr std::size_t kTotalClusterTriangles = 160; // u64
inline constexpr std::size_t kClusterCount = 168;   // u32
inline constexpr std::size_t kGroupCount = 172;     // u32
inline constexpr std::size_t kNodeCount = 176;      // u32
inline constexpr std::size_t kPageCount = 180;      // u32
inline constexpr std::size_t kPinnedPageCount = 184; // u32
inline constexpr std::size_t kLevelCount = 188;     // u32
inline constexpr std::size_t kAttributeMask = 192;  // u32
inline constexpr std::size_t kVertexStride = 196;   // u32
inline constexpr std::size_t kBoundsMin = 200;      // 12 bytes (3 floats)
inline constexpr std::size_t kBoundsMax = 212;      // 12 bytes
inline constexpr std::size_t kMaxNonterminalError = 224; // f32
inline constexpr std::size_t kHeaderCrc = 228;      // u32 (computed with this field zero)
inline constexpr std::size_t kDirectoryCrc = 232;   // u32
inline constexpr std::size_t kReserved = 236;       // 20 bytes
inline constexpr std::size_t kReservedSize = 20;
} // namespace header

namespace section_entry {
inline constexpr std::size_t kType = 0;          // u32
inline constexpr std::size_t kFlags = 4;         // u32
inline constexpr std::size_t kOffset = 8;        // u64
inline constexpr std::size_t kStoredBytes = 16;  // u64
inline constexpr std::size_t kDecodedBytes = 24; // u64
inline constexpr std::size_t kElementCount = 32; // u32
inline constexpr std::size_t kElementStride = 36; // u32
inline constexpr std::size_t kCrc = 40;          // u32
inline constexpr std::size_t kAlignment = 44;    // u32
inline constexpr std::size_t kReserved = 48;     // 16 bytes
inline constexpr std::size_t kReservedSize = 16;
} // namespace section_entry

namespace group_record {
inline constexpr std::size_t kSphere = 0;          // float32x4 (center + radius)
inline constexpr std::size_t kError = 16;          // f32
inline constexpr std::size_t kDepth = 20;          // u32
inline constexpr std::size_t kFirstCluster = 24;   // u32
inline constexpr std::size_t kClusterCount = 28;   // u32
inline constexpr std::size_t kFirstPageRef = 32;   // u32
inline constexpr std::size_t kPageRefCount = 36;   // u16
inline constexpr std::size_t kFlags = 38;          // u16
inline constexpr std::size_t kSourceTriangles = 40; // u32
inline constexpr std::size_t kEmittedTriangles = 44; // u32
inline constexpr std::size_t kReserved = 48;       // 16 bytes
inline constexpr std::size_t kReservedSize = 16;
} // namespace group_record

namespace cluster_record {
inline constexpr std::size_t kSphere = 0;         // float32x4
inline constexpr std::size_t kError = 16;         // f32
inline constexpr std::size_t kGroupId = 20;       // u32
inline constexpr std::size_t kRefinedGroupId = 24; // i32
inline constexpr std::size_t kPageId = 28;        // u32
inline constexpr std::size_t kFirstVertex = 32;   // u32
inline constexpr std::size_t kFirstLocalIndex = 36; // u32 (in u16 elements)
inline constexpr std::size_t kVertexCount = 40;   // u16
inline constexpr std::size_t kTriangleCount = 42; // u16
inline constexpr std::size_t kSourceTriangles = 44; // u32
inline constexpr std::size_t kFlags = 48;         // u32
inline constexpr std::size_t kNormalCone = 52;    // u32
inline constexpr std::size_t kConeCutoff = 56;    // u32
inline constexpr std::size_t kReserved = 60;      // u32
inline constexpr std::size_t kReservedSize = 4;
} // namespace cluster_record

namespace node_record {
inline constexpr std::size_t kSphere = 0;      // float32x4
inline constexpr std::size_t kError = 16;      // f32
inline constexpr std::size_t kGroup = 20;      // i32
inline constexpr std::size_t kFirstChild = 24; // u32
inline constexpr std::size_t kChildCount = 28; // u32
} // namespace node_record

namespace page_table {
inline constexpr std::size_t kOffset = 0;           // u64
inline constexpr std::size_t kStoredBytes = 8;      // u32
inline constexpr std::size_t kMeaningfulBytes = 12; // u32
inline constexpr std::size_t kDecodedBytes = 16;    // u32
inline constexpr std::size_t kCrc = 20;             // u32
inline constexpr std::size_t kVertexCount = 24;     // u32
inline constexpr std::size_t kLocalIndexCount = 28; // u32
inline constexpr std::size_t kVertexByteOffset = 32; // u32
inline constexpr std::size_t kIndexByteOffset = 36; // u32
inline constexpr std::size_t kFirstCluster = 40;    // u32
inline constexpr std::size_t kClusterCount = 44;    // u32
inline constexpr std::size_t kFlags = 48;           // u32
inline constexpr std::size_t kMinDepth = 52;        // u16
inline constexpr std::size_t kMaxDepth = 54;        // u16
inline constexpr std::size_t kReserved = 56;        // 8 bytes
inline constexpr std::size_t kReservedSize = 8;
} // namespace page_table

namespace stored_page {
inline constexpr std::size_t kMagic = 0;             // 4 bytes
inline constexpr std::size_t kMajor = 4;             // u16
inline constexpr std::size_t kHeaderBytes = 6;       // u16
inline constexpr std::size_t kPageId = 8;            // u32
inline constexpr std::size_t kFlags = 12;            // u32
inline constexpr std::size_t kVertexCount = 16;      // u32
inline constexpr std::size_t kLocalIndexCount = 20;  // u32
inline constexpr std::size_t kEncVertexOffset = 24;  // u32
inline constexpr std::size_t kEncVertexBytes = 28;   // u32
inline constexpr std::size_t kDecVertexBytes = 32;   // u32
inline constexpr std::size_t kEncIndexOffset = 36;   // u32
inline constexpr std::size_t kEncIndexBytes = 40;    // u32
inline constexpr std::size_t kDecIndexBytes = 44;    // u32
inline constexpr std::size_t kVertexStride = 48;     // u32
inline constexpr std::size_t kIndexStride = 52;      // u32
inline constexpr std::size_t kReserved = 56;         // 8 bytes
inline constexpr std::size_t kReservedSize = 8;
} // namespace stored_page

// Layout self-checks: each record's final field plus its size fills the record.
static_assert(header::kReserved + header::kReservedSize == kHeaderSize, "header layout");
static_assert(section_entry::kReserved + section_entry::kReservedSize == kSectionEntrySize,
              "section entry layout");
static_assert(group_record::kReserved + group_record::kReservedSize == kGroupRecordSize,
              "group record layout");
static_assert(cluster_record::kReserved + cluster_record::kReservedSize == kClusterRecordSize,
              "cluster record layout");
static_assert(node_record::kChildCount + 4 == kHierarchyNodeSize, "node record layout");
static_assert(page_table::kReserved + page_table::kReservedSize == kPageTableRecordSize,
              "page table layout");
static_assert(stored_page::kReserved + stored_page::kReservedSize == kStoredPageHeaderSize,
              "stored page header layout");

// ---------------------------------------------------------------------------
// Little-endian read/write helpers
// ---------------------------------------------------------------------------

namespace le {

inline void writeU16(unsigned char* p, std::uint16_t v) {
    p[0] = static_cast<unsigned char>(v & 0xFFu);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xFFu);
}

inline void writeU32(unsigned char* p, std::uint32_t v) {
    p[0] = static_cast<unsigned char>(v & 0xFFu);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xFFu);
    p[2] = static_cast<unsigned char>((v >> 16) & 0xFFu);
    p[3] = static_cast<unsigned char>((v >> 24) & 0xFFu);
}

inline void writeU64(unsigned char* p, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<unsigned char>((v >> (8 * i)) & 0xFFu);
    }
}

inline void writeI32(unsigned char* p, std::int32_t v) {
    writeU32(p, static_cast<std::uint32_t>(v));
}

inline void writeF32(unsigned char* p, float v) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    writeU32(p, bits);
}

inline std::uint16_t readU16(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}

inline std::uint32_t readU32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

inline std::uint64_t readU64(const unsigned char* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    }
    return v;
}

inline std::int32_t readI32(const unsigned char* p) {
    return static_cast<std::int32_t>(readU32(p));
}

inline float readF32(const unsigned char* p) {
    std::uint32_t bits = readU32(p);
    float v = 0.0f;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

} // namespace le

// ---------------------------------------------------------------------------
// Overflow-checked integer arithmetic and range helpers
// ---------------------------------------------------------------------------

inline bool checkedAdd(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
    if (a > UINT64_MAX - b) {
        return false;
    }
    out = a + b;
    return true;
}

inline bool checkedMul(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
    if (a != 0 && b > UINT64_MAX / a) {
        return false;
    }
    out = a * b;
    return true;
}

// True if [offset, offset+length) fits within [0, total) without overflow.
inline bool rangeWithin(std::uint64_t offset, std::uint64_t length, std::uint64_t total) {
    std::uint64_t end = 0;
    return checkedAdd(offset, length, end) && end <= total;
}

// True if [offsetA, +lengthA) and [offsetB, +lengthB) do not overlap (touching
// is allowed). Assumes both ranges are individually valid.
inline bool rangesDisjoint(std::uint64_t offsetA, std::uint64_t lengthA, std::uint64_t offsetB,
                           std::uint64_t lengthB) {
    return offsetA + lengthA <= offsetB || offsetB + lengthB <= offsetA;
}

} // namespace mlod

#endif // MLOD_FORMAT_H
