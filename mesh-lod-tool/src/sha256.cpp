#include "sha256.h"

#include "cli.h"
#include "mlod_format.h"
#include "mlod_version.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mlod {
namespace {

inline std::uint32_t rotr(std::uint32_t value, std::uint32_t bits) {
    return (value >> bits) | (value << (32 - bits));
}

const std::uint32_t kRoundConstants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

} // namespace

Sha256::Sha256() : buffer_{}, bufferLength_(0), totalBits_(0) {
    state_[0] = 0x6a09e667;
    state_[1] = 0xbb67ae85;
    state_[2] = 0x3c6ef372;
    state_[3] = 0xa54ff53a;
    state_[4] = 0x510e527f;
    state_[5] = 0x9b05688c;
    state_[6] = 0x1f83d9ab;
    state_[7] = 0x5be0cd19;
}

void Sha256::processBlock(const std::uint8_t* block) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + s1 + ch + kRoundConstants[i] + w[i];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(const void* data, std::size_t size) {
    const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
    totalBits_ += static_cast<std::uint64_t>(size) * 8;
    while (size > 0) {
        const std::size_t take = std::min<std::size_t>(64 - bufferLength_, size);
        std::memcpy(buffer_ + bufferLength_, bytes, take);
        bufferLength_ += take;
        bytes += take;
        size -= take;
        if (bufferLength_ == 64) {
            processBlock(buffer_);
            bufferLength_ = 0;
        }
    }
}

std::array<std::uint8_t, 32> Sha256::finalize() {
    const std::uint64_t messageBits = totalBits_;
    const std::uint8_t one = 0x80;
    update(&one, 1);
    const std::uint8_t zero = 0x00;
    while (bufferLength_ != 56) {
        update(&zero, 1);
    }
    std::uint8_t lengthBytes[8];
    for (int i = 0; i < 8; ++i) {
        lengthBytes[i] = static_cast<std::uint8_t>((messageBits >> (56 - i * 8)) & 0xFFu);
    }
    update(lengthBytes, 8);

    std::array<std::uint8_t, 32> digest{};
    for (int i = 0; i < 8; ++i) {
        digest[i * 4] = static_cast<std::uint8_t>((state_[i] >> 24) & 0xFFu);
        digest[i * 4 + 1] = static_cast<std::uint8_t>((state_[i] >> 16) & 0xFFu);
        digest[i * 4 + 2] = static_cast<std::uint8_t>((state_[i] >> 8) & 0xFFu);
        digest[i * 4 + 3] = static_cast<std::uint8_t>(state_[i] & 0xFFu);
    }
    return digest;
}

std::array<std::uint8_t, 32> sha256(const void* data, std::size_t size) {
    Sha256 hash;
    hash.update(data, size);
    return hash.finalize();
}

std::array<std::uint8_t, 32> computeSourceDigest(const std::vector<SourcePart>& parts) {
    Sha256 hash;
    for (const SourcePart& part : parts) {
        unsigned char lengthPrefix[8];
        le::writeU64(lengthPrefix, static_cast<std::uint64_t>(part.size));
        hash.update(lengthPrefix, sizeof(lengthPrefix));
        if (part.size > 0) {
            hash.update(part.data, part.size);
        }
    }
    return hash.finalize();
}

std::array<std::uint8_t, 32> computeBuildFingerprint(const ConversionSettings& options) {
    std::string material;
    material += "tool_version=";
    material += kToolVersion;
    material += "\nformat_version=";
    material += std::to_string(kFormatMajor);
    material += ".";
    material += std::to_string(kFormatMinor);
    material += "\nmeshoptimizer_revision=";
    material += kMeshoptimizerRev;
    material += "\ncgltf_revision=";
    material += kCgltfRev;
    material += "\n";
    material += canonicalConversionSettings(options);
    return sha256(material.data(), material.size());
}

std::array<std::uint8_t, 32> computeBuildFingerprint(const ConversionOptions& options) {
    return computeBuildFingerprint(toConversionSettings(options));
}

} // namespace mlod
