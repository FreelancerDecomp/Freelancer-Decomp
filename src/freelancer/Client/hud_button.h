// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail WinButton (RTTI .?AUWinButton@@ : WinObject, vftable 0x005d7024, 0xf4 bytes) and
// HUD_Button (RTTI .?AUHUD_Button@@ : WinButton, HUDItem @ +0xf4; vftables 0x005d6fac and
// 0x005d6f78, 0x154 bytes).
//
// WinButton is a WinObject whose mesh spins about its Y axis while the cursor is over it and
// eases back to rest afterwards; its hit test is the mesh's projected screen rectangle. It has
// no constructor or destructor of its own in the PE: HUD_Button's factory FUN_004c8d40 inlines
// the constructor (0x004c8d84 .. 0x004c8dc4), and slot 0 is WinObject's FUN_004c9210.
//
// WinButton layout (after WinObject's 0xac):
//   +0xac  FTransform rest pose (FUN_004b5150: identity rotation, zero translation). Slot 7
//          snapshots the live orientation + position into it, slot 5 rebuilds the live
//          transform from it; +0xd0 is its translation
//   +0xdc  bHover -- slot 14 stores the hit test, slot 16 clears it
//   +0xe0  spin angle in degrees
//   +0xe4  spin speed, 180.0f degrees per second
//   +0xe8  0.85f, +0xec 0.75f -- written by the ctor, read nowhere in the PE
//   +0xf0  byte, cleared by the ctor, read nowhere in the PE
//
// WinButton overrides of WinObject's vftable:
//   4 FUN_0059f2e0 HitTest, 5 LAB_004c9320 SetPosition, 7 LAB_004c92e0 SetOrientation,
//   11 FUN_0059f360 Update, 13 FUN_0059f510 Draw, 14 FUN_004c9380 MouseOver,
//   16 LAB_004c93a0 MouseLeave.
// HUD_Button overrides on top: 0 FUN_004c9490 (dtor FUN_004c94b0), 11 LAB_004c93d0 Update,
// 13 LAB_004c93b0 Draw, 19 LAB_004c9430 MouseEvent. Its HUDItem face overrides only slot 0
// (LAB_004c9680, the this-0xf4 thunk).

#include "hud_element_list.h"
#include "win_object.h"
#include "x86math.h"

#include <stddef.h>

struct WinButton : public WinObject
{
    // 0x004c8d84 .. 0x004c8dc4 (inlined into FUN_004c8d40)
    WinButton();

    // slot 4 FUN_0059f2e0 -- the cursor (DAT_00616840 / DAT_00616844) inside the mesh's
    // projected screen rectangle, edges included. The ray is not read.
    bool HitTest(float const* Ray) override;
    // slot 5 LAB_004c9320 -- position into +0x60 and the rest pose, then the live transform
    // rebuilt from the rest pose
    void SetPosition(float const* NewPosition) override;
    // slot 7 LAB_004c92e0 -- orientation into +0x3c, then the live transform becomes the rest
    // pose
    void SetOrientation(float const* NewOrientation) override;
    // slot 11 FUN_0059f360
    void Update() override;
    // slot 13 FUN_0059f510
    void Draw() override;
    // slot 14 FUN_004c9380 -- bHover = slot 4 (Ray)
    void MouseOver(float const* Ray) override;
    // slot 16 LAB_004c93a0 -- bHover = 0
    void MouseLeave(float const* Ray) override;

    // FUN_004c92b0 (thiscall, RET 4) -- the twelve dwords into +0x3c, then the holder's
    // instance matrix (FUN_00421050) and FUN_00421070. The rest pose is left alone.
    void SetTransform(FTransform const* NewTransform);

    FTransform RestPose;         // +0xac
    unsigned char bHover;        // +0xdc
    unsigned char PadDD[3];
    float SpinAngle;             // +0xe0
    float SpinSpeed;             // +0xe4
    float FieldE8;               // +0xe8
    float FieldEC;               // +0xec
    unsigned char ByteF0;        // +0xf0
    unsigned char PadF1[3];
};

static_assert(sizeof(WinButton) == 0xf4, "retail WinButton is 0xf4 bytes");
static_assert(offsetof(WinButton, RestPose) == 0xac, "WinButton +0xac rest pose");
static_assert(offsetof(WinButton, bHover) == 0xdc, "WinButton +0xdc hover");
static_assert(offsetof(WinButton, SpinSpeed) == 0xe4, "WinButton +0xe4 spin speed");

// FUN_0059f200 (cdecl Holder, Out) -- the instance's projected screen rectangle as four floats
// (left, top, right, bottom): IEngine::GetInstanceExtent (+0x88) gives the bounding sphere
// centre and radius, GetInstancePosition (+0x90) moves the centre, and
// get_projected_bounding_box through the Win3D camera (DAT_0067dbf8) projects it. False without
// an instance or when the projection fails.
bool WinButtonProjectedRect(FClientMeshHolder* Holder, float* OutRect4);

struct HUD_Button : public WinButton, public HUDItem
{
    // FUN_004c8d40 after operator new(0x154) and WinObject's ctor
    explicit HUD_Button(INI_Reader& Reader);
    // FUN_004c94b0
    ~HUD_Button() override;

    // slot 11 LAB_004c93d0
    void Update() override;
    // slot 13 LAB_004c93b0
    void Draw() override;
    // slot 19 LAB_004c9430
    bool MouseEvent(FWin3DMouseEvent const* Event) override;

    TString<32> Action;          // +0x12c "action"
    int CommandId;               // +0x150 the command the action maps to
};

static_assert(offsetof(HUD_Button, Action) == 0x12c, "HUD_Button +0x12c action");
static_assert(offsetof(HUD_Button, CommandId) == 0x150, "HUD_Button +0x150 command");
static_assert(sizeof(HUD_Button) == 0x154, "retail HUD_Button is 0x154 bytes");

// FUN_004c8ca0 / FUN_004c8d40 -- UHUD_Button::HUDFACTORY, the static object DAT_006733b0 built
// by the XC entry LAB_004c8320 ("Button") and torn down by LAB_004c8340.
class HUD_ButtonFactory : public HUDItem_Factory
{
public:
    explicit HUD_ButtonFactory(char const* SectionName) : HUDItem_Factory(SectionName) {}
    // FUN_004c8d40
    HUDItem* Create(INI_Reader& Reader) override;
};
