// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail WireStaticControl / WireButtonControl / RichTextWireButtonControl -- the NeuroNet
// "wire box" controls: a rectangle of thin lines around a hit box, optionally blinking, that the
// dialogs use as buttons (NN_Error's OK box, NN_Prompt's Yes / No / OK).
//
// RTTI: .?AVRichTextWireButtonControl@@ : WireButtonControl : WireStaticControl : WinControl.
// Every class keeps WinControl's IDataViewOwner face (LAB_0053aaa0 / FUN_0053e4a0).
//
// WireStaticControl -- vftable 0x005e6084 (45 slots, COL 0x005f1608). Overrides of WinControl:
//   0 FUN_005a32f0 (scalar deleting around FUN_005a3310), 4 FUN_0053f330 (Win3D's: false),
//   5 LAB_005a3950 SetPosition, 7 LAB_005a3a30 SetOrientation, 9 FUN_005a3ea0 Create,
//   11 FUN_005a3aa0 Update, 13 LAB_005a3420 Draw, 34 FUN_005a3c30 ShowToolTip,
//   42 LAB_005a3b00 SetProperty.
// WINFACTORY<WireStaticControl> DAT_0067de10 "WireStaticControl": static init LAB_005a2830
// (ctor FUN_005a4c90 + atexit LAB_005a2850), Create FUN_005a4d00 (operator_new(0x388)).
//
// WireButtonControl -- vftable 0x005e614c (46 slots, COL 0x005f1670). Overrides of
// WireStaticControl: 0 FUN_005a3d90 (scalar deleting around FUN_005a3db0), 4 LAB_005a42c0
// (JMP FUN_005a0340, WinControl's HitTest), 11 LAB_005a4080 Update, 15 FUN_005a42d0 MouseEnter,
// 16 FUN_005a4330 MouseLeave, 19 FUN_005a4380 MouseEvent, 34 FUN_005a46c0 ShowToolTip,
// 42 LAB_005a4690 SetProperty; own slot 45 FUN_005a3e50 Activate.
// WINFACTORY<WireButtonControl> DAT_0067dde4 "WireButtonControl": static init LAB_005a2860
// (ctor FUN_005a4da0 + atexit LAB_005a2880), Create FUN_005a4e10 (operator_new(0x390)).
//
// RichTextWireButtonControl -- vftable 0x005e622c (COL 0x005f17b4) is WireButtonControl's slot
// for slot: no override of its own, not even the destructor (slot 0 is FUN_005a3d90).
// WINFACTORY<RichTextWireButtonControl> DAT_0067ddb8 "RichTextWireButtonControl": static init
// LAB_005a2890 (ctor FUN_005a4ed0 + atexit LAB_005a28b0), Create FUN_005a4f40
// (operator_new(0x398)).
//
// Layout (FUN_005a3210 + the factory Creates):
//   +0x000 WinControl (0x32c)
//   +0x32c orientation[9] (ctor identity; SetOrientation copies the attached result here too)
//   +0x350 wire centre[3]  (ctor 0; Init / SetPosition / Draw: position + offset)
//   +0x35c state bits (ctor 0x60): 0 hovered, 1 focused (0x2c / 0x2d), 2 pressed, 3 always
//          white (Init), 4 lit (0xc), 5 lit while pressed (ctor), 6 pulse while hovered (ctor),
//          7 draw while inactive (0x34)
//   +0x35d bit0 flush both line batchers at the end of Draw (ctor clears bit 0 only)
//   +0x35e blink on (0x51), +0x35f blink phase, +0x360 blink timer
//   +0x364 dword the ToolTip record's +0x44 takes; nothing in the class writes it
//   +0x368 sound name (VC6 std::string, 16 bytes), +0x378 RGBA override (0x33) and +0x37c its
//          switch, +0x380 UI sound slot (FUN_00564610, -1 when not a UI sound), +0x384 edge mask
//          (bit 0 top, 1 right, 2 bottom, 3 left; Init: 0xf when zero)
//   size 0x388
// WireButtonControl adds +0x388 button bits (factory: bit 0 cleared, bit 2 set) -- 0 captured,
// 1 right button, 2 enabled -- and +0x38c the KeyMap command posted on a click (0: the parent
// hears 0x26); size 0x390.
// RichTextWireButtonControl adds +0x390 the caption RichTextWin (built by Init) and +0x394 a
// pointer to +0x390; size 0x398.
//
// PORT: the sound name is the port's std::string, not the VC6 one (AGENTS.md: no retail ABI),
// so every field from +0x368 on sits at a different offset than retail. Nothing outside these
// classes reaches those fields by offset; the static_asserts below stop at +0x364.

#include "rich_text_win.h"
#include "wincontrol_base.h"

#include <stddef.h>
#include <stdint.h>
#include <string>

class WireStaticControl : public WinControl
{
public:
    // FUN_005a3210
    WireStaticControl();
    // FUN_005a3310
    ~WireStaticControl() override;

    // slot 4 FUN_0053f330 -- Win3D's pick: false
    bool HitTest(float const* Ray) override;
    // slot 5 LAB_005a3950
    void SetPosition(float const* NewPosition) override;
    // slot 7 LAB_005a3a30
    void SetOrientation(float const* NewOrientation) override;
    // slot 9 FUN_005a3ea0 (RET 8)
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 FUN_005a3aa0
    void Update() override;
    // slot 13 LAB_005a3420
    void Draw() override;
    // slot 34 FUN_005a3c30
    void ShowToolTip() override;
    // slot 42 LAB_005a3b00 (RET 0xc)
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // FUN_005a3360 (RET 0x18)
    void Init(float const* NewOffset, float const* NewHalfExtents, unsigned char bAlwaysWhite,
              unsigned char bNewNoZEnable, int NewToolTipIds, unsigned NewEdgeMask);

    float WireOrientation[9];      // +0x32c
    float WireCenter[3];           // +0x350
    unsigned char WireState;       // +0x35c
    unsigned char WireState2;      // +0x35d
    unsigned char bBlink;          // +0x35e
    unsigned char bBlinkPhase;     // +0x35f
    float BlinkTimer;              // +0x360
    int ToolTipAux;                // +0x364 -- not written by any WireStaticControl code
    std::string SoundName;         // retail +0x368 (VC6 std::string)
    unsigned char Colour[4];       // retail +0x378
    unsigned char bUseColour;      // retail +0x37c
    int SoundSlot;                 // retail +0x380
    unsigned EdgeMask;             // retail +0x384

protected:
    // The blink half of slots 11 (0x005a3aa8..0x005a3afe, again at 0x005a408d..0x005a40e6).
    void UpdateBlink();

private:
    WireStaticControl(WireStaticControl const&) = delete;
    WireStaticControl& operator=(WireStaticControl const&) = delete;
};

static_assert(offsetof(WireStaticControl, WireOrientation) == 0x32c, "WireStaticControl +0x32c");
static_assert(offsetof(WireStaticControl, WireCenter) == 0x350, "WireStaticControl +0x350");
static_assert(offsetof(WireStaticControl, WireState) == 0x35c, "WireStaticControl +0x35c");
static_assert(offsetof(WireStaticControl, BlinkTimer) == 0x360, "WireStaticControl +0x360");
static_assert(offsetof(WireStaticControl, ToolTipAux) == 0x364, "WireStaticControl +0x364");

class WireButtonControl : public WireStaticControl
{
public:
    // The WireButtonControl part of FUN_005a4e10 / FUN_005a4f40 (inlined there):
    // +0x388 = (+0x388 & 0xfe) | 4, +0x38c = 0, then the vptrs.
    WireButtonControl();
    // FUN_005a3db0
    ~WireButtonControl() override;

    // slot 4 LAB_005a42c0 -- JMP FUN_005a0340
    bool HitTest(float const* Ray) override;
    // slot 11 LAB_005a4080
    void Update() override;
    // slot 15 FUN_005a42d0 (RET 4)
    void MouseEnter(float const* Ray) override;
    // slot 16 FUN_005a4330 (RET 4)
    void MouseLeave(float const* Ray) override;
    // slot 19 FUN_005a4380 (RET 4)
    bool MouseEvent(FWin3DMouseEvent const* Event) override;
    // slot 34 FUN_005a46c0
    void ShowToolTip() override;
    // slot 42 LAB_005a4690 (RET 0xc) -- 0x2e reads the command id, the rest is WireStatic's
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // slot 45 (+0xb4) FUN_005a3e50 -- a left click: post the command, else tell the parent 0x26
    virtual void Activate();

    // FUN_005a3ee0 (RET 0x20)
    void Init(char const* NewSoundName, float const* NewOffset, float const* NewHalfExtents,
              unsigned char bAlwaysWhite, unsigned char bNewNoZEnable, int NewToolTipIds,
              int NewCommandId, unsigned NewEdgeMask);

    unsigned char ButtonState;     // retail +0x388
    int CommandId;                 // retail +0x38c

private:
    WireButtonControl(WireButtonControl const&) = delete;
    WireButtonControl& operator=(WireButtonControl const&) = delete;
};

class RichTextWireButtonControl : public WireButtonControl
{
public:
    // The RichTextWireButtonControl part of FUN_005a4f40 (inlined there): +0x394 = &+0x390,
    // +0x390 = 0.
    RichTextWireButtonControl();
    // No destructor of its own: slot 0 is WireButtonControl's FUN_005a3d90. The caption window
    // is a child and goes with the tree.
    ~RichTextWireButtonControl() override;

    // FUN_005a4810 (RET 0x24)
    void Init(char const* NewSoundName, float const* NewOffset, float const* NewHalfExtents,
              unsigned char bAlwaysWhite, unsigned char bNewNoZEnable, int NewToolTipIds,
              unsigned CaptionIds, int NewCommandId, unsigned NewEdgeMask);

    RichTextWin* TextWin;          // retail +0x390
    RichTextWin** TextWinRef;      // retail +0x394 -- &TextWin

private:
    RichTextWireButtonControl(RichTextWireButtonControl const&) = delete;
    RichTextWireButtonControl& operator=(RichTextWireButtonControl const&) = delete;
};

// FUN_005a4b50 (cdecl Controls, Count) -- join a column of wire controls: recompute each one's
// edge mask (+0x384) from its own state bits (+0x35c), its neighbours' and whether the next one
// is visible (Flags bits 0 and 1), so a hovered or focused row and its lit neighbours share
// edges. Every element must be a wire control; retail tests none of them for null. Callers:
// NN_Inventory's Draw (0x0047980e / 0x0047981c) on the ItemButton and ItemView columns.
void WireControlsJoinColumn(WinControl* const* Controls, int Count);
