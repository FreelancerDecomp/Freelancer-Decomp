// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail NN_Exit (RTTI .?AVNN_Exit@@ : WinControl, Event::Handler) -- the "quit Freelancer?"
// confirmation raised by SC_CLOSE / Alt+F4 (FUN_00471f30).
//
// WINFACTORY<NN_Exit> DAT_0066db08 "NN_Exit": static init LAB_00471740 (FUN_00471fb0 + atexit
// LAB_00471760 -> FUN_00579380), Create FUN_00472020 (operator_new(0x350)).
//
// vftable 0x005d035c (45 slots) -- WinControl's with these overrides:
//   0 FUN_00471770 (scalar deleting dtor around FUN_00471790), 9 FUN_00471810 Create,
//   11 FUN_00471e00 Update, 13 FUN_00471b60 Draw, 42 LAB_00471cb0 SetProperty.
// IDataViewOwner face +0x7c vftable 0x005d0350; Event::Handler face +0x32c vftable 0x005d0348
// (LAB_00471b90).
//
// Layout:
//   +0x000 WinControl (0x32c)
//   +0x32c Event::Handler vptr
//   +0x330 result callback, cdecl (Arg, Result): 0 = quit confirmed, 1 = cancelled
//   +0x334 callback argument
//   +0x338 bImmediate -- Update closes at once (FUN_00471f30's argument)
//   +0x339 software-cursor draw flag saved by FUN_004a7670
//   +0x33c ExitText (TextControl), +0x340 ExitButtonBox0/1 (WireButtonControl),
//   +0x348 ExitButtonText0/1 (TextControl)
//   size 0x350

#include "event_handler.h"
#include "wincontrol_base.h"

#include <stddef.h>

typedef void (*FNnDialogResultFn)(void* Arg, int Result);

class NnExit : public WinControl, public EventHandler
{
public:
    // Inlined into FUN_00472020: WinControl ctor, then +0x330 / +0x334 / +0x338 = 0.
    NnExit();
    // FUN_00471790
    ~NnExit() override;

    // slot 9 FUN_00471810
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 FUN_00471e00
    void Update() override;
    // slot 13 FUN_00471b60
    void Draw() override;
    // slot 42 LAB_00471cb0
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // Event::Handler face, LAB_00471b90
    bool OnEvent(unsigned MsgId) override;

    FNnDialogResultFn ResultFn;   // +0x330
    void* ResultArg;              // +0x334
    unsigned char bImmediate;     // +0x338
    unsigned char bCursorDrawFlag; // +0x339
    unsigned char Pad33a[2];      // +0x33a
    WinControl* ExitText;         // +0x33c
    WinControl* ButtonBox[2];     // +0x340
    WinControl* ButtonText[2];    // +0x348

private:
    // The close sequence every exit path shares: FUN_004a7690(+0x339), the callback with
    // Result, slot 33 ReleaseMouse, FUN_0043d2a0 on every child, slot 10 Destroy, delete.
    void Close(int Result);
};

static_assert(sizeof(NnExit) == 0x350, "retail NN_Exit is 0x350 bytes (operator_new 00472039)");
static_assert(offsetof(NnExit, ResultFn) == 0x330, "NN_Exit +0x330 callback");
static_assert(offsetof(NnExit, bImmediate) == 0x338, "NN_Exit +0x338 immediate");
static_assert(offsetof(NnExit, ExitText) == 0x33c, "NN_Exit +0x33c ExitText");
static_assert(offsetof(NnExit, ButtonBox) == 0x340, "NN_Exit +0x340 button boxes");
static_assert(offsetof(NnExit, ButtonText) == 0x348, "NN_Exit +0x348 button texts");

// FUN_00471e80 (cdecl) -- the player's ship is gone: a mission message of kind 5 (the story star,
// FUN_004b9c60) whose FmtStr is the bare ids 0x723. Caller: the space state's frame 0x0054b1e2,
// once per loss (space_state.cpp, latched in its +0x09).
void NnExitPostShipLostMessage();

// FUN_00471f30 (cdecl) -- unless an NN_Exit is registered, build one through the factory, centre
// it (SetProperty 0x42 with {0,0,0}), store bImmediate at +0x338 and capture the mouse.
void NnExitOpen(bool bImmediate);
