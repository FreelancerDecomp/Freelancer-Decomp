// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Retail FUN_06716c80. The old CRT TLS flag is zero: imeui owns no TLS.
BOOL WINAPI DllMain(HINSTANCE Instance, DWORD Reason, LPVOID Reserved)
{
    if (Reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(Instance);
    return TRUE;
}
