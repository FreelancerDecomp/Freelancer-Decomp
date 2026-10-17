// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <windows.h>

struct FApproach2Player;
struct IGraphBuilder;
struct IMediaEventEx;
struct IMediaControl;
struct IBasicVideo;
struct IVideoWindow;
struct IUnknown;

// Global state variables (BSS)
extern int g_stateFlag;           // DAT_06ae0930: 0=not initialized, 1=active, 2=fullscreen
extern FApproach2Player* g_playerObject; // DAT_06ae092c
extern IGraphBuilder* g_graphBuilder;     // DAT_06ae0940
extern IMediaEventEx* g_mediaEventEx;     // DAT_06ae0934
extern IMediaControl* g_mediaControl;     // DAT_06ae0938
extern IBasicVideo* g_basicVideo;         // DAT_06ae0944
extern IVideoWindow* g_videoWindow;       // DAT_06ae093c
extern int g_initSuccess;         // DAT_06ae0954: initialization success flag
extern unsigned char g_playing;   // DAT_06ae0955: playback active flag
extern HWND g_hwndParent;         // DAT_06ae094c: parent window handle
extern WNDPROC g_originalWndProc; // DAT_06ae0950: original WindowProc for subclassing
extern IUnknown* g_serviceProvider;       // DAT_06ae0948
extern HMODULE g_movieModule;     // DAT_06ae0904

// Exported functions (7 C++ style exports with decorated names)
namespace MediaPlayer
{
    // Approach enum inside namespace to match retail decoration
    enum Approach
    {
        ApproachStandard = 0
    };

    __declspec(dllexport) void __cdecl Startup();
    __declspec(dllexport) bool __cdecl UpdateMovie();
    __declspec(dllexport) void __cdecl StopMovie();
    __declspec(dllexport) long __cdecl PlayMovie(HWND hwnd, bool fullscreen, const char* path);
    __declspec(dllexport) void __cdecl FreeMovie();
    __declspec(dllexport) void __cdecl Shutdown();
    __declspec(dllexport) void __cdecl SetApproach(Approach approach);
}
