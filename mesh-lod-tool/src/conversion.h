#ifndef MLOD_CONVERSION_H
#define MLOD_CONVERSION_H

#include "conversion_types.h"

namespace mlod {

// Validates the resource graph, primitive inventory, and settings from
// `bundle`/`settings` and returns a path-free inspection report (architecture
// section 7.9 / 7.12): it never builds a hierarchy, packs pages, serializes
// `.mlod` bytes, or calls the writer/validator/packaging code. Diagnostics
// and progress are reported through `diagnostics`/`progress` when present
// (either may be an empty/absent std::function); `cancel` is polled per the
// checkpoints described in architecture 7.14 and once more before returning.
//
// `out.primitives` inventories every document primitive (mesh/primitive
// order) with a `supported` flag, independent of `settings.selection`.
// `out.supportedSelection` is the subset of `settings.selection`'s resolved
// identities that are actually supported -- unsupported primitives inside the
// selection are excluded (with a warning Diagnostic already appended to
// `out.warnings` by inspectDocument) rather than failing the whole request,
// so callers can convert "everything supported" after being
// warned about what isn't. `out.preflight` is computed only over
// `out.supportedSelection` using the architecture 7.12 formula.
//
// Returns true when the entry, resource graph, and settings are valid (an
// out-of-range explicit mesh/primitive selection is the one selection-level
// fatal error, matching native CLI behavior); returns false on a fatal
// validation failure. `out` is always assigned to a well-defined, safe value
// before returning (empty on failure).
bool inspectConversion(const InputBundle& bundle, const ConversionSettings& settings, InspectionResult& out,
                       const DiagnosticSink& diagnostics, const ProgressSink& progress,
                       const CancellationProbe& cancel);

// Runs the complete conversion pipeline entirely in memory: requires a
// successful inspection of `bundle`/`settings` first (see inspectConversion),
// then for every primitive in the inspection's `supportedSelection` (source
// mesh/primitive order) normalizes, builds the hierarchy, packs pages,
// serializes, and independently validates a complete `.mlod` image -- the
// exact same algorithms and defaults the native CLI has always used
// (architecture section 7.9). `out` is populated with every validated
// `PrimitiveOutput` plus canonical aggregate statistics JSON only when EVERY
// primitive succeeds; on any validation, cancellation, or stage failure `out`
// retains zero outputs and `out.success` is false (REQ-BROWSER-7 atomicity --
// no partial results are ever returned). `out.sourceDigest` is the
// host-independent digest over the entry bytes and every referenced external
// geometry buffer (architecture section 7.9). Diagnostics/progress follow the
// same absent-sink-is-a-no-op contract as inspectConversion; `cancel` is
// polled before each primitive and at each stage boundary. Host wall-clock
// timing may be recorded into `out.elapsedHostSeconds` by the caller -- this
// function does not measure time itself, keeping deterministic output bytes
// free of host timing. Returns true on complete success, false otherwise.
bool convert(const InputBundle& bundle, const ConversionSettings& settings, ConversionResult& out,
            const DiagnosticSink& diagnostics, const ProgressSink& progress, const CancellationProbe& cancel);

} // namespace mlod

#endif // MLOD_CONVERSION_H
