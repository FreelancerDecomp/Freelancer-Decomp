// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// UITextEdit -- PAGE_*.txt edit field (RTTI .?AUUITextEdit@@ : RichTextEditWin : RichTextWin :
// BaseRichTextWin : WinControl; Event::Handler face at +0x498).
//
// Retail:
//   WINFACTORY<UITextEdit> DAT_0067db18, static init LAB_0059c240 -> FUN_0059c680("UITextEdit")
//   Create FUN_0059c6f0: operator_new(0x538), RichTextEditWin ctor FUN_0057b8d0, then vftables
//   0x005e5bd4 (main) / 0x005e5bc8 (+0x7c) / 0x005e5bc0 (+0x498 Event::Handler).
//   slot 0 FUN_0059cde0 scalar deleting destructor (dtor FUN_0059cf50)
//   slot 9 FUN_0059a410 Create
//
// Retail offsets are kept in the comments only: RichTextEditWin holds std containers (current
// MSVC STL), so the port's layout past +0x4c4 no longer matches retail's 0x538 bytes.

#include "rich_text_win.h"

class UiTextEdit : public RichTextEditWin
{
public:
    UiTextEdit();
    ~UiTextEdit() override;

    // slot 9 (+0x24) -- FUN_0059a410 (RET 8); Data is an FUiPayloadText*.
    bool Create(Win3D* NewParent, void* Data) override;

    float LocalWidth;      // +0x530 -- payload +0x00 (also +0x330)
    float LocalHeight;     // +0x534 -- payload +0x04 (also +0x334)
};
