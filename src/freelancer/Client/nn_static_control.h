// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail StaticControl (RTTI .?AUStaticControl@@ : WinControl, StaticControl.cpp) -- the
// NeuroNet / HUD mesh that only draws: frames, gauges, cargo bars, the menu "bImg" plates.
//
// vftable 0x005e4d7c (45 slots). Overrides of WinControl (slot -> function):
//   0 FUN_00587f40 (scalar deleting around FUN_00587f60), 5 LAB_005896b0 SetPosition,
//   7 LAB_00588d00 SetOrientation, 9 LAB_00587fc0 Create (the INI parser), 11 LAB_00588e80
//   Update, 13 LAB_00588770 Draw, 19 FUN_00588fe0 MouseEvent, 34 FUN_005897d0 ShowToolTip,
//   42 LAB_00589050 SetProperty. Slots 15 / 16 stay Win3D's RET 4, slot 4 WinControl's
//   FUN_005a0340; slot 44 is WinControl's LAB_00585090.
// WINFACTORY<StaticControl> DAT_0067c504 "StaticControl": static init LAB_00587e10 (ctor
// FUN_005899b0 + atexit LAB_00587e30 -> FUN_00579380), Create FUN_00589a20
// (operator_new(0x420) + FUN_00587e40, slot 9, scalar delete on failure).
//
// Layout (ctor FUN_00587e40 after FUN_0059f860):
//   +0x000 WinControl (0x32c)
//   +0x32c current roll, +0x330 current pitch, +0x334 current yaw (radians; the instance's
//          orientation is the parent orientation turned by these three, SetOrientation)
//   +0x338 "starting_yaw_pitch_roll": yaw, +0x33c pitch, +0x340 roll (Update snaps the current
//          angles back to these while rotation is off)
//   +0x344 "no_mesh_render", +0x345 "no_wire_render", +0x346 "no_coloring", +0x347 property
//          0xc (nothing in this class reads it), +0x348 use the two colours at +0x410 / +0x414
//          (property 0x18), +0x349 "enable_rotation", +0x34a "no_clear_render" (ctor 1; not
//          read by this class), +0x34b "render_to_window", +0x34c "zclear"
//   +0x350 the render-to-window camera (FUN_0040f150 base camera, vftable 0x005c8944, 0xc0)
//   +0x410 mesh colour RGBA, +0x414 wire colour RGBA (property 0x18)
//   +0x418 "force_radius" (the draw scale is force_radius / the instance's radius)
//   +0x41c "rotation_speed" (radians per second added to the yaw while rotation is on)
//   size 0x420
//
// The port-only menu adapter (the seven-argument Init, the screen rect, ButtonId,
// ContainsScreenPoint) is gone with its last user, menu_ui_widgets' FOverlayNav /
// CreateWinStaticChrome (the flat NN_Preferences shell, 2026-09-26); menu_enter_state's case-18
// chrome uses the retail 15-argument Init. The flat registry's own StaticControl
// (MenuUiCreateStaticControl[Ex]) and the caption-id adapter ApplyIdsCaption are gone too
// (2026-09-26): their only user was the briefing window, now LAB_0043d170 itself
// (mission_briefing.cpp).

#include "space_camera.h"
#include "wincontrol_base.h"

#include <stddef.h>
#include <stdint.h>
#include <windows.h> // was reached through the removed menu_input.h

class NnStaticControl : public WinControl
{
public:
    // FUN_00587e40
    NnStaticControl();
    // FUN_00587f60 (slot 0 FUN_00587f40 is its scalar deleting wrapper)
    ~NnStaticControl() override;

    // slot 5 LAB_005896b0 (RET 4)
    void SetPosition(float const* NewPosition) override;
    // slot 7 LAB_00588d00 (RET 4)
    void SetOrientation(float const* NewOrientation) override;
    // slot 9 LAB_00587fc0 (RET 8) -- Data is the INI_Reader positioned on this control's section
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 LAB_00588e80
    void Update() override;
    // slot 13 LAB_00588770
    void Draw() override;
    // slot 19 FUN_00588fe0 (RET 4)
    bool MouseEvent(FWin3DMouseEvent const* Event) override;
    // slot 34 FUN_005897d0
    void ShowToolTip() override;
    // slot 42 LAB_00589050 (RET 0xc)
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // FUN_00588540 (RET 0x3c). Callers: WinControl::CreateStaticControl FUN_005a0db0
    // (0x005a0e19) and LAB_0043d170 (0x0043d222, the briefing window, mission_briefing.cpp).
    // ForceRadius / RotationSpeed arrive as ints and are stored as floats (FILD).
    void Init(char const* NewMeshPath, float const* NewOffset, char const* Hardpoint,
              float const* NewHalfExtents, unsigned char bNewNoZEnable, unsigned char bNewZClear,
              unsigned char bNewNoMeshRender, unsigned char bNewNoWireRender,
              unsigned char bNewNoColoring, unsigned char bNewRenderToWindow,
              unsigned char bNewNoClearRender, int NewForceRadius, int NewRotationSpeed,
              unsigned char bNewEnableRotation, float const* NewStartYpr);

    float CurRoll;                 // +0x32c
    float CurPitch;                // +0x330
    float CurYaw;                  // +0x334
    float StartYpr[3];             // +0x338 yaw, +0x33c pitch, +0x340 roll
    unsigned char bNoMeshRender;   // +0x344
    unsigned char bNoWireRender;   // +0x345
    unsigned char bNoColoring;     // +0x346
    unsigned char Field_0x347;     // +0x347 -- property 0xc; TODO: meaning (no reader found)
    unsigned char bUseColours;     // +0x348
    unsigned char bEnableRotation; // +0x349
    unsigned char bNoClearRender;  // +0x34a
    unsigned char bRenderToWindow; // +0x34b
    unsigned char bZClear;         // +0x34c
    unsigned char Pad34d[3];       // +0x34d
    FSpaceCamera Camera;           // +0x350
    unsigned char MeshColour[4];   // +0x410
    unsigned char WireColour[4];   // +0x414
    float ForceRadius;             // +0x418
    float RotationSpeed;           // +0x41c

private:
    NnStaticControl(NnStaticControl const&) = delete;
    NnStaticControl& operator=(NnStaticControl const&) = delete;
};

static_assert(offsetof(NnStaticControl, CurRoll) == 0x32c, "StaticControl +0x32c");
static_assert(offsetof(NnStaticControl, StartYpr) == 0x338, "StaticControl +0x338");
static_assert(offsetof(NnStaticControl, bNoMeshRender) == 0x344, "StaticControl +0x344");
static_assert(offsetof(NnStaticControl, bZClear) == 0x34c, "StaticControl +0x34c");
static_assert(offsetof(NnStaticControl, Camera) == 0x350, "StaticControl +0x350");
static_assert(offsetof(NnStaticControl, MeshColour) == 0x410, "StaticControl +0x410");
static_assert(offsetof(NnStaticControl, ForceRadius) == 0x418, "StaticControl +0x418");
static_assert(offsetof(NnStaticControl, RotationSpeed) == 0x41c, "StaticControl +0x41c");
