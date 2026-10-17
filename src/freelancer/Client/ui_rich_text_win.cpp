// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ui_rich_text_win.h"

#include "ui_page_desc.h"
#include "win_factory.h"

UiRichTextWin::UiRichTextWin()
{
    // FUN_0059cbd0 inline: FUN_0057b1b0 (RichTextWin ctor), then vft 0x005e58d4 and the
    // +0x7c face 0x005e58c8. No UIRichTextWin fields of its own.
}

UiRichTextWin::~UiRichTextWin()
{
    // FUN_0059bf80: vftables back to UIRichTextWin, JMP FUN_0057b260 (RichTextWin dtor).
}

bool UiRichTextWin::Create(Win3D* NewParent, void* Data)
{
    // FUN_0059bfa0 (RET 8): AL = FUN_0057b5a0(Parent, 0) (RichTextWin slot 9, no payload);
    // if (AL && Payload) { +0x330 = Payload[0]; +0x334 = Payload[1]; } return AL.
    bool const bCreated = RichTextWin::Create(NewParent, nullptr);
    if (bCreated && Data != nullptr)
    {
        FUiPayloadBounds const* const Bounds = static_cast<FUiPayloadBounds const*>(Data);
        NormWidth = Bounds->Right;
        NormHeight = Bounds->Bottom;
    }
    return bCreated;
}

// Retail DAT_0067da68 -- WINFACTORY<UIRichTextWin> static (LAB_0059c300 -> FUN_0059cb60; Create
// FUN_0059cbd0); atexit dtor FUN_00579380.
WINFACTORY<UiRichTextWin> G_UIRichTextWinFactory("UIRichTextWin");
