// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail CargoBarControl (RTTI .?AUCargoBarControl@@ : WinControl) -- the hold bar of the
// NeuroNet inventory and dealer windows and the HUD cargo gauge: a quad strip filled to
// Used / Capacity, with an optional highlighted head (+0x33c) and tail (+0x338) segment, a
// line-loop border and a "used/capacity" tooltip.
//
// WINFACTORY<CargoBarControl> "CargoBarControl": ctor FUN_0055fcb0 (called from the static
// initializer at 0x0055e90a), Create FUN_0055fd20 (operator_new(0x350), FUN_0055e930, slot 9).
// Built directly by WinControl::CreateCargoBarControl (FUN_005a1080 -> Init FUN_0055ee80).
//
// vftable 0x005e1924 (45 slots) -- WinControl's with these overrides:
//    0 FUN_0055e9f0 scalar deleting dtor (WinControl's FUN_0059fa50 only)
//    4 LAB_0055fa80 HitTest          5 LAB_0055f5a0 SetPosition     6 LAB_0043d8e0 (Win3D's)
//    7 LAB_0055f5f0 SetOrientation   9 LAB_0055eb40 Create         11 LAB_0055f660 Update
//   13 LAB_0055ef60 Draw            15 FUN_0055fa00 MouseEnter     16 FUN_0055fa40 MouseLeave
//   19 FUN_005a0650 (WinControl's)  27 LAB_0055e9e0 GetHitPriority (0x64)
//   35 FUN_00401e90 RefreshAttachment (RET -- no hardpoint re-attach)
//   40 LAB_0055ea20 GetAttachOrientation   41 FUN_0055eaf0 GetAttachPosition
//   42 LAB_0055fb00 SetProperty
//
// Layout (ctor FUN_0055e930, size 0x350):
//   +0x32c "tooltip2" IDS -- the caption prefix of the tooltip ("%s %d/%d")
//   +0x330 capacity, +0x334 used, +0x338 tail segment, +0x33c head segment (floats)
//   +0x340 fill colour RGBA bytes ("color", ctor 0,0,0,0)
//   +0x344 segment colour RGBA bytes ("color_highlight", ctor 0xff,0xff,0,0xff)
//   +0x348 bHorizontal, +0x349 bHovered, +0x34a bToolTip, +0x34b (properties 0x2c / 0x2d),
//   +0x34c bBorder (ctor 1), +0x34d bShow (ctor 1)

#include "wincontrol_base.h"

#include <stddef.h>
#include <stdint.h>

class CargoBarControl : public WinControl
{
public:
    // FUN_0055e930
    CargoBarControl();

    // FUN_0055ee80 (RET 0x20) -- the argument list of FUN_005a1080 after the name.
    void Init(char const* MeshPath, float const* NewOffset, char const* Hardpoint,
              float const* NewHalfExtents, unsigned char bNewHorizontal, int NewToolTip2Ids,
              unsigned NewColor, unsigned NewHighlightColor);

    // --- Win3D / WinControl overrides ---
    bool HitTest(float const* Ray) override;                       // slot 4  LAB_0055fa80
    void SetPosition(float const* NewPosition) override;           // slot 5  LAB_0055f5a0
    void SetOrientation(float const* NewOrientation) override;     // slot 7  LAB_0055f5f0
    bool Create(Win3D* NewParent, void* Data) override;            // slot 9  LAB_0055eb40
    void Update() override;                                        // slot 11 LAB_0055f660
    void Draw() override;                                          // slot 13 LAB_0055ef60
    void MouseEnter(float const* Ray) override;                    // slot 15 FUN_0055fa00
    void MouseLeave(float const* Ray) override;                    // slot 16 FUN_0055fa40
    int GetHitPriority() override;                                 // slot 27 LAB_0055e9e0
    void RefreshAttachment() override;                             // slot 35 FUN_00401e90
    void GetAttachOrientation(float* OutOrientation9) override;    // slot 40 LAB_0055ea20
    void GetAttachPosition(float* OutPosition3) override;          // slot 41 FUN_0055eaf0
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override; // slot 42 LAB_0055fb00

    int ToolTip2Ids;                  // +0x32c
    float Capacity;                   // +0x330
    float Used;                       // +0x334
    float TailSegment;                // +0x338
    float HeadSegment;                // +0x33c
    unsigned char Color[4];           // +0x340
    unsigned char HighlightColor[4];  // +0x344
    unsigned char bHorizontal;        // +0x348
    unsigned char bHovered;           // +0x349
    unsigned char bToolTip;           // +0x34a
    unsigned char Field34b;           // +0x34b
    unsigned char bBorder;            // +0x34c
    unsigned char bShow;              // +0x34d
    unsigned char Pad34e[2];          // +0x34e

private:
    CargoBarControl(CargoBarControl const&) = delete;
    CargoBarControl& operator=(CargoBarControl const&) = delete;
};

static_assert(offsetof(CargoBarControl, ToolTip2Ids) == 0x32c, "CargoBarControl +0x32c");
static_assert(offsetof(CargoBarControl, Color) == 0x340, "CargoBarControl +0x340");
static_assert(offsetof(CargoBarControl, bShow) == 0x34d, "CargoBarControl +0x34d");
static_assert(sizeof(CargoBarControl) == 0x350, "retail CargoBarControl is 0x350 bytes");

// The tooltip hover record (tooltip_hover.h) as retail inlines it at each hover site: the
// per-site start values after construction and the caption copy (FUN_004142a0 + wcsncpy). The
// construction and the teardown (FUN_00444ee0) are the record's own ctor / dtor. Defined in
// cargo_bar_control.cpp; NN_Inventory's SetProperty 0x4f uses them too.
struct FToolTipHoverInfo;
void ToolTipHoverInit(FToolTipHoverInfo* Info);
void ToolTipHoverSetCaption(FToolTipHoverInfo* Info, wchar_t const* Text);
