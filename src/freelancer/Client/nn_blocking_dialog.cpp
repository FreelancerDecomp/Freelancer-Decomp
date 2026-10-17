// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "nn_blocking_dialog.h"

#include "globals.h"            // PushClientTimingPause / PopClientTimingPause
#include "input_manager.h"      // MouseSetCursor (FUN_0041dde0)
#include "nn_modal.h"
#include "space_letterbox.h"    // IsLetterboxVisible (FUN_0041a3e0)
#include "win3d.h"
#include "win3d_renderables.h"  // Win3DFlushBeforeOverlayDraw (FUN_0045a9f0)
#include "win_factory.h"

// Retail DAT_0066db9c -- WINFACTORY<BlockingDialog> static (LAB_00472150 -> FUN_00474750; Create
// FUN_004747c0; atexit LAB_00472170 -> FUN_00579380).
WINFACTORY<BlockingDialog> G_BlockingDialogFactory("BlockingDialog");

BlockingDialog::BlockingDialog()
    : bCursorDrawFlag(1)
{
    // FUN_004747c0 004747fb..0047481e: FUN_0059f860, vptrs, byte +0x32c = 1, FUN_0042d6c0,
    // INC DAT_0067dccc.
    PushClientTimingPause();
    G_ModalCaptureCount = G_ModalCaptureCount + 1;
}

BlockingDialog::~BlockingDialog()
{
    // FUN_00474420: DEC DAT_0067dccc, FUN_0059d8c0(0), FUN_0042d6d0; then FUN_0059fa50 as the
    // base-class destructor.
    G_ModalCaptureCount = G_ModalCaptureCount - 1;
    Win3DSetKeyFocus(nullptr);
    PopClientTimingPause();
}

bool BlockingDialog::Create(Win3D* NewParent, void* Data)
{
    // FUN_00474490 (RET 8).
    // 00474494..0047449d: FUN_0041dde0("Arrow", 0) -- the Mouse.cpp cursor shape.
    MouseSetCursor("Arrow", false);

    // 004744a2 / 004744a9
    DrawLayer = 0x64;
    DrawSubLayer = 6;
    // 004744b0..004744bc
    bCursorDrawFlag = NnModalPauseGame();
    Win3DSetKeyFocus(this);
    // 004744c1..004744f7: +0xb4 = {2.0f, 2.0f, 0.0f}.
    HalfExtents[0] = 2.0f;
    HalfExtents[1] = 2.0f;
    HalfExtents[2] = 0.0f;

    // 00474502: FUN_004b5060, SETNZ.
    return Win3D::Create(NewParent, Data);
}

void BlockingDialog::Draw()
{
    // FUN_00488960.
    Win3DFlushBeforeOverlayDraw(); // FUN_0045a9f0
    WinControl::Draw();            // JMP FUN_005a0760
}

void BlockingDialogOpen()
{
    // FUN_00474520.
    if (Win3DFindByClassName("BlockingDialog") != nullptr)
        return;
    Win3D* const Created =
        WinFactoryCreate("BlockingDialog", "BlockingDialog", nullptr, nullptr);
    if (Created == nullptr)
        return;
    BlockingDialog* const Dialog = static_cast<BlockingDialog*>(Created);
    float const Zero[3] = {0.0f, 0.0f, 0.0f};
    Dialog->SetProperty(0x42, reinterpret_cast<uintptr_t>(Zero), 0);
    Dialog->CaptureMouse();
}

void BlockingDialogClose()
{
    // FUN_00474590.
    Win3D* const Found = Win3DFindByClassName("BlockingDialog");
    if (Found == nullptr)
        return;
    BlockingDialog* const Dialog = static_cast<BlockingDialog*>(Found);
    // 004745a4..004745ad
    if (IsLetterboxVisible())
        Dialog->bCursorDrawFlag = 0;
    NnModalResumeGame(Dialog->bCursorDrawFlag);
    Dialog->ReleaseMouse();
    Win3D* Child = Dialog->ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        Win3DDeleteTree(Child);
        Child = Next;
    }
    Dialog->Destroy();
    delete Dialog;
}
