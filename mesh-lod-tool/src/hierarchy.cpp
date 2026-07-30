#include "hierarchy.h"

#include "cli.h"
#include "normalize.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "meshoptimizer.h"
#include <cassert>
#define CLUSTERLOD_IMPLEMENTATION
#include "clusterlod.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace mlod {
namespace {

bool finite3(const float v[3]) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

void copyVec3(float dst[3], const float src[3]) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
}

// Distributes each replaced group's source-triangle coverage across the clusters
// that replace it, conserving the total. After this, the sum of terminal groups'
// coverage equals the primitive's source triangle count.
void computeSourceTriangles(PrimitiveHierarchy& h) {
    const std::size_t groupCount = h.groups.size();
    std::vector<std::uint32_t> replaceCount(groupCount, 0);
    for (const HierarchyCluster& c : h.clusters) {
        if (c.refinedGroupId >= 0) {
            ++replaceCount[static_cast<std::size_t>(c.refinedGroupId)];
        }
    }

    std::vector<std::uint32_t> assigned(groupCount, 0);
    for (std::size_t gid = 0; gid < groupCount; ++gid) {
        HierarchyGroup& g = h.groups[gid];
        std::uint32_t source = 0;
        std::uint32_t emitted = 0;
        for (std::uint32_t ci = g.firstCluster; ci < g.firstCluster + g.clusterCount; ++ci) {
            HierarchyCluster& c = h.clusters[ci];
            std::uint32_t share = 0;
            if (c.refinedGroupId < 0) {
                share = c.triangleCount;
            } else {
                const std::size_t r = static_cast<std::size_t>(c.refinedGroupId);
                const std::uint32_t total = h.groups[r].sourceTriangles;
                const std::uint32_t count = replaceCount[r];
                if (count != 0) {
                    share = total / count + (assigned[r] < (total % count) ? 1u : 0u);
                    ++assigned[r];
                } else {
                    share = total;
                }
            }
            c.sourceTriangles = share;
            source += share;
            emitted += c.triangleCount;
        }
        g.sourceTriangles = source;
        g.emittedTriangles = emitted;
    }
}

} // namespace

int buildHierarchy(const NormalizedPrimitive& primitive, const ConversionSettings& options,
                   PrimitiveHierarchy& out, std::ostream& err) {
    const auto fail = [&](const std::string& message) {
        err << "error: mesh " << primitive.meshIndex << " primitive " << primitive.primitiveIndex
            << ": hierarchy " << message << "\n";
        return kExitHierarchy;
    };

    const std::uint32_t vertexCount = primitive.vertexCount();
    if (vertexCount == 0 || primitive.triangleCount() == 0) {
        return fail("has no geometry");
    }

    // Interleave simplification attributes: normals (+ UV when present). All
    // present attributes are protected so hard edges and UV seams survive
    // permissive simplification.
    const bool hasUv = primitive.hasUv;
    const std::uint32_t attributeCount = hasUv ? 5u : 3u;
    std::vector<float> attributes(static_cast<std::size_t>(vertexCount) * attributeCount);
    std::vector<float> weights(attributeCount);
    weights[0] = weights[1] = weights[2] = 0.5f;
    if (hasUv) {
        weights[3] = weights[4] = 1.0f;
    }
    for (std::uint32_t v = 0; v < vertexCount; ++v) {
        float* dst = &attributes[static_cast<std::size_t>(v) * attributeCount];
        dst[0] = primitive.normals[static_cast<std::size_t>(v) * 3 + 0];
        dst[1] = primitive.normals[static_cast<std::size_t>(v) * 3 + 1];
        dst[2] = primitive.normals[static_cast<std::size_t>(v) * 3 + 2];
        if (hasUv) {
            dst[3] = primitive.uvs[static_cast<std::size_t>(v) * 2 + 0];
            dst[4] = primitive.uvs[static_cast<std::size_t>(v) * 2 + 1];
        }
    }

    clodConfig config = clodDefaultConfig(options.meshletMaxTriangles);
    config.max_vertices = options.meshletMaxVertices;
    config.min_triangles = options.meshletMinTriangles;
    config.max_triangles = options.meshletMaxTriangles;
    config.partition_size = options.partitionSize;
    config.simplify_ratio = options.simplifyRatio;
    config.simplify_threshold = options.simplifyThreshold;

    clodMesh mesh = {};
    mesh.indices = primitive.indices.data();
    mesh.index_count = primitive.indices.size();
    mesh.vertex_count = vertexCount;
    mesh.vertex_positions = primitive.positions.data();
    mesh.vertex_positions_stride = sizeof(float) * 3;
    mesh.vertex_attributes = attributes.data();
    mesh.vertex_attributes_stride = sizeof(float) * attributeCount;
    mesh.vertex_lock = nullptr;
    mesh.attribute_weights = weights.data();
    mesh.attribute_count = attributeCount;
    mesh.attribute_protect_mask = (1u << attributeCount) - 1u;

    out = PrimitiveHierarchy{};
    out.meshIndex = primitive.meshIndex;
    out.primitiveIndex = primitive.primitiveIndex;
    out.material = primitive.material;
    copyVec3(out.boundsMin, primitive.boundsMin);
    copyVec3(out.boundsMax, primitive.boundsMax);
    out.sourceVertexCount = vertexCount;
    out.sourceTriangleCount = primitive.triangleCount();

    std::vector<HierarchyGroup>& groups = out.groups;
    std::vector<HierarchyCluster>& clusters = out.clusters;
    bool localFailure = false;

    clodBuild(config, mesh,
              [&](clodGroup group, const clodCluster* groupClusters, size_t count) -> int {
                  const int groupId = static_cast<int>(groups.size());
                  HierarchyGroup g;
                  copyVec3(g.center, group.simplified.center);
                  g.radius = group.simplified.radius;
                  g.simplifiedError = group.simplified.error;
                  g.depth = group.depth;
                  g.firstCluster = static_cast<std::uint32_t>(clusters.size());
                  g.clusterCount = static_cast<std::uint32_t>(count);
                  g.terminal = group.simplified.error == FLT_MAX;
                  g.pinned = g.terminal;

                  for (size_t i = 0; i < count; ++i) {
                      const clodCluster& c = groupClusters[i];
                      HierarchyCluster hc;
                      copyVec3(hc.center, c.bounds.center);
                      hc.radius = c.bounds.radius;
                      hc.error = c.bounds.error;
                      hc.groupId = static_cast<std::uint32_t>(groupId);
                      hc.refinedGroupId = c.refined;
                      hc.triangleCount = static_cast<std::uint16_t>(c.index_count / 3);
                      hc.vertexCount = static_cast<std::uint16_t>(c.vertex_count);
                      hc.globalIndices.assign(c.indices, c.indices + c.index_count);

                      const meshopt_Bounds coneBounds =
                          meshopt_computeClusterBounds(c.indices, c.index_count,
                                                       primitive.positions.data(), vertexCount,
                                                       sizeof(float) * 3);
                      copyVec3(hc.center, coneBounds.center);
                      hc.radius = coneBounds.radius;
                      hc.normalConeValid = coneBounds.cone_cutoff_s8 < 127;
                      if (hc.normalConeValid) {
                          for (std::size_t axis = 0; axis < 3; ++axis) {
                              hc.normalConeAxis[axis] =
                                  static_cast<float>(coneBounds.cone_axis_s8[axis]) / 127.0f;
                          }
                          hc.normalConeCutoff =
                              static_cast<float>(coneBounds.cone_cutoff_s8) / 127.0f;
                      }

                      std::vector<unsigned int> localVertices(c.index_count);
                      std::vector<unsigned char> localTriangles(c.index_count);
                      const size_t unique = clodLocalIndices(localVertices.data(),
                                                             localTriangles.data(), c.indices,
                                                             c.index_count);
                      if (unique != c.vertex_count) {
                          localFailure = true;
                      }
                      localVertices.resize(unique);
                      hc.localVertices.assign(localVertices.begin(), localVertices.end());
                      hc.localTriangles.assign(localTriangles.begin(), localTriangles.end());

                      clusters.push_back(std::move(hc));
                  }

                  groups.push_back(g);
                  return groupId;
              });

    if (localFailure) {
        return fail("meshlet-local index extraction was inconsistent");
    }
    if (groups.empty() || clusters.empty()) {
        return fail("produced no groups or clusters");
    }

    // A simplification can legally produce no replacement clusters. Preserve
    // that region by treating its finest available group as terminal.
    std::vector<bool> replaced(groups.size(), false);
    for (const HierarchyCluster& cluster : clusters) {
        if (cluster.refinedGroupId >= 0) {
            replaced[static_cast<std::size_t>(cluster.refinedGroupId)] = true;
        }
    }
    for (std::size_t groupId = 0; groupId < groups.size(); ++groupId) {
        HierarchyGroup& group = groups[groupId];
        if (!group.terminal && !replaced[groupId]) {
            group.terminal = true;
            group.pinned = true;
            group.simplifiedError = FLT_MAX;
        }
    }

    int maxDepth = 0;
    for (const HierarchyGroup& g : groups) {
        maxDepth = std::max(maxDepth, g.depth);
    }
    out.levelCount = static_cast<std::uint32_t>(maxDepth) + 1u;

    computeSourceTriangles(out);

    // Build the eight-wide spatial forest over the recorded groups.
    std::vector<clodGroup> clodGroups(groups.size());
    for (std::size_t i = 0; i < groups.size(); ++i) {
        clodGroups[i].depth = groups[i].depth;
        copyVec3(clodGroups[i].simplified.center, groups[i].center);
        clodGroups[i].simplified.radius = groups[i].radius;
        clodGroups[i].simplified.error = groups[i].simplifiedError;
    }
    const size_t bound = clodBuildHierarchyBound(groups.size(), 8, out.levelCount);
    std::vector<clodNode> nodes(bound);
    const size_t nodeCount =
        clodBuildHierarchy(nodes.data(), clodGroups.data(), groups.size(), 8, out.levelCount);
    nodes.resize(nodeCount);

    out.nodes.reserve(nodeCount);
    for (const clodNode& n : nodes) {
        HierarchyNode hn;
        copyVec3(hn.center, n.bounds.center);
        hn.radius = n.bounds.radius;
        hn.error = n.bounds.error;
        hn.group = n.group;
        hn.firstChild = n.child_offset;
        hn.childCount = n.child_count;
        out.nodes.push_back(hn);
    }

    return validateHierarchy(out, options, err);
}

int buildHierarchy(const NormalizedPrimitive& primitive, const ConversionOptions& options,
                   PrimitiveHierarchy& out, std::ostream& err) {
    return buildHierarchy(primitive, toConversionSettings(options), out, err);
}

int validateHierarchy(const PrimitiveHierarchy& h, const ConversionSettings& options,
                      std::ostream& err) {
    const auto fail = [&](const std::string& message) {
        err << "error: mesh " << h.meshIndex << " primitive " << h.primitiveIndex << ": hierarchy "
            << message << "\n";
        return kExitHierarchy;
    };

    if (h.groups.empty()) {
        return fail("has no groups");
    }
    if (h.clusters.empty()) {
        return fail("has no clusters");
    }
    if (h.nodes.empty()) {
        return fail("has no hierarchy nodes");
    }
    if (h.levelCount == 0) {
        return fail("has zero DAG levels");
    }

    std::uint32_t expectedFirst = 0;
    std::uint32_t terminalCount = 0;
    std::uint64_t terminalSource = 0;
    for (std::size_t gid = 0; gid < h.groups.size(); ++gid) {
        const HierarchyGroup& g = h.groups[gid];
        if (g.firstCluster != expectedFirst) {
            return fail("groups are not contiguous");
        }
        if (g.clusterCount == 0) {
            return fail("contains an empty group");
        }
        expectedFirst += g.clusterCount;
        if (g.depth < 0 || static_cast<std::uint32_t>(g.depth) >= h.levelCount) {
            return fail("group depth is out of range");
        }
        if (!finite3(g.center) || !std::isfinite(g.radius) || g.radius < 0.0f) {
            return fail("group bounds are not finite");
        }
        if (g.terminal) {
            if (g.simplifiedError != FLT_MAX) {
                return fail("terminal group without FLT_MAX error");
            }
            ++terminalCount;
            terminalSource += g.sourceTriangles;
        } else if (!std::isfinite(g.simplifiedError)) {
            return fail("non-terminal group with non-finite error");
        }
    }
    if (expectedFirst != h.clusters.size()) {
        return fail("group cluster ranges do not cover all clusters");
    }
    if (terminalCount == 0) {
        return fail("has no terminal groups");
    }

    for (std::size_t ci = 0; ci < h.clusters.size(); ++ci) {
        const HierarchyCluster& c = h.clusters[ci];
        if (c.groupId >= h.groups.size()) {
            return fail("cluster owning group is out of range");
        }
        const HierarchyGroup& owner = h.groups[c.groupId];
        if (ci < owner.firstCluster || ci >= owner.firstCluster + owner.clusterCount) {
            return fail("cluster is outside its owning group range");
        }
        if (c.refinedGroupId != -1) {
            if (c.refinedGroupId < 0 || static_cast<std::size_t>(c.refinedGroupId) >= h.groups.size()) {
                return fail("cluster refined group is out of range");
            }
            if (static_cast<std::uint32_t>(c.refinedGroupId) >= c.groupId) {
                return fail("cluster refines a group that is not strictly finer");
            }
        }
        if (c.triangleCount == 0 || c.triangleCount > options.meshletMaxTriangles) {
            return fail("cluster triangle count is out of limits");
        }
        if (c.vertexCount < 3 || c.vertexCount > options.meshletMaxVertices) {
            return fail("cluster vertex count is out of limits");
        }
        if (c.globalIndices.size() != static_cast<std::size_t>(c.triangleCount) * 3) {
            return fail("cluster global index count mismatch");
        }
        if (c.localVertices.size() != c.vertexCount) {
            return fail("cluster local vertex count mismatch");
        }
        if (c.localTriangles.size() != static_cast<std::size_t>(c.triangleCount) * 3) {
            return fail("cluster local index count mismatch");
        }
        for (std::size_t i = 0; i < c.localTriangles.size(); ++i) {
            const std::uint16_t local = c.localTriangles[i];
            if (local >= c.vertexCount) {
                return fail("cluster local index is out of range");
            }
            if (c.localVertices[local] != c.globalIndices[i]) {
                return fail("cluster local index is inconsistent with global index");
            }
        }
        for (const std::uint32_t index : c.globalIndices) {
            if (index >= h.sourceVertexCount) {
                return fail("cluster references an invalid source vertex");
            }
        }
        if (!finite3(c.center) || !std::isfinite(c.radius) || c.radius < 0.0f) {
            return fail("cluster bounds are not finite");
        }
        if (c.normalConeValid &&
            (!finite3(c.normalConeAxis) || !std::isfinite(c.normalConeCutoff) ||
             c.normalConeCutoff < 0.0f || c.normalConeCutoff >= 1.0f)) {
            return fail("cluster normal cone is invalid");
        }
        if (!std::isfinite(c.error)) {
            return fail("cluster error is not finite");
        }
    }

    if (h.nodes.size() < h.levelCount) {
        return fail("has fewer nodes than DAG levels");
    }
    for (const HierarchyNode& n : h.nodes) {
        if (!finite3(n.center) || !std::isfinite(n.radius) || n.radius < 0.0f) {
            return fail("hierarchy node bounds are not finite");
        }
        if (n.group == -1) {
            if (n.childCount == 0 || n.childCount > 8) {
                return fail("internal node child count is out of range");
            }
            if (static_cast<std::uint64_t>(n.firstChild) + n.childCount > h.nodes.size()) {
                return fail("internal node child range is out of bounds");
            }
        } else {
            if (n.group < 0 || static_cast<std::size_t>(n.group) >= h.groups.size()) {
                return fail("leaf node group is out of range");
            }
            if (n.childCount != 0) {
                return fail("leaf node has children");
            }
        }
    }

    // Exact coarse coverage: terminal groups together represent every source
    // triangle exactly once.
    if (terminalSource != h.sourceTriangleCount) {
        std::ostringstream message;
        message << "terminal groups cover " << terminalSource << " source triangles, expected "
                << h.sourceTriangleCount;
        return fail(message.str());
    }

    // Completeness: every group is reachable by refining downward from the
    // terminal (coarsest) groups, so no region is left uncovered.
    std::vector<std::vector<std::uint32_t>> replaces(h.groups.size());
    for (const HierarchyCluster& c : h.clusters) {
        if (c.refinedGroupId >= 0) {
            replaces[c.groupId].push_back(static_cast<std::uint32_t>(c.refinedGroupId));
        }
    }
    std::vector<char> visited(h.groups.size(), 0);
    std::vector<std::uint32_t> stack;
    for (std::size_t gid = 0; gid < h.groups.size(); ++gid) {
        if (h.groups[gid].terminal) {
            visited[gid] = 1;
            stack.push_back(static_cast<std::uint32_t>(gid));
        }
    }
    while (!stack.empty()) {
        const std::uint32_t g = stack.back();
        stack.pop_back();
        for (const std::uint32_t r : replaces[g]) {
            if (!visited[r]) {
                visited[r] = 1;
                stack.push_back(r);
            }
        }
    }
    for (std::size_t gid = 0; gid < h.groups.size(); ++gid) {
        if (!visited[gid]) {
            return fail("a group is not reachable from the terminal coarse representation");
        }
    }

    return kExitSuccess;
}

int validateHierarchy(const PrimitiveHierarchy& h, const ConversionOptions& options, std::ostream& err) {
    return validateHierarchy(h, toConversionSettings(options), err);
}

} // namespace mlod
