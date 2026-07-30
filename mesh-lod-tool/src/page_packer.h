#ifndef MLOD_PAGE_PACKER_H
#define MLOD_PAGE_PACKER_H

#include "cli.h"
#include "hierarchy.h"
#include "normalize.h"

#include <cstdint>
#include <ostream>
#include <vector>

namespace mlod {

// One packed, independently decodable geometry page. storedBytes is the exact
// on-disk page (64-byte header + meshopt-encoded vertex/index streams + zero
// padding to a 64 KiB multiple) and its CRC32C covers all of it, padding
// included. fileOffset is assigned later when the container is laid out.
struct PackedPage {
    std::uint32_t pageId = 0;
    bool pinned = false;
    std::uint32_t firstCluster = 0;
    std::uint32_t clusterCount = 0;
    std::uint16_t minDepth = 0;
    std::uint16_t maxDepth = 0;

    std::uint32_t vertexCount = 0;
    std::uint32_t localIndexCount = 0;
    std::uint32_t decodedVertexBytes = 0;
    std::uint32_t decodedIndexBytes = 0;
    std::uint32_t decodedBytes = 0; // 64 KiB-multiple decoded allocation

    std::uint32_t meaningfulBytes = 0; // header + encoded streams, before padding
    std::uint32_t crc = 0;             // CRC32C over storedBytes (padding included)
    std::vector<unsigned char> storedBytes; // 64 KiB-multiple stored page
    std::uint64_t fileOffset = 0;           // assigned during container layout
};

// Result of packing. pages are ordered pinned-first with sequential page ids;
// groupPageRefs is the flattened GROUP_PAGE_REFS section. Cluster page fields and
// group page-ref/pinned fields are written back into the hierarchy.
struct PackedGeometry {
    std::vector<PackedPage> pages;
    std::vector<std::uint32_t> groupPageRefs;
    std::uint32_t pinnedPageCount = 0;
    std::uint64_t totalStoredBytes = 0;
    std::uint64_t totalDecodedBytes = 0;
};

// Packs the hierarchy's clusters into deterministic 64-256 KiB pages, encoding
// meshopt glTF-compatible vertex/index streams, pinning terminal-group pages and
// ordering them first, and writing page references back into the hierarchy.
// Returns kExitSuccess, or kExitValidation on a packing/limit violation.
int packPages(PrimitiveHierarchy& hierarchy, const NormalizedPrimitive& primitive,
              const ConversionSettings& settings, PackedGeometry& out, std::ostream& err);

// Native-adapter overload: maps `options` to ConversionSettings and delegates.
int packPages(PrimitiveHierarchy& hierarchy, const NormalizedPrimitive& primitive,
              const ConversionOptions& options, PackedGeometry& out, std::ostream& err);

// Decodes a stored page back into 24-byte vertex records and u16 local indices.
// Used by verification and as the reference decode for the runtime. Returns
// kExitSuccess or kExitValidation.
int decodeStoredPage(const unsigned char* stored, std::size_t storedSize,
                     std::vector<unsigned char>& vertices, std::vector<std::uint16_t>& indices,
                     std::ostream& err);

// Octahedral snorm16 normal encode/decode used by the 24-byte vertex record.
void octEncodeNormal(float x, float y, float z, std::int16_t& e0, std::int16_t& e1);
void octDecodeNormal(std::int16_t e0, std::int16_t e1, float& x, float& y, float& z);

} // namespace mlod

#endif // MLOD_PAGE_PACKER_H
