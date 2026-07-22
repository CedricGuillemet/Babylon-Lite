#include "crc32c.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace mlod {
namespace {

const std::array<std::uint32_t, 256>& crcTable() {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> generated{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t crc = i;
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc & 1u) ? (0x82F63B78u ^ (crc >> 1)) : (crc >> 1);
            }
            generated[i] = crc;
        }
        return generated;
    }();
    return table;
}

} // namespace

std::uint32_t crc32cUpdate(std::uint32_t crc, const void* data, std::size_t size) {
    const std::array<std::uint32_t, 256>& table = crcTable();
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8);
    }
    return ~crc;
}

std::uint32_t crc32c(const void* data, std::size_t size) {
    return crc32cUpdate(0, data, size);
}

} // namespace mlod
