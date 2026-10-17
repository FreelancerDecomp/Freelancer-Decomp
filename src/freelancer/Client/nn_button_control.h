// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail ButtonControl (RTTI .?AUButtonControl@@ : WinControl) -- the NeuroNet / HUD mesh or
// ButtonMontage-icon button.
//
// vftable 0x005e15f4 (46 slots). Overrides of WinControl (slot -> function):
//   0 FUN_0055a640 (scalar deleting around FUN_0055d610), 5 FUN_0055b4a0 SetPosition,
//   7 LAB_0055b5c0 SetOrientation, 9 FUN_0055a6b0 Create (the INI parser), 11 LAB_0055b660
//   Update, 13 FUN_0055af90 Draw, 15 FUN_0055b8a0 MouseEnter, 16 FUN_0055b900 MouseLeave,
//   19 FUN_0055b950 MouseEvent, 34 FUN_0055bde0 ShowToolTip, 42 FUN_0055bc30 SetProperty;
//   own slot 45 (+0xb4) FUN_0055a660 Activate. Slot 4 stays WinControl's FUN_005a0340.
// WINFACTORY<ButtonControl> DAT_00679aac "ButtonControl": static init LAB_005597a0 (ctor
// FUN_0055d240 + atexit LAB_005597c0 -> FUN_00579380), Create FUN_0055d2b0
// (operator_new(0x378) + FUN_0055a560, slot 9, scalar delete on failure).
//
// Layout (ctor FUN_0055a560 after FUN_0059f860):
//   +0x000 WinControl (0x32c)
//   +0x32c "tooltip2" id (Create) / Init argument 13; ctor 0. ButtonControl's own slots never
//          read it; ToggleButtonControl's Update does (0x0055c829 / 0x0055c870).
//   +0x330 state bits (ctor: & 0x80): 0 hovered (MouseEnter / MouseLeave), 1 focused (0x2c /
//          0x2d), 2 lit while pressed (MouseEvent / Update), 3 "action_button", 4 captured
//          (the press this button holds), 5 "no_color_change" (draw white), 6 selected (0xc),
//          7 the press was the right button
//   +0x331 state bits (ctor: (old & 0xee) | 0xe): 0 "no_mesh_render" (0x32), 1 enabled,
//          2 light while pressed, 3 pulse while hovered, 4 draw while inactive (0x34)
//   +0x332 spin (0x36), +0x333 blink (0x51), +0x334 blink phase, +0x335 montage mode
//          (FUN_0055bfc0; Draw hands it to FUN_00559aa0, which does not read it),
//   +0x336 use the +0x370 colour (0x33), +0x337 draw the glow pass (0x35)
//   +0x338 FButtonMontageTile* (Init / Create look it up by the mesh path; not set by the ctor)
//   +0x33c spin angle, +0x340 spin speed (ctor 180.0f), +0x344 blink timer
//   +0x348 the packed ARGB of the last glow pass (Draw)
//   +0x34c KeyMap command posted by Activate ("event" / FUN_0055a490 / Init argument 14)
//   +0x350 "mesh2" (VC6 std::string), +0x360 "button_sound" (VC6 std::string)
//   +0x370 RGBA colour (0x33), +0x374 UI sound slot (FUN_00564610; ctor 0)
//   size 0x378
//
// PORT: the two strings are the port's std::string, not VC6's (AGENTS.md: no retail ABI), so
// every field from +0x350 on sits at a different offset than retail. Nothing outside this class
// reaches those fields by offset; the static_asserts below stop at +0x34c.
//
// (2026-10-07) The port-only menu adapter that sat at the bottom of this class (the
// nine-argument Init, a screen rect, ButtonId / LabelIds, ContainsScreenPoint, SetHovered /
// SetFocused, SoundSlot) is gone with its only user, the flat menu layer of menu_ui_widgets.cpp.

#include "wincontrol_base.h"

#include <windows.h>

#include <stddef.h>
#include <stdint.h>
#include <string>

struct FButtonMontageTile;

class NnButtonControl : public WinControl
{
public:
    // FUN_0055a560
    NnButtonControl();
    // FUN_0055d610 (slot 0 FUN_0055a640 is its scalar deleting wrapper)
    ~NnButtonControl() override;

    // slot 5 FUN_0055b4a0 (RET 4)
    void SetPosition(float const* NewPosition) override;
    // slot 7 LAB_0055b5c0 (RET 4)
    void SetOrientation(float const* NewOrientation) override;
    // slot 9 FUN_0055a6b0 (RET 8) -- Data is the INI_Reader positioned on this control's section
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 LAB_0055b660
    void Update() override;
    // slot 13 FUN_0055af90
    void Draw() override;
    // slot 15 FUN_0055b8a0 (RET 4)
    void MouseEnter(float const* Ray) override;
    // slot 16 FUN_0055b900 (RET 4)
    void MouseLeave(float const* Ray) override;
    // slot 19 FUN_0055b950 (RET 4)
    bool MouseEvent(FWin3DMouseEvent const* Event) override;
    // slot 34 FUN_0055bde0
    void ShowToolTip() override;
    // slot 42 FUN_0055bc30 (RET 0xc)
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // slot 45 (+0xb4) FUN_0055a660 -- post the command, else tell the parent 0x26
    virtual void Activate();

    // FUN_0055ac30 (RET 0x38). Callers: WinControl::CreateButtonControl FUN_005a0c90
    // (0x005a0cf4), HUD_Receiver's ctor (0x004cd876), ToggleButtonControl::Init (0x0055c218).
    void Init(char const* NewMeshPath, char const* NewMesh2, char const* NewSoundName,
              char const* KeyCommandName, float const* NewOffset, char const* Hardpoint,
              float const* NewHalfExtents, unsigned char bNoMeshRender,
              unsigned char bActionButton, unsigned char bNoColorChange,
              unsigned char bNewNoZEnable, int NewToolTipIds, int NewToolTip2Ids,
              int NewCommandId);

    // FUN_0055bfc0 (RET 4) -- +0x335. Callers 0x00485ee4.. and 0x004a9c71.. (not ported).
    void SetMontageMode(unsigned char Mode) { MontageMode = Mode; }

    // Retail +0x330 / +0x331 / +0x337, for owners that write a button's state directly rather
    // than through SetProperty (CTradeEquipListLine 0x004bea35, FUN_004bee70, FUN_004bef00,
    // NN_Trade 0x004c2d82 / 0x004c35bb).
    unsigned char GetFlags330() const { return ButtonBits; }
    void SetFlags330(unsigned char Value) { ButtonBits = Value; }
    unsigned char GetFlags331() const { return ButtonBits2; }
    void SetFlags331(unsigned char Value) { ButtonBits2 = Value; }
    void SetByte337(unsigned char Value) { bGlow = Value; }

    int ToolTip2Ids;               // +0x32c
    unsigned char ButtonBits;      // +0x330
    unsigned char ButtonBits2;     // +0x331
    unsigned char bSpin;           // +0x332
    unsigned char bBlink;          // +0x333
    unsigned char bBlinkPhase;     // +0x334
    unsigned char MontageMode;     // +0x335
    unsigned char bUseColour;      // +0x336
    unsigned char bGlow;           // +0x337
    FButtonMontageTile* MontageTile; // +0x338
    float SpinAngle;               // +0x33c
    float SpinSpeed;               // +0x340
    float BlinkTimer;              // +0x344
    unsigned long GlowArgb;        // +0x348
    int CommandId;                 // +0x34c
    std::string Mesh2;             // retail +0x350 (VC6 std::string)
    std::string SoundName;         // retail +0x360 (VC6 std::string)
    unsigned char Colour[4];       // retail +0x370
    int UiSoundSlot;               // retail +0x374

private:
    NnButtonControl(NnButtonControl const&) = delete;
    NnButtonControl& operator=(NnButtonControl const&) = delete;
};

static_assert(offsetof(NnButtonControl, ToolTip2Ids) == 0x32c, "ButtonControl +0x32c");
static_assert(offsetof(NnButtonControl, ButtonBits) == 0x330, "ButtonControl +0x330");
static_assert(offsetof(NnButtonControl, bGlow) == 0x337, "ButtonControl +0x337");
static_assert(offsetof(NnButtonControl, MontageTile) == 0x338, "ButtonControl +0x338");
static_assert(offsetof(NnButtonControl, SpinSpeed) == 0x340, "ButtonControl +0x340");
static_assert(offsetof(NnButtonControl, GlowArgb) == 0x348, "ButtonControl +0x348");
static_assert(offsetof(NnButtonControl, CommandId) == 0x34c, "ButtonControl +0x34c");

// Retail ToggleButtonControl (RTTI .?AUToggleButtonControl@@ : ButtonControl) -- a
// ButtonControl with a second state: its own mesh ("mesh2", drawn while the state is 1), its
// own tooltip (+0x32c while the state is 1) and a button sound per state.
//
// vftable 0x005e1764 (46 slots, split in the tracker as 0x005e1764 + 0x005e1788). Overrides of
// ButtonControl (slot -> function): 0 FUN_0055d6f0 (scalar deleting around FUN_0055d710),
// 5 LAB_0055c510 SetPosition, 7 LAB_0055c5a0 SetOrientation, 9 FUN_0055bfd0 Create,
// 11 LAB_0055c690 Update, 13 LAB_0055c3b0 Draw, 19 LAB_0055cd20 MouseEvent, 42 LAB_0055cf90
// SetProperty, 45 FUN_0055cc90 Activate. Every other slot is ButtonControl's (0x005e15f4).
// WINFACTORY<ToggleButtonControl> DAT_00679a80 "ToggleButtonControl": static init LAB_005597d0
// (ctor FUN_0055d350 + atexit LAB_005597f0 -> FUN_00579380), Create FUN_0055d3c0
// (operator_new(0x3cc), FUN_0055a560 + the members below, slot 9, scalar delete on failure).
//
// Layout past ButtonControl (retail offsets; the port's strings move everything from +0x350 on):
//   +0x378 FClientMeshHolder (0x28) -- the state-1 mesh, instance at +0x37c
//   +0x3a0 the state (0 / 1); not written by the ctor (Create / Init zero it)
//   +0x3a4 button sound of state 0, +0x3b4 of state 1 (VC6 std::string)
//   +0x3c4 / +0x3c8 their UI sound slots (FUN_00564610); not written by the ctor
//   size 0x3cc
class NnToggleButtonControl : public NnButtonControl
{
public:
    NnToggleButtonControl();
    // FUN_0055d710 (slot 0 FUN_0055d6f0 is its scalar deleting wrapper)
    ~NnToggleButtonControl() override;

    // slot 5 LAB_0055c510 (RET 4)
    void SetPosition(float const* NewPosition) override;
    // slot 7 LAB_0055c5a0 (RET 4)
    void SetOrientation(float const* NewOrientation) override;
    // slot 9 FUN_0055bfd0 (RET 8)
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 LAB_0055c690
    void Update() override;
    // slot 13 LAB_0055c3b0
    void Draw() override;
    // slot 19 LAB_0055cd20 (RET 4)
    bool MouseEvent(FWin3DMouseEvent const* Event) override;
    // slot 42 LAB_0055cf90 (RET 0xc)
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;
    // slot 45 FUN_0055cc90
    void Activate() override;

    // FUN_0055c1c0 (RET 0x34). Caller: WinControl::CreateToggleButtonControl FUN_005a0fb0
    // (0x005a100f). ButtonControl::Init with these thirteen arguments and command 0, then
    // NewMesh2 (when not null) into the +0x378 holder.
    void Init(char const* NewMeshPath, char const* NewMesh2, char const* NewSoundName,
              char const* KeyCommandName, float const* NewOffset, char const* Hardpoint,
              float const* NewHalfExtents, unsigned char bNoMeshRender,
              unsigned char bActionButton, unsigned char bNoColorChange,
              unsigned char bNewNoZEnable, int NewToolTipIds, int NewToolTip2Ids);

    // retail +0x378: an Object3D built by the factory's inlined ctor (FUN_00420d40, the five
    // floats 1.0f, vftable 0x005d2efc, +0x10 = -1) and torn down by its own dtor FUN_0059ef20
    // (~FClientMeshHolder) after the two strings declared behind it -- the retail order of
    // FUN_0055d710.
    FClientMeshHolder ToggleMesh;
    int ToggleState;               // retail +0x3a0
    std::string SoundState0;       // retail +0x3a4 (VC6 std::string)
    std::string SoundState1;       // retail +0x3b4 (VC6 std::string)
    int UiSoundSlotState0;         // retail +0x3c4
    int UiSoundSlotState1;         // retail +0x3c8

private:
    // The block Create (0x0055c055..0x0055c126), Init (0x0055c250..0x0055c320) and Update
    // (0x0055c708..0x0055c7e1) share after loading the +0x378 holder: with an instance, the
    // instance at +0x60 + +0xa8 and, with a main instance too, the main instance's transform
    // as its orientation; then slot 35 either way.
    void PlaceToggleMesh();

    NnToggleButtonControl(NnToggleButtonControl const&) = delete;
    NnToggleButtonControl& operator=(NnToggleButtonControl const&) = delete;
};

// Retail RichTextButtonControl (RTTI .?AVRichTextButtonControl@@ : ButtonControl).
// WINFACTORY<RichTextButtonControl> DAT_00679a54 "RichTextButtonControl": static init
// LAB_00559800 (ctor FUN_0055d4e0 + atexit LAB_00559820 -> FUN_00579380). Its Create FUN_0055d550
// inlines the ctor: operator_new(0x380), ButtonControl's FUN_0055a560, +0x37c = &+0x378, the
// vftables 0x005e1834 / 0x005e1828 (+0x7c), +0x378 = 0; then slot 9, scalar delete on failure.
// vftable 0x005e1834 is ButtonControl's 0x005e15f4 slot for slot (46 slots, raw dword diff: no
// override, slot 0 is ButtonControl's FUN_0055a640), so the class adds only the two fields below
// and no behaviour. No retail code reads them.
class NnRichTextButtonControl : public NnButtonControl
{
public:
    NnRichTextButtonControl()
        : Field_0x378(nullptr)
        , Field_0x37c(&Field_0x378) // 0x0055d590..0x0055d596
    {
    }

    // TODO: meaning not recovered -- a null head (+0x378) and a pointer to it (+0x37c), the
    // shape of an empty singly linked queue; nothing in the exe reads either field.
    void* Field_0x378;          // retail +0x378
    void** Field_0x37c;         // retail +0x37c

private:
    NnRichTextButtonControl(NnRichTextButtonControl const&) = delete;
    NnRichTextButtonControl& operator=(NnRichTextButtonControl const&) = delete;
};

// FUN_0055a490 (cdecl) -- both outputs zeroed, then "tractor" 0x8b / 0xbc2, "next" 0x1d / 0xbb9,
// "prev" 0x1e / 0xbba, "next_subtarget" 0x21 / 0x41f, "prev_subtarget" 0x22 / 0x420 (stricmp,
// first match wins). Callers: Create's "event" (0x0055a850) and Init (0x0055adf7).
void ButtonKeyCommandLookup(char const* KeyName, int* OutCommandId, int* OutToolTipIds);
