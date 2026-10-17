// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// -------------------------------------------------------------------------------------------
// HUD_CruiseProgress / [CruiseProgress] -- the cruise-engine charge caption under the crosshair
// ("<ids 0x585> - NN%" while the engine charges, "<ids 0x64f> - N%" while the charge timer is
// still negative).
//
// RTTI HUD_CruiseProgress : HUDItem @0, WinControl @0x38 (IDataViewOwner @0xb4). Size 0x42c.
// vftables:
//   HUDItem face 0x005d7f74 (11 slots) -- only slot 0 overridden (FUN_004d5d20, scalar deleting
//     dtor around FUN_004d5d40); the Render slot is inherited, the window draws from Win3D.
//   WinControl face 0x005d7ebc (45 slots) -- overrides 0 LAB_004d6260 (thunk, ECX -= 0x38, into
//     FUN_004d5d20), 9 FUN_004d5db0 Create, 11 FUN_004d6100 Update, 12 FUN_004d6140
//     UpdateInactive, 13 LAB_004d5eb0 Draw; every other slot is WinControl's 0x005e5e44 entry.
//
// Factory: the static HUDFACTORY<HUD_CruiseProgress> DAT_00674998 ("CruiseProgress"; XC entry
// LAB_004d5ba0, ctor FUN_004d6180, atexit LAB_004d5bc0 -> FUN_004cf010), vftable 0x005d7ff4
// whose Create FUN_004d61f0 is operator new(0x42c) + FUN_004d5bd0.
// -------------------------------------------------------------------------------------------

#include "hud_element_list.h"
#include "wincontrol_base.h"

class INI_Reader;

class HUD_CruiseProgress : public HUDItem, public WinControl
{
public:
    // FUN_004d5bd0 (thiscall, RET 4) -- the reader is not read.
    explicit HUD_CruiseProgress(INI_Reader& Reader);
    // FUN_004d5d40 (scalar deleting FUN_004d5d20; WinControl-face thunk LAB_004d6260)
    ~HUD_CruiseProgress() override;

    // WinControl slot 9 FUN_004d5db0 (RET 8) -- the "CruiseProgressText" label and the two
    // ids captions.
    bool Create(Win3D* NewParent, void* Data) override;
    // WinControl slot 11 FUN_004d6100 -- shown (and active) while the cursor is drawn and there
    // is a player ship, hidden otherwise; then Win3D's child walk.
    void Update() override;
    // WinControl slot 12 FUN_004d6140 (also in vftable 0x005d96fc) -- the show half of Update,
    // then Win3D's inactive child walk.
    void UpdateInactive() override;
    // WinControl slot 13 LAB_004d5eb0 -- word and draw the label from the player engine's
    // cruise charge.
    void Draw() override;

    WinControl* CruiseProgressText;  // +0x364 (WinControl +0x32c) "CruiseProgressText"
    wchar_t ChargingCaption[0x30];   // +0x368 (+0x330) ids 0x585, "%s - %d%%" while charging
    wchar_t CountdownCaption[0x30];  // +0x3c8 (+0x390) ids 0x64f, "%s - %d%" before the charge
    // +0x428 (+0x3f0) -- set while the "charging" caption is up; the first frame that sets it
    // invalidates the label's cached draw (FUN_0058ad70's second argument).
    unsigned char bCharging;
    unsigned char Pad429[3];

private:
    HUD_CruiseProgress(HUD_CruiseProgress const&) = delete;
    HUD_CruiseProgress& operator=(HUD_CruiseProgress const&) = delete;
};
