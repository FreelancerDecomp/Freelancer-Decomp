// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail LoadingScreen (RTTI .?AVLoadingScreen@@, vftable 0x005e448c) and its
// LoadingProgressBar (.?AVLoadingProgressBar@@, vftable 0x005e44a4), 0x00578b70..0x005792a6.
// One static instance, DAT_0067c3b8 (static init LAB_00579110, atexit LAB_00579150), which
// InitializeEngine registers with Common.dll (RegisterLoadingScreen, FUN_00579180) so every
// RefreshLoadingProgress lands in slot 0.

#include "common_ini.h" // ILoadingScreen
#include "wincontrol_base.h"

struct Image;

// 0x384 bytes (operator new(0x384) in FUN_00578d50): a WinControl with a "LoadingProgressText"
// TextControl and the FRONT_loadingbar.cmp StaticControl whose material animation the phase
// drives.
class LoadingProgressBar : public WinControl
{
public:
    // The ctor inlined in FUN_00578d50 (0x00578d94..0x00578dd3): WinControl's ctor, +0x338 =
    // 3.0f, +0x380 = 0, vptrs, +0x33c = 0, DAT_0067c3e0 = 0, Flags bit0 cleared, +0x32c = 0,
    // +0x334 = 0. +0x330 is not written.
    LoadingProgressBar();
    // FUN_00578e70 (FUN_00578e50 deleting): vptrs, then WinControl's dtor FUN_0059fa50.
    ~LoadingProgressBar() override;

    // slot 9 FUN_00578b70 (RET 8)
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 LAB_00578cd0
    void Update() override;
    // slot 12 LAB_00578d10
    void UpdateInactive() override;
    // slot 13 FUN_00401e90 (a bare RET): the bar never draws through the Win3D tree; the loading
    // screen's Update draws its two children itself.
    void Draw() override;

    unsigned char Field32c;   // +0x32c -- ctor 0; no other reader or writer in the class
    unsigned char Pad32d[3];  // +0x32d
    float Phase;              // +0x330 -- advanced by dt modulo Period (FUN_00578f70)
    float Fraction;           // +0x334 -- min(dt, 1.0), handed to FUN_0041b2e0
    float Period;             // +0x338 -- ctor 3.0f
    WinControl* Text;         // +0x33c -- "LoadingProgressText"
    wchar_t Caption[0x20];    // +0x340 -- ids 0x7c
    WinControl* Bar;          // +0x380 -- "LoadingProgressBar" StaticControl

private:
    LoadingProgressBar(LoadingProgressBar const&) = delete;
    LoadingProgressBar& operator=(LoadingProgressBar const&) = delete;
};

static_assert(sizeof(void*) != 4 || sizeof(LoadingProgressBar) == 0x384,
              "retail LoadingProgressBar is 0x384 bytes");

// DAT_0067c3b8.
class LoadingScreen : public ILoadingScreen
{
public:
    // LAB_00579110 (static init): vptr, Screen = 0, bBeginEnd = 1, DAT_0067c3e0 = 0, bClear = 1,
    // bShowBar = 1, bDisabled = 0, ProgressBar = 0.
    LoadingScreen();
    // LAB_00579150 (atexit): vptr, then the screen's deleting dtor when there is one. Not
    // virtual: the vftable 0x005e448c has only the two slots below.
    ~LoadingScreen();

    // slot 0 FUN_00578e90 -- RefreshLoadingProgress's entry.
    void Refresh() override;
    // slot 1 LAB_00578f50 (ILoadingScreen +0x04, name unrecoverable): DAT_0067c3e0 = 1,
    // Update(-1.0f) -- the body FUN_00579290 (LoadingScreenStart) runs non-virtually.
    void Slot01() override;

    // FUN_00578f70 (thiscall, RET 4).
    void Update(float DeltaSeconds);
    // FUN_00578d50 -- build the progress bar once.
    void CreateProgressBar();

    // The vptr is +0x00.
    Image* Screen;                   // +0x04 DAT_0067c3bc
    LoadingProgressBar* ProgressBar; // +0x08 DAT_0067c3c0
    unsigned char Pad0c[4];          // +0x0c DAT_0067c3c4 -- no reference in the PE
    long long ResetTicks;            // +0x10 DAT_0067c3c8 (FUN_00579190)
    long long LastUpdateTicks;       // +0x18 DAT_0067c3d0 (FUN_00578f70)
    unsigned char bBeginEnd;         // +0x20 DAT_0067c3d8 -- Update brackets its own scene
    unsigned char bClear;            // +0x21 DAT_0067c3d9 -- clear when there is no Screen
    unsigned char bShowBar;          // +0x22 DAT_0067c3da
    unsigned char bDisabled;         // +0x23 DAT_0067c3db -- set by FUN_005791c0

private:
    LoadingScreen(LoadingScreen const&) = delete;
    LoadingScreen& operator=(LoadingScreen const&) = delete;
};

// DAT_0067c3e0 -- the bar is running (FUN_00579290 / slot 1 set it, FUN_00579190 and the bar's
// ctor clear it). A separate .data byte, not a field of DAT_0067c3b8.
extern unsigned char G_LoadingScreenStarted;
// DAT_0067c3f4 / DAT_0067c3f8 -- Refresh's counts of updates run / skipped as too soon.
extern int G_LoadingScreenUpdateCount;
extern int G_LoadingScreenSkipCount;

LoadingScreen& GetLoadingScreen();

// FUN_00579170 -- CreateProgressBar on DAT_0067c3b8. Caller: IntroFocus 0x00574325.
void LoadingScreenCreateProgressBar();
// FUN_00579180 -- RegisterLoadingScreen(&DAT_0067c3b8). Caller: InitializeEngine 0x005b1a06.
void LoadingScreenRegister();
// FUN_00579190 -- reset: ResetTicks = now, DAT_0067c3e0 = 0, bClear = 0, bShowBar = 1.
void LoadingScreenReset();
// FUN_005791c0 -- delete the screen and the bar's tree, then bDisabled = 1. Caller: WinMain
// shutdown 0x005b2195.
void LoadingScreenShutdown();
// FUN_00579220 (cdecl) -- replace the screen with a new Image and Load(Path) it; false when the
// Image could not be allocated. Caller: InitializeEngine 0x005b1a0c.
bool LoadingScreenSetImage(char const* Path);
// FUN_00579260 -- delete the screen (Screen = 0). Caller: IntroFocus 0x0057432a.
void LoadingScreenReleaseImage();
// FUN_00579280 (cdecl) -- bClear = bValue.
void LoadingScreenSetClear(unsigned char bValue);
// FUN_00579290 -- DAT_0067c3e0 = 1, Update(-1.0f) on DAT_0067c3b8.
void LoadingScreenStart();
