// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <windows.h>

// 0x06ac1e50. Builds the filter graph with WM ASF Reader, calls Run, returns while playback continues.
long __cdecl BuildAndRunWmAsfGraph(HWND Hwnd, int Fullscreen, const char* Path);

// 0x06ac16a0. Logs when Code < 0. Returns true when Code < 0.
bool __cdecl Movie_LogNegativeCode(long Code, const char* File, int Line);

// 0x06ac1bd0. Returns the playback flag.
bool __cdecl Movie_IsPlaying();

// 0x06ac1be0. Releases the COM pointers and restores the window procedure.
void __cdecl Movie_ReleaseComAndRestoreWndProc();

// 0x06ac1ce0. Empty.
void __cdecl Movie_Nop();
