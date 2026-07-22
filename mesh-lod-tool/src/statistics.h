#ifndef MLOD_STATISTICS_H
#define MLOD_STATISTICS_H

#include "hierarchy.h"
#include "page_packer.h"

#include <ostream>
#include <string>
#include <vector>

namespace mlod {

// Human-readable per-primitive conversion statistics (groups, meshlets, source
// and output triangles, hierarchy depth, bounds, simplification error, and page
// layout) written to a stream for interactive inspection.
void writeStatisticsText(const std::vector<PrimitiveHierarchy>& hierarchies,
                         const std::vector<PackedGeometry>& packs, std::ostream& out);

// Canonical UTF-8 statistics JSON with lexicographically fixed keys and no
// timestamp, suitable for automated verification (--stats-json).
std::string buildStatisticsJson(const std::vector<PrimitiveHierarchy>& hierarchies,
                                const std::vector<PackedGeometry>& packs);

} // namespace mlod

#endif // MLOD_STATISTICS_H
