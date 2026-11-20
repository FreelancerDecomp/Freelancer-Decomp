// Freelancer Decomp: FLRegress, the headless regression harness (port-only, not part of retail).
//
// Crash capture. On an unhandled exception (or a CRT abort, pure call or invalid parameter)
// FLRegress writes two files to the crash directory and exits with code 4:
//
//     FLRegress-<pid>.dmp   a minidump (thread stacks and module list, no heap)
//     FLRegress-<pid>.txt   a text report: exception, phase, symbolised stack and a signature
//
// The report's "signature:" line names the exception code and the first frame in our own
// modules, so the same bug crashing twice gives the same signature. CI uses it to find an
// open issue for the crash instead of opening a new one.

#pragma once

// Installs the handlers. Directory is created if needed; Phase points at the harness's
// current phase name, which the report quotes.
void InstallCrashHandler(const char* Directory, volatile const char* const* Phase);
