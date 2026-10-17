// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// IFont / FontImp -- retail Font.cpp (`.?AUIFont@@` / `.?AUFontImp@@`).
// Exe-local plain C++ vtable (not DACOM). Font::String holds IFont* at +0x1c.
// IFont's own vtable is .rdata:005c8e1c (every slot _purecall LAB_005b8064); FontImp's is
// .rdata:005c8dec (installed by the ctor FUN_00417d50 at 0x00417e11, the copy ctor FUN_00418cd0
// at 0x00418eea and the dtor FUN_00416490 at 0x004164a0). The only implementation is FontImp
// (src/freelancer/Client/font_imp.h).
//
// Every slot that touches the face first re-derives the pixel height when the layout generation
// DAT_006105b0 moved (the autoscale prelude, see FontImp::RefreshScale). Glyph metrics come from
// GDI on the font's own memory DC (+0xd8).

struct tagLOGFONTA;
struct HFONT__;

class IFont
{
public:
    // +0x00 LAB_00416550 -- `LEA EAX,[ECX+0x4c]`: the id string's text (FontImp::Id, "%s,%d"
    // written by FUN_00415a80). Caller not identified.
    virtual char const* Slot00() = 0;
    // +0x04 LAB_00416560 -- `LEA EAX,[ECX+0x8]`: the nickname (FUN_004177b0 matches it).
    virtual char const* GetName() = 0;
    // +0x08 LAB_00416ad0 -- `FLD [ECX+0xd0]`: fixed_height.
    virtual float GetFixedHeight() = 0;
    // +0x0c LAB_00416570 -- the autoscale prelude, then the realized HFONT (+0x118). The glyph
    // atlas rasterizer selects it into its scratch DC (FUN_004127d0, 0x00412a0c).
    virtual HFONT__* Slot03() = 0;
    // +0x10 LAB_00416820 -- the autoscale prelude, then &this+0xdc, the LOGFONTA the face was
    // built from (`LEA EAX,[EBP+0xdc]` at 0x00416abe). FLImeInputBox (FUN_00564280, 0x005642bc)
    // hands it to IMEInputBox's constructor.
    virtual tagLOGFONTA const* GetLogFont() = 0;
    // +0x14 LAB_00416dc0 -- the ratio-target autoscale prelude, then GetCharWidthW of one
    // character as a float (FUN_00418180 word-wrap). Named GetGlyphAdvance: Windows.h maps
    // GetCharWidth to GetCharWidthA/W.
    virtual float GetGlyphAdvance(wchar_t Ch) = 0;
    // +0x18 LAB_00416af0 -- the autoscale prelude, then the realized TEXTMETRIC height (+0x120)
    // or, with no HFONT (+0x118), Slot07. ListBox::Update (FUN_005a2230) divides the cursor
    // offset by it to find the hovered row.
    virtual int GetLineHeight() = 0;
    // +0x1c LAB_00416ae0 -- `MOV EAX,[ECX+0xd4]`: the tmHeight FUN_00415a80 snapshots at init.
    virtual int Slot07() = 0;
    // +0x20 LAB_00415f60 -- measure a string: Out[0] = width, Out[1] = height.
    virtual void MeasureString(int* OutWH, wchar_t const* Text, int Len) = 0;
    // +0x24 LAB_004172b0 (RET 0xc) -- the ratio-target autoscale prelude, then the number of
    // leading characters whose summed GetCharWidthW stays within MaxWidth (Len == -1: wcslen).
    virtual int ProbeString(int MaxWidth, wchar_t const* Text, int Len) = 0;
    // +0x28 LAB_004170e0 (RET 0xc) -- the ratio-target autoscale prelude, then GetCharWidthW for
    // each of Len characters (Len == -1: wcslen) into OutWidths[i]; returns the sum. Callers: the
    // NumberString face build (FUN_00414070 0x004141ed, LAB_00415090 0x00415220) with the glyph
    // string's capacity as Len.
    virtual int GetCharWidths(wchar_t const* Text, int Len, int* OutWidths) = 0;
};

static_assert(sizeof(void*) == 4, "IFont is 32-bit exe");
