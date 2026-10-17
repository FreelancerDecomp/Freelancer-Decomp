// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "loading_screen.h"

#include "client_frame_tick.h"   // BeginClientRenderFrame / EndClientRenderScene / Present...
#include "globals.h"             // RenderMgrFrameReady (FUN_00424cb0)
#include "ids_strings.h"         // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "image.h"
#include "nn_ui_pulse.h"         // NnUiPulseIdleArgb (DAT_00679bb0)
#include "win3d.h"
#include "win3d_immediate.h"     // Win3DSetPipelineClearColor (FUN_004214d0)
#include "win3d_render.h"        // ClearWin3DBuffers (FUN_004215e0)
#include "win3d_renderables.h"   // UiOverlayPrepPipeline (FUN_00417f10)

#include "common_ini.h"
#include "common_timing.h"       // Timing

#include <math.h>
#include <stddef.h>
#include <stdint.h>

static_assert(sizeof(void*) != 4 || offsetof(LoadingProgressBar, Field32c) == 0x32c,
              "LoadingProgressBar +0x32c");
static_assert(sizeof(void*) != 4 || offsetof(LoadingProgressBar, Phase) == 0x330,
              "LoadingProgressBar +0x330");
static_assert(sizeof(void*) != 4 || offsetof(LoadingProgressBar, Text) == 0x33c,
              "LoadingProgressBar +0x33c");
static_assert(sizeof(void*) != 4 || offsetof(LoadingProgressBar, Caption) == 0x340,
              "LoadingProgressBar +0x340");
static_assert(sizeof(void*) != 4 || offsetof(LoadingProgressBar, Bar) == 0x380,
              "LoadingProgressBar +0x380");
static_assert(sizeof(void*) != 4 || offsetof(LoadingScreen, Screen) == 0x04, "LoadingScreen +0x04");
static_assert(sizeof(void*) != 4 || offsetof(LoadingScreen, ResetTicks) == 0x10,
              "LoadingScreen +0x10");
static_assert(sizeof(void*) != 4 || offsetof(LoadingScreen, LastUpdateTicks) == 0x18,
              "LoadingScreen +0x18");
static_assert(sizeof(void*) != 4 || offsetof(LoadingScreen, bBeginEnd) == 0x20,
              "LoadingScreen +0x20");
static_assert(sizeof(void*) != 4 || offsetof(LoadingScreen, bDisabled) == 0x23,
              "LoadingScreen +0x23");

unsigned char G_LoadingScreenStarted = 0; // DAT_0067c3e0
int G_LoadingScreenUpdateCount = 0;       // DAT_0067c3f4
int G_LoadingScreenSkipCount = 0;         // DAT_0067c3f8

namespace
{
    // DAT_0067c3b8.
    LoadingScreen G_LoadingScreen;
}

// ---------------------------------------------------------------------------------------
// LoadingProgressBar
// ---------------------------------------------------------------------------------------

LoadingProgressBar::LoadingProgressBar()
{
    // 0x00578d9b..0x00578dd3 (after FUN_0059f860).
    Period = 3.0f;
    Bar = nullptr;
    Text = nullptr;
    G_LoadingScreenStarted = 0;
    Flags = static_cast<unsigned char>(Flags & 0xfe);
    Field32c = 0;
    Fraction = 0.0f;
}

LoadingProgressBar::~LoadingProgressBar()
{
    // FUN_00578e70: nothing of its own before WinControl's dtor.
}

bool LoadingProgressBar::Create(Win3D* NewParent, void* Data)
{
    // FUN_00578b70 (RET 8).
    float TextPosition[3] = {-0.095f, 0.0192f, 0.0f}; // 0xbdc28f5c, 0x3c9d4952, 0
    Text = CreateTextControl("LoadingProgressText", "PageHeader", TextPosition, nullptr, 0.2f,
                             0.04f, 2.0f, 0, 1, 0, 1); // FUN_005a0e30

    float BarPosition[3] = {0.0f, 0.0f, -1.0f};
    float BarHalfExtents[3] = {0.0f, 0.0f, 0.0f};
    float BarVector338[3] = {0.0f, 0.0f, 0.0f};
    Bar = CreateStaticControl("LoadingProgressBar", "Interface\\Intro\\objects\\FRONT_loadingbar.cmp",
                              BarPosition, nullptr, BarHalfExtents, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                              BarVector338); // FUN_005a0db0

    SetOffset(0.0f, 0.0f, -1.0f); // slot 31

    // 0x00578c7e..0x00578c90: FUN_004347e0(DAT_0067eca8, 0x7c, +0x340, 0x20).
    GetIdsStringWWithRawFallback(0x7c, Caption, 0x20);
    if (Text != nullptr)
        Text->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Caption), 0); // slot 42

    // FUN_004b5060, SETNZ on its bool.
    return Win3D::Create(NewParent, Data);
}

void LoadingProgressBar::Update()
{
    // LAB_00578cd0: shown (and active) only while the bar runs, then JMP FUN_005a17b0.
    if (G_LoadingScreenStarted != 0)
    {
        SetHiddenWithChildren(false); // FUN_005a0c30(0)
        Flags = static_cast<unsigned char>(Flags | 1);
    }
    else
    {
        SetHiddenWithChildren(true); // FUN_005a0c30(1)
        Flags = static_cast<unsigned char>(Flags & 0xfe);
    }
    Win3D::Update();
}

void LoadingProgressBar::UpdateInactive()
{
    // LAB_00578d10: re-activate once the bar runs, then slot 12 on every inactive child (the
    // next sibling read after the call) -- LAB_004682f0's loop, inlined.
    if (G_LoadingScreenStarted != 0)
    {
        SetHiddenWithChildren(false);
        Flags = static_cast<unsigned char>(Flags | 1);
    }
    Win3D::UpdateInactive();
}

void LoadingProgressBar::Draw()
{
    // FUN_00401e90: RET.
}

// ---------------------------------------------------------------------------------------
// LoadingScreen
// ---------------------------------------------------------------------------------------

LoadingScreen::LoadingScreen()
{
    // LAB_00579110.
    Screen = nullptr;
    bBeginEnd = 1;
    G_LoadingScreenStarted = 0;
    bClear = 1;
    bShowBar = 1;
    bDisabled = 0;
    ProgressBar = nullptr;
}

LoadingScreen::~LoadingScreen()
{
    // LAB_00579150: the screen's deleting dtor (slot 0 with 1); the pointer is not cleared.
    if (Screen != nullptr)
        delete Screen;
}

void LoadingScreen::Refresh()
{
    // FUN_00578e90.
    if (bDisabled != 0)
        return;

    long long const Now = Timing::read_ticks();
    if (G_LoadingScreenStarted == 0)
    {
        // 0x00578eb7..0x00578ee6: FCOMP 2.0 / TEST AH,0x41 / JNZ -- only more than two
        // seconds since the reset (unordered stays) starts the bar through slot 1.
        long long const SinceReset = Now - ResetTicks;
        if (Timing::seconds(SinceReset) > 2.0)
            Slot01();
        return;
    }

    // 0x00578eee..0x00578f38: the time since the last update, stored as a float for Update but
    // compared unrounded: FABS, FLD 0.05f (DAT_00615108), FCOMPP, TEST AH,5 / JP -- the update
    // runs only when 0.05 < |dt| (an unordered dt counts as skipped).
    long long const SinceUpdate = Now - LastUpdateTicks;
    double const Seconds = Timing::seconds(SinceUpdate);
    float const DeltaSeconds = static_cast<float>(Seconds);
    if (static_cast<double>(0.05f) < fabs(Seconds))
    {
        Update(DeltaSeconds);
        ++G_LoadingScreenUpdateCount;
    }
    else
    {
        ++G_LoadingScreenSkipCount;
    }
}

void LoadingScreen::Slot01()
{
    // LAB_00578f50.
    G_LoadingScreenStarted = 1;
    Update(-1.0f);
}

void LoadingScreen::Update(float DeltaSeconds)
{
    // FUN_00578f70 (RET 4).
    if (bDisabled != 0)
        return;
    LastUpdateTicks = Timing::read_ticks();
    if (!RenderMgrFrameReady()) // FUN_00424cb0
        return;
    if (bBeginEnd != 0 && !BeginClientRenderFrame()) // FUN_00424f30
        return;

    if (Screen != nullptr)
    {
        Screen->Draw(); // slot 2
    }
    else if (bClear != 0)
    {
        Win3DSetPipelineClearColor(0.0f, 0.0f, 0.0f, 0.0f); // FUN_004214d0
        ClearWin3DBuffers(0x4100);                          // FUN_004215e0
    }

    if (ProgressBar != nullptr && bShowBar != 0 && G_LoadingScreenStarted != 0)
    {
        Win3DBeginDraw(); // FUN_0059e500
        LoadingProgressBar* const Bar = ProgressBar;
        // 0x00579003..0x00579015: FCOMP 0.0 / TEST AH,5 / JP -- a negative dt resets; zero,
        // positive and unordered advance.
        if (DeltaSeconds < 0.0f)
        {
            Bar->Phase = 0.0f;
            Bar->Fraction = 0.0f;
        }
        else
        {
            // 0x0057902d..0x00579069: the sum stays on the FPU stack; it is stored once, and
            // again after Period is subtracted while it is still above Period (TEST AH,0x41 /
            // JNZ leaves at <= or unordered).
            double const Period = static_cast<double>(Bar->Period);
            double Phase = static_cast<double>(DeltaSeconds) + static_cast<double>(Bar->Phase);
            Bar->Phase = static_cast<float>(Phase);
            if (Phase > Period)
            {
                do
                {
                    Phase -= Period;
                } while (Phase > Period);
                Bar->Phase = static_cast<float>(Phase);
            }
            // 0x00579073..0x00579090: FCOMP 1.0 / TEST AH,5 / JP -- dt when below 1.0, else
            // (and unordered) 1.0.
            Bar->Fraction = (DeltaSeconds < 1.0f) ? DeltaSeconds : 1.0f;
        }

        if (Bar->Bar != nullptr)
        {
            TickMaterialAnimationPlayback(Bar->Fraction); // FUN_0041b2e0
            Bar->Bar->Draw();                             // slot 13
        }
        if (Bar->Text != nullptr)
        {
            // 0x005790c8..0x005790ea: property 0x33 with a copy of DAT_00679bb0, then Draw.
            unsigned long Color = NnUiPulseIdleArgb();
            Bar->Text->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Color), 1);
            Bar->Text->Draw();
        }
        Win3DEndDraw(); // FUN_0059e5a0
    }

    if (bBeginEnd != 0)
    {
        UiOverlayPrepPipeline();    // FUN_00417f10
        EndClientRenderScene();     // FUN_00425080
        PresentClientRenderFrame(); // FUN_00425140
    }
}

void LoadingScreen::CreateProgressBar()
{
    // FUN_00578d50: only once. operator new(0x384) + the inlined ctor (no failure path: a null
    // result is dereferenced at 0x00578de1).
    if (ProgressBar != nullptr)
        return;
    LoadingProgressBar* const Bar = new LoadingProgressBar();
    ProgressBar = Bar;
    Bar->Create(nullptr, nullptr); // slot 9
    // 0x00578df2..0x00578e2f: the inline name copy (<= 31 chars), then +0x10 = the same string.
    Bar->SetName("LoadingProgressBar");
    Bar->ClassName = "LoadingProgressBar";
}

LoadingScreen& GetLoadingScreen()
{
    return G_LoadingScreen;
}

void LoadingScreenCreateProgressBar()
{
    // FUN_00579170: JMP FUN_00578d50 on DAT_0067c3b8.
    G_LoadingScreen.CreateProgressBar();
}

void LoadingScreenRegister()
{
    // FUN_00579180.
    RegisterLoadingScreen(&G_LoadingScreen);
}

void LoadingScreenReset()
{
    // FUN_00579190.
    G_LoadingScreen.ResetTicks = Timing::read_ticks();
    G_LoadingScreenStarted = 0;
    G_LoadingScreen.bClear = 0;
    G_LoadingScreen.bShowBar = 1;
}

void LoadingScreenShutdown()
{
    // FUN_005791c0.
    if (G_LoadingScreen.Screen != nullptr)
        delete G_LoadingScreen.Screen; // slot 0 with 1
    G_LoadingScreen.Screen = nullptr;

    LoadingProgressBar* const Bar = G_LoadingScreen.ProgressBar;
    if (Bar != nullptr)
    {
        // FUN_0043d2a0 on every child (the next sibling read first), slot 10 Destroy, the
        // scalar deleting dtor. DAT_0067c3c0 keeps the dead pointer (not cleared).
        Win3D* Child = Bar->GetChildHead();
        while (Child != nullptr)
        {
            Win3D* const Next = Child->GetNextSibling();
            Win3DDeleteTree(Child);
            Child = Next;
        }
        Bar->Destroy();
        delete Bar;
    }
    G_LoadingScreen.bDisabled = 1;
}

bool LoadingScreenSetImage(char const* Path)
{
    // FUN_00579220 (cdecl).
    if (G_LoadingScreen.Screen != nullptr)
    {
        delete G_LoadingScreen.Screen;
        G_LoadingScreen.Screen = nullptr;
    }
    G_LoadingScreen.Screen = CreateImage(); // FUN_00565d90
    if (G_LoadingScreen.Screen != nullptr)
        return G_LoadingScreen.Screen->Load(Path); // slot 1
    return false;
}

void LoadingScreenReleaseImage()
{
    // FUN_00579260.
    if (G_LoadingScreen.Screen != nullptr)
    {
        delete G_LoadingScreen.Screen;
        G_LoadingScreen.Screen = nullptr;
    }
}

void LoadingScreenSetClear(unsigned char bValue)
{
    // FUN_00579280.
    G_LoadingScreen.bClear = bValue;
}

void LoadingScreenStart()
{
    // FUN_00579290: FUN_00578f70 called directly, not through slot 1.
    G_LoadingScreenStarted = 1;
    G_LoadingScreen.Update(-1.0f);
}
