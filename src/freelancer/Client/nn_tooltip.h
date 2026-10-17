// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail ToolTip (RTTI .?AUToolTip@@ : TextWin, vftable 0x005d5f7c, 29 slots, size 0x16c),
// built by WINFACTORY<ToolTip>::Create FUN_004be420 (the inlined TextWin part, the +0xf4 text
// entry, then FUN_004bd770) and kept in the singleton DAT_0067244c. Overrides against TextWin:
// 0 FUN_004bd670 (dtor FUN_004bd690), 4 FUN_0053f330 (never hit), 9 FUN_004bd830 Create,
// 13 LAB_004bdfe0 Draw. SetText (slot 28) is TextWin's LAB_005a1cb0.
// Feed path: ButtonControl vt+0x88 FUN_0055bde0 / WireButton FUN_005a46c0
//   → FUN_004be370 → FUN_004bdea0 → FUN_004bd880.
// Draw LAB_004bdfe0 + reposition FUN_004bda70 use Font::String at +0x7c.

#include "ids_strings.h"
#include "text_win.h"
#include "font_string.h"
#include "tooltip_hover.h"
#include "win_factory.h"

#include <stddef.h>

// DAT_0067244c — set by ToolTip factory (LAB_004be280 / FUN_004be3b0 path).
class NnToolTipControl : public TextWin
{
public:
    NnToolTipControl();
    ~NnToolTipControl() override;

    // FUN_004bd770 — style + default scalars.
    void InitToolTipDefaults();

    // slot 4 FUN_0053f330 -- never picked.
    bool HitTest(float const* Ray) override;

    // slot 9 FUN_004bd830 -- TextWin's setup, hidden, sub-layer 7.
    bool Create(Win3D* NewParent, void* Data) override;

    // FUN_004bdea0 — thiscall; hover info on stack.
    void UpdateFromHover(FToolTipHoverInfo const* Info);

    // FUN_004bd880 — copy hover into live fields + resolve caption + KeyMap append.
    void ApplyHover(FToolTipHoverInfo const* Info);

    // FUN_004bdda0 — per-frame delay timers; param is delta seconds.
    void TickToolTip(double DeltaSeconds);

    // FUN_004be390 — clear sticky-visible + hover flag.
    void HideNow();

    // LAB_004bdfe0 — gates + background quads + FontStringDrawChildren.
    void Draw() override;

    // The body of FUN_004be2f0: the live hover's ids (+0xf4 / +0xf8), position (+0x13c),
    // half extents (+0x148) and live byte (+0x154) into Out; nothing else of Out is written.
    void ReadHover(FToolTipHoverInfo* Out) const;

    int HoverIds() const { return Hover_.Ids; }
    unsigned char bHasText() const { return bHasText_; }
    unsigned char bFedThisFrame() const { return bFedThisFrame_; }

private:
    // FUN_004bda70 (0x004bda70..0x004bdd1d): re-anchors the tooltip under the cursor ray cached
    // by the Win3D input handler (Win3DGetMouseRay, FUN_0059da70) and clamps it to the viewport.
    void RepositionAfterCaption(FToolTipHoverInfo const* Info);

    // +0xf4..+0x158: an embedded hover record, built by FUN_004be420 (0x004be4cc..0x004be507)
    // and torn down by FUN_004bd690 (0x004bd6ca..0x004bd747). FUN_004bd880 copies only Ids,
    // IdsAlt, WorldPos, HalfExtents and bLive into it; its caption (+0xfc), Font::String
    // (+0x104) and AuxField (+0x138) are never written after construction.
    FToolTipHoverInfo Hover_;
    unsigned char bBuilt_;       // +0x158  FUN_004bdda0: the +0x6c bit 1 of the last tick
    // +0x159: set to 1 only by FUN_004bdda0 while a mouse button is down (DAT_00616850 & 3,
    // 0x004bde87); FUN_004bdea0 then hides a repeated hover and clears it on a new one.
    unsigned char bHasText_;
    unsigned char bFedThisFrame_; // +0x164
    float DelayRemain_;          // +0x15c
    float DelayAccum_;           // +0x160
    float DelayThreshold_;       // +0x168 = 0.25f
    float OffsetX_;              // +0xec = 4.0f
    float OffsetY_;              // +0xf0 = -42.0f
};

// FUN_004be2b0 — guarded TickToolTip on singleton. Retail forwards the double dt of
// FUN_0059e080 as two dwords (0059e0d6..0059e0d8).
void NnToolTipTickGlobal(double DeltaSeconds);

// FUN_004be390.
void NnToolTipHideGlobal();

// FUN_004be2d0 -- the singleton's +0x164 byte (set by FUN_004bdea0 when a hover is fed, cleared
// by FUN_004bdda0), false without a singleton. Callers: 0x00494d06, HUD_Targetable_Objects
// Render 0x004ec95b.
bool NnToolTipWasFedThisFrame();

// FUN_004be2f0 (cdecl) -- with a singleton, its live hover into *Out (NnToolTipControl::
// ReadHover) and true; false (Out untouched) without one. Callers: NeuroNetNavMap::Update
// (0x00494f48, 0x00495131, 0x004954cc, 0x00495729).
bool NnToolTipReadHover(FToolTipHoverInfo* Out);

NnToolTipControl* GetNnToolTipSingleton();
void SetNnToolTipSingleton(NnToolTipControl* Tip);

// WINFACTORY<ToolTip> DAT_00672410 (vftable 0x005d6010), built by the static initialiser
// LAB_004be280 through FUN_004be3b0("ToolTip") and torn down by LAB_004be2a0 (FUN_00579380).
// FUN_0059db20("ToolTip", ...) reaches Create by name.
class ToolTipFactory : public WinFactory
{
public:
    explicit ToolTipFactory(char const* Name)
        : WinFactory(Name)
    {
    }

    // FUN_004be420 -- also installs DAT_0067244c.
    Win3D* Create(Win3D* Parent, void* Payload) override;
};
