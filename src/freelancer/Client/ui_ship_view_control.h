// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// UIShipViewControl -- the PAGE_*.page ship-preview widget (retail UIWin.cpp): a ShipViewControl
// whose size comes from the page line ("UIShipViewControl name x y w h", FUiPayloadBounds) and
// whose view box follows the widget's transform.
//
// Retail:
//   WINFACTORY<UIShipViewControl> DAT_0067da3c, static init LAB_0059c330 -> FUN_0059cc80
//   ("UIShipViewControl"), atexit LAB_0059c350 -> FUN_00579380. Create FUN_0059ccf0:
//   operator_new(0x57c), ShipViewControl ctor FUN_005836a0, vftables 0x005e59a4 / 0x005e5998
//   (+0x7c), then slot 9.
//   vftable 0x005e59a4 is ShipViewControl's 0x005e4bcc with three slots replaced:
//     slot 0 FUN_0059bfd0  scalar deleting destructor (FUN_0059bff0: the two vftables back, then
//                          ShipViewControl's destructor FUN_00583c30)
//     slot 8 LAB_0059c100  SetTransform
//     slot 9 FUN_0059c010  Create (payload {Width, Height})
//
// Layout past ShipViewControl (retail 0x570 bytes):
//   +0x570 the view box's half extent {Width * 0.5, Height * -0.5, 0}; size 0x57c.

#include "ship_view_control.h"

class UiShipViewControl : public ShipViewControl
{
public:
    // The inlined part of FUN_0059ccf0 after FUN_005836a0: only the vftables; +0x570..+0x578 are
    // written by Create.
    UiShipViewControl();
    // FUN_0059bff0.
    ~UiShipViewControl() override;

    // slot 8 (+0x20) -- LAB_0059c100 (RET 4).
    void SetTransform(float const* Transform) override;
    // slot 9 (+0x24) -- FUN_0059c010 (RET 8).
    bool Create(Win3D* NewParent, void* Payload) override;

    float HalfExtent[3]; // +0x570

private:
    UiShipViewControl(UiShipViewControl const&) = delete;
    UiShipViewControl& operator=(UiShipViewControl const&) = delete;
};
