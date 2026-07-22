#ifndef MLOD_VALIDATOR_H
#define MLOD_VALIDATOR_H

#include <cstddef>
#include <ostream>

namespace mlod {

// Independently reparses a .mlod byte image and validates version/compatibility,
// the header and directory CRC32C, section offsets/alignment/non-overlap and
// per-section CRCs, cross-checked record counts, DAG references, per-page records
// and CRCs, bootstrap/pinned layout, and zero-reserved fields. Returns
// kExitSuccess or kExitValidation with a diagnostic on err.
int validateContainer(const unsigned char* bytes, std::size_t size, std::ostream& err);

} // namespace mlod

#endif // MLOD_VALIDATOR_H
