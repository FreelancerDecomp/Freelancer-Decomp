// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "fl_ime_input_box.h"

#include "client_ime_focus.h"   // ClientConfirmPlayerFont, ClientIme*Key, ClientImeSwapFontMaterialKey
#include "font.h"               // IFont::GetLogFont
#include "font_atlas.h"         // FontAtlasSetDrawColor (FUN_00417780)
#include "font_string.h"        // FontStringChild, FFontStringDrawOrigin
#include "rich_text_win.h"      // RichTextEditWin::SetText
#include "screen_quad_state.h"  // ScreenQuadFlush (FUN_00403ea0)

#include "DALib.h"              // DALib::Pipeline

FLImeInputBox::FLImeInputBox(RichTextEditWin* InOwner)
    // FUN_00564280: DAT_0067a4a0 fetched on first use, its slot 4 (+0x10) LOGFONTA handed to
    // IMEInputBox's constructor (0x005642c2).
    : IMEInputBox(ClientConfirmPlayerFont()->GetLogFont())
    , Owner(InOwner)
    , TextString(nullptr)
    , CompositionString(nullptr)
{
    // 0x005642f4..0x00564330: +0x98 = FUN_00417620, the font bound (slot 2) and slot 8(0).
    TextString = new FontStringChild();
    TextString->BindFont(ClientConfirmPlayerFont());
    TextString->SetShadow(0);
    // 0x00564333..0x0056436f: the same for +0x9c.
    CompositionString = new FontStringChild();
    CompositionString->BindFont(ClientConfirmPlayerFont());
    CompositionString->SetShadow(0);
}

FLImeInputBox::~FLImeInputBox()
{
    // FUN_005641f0: slot 1 (Release) on +0x98 and +0x9c when set, then ~IMEInputBox.
    if (TextString != nullptr)
        TextString->Release();
    if (CompositionString != nullptr)
        CompositionString->Release();
}

void FLImeInputBox::Resize(const RECT* pRect)
{
    // LAB_005643c0: a local {0, 0, right (+8), bottom (+0xc)} to IMEInputBox::Resize.
    RECT Local;
    Local.left = 0;
    Local.top = 0;
    Local.right = pRect->right;
    Local.bottom = pRect->bottom;
    IMEInputBox::Resize(&Local);
}

void FLImeInputBox::UpdatedText(const unsigned short* pText)
{
    // LAB_00564390: FUN_0057c0e0(Text, 0) on the owner, then slot 5 (SetText) on +0x9c.
    wchar_t const* const Text = reinterpret_cast<wchar_t const*>(pText);
    Owner->SetText(Text, nullptr);
    CompositionString->SetText(Text);
}

void FLImeInputBox::Draw(void* /*pContext*/)
{
    // LAB_0053e4a0: RET 4.
}

void FLImeInputBox::DrawString(int X, int Y, unsigned Color, const wchar_t* Text)
{
    // FUN_005643f0 (RET 0x10).
    // 0x005643f3..0x0056440f: the font material swapped to DAT_0067a484.
    unsigned PreviousKey = 0;
    ClientImeSwapFontMaterialKey(&PreviousKey, static_cast<unsigned>(ClientImeTextKey()));

    // 0x00564407..0x0056441e: the composed text measured (slot 6); its width comes off X.
    int Size[2];
    CompositionString->GetSize(Size);
    int const Left = X - Size[0];

    // 0x00564422..0x00564448: the draw colour, Color's bytes reordered {2, 1, 0, 3} into the
    // record FUN_00417780 reads (0xAARRGGBB -> R, G, B, A in memory).
    unsigned char Rgba[4];
    Rgba[0] = static_cast<unsigned char>((Color >> 16) & 0xffu);
    Rgba[1] = static_cast<unsigned char>((Color >> 8) & 0xffu);
    Rgba[2] = static_cast<unsigned char>(Color & 0xffu);
    Rgba[3] = static_cast<unsigned char>((Color >> 24) & 0xffu);
    unsigned Packed = 0;
    Packed |= static_cast<unsigned>(Rgba[0]);
    Packed |= static_cast<unsigned>(Rgba[1]) << 8;
    Packed |= static_cast<unsigned>(Rgba[2]) << 16;
    Packed |= static_cast<unsigned>(Rgba[3]) << 24;
    FontAtlasSetDrawColor(Packed);

    // 0x0056444d..0x0056445d: +0x98 takes the text (slot 5).
    TextString->SetText(Text);

    // 0x00564460..0x0056449f: X = margin X + Left and Y = margin Y + Y, each summed as a dword
    // and converted as an UNSIGNED 32-bit value (FILD qword with a zero high half); then slot 9
    // (Paint) with (origin, 0, 0). Retail's origin record is a stack run whose tail holds the
    // measured size and other residue; the port's Paint reads X and Y only, so the rest is zero.
    unsigned const SumX = static_cast<unsigned>(m_dwMarginX) + static_cast<unsigned>(Left);
    unsigned const SumY = static_cast<unsigned>(m_dwMarginY) + static_cast<unsigned>(Y);
    FFontStringDrawOrigin Origin;
    Origin.X = static_cast<float>(static_cast<double>(SumX));
    Origin.Y = static_cast<float>(static_cast<double>(SumY));
    Origin.Scale = 0.0f;
    Origin.Height = 0.0f;
    Origin.Field10 = 0;
    Origin.Field14 = 0;
    TextString->Paint(&Origin, nullptr, nullptr);

    // 0x005644a2..0x005644d4: the previous material key back, then FUN_00403ea0 for the
    // DAT_0067a480 and DAT_0067a484 lists of the screen-quad container map.
    unsigned Discard = 0;
    ClientImeSwapFontMaterialKey(&Discard, PreviousKey);
    ScreenQuadFlush(DALib::Pipeline, ClientImeBackgroundKey());   // FUN_00403ea0(.., DAT_0067a480)
    ScreenQuadFlush(DALib::Pipeline, ClientImeTextKey());         // FUN_00403ea0(.., DAT_0067a484)
}
