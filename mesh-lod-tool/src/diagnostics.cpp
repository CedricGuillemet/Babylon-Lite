#include "diagnostics.h"

#include <utility>

namespace mlod {

Diagnostic makeDiagnostic(std::string code, ExitCode nativeExitCategory, DiagnosticSeverity severity,
                          std::string message, DiagnosticContext context) {
    Diagnostic diagnostic;
    diagnostic.code = std::move(code);
    diagnostic.nativeExitCategory = nativeExitCategory;
    diagnostic.severity = severity;
    diagnostic.message = std::move(message);
    diagnostic.context = std::move(context);
    return diagnostic;
}

void emitDiagnostic(const DiagnosticSink& sink, const Diagnostic& diagnostic) {
    if (sink) {
        sink(diagnostic);
    }
}

} // namespace mlod
