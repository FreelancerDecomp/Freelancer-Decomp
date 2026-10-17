// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail FLImeInputBox (RTTI .?AUFLImeInputBox@@ : IMEInputBox : CInputBox), vftable 0x005e2250,
// operator new(0xa0). A RichTextEditWin builds one at +0x52c while an IMEFocus singleton exists
// (FUN_0057b8d0 / FUN_0057be30 / FUN_0057bff0) and routes keys and characters through it; the
// box writes the composed text back into its owner (slot 6) and draws the composition through
// two Font::Strings.
//
// Slots (0x005e2250): 0 FUN_00564260 (scalar deleting, dtor FUN_005641f0), 1 / 2 IMEInputBox's
// OnSetFocus / OnKillFocus, 3 LAB_005643c0 Resize, 4 / 5 CInputBox's HandleKey / CharFilter,
// 6 LAB_00564390 UpdatedText, 7 IMEInputBox::ToggleInsertMode, 8 CInputBox::MoveCaret,
// 9 IMEInputBox::CalcCaretPos, 10 CInputBox::RecalcCaretPos, 11 LAB_0053e4a0 Draw (RET 4),
// 12 FUN_005643f0 DrawString, 13 IMEInputBox::ImeShowToolbar.
//
// Layout after IMEInputBox (0x94): +0x94 owner RichTextEditWin*, +0x98 Font::String (the text
// DrawString paints), +0x9c Font::String (the composed text, measured for the X offset).

#include "imeui.h"

class FontStringChild;
class RichTextEditWin;

struct FLImeInputBox : public IMEInputBox
{
public:
    // FUN_00564280 (RET 4)
    explicit FLImeInputBox(RichTextEditWin* InOwner);
    // FUN_005641f0
    ~FLImeInputBox() override;

    // slot 3 LAB_005643c0 (RET 4) -- IMEInputBox::Resize with {0, 0, right, bottom}.
    void Resize(const RECT* pRect) override;
    // slot 6 LAB_00564390 (RET 4) -- the owner's SetText(Text, 0), then +0x9c SetText(Text).
    void UpdatedText(const unsigned short* pText) override;
    // slot 11 LAB_0053e4a0 -- RET 4.
    void Draw(void* pContext) override;
    // slot 12 FUN_005643f0 (RET 0x10)
    void DrawString(int X, int Y, unsigned Color, const wchar_t* Text) override;

    RichTextEditWin* Owner;             // +0x94
    FontStringChild* TextString;        // +0x98
    FontStringChild* CompositionString; // +0x9c

private:
    FLImeInputBox(FLImeInputBox const&) = delete;
    FLImeInputBox& operator=(FLImeInputBox const&) = delete;
};
