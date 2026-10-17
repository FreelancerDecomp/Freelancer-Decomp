// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// The IVP library's error-message sink. Moved verbatim from
// src/Common/common_stubs.cpp (via common_stubs_a3.cpp) on 2026-10-14
// (claude-code:cmn-a3). The name stays FUN_06356960 because the vendored
// qhull sources call it at ~400 sites (declared in src/qhull/vendor/mem.h:173,
// C linkage); C++ callers declare it extern "C" (ivp_environment.cpp).

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <windows.h>

// FUN_06356960 (item 3255, Common.dll.txt:437865-437897, 25 insns). Generic
// "print an error message to the debugger" helper: reserves a 10000-byte
// (0x2710) stack buffer (retail's own `_chkstk`-style probe via
// FUN_06391dc0, see ivp_compact_ledge.h's own precedent for this exact
// probe function), formats `Format`/varargs into it via vsprintf, then
// OutputDebugStringA's the result.
//
// Retail curiosity, reproduced faithfully rather than "fixed": retail first
// sprintf()s a literal "ERROR: " prefix into the SAME buffer address the
// vsprintf() call immediately afterward also targets (both LEA computations
// resolve to the identical stack offset, confirmed by hand-tracing every
// intervening PUSH against ESP, Common.dll.txt:06356978-0635699d) -- so the
// vsprintf() call unconditionally overwrites that prefix before
// OutputDebugStringA ever sees it. The sprintf() call's own effect is
// therefore always fully discarded; kept in this port anyway for 1:1
// instruction-level fidelity since it's harmless and not a correctness risk.
// Retail also zero-fills only the first 1000 bytes (0xfa dwords) of the
// buffer before either call, not the whole 10000-byte reservation -- also
// reproduced as-is.
extern "C" void FUN_06356960(const char* Format, ...)
{
    char Buffer[10000]; // retail's own 0x2710-byte stack reservation
    memset(Buffer, 0, 1000); // retail only zero-fills the first 0xfa dwords
    sprintf(Buffer, "ERROR: "); // retail's own dead write -- see comment above
    va_list Args;
    va_start(Args, Format);
    vsprintf(Buffer, Format, Args);
    va_end(Args);
    OutputDebugStringA(Buffer);
}
