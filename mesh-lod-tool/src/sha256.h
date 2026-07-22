#ifndef MLOD_SHA256_H
#define MLOD_SHA256_H

#include "cli.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mlod {

// Streaming SHA-256. finalize() may be called once per instance.
class Sha256 {
public:
    Sha256();
    void update(const void* data, std::size_t size);
    std::array<std::uint8_t, 32> finalize();

private:
    void processBlock(const std::uint8_t* block);

    std::uint32_t state_[8];
    std::uint8_t buffer_[64];
    std::size_t bufferLength_;
    std::uint64_t totalBits_;
};

std::array<std::uint8_t, 32> sha256(const void* data, std::size_t size);

// One geometry span contributing to the source-bundle digest.
struct SourcePart {
    const void* data;
    std::size_t size;
};

// Length-prefixed digest over the glTF/GLB bytes plus every external geometry
// buffer used by the selected primitive, in the order supplied. The 8-byte
// little-endian length prefix makes the concatenation unambiguous. Images are
// excluded by the caller (they never affect geometry output).
std::array<std::uint8_t, 32> computeSourceDigest(const std::vector<SourcePart>& parts);

// Deterministic build fingerprint over the tool version, format version, both
// dependency revisions, compiler/target string, and canonical conversion
// options only. It contains no timestamp or host path.
std::array<std::uint8_t, 32> computeBuildFingerprint(const ConversionOptions& options);

} // namespace mlod

#endif // MLOD_SHA256_H
