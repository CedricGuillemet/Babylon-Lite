#ifndef MLOD_WRITER_H
#define MLOD_WRITER_H

#include "cli.h"
#include "hierarchy.h"
#include "normalize.h"
#include "page_packer.h"

#include <array>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace mlod {

// Assembles the complete .mlod container image in memory: a 256-byte header, a
// type-sorted 64-byte section directory, 64-byte-aligned metadata sections, and
// 64 KiB-aligned page data with pinned pages first inside bootstrapBytes. All
// section, page, directory, and header CRC32C values are computed in dependency
// order (header CRC with its own field zeroed; PAGE_DATA uses per-page CRCs).
// Returns kExitSuccess or kExitWrite.
int writeContainer(const PrimitiveHierarchy& hierarchy, const PackedGeometry& packed,
                   const NormalizedPrimitive& primitive, const ConversionOptions& options,
                   const std::array<std::uint8_t, 32>& sourceDigest,
                   std::vector<unsigned char>& out, std::ostream& err);

// Canonical UTF-8 provenance JSON with lexicographically ordered keys and no
// timestamp. Embedded in the container's PROVENANCE_JSON section and reusable by
// external statistics.
std::string buildProvenanceJson(const PrimitiveHierarchy& hierarchy, const PackedGeometry& packed,
                                const NormalizedPrimitive& primitive,
                                const ConversionOptions& options);

} // namespace mlod

#endif // MLOD_WRITER_H
