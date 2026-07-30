#ifndef MLOD_EXIT_CODE_H
#define MLOD_EXIT_CODE_H

namespace mlod {

// Process exit codes shared by the CLI and every later conversion stage
// (architecture section 7.7). Kept in its own header so both the native
// adapter (cli.h) and the host-independent converter core (diagnostics.h,
// conversion_types.h) can depend on the single definition without a circular
// include between cli.h and the core contracts headers.
enum ExitCode : int {
    kExitSuccess = 0,
    kExitCli = 2,         // CLI argument error
    kExitIo = 3,          // input/output I/O error
    kExitMalformed = 4,   // malformed glTF/GLB/accessor
    kExitUnsupported = 5, // unsupported source feature/layout/material
    kExitHierarchy = 6,   // hierarchy generation failure
    kExitValidation = 7,  // output validation/integrity failure
    kExitWrite = 8,       // final write/rename failure
};

} // namespace mlod

#endif // MLOD_EXIT_CODE_H
