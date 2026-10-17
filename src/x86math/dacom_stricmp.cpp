// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Isolated TU: import DACOM's stricmp without CRT <cstring> macros remapping the symbol.
// Retail x86math.dll IAT: DACOM.DLL!stricmp (export alias of DacomStricmp).

extern "C" __declspec(dllimport) int __cdecl stricmp(const char* Left, const char* Right);

int CallDacomStricmp(const char* Left, const char* Right)
{
    return stricmp(Left, Right);
}
