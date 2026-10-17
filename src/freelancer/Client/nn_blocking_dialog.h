// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail BlockingDialog (RTTI .?AVBlockingDialog@@ : WinControl) -- the invisible input blocker
// WaitForServerResponse (FUN_005a89b0) raises around its multiplayer server pump
// (FUN_00474520 before the loop, FUN_00474590 after it). It draws nothing of its own; while it is
// up it holds the key focus, the mouse capture and the client timing pause.
//
// WINFACTORY<BlockingDialog> DAT_0066db9c "BlockingDialog": static init LAB_00472150
// (FUN_00474750 + atexit LAB_00472170 -> FUN_00579380), Create FUN_004747c0
// (operator_new(0x330)).
//
// vftable 0x005d08e4 (45 slots) -- WinControl's with these overrides:
//   0 FUN_00474400 (scalar deleting dtor around FUN_00474420), 9 FUN_00474490 Create,
//   13 FUN_00488960 Draw (FUN_0045a9f0, then WinControl::Draw).
// No Event::Handler face; slot 42 stays WinControl's FUN_005a13c0.
//
// Layout:
//   +0x000 WinControl (0x32c)
//   +0x32c software-cursor draw flag: 1 from the factory, then FUN_004a7670's result
//   size 0x330

#include "wincontrol_base.h"

#include <stddef.h>

class BlockingDialog : public WinControl
{
public:
    // Inlined into FUN_004747c0: WinControl ctor, byte +0x32c = 1, FUN_0042d6c0, ++DAT_0067dccc.
    BlockingDialog();
    // FUN_00474420
    ~BlockingDialog() override;

    // slot 9 FUN_00474490
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 13 FUN_00488960
    void Draw() override;

    unsigned char bCursorDrawFlag; // +0x32c
    unsigned char Pad32d[3];       // +0x32d
};

static_assert(sizeof(BlockingDialog) == 0x330,
              "retail BlockingDialog is 0x330 bytes (operator_new 004747d8)");
static_assert(offsetof(BlockingDialog, bCursorDrawFlag) == 0x32c, "BlockingDialog +0x32c");

// FUN_00474520 (cdecl) -- unless one is registered, build a BlockingDialog, send it SetProperty
// 0x42 with {0,0,0} (WinControl's switch ignores 0x42) and capture the mouse.
void BlockingDialogOpen();
// FUN_00474590 (cdecl) -- close the registered BlockingDialog, if any. While the letterbox is
// visible (FUN_0041a3e0) the saved cursor flag is dropped to 0 first.
void BlockingDialogClose();
