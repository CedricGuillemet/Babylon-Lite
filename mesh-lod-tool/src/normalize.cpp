#include "normalize.h"

#include "cli.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <ostream>
#include <vector>

namespace mlod {
namespace {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

Vec3 loadVec3(const std::vector<float>& data, std::uint32_t vertex) {
    const std::size_t base = static_cast<std::size_t>(vertex) * 3;
    return Vec3{data[base], data[base + 1], data[base + 2]};
}

Vec3 subtract(const Vec3& a, const Vec3& b) {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float length(const Vec3& v) {
    return std::sqrt(dot(v, v));
}

Vec3 normalizeOrZero(const Vec3& v) {
    const float len = length(v);
    if (len <= 0.0f || !std::isfinite(len)) {
        return Vec3{0.0f, 0.0f, 0.0f};
    }
    return Vec3{v.x / len, v.y / len, v.z / len};
}

bool allFinite(const std::vector<float>& data) {
    for (const float value : data) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

// Angle-weighted vertex normals: each triangle contributes its unit face normal
// to each incident vertex, weighted by the corner angle at that vertex. Result
// is always finite; a vertex with no valid contribution falls back to +Z.
void generateNormals(const std::vector<float>& positions, const std::vector<std::uint32_t>& indices,
                     std::uint32_t vertexCount, std::vector<float>& normals) {
    std::vector<Vec3> accumulated(vertexCount, Vec3{0.0f, 0.0f, 0.0f});

    for (std::size_t tri = 0; tri + 2 < indices.size(); tri += 3) {
        const std::uint32_t i0 = indices[tri];
        const std::uint32_t i1 = indices[tri + 1];
        const std::uint32_t i2 = indices[tri + 2];
        const Vec3 p0 = loadVec3(positions, i0);
        const Vec3 p1 = loadVec3(positions, i1);
        const Vec3 p2 = loadVec3(positions, i2);

        const Vec3 faceNormal = normalizeOrZero(cross(subtract(p1, p0), subtract(p2, p0)));
        if (faceNormal.x == 0.0f && faceNormal.y == 0.0f && faceNormal.z == 0.0f) {
            continue; // degenerate triangle
        }

        const std::array<std::uint32_t, 3> corners = {i0, i1, i2};
        const std::array<Vec3, 3> points = {p0, p1, p2};
        for (int corner = 0; corner < 3; ++corner) {
            const Vec3& here = points[static_cast<std::size_t>(corner)];
            const Vec3& next = points[static_cast<std::size_t>((corner + 1) % 3)];
            const Vec3& prev = points[static_cast<std::size_t>((corner + 2) % 3)];
            const Vec3 edge1 = normalizeOrZero(subtract(next, here));
            const Vec3 edge2 = normalizeOrZero(subtract(prev, here));
            float cosAngle = dot(edge1, edge2);
            cosAngle = std::fmax(-1.0f, std::fmin(1.0f, cosAngle));
            const float angle = std::acos(cosAngle);
            const float weight = std::isfinite(angle) ? angle : 0.0f;
            Vec3& target = accumulated[corners[static_cast<std::size_t>(corner)]];
            target.x += faceNormal.x * weight;
            target.y += faceNormal.y * weight;
            target.z += faceNormal.z * weight;
        }
    }

    normals.resize(static_cast<std::size_t>(vertexCount) * 3);
    for (std::uint32_t v = 0; v < vertexCount; ++v) {
        Vec3 n = normalizeOrZero(accumulated[v]);
        if (n.x == 0.0f && n.y == 0.0f && n.z == 0.0f) {
            n = Vec3{0.0f, 0.0f, 1.0f};
        }
        const std::size_t base = static_cast<std::size_t>(v) * 3;
        normals[base] = n.x;
        normals[base + 1] = n.y;
        normals[base + 2] = n.z;
    }
}

} // namespace

int normalizePrimitive(const SourcePrimitive& source, NormalizedPrimitive& out, std::ostream& err) {
    const auto context = [&](const char* message) {
        err << "error: mesh " << source.meshIndex << " primitive " << source.primitiveIndex << ": "
            << message << "\n";
    };

    if (source.positions.empty() || source.positions.size() % 3 != 0) {
        context("POSITION data is empty or not a multiple of three floats");
        return kExitMalformed;
    }
    const std::uint32_t vertexCount = static_cast<std::uint32_t>(source.positions.size() / 3);
    if (source.vertexCount != 0 && source.vertexCount != vertexCount) {
        context("declared vertex count does not match POSITION data");
        return kExitMalformed;
    }

    NormalizedPrimitive result;
    result.meshIndex = source.meshIndex;
    result.primitiveIndex = source.primitiveIndex;
    result.material = source.material;
    result.positions = source.positions;

    if (!allFinite(result.positions)) {
        context("POSITION contains non-finite values");
        return kExitMalformed;
    }

    // Indices: validate provided indices, or synthesize sequential ones.
    if (source.indices.empty()) {
        if (vertexCount % 3 != 0) {
            context("unindexed geometry vertex count is not a multiple of three");
            return kExitMalformed;
        }
        result.indices.resize(vertexCount);
        for (std::uint32_t i = 0; i < vertexCount; ++i) {
            result.indices[i] = i;
        }
    } else {
        if (source.indices.size() % 3 != 0) {
            context("index count is not a multiple of three");
            return kExitMalformed;
        }
        for (const std::uint32_t index : source.indices) {
            if (index >= vertexCount) {
                context("index references a vertex outside the vertex range");
                return kExitMalformed;
            }
        }
        result.indices = source.indices;
    }

    // Normals: copy finite source normals or generate angle-weighted ones.
    if (!source.normals.empty()) {
        if (source.normals.size() != static_cast<std::size_t>(vertexCount) * 3) {
            context("NORMAL count does not match vertex count");
            return kExitMalformed;
        }
        if (!allFinite(source.normals)) {
            context("NORMAL contains non-finite values");
            return kExitMalformed;
        }
        result.normals = source.normals;
    } else {
        generateNormals(result.positions, result.indices, vertexCount, result.normals);
        if (!allFinite(result.normals)) {
            context("generated normals are non-finite");
            return kExitMalformed;
        }
    }

    // UVs: optional, but must be well-formed and finite when present.
    if (!source.uvs.empty()) {
        if (source.uvs.size() != static_cast<std::size_t>(vertexCount) * 2) {
            context("TEXCOORD_0 count does not match vertex count");
            return kExitMalformed;
        }
        if (!allFinite(source.uvs)) {
            context("TEXCOORD_0 contains non-finite values");
            return kExitMalformed;
        }
        result.uvs = source.uvs;
        result.hasUv = true;
    }

    // Bounds over the primitive-local positions.
    float minX = result.positions[0];
    float minY = result.positions[1];
    float minZ = result.positions[2];
    float maxX = minX;
    float maxY = minY;
    float maxZ = minZ;
    for (std::uint32_t v = 1; v < vertexCount; ++v) {
        const std::size_t base = static_cast<std::size_t>(v) * 3;
        minX = std::fmin(minX, result.positions[base]);
        minY = std::fmin(minY, result.positions[base + 1]);
        minZ = std::fmin(minZ, result.positions[base + 2]);
        maxX = std::fmax(maxX, result.positions[base]);
        maxY = std::fmax(maxY, result.positions[base + 1]);
        maxZ = std::fmax(maxZ, result.positions[base + 2]);
    }
    result.boundsMin[0] = minX;
    result.boundsMin[1] = minY;
    result.boundsMin[2] = minZ;
    result.boundsMax[0] = maxX;
    result.boundsMax[1] = maxY;
    result.boundsMax[2] = maxZ;
    if (!std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(minZ) ||
        !std::isfinite(maxX) || !std::isfinite(maxY) || !std::isfinite(maxZ)) {
        context("computed bounds are non-finite");
        return kExitMalformed;
    }

    out = std::move(result);
    return kExitSuccess;
}

} // namespace mlod
