// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail NN_Pause (NN_Pause.cpp). See nn_pause.h.

#include "nn_pause.h"

#include "globals.h"               // IsClientServerTickPaused (FUN_0042d730), SetClientServerTickPause
#include "gf_script_object.h"      // G_GfScriptManager (DAT_0066d2d0), CanSkipAll (FUN_004568a0)
#include "hud_command_dispatch.h"  // HudPushInputFocus / HudPopInputFocus / HudPostCommand
#include "hud_target.h"
#include "ids_strings.h"           // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "input_manager.h"         // MouseSetCursor, Get/SetClientMouseCursorDrawFlag
#include "menu_enter_state.h"      // MenuNavSetIntroModeRequest (FUN_00564b10, DAT_0067c200)
#include "nn_load_save.h"           // OpenNnLoadSave (FUN_00488970)
#include "menu_universe_base.h"    // GetClientLocationCurrentBase (DAT_0066873c)
#include "nav_bar.h"               // NavBarCloseService (FUN_004424f0)
#include "nn_confirm.h"            // NnConfirmOpen (FUN_004889e0)
#include "nn_inventory.h"          // IsPlayerDockedRetail (FUN_004c4810)
#include "nn_modal.h"              // G_bNnScreenUp, NnMainButtonOnModalOpen, NnFloatBits
#include "nn_rollover.h"           // NnRolloverHideGlobal (FUN_004b65a0)
#include "player_behavior.h"       // GetPlayerObjRW (FUN_0054baf0)
#include "win3d.h"
#include "win3d_renderables.h"     // Win3DFlushBeforeOverlayDraw (FUN_0045a9f0)
#include "win_factory.h"

#include "common_misc.h"           // SinglePlayer
#include "dacom.h"                 // FDUMP

#include <stdio.h>
#include <windows.h>

// Retail DAT_00672144 -- WINFACTORY<NN_Pause> static (LAB_004a6ad0 -> FUN_004a76b0; Create
// FUN_004a7720; atexit LAB_004a6af0 -> FUN_00579380).
WINFACTORY<NnPause> G_NnPauseFactory("NN_Pause");

namespace
{
    char const kPauseBlackPlaneMesh[] = "Interface\\NeuroNet\\MISC_bigblackplane.3db"; // 005d21b0
    char const kPauseFrameMesh[] = "Interface\\NeuroNet\\Options\\escapewindow2.cmp";  // 005d4720
}

NnPause::NnPause()
    : bCursorDrawFlag(0)
    , bClosePending(0)
    , Title(nullptr)
{
    // FUN_004a7720 004a7759..004a7781: FUN_0059f860, the three vptrs, +0x331 = 0. The other
    // members are written by Create before anything reads them; the port zeroes them here.
    Pad332[0] = 0;
    Pad332[1] = 0;
    for (int Index = 0; Index < 5; ++Index)
    {
        ButtonBox[Index] = nullptr;
        ButtonText[Index] = nullptr;
        ButtonAction[Index] = 0;
    }
}

NnPause::~NnPause()
{
    // FUN_004a6b20: vptrs back to NN_Pause's, FUN_00577c30(+0x32c); then the Event::Handler base
    // dtor FUN_00576010 and WinControl's FUN_0059fa50 run as the base-class destructors.
    HudPopInputFocus(AsHudCmdHandler());
}

bool NnPause::Create(Win3D* NewParent, void* Data)
{
    // FUN_004a6ba0 (RET 8).
    //
    // 0x004a6bac..0x004a6bca: FUN_0045a460 (close NN_MainButton's panel -- no DAT_0066d359 test
    // here, unlike NN_Exit), FUN_004424f0(1), FUN_004b65a0 (hide the rollover),
    // FUN_0041dde0("arrow", 0).
    NnMainButtonOnModalOpen();
    NavBarCloseService(true); // FUN_004424f0(1)
    NnRolloverHideGlobal();
    MouseSetCursor("arrow", false);

    float const Zero[3] = {0.0f, 0.0f, 0.0f};

    // 0x004a6bcd..0x004a6c1e: FUN_005a0db0("Pause_BlackPlane", mesh, {0, 0, -1}, 0, {0, 0, 0},
    // NoZEnable 0, ZClear 0, NoMeshRender 0, NoWireRender 1, NoColoring 1, RenderToWindow 0,
    // NoClearRender 1, 0, 0, 0, {0, 0, 0}).
    float const PlanePosition[3] = {0.0f, 0.0f, -1.0f};
    CreateStaticControl("Pause_BlackPlane", kPauseBlackPlaneMesh, PlanePosition, nullptr, Zero, 0,
                        0, 0, 1, 1, 0, 1, 0, 0, 0, Zero);

    // 0x004a6c23..0x004a6c78: the same flags, at {0, 0x3c1374c0 (0.009), -1}.
    float const FramePosition[3] = {0.0f, NnFloatBits(0x3c1374c0), -1.0f};
    CreateStaticControl("PauseFrame", kPauseFrameMesh, FramePosition, nullptr, Zero, 0, 0, 0, 1, 1,
                        0, 1, 0, 0, 0, Zero);

    // 0x004a6c7d..0x004a6cc0: FUN_005a0e30("PauseTitle", "PageHeader", {0xb2800000, 0.22, -1},
    // 0, 0.35, 0.03, 2.7, 0, 1, 0, 0) -> +0x334.
    float const TitlePosition[3] = {NnFloatBits(0xb2800000), NnFloatBits(0x3e6147af), -1.0f};
    Title = CreateTextControl("PauseTitle", "PageHeader", TitlePosition, nullptr,
                              NnFloatBits(0x3eb33333), NnFloatBits(0x3cf5c28f),
                              NnFloatBits(0x402ccccd), 0, 1, 0, 0);

    // 0x004a6cc6..0x004a6d52: the button actions. +0x360 is always 1 (resume).
    //   single player, docked (DAT_0066873c != 0) and FUN_004568a0 true: 1, 2, 3, 4, 5
    //   single player otherwise:                                        1, 3, 4, 5
    //   multiplayer:                                                    1, 6, 7, 4, 5
    // FUN_004568a0 is asked before the docked byte is tested (0x004a6ce2).
    int Count = 0;
    if (SinglePlayer())
    {
        bool const bDocked = GetClientLocationCurrentBase() != 0;
        bool const bCanSkip = G_GfScriptManager.CanSkipAll();
        if (bCanSkip && bDocked)
        {
            ButtonAction[1] = 2;
            ButtonAction[2] = 3;
            ButtonAction[3] = 4;
            ButtonAction[4] = 5;
            Count = 5;
        }
        else
        {
            ButtonAction[1] = 3;
            ButtonAction[2] = 4;
            ButtonAction[3] = 5;
            Count = 4;
        }
    }
    else
    {
        ButtonAction[1] = 6;
        ButtonAction[2] = 7;
        ButtonAction[3] = 4;
        ButtonAction[4] = 5;
        Count = 5;
    }
    ButtonAction[0] = 1;

    // 0x004a6d5c..0x004a6d76: row origin / step -- 0.149 / 0.0825, or 0.128375 / 0.09625 for
    // four rows.
    float RowOrigin = NnFloatBits(0x3e189375);
    float RowStep = NnFloatBits(0x3da8f5c3);
    if (Count == 4)
    {
        RowOrigin = NnFloatBits(0x3e0374bd);
        RowStep = NnFloatBits(0x3dc51eb9);
    }

    // 0x004a6d7e..0x004a6e8b: one row per action. Y = origin - i * step (FILD / FMUL / FSUB in
    // the x87 registers, FSTP float).
    for (int Index = 0; Index < Count; ++Index)
    {
        float const RowY = static_cast<float>(static_cast<double>(RowOrigin) -
                                              static_cast<double>(Index) *
                                                  static_cast<double>(RowStep));
        char Name[0x100];

        // FUN_005a0d00(name, 0, {0, Y, -1}, {0.175, 0.028, 0}, 0, 0, 0, 0, 0) -> +0x338[i].
        sprintf(Name, "PauseButtonBox%d", Index);
        float const BoxPosition[3] = {0.0f, RowY, -1.0f};
        float const BoxExtents[3] = {NnFloatBits(0x3e333333), NnFloatBits(0x3ce56042), 0.0f};
        ButtonBox[Index] = CreateWireButtonControl(Name, nullptr, BoxPosition, BoxExtents, 0, 0, 0,
                                                   0, 0);
        // 0x004a6e0c..0x004a6e16: the last box gets +0x35d |= 1 (no null test).
        if (Index == Count - 1)
        {
            unsigned char* const Wire = reinterpret_cast<unsigned char*>(ButtonBox[Index]);
            Wire[0x35d] = static_cast<unsigned char>(Wire[0x35d] | 1);
        }

        // FUN_005a0e30(name, "ButtonText", {0x32300000, Y + 0.005, -1}, 0, 0.27, 0.03, 1.5, 0,
        // 1, 0, 0) -> +0x34c[i]. The 0.005 is DAT_005d3e44 (FADD float).
        sprintf(Name, "PauseButtonText%d", Index);
        float const TextY = static_cast<float>(static_cast<double>(RowY) +
                                               static_cast<double>(NnFloatBits(0x3ba3d70a)));
        float const TextPosition[3] = {NnFloatBits(0x32300000), TextY, -1.0f};
        ButtonText[Index] = CreateTextControl(Name, "ButtonText", TextPosition, nullptr,
                                              NnFloatBits(0x3e8a3d71), NnFloatBits(0x3cf5c28f),
                                              NnFloatBits(0x3fc00000), 0, 1, 0, 0);
    }

    // 0x004a6e93..0x004a6eb2: the unused rows are zeroed (box, text, action).
    for (int Index = Count; Index < 5; ++Index)
    {
        ButtonBox[Index] = nullptr;
        ButtonText[Index] = nullptr;
        ButtonAction[Index] = 0;
    }

    // 0x004a6eb4..0x004a6ee5: the title -- 0x71c in single player, 0x724 in multiplayer (slot 42
    // (0x1e, ids, 0), no null test).
    if (SinglePlayer())
        Title->SetProperty(0x1e, 0x71c, 0);
    else
        Title->SetProperty(0x1e, 0x724, 0);

    // 0x004a6ee5..0x004a6fe7: each row's caption (text slot 42 (0x1e, ids, 0)) and tooltip (box
    // slot 42 (3, ids, 0)) by its action; an action outside 1..7 gets neither.
    for (int Index = 0; Index < Count; ++Index)
    {
        unsigned CaptionIds = 0;
        unsigned ToolTipIds = 0;
        switch (ButtonAction[Index])
        {
        case 1: CaptionIds = 0x71d; ToolTipIds = 0x33a; break;
        case 2: CaptionIds = 0x71e; ToolTipIds = 0x33b; break;
        case 3: CaptionIds = 0x71f; ToolTipIds = 0x33c; break;
        case 4: CaptionIds = 0x720; ToolTipIds = 0x33d; break;
        case 5: CaptionIds = 0x721; ToolTipIds = 0x33e; break;
        case 6: CaptionIds = 0x752; ToolTipIds = 0x33f; break;
        case 7: CaptionIds = 0x725; ToolTipIds = 0x340; break;
        default: continue;
        }
        ButtonText[Index]->SetProperty(0x1e, CaptionIds, 0);
        ButtonBox[Index]->SetProperty(3, ToolTipIds, 0);
    }

    // 0x004a6fed..0x004a6ff6
    DrawLayer = 0x64;
    DrawSubLayer = 6;
    // 0x004a6ffd..0x004a7017: FUN_004a7670's body inline -- hold the server tick, keep the cursor
    // flag, force the cursor on.
    bCursorDrawFlag = NnModalPauseGame();
    // 0x004a701d: FUN_00577b40(+0x32c).
    HudPushInputFocus(AsHudCmdHandler());
    // 0x004a7025..0x004a703b
    RegisterEvent(0x11);
    RegisterEvent(0xf);
    RegisterEvent(0x14);

    // 0x004a7052: FUN_004b5060, SETNZ.
    return Win3D::Create(NewParent, Data);
}

void NnPause::Update()
{
    // LAB_004a70d0: WinControl's Update (FUN_005a17b0), the software cursor forced on, then a
    // pending close deletes the window (FUN_0043d2a0 per child, slot 10, deleting dtor).
    WinControl::Update();
    if (GetClientMouseCursorDrawFlag() == 0)
        SetClientMouseCursorDrawFlag(1);
    if (bClosePending == 0)
        return;
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        Win3DDeleteTree(Child);
        Child = Next;
    }
    Destroy();
    delete this;
}

void NnPause::Draw()
{
    // FUN_00488960.
    Win3DFlushBeforeOverlayDraw(); // FUN_0045a9f0
    WinControl::Draw();            // JMP FUN_005a0760
}

bool NnPause::Slot23(unsigned Msg, unsigned A, unsigned B)
{
    // LAB_004a70a0 (RET 0xc): 0x3006 (NN_Confirm's answer) goes on to slot 42 with A and B;
    // always false.
    if (Msg == 0x3006)
        SetProperty(0x3006, A, B);
    return false;
}

intptr_t NnPause::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_004a7130 (RET 0xc). Nothing reaches WinControl's switch: every path returns 0 here.
    (void)B;
    if (Msg == 0x26)
    {
        // 0x004a71d0..0x004a7203: which button box sent the click; its action.
        int Index = 0;
        while (Index < 5)
        {
            if (A == reinterpret_cast<uintptr_t>(ButtonBox[Index]))
                break;
            Index = Index + 1;
        }
        if (Index == 5)
            return 0;
        // 0x004a71f4 `CMP EAX,-1` cannot hold here.
        DoAction(ButtonAction[Index]);
        return 0;
    }
    if (Msg == 0x42)
    {
        // 0x004a719e..0x004a71c6: slot 31 SetOffset with the three floats at A (by value), then
        // slot 5 SetPosition(&+0x60).
        float const* const Offset3 = reinterpret_cast<float const*>(A);
        SetOffset(Offset3[0], Offset3[1], Offset3[2]);
        SetPosition(Position);
        return 0;
    }
    if (Msg == 0x3006 && A == 1)
    {
        // 0x004a715c..0x004a7190: NN_Confirm said yes -- FUN_005763b0(0xc5, 0) (USER_EXIT_GAME),
        // FUN_004a7690(+0x330) inline, slot 33 ReleaseMouse, and the window goes at the next
        // Update (+0x331 = 1).
        HudPostCommand(0xc5, 0);
        NnModalResumeGame(bCursorDrawFlag);
        ReleaseMouse();
        bClosePending = 1;
        return 0;
    }
    return 0;
}

bool NnPause::OnEvent(unsigned MsgId)
{
    // LAB_004a7070 (RET 4): 0xf, 0x11 and 0x14 resume (FUN_004a7210(1) on this - 0x32c); true.
    // Anything else is not consumed.
    if (MsgId == 0xf || MsgId == 0x11 || MsgId == 0x14)
    {
        DoAction(1);
        return true;
    }
    return false;
}

void NnPause::ResumeAndDelete()
{
    NnModalResumeGame(bCursorDrawFlag);
    ReleaseMouse();
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        Win3DDeleteTree(Child);
        Child = Next;
    }
    Destroy();
    delete this;
}

void NnPause::DoAction(int Action)
{
    // FUN_004a7210 (thiscall, RET 4, a 0x800-byte wchar_t buffer on the stack).
    wchar_t Text[0x400];
    switch (Action)
    {
    case 0:
    case 1:
        // 0x004a7222..0x004a727a / 0x004a7282..0x004a72da: resume -- close the window.
        ResumeAndDelete();
        return;
    case 2:
    case 3:
    {
        // 0x004a72e2..0x004a734c / 0x004a735a..0x004a73c4: FUN_0042d6f0(1), close, then
        // FUN_00488970 (NN_LoadSave) with mode 2 (save) for action 2 and 1 (load) for action 3,
        // and FUN_0042d6f0(0). The window is deleted before the load/save one opens.
        SetClientServerTickPause(true);
        ResumeAndDelete();
        OpenNnLoadSave(Action == 2 ? 2 : 1);
        SetClientServerTickPause(false);
        return;
    }
    case 4:
    {
        // 0x004a73d6..0x004a7467: FUN_0042d6f0(1), close, FUN_0059db20("NN_Preferences",
        // "NN_Preferences", 0, 0) and slot 32 CaptureMouse on it, FUN_0042d6f0(0).
        SetClientServerTickPause(true);
        ResumeAndDelete();
        Win3D* const Preferences = WinFactoryCreate("NN_Preferences", "NN_Preferences", nullptr,
                                                    nullptr);
        if (Preferences != nullptr)
            static_cast<WinControl*>(Preferences)->CaptureMouse();
        SetClientServerTickPause(false);
        return;
    }
    case 5:
        // 0x004a746f..0x004a7497: FUN_004347e0(DAT_0067eca8, 0x75b, buffer, 0x400) and
        // FUN_004889e0(this, buffer, 0) -- the "quit?" NN_Confirm under this window; its answer
        // comes back through slot 23 (0x3006).
        GetIdsStringWWithRawFallback(0x75b, Text, 0x400);
        NnConfirmOpen(this, Text, nullptr);
        return;
    case 6:
        // 0x004a74aa..0x004a7519: FUN_00564b10(2) (DAT_0067c200 = 2), FUN_005763b0(0xc5, 0),
        // close.
        MenuNavSetIntroModeRequest(2);
        HudPostCommand(0xc5, 0);
        ResumeAndDelete();
        return;
    case 7:
        // 0x004a7521..0x004a7550: FUN_00564b10(3), then the NN_Confirm with ids 0x76b.
        MenuNavSetIntroModeRequest(3);
        GetIdsStringWWithRawFallback(0x76b, Text, 0x400);
        NnConfirmOpen(this, Text, nullptr);
        return;
    default:
        return;
    }
}

void NnPause::CloseNow()
{
    // FUN_004a7560.
    ResumeAndDelete();
}

void NnPauseOpen()
{
    // FUN_004a75c0.
    // 0x004a75c0..0x004a75e5: not with a NeuroNet screen up (DAT_0067dcc8), the server tick held
    // (FUN_0042d730) or DAT_00668761.
    if (G_bNnScreenUp)
        return;
    if (IsClientServerTickPaused())
        return;
    if (IsClientLocationInTransition()) // DAT_00668761 (0x004a75de)
        return;
    // 0x004a75e7..0x004a7600: with the software cursor shown, only when docked (FUN_004c4810) or
    // with a player object (FUN_0054baf0).
    if (GetClientMouseCursorDrawFlag() != 0)
    {
        if (!IsPlayerDockedRetail() && GetPlayerObjRW() == nullptr)
            return;
    }
    // 0x004a7602..0x004a7611: one NN_Pause at most (FUN_0059d9c0).
    if (Win3DFindByClassName("NN_Pause") != nullptr)
        return;
    // 0x004a7613..0x004a765c: FUN_0059db20("NN_Pause", "NN_Pause", 0, 0); slot 42 (0x42,
    // &{0, 0, 0}, 0) and slot 32 CaptureMouse on it.
    Win3D* const Created = WinFactoryCreate("NN_Pause", "NN_Pause", nullptr, nullptr);
    if (Created == nullptr)
        return;
    NnPause* const Pause = static_cast<NnPause*>(Created);
    float const Zero[3] = {0.0f, 0.0f, 0.0f};
    Pause->SetProperty(0x42, reinterpret_cast<uintptr_t>(Zero), 0);
    Pause->CaptureMouse();
}
