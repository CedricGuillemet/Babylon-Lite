#ifndef MLOD_CLI_H
#define MLOD_CLI_H

#include <ostream>
#include <string>
#include <vector>

namespace mlod {

// Process exit codes shared by the CLI and later conversion stages
// (architecture section 7.7). Keep this the single definition so diagnostics
// map failure classes to stable codes.
enum ExitCode : int {
    kExitSuccess = 0,
    kExitCli = 2,        // CLI argument error
    kExitIo = 3,         // input/output I/O error
    kExitMalformed = 4,  // malformed glTF/GLB/accessor
    kExitUnsupported = 5, // unsupported source feature/layout/material
    kExitHierarchy = 6,  // hierarchy generation failure
    kExitValidation = 7, // output validation/integrity failure
    kExitWrite = 8,      // final write/rename failure
};

// Runs the command-line interface against pre-split arguments (argv without the
// program name). Output and diagnostics are written to the provided streams so
// the behavior can be exercised in-process by the smoke tests. Returns one of
// the ExitCode values.
int runCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

} // namespace mlod

#endif // MLOD_CLI_H
