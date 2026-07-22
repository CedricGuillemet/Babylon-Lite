#include "cli.h"
#include "crc32c.h"
#include "mlod_format.h"
#include "mlod_version.h"
#include "sha256.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
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

    if (g_failures == 0) {
        std::cout << "all mesh-lod-tool format tests passed\n";
        return 0;
    }
    std::cerr << g_failures << " mesh-lod-tool format test(s) failed\n";
    return 1;
}
