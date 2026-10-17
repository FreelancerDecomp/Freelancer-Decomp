// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail NN_NetworkOperation (RTTI .?AVNN_NetworkOperation@@ : WinControl, Event::Handler) --
// the "waiting for the server" notice WaitForServerResponse raises while a multiplayer connect
// is pending (FUN_00473ad0 / FUN_00473b60 / FUN_00473be0).
//
// WINFACTORY<NN_NetworkOperation> DAT_0066dbc8 "NN_NetworkOperation": static init LAB_00472120
// (FUN_00474600 + atexit LAB_00472140 -> FUN_00579380), Create FUN_00474670
// (operator_new(0x340)).
//
// vftable 0x005d0554 (45 slots) -- WinControl's with these overrides:
//   0 FUN_00472180 (scalar deleting dtor around FUN_004721a0), 9 FUN_00472230 Create,
//   13 FUN_00488960 Draw (FUN_0045a9f0, then WinControl::Draw -- shared by the NN dialogs),
//   20 LAB_00472450 KeyEvent, 42 LAB_00472390 SetProperty.
// Event::Handler face +0x32c vftable 0x005d0540 (LAB_004723d0).
//
// Layout:
//   +0x000 WinControl (0x32c), +0x32c Event::Handler vptr
//   +0x330 result callback, cdecl (Arg, Result) -- only ever called with 0
//   +0x334 callback argument
//   +0x338 software-cursor draw flag saved by FUN_004a7670
//   +0x33c ExitText (TextControl)
//   size 0x340

#include "event_handler.h"
#include "nn_exit.h" // FNnDialogResultFn
#include "wincontrol_base.h"

#include <stddef.h>

class NnNetworkOperation : public WinControl, public EventHandler
{
public:
    // Inlined into FUN_00474670: WinControl ctor, FUN_0042d6c0, ++DAT_0067dccc, +0x330 and
    // +0x334 = 0.
    NnNetworkOperation();
    // FUN_004721a0
    ~NnNetworkOperation() override;

    // slot 9 FUN_00472230
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 13 FUN_00488960
    void Draw() override;
    // slot 20 LAB_00472450
    bool KeyEvent(unsigned Key, unsigned char* Modifiers) override;
    // slot 42 LAB_00472390
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // Event::Handler face, LAB_004723d0
    bool OnEvent(unsigned MsgId) override;

    FNnDialogResultFn ResultFn;    // +0x330
    void* ResultArg;               // +0x334
    unsigned char bCursorDrawFlag; // +0x338
    unsigned char Pad339[3];       // +0x339
    WinControl* ExitText;          // +0x33c

    // FUN_004a7690(+0x338), the callback with 0, slot 33 ReleaseMouse, FUN_0043d2a0 on every
    // child, slot 10 Destroy, delete -- inlined identically at 004723e3, 00472463 and in
    // FUN_00473b60 (00473b74..00473bd0).
    void Close();
};

static_assert(sizeof(NnNetworkOperation) == 0x340,
              "retail NN_NetworkOperation is 0x340 bytes (operator_new 00474688)");
static_assert(offsetof(NnNetworkOperation, ResultFn) == 0x330, "NN_NetworkOperation +0x330");
static_assert(offsetof(NnNetworkOperation, bCursorDrawFlag) == 0x338, "NN_NetworkOperation +0x338");
static_assert(offsetof(NnNetworkOperation, ExitText) == 0x33c, "NN_NetworkOperation +0x33c");

// FUN_00473ad0 (cdecl) -- unless one is registered, build an NN_NetworkOperation, centre it
// (SetProperty 0x42 with {0,0,0}), set its text to Ids (SetProperty 0x1e) and capture the mouse.
void NnNetworkOperationOpen(unsigned Ids);
// FUN_00473b60 (cdecl) -- close the registered NN_NetworkOperation, if any: the same sequence as
// its own cancel path, callback included.
void NnNetworkOperationClose();
// FUN_00473be0 (cdecl) -- FUN_0059d9c0("NN_NetworkOperation") != 0.
bool NnNetworkOperationIsOpen();
