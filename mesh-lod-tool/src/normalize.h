#ifndef MLOD_NORMALIZE_H
#define MLOD_NORMALIZE_H

#include <cstdint>
#include <ostream>
#include <vector>

namespace mlod {

// Supported facts about a primitive's material. Only opaque metallic-roughness
// (optionally unlit and/or double-sided) is supported in v1; anything else is
// rejected during ingestion (architecture section 2, decision 5).
struct MaterialFacts {
    bool hasMaterial = false;
    bool doubleSided = false;
    bool unlit = false;
    bool requiresUv = false; // material references a supported TEXCOORD_0 texture
};

// Raw supported geometry read directly from one glTF primitive, before
// normalization. Arrays are tightly packed (positions/normals: 3 floats per
// vertex, uvs: 2 floats per vertex). normals/uvs are empty when the source omits
// them; indices are empty for unindexed primitives.
struct SourcePrimitive {
    std::uint32_t meshIndex = 0;
    std::uint32_t primitiveIndex = 0;
    std::uint32_t vertexCount = 0;
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<std::uint32_t> indices;
    MaterialFacts material;
};

// Deterministic, indexed, primitive-local CPU geometry ready for hierarchy
// generation. Normals are always present (generated when the source lacked
// them); uvs are present only when the source supplied them.
struct NormalizedPrimitive {
    std::uint32_t meshIndex = 0;
    std::uint32_t primitiveIndex = 0;
    std::vector<float> positions; // 3 per vertex
    std::vector<float> normals;   // 3 per vertex
    std::vector<float> uvs;       // 2 per vertex, empty when absent
    bool hasUv = false;
    std::vector<std::uint32_t> indices; // 3 per triangle
    MaterialFacts material;
    float boundsMin[3] = {0.0f, 0.0f, 0.0f};
    float boundsMax[3] = {0.0f, 0.0f, 0.0f};

    std::uint32_t vertexCount() const {
        return static_cast<std::uint32_t>(positions.size() / 3);
    }
    std::uint32_t triangleCount() const {
        return static_cast<std::uint32_t>(indices.size() / 3);
    }
};

// Normalizes raw source geometry: materializes sequential indices for unindexed
// input, generates finite angle-weighted normals when absent, computes finite
// bounds, and validates that all data is finite and indices are in range.
// Returns kExitSuccess, or kExitMalformed with a contextual diagnostic on err.
int normalizePrimitive(const SourcePrimitive& source, NormalizedPrimitive& out, std::ostream& err);

} // namespace mlod

#endif // MLOD_NORMALIZE_H
