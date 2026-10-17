// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <windows.h>

// Client BSS / globals recovered from freelancer.exe WinMain (FUN_005b2f50).

// FUN_00417c50 -- the exe's bounded string copy: copies min(strlen(Src), Capacity - 1) chars and
// a terminator into Dest and returns the count copied. A null Src returns 0 and leaves Dest
// untouched (callers that can see a null clear the buffer themselves). Shared by the options
// object below and many loaders across the exe.
unsigned CopyBoundedString(char* Dest, unsigned Capacity, const char* Src);

// The {length, text[0x104]} path buffer the options object keeps its strings in. FUN_00417c50
// fills Text and the caller stores the returned length in front of it; the accessor retail calls
// (ICF-folded with MD5Hash::GetStatePtr, .text:004d9980 `LEA EAX,[ECX+4]`) returns Text.
struct FClientPathString
{
    unsigned Length;  // +0x00
    char Text[0x104]; // +0x04

    const char* c_str() const { return Text; }
};
static_assert(sizeof(FClientPathString) == 0x108, "FClientPathString stride is 0x108");

// The options object at DAT_00679bc0 (this of FUN_005621a0). Its first 0x28 bytes are the
// ten-dword render mode FUN_004249c0 hands to RP8 create_buffers as the out-blob (RP8 FRp8Mode:
// Field00, Field04, Width, Height, Format, WantedZ, WantedStencil, Interval, BufferCount,
// SwapExtra, Fullscreen); the rest is FUN_005621a0's freelancer.ini state. Field defaults are
// the stores at 0x00562225..0x00562265.
struct DisplaySettings
{
    int mode_field00;   // +0x000  -1
    int refresh_rate;   // +0x004  0x3c
    int width;          // +0x008  0x400
    int height;         // +0x00c  0x300
    int color_bpp;      // +0x010  -1   (RP8 Format)
    int depth_bpp;      // +0x014  -1   (RP8 WantedZ)
    int stencil_bpp;    // +0x018  0    (RP8 WantedStencil)
    int interval;       // +0x01c  1
    int buffer_count;   // +0x020  2
    char border;        // +0x024  1    (FUN_004242e0 "B")
    char fullscreen;    // +0x025  1    (FUN_004242e0 "F"/"W"; read as DAT_00679be5)
    char pad26[2];
    // +0x028 .. +0x140: the debug option table FUN_005792b0 reads (DAT_00679be8); written only
    // by the scatter loop at 0x005621c0. See game_options.h.
    int options[0x46];
    FClientPathString ini_path;       // +0x140  "Freelancer.ini" (text is DAT_00679d04)
    FClientPathString data_path;      // +0x248  "..\\Data" / [Freelancer] data path (DAT_00679e0c)
    FClientPathString movie_files[3]; // +0x350  [Startup] movie_file, first three only
    FClientPathString gate_tunnels;   // +0x668  [Data] gate_tunnels
    FClientPathString jump_effect;    // +0x770  [Data] jump_effect
    char field_878;                   // +0x878  (DAT_0067a438) cleared by FUN_005621a0 through `this`;
                                      //         read once, by ClientWndProc @0x005b2667 -- modelled
                                      //         there as window.cpp's kActivateOverrideByte (always 0)
    char quick_flag;                  // +0x879  "+Q"/"-Q": set to 1, never read anywhere in the PE
    char pad87a[2];
};
static_assert(sizeof(DisplaySettings) == 0x87c, "options object ends after +0x879");

// WinMain's command-line block (local_5e4, 0x244 bytes of WinMain's frame). FUN_005b33f0 zeroes
// Count; FUN_005b3400 copies lpCmdLine into Buffer and splits it in place on " \t", honouring
// double quotes, appending each token to Argv. Retail bounds neither the token count nor the
// copied length, and neither does the port.
struct FClientCommandLine
{
    unsigned Count;     // +0x000
    char* Argv[16];     // +0x004
    char Buffer[0x200]; // +0x044

    FClientCommandLine();                // FUN_005b33f0
    void Parse(const char* CommandLine); // FUN_005b3400
};
static_assert(sizeof(FClientCommandLine) == 0x244, "WinMain local_5e4 is 0x244 bytes");

struct IEnvironment;

struct ClientGlobals
{
    HINSTANCE instance;
    HWND window;
    HMODULE resources;
    unsigned int packed_version;
    IEnvironment* current_state; // DAT_0067ecac — the current client state (client_environment.h)
};

extern DisplaySettings g_display; // DAT_00679bc0
extern ClientGlobals g_client;

// DAT_0067ecbc -- "+X"/"-X" (FUN_005621a0's second command-line loop); WinMain and the frame
// bracket (FUN_00424f30 / FUN_00425080) gate FUN_005b27c0 on it.
extern bool G_bFpExceptionTrap;

// DAT_0067ecb4 -- the "WinDebug" root window: InitializeEngine stores
// FUN_0059db20("WinDebug", "WinDebug", 0, 0) (0x005b1d73), the atexit teardown deletes its tree
// (0x005b223c..0x005b226f, FUN_0043d2a0 inlined). No WinFactory in the exe registers the class
// "WinDebug" (the only copy of the string, 0x005e6a8c, is referenced by those two pushes alone),
// so the lookup finds nothing and the root is always null in retail.
struct Win3D;
extern Win3D* G_WinDebugRoot;

// FUN_005621a0 -- thiscall on the options object at DAT_00679bc0, argument WinMain's command line.
void LoadStartupConfig(DisplaySettings* Options, const FClientCommandLine* CommandLine);
// FUN_004242e0 -- thiscall on a render mode block (the first 0x28 bytes of the DisplaySettings
// layout): applies "+B/-B", "+F/-F", "+W", "+M<size>". True if any of those letters was seen.
bool ApplyDisplayCmdlineOverrides(DisplaySettings* Mode, const FClientCommandLine* CommandLine);

HWND CreateGameWindow(const char* class_name, DisplaySettings* display, const char* title);
// FUN_00424870 -- record the surface size/mode and restyle + resize the window for it.
void ApplyGameWindowLayout(HWND Hwnd, bool bFullscreen, unsigned Width, unsigned Height);

// RenderMgr.cpp state.
extern HWND G_RenderMgrWindow;          // DAT_006679f4 -- the game window (FUN_004246e0)
extern unsigned G_RenderSurfaceWidth;   // DAT_00610850 -- surface size last laid out (0x280)
extern unsigned G_RenderSurfaceHeight;  // DAT_00610854 -- (0x1e0)
extern bool G_bRenderSurfaceFullscreen; // DAT_006679f0
extern bool G_bRenderMgrShuttingDown;   // DAT_006679f9 -- FUN_004247c0 re-entry latch
extern float G_RenderGamma;             // DAT_00610858 -- .data 1.3f; "gamma_ramp" option + 1.0
extern int G_bRenderDither;             // DAT_0061085c -- .data 1; "DitherControl" option >= 0.3
extern int G_RenderMipFilter;           // DAT_00610860 -- .data 1; "MipFilterControlCallback"
// DAT_00610864 -- .data 1. Cleared by FUN_004249c0 on entry (0x00424a09) and by FUN_00424c60 when
// a fullscreen surface is re-activated (0x00424c7c); set at the end of a successful
// FUN_004249c0 (0x00424c4f); FUN_00425140 stores (SwapBuffers == 0) into it (0x00425158).
extern bool G_bRenderPostCreate;
// DAT_006679f8 -- this frame's BeginScene succeeded. Written only by FUN_00424f30 (which returns
// it); FUN_00424cb0 reads it.
extern bool G_bRenderFrameBegun;
// DAT_00610848 -- .data 0x35, never written: the FPU precision the renderer wants (0x35 = 53-bit,
// 0x18 = 24-bit). DAT_0061084c -- .data 0x18, the precision FUN_004249c0 found in the control word
// after create_buffers (0x00424b6c). FUN_00424f30 puts the found one in place for the frame,
// FUN_00425080 puts the wanted one back.
extern int G_FpuPrecisionWanted;
extern int G_FpuPrecisionFound;

// FUN_00424510 -- QI the pipeline for IGammaControl; with it, SetGammaEnable(fullscreen) and
// SetGammaFunction(7, G_RenderGamma, 0, 1.0f, 0). True when the function call succeeded.
bool RenderMgrApplyGamma();
// The apply slots (vtable slot 1) of the three RenderMgr.cpp perf options.
void ApplyGammaRampOption(float Value);         // LAB_004244e0
void ApplyDitherControlOption(float Value);     // LAB_00424630
void ApplyMipFilterControlOption(float Value);  // LAB_004246d0

// FUN_004247c0 -- release the render device. With bRestoreWindowedMode (the fatal-error and
// crash paths) a fullscreen session is first switched back to a windowed surface.
void ShutdownRenderMgr(bool bRestoreWindowedMode);
// FUN_004249c0 -- create the device buffers for the render mode in the first 0x28 bytes of Mode;
// the pipeline writes the mode it actually got to Out (the options object at every retail call).
bool RenderMgrCreateBuffers(HWND Hwnd, const DisplaySettings* Mode, DisplaySettings* Out);
// FUN_00424ce0 (cdecl) -- try Mode (its first 0x28 bytes); a windowed mode then retries Format
// 32/24/16, then the options object itself, then a 640x480 window. Callers: InitializeEngine
// (engine_init.cpp) and NN_Preferences::TryApplyVideoMode 0x004b1ce4.
bool SetRenderModeWithFallback(HWND Hwnd, const DisplaySettings* Mode);
LRESULT CALLBACK ClientWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

// DAT_00615ba8 — WinMain only presents a frame while this is set (0x5b0fe8). WM_ACTIVATEAPP
// clears it when a fullscreen single-player session loses focus.
bool ClientPresentAllowed();
// DAT_00610865 — latched by FUN_00424c60 on WM_ACTIVATEAPP.
bool ClientAppIsActive();
// FUN_00424cb0 -- DAT_00610865 && DAT_00610864 && DAT_006679f8. Callers: the loading screen's
// Update (0x00578f8b) and WndProc's SC_CLOSE (0x005b2727).
bool RenderMgrFrameReady();
// DAT_0067ecc8 / DAT_0067ecc9 — WinMain arms the quit screen once InitializeEngine returns and
// replays a close that arrived earlier.
void ClientNoteEngineReady();
// FUN_00471f30(0) — raise NN_Exit (nn_exit.h NnExitOpen).
void RequestClientQuitDialog();

bool PumpMessages(int* exit_code); // FUN_005b0b60
void UpdateClientTiming();         // FUN_005b2360
void TickClientFrameTimer();       // FUN_0042d770 on its own
double GetClientAccumulatedTime();
void SeedClientAccumulatedTime(double Seconds); // FUN_005ab380 -> _DAT_0067ecd8
float GetClientFrameDeltaSeconds();
// The retail frame-timer accessors around FUN_0042d770 (0042d650..0042d6e0).
double GetClientFrameTimeCapped();      // FUN_0042d680 — 0.0 while paused
double GetClientFrameTimeRaw();         // FUN_0042d6a0 — 0.0 while paused
double GetClientElapsedTime();          // FUN_0042d660 — not pause-gated
unsigned GetClientFrameCount();         // FUN_0042d6e0
unsigned long GetClientFrameTimestampMs(); // FUN_0042d670
void PushClientTimingPause();           // FUN_0042d6c0
void PopClientTimingPause();            // FUN_0042d6d0
void SetClientServerTickPause(bool bPause); // FUN_0042d6f0 — DAT_00667d54
bool ShouldSkipLocalServerTick();       // FUN_0042d740 — SP && DAT_00667d54 > 0
bool IsClientServerTickPaused();        // FUN_0042d730 — DAT_00667d54 > 0 (signed, no SP test)
void SetClientFpsCap(float Fps);        // DAT_00610a0c
bool InitializeEngine(const FClientCommandLine* CommandLine); // FUN_005b1300
void OpenSpewLog();               // FUN_005b2800
// FUN_005b27c0 — developer FPU-exception-trap toggle, "+X"/"-X"-gated (G_bFpExceptionTrap).
// Retail calls this every frame (Enable=false in TryBeginFrame/FUN_00424f30, Enable=true at the end
// of FinishPresentFrame's first half/FUN_00425080), not just once at WinMain startup.
void SetFpuExceptionTrap(bool Enable);
