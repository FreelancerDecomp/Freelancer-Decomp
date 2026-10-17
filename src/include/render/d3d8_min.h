// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#if defined(RP_DIRECT3D9)

// RP9.dll (src/RP9) only: the public render interfaces speak the Direct3D 8 "language"
// (D3DLIGHT8, D3DMATERIAL8, D3DRENDERSTATETYPE, D3D8 texture-stage numbers, FVF codes), but
// d3d8types.h and d3d9types.h cannot share one translation unit. RP9 therefore sees the
// interface through Direct3D 9 types whose layout is identical to the D3D8 ones; it translates
// everything else (state numbers, device blobs) itself. See docs/re/rp9.md.
//
// <d3d9.h> must resolve to the Windows SDK header: the DirectX SDK (June 2010) include
// directory, which also ships an older d3d9.h, is appended after the Windows SDK in RP9.vcxproj.
// Include this header before any d3dx9*.h -- d3dx9.h pulls "d3d9.h" by quoted path, and the
// shared include guard (_D3D9_H_) then keeps the SDK copy.

#ifndef DIRECT3D_VERSION
#define DIRECT3D_VERSION 0x0900
#endif

#include <stddef.h>
#include <windows.h>

#include <d3d9.h>

// D3DLIGHT9 / D3DMATERIAL9 are field-for-field the D3D8 structures (d3d8types.h).
typedef D3DLIGHT9 D3DLIGHT8;
typedef D3DMATERIAL9 D3DMATERIAL8;

static_assert(sizeof(D3DLIGHT8) == 0x68, "D3DLIGHT9 has the D3DLIGHT8 layout");
static_assert(sizeof(D3DMATERIAL8) == 0x44, "D3DMATERIAL9 has the D3DMATERIAL8 layout");
static_assert(sizeof(D3DRENDERSTATETYPE) == 4, "D3DRENDERSTATETYPE is one dword");

#else

// DirectX 8 SDK headers. Include the SDK file by quoted path so d3d8types.h /
// d3d8caps.h resolve next to it. Do NOT add DX8\include to /I — that SDK ships
// an old basetsd.h that breaks Windows 10 winnt.h (PVOID64).
//
// Default SDK: $(Dx8SdkDir) = repo-root dx8/ = DirectX 8.1 SDK (D3D_SDK_VERSION 220). Retail
// rp8.dll statically links the 8.1 D3DX (D3DXSaveSurfaceToFile chain, FUN_06d2f880) but calls
// Direct3DCreate8 with the literal 0x78 (120, rp8.dll.txt 06d05db7), so RP8 passes 0x78 too
// and does not use D3D_SDK_VERSION.

#ifndef DIRECT3D_VERSION
#define DIRECT3D_VERSION 0x0800
#endif

#include <stddef.h>
#include <windows.h>

#include "d3d8.h"


static_assert(sizeof(D3DPRESENT_PARAMETERS) == 0x34, "D3DPRESENT_PARAMETERS");
static_assert(sizeof(D3DCAPS8) == 0xD4, "D3DCAPS8 DX8");
static_assert(offsetof(D3DCAPS8, DevCaps) == 0x1C, "DevCaps");
static_assert(offsetof(D3DCAPS8, RasterCaps) == 0x24, "RasterCaps");
static_assert(offsetof(D3DCAPS8, TextureCaps) == 0x3C, "TextureCaps");
static_assert(offsetof(D3DCAPS8, MaxTextureWidth) == 0x58, "MaxTextureWidth");
static_assert(offsetof(D3DCAPS8, MaxTextureHeight) == 0x5C, "MaxTextureHeight");

typedef IDirect3D8*(WINAPI* FnDirect3DCreate8)(UINT SdkVersion);

#endif
