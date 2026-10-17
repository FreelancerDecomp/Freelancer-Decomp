// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail NN_Pause (RTTI .?AVNN_Pause@@ : WinControl, Event::Handler;
// E:\FL\Scratch\Source\Client\nn\NN_Pause.cpp) -- the in-game pause window raised by USER_PAUSE
// (0x14) through FUN_004a75c0.
//
// WINFACTORY<NN_Pause> DAT_00672144 "NN_Pause": static init LAB_004a6ad0 (ctor FUN_004a76b0,
// atexit LAB_004a6af0 -> FUN_00579380), Create FUN_004a7720 (operator_new(0x374), WinControl
// ctor FUN_0059f860, the three vptrs, +0x331 = 0, slot 9; on failure the scalar deleting dtor).
//
// vftable 0x005d462c (45 slots, COL 0x005eb86c) -- WinControl's with these overrides:
//   0 FUN_004a6b00 (scalar deleting dtor around FUN_004a6b20), 9 FUN_004a6ba0 Create,
//   11 LAB_004a70d0 Update, 13 FUN_00488960 Draw (the body NN_Error / NN_NetworkOperation
//   share), 23 LAB_004a70a0, 42 LAB_004a7130 SetProperty.
// IDataViewOwner face +0x7c vftable 0x005d461c; Event::Handler face +0x32c vftable 0x005d4614
// (LAB_004a7070).
//
// Layout:
//   +0x000 WinControl (0x32c)
//   +0x32c Event::Handler vptr
//   +0x330 software-cursor draw flag saved when the window opened (FUN_0041dd90)
//   +0x331 close pending -- Update deletes the window (set when NN_Confirm answered yes)
//   +0x334 PauseTitle (TextControl)
//   +0x338 PauseButtonBox0..4 (WireButtonControl), +0x34c PauseButtonText0..4 (TextControl),
//   +0x360 the action of each button (1..7, see DoAction); unused rows are all zero
//   size 0x374

#include "event_handler.h"
#include "wincontrol_base.h"

#include <stddef.h>

class NnPause : public WinControl, public EventHandler
{
public:
    // Inlined into FUN_004a7720: WinControl ctor, +0x331 = 0.
    NnPause();
    // FUN_004a6b20
    ~NnPause() override;

    // slot 9 FUN_004a6ba0 (RET 8)
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 LAB_004a70d0
    void Update() override;
    // slot 13 FUN_00488960
    void Draw() override;
    // slot 23 LAB_004a70a0 (RET 0xc)
    bool Slot23(unsigned Msg, unsigned A, unsigned B) override;
    // slot 42 LAB_004a7130 (RET 0xc)
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // Event::Handler face, LAB_004a7070 (RET 4)
    bool OnEvent(unsigned MsgId) override;

    // FUN_004a7210 (thiscall, RET 4) -- carry out a button's action.
    void DoAction(int Action);
    // FUN_004a7560 (thiscall) -- close at once: resume, release the mouse, delete the window.
    // Caller: NN_Exit::Create 0x00471844.
    void CloseNow();

    unsigned char bCursorDrawFlag;   // +0x330
    unsigned char bClosePending;     // +0x331
    unsigned char Pad332[2];         // +0x332
    WinControl* Title;               // +0x334
    WinControl* ButtonBox[5];        // +0x338
    WinControl* ButtonText[5];       // +0x34c
    int ButtonAction[5];             // +0x360

private:
    // The tail every closing path shares (0x004a7222..0x004a7270 and the copies after it):
    // FUN_004a7690(+0x330) inline (FUN_0042d6f0(0), FUN_0041dda0), slot 33 ReleaseMouse,
    // FUN_0043d2a0 on every child, slot 10 Destroy, the scalar deleting dtor.
    void ResumeAndDelete();
};

static_assert(sizeof(void*) != 4 || sizeof(NnPause) == 0x374,
              "retail NN_Pause is 0x374 bytes (operator_new 004a7738)");
static_assert(sizeof(void*) != 4 || offsetof(NnPause, bCursorDrawFlag) == 0x330,
              "NN_Pause +0x330 cursor flag");
static_assert(sizeof(void*) != 4 || offsetof(NnPause, Title) == 0x334, "NN_Pause +0x334 title");
static_assert(sizeof(void*) != 4 || offsetof(NnPause, ButtonBox) == 0x338,
              "NN_Pause +0x338 button boxes");
static_assert(sizeof(void*) != 4 || offsetof(NnPause, ButtonText) == 0x34c,
              "NN_Pause +0x34c button texts");
static_assert(sizeof(void*) != 4 || offsetof(NnPause, ButtonAction) == 0x360,
              "NN_Pause +0x360 button actions");

// FUN_004a75c0 (cdecl) -- open the pause window unless a NeuroNet screen is up (DAT_0067dcc8),
// the server tick is held (FUN_0042d730), the room camera sequence runs (DAT_00668761) or the
// software cursor is shown in space with no player object; one live NN_Pause at most.
// Caller: the key-command handler LAB_005b2440 case USER_PAUSE (0x005b2466).
void NnPauseOpen();
