// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ui_text_edit.h"

#include "ui_page_desc.h"
#include "win_factory.h"

UiTextEdit::UiTextEdit()
    : LocalWidth(0.0f)
    , LocalHeight(0.0f)
{
    // FUN_0059c6f0 inline: RichTextEditWin ctor FUN_0057b8d0, then the three UITextEdit
    // vftables. UITextEdit writes no field of its own; the zeros are only C++ initialisation.
}

UiTextEdit::~UiTextEdit()
{
    // FUN_0059cf50 -> the RichTextEditWin dtor.
}

bool UiTextEdit::Create(Win3D* NewParent, void* Data)
{
    // FUN_0059a410 (RET 8): RichTextWin's Create (FUN_0057b5a0) called directly.
    bool const bResult = RichTextWin::Create(NewParent, nullptr);
    // 0x0059a424..0x0059a432: FUN_0057bff0, then FUN_0057c0e0(u"TextEdit" 0x005e57ec, 0).
    ClearText();
    SetText(L"TextEdit", nullptr);

    if (Data != nullptr)
    {
        FUiPayloadText const* const Payload = static_cast<FUiPayloadText const*>(Data);
        LocalWidth = Payload->Right;                                        // +0x530
        NormWidth = Payload->Right;                                         // +0x330
        LocalHeight = Payload->Bottom;                                      // +0x534
        NormHeight = Payload->Bottom;                                       // +0x334
        // 0x0059a45f..0x0059a46c: FUN_0057bff0, then FUN_0057c0e0(payload +8, 0) -- retail does
        // not test the caption pointer.
        ClearText();
        SetText(Payload->CaptionUtf16, nullptr);
    }

    SetEditable(false);                                                     // FUN_0057bfa0(0)
    return bResult;
}

// Retail DAT_0067db18 -- WINFACTORY<UITextEdit> static (LAB_0059c240 -> FUN_0059c680; Create
// FUN_0059c6f0); atexit dtor FUN_00579380.
WINFACTORY<UiTextEdit> G_UITextEditFactory("UITextEdit");
