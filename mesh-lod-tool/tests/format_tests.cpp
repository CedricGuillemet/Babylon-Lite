#include "cli.h"
#include "crc32c.h"
#include "hierarchy.h"
#include "input.h"
#include "mlod_format.h"
#include "mlod_version.h"
#include "mlod_writer.h"
#include "native_filesystem.h"
#include "normalize.h"
#include "page_packer.h"
#include "sha256.h"
#include "validator.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

std::string toHex(const std::array<std::uint8_t, 32>& digest) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (std::uint8_t byte : digest) {
        out.push_back(hex[(byte >> 4) & 0xF]);
        out.push_back(hex[byte & 0xF]);
    }
    return out;
}

void testLayout() {
    expect(mlod::kHeaderSize == 256, "header size is 256");
    expect(mlod::kSectionEntrySize == 64, "section entry is 64");
    expect(mlod::kGroupRecordSize == 64, "group record is 64");
    expect(mlod::kClusterRecordSize == 64, "cluster record is 64");
    expect(mlod::kHierarchyNodeSize == 32, "hierarchy node is 32");
    expect(mlod::kPageTableRecordSize == 64, "page table record is 64");
    expect(mlod::kStoredPageHeaderSize == 64, "stored page header is 64");
    expect(mlod::kDecodedVertexStride == 24, "decoded vertex stride is 24");
    expect(mlod::kPageAlignment == 65536, "page alignment is 64 KiB");

    // Spot-check the header offsets that the reader depends on.
    expect(mlod::header::kEndianTag == 16, "endian tag offset");
    expect(mlod::header::kDirectoryOffset == 32, "directory offset field");
    expect(mlod::header::kSourceDigest == 64, "source digest offset");
    expect(mlod::header::kBuildFingerprint == 96, "build fingerprint offset");
    expect(mlod::header::kHierarchyId == 128, "hierarchy id offset");
    expect(mlod::header::kHeaderCrc == 228, "header CRC offset");
    expect(mlod::header::kDirectoryCrc == 232, "directory CRC offset");

    // The binary format version must agree with the --version provenance.
    expect(mlod::kFormatMajor == static_cast<std::uint16_t>(mlod::kFormatVersionMajor),
           "format major matches version header");
    expect(mlod::kFormatMinor == static_cast<std::uint16_t>(mlod::kFormatVersionMinor),
           "format minor matches version header");

    // Required section identifiers.
    expect(mlod::kSectionProvenanceJson == 1 && mlod::kSectionPageData == 7,
           "section identifiers span 1..7");
}

void testLittleEndian() {
    unsigned char buffer[8] = {};

    mlod::le::writeU16(buffer, 0x1234);
    expect(buffer[0] == 0x34 && buffer[1] == 0x12, "u16 is little-endian");
    expect(mlod::le::readU16(buffer) == 0x1234, "u16 round-trips");

    mlod::le::writeU32(buffer, 0x01020304);
    expect(buffer[0] == 0x04 && buffer[3] == 0x01, "u32 is little-endian");
    expect(mlod::le::readU32(buffer) == 0x01020304u, "u32 round-trips");

    mlod::le::writeU64(buffer, 0x0102030405060708ull);
    expect(buffer[0] == 0x08 && buffer[7] == 0x01, "u64 is little-endian");
    expect(mlod::le::readU64(buffer) == 0x0102030405060708ull, "u64 round-trips");

    mlod::le::writeI32(buffer, -1);
    expect(mlod::le::readI32(buffer) == -1, "i32 round-trips negative");

    mlod::le::writeF32(buffer, 0.15625f);
    expect(mlod::le::readF32(buffer) == 0.15625f, "f32 round-trips");
}

void testCheckedArithmetic() {
    std::uint64_t out = 0;
    expect(mlod::checkedAdd(2, 3, out) && out == 5, "checked add");
    expect(!mlod::checkedAdd(UINT64_MAX, 1, out), "checked add overflow");
    expect(mlod::checkedMul(4, 5, out) && out == 20, "checked mul");
    expect(!mlod::checkedMul(UINT64_MAX, 2, out), "checked mul overflow");

    expect(mlod::rangeWithin(10, 20, 30), "range within bounds");
    expect(!mlod::rangeWithin(20, 20, 30), "range past bounds");
    expect(!mlod::rangeWithin(UINT64_MAX, 1, UINT64_MAX), "range overflow rejected");

    expect(mlod::rangesDisjoint(0, 10, 10, 5), "touching ranges are disjoint");
    expect(!mlod::rangesDisjoint(0, 11, 10, 5), "overlapping ranges detected");
}

void testCrc32c() {
    expect(mlod::crc32c("", 0) == 0u, "CRC32C of empty is 0");
    const char check[] = "123456789";
    expect(mlod::crc32c(check, 9) == 0xE3069283u, "CRC32C check vector");

    // Streaming must equal the one-shot result.
    std::uint32_t streamed = mlod::crc32cUpdate(0, "1234", 4);
    streamed = mlod::crc32cUpdate(streamed, "56789", 5);
    expect(streamed == 0xE3069283u, "CRC32C streaming equals one-shot");
}

void testSha256() {
    expect(toHex(mlod::sha256("", 0)) ==
               "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
           "SHA-256 of empty");
    expect(toHex(mlod::sha256("abc", 3)) ==
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
           "SHA-256 of 'abc'");

    const std::string longMessage =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    expect(toHex(mlod::sha256(longMessage.data(), longMessage.size())) ==
               "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
           "SHA-256 multi-block vector");
}

void testSourceDigest() {
    // Length prefixes make part boundaries unambiguous.
    std::vector<mlod::SourcePart> combined = {{"ab", 2}};
    std::vector<mlod::SourcePart> split = {{"a", 1}, {"b", 1}};
    expect(mlod::computeSourceDigest(combined) != mlod::computeSourceDigest(split),
           "source digest distinguishes part boundaries");

    std::vector<mlod::SourcePart> again = {{"ab", 2}};
    expect(mlod::computeSourceDigest(combined) == mlod::computeSourceDigest(again),
           "source digest is stable for identical parts");

    std::vector<mlod::SourcePart> withExternal = {{"gltf", 4}, {"buffer", 6}};
    std::vector<mlod::SourcePart> withoutExternal = {{"gltf", 4}};
    expect(mlod::computeSourceDigest(withExternal) != mlod::computeSourceDigest(withoutExternal),
           "external geometry buffers affect the source digest");
}

void testBuildFingerprint() {
    mlod::ConversionOptions a;
    a.inputPath = "one.glb";
    a.outputPath = "one.mlod";
    mlod::ConversionOptions b;
    b.inputPath = "two.glb"; // paths must not influence the fingerprint
    b.outputPath = "two.mlod";
    expect(mlod::computeBuildFingerprint(a) == mlod::computeBuildFingerprint(b),
           "build fingerprint ignores file paths");

    // Stability across repeated evaluation (no timestamps).
    expect(mlod::computeBuildFingerprint(a) == mlod::computeBuildFingerprint(a),
           "build fingerprint is stable");

    b.partitionSize = 16; // a conversion-affecting knob
    expect(mlod::computeBuildFingerprint(a) != mlod::computeBuildFingerprint(b),
           "build fingerprint reflects conversion options");

    // The compiler/target string is diagnostic-only (native --version /
    // version diagnostics) and must never enter the build fingerprint or
    // .mlod provenance.
    // Reconstruct the exact hash material WITHOUT the target string and
    // confirm it matches computeBuildFingerprint(a) byte-for-byte: if
    // production code ever reintroduced "target=" into the material, this
    // comparison would fail.
    std::string materialWithoutTarget;
    materialWithoutTarget += "tool_version=";
    materialWithoutTarget += mlod::kToolVersion;
    materialWithoutTarget += "\nformat_version=";
    materialWithoutTarget += std::to_string(mlod::kFormatMajor);
    materialWithoutTarget += ".";
    materialWithoutTarget += std::to_string(mlod::kFormatMinor);
    materialWithoutTarget += "\nmeshoptimizer_revision=";
    materialWithoutTarget += mlod::kMeshoptimizerRev;
    materialWithoutTarget += "\ncgltf_revision=";
    materialWithoutTarget += mlod::kCgltfRev;
    materialWithoutTarget += "\n";
    materialWithoutTarget += mlod::canonicalConversionOptions(a);
    const std::array<std::uint8_t, 32> expected =
        mlod::sha256(materialWithoutTarget.data(), materialWithoutTarget.size());
    expect(mlod::computeBuildFingerprint(a) == expected,
           "build fingerprint matches a hash of tool/format/dependency/settings only (no compiler target)");
}

std::string fixture(const std::string& name) {
    return std::string(MLOD_FIXTURES_DIR) + "/" + name;
}

int buildPacked(const std::string& name, const mlod::ConversionOptions& options,
                mlod::NormalizedPrimitive& normalized, mlod::PrimitiveHierarchy& hierarchy,
                mlod::PackedGeometry& packed) {
    mlod::ConversionOptions loadOptions = options;
    loadOptions.inputPath = fixture(name);
    loadOptions.outputPath = "out.mlod";
    std::vector<mlod::SourcePrimitive> primitives;
    std::ostringstream errStream;
    if (mlod::loadSourcePrimitives(loadOptions, primitives, errStream) != mlod::kExitSuccess ||
        primitives.empty()) {
        return -1;
    }
    if (mlod::normalizePrimitive(primitives[0], normalized, errStream) != mlod::kExitSuccess) {
        return -1;
    }
    if (mlod::buildHierarchy(normalized, loadOptions, hierarchy, errStream) != mlod::kExitSuccess) {
        return -1;
    }
    return mlod::packPages(hierarchy, normalized, loadOptions, packed, errStream);
}

void verifyPageRoundTrip(const mlod::PrimitiveHierarchy& hierarchy,
                         const mlod::NormalizedPrimitive& primitive,
                         const mlod::PackedPage& page) {
    std::vector<unsigned char> vertices;
    std::vector<std::uint16_t> indices;
    std::ostringstream errStream;
    expect(mlod::decodeStoredPage(page.storedBytes.data(), page.storedBytes.size(), vertices,
                                  indices, errStream) == mlod::kExitSuccess,
           "stored page decodes");
    expect(vertices.size() == static_cast<std::size_t>(page.vertexCount) * 24,
           "decoded vertex byte count matches");
    expect(indices.size() == page.localIndexCount, "decoded index count matches");

    // Verify every cluster in the page reconstructs its geometry.
    for (const mlod::HierarchyCluster& c : hierarchy.clusters) {
        if (c.pageId != page.pageId) {
            continue;
        }
        for (std::uint16_t k = 0; k < c.vertexCount; ++k) {
            const std::uint32_t sourceVertex = c.localVertices[k];
            const std::size_t base = (static_cast<std::size_t>(c.firstVertexInPage) + k) * 24;
            const float px = mlod::le::readF32(&vertices[base + 0]);
            const float py = mlod::le::readF32(&vertices[base + 4]);
            const float pz = mlod::le::readF32(&vertices[base + 8]);
            expect(px == primitive.positions[sourceVertex * 3 + 0] &&
                       py == primitive.positions[sourceVertex * 3 + 1] &&
                       pz == primitive.positions[sourceVertex * 3 + 2],
                   "decoded position is exact");
            float nx = 0.0f;
            float ny = 0.0f;
            float nz = 0.0f;
            mlod::octDecodeNormal(static_cast<std::int16_t>(mlod::le::readU16(&vertices[base + 12])),
                                  static_cast<std::int16_t>(mlod::le::readU16(&vertices[base + 14])),
                                  nx, ny, nz);
            const float dot = nx * primitive.normals[sourceVertex * 3 + 0] +
                              ny * primitive.normals[sourceVertex * 3 + 1] +
                              nz * primitive.normals[sourceVertex * 3 + 2];
            expect(dot > 0.99f, "decoded normal is within packing precision");
        }
        // Each cluster triangle round-trips as the same set of source vertices.
        // meshopt's index codec preserves triangles up to winding-preserving
        // rotation, so triangles are compared as sets of decoded positions.
        for (std::uint16_t t = 0; t < c.triangleCount; ++t) {
            std::array<std::array<float, 3>, 3> decodedTri;
            std::array<std::array<float, 3>, 3> expectedTri;
            for (int corner = 0; corner < 3; ++corner) {
                const std::uint16_t pageIndex =
                    indices[c.firstLocalIndexInPage + static_cast<std::size_t>(t) * 3 + corner];
                const std::size_t vbase = static_cast<std::size_t>(pageIndex) * 24;
                decodedTri[static_cast<std::size_t>(corner)] = {
                    mlod::le::readF32(&vertices[vbase + 0]), mlod::le::readF32(&vertices[vbase + 4]),
                    mlod::le::readF32(&vertices[vbase + 8])};
                const std::uint32_t sourceVertex =
                    c.globalIndices[static_cast<std::size_t>(t) * 3 + corner];
                expectedTri[static_cast<std::size_t>(corner)] = {
                    primitive.positions[sourceVertex * 3 + 0],
                    primitive.positions[sourceVertex * 3 + 1],
                    primitive.positions[sourceVertex * 3 + 2]};
            }
            bool matched = true;
            for (const auto& expectedCorner : expectedTri) {
                bool found = false;
                for (const auto& decodedCorner : decodedTri) {
                    if (decodedCorner == expectedCorner) {
                        found = true;
                    }
                }
                matched = matched && found;
            }
            expect(matched, "decoded triangle maps to the correct source vertices");
        }
    }
}

void testPagePacking() {
    // Grid: multi-page packing with pinned prefix and exact decode round trip.
    mlod::ConversionOptions options;
    mlod::NormalizedPrimitive gridNormalized;
    mlod::PrimitiveHierarchy gridHierarchy;
    mlod::PackedGeometry gridPacked;
    expect(buildPacked("grid.gltf", options, gridNormalized, gridHierarchy, gridPacked) ==
               mlod::kExitSuccess,
           "grid packs successfully");
    expect(!gridPacked.pages.empty(), "grid produces pages");
    expect(gridPacked.pinnedPageCount >= 1, "grid pins at least one page");

    bool pinnedPrefix = true;
    for (std::uint32_t i = 0; i < gridPacked.pages.size(); ++i) {
        const bool shouldPin = i < gridPacked.pinnedPageCount;
        if (gridPacked.pages[i].pinned != shouldPin) {
            pinnedPrefix = false;
        }
        const std::size_t size = gridPacked.pages[i].storedBytes.size();
        expect(size >= 65536 && size <= 262144 && size % 65536 == 0,
               "stored page respects 64-256 KiB alignment");
    }
    expect(pinnedPrefix, "pinned pages form a contiguous prefix");

    for (const mlod::PackedPage& page : gridPacked.pages) {
        verifyPageRoundTrip(gridHierarchy, gridNormalized, page);
    }

    // Determinism: a second independent pack is byte-identical.
    mlod::NormalizedPrimitive gridNormalized2;
    mlod::PrimitiveHierarchy gridHierarchy2;
    mlod::PackedGeometry gridPacked2;
    expect(buildPacked("grid.gltf", options, gridNormalized2, gridHierarchy2, gridPacked2) ==
               mlod::kExitSuccess,
           "grid re-packs successfully");
    bool identical = gridPacked.pages.size() == gridPacked2.pages.size();
    for (std::size_t i = 0; identical && i < gridPacked.pages.size(); ++i) {
        identical = gridPacked.pages[i].storedBytes == gridPacked2.pages[i].storedBytes &&
                    gridPacked.pages[i].crc == gridPacked2.pages[i].crc;
    }
    expect(identical, "repeated packing is byte-identical");

    // Single triangle: one pinned 64 KiB page.
    mlod::NormalizedPrimitive triNormalized;
    mlod::PrimitiveHierarchy triHierarchy;
    mlod::PackedGeometry triPacked;
    expect(buildPacked("triangle_indexed.gltf", options, triNormalized, triHierarchy, triPacked) ==
               mlod::kExitSuccess,
           "triangle packs successfully");
    expect(triPacked.pages.size() == 1 && triPacked.pinnedPageCount == 1,
           "triangle yields a single pinned page");
    expect(triPacked.pages[0].storedBytes.size() == 65536, "triangle page is the 64 KiB minimum");

    // Forced smaller pages: reduce the maximum so a group must split, and confirm
    // the result still validates and round-trips.
    mlod::ConversionOptions small = options;
    small.pageMinKiB = 64;
    small.pageTargetKiB = 64;
    small.pageMaxKiB = 64;
    mlod::NormalizedPrimitive smallNormalized;
    mlod::PrimitiveHierarchy smallHierarchy;
    mlod::PackedGeometry smallPacked;
    expect(buildPacked("grid.gltf", small, smallNormalized, smallHierarchy, smallPacked) ==
               mlod::kExitSuccess,
           "grid packs with 64 KiB pages");
    expect(smallPacked.pages.size() >= gridPacked.pages.size(),
           "smaller pages produce at least as many pages");
    for (const mlod::PackedPage& page : smallPacked.pages) {
        expect(page.storedBytes.size() == 65536, "forced-small pages are all 64 KiB");
    }
}

int writeGridContainer(std::vector<unsigned char>& bytes) {
    mlod::ConversionOptions options;
    mlod::NormalizedPrimitive normalized;
    mlod::PrimitiveHierarchy hierarchy;
    mlod::PackedGeometry packed;
    if (buildPacked("grid.gltf", options, normalized, hierarchy, packed) != mlod::kExitSuccess) {
        return -1;
    }
    std::array<std::uint8_t, 32> digest{};
    std::ostringstream errStream;
    return mlod::writeContainer(hierarchy, packed, normalized, options, digest, bytes, errStream);
}

void testWriteValidate() {
    std::vector<unsigned char> bytes;
    expect(writeGridContainer(bytes) == mlod::kExitSuccess, "grid container writes");
    std::ostringstream errStream;
    expect(mlod::validateContainer(bytes.data(), bytes.size(), errStream) == mlod::kExitSuccess,
           "written container reparses and validates");

    // Determinism at the container level.
    std::vector<unsigned char> bytes2;
    expect(writeGridContainer(bytes2) == mlod::kExitSuccess, "grid container re-writes");
    expect(bytes == bytes2, "two writes are byte-identical");

    const auto rejects = [&](std::vector<unsigned char> mutated, const std::string& what) {
        std::ostringstream local;
        expect(mlod::validateContainer(mutated.data(), mutated.size(), local) ==
                   mlod::kExitValidation,
               what);
    };

    // Header field mutation breaks the header CRC.
    std::vector<unsigned char> headerMutated = bytes;
    headerMutated[mlod::header::kBoundsMin] ^= 0x01;
    rejects(headerMutated, "header mutation fails validation");

    // Version mutation is rejected before the CRC check.
    std::vector<unsigned char> versionMutated = bytes;
    mlod::le::writeU16(versionMutated.data() + mlod::header::kFormatMajor, 2);
    rejects(versionMutated, "format version mutation fails validation");

    // Directory mutation breaks the directory CRC.
    const std::uint64_t directoryOffset = mlod::le::readU64(bytes.data() + mlod::header::kDirectoryOffset);
    std::vector<unsigned char> directoryMutated = bytes;
    directoryMutated[directoryOffset + 8] ^= 0x01;
    rejects(directoryMutated, "directory mutation fails validation");

    // Section-body mutation breaks that section's CRC (groups is directory entry 1).
    const std::uint64_t groupOffset =
        mlod::le::readU64(bytes.data() + directoryOffset + mlod::kSectionEntrySize +
                          mlod::section_entry::kOffset);
    std::vector<unsigned char> sectionMutated = bytes;
    sectionMutated[groupOffset] ^= 0x01;
    rejects(sectionMutated, "section mutation fails validation");

    // Page-data mutation (including zero padding) breaks a per-page CRC.
    std::vector<unsigned char> pageMutated = bytes;
    pageMutated[pageMutated.size() - 1] ^= 0x01;
    rejects(pageMutated, "page mutation fails validation");

    // Truncation is rejected.
    std::vector<unsigned char> truncated = bytes;
    truncated.pop_back();
    rejects(truncated, "truncation fails validation");
}

void testProvenanceExcludesCompilerTarget() {
    mlod::ConversionOptions options;
    mlod::NormalizedPrimitive normalized;
    mlod::PrimitiveHierarchy hierarchy;
    mlod::PackedGeometry packed;
    expect(buildPacked("triangle_indexed.gltf", options, normalized, hierarchy, packed) == mlod::kExitSuccess,
          "triangle_indexed.gltf builds for the provenance test");

    const std::string provenance = mlod::buildProvenanceJson(hierarchy, packed, normalized, options);
    expect(provenance.find("compilerTarget") == std::string::npos,
          "provenance JSON never embeds the compiler/target string");
    expect(provenance.find("toolVersion") != std::string::npos, "provenance JSON still embeds the tool version");
    expect(provenance.find("formatVersion") != std::string::npos, "provenance JSON still embeds the format version");

    // Both overloads (ConversionOptions and the canonical ConversionSettings)
    // must produce byte-identical provenance for equivalent settings.
    const std::string provenanceFromSettings =
        mlod::buildProvenanceJson(hierarchy, packed, normalized, mlod::toConversionSettings(options));
    expect(provenance == provenanceFromSettings,
          "the ConversionOptions and ConversionSettings provenance overloads agree exactly");
}

} // namespace

int main() {
    testLayout();
    testLittleEndian();
    testCheckedArithmetic();
    testCrc32c();
    testSha256();
    testSourceDigest();
    testBuildFingerprint();
    testPagePacking();
    testWriteValidate();
    testProvenanceExcludesCompilerTarget();

    if (g_failures == 0) {
        std::cout << "all mesh-lod-tool format tests passed\n";
        return 0;
    }
    std::cerr << g_failures << " mesh-lod-tool format test(s) failed\n";
    return 1;
}
