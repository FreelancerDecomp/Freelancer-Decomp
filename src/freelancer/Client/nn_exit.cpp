// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "nn_exit.h"

#include "globals.h"            // g_client.window (DAT_0067eca0)
#include "hud_command_dispatch.h"
#include "input_manager.h"      // MouseSetCursor, Get/SetClientMouseCursorDrawFlag
#include "nav_bar.h"            // NavBarCloseService (FUN_004424f0)
#include "nn_pause.h"           // NnPause::CloseNow (FUN_004a7560)
#include "nn_main_button.h"     // G_bNnKeepPanelOpen (DAT_0066d359)
#include "nn_modal.h"
#include "nn_story_star.h"      // StoryStarOnMissionMessage (FUN_004b9c60)
#include "win3d.h"
#include "win3d_renderables.h"  // Win3DFlushBeforeOverlayDraw (FUN_0045a9f0)
#include "win_factory.h"

#include "common_text.h"        // FmtStr

#include <stdio.h>
#include <windows.h>

// Retail DAT_0066db08 -- WINFACTORY<NN_Exit> static (LAB_00471740 -> FUN_00471fb0; Create
// FUN_00472020; atexit LAB_00471760 -> FUN_00579380).
WINFACTORY<NnExit> G_NnExitFactory("NN_Exit");

namespace
{
    char const kExitFrameMesh[] = "Interface\\NeuroNet\\Options\\alt-f4window.cmp";     // 005d04a8
    char const kExitTextFrameMesh[] = "Interface\\NeuroNet\\Options\\alt-f4textplain.3db"; // 005d046c
}

NnExit::NnExit()
    : ResultFn(nullptr)
    , ResultArg(nullptr)
    , bImmediate(0)
{
    // FUN_00472020 00472058..0047208c: FUN_0059f860, the three vptrs, +0x330 = 0, +0x334 = 0,
    // byte +0x338 = 0. Nothing else is written before Create.
}

NnExit::~NnExit()
{
    // FUN_00471790: vptrs back to NN_Exit's, FUN_00577c30(+0x32c); then the Event::Handler base
    // dtor FUN_00576010 and WinControl's FUN_0059fa50 run as the base-class destructors.
    HudPopInputFocus(AsHudCmdHandler());
}

bool NnExit::Create(Win3D* NewParent, void* Data)
{
    // FUN_00471810 (RET 8).
    //
    // 00471810..00471825: `if (!DAT_0066d359) FUN_0045a460()` -- close NN_MainButton's panel
    // unless something holds it open (G_bNnKeepPanelOpen, nn_main_button.h).
    if (G_bNnKeepPanelOpen == 0)
        NnMainButtonOnModalOpen();
    // 0047182a: FUN_004424f0(1) -- the NavBar's open service tab closes.
    NavBarCloseService(true);

    // 00471831..00471844: FUN_0059da10("NN_Pause"), and FUN_004a7560 on it when found.
    NnPause* const Pause = static_cast<NnPause*>(Win3DFindByName("NN_Pause"));
    if (Pause != nullptr)
        Pause->CloseNow();

    // 00471849: FUN_0041dde0("arrow", 0) -- the Mouse.cpp cursor shape (lower-case in retail;
    // the lookup is stricmp).
    MouseSetCursor("arrow", false);

    float const Zero[3] = {0.0f, 0.0f, 0.0f}; // local_134 / local_128

    // 00471858..004718cb: FUN_00422b80(&local_118, 0, 0, -1), then FUN_005a0db0.
    float FramePosition[3] = {0.0f, 0.0f, -1.0f};
    CreateStaticControl("ExitFrame", kExitFrameMesh, FramePosition, nullptr, Zero, 0, 0, 0, 1, 1,
                        0, 1, 0, 0, 0, Zero);

    // 004718d0..00471946: (0, 0x3cf5c288, -1).
    float TextFramePosition[3] = {0.0f, NnFloatBits(0x3cf5c288), -1.0f};
    CreateStaticControl("ExitTextFrame", kExitTextFrameMesh, TextFramePosition, nullptr, Zero, 0,
                        0, 1, 0, 1, 0, 1, 0, 0, 0, Zero);

    // 0047194b..00471997: (0, 0x3d0b4394, -1) -> +0x33c.
    float TextPosition[3] = {0.0f, NnFloatBits(0x3d0b4394), -1.0f};
    ExitText = CreateTextControl("ExitText", "PageHeader", TextPosition, nullptr,
                                 NnFloatBits(0x3eb851ec), NnFloatBits(0x3cf5c28f),
                                 NnFloatBits(0x3fc00000), 0, 1, 0, 0);

    // 004719a7..00471a93: two buttons side by side. The row coordinate is
    // i * DAT_005d043c - DAT_005d0438 (FILD / FMUL / FSUB, FSTP float); the box sits at
    // + DAT_005d0434 and the caption at + DAT_005d041c, both as the X component (the FSTP lands
    // in the first dword of the position; Y is the immediate that follows).
    float const kRowStep = NnFloatBits(0x3e4ccccd);     // DAT_005d043c 0.2
    float const kRowOrigin = NnFloatBits(0x3e6b851e);   // DAT_005d0438 0.23
    float const kBoxOffset = NnFloatBits(0x3e051eb8);   // DAT_005d0434 0.13
    float const kCaptionOffset = NnFloatBits(0x3e051eb9); // DAT_005d041c
    for (int Index = 0; Index < 2; ++Index)
    {
        float const Row = static_cast<float>(Index) * kRowStep - kRowOrigin;
        char Name[0x100]; // local_100

        sprintf(Name, "ExitButtonBox%d", Index);
        float BoxPosition[3] = {Row + kBoxOffset, NnFloatBits(0xbd99999a), -1.0f};
        float BoxExtents[3] = {NnFloatBits(0x3db645a2), NnFloatBits(0x3cac0831), 0.0f};
        ButtonBox[Index] = CreateWireButtonControl(Name, "ui_select_item", BoxPosition,
                                                   BoxExtents, 0, 0, 0, 0, 0);

        sprintf(Name, "ExitButtonText%d", Index);
        float CaptionPosition[3] = {Row + kCaptionOffset, NnFloatBits(0xbd916873), -1.0f};
        ButtonText[Index] = CreateTextControl(Name, "ButtonText", CaptionPosition, nullptr,
                                              NnFloatBits(0x3e8a3d71), NnFloatBits(0x3cf5c28f),
                                              NnFloatBits(0x3fc00000), 0, 1, 0, 0);
    }

    // 00471a99..00471ad8: slot 42 (0x1e, ids, 0) on the three captions, no null test.
    ExitText->SetProperty(0x1e, 0x726, 0);
    ButtonText[0]->SetProperty(0x1e, 0x727, 0);
    ButtonText[1]->SetProperty(0x1e, 0x728, 0);

    // 00471ade / 00471ae5
    DrawLayer = 0x6e;
    DrawSubLayer = 6;
    // 00471aec..00471afe
    bCursorDrawFlag = NnModalPauseGame();
    HudPushInputFocus(AsHudCmdHandler());
    // 00471b06..00471b25
    RegisterEvent(0x13);
    RegisterEvent(0x12);
    RegisterEvent(0xf);
    RegisterEvent(0x11);

    // 00471b3c: FUN_004b5060, SETNZ.
    return Win3D::Create(NewParent, Data);
}

void NnExit::Close(int Result)
{
    NnModalResumeGame(bCursorDrawFlag);
    if (ResultFn != nullptr)
        ResultFn(ResultArg, Result);
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

void NnExit::Update()
{
    // FUN_00471e00 -- no WinControl Update call. With +0x338 set the dialog confirms at once:
    // the same sequence as the confirm button, DestroyWindow included (00471e6a).
    if (bImmediate == 0)
        return;
    Close(0);
    DestroyWindow(g_client.window);
}

void NnExit::Draw()
{
    // FUN_00471b60.
    if (GetClientMouseCursorDrawFlag() == 0)
        SetClientMouseCursorDrawFlag(1);
    Win3DFlushBeforeOverlayDraw(); // FUN_0045a9f0
    WinControl::Draw();            // JMP FUN_005a0760
}

intptr_t NnExit::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_00471cb0 (RET 0xc). Nothing reaches WinControl's switch: every path returns 0 here.
    (void)B;
    if (Msg == 0x26)
    {
        // 00471cf8..00471d0f: which button box sent the click.
        int Index = 0;
        while (Index < 2)
        {
            if (A == reinterpret_cast<uintptr_t>(ButtonBox[Index]))
                break;
            Index = Index + 1;
        }
        if (Index == 2)
            return 0;
        // 00471d18 `CMP EAX,-1` cannot hold here; 00471d21: box 0 confirms and closes the game
        // window, 00471d95: box 1 cancels.
        if (Index == 0)
        {
            Close(0);
            DestroyWindow(g_client.window);
            return 0;
        }
        Close(1);
        return 0;
    }
    if (Msg == 0x42)
    {
        // 00471cc6..00471cee: slot 31 SetOffset with the three floats at A, then slot 5
        // SetPosition(&+0x60).
        float const* const Offset3 = reinterpret_cast<float const*>(A);
        SetOffset(Offset3[0], Offset3[1], Offset3[2]);
        SetPosition(Position);
        return 0;
    }
    return 0;
}

bool NnExit::OnEvent(unsigned MsgId)
{
    // LAB_00471b90 -- jump table over MsgId - 0xf (00471c94):
    //   0xf / 0x12 -> caseD_0 00471c25: cancel (callback 1), no DestroyWindow
    //   0x11 / 0x13 -> caseD_2 00471ba9: confirm (callback 0), DestroyWindow
    //   0x10 and everything outside 0xf..0x13 -> false
    switch (MsgId)
    {
    case 0xf:
    case 0x12:
        Close(1);
        return true;
    case 0x11:
    case 0x13:
        Close(0);
        DestroyWindow(g_client.window);
        return true;
    default:
        return false;
    }
}

void NnExitPostShipLostMessage()
{
    // FUN_00471e80. 0x00471e98..0x00471ed6: the FmtStr zeroed (+0, +4 and the fourteen index
    // bytes); begin_mad_lib(0x723), end_mad_lib, FUN_004b9c60(&text, 5), ~FmtStr.
    FmtStr Text;
    Text.begin_mad_lib(0x723);
    Text.end_mad_lib();
    StoryStarOnMissionMessage(&Text, 5);
}

void NnExitOpen(bool bImmediate)
{
    // FUN_00471f30.
    if (Win3DFindByClassName("NN_Exit") != nullptr)
        return;
    Win3D* const Created = WinFactoryCreate("NN_Exit", "NN_Exit", nullptr, nullptr);
    if (Created == nullptr)
        return;
    NnExit* const Exit = static_cast<NnExit*>(Created);
    float const Zero[3] = {0.0f, 0.0f, 0.0f};
    Exit->SetProperty(0x42, reinterpret_cast<uintptr_t>(Zero), 0);
    Exit->bImmediate = bImmediate ? 1 : 0;
    Exit->CaptureMouse();
}
