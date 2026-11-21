// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// DirectX 8 SDK DirectSound. Do NOT add DX8\include as an include directory (/I)
// — that SDK ships an old basetsd.h that breaks Windows 10 winnt.h (PVOID64).
// Wired via dx8\include appended to cl's INCLUDE (cmake/Freelancer.cmake).
//
// Default SDK: $(Dx8SdkDir) = repo-root dx8/
// CoCreate still uses retail IID last word E5 60, not SDK A7 70.

#ifndef DIRECTSOUND_VERSION
#define DIRECTSOUND_VERSION 0x0800
#endif

#include <windows.h>

#include "dsound.h"
