// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "nn_network_operation.h"

#include "font_string.h"        // FFontString (+0x28 LayoutGen, +0x30 bMultiLine)
#include "globals.h"            // PushClientTimingPause / PopClientTimingPause
#include "hud_command_dispatch.h"
#include "input_manager.h"      // MouseSetCursor (FUN_0041dde0)
#include "nn_modal.h"
#include "nn_text_control.h"    // NnTextControl::GetFontString (+0x334)
#include "win3d.h"
#include "win3d_renderables.h"  // Win3DFlushBeforeOverlayDraw (FUN_0045a9f0)
#include "win_factory.h"

// Retail DAT_0066dbc8 -- WINFACTORY<NN_NetworkOperation> static (LAB_00472120 -> FUN_00474600;
// Create FUN_00474670; atexit LAB_00472140 -> FUN_00579380).
WINFACTORY<NnNetworkOperation> G_NnNetworkOperationFactory("NN_NetworkOperation");

namespace
{
    char const kExitFrameMesh[] = "Interface\\NeuroNet\\Options\\alt-f4window.cmp"; // 005d04a8
}

NnNetworkOperation::NnNetworkOperation()
    : ResultFn(nullptr)
    , ResultArg(nullptr)
{
    // FUN_00474670 004746a9..004746eb: FUN_0059f860, vptrs, FUN_0042d6c0, INC DAT_0067dccc,
    // +0x330 = 0, +0x334 = 0 (the member initialisers above run before this body; nothing reads
    // them in between).
    PushClientTimingPause();
    G_ModalCaptureCount = G_ModalCaptureCount + 1;
}

NnNetworkOperation::~NnNetworkOperation()
{
    // FUN_004721a0: DEC DAT_0067dccc, FUN_0059d8c0(0), FUN_00577c30(+0x32c), FUN_0042d6d0; then
    // FUN_00576010 and FUN_0059fa50 as the base-class destructors.
    G_ModalCaptureCount = G_ModalCaptureCount - 1;
    Win3DSetKeyFocus(nullptr);
    HudPopInputFocus(AsHudCmdHandler());
    PopClientTimingPause();
}

bool NnNetworkOperation::Create(Win3D* NewParent, void* Data)
{
    // FUN_00472230 (RET 8).
    // 00472235..0047223e: FUN_0041dde0("Arrow", 0) -- the Mouse.cpp cursor shape.
    MouseSetCursor("Arrow", false);

    float const Zero[3] = {0.0f, 0.0f, 0.0f}; // local_24 / local_18

    // 00472246..004722bf: FUN_005a0db0 at (0, 0, -1).
    float FramePosition[3] = {0.0f, 0.0f, -1.0f};
    CreateStaticControl("ExitFrame", kExitFrameMesh, FramePosition, nullptr, Zero, 0, 0, 0, 1, 1,
                        0, 1, 0, 0, 0, Zero);

    // 004722c4..0047230e: FUN_005a0e30 at (0, 0x3d71a9fc, -1), ids 0x736 -> +0x33c.
    float TextPosition[3] = {0.0f, NnFloatBits(0x3d71a9fc), -1.0f};
    ExitText = CreateTextControl("ExitText", "Normal", TextPosition, nullptr,
                                 NnFloatBits(0x3eb851ec), NnFloatBits(0x3cf5c28f),
                                 NnFloatBits(0x3fa00000), 0x736, 1, 0, 0);

    // 00472314..00472324: the text's Font::String (+0x334) goes multi-line (+0x30 = 1, and
    // +0x28 = 0 to re-lay it out) unless it already is. No null test on the text.
    FFontString* const Font = static_cast<NnTextControl*>(ExitText)->GetFontString();
    if (Font->bMultiLine != 1)
    {
        Font->bMultiLine = 1;
        Font->LayoutGen = 0;
    }

    // 0047232b / 00472332
    DrawLayer = 0x6e;
    DrawSubLayer = 6;
    // 00472339..00472351
    bCursorDrawFlag = NnModalPauseGame();
    Win3DSetKeyFocus(this);
    HudPushInputFocus(AsHudCmdHandler());
    // 00472359..00472366
    RegisterEvent(0xf);
    RegisterEvent(0x11);

    // 00472377: FUN_004b5060, SETNZ.
    return Win3D::Create(NewParent, Data);
}

void NnNetworkOperation::Close()
{
    NnModalResumeGame(bCursorDrawFlag);
    if (ResultFn != nullptr)
        ResultFn(ResultArg, 0);
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

void NnNetworkOperation::Draw()
{
    // FUN_00488960.
    Win3DFlushBeforeOverlayDraw(); // FUN_0045a9f0
    WinControl::Draw();            // JMP FUN_005a0760
}

bool NnNetworkOperation::KeyEvent(unsigned Key, unsigned char* Modifiers)
{
    // LAB_00472450 (RET 8): only with modifier bit 1 clear (00472457 TEST [EAX],2 / JNZ) and
    // Key 0x1b does Escape cancel the wait.
    if ((*Modifiers & 0x02) != 0)
        return false;
    if (Key != 0x1b)
        return false;
    Close();
    return true;
}

intptr_t NnNetworkOperation::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_00472390 (RET 0xc): 0x42 recentres; every message returns 0 without reaching
    // WinControl's switch.
    (void)B;
    if (Msg == 0x42)
    {
        float const* const Offset3 = reinterpret_cast<float const*>(A);
        SetOffset(Offset3[0], Offset3[1], Offset3[2]);
        SetPosition(Position);
    }
    return 0;
}

bool NnNetworkOperation::OnEvent(unsigned MsgId)
{
    // LAB_004723d0: 0xf closes the dialog; 0x11 is swallowed (00472448 MOV AL,1); anything else
    // is not handled.
    if (MsgId == 0xf)
    {
        Close();
        return true;
    }
    if (MsgId == 0x11)
        return true;
    return false;
}

void NnNetworkOperationOpen(unsigned Ids)
{
    // FUN_00473ad0.
    if (Win3DFindByClassName("NN_NetworkOperation") != nullptr)
        return;
    Win3D* const Created =
        WinFactoryCreate("NN_NetworkOperation", "NN_NetworkOperation", nullptr, nullptr);
    if (Created == nullptr)
        return;
    NnNetworkOperation* const Dialog = static_cast<NnNetworkOperation*>(Created);
    float const Zero[3] = {0.0f, 0.0f, 0.0f};
    Dialog->SetProperty(0x42, reinterpret_cast<uintptr_t>(Zero), 0);
    // 00473b2a..00473b3f: the text only when +0x33c is set.
    if (Dialog->ExitText != nullptr)
        Dialog->ExitText->SetProperty(0x1e, Ids, 0);
    Dialog->CaptureMouse();
}

void NnNetworkOperationClose()
{
    // FUN_00473b60.
    Win3D* const Found = Win3DFindByClassName("NN_NetworkOperation");
    if (Found == nullptr)
        return;
    static_cast<NnNetworkOperation*>(Found)->Close();
}

bool NnNetworkOperationIsOpen()
{
    // FUN_00473be0.
    return Win3DFindByClassName("NN_NetworkOperation") != nullptr;
}
