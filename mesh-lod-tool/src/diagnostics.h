#ifndef MLOD_DIAGNOSTICS_H
#define MLOD_DIAGNOSTICS_H

#include "exit_code.h"

#include <cstdint>
#include <functional>
#include <string>

namespace mlod {

// Severity of a Diagnostic. Errors abort the current request; warnings and
// info are informational and never change the ExitCode/success outcome.
enum class DiagnosticSeverity : int {
    kInfo = 0,
    kWarning = 1,
    kError = 2,
};

// Structured, optional context describing exactly which part of the source,
// resource graph, or options a diagnostic concerns (architecture section 7.9
// / 7.18). Every field is optional; absence is represented by empty strings
// or `has*` flags, never by overloading a numeric field as a sentinel.
struct DiagnosticContext {
    std::string resourceUri;  // offending glTF buffer/image URI or virtual path
    std::string gltfProperty; // e.g. "buffers[2].uri", "meshes[0].primitives[1]"

    bool hasMesh = false;
    std::uint32_t meshIndex = 0;
    bool hasPrimitive = false;
    std::uint32_t primitiveIndex = 0;
    bool hasAccessor = false;
    std::uint32_t accessorIndex = 0;

    std::string extensionName; // e.g. "KHR_materials_clearcoat"
    std::string optionName;    // e.g. "pageTargetKiB"

    bool hasOutputRange = false;
    std::uint64_t outputRangeBegin = 0;
    std::uint64_t outputRangeEnd = 0;
};

// A stable diagnostic code, its native exit category, severity, human
// message, and optional structured context (architecture section 7.9).
// `code` is stable API surface for tests and diagnostics; `message`
// is presentation text and may be reworded freely.
struct Diagnostic {
    std::string code;
    ExitCode nativeExitCategory = kExitSuccess;
    DiagnosticSeverity severity = DiagnosticSeverity::kError;
    std::string message;
    DiagnosticContext context;
};

// Callback-style diagnostic sink. A default-constructed (empty) std::function
// means "absent": callers MUST check for emptiness before use and MUST NOT
// assume the sink retains any reference to the Diagnostic after it returns
// (architecture section 7.9 implementation detail 4: absent sinks perform no
// allocation or logging).
using DiagnosticSink = std::function<void(const Diagnostic&)>;

// Cooperative cancellation probe. Returns true once cancellation has been
// requested; an absent probe (empty std::function) means "never cancelled".
// MUST NOT block and MUST NOT throw (architecture section 7.14).
using CancellationProbe = std::function<bool()>;

// Stable diagnostic code families (architecture section 7.18). Native and
// adapters attach one of these to every Diagnostic; callers
// layer adds its own additional `MLOD-*` families on top but never redefines
// these native ones.
namespace diag_code {
constexpr const char* kCliOption = "MLOD-OPTION-INVALID";
constexpr const char* kCliSelection = "MLOD-OPTION-SELECTION";
constexpr const char* kIoRead = "MLOD-IO-READ";
constexpr const char* kIoWrite = "MLOD-IO-WRITE";
constexpr const char* kMalformed = "MLOD-INPUT-MALFORMED";
constexpr const char* kUnsupported = "MLOD-UNSUPPORTED";
constexpr const char* kHierarchy = "MLOD-CONVERT-HIERARCHY";
constexpr const char* kOutputValidation = "MLOD-OUTPUT-VALIDATION";
constexpr const char* kWrite = "MLOD-OUTPUT-WRITE";
} // namespace diag_code

// Builds a Diagnostic from its parts. Pure value construction; never
// allocates beyond the strings/context supplied.
Diagnostic makeDiagnostic(std::string code, ExitCode nativeExitCategory, DiagnosticSeverity severity,
                          std::string message, DiagnosticContext context = DiagnosticContext{});

// Emits `diagnostic` through `sink` if present; a no-op (empty) sink performs
// no allocation or logging.
void emitDiagnostic(const DiagnosticSink& sink, const Diagnostic& diagnostic);

} // namespace mlod

#endif // MLOD_DIAGNOSTICS_H
