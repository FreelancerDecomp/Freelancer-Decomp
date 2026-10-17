// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// The MissionBriefing client state (DAT_00668774, vftable 0x005cb094). See mission_briefing.h.

#include "mission_briefing.h"

#include "best_path.h"              // BestPathGetWaypointCount (FUN_004c4fb0), G_BestPaths (DAT_00672950)
#include "character_select_equip.h" // SetPlayerBaseIdRetail (FUN_004c4910), GetPlayerStarSystemIdRetail
#include "chat_init.h"              // ChatIsInputOpen (FUN_0046a480)
#include "client_frame_tick.h"      // FUN_00424f30 / FUN_00425080 / FUN_00425140
#include "gameflow.h"               // DAT_00668780 / DAT_00668781 / DAT_00668784
#include "input_manager.h"          // MouseSetCursor (FUN_0041dde0), SetClientMouseCursorDrawFlag
#include "loading_screen.h"         // LoadingScreenReset (FUN_00579190)
#include "menu_sound_arch.h"        // TickClientSoundMixer (FUN_00428040), SetClientBackgroundMusic
#include "menu_ui_init.h"           // IsAnyUiSoundPlaying (FUN_00564760)
#include "nn_modal.h"               // NnSystemInit / NnSystemShutdown (FUN_0045a100 / FUN_0045a140)
#include "nn_static_control.h"      // StaticControl::Init (FUN_00588540)
#include "nn_story_star.h"          // StoryStarReset (FUN_004b9bb0)
#include "win3d.h"                  // Win3DDrawAll (FUN_0059e300), Win3DDeleteTree (FUN_0043d2a0)
#include "win3d_render.h"           // ClearWin3DBuffersToColor (FUN_00564f90)
#include "win_factory.h"            // WinFactoryCreate (FUN_0059db20)

#include "common_universe.h"        // Universe::get_system
#include "dacom.h"                  // FDUMP

#include <windows.h>

namespace
{
    // .rdata 0x005cb0d0 -- the only mesh LAB_0043d170 hands FUN_00588540.
    char const kBriefingWindowMesh[] = "Interface\\NeuroNet\\Inventory\\repairblackbox.3db";

    // RTTI .?AUMissionBriefing@@ (COL 0x005e88d4), vftable 0x005cb094: fifteen slots, raw PE
    // dwords at 0x005cb094..0x005cb0cc (the next dword is string data):
    //   0 LAB_0043d130  1 LAB_0043d160  2 LAB_0043d160  3 LAB_0043d150  4 LAB_0043d160
    //   5 0x00401e90    6 0x0053e4a0    7 LAB_0043d2e0  8 LAB_0043d170  9 LAB_0043d260
    //   10 0x00401e90   11 0x0053aaa0   12 LAB_0043d2f0 13 LAB_0043d300 14 LAB_0043d360
    struct MissionBriefing : public IEnvironment
    {
        bool Slot00(unsigned A, unsigned Message, unsigned B, unsigned C) override;
        bool Slot01(unsigned A, unsigned B, unsigned C) override;
        bool Slot02(unsigned A, unsigned B, unsigned long Packed) override;
        bool Slot03(unsigned A, unsigned Message, unsigned B, unsigned long Packed) override;
        bool Slot04(unsigned A, unsigned B, unsigned C) override;
        void Slot05() override;
        void Slot06(unsigned A) override;
        int GetEnvironmentType() override;
        void PreInit() override;
        void Shutdown() override;
        void Slot10() override;
        void Resize(int Width, int Height) override;
        bool Init(int Param) override;
        void Destroy() override;
        int Update(float DeltaSeconds) override;

        // Its static initializer LAB_0043d120 only stamps the vptr; the rest is .bss zero until
        // slot 8 runs.
        unsigned char bFirstFramePending = 0; // +0x04
        Win3D* ParentWindow = nullptr;        // +0x08 "BriefingParentWindow"
        // +0x0c / +0x0d / +0x10: G_bMissionBriefingNavMapUp, G_bMissionBriefingContinue and
        // DAT_00668784 (gameflow.cpp).
        unsigned char bIdle = 0;              // +0x14
    };

    // The frame both halves of LAB_0043d360 draw (0x0043d381..0x0043d3bc and 0x0043d4c5..
    // 0x0043d500): FUN_00428040(0, Dt), then with a scene begun FUN_00564f90({0, 0, 0, 0xff}),
    // FUN_0059e300, FUN_00425080, FUN_00425140.
    void DrawBriefingFrame(float DeltaSeconds)
    {
        TickClientSoundMixer(nullptr, DeltaSeconds);
        if (!BeginClientRenderFrame())
            return;
        unsigned char const Black[4] = {0, 0, 0, 0xff};
        ClearWin3DBuffersToColor(Black);
        Win3DDrawAll();
        EndClientRenderScene();
        PresentClientRenderFrame();
    }

    // The exit both LAB_0043d300 and LAB_0043d360 take (0x0043d312..0x0043d337 /
    // 0x0043d50a..0x0043d52d): idle, first frame pending again, no continue request, the window
    // hidden, the player's base cleared (FUN_004c4910(&0)) and the NN system torn down.
    void LeaveBriefing(MissionBriefing* State)
    {
        State->bIdle = 1;
        State->bFirstFramePending = 1;
        G_bMissionBriefingContinue = 0;
        State->ParentWindow->Flags = static_cast<unsigned char>(State->ParentWindow->Flags & 0xfc);
        SetPlayerBaseIdRetail(0);
        NnSystemShutdown();
    }

    // Slot 0 (LAB_0043d130, RET 0x10) -- a key message (A, Message, B, C). Unless the chat
    // input "CTID" is up (FUN_0046a480), GetKeyState(VK_SHIFT) and GetKeyState(VK_CONTROL) with
    // their results dropped; true either way.
    bool MissionBriefing::Slot00(unsigned A, unsigned Message, unsigned B, unsigned C)
    {
        (void)A;
        (void)Message;
        (void)B;
        (void)C;
        if (!ChatIsInputOpen())
        {
            GetKeyState(VK_SHIFT);
            GetKeyState(VK_CONTROL);
        }
        return true;
    }

    // Slots 1, 2 and 4 (LAB_0043d160, MOV AL,1 / RET 0xc) -- true.
    bool MissionBriefing::Slot01(unsigned A, unsigned B, unsigned C)
    {
        (void)A;
        (void)B;
        (void)C;
        return true;
    }

    bool MissionBriefing::Slot02(unsigned A, unsigned B, unsigned long Packed)
    {
        (void)A;
        (void)B;
        (void)Packed;
        return true;
    }

    bool MissionBriefing::Slot04(unsigned A, unsigned B, unsigned C)
    {
        (void)A;
        (void)B;
        (void)C;
        return true;
    }

    // Slot 3 (LAB_0043d150, MOV AL,1 / RET 0x10) -- true.
    bool MissionBriefing::Slot03(unsigned A, unsigned Message, unsigned B, unsigned long Packed)
    {
        (void)A;
        (void)Message;
        (void)B;
        (void)Packed;
        return true;
    }

    // Slots 5 and 10 (0x00401e90) -- a bare RET.
    void MissionBriefing::Slot05()
    {
    }

    void MissionBriefing::Slot10()
    {
    }

    // Slot 6 (0x0053e4a0, RET 4) -- a bare RET.
    void MissionBriefing::Slot06(unsigned A)
    {
        (void)A;
    }

    // Slot 7 (LAB_0043d2e0) -- 3, the environment type (GF answers 1, IntroFocus 2).
    int MissionBriefing::GetEnvironmentType()
    {
        return 3;
    }

    // Slot 8 (LAB_0043d170).
    void MissionBriefing::PreInit()
    {
        MissionBriefing* const State = this;
        // 0x0043d184..0x0043d197.
        State->bFirstFramePending = 1;
        G_bMissionBriefingNavMapUp = 0;
        G_bMissionBriefingContinue = 0;
        SetGameflowLocationBase(0);
        State->bIdle = 1;
        // 0x0043d19b..0x0043d1a8: +0x08 = FUN_0059db20("BriefingParentWindow", "StaticControl",
        // 0, 0); null skips the rest (JZ 0x0043d253).
        State->ParentWindow = WinFactoryCreate("BriefingParentWindow", "StaticControl", nullptr,
                                               nullptr);
        if (State->ParentWindow == nullptr)
            return;
        // 0x0043d1ae..0x0043d222: FUN_00588540 with three zeroed vectors -- Offset (local +0x1c),
        // HalfExtents (+0x10), StartYpr (+0x04) -- no hardpoint, and the bytes NoZEnable 0,
        // ZClear 0, NoMeshRender 0, NoWireRender 1, NoColoring 0, RenderToWindow 0,
        // NoClearRender 1, ForceRadius 0, RotationSpeed 0, EnableRotation 0.
        float const Offset[3] = {0.0f, 0.0f, 0.0f};
        float const HalfExtents[3] = {0.0f, 0.0f, 0.0f};
        float const StartYpr[3] = {0.0f, 0.0f, 0.0f};
        static_cast<NnStaticControl*>(State->ParentWindow)
            ->Init(kBriefingWindowMesh, Offset, nullptr, HalfExtents, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0,
                   StartYpr);
        // 0x0043d22e..0x0043d249: slot 5 SetPosition({-0.62f, 0.3f, -1.25f}).
        float const Position[3] = {-0.62f, 0.3f, -1.25f}; // 0xbf1eb852 / 0x3e99999a / 0xbfa00000
        State->ParentWindow->SetPosition(Position);
        // 0x0043d24f: hidden until the first frame.
        State->ParentWindow->Flags = static_cast<unsigned char>(State->ParentWindow->Flags & 0xfc);
    }

    // Slot 9 (LAB_0043d260) -- FUN_0043d2a0's body on the window (children first, next sibling
    // read before each recursion, then slot 10 and the deleting destructor), then +0x08 = 0.
    void MissionBriefing::Shutdown()
    {
        if (ParentWindow == nullptr)
            return;
        Win3DDeleteTree(ParentWindow);
        ParentWindow = nullptr;
    }

    // Slot 11 (0x0053aaa0, RET 8) -- a bare RET.
    void MissionBriefing::Resize(int Width, int Height)
    {
        (void)Width;
        (void)Height;
    }

    // Slot 12 (LAB_0043d2f0, RET 4) -- +0x14 = 0, true. The argument is not read.
    bool MissionBriefing::Init(int Param)
    {
        (void)Param;
        bIdle = 0;
        return true;
    }

    // Slot 13 (LAB_0043d300).
    void MissionBriefing::Destroy()
    {
        MissionBriefing* const State = this;
        // 0x0043d304..0x0043d310: only while the briefing is up (first frame done) and the
        // window exists.
        if (State->bFirstFramePending != 0 || State->ParentWindow == nullptr)
            return;
        // 0x0043d312..0x0043d337.
        LeaveBriefing(State);
        // 0x0043d33c.
        StoryStarReset(); // FUN_004b9bb0
        // 0x0043d341..0x0043d356: the window's first child -- the NeuroNetNavMap -- gets slot 23
        // (0xef02, 0, 0), the close.
        Win3D* const NavMap = State->ParentWindow->GetChildHead();
        if (NavMap != nullptr)
            NavMap->Slot23(0xef02, 0, 0);
    }

    // Slot 14 (LAB_0043d360, RET 4). EBX = 0, EBP = DeltaSeconds throughout.
    int MissionBriefing::Update(float DeltaSeconds)
    {
        MissionBriefing* const State = this;

        // 0x0043d360..0x0043d37f: FCOMP 0.0, TEST AH,0x44, JNP -- the jump (to the frame) is
        // taken only when DeltaSeconds equals 0 (C3 alone; unordered sets C3 and C2 and falls
        // through). Otherwise the frame is drawn only with the continue request set.
        if (DeltaSeconds == 0.0f || G_bMissionBriefingContinue != 0)
        {
            DrawBriefingFrame(DeltaSeconds);
            // 0x0043d3c1..0x0043d3cd: with the continue request up, wait while a UI sound plays.
            if (G_bMissionBriefingContinue != 0 && IsAnyUiSoundPlaying())
                return 0;
        }

        // 0x0043d3d3..0x0043d3d6.
        if (State->bIdle != 0)
            return 0;

        if (State->bFirstFramePending != 0)
        {
            // 0x0043d3e5..0x0043d42d: no route, an empty route list, or a route whose Text
            // MadLibId (node +0x3c) is 1 or 2 -- no briefing: FUN_004b9bb0 and back to space.
            if (BestPathGetWaypointCount() == 0)
            {
                StoryStarReset(); // FUN_004b9bb0
                return 2;
            }
            if (G_BestPaths.empty())
            {
                StoryStarReset(); // FUN_004b9bb0
                return 2;
            }
            unsigned const MadLibId = G_BestPaths.begin()->second.Text.MadLibId;
            if (MadLibId == 2 || MadLibId == 1)
            {
                StoryStarReset(); // FUN_004b9bb0
                return 2;
            }

            // 0x0043d431..0x0043d434: +0x04 = 0, FUN_0045a100(0) (no NN_MainButton).
            State->bFirstFramePending = 0;
            NnSystemInit(false);
            // 0x0043d439..0x0043d43c: the window shown.
            State->ParentWindow->Flags = static_cast<unsigned char>(State->ParentWindow->Flags | 3);
            // 0x0043d440..0x0043d444: FUN_004c4910(&+0x10).
            SetPlayerBaseIdRetail(GetGameflowLocationBase());
            // 0x0043d449..0x0043d483: the nav map inside the window, built with +0x0c = 1 (the
            // nav map's Create reads it through FUN_0043d570), then slot 23 (0xef01, 0, 0) and
            // +0x6c |= 3. No null test on the create.
            G_bMissionBriefingNavMapUp = 1;
            Win3D* const NavMap = WinFactoryCreate("NeuroNetNavMap", "NeuroNetNavMap",
                                                   State->ParentWindow, nullptr);
            NavMap->Slot23(0xef01, 0, 0);
            NavMap->Flags = static_cast<unsigned char>(NavMap->Flags | 3);
            G_bMissionBriefingNavMapUp = 0;
            // 0x0043d47e..0x0043d48d: FUN_0041dde0("arrow", 0), FUN_0041dda0(1).
            MouseSetCursor("arrow", false);
            SetClientMouseCursorDrawFlag(1);
            // 0x0043d492..0x0043d4c2: the player's system's [Music] space track (ISystem slot 21)
            // through FUN_004288e0(&id, 1, -1.0f); no system, no music change.
            Universe::ISystem const* const System =
                Universe::get_system(GetPlayerStarSystemIdRetail());
            if (System != nullptr)
                SetClientBackgroundMusic(System->GetSpaceMusic(), 1, -1.0f);
        }

        // 0x0043d4c5..0x0043d500.
        DrawBriefingFrame(DeltaSeconds);

        if (G_bMissionBriefingContinue == 0)
        {
            // 0x0043d542: FUN_00579190 -- the loading screen's timer reset (DAT_0067c3b8).
            LoadingScreenReset();
            // 0x0043d547..0x0043d549: FUN_0041dda0(1).
            SetClientMouseCursorDrawFlag(1);
            return 0;
        }

        // 0x0043d50a..0x0043d53f: the nav map asked to continue -- leave, FUN_004b9bb0, 2.
        LeaveBriefing(State);
        StoryStarReset(); // FUN_004b9bb0
        return 2;
    }

    // DAT_00668774. Its static initializer LAB_0043d120 only stamps the vptr 0x005cb094; the
    // rest is .bss zero until slot 8 runs.
    MissionBriefing G_MissionBriefing;
}

IEnvironment* GetMissionBriefingState()
{
    // FUN_0043d560: MOV EAX,0x00668774.
    return &G_MissionBriefing;
}
