#include "statistics.h"

#include "hierarchy.h"
#include "page_packer.h"

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ostream>
#include <string>
#include <vector>

namespace mlod {
namespace {

struct PrimitiveStats {
    std::uint32_t meshIndex = 0;
    std::uint32_t primitiveIndex = 0;
    std::uint64_t groupCount = 0;
    std::uint64_t terminalGroupCount = 0;
    std::uint64_t clusterCount = 0;
    std::uint64_t nodeCount = 0;
    std::uint64_t pageCount = 0;
    std::uint64_t pinnedPageCount = 0;
    std::uint32_t hierarchyLevels = 0;
    std::uint64_t sourceTriangleCount = 0;
    std::uint64_t totalClusterTriangles = 0;
    std::uint64_t terminalCoverage = 0;
    std::uint64_t totalStoredBytes = 0;
    std::uint64_t totalDecodedBytes = 0;
    float maxSimplificationError = 0.0f;
    float boundsMin[3] = {0.0f, 0.0f, 0.0f};
    float boundsMax[3] = {0.0f, 0.0f, 0.0f};
};

PrimitiveStats gather(const PrimitiveHierarchy& h, const PackedGeometry& p) {
    PrimitiveStats s;
    s.meshIndex = h.meshIndex;
    s.primitiveIndex = h.primitiveIndex;
    s.groupCount = h.groups.size();
    s.clusterCount = h.clusters.size();
    s.nodeCount = h.nodes.size();
    s.pageCount = p.pages.size();
    s.pinnedPageCount = p.pinnedPageCount;
    s.hierarchyLevels = h.levelCount;
    s.sourceTriangleCount = h.sourceTriangleCount;
    s.totalStoredBytes = p.totalStoredBytes;
    s.totalDecodedBytes = p.totalDecodedBytes;
    for (const HierarchyGroup& g : h.groups) {
        if (g.terminal) {
            ++s.terminalGroupCount;
            s.terminalCoverage += g.sourceTriangles;
        } else if (std::isfinite(g.simplifiedError)) {
            s.maxSimplificationError = std::fmax(s.maxSimplificationError, g.simplifiedError);
        }
    }
    for (const HierarchyCluster& c : h.clusters) {
        s.totalClusterTriangles += c.triangleCount;
    }
    for (int i = 0; i < 3; ++i) {
        s.boundsMin[i] = h.boundsMin[i];
        s.boundsMax[i] = h.boundsMax[i];
    }
    return s;
}

void appendUint(std::string& target, std::uint64_t value) {
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    target += buffer;
}

void appendFloat(std::string& target, float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
    target += buffer;
}

void appendStatsObject(std::string& json, const PrimitiveStats& s) {
    json += "{\"boundsMax\":[";
    appendFloat(json, s.boundsMax[0]);
    json += ",";
    appendFloat(json, s.boundsMax[1]);
    json += ",";
    appendFloat(json, s.boundsMax[2]);
    json += "],\"boundsMin\":[";
    appendFloat(json, s.boundsMin[0]);
    json += ",";
    appendFloat(json, s.boundsMin[1]);
    json += ",";
    appendFloat(json, s.boundsMin[2]);
    json += "],\"clusterCount\":";
    appendUint(json, s.clusterCount);
    json += ",\"groupCount\":";
    appendUint(json, s.groupCount);
    json += ",\"hierarchyLevels\":";
    appendUint(json, s.hierarchyLevels);
    json += ",\"maxSimplificationError\":";
    appendFloat(json, s.maxSimplificationError);
    json += ",\"meshIndex\":";
    appendUint(json, s.meshIndex);
    json += ",\"nodeCount\":";
    appendUint(json, s.nodeCount);
    json += ",\"pageCount\":";
    appendUint(json, s.pageCount);
    json += ",\"pinnedPageCount\":";
    appendUint(json, s.pinnedPageCount);
    json += ",\"primitiveIndex\":";
    appendUint(json, s.primitiveIndex);
    json += ",\"sourceTriangleCount\":";
    appendUint(json, s.sourceTriangleCount);
    json += ",\"terminalCoverage\":";
    appendUint(json, s.terminalCoverage);
    json += ",\"terminalGroupCount\":";
    appendUint(json, s.terminalGroupCount);
    json += ",\"totalClusterTriangles\":";
    appendUint(json, s.totalClusterTriangles);
    json += ",\"totalDecodedBytes\":";
    appendUint(json, s.totalDecodedBytes);
    json += ",\"totalStoredBytes\":";
    appendUint(json, s.totalStoredBytes);
    json += "}";
}

} // namespace

void writeStatisticsText(const std::vector<PrimitiveHierarchy>& hierarchies,
                         const std::vector<PackedGeometry>& packs, std::ostream& out) {
    for (std::size_t i = 0; i < hierarchies.size(); ++i) {
        const PrimitiveStats s = gather(hierarchies[i], packs[i]);
        out << "mesh " << s.meshIndex << " primitive " << s.primitiveIndex << ":\n";
        out << "  groups=" << s.groupCount << " (terminal " << s.terminalGroupCount << ")"
            << " clusters=" << s.clusterCount << " nodes=" << s.nodeCount
            << " levels=" << s.hierarchyLevels << "\n";
        out << "  sourceTriangles=" << s.sourceTriangleCount
            << " outputTriangles=" << s.totalClusterTriangles
            << " terminalCoverage=" << s.terminalCoverage << "\n";
        out << "  pages=" << s.pageCount << " (pinned " << s.pinnedPageCount << ")"
            << " storedBytes=" << s.totalStoredBytes << " decodedBytes=" << s.totalDecodedBytes
            << "\n";
        out << "  maxSimplificationError=" << s.maxSimplificationError << " boundsMin=["
            << s.boundsMin[0] << "," << s.boundsMin[1] << "," << s.boundsMin[2] << "] boundsMax=["
            << s.boundsMax[0] << "," << s.boundsMax[1] << "," << s.boundsMax[2] << "]\n";
    }
}

std::string buildStatisticsJson(const std::vector<PrimitiveHierarchy>& hierarchies,
                                const std::vector<PackedGeometry>& packs) {
    std::string json = "{\"outputs\":[";
    for (std::size_t i = 0; i < hierarchies.size(); ++i) {
        if (i > 0) {
            json += ",";
        }
        appendStatsObject(json, gather(hierarchies[i], packs[i]));
    }
    json += "],\"primitiveCount\":";
    appendUint(json, hierarchies.size());
    json += "}\n";
    return json;
}

} // namespace mlod
