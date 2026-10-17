// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "page_options.h"

#include "menu_ui_init.h"
#include "win_factory.h"
#include "wincontrol_base.h"

namespace
{
    // DAT_0067a7d0.
    PageOptions G_PageOptions;
}

PageOptions::PageOptions()
    : UIPage::MasterWin(nullptr)
    , Result(0)
    , Preferences(nullptr)
{
    // FUN_005735d0: FUN_00591ae0(0), FUN_00591e30 on +0x8c (Desc), the three vftables, then
    // +0xb4 = 0. +0xb8 is not written; the static object starts zeroed, which is what the
    // initialiser above reproduces.
}

PageOptions::~PageOptions()
{
    // FUN_00573710: vftables back, FUN_00591ef0 on +0x8c, FUN_00591ba0 -- the member and base
    // destructors.
}

bool PageOptions::Create(Win3D* NewParent, void* /*Payload*/)
{
    // FUN_00573650. MasterWin::Create (FUN_00591cd0) with a null payload; its result is not
    // tested (there is no page data, so it only reparents / registers / sets the flags).
    UIPage::MasterWin::Create(NewParent, nullptr);
    Result = 0;
    // 0x00573661..0x00573686: FUN_0059db20("NN_Preferences", "NN_Preferences", 0, 1), then
    // slot 2 (AsWinControl) on the result with no null test.
    Win3D* const Created =
        WinFactoryCreate("NN_Preferences", "NN_Preferences", nullptr, reinterpret_cast<void*>(1));
    Preferences = Created->AsWinControl();
    return true;
}

bool PageOptions::Slot23(unsigned Msg, unsigned A, unsigned /*B*/)
{
    // FUN_005736a0.
    if (Msg == 0x3004)
    {
        PlayOrRewindUiSoundSlot(0); // FUN_005645c0(0)
        if (A != 0x300c)
            return false;
        Result = 1;
        return true;
    }
    return Msg == 0xf002;
}

PageOptions& GetPageOptions()
{
    return G_PageOptions;
}
