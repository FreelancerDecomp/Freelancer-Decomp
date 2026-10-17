// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// dalib_core.h -- the part of DALib.dll's public ABI that needs neither the engine nor the
// render-pipeline headers: the DALIB_API macro, the FileSystem AGGDESC, the two error hooks
// and Startup / OpenData / CloseData / Shutdown. DALib.h includes it and adds the subsystem
// globals; dalib_net.h needs only this part.
//
// Split out because render/IRenderPipeline.h pulls the DirectX 8 headers (d3d8types.h), and
// every MFC translation unit already has d3d9types.h through afxwin.h -> afxrendertarget.h:
// the two define the same enums, so FLServer.exe's MFC code can include this header and
// dalib_net.h, but never DALib.h.
#pragma once

#include <windows.h>

#ifdef DALIB_EXPORTS
#define DALIB_API __declspec(dllexport)
#else
#define DALIB_API __declspec(dllimport)
#endif

// Retail DALib::OpenData AGGDESC size 0x34 @ 0x1610 — ReadFile DOS FileSystem factory.
struct FFileSystemAggDesc
{
    unsigned long Size;
    const char* ComponentName;
    const char* BackendName;
    const char* DataPath;
    unsigned long Field10;
    unsigned long Field14;
    unsigned long Field18;
    unsigned long Field1C;
    unsigned long Field20;
    unsigned long Field24;
    unsigned long Field28;
    unsigned long Field2C;
    unsigned long Field30;
};

using FatalErrorFnType = void(__cdecl*)(unsigned int, const char*);
using CriticalWarningFnType = bool(__cdecl*)(unsigned int, const char*);

DALIB_API extern FatalErrorFnType FatalErrorFn;
DALIB_API extern CriticalWarningFnType CriticalWarningFn;

namespace DALib
{
    // Startup phases (retail DALib.dll.c @ 0x1160): DACOM INI → SystemContainer QIs →
    // Sound +0xA4(hwnd,0x113) / +0xAC / Streamer +0x0C desc 0x18 → Engine → FxEffectLibrary.
    DALIB_API bool __cdecl Startup(HWND hwnd, const char* ini_path);
    DALIB_API void __cdecl OpenData(const char* data_path);
    DALIB_API void __cdecl CloseData();
    DALIB_API void __cdecl Shutdown(HWND hwnd);
}
