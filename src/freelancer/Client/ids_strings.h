// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <windows.h>

#include "common_text.h" // FmtStr, FmtStr::Val

// IDS lookup from resource DLLs (retail FUN_0057d800 / FUN_0057d980 / FUN_00434720).
// High word of Ids selects the registered HMODULE; low word is the STRINGTABLE id.

// FUN_0057d800: push the module (null included) onto the ids module list; returns the old
// element count << 16. Callers: WinMain 0x005b3127 (resources.dll), FUN_005b0c30 0x005b0f7f
// (each [Resources] DLL).
unsigned int RegisterIdsResourceModule(HMODULE Module);
// FUN_0057d980 without the write-back: the module the high word of Ids names, null when the
// index is past the list.
HMODULE GetIdsResourceModule(unsigned int Ids);

// Returns wchar count excluding NUL. Dest may be null to query length (retail FUN_00434720).
unsigned int GetIdsStringW(unsigned int Ids, wchar_t* Dest, int DestChars);
unsigned int GetIdsStringA(unsigned int Ids, char* Dest, int DestChars);

// Retail FUN_004347e0: like GetIdsStringW, but when the id is missing from the module's
// STRINGTABLE it falls back to a raw UTF-16 RCDATA resource (type 0x17) with the same
// numeric id (retail FUN_0057d9c0), copied from its second word on. The fallback writes Dest
// without a null test, as retail does.
unsigned int GetIdsStringWWithRawFallback(unsigned int Ids, wchar_t* Dest, int DestChars);

// Retail FUN_004bb140 (NN_StoryStar.cpp:0x2b3). Appends one ids-string (via
// GetIdsStringWWithRawFallback) at *pCursor, advancing it by the copied wchar count; FDUMPs a
// warning on a lookup miss. If the copy exactly fills the buffer up to pEnd, the last wchar is
// clobbered with a NUL so a fully-packed buffer still comes out terminated (matches retail's
// pointer-rewind-and-zero tail).
void AppendIdsStringOrWarn(wchar_t** pCursor, wchar_t* pEnd, unsigned int Ids);

// Retail FUN_004ba830 (NN_StoryStar.cpp, cdecl) -- the first Val of Fmt whose TypeTag is Tag and
// whose Flags (the per-kind running index) is Index; null when there is none.
FmtStr::Val const* FmtStrFindValue(FmtStr const* Fmt, unsigned char Index, unsigned short Tag);

// Retail FUN_004ba860 (NN_StoryStar.cpp, cdecl) -- the story star's own mad-lib formatter (not
// MiscUtil's FmtStrSprintf): Fmt's template ids expanded against its Val list into Dest.
// "%[digits][v<digit>]<code>" escapes: ! B F G I L M R S Z d s; 'M' only with
// bIncludeMission. Returns the characters written, terminator excluded. An empty template
// leaves Dest untouched (retail writes nothing then).
int FormatFmtStrMadLib(FmtStr const* Fmt, wchar_t* Dest, int DestChars, bool bIncludeMission);
