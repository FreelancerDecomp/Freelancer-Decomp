// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Intro.cpp PageOptions (RTTI .?AUPageOptions@@ : UIPage::MasterWin) -- the main-menu
// Options page. One static instance, DAT_0067a7d0 (static init LAB_005735b0 -> ctor
// FUN_005735d0, atexit LAB_00573780 -> dtor FUN_00573710). vftables: main 0x005e2fbc,
// IUIPage face (+0x7c) 0x005e2fa4, Event::Handler face (+0x80) 0x005e2f9c (slot 0
// FUN_0053f330, false).
//
// Call path: TypeSelect command 0xa -> IntroFocus state 10. EnterState case 10 (0x00574a2f)
// releases the active page, calls slot 9 Create(0, 0) and makes this the active page (+0x228).
// Create builds the NN_Preferences WinControl (+0xb8, DAT_0067a888). IntroFocus::Update poll
// index 4 (0x00573f96) watches +0xb8 / +0xb4 (DAT_0067a884) to leave the state.
//
// Retail layout: MasterWin (+0x00..+0x8b, Data = 0 -- the ctor passes no page data), the
// FUiPageDesc at +0x8c (FUN_00591e30 / FUN_00591ef0, never loaded), +0xb4 Result,
// +0xb8 Preferences. The port's MasterWin has no Event::Handler face, so the members below
// do not sit at their retail byte offsets.

#include "ui_page.h"
#include "ui_page_desc.h"

class WinControl;

class PageOptions : public UIPage::MasterWin
{
public:
    // FUN_005735d0 -- MasterWin ctor with no page data (0x005735e9 PUSH 0), the desc ctor,
    // +0xb4 = 0.
    PageOptions();
    // FUN_00573710 -- the desc dtor, then the MasterWin dtor.
    ~PageOptions() override;

    // slot 9 FUN_00573650 (RET 8).
    bool Create(Win3D* NewParent, void* Payload) override;
    // slot 10 is LAB_00573640 = JMP FUN_00591de0 (MasterWin::Destroy): not overridden.

    // slot 23 FUN_005736a0 (RET 0xc).
    bool Slot23(unsigned Msg, unsigned A, unsigned B) override;

    FUiPageDesc Desc;        // retail +0x8c
    int Result;              // retail +0xb4 -- DAT_0067a884
    WinControl* Preferences; // retail +0xb8 -- DAT_0067a888
};

// DAT_0067a7d0.
PageOptions& GetPageOptions();
