// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// UIStaticText -- PAGE_*.txt caption widget (RTTI .?AUUIStaticText@@ : StaticText : TextWin :
// Win3D). StaticText has no vftable of its own (nothing but UIStaticText derives from it), so
// the port hangs UiStaticText directly on TextWin. UIStaticText's vftable 0x005e5a64 overrides
// TextWin 0x005d54cc in:
//   slot 0  FUN_0059cda0  scalar deleting destructor
//   slot 8  LAB_00599b70  SetTransform
//   slot 9  FUN_00599ac0  Create
//   slot 13 0x00599ce0    draw: debug frame (dead -- DAT_0067dbc4 has no writer), then
//                         JMP TextWin draw FUN_005a1d40 (the port inherits TextWin::Draw)
// Size 0xf4 (operator_new in WINFACTORY<UIStaticText>::Create FUN_0059c3d0, which inlines the
// Win3D + TextWin constructors). UIStaticText's own fields: +0xec / +0xf0.

#include "text_win.h"

#include <stddef.h>

class UiStaticText : public TextWin
{
public:
    UiStaticText();
    ~UiStaticText() override;

    // slot 8 (+0x20) -- LAB_00599b70.
    void SetTransform(float const* Transform) override;
    // slot 9 (+0x24) -- FUN_00599ac0; args (Parent, FUiPayloadText*). Always returns true.
    bool Create(Win3D* NewParent, void* Payload) override;

    float LocalWidth;               // +0xec -- payload +0x00
    float LocalHeight;              // +0xf0 -- payload +0x04
};

static_assert(sizeof(UiStaticText) == 0xf4, "retail UIStaticText is 0xf4 bytes");
static_assert(offsetof(UiStaticText, LocalWidth) == 0xec, "UIStaticText +0xec");
static_assert(offsetof(UiStaticText, LocalHeight) == 0xf0, "UIStaticText +0xf0");
