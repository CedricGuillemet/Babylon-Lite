#ifndef MLOD_HIERARCHY_H
#define MLOD_HIERARCHY_H

#include "cli.h"
#include "normalize.h"

#include <cstdint>
#include <ostream>
#include <vector>

namespace mlod {

// One meshlet. Fields mirror the future 64-byte cluster record; page-relative
// fields (page id, in-page vertex/index offsets) are assigned later by packing.
struct HierarchyCluster {
    float center[3] = {0.0f, 0.0f, 0.0f};
    float radius = 0.0f;
    float error = 0.0f; // accumulated finite cluster error
    std::uint32_t groupId = 0;
    std::int32_t refinedGroupId = -1; // -1 = original finest geometry
    std::uint16_t vertexCount = 0;
    std::uint16_t triangleCount = 0;
    std::uint32_t sourceTriangles = 0; // original triangles this cluster represents
    bool normalConeValid = false;
    float normalConeAxis[3] = {0.0f, 0.0f, 0.0f};
    float normalConeCutoff = 1.0f;
    std::uint32_t pageId = 0;              // assigned by page packing
    std::uint32_t firstVertexInPage = 0;   // vertex base within the decoded page
    std::uint32_t firstLocalIndexInPage = 0; // index base (u16 elements) within the page
    std::vector<std::uint32_t> globalIndices;  // triangleCount*3, into primitive vertices
    std::vector<std::uint32_t> localVertices;  // vertexCount unique source vertex ids
    std::vector<std::uint16_t> localTriangles; // triangleCount*3 indices into localVertices
};

// One hierarchy transition unit. Fields mirror the future 64-byte group record.
struct HierarchyGroup {
    float center[3] = {0.0f, 0.0f, 0.0f};
    float radius = 0.0f;
    float simplifiedError = 0.0f; // FLT_MAX only for terminal groups
    std::int32_t depth = 0;
    std::uint32_t firstCluster = 0;
    std::uint32_t clusterCount = 0;
    bool terminal = false;
    bool pinned = false; // terminal (coarse) groups whose pages are pinned
    std::uint32_t firstPageRef = 0;  // index into GROUP_PAGE_REFS, set by packing
    std::uint16_t pageRefCount = 0;  // distinct pages referenced by the group
    std::uint32_t sourceTriangles = 0;  // original triangles represented by the group
    std::uint32_t emittedTriangles = 0; // triangles in the group's emitted clusters
};

// One eight-wide spatial-forest node. Fields mirror the future 32-byte node.
struct HierarchyNode {
    float center[3] = {0.0f, 0.0f, 0.0f};
    float radius = 0.0f;
    float error = 0.0f; // worst-case error in subtree
    std::int32_t group = -1; // -1 = internal node
    std::uint32_t firstChild = 0;
    std::uint32_t childCount = 0;
};

// The complete clustered LOD hierarchy for one material primitive, built in
// primitive-local space. Groups and their clusters are contiguous and source
// ordered; the node forest has one tree per DAG depth with roots first.
struct PrimitiveHierarchy {
    std::uint32_t meshIndex = 0;
    std::uint32_t primitiveIndex = 0;
    MaterialFacts material;
    float boundsMin[3] = {0.0f, 0.0f, 0.0f};
    float boundsMax[3] = {0.0f, 0.0f, 0.0f};
    std::uint32_t sourceVertexCount = 0;
    std::uint32_t sourceTriangleCount = 0;
    std::uint32_t levelCount = 0;
    std::vector<HierarchyGroup> groups;
    std::vector<HierarchyCluster> clusters;
    std::vector<HierarchyNode> nodes;
};

// Builds the clustered group-DAG and eight-wide hierarchy forest from normalized
// geometry using meshoptimizer's clusterlod facilities with the given
// meshlet/partition/simplification settings and mandatory boundary protection.
// Validates the result before returning. Returns kExitSuccess or kExitHierarchy
// with a contextual diagnostic on err.
int buildHierarchy(const NormalizedPrimitive& primitive, const ConversionSettings& settings,
                   PrimitiveHierarchy& out, std::ostream& err);

// Native-adapter overload: maps `options` to ConversionSettings and delegates.
// Kept for native CLI/test source compatibility (architecture section 7.9).
int buildHierarchy(const NormalizedPrimitive& primitive, const ConversionOptions& options,
                   PrimitiveHierarchy& out, std::ostream& err);

// Validates a built hierarchy: contiguity, reference ranges, meshlet limits,
// finite bounds/errors, local-index consistency, node structure, and exact
// terminal-group coverage of the source surface. Returns kExitSuccess or
// kExitHierarchy.
int validateHierarchy(const PrimitiveHierarchy& hierarchy, const ConversionSettings& settings,
                      std::ostream& err);

// Native-adapter overload: maps `options` to ConversionSettings and delegates.
int validateHierarchy(const PrimitiveHierarchy& hierarchy, const ConversionOptions& options,
                      std::ostream& err);

} // namespace mlod

#endif // MLOD_HIERARCHY_H
