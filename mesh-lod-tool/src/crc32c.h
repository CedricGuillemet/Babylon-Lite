#ifndef MLOD_CRC32C_H
#define MLOD_CRC32C_H

#include <cstddef>
#include <cstdint>

namespace mlod {

// CRC32C (Castagnoli, reflected polynomial 0x82F63B78). crc32cUpdate continues a
// previously returned CRC so a value can be accumulated over multiple spans:
// crc32cUpdate(crc32cUpdate(0, a, na), b, nb) == crc32c(a||b).
std::uint32_t crc32cUpdate(std::uint32_t crc, const void* data, std::size_t size);
std::uint32_t crc32c(const void* data, std::size_t size);

} // namespace mlod

#endif // MLOD_CRC32C_H
