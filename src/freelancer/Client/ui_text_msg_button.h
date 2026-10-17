// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// UITextMsgButton -- PAGE_*.txt text button (RTTI .?AUUITextMsgButton@@ : RichTextWin :
// BaseRichTextWin : WinControl; IDataViewOwner face at +0x7c).
//
// Retail:
//   WINFACTORY<UITextMsgButton> DAT_0067db44, static init LAB_0059c210 -> FUN_0059c510
//   Create FUN_0059c580: operator_new(0x4c8), RichTextWin ctor FUN_0057b1b0, two empty
//   RenderDisplayLists at +0x4a8 / +0x4b4, vftables 0x005e5af4 / 0x005e5ae4 (+0x7c).
//   vftable 0x005e5af4 overrides of RichTextWin 0x005e476c:
//     slot 0  FUN_0059cdc0  scalar deleting destructor (dtor FUN_0059ce50)
//     slot 5  LAB_00599eb0  SetPosition -- style-mesh offset (+0xa8..+0xb0), then 0057b0a0
//     slot 7  LAB_00599e00  SetOrientation -- FUN_005a0950, then the style mesh kept upright
//     slot 9  FUN_00599f60  Create
//     slot 13 LAB_0059a260  Draw -- hover/idle/inactive caption colour, then FUN_0057ab10
//     slot 15 LAB_0059a240  MouseEnter -- if (+0x498) FUN_00564700 ("ui_rollover" sound)
//     slot 19 LAB_0059a3d0  MouseEvent -- click -> parent slot 23 (0x3004, +0x4a4, 0)
//
// Layout (after RichTextWin's 0x498):
//   +0x498 interactive (Create writes 1), +0x49c / +0x4a0 payload width / height,
//   +0x4a4 message argument, +0x4a8 caption RDL, +0x4b4 text RDL, +0x4c0 caption TRA
//   attributes, +0x4c4 never written; size 0x4c8.

#include "rich_text_win.h"

#include <stddef.h>

class UiTextMsgButton : public RichTextWin
{
public:
    UiTextMsgButton();
    // FUN_0059ce50
    ~UiTextMsgButton() override;

    // slot 5 LAB_00599eb0 (RET 4)
    void SetPosition(float const* NewPosition) override;
    // slot 7 LAB_00599e00 (RET 4)
    void SetOrientation(float const* NewOrientation) override;
    // slot 9 FUN_00599f60 (RET 8); Data is an FUiPayloadTextMsgButton*.
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 13 LAB_0059a260
    void Draw() override;
    // slot 15 LAB_0059a240 (RET 4) -- an interactive button plays the rollover sound.
    void MouseEnter(float const* Ray) override;
    // slot 19 LAB_0059a3d0 (RET 4)
    bool MouseEvent(FWin3DMouseEvent const* Event) override;

    // FUN_0059a110 -- rebuild the content: Justify(1), RDLRef(+0x4a8), Style(0x828b),
    // TRA(+0x4c0, 0xffffff00, 0), RDLRef(+0x4b4).
    void ApplyCaptionLayout();

    unsigned char bInteractive;            // +0x498
    unsigned char Pad499[3];               // +0x499
    float LocalWidth;                      // +0x49c
    float LocalHeight;                     // +0x4a0
    int MessageArg;                        // +0x4a4
    RenderDisplayList CaptionBodyList;     // +0x4a8
    RenderDisplayList TextBodyList;        // +0x4b4
    TextRenderAttributes CaptionAttrs;     // +0x4c0
    unsigned Field4c4;                     // +0x4c4 -- never written

private:
    UiTextMsgButton(UiTextMsgButton const&) = delete;
    UiTextMsgButton& operator=(UiTextMsgButton const&) = delete;
};

static_assert(sizeof(UiTextMsgButton) == 0x4c8, "FUN_0059c580 allocates 0x4c8");
static_assert(offsetof(UiTextMsgButton, bInteractive) == 0x498, "UITextMsgButton +0x498");
static_assert(offsetof(UiTextMsgButton, MessageArg) == 0x4a4, "UITextMsgButton +0x4a4");
static_assert(offsetof(UiTextMsgButton, CaptionBodyList) == 0x4a8, "UITextMsgButton +0x4a8");
static_assert(offsetof(UiTextMsgButton, TextBodyList) == 0x4b4, "UITextMsgButton +0x4b4");
static_assert(offsetof(UiTextMsgButton, CaptionAttrs) == 0x4c0, "UITextMsgButton +0x4c0");
