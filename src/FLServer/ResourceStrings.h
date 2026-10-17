// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// String-table access through FLServer's list of resource DLLs (retail TU .text:00417430..
// 004176f9). An "ids" value carries the DLL index in its high word and the string id in its
// low word, the scheme the game's data files use (strid_name, ids_info).
#pragma once

#include <windows.h>
#include <vector>

// .data:00429e5c — the loaded resource DLLs in load order (std::vector<HMODULE>; built empty by
// the CRT initializer .text:00417430, storage freed by its atexit thunk 00417460).
extern std::vector<HMODULE> GResourceDlls;

// .text:00417480 (cdecl) — appends Module and returns its ids base, (index << 16).
unsigned int AddResourceDll(HMODULE Module);

// .text:00417600 (cdecl) — reads string Id of Module's RT_STRING table (block (Id >> 4) + 1,
// language 0) into Buf. Returns the copied length (at most BufChars - 1, terminated), or the
// string's length when Buf is null; 0 when the resource is missing (Buf[0] = 0 then) or when
// BufChars < 1 with a buffer.
int LoadResourceString(HMODULE Module, unsigned int Id, wchar_t* Buf, int BufChars);

// .text:004176b0 (cdecl) — LoadResourceString on the DLL named by Ids' high word. An index
// outside GResourceDlls reads module NULL with the unmasked Ids. Unused is never read (both
// retail callers pass 0).
int LoadIdsString(void* Unused, unsigned int Ids, wchar_t* Buf, int BufChars);
