// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// ============================================================================
// ErrorHandler — retail's FDUMP backend. A small set of named "log" channels
// (real files, or the virtual "$Text"/"$Window" sinks) that log_comment()
// broadcasts formatted lines to. ErrorHandler::activate() installs this
// subsystem as the process-wide dacom.h FDUMP hook and optionally seeds
// channels from an ini file's [Error] "log = Name" lines; deactivate() tears
// everything down and restores whatever FDUMP was installed before.
// ============================================================================

// Retail's global severity enum -- the low nibble of an FDUMP flags word (log_last_error
// masks it with & 0xf and ORs in 0x100000). The only value passed to log_last_error in the
// retail binaries is 2, always with a "WARNING" format (freelancer.exe .text:005661f9,
// 00566a2a, 00566ae2, 00573c4c); 1 is the nibble every "*** ERROR" FDUMP uses (0x100001).
// The enumerator names are the port's.
enum SeverityLevel
{
    SeverityError = 1,
    SeverityWarning = 2
};

namespace ErrorHandler
{
    // Common.dll.c ErrorHandler::activate @ 0xaad30. Installs the FDUMP hook
    // (LAB_0630abd0, common_errorhandler.cpp FdumpHook): each FDUMP call goes to every
    // active channel whose LogId is -1 or equals flags >> 4 and whose severity limit
    // is not below the flags' low nibble. A non-null AssertHandler is stored into
    // PTR_LAB_063ed214 (common_errorhandler.cpp g_ErrorAssertHandler, default = the
    // MessageBoxA wrapper LAB_0630ab90); retail Common never reads that pointer back.
    COMMON_API void __cdecl activate(char const* IniPath,
                                      int (__cdecl* AssertHandler)(char const*, char const*, bool));
    COMMON_API void __cdecl deactivate();

    // Common.dll.c ErrorHandler::new_log @ 0xab320. Name[0]=='$' selects a virtual sink
    // ("$Text"/"$Window") instead of opening a real file for write ("wb"). Returns an
    // opaque handle (nullptr when the channel ends up with no sink); there is no
    // by-handle free, only deactivate(). SeverityLimit: the FDUMP hook drops calls whose
    // severity nibble is above it (the exe passes 7). Retail's insertion rule is
    // asymmetric: non-$Window channels are pushed to the front of the internal list,
    // $Window channels are appended at the back.
    COMMON_API void* __cdecl new_log(char const* Name, int LogId, int SeverityLimit,
                                      bool bText, bool bWindow);

    // Common.dll.c @ 0xab460 / 0xab4b0. LogId == -1 means "all channels".
    COMMON_API void __cdecl log_enable(int LogId);
    COMMON_API void __cdecl log_disable(int LogId);
    // Common.dll.c @ 0xab4e0. Unlike log_enable/log_disable, -1 is NOT special here --
    // it only matches a channel literally registered with LogId == -1.
    COMMON_API bool __cdecl is_log_enabled(int LogId);
    // Common.dll.c @ 0xab540. Formats once, writes the (newline-terminated) result to
    // the file of every active channel whose LogId equals LogId (LogId -1: every channel).
    COMMON_API void __cdecl log_comment(int LogId, char const* Format, ...);

    // Common.dll.txt:0630b680 (0xab680). Win32 GetLastError()-style helper: formats
    // ErrorCode via FormatMessageA (FORMAT_MESSAGE_ALLOCATE_BUFFER|FROM_SYSTEM|
    // IGNORE_INSERTS, neutral/default language), then calls
    // FDUMP((Severity & 0xf) | 0x100000, Format, Source, Line, <formatted message>)
    // and LocalFree()s the FormatMessage buffer. `Format` is the caller-supplied
    // printf-style template (e.g. "%s(%d) : *** ERROR: ...: %s") -- Source/Line are
    // its first two substitutions, the Win32 message text is the third. Retail's
    // signature is ?log_last_error@ErrorHandler@@YAXPBDHW4SeverityLevel@@PADK@Z: the
    // severity is the global enum SeverityLevel (only ever masked with & 0xf) and the
    // format is a non-const char*.
    COMMON_API void __cdecl log_last_error(char const* Source, int Line, SeverityLevel Severity,
                                            char* Format, unsigned long ErrorCode);
}
