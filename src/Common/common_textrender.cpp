// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// TextRenderContext, DeviceTRC (fonts, images, GDI output) and the rich-text font tables --
// split out of common_text.cpp (retail RichDisplayList.cpp) in CMN-SPLIT phase B.

#include "common_text.h"
#include "common_text_internal.h"
#include "common_misc.h" // safe_strcpy (SetStandardFont)
#include "dacom.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <map>
#include <new>
#include <string>
#include <deque>
#include <list>
#include <vector>
#include <windows.h>

// ============================================================================
// TextRenderContext — base class. Virtual device interface.
// Most getters/setters are simple field access. State stack uses linked lists.
// ============================================================================

TextRenderContext::TextRenderContext()
    : CurrentAttrs()
    , Unused20(0)
    , DefaultAttrs()
    , Unused28(0)
{
    // .text:06327380. Both lists start empty (operator new(0x28) / operator new(0x10) self-linked
    // heads, size 0 -- the std::list default constructors), then the vftable, then CurrentAttrs
    // and DefaultAttrs from DAT_0640957c, then +0x30/+0x2c/+0x38/+0x34 = 0. Retail never writes
    // +0x20 or +0x28 (left as heap/stack garbage); the port zeroes them, deterministic.
    DefaultAttrs.Packed = GDefaultTextAttribsPacked;
    CurrentAttrs.Packed = GDefaultTextAttribsPacked;
    PosH = 0;
    PosV = 0;
    OriginH = 0;
    OriginV = 0;
}

TextRenderContext::TextRenderContext(const TextRenderContext& Other)
    : StateStack(Other.StateStack)
    , AttrStack(Other.AttrStack)
    , CurrentAttrs(Other.CurrentAttrs)
    , Unused20(0)
    , DefaultAttrs(Other.DefaultAttrs)
    , Unused28(0)
{
    // .text:06324e90. New state list with Other's nodes copied in order (FUN_0632c3a0 = range
    // insert over [Other.begin, Other.end) before the new head; FUN_0632d510 is the State copy),
    // then the same for the attribute list (FUN_0632c420, element copy FUN_0630cd00), then
    // +0x1c/+0x24/+0x2c/+0x30/+0x34/+0x38. +0x20 and +0x28 are not copied (zeroed here).
    PosV = Other.PosV;
    PosH = Other.PosH;
    OriginV = Other.OriginV;
    OriginH = Other.OriginH;
}

TextRenderContext& TextRenderContext::operator=(const TextRenderContext& Other)
{
    // .text:06324f60. Retail runs VC6 list::operator= on each list (skipped when this list IS
    // Other's: CMP EBX,EDX at 06324f71 / 06324ff4): assign over the common prefix, then erase
    // this list's surplus or insert Other's remainder (FUN_0632c3a0 / FUN_0632c420) -- which
    // leaves each list equal to Other's, exactly what std::list::operator= does. Then
    // +0x1c/+0x24/+0x2c/+0x30/+0x34/+0x38; +0x20/+0x28 are left alone. There is no other
    // self-assignment test: on `a = a` both list steps are skipped and the plain copies are
    // no-ops, which std::list's own self-assignment check reproduces.
    StateStack = Other.StateStack;
    AttrStack = Other.AttrStack;
    CurrentAttrs = Other.CurrentAttrs;
    DefaultAttrs = Other.DefaultAttrs;
    PosV = Other.PosV;
    PosH = Other.PosH;
    OriginV = Other.OriginV;
    OriginH = Other.OriginH;
    return *this;
}

TextRenderContext::~TextRenderContext()
{
    // .text:06327c90 (ICF-shared with ~DeviceTRC): erase every attribute node and free its head,
    // then the same for the state list -- the two std::list destructors, in reverse declaration
    // order (AttrStack first), which is retail's order.
}

const TextRenderAttributes& TextRenderContext::get_current_attributes()
{
    // .text:06327470 (ICF-shared with CSteering::GetAxisThrottle): LEA EAX,[ECX+0x1c].
    return CurrentAttrs;
}

const TextRenderAttributes& TextRenderContext::get_default_attributes()
{
    // .text:06327480: size (+0x18) == 0 -> &DefaultAttrs (+0x24); otherwise the FRONT node's
    // value (head->_Next + 8), i.e. the most recently pushed attributes.
    if (AttrStack.size() == 0)
    {
        return DefaultAttrs;
    }
    return AttrStack.front();
}

void TextRenderContext::set_current_attributes(const TextRenderAttributes& Attrs)
{
    // Common.dll.c 0xc74a0: *(this+0x1c) = *(int*)param_1. TextRenderAttributes is a single
    // dword (Packed), so the struct assignment below is identical to retail's raw dword copy.
    CurrentAttrs = Attrs;
}

void TextRenderContext::set_default_attributes(const TextRenderAttributes& Attrs)
{
    // Common.dll.c 0xc74b0: *(this+0x24) = *(int*)param_1 (sibling of set_current_attributes).
    DefaultAttrs = Attrs;
}

void TextRenderContext::push_default_attributes()
{
    // .text:063274c0: a new 0x10-byte node is linked in before head->_Next (push_front), its
    // value copy-constructed from CurrentAttrs (+0x1c) by FUN_0630cd00, then ++size (+0x18).
    // NOTE the name: it pushes the CURRENT attributes; get_default_attributes returns them.
    AttrStack.push_front(CurrentAttrs);
}

void TextRenderContext::pop_default_attributes()
{
    // .text:06327510: unlink head->_Next, free it, --size. No empty check: on an empty stack
    // retail unlinks and frees the head node itself (heap corruption) -- retail UB that is not
    // reproduced; the port leaves an empty stack alone instead (deterministic).
    if (AttrStack.empty())
    {
        return;
    }
    AttrStack.pop_front();
}

void TextRenderContext::clear_default_attributes()
{
    // .text:06327540: from head->_Next until the head, unlink and free each node, --size per
    // node -- std::list::clear.
    AttrStack.clear();
}

void TextRenderContext::set_pos(int H, int V)
{
    // RVA 0xC4E50
    PosH = H;
    PosV = V;
}

void TextRenderContext::set_origin(int H, int V)
{
    // RVA 0xC4E70
    OriginH = H;
    OriginV = V;
}

int TextRenderContext::get_pos_h()
{
    // RVA 0xC75A0
    return PosH;
}

int TextRenderContext::get_pos_v()
{
    // RVA 0xC7580
    return PosV;
}

int TextRenderContext::get_origin_h()
{
    // RVA 0xC75E0
    return OriginH;
}

int TextRenderContext::get_origin_v()
{
    // RVA 0xC75C0
    return OriginV;
}

void TextRenderContext::set_pos_h(int H)
{
    // RVA 0xC75B0
    PosH = H;
}

void TextRenderContext::set_pos_v(int V)
{
    // RVA 0xC7590
    PosV = V;
}

void TextRenderContext::set_origin_h(int H)
{
    // RVA 0xC75F0
    OriginH = H;
}

void TextRenderContext::set_origin_v(int V)
{
    // RVA 0xC75D0
    OriginV = V;
}

void TextRenderContext::push_state()
{
    // .text:06327690: a 0x20-byte State on the stack -- +0x00 CurrentAttrs, +0x08 PosV,
    // +0x0C PosH (+0x04 never written), then get_clip_rect (vtbl+0x24) into +0x10 -- copied by
    // FUN_0632d510 into a new 0x28-byte node linked in before head->_Next (push_front), ++size.
    State Saved;
    Saved.Attributes = CurrentAttrs.Packed;
    Saved.Unused04 = 0; // retail leaves this stack dword unwritten; nothing reads it
    Saved.PosV = PosV;
    Saved.PosH = PosH;
    get_clip_rect(Saved.ClipRect);
    StateStack.push_front(Saved);
}

void TextRenderContext::pop_state()
{
    // .text:06327700: from the FRONT node: CurrentAttrs = +0x00, PosV = +0x08, PosH = +0x0C,
    // set_clip_rect (vtbl+0x20) with +0x10; then unlink and free that node, --size. No empty
    // check in retail: on an empty stack it would restore from the head node's uninitialised
    // payload and free the head itself -- retail UB that is not reproduced; the port leaves an
    // empty stack alone instead (deterministic).
    if (StateStack.empty())
    {
        return;
    }
    const State& Top = StateStack.front();
    CurrentAttrs.Packed = Top.Attributes;
    PosV = Top.PosV;
    PosH = Top.PosH;
    set_clip_rect(Top.ClipRect);
    StateStack.pop_front();
}

void TextRenderContext::reset_to_default()
{
    // .text:06327600. Retail's own leak diagnostic: when the attribute stack is not empty it
    // formats "size %d\n" with the size word (+0x18) into an 0x2000-byte stack buffer
    // (FUN_06306fe0 = _vsnprintf(Buf, 0x2000, Fmt, va), returns Buf) and hands it to
    // OutputDebugStringA. Then every attribute node is erased (the erase is the ICF-folded
    // list<...>::erase, the list at +0x10 is this object's own), and CurrentAttrs is set from
    // the front of the attribute stack if it is still non-empty -- which it never is after the
    // erase loop -- else from DefaultAttrs (+0x24). PosH / PosV are not touched.
    if (AttrStack.size() != 0)
    {
        char Buffer[0x2000];
        _snprintf(Buffer, 0x2000, "size %d\n", (int)AttrStack.size());
        OutputDebugStringA(Buffer);
    }
    AttrStack.clear();
    if (AttrStack.size() == 0)
    {
        CurrentAttrs = DefaultAttrs;
    }
    else
    {
        CurrentAttrs = AttrStack.front();
    }
}

void TextRenderContext::get_state(State& OutState)
{
    // .text:06327410: +0x00 = CurrentAttrs, +0x0C = PosH, +0x08 = PosV, then tail call
    // get_clip_rect (vtbl+0x24) on &OutState.ClipRect (+0x10).
    OutState.Attributes = CurrentAttrs.Packed;
    OutState.PosH = PosH;
    OutState.PosV = PosV;
    get_clip_rect(OutState.ClipRect);
}

void TextRenderContext::set_state(const State& InState)
{
    // .text:06327440: CurrentAttrs = +0x00, PosV = +0x08, PosH = +0x0C, then tail call
    // set_clip_rect (vtbl+0x20) with InState.ClipRect (+0x10).
    CurrentAttrs.Packed = InState.Attributes;
    PosV = InState.PosV;
    PosH = InState.PosH;
    set_clip_rect(InState.ClipRect);
}

// TextRenderContext's eleven device virtuals are pure (_purecall in .rdata:063a3de8).

// ============================================================================
// DeviceTRC — GDI-based text rendering context.
// Most methods require actual HDC/font state; stubbed where GDI calls needed.
// ============================================================================

// .data:063ed670 / .data:063ed674, both statically initialised to 2.
int DeviceTRC::mDropShadowX = 2;
int DeviceTRC::mDropShadowY = 2;

DeviceTRC::DeviceTRC()
{
    // .text:06327b40: the TextRenderContext constructor inlined, then +0x3C = 0 and the four
    // ClipRect dwords = 0.
    MHdc = nullptr;
    ClipRect.left = 0;
    ClipRect.top = 0;
    ClipRect.right = 0;
    ClipRect.bottom = 0;
}

DeviceTRC::DeviceTRC(HDC InDC)
{
    // .text:06327bf0: the TextRenderContext constructor inlined, then +0x3C = InDC. Retail
    // leaves ClipRect unwritten; the port zeroes it.
    MHdc = InDC;
    ClipRect.left = 0;
    ClipRect.top = 0;
    ClipRect.right = 0;
    ClipRect.bottom = 0;
}

DeviceTRC::DeviceTRC(const DeviceTRC& Other)
    : TextRenderContext(Other)
{
    // .text:063250f0: the TextRenderContext copy (both lists copied node by node through
    // FUN_0632c3a0 / FUN_0632c420, then +0x1c/+0x24/+0x2c..+0x38) is inlined, then +0x3c and
    // the four ClipRect dwords at +0x40.
    MHdc = Other.MHdc;
    ClipRect = Other.ClipRect;
}

DeviceTRC::~DeviceTRC()
{
    // RVA 0xC7BA0
}

DeviceTRC& DeviceTRC::operator=(const DeviceTRC& Other)
{
    // .text:06325200: TextRenderContext::operator= inlined (list assignments skipped when the
    // lists are the same object, then the plain fields), then +0x3c and +0x40..+0x4c.
    TextRenderContext::operator=(Other);
    MHdc = Other.MHdc;
    ClipRect = Other.ClipRect;
    return *this;
}

// The font table, retail ?mFont@DeviceTRC@@0PAVWinFont@1@A at .data:06401968: 32 styles x 8
// bold/italic/underline combinations of 0x7C-byte WinFont entries (0x3E0 per style; the
// retail loops walk the HFONT slots 0x064019a8..0x064095a8, exactly 32 blocks). A font index
// is (Style << 3) | Combination, one byte of TextRenderAttributes::Packed.
static const int GMaxFontIndices = 32;
static const int GSubEntriesPerFont = 8;
static_assert(sizeof(DeviceTRC::WinFont) == 0x7C, "retail WinFont stride is 0x7C");
DeviceTRC::WinFont DeviceTRC::mFont[256];

// The 32 standard styles — retail DAT_06409588, 0x8C per style. SetStandardFont writes
// +0x00 (a char* aimed at the block's own buffer), +0x04, the three flag bytes at
// +0x08..+0x0A, and the 0x80-byte name buffer at +0x0C.
struct FStandardFontStyle
{
    char const* Name;           // +0x00
    unsigned long Size;         // +0x04
    unsigned char bBold;        // +0x08
    unsigned char bItalic;      // +0x09
    unsigned char bUnderline;   // +0x0A
    unsigned char Pad0B;        // +0x0B
    char NameBuffer[0x80];      // +0x0C
};
static_assert(sizeof(FStandardFontStyle) == 0x8C, "retail style stride is 0x8C");
static FStandardFontStyle GStandardFonts[GMaxFontIndices];

// RVA 0xC5F20 (LAB_06325f20) — a DAT_063ea574 static-initializer table entry that runs at
// DLL load, before any explicit SetStandardFont call. Populates all 32 standard styles with
// retail's built-in defaults, verified index-by-index against the raw disassembly: every one
// of the 32 GStandardFonts[i] slots (base DAT_06409588, stride 0x8C, confirmed via the target
// address of each of its 21 FUN_06326250 calls plus the three manually-inlined first entries)
// gets non-bold/non-italic/non-underline defaults following a clean repeating 4-style cycle:
//   pattern = (i + 2) % 4  ->  0:"Vibrocentric" 16pt, 1:"Vibrocentric" 18pt,
//                              2:"Arial Unicode MS" 22pt, 3:"Arial Unicode MS" 26pt
// Without this, a style never touched by an explicit SetStandardFont call would be left at
// GStandardFonts[i]'s zero-initialized state (Name=nullptr, Size=0) instead of retail's font.
static struct FDefaultStandardFontsInit
{
    FDefaultStandardFontsInit()
    {
        static const char* const Names[4] = {
            "Vibrocentric", "Vibrocentric", "Arial Unicode MS", "Arial Unicode MS"
        };
        static const unsigned long Sizes[4] = { 16, 18, 22, 26 };
        for (int i = 0; i < GMaxFontIndices; ++i)
        {
            const int Pattern = (i + 2) % 4;
            FStandardFontStyle& Style = GStandardFonts[i];
            // FUN_06326250 / the three inlined copies: Name = buffer, safe_strcpy(buffer, 0x80,
            // name), Size, then the three flag bytes.
            Style.Name = Style.NameBuffer;
            safe_strcpy(Style.NameBuffer, 0x80, Names[Pattern]);
            Style.Size = Sizes[Pattern];
            Style.bBold = 0;
            Style.bItalic = 0;
            Style.bUnderline = 0;
        }
    }
} GDefaultStandardFontsInit;

// DAT_063ed608 — eight three-dword entries whose third dword packs {bold, italic, underline}
// as bytes. The eight rows are every combination, indexed bold | italic<<1 | underline<<2.
struct FRichFontCombination
{
    unsigned char bBold;
    unsigned char bItalic;
    unsigned char bUnderline;
};
static const FRichFontCombination GFontCombinations[GSubEntriesPerFont] = {
    {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1},
};

// DAT_063ed668 / DAT_063ed66c, DAT_0640a708 / DAT_0640a709.
static float GRichTextScaleX = 1.0f;
namespace CmnText
{
    float GRichTextScaleY = 1.0f; // declared in common_text_internal.h (ImageNode reads it)
}
static bool GRichTextInitialised = false;
static bool GRichTextOsAboveNt4 = false;

// LAB_063277b0 -- mFont's element constructor: REP STOSD 0xF dwords of 0 over +0x00..+0x3B
// (LogFont) and [this+0x40] = 0 (FontHandle). +0x3C and the TEXTMETRICA are not touched.
DeviceTRC::WinFont::WinFont()
{
    memset(&LogFont, 0, sizeof(LogFont));
    FontHandle = nullptr;
}

// FUN_063277d0 -- mFont's element destructor: DeleteObject(FontHandle) if set, then null it.
DeviceTRC::WinFont::~WinFont()
{
    if (FontHandle != nullptr)
    {
        DeleteObject(FontHandle);
        FontHandle = nullptr;
    }
}

// FUN_063277f0 (thiscall on the WinFont, RET 4) -- FontDesc to LOGFONTA:
//   +0x3C = Desc.Size; zero +0x00..+0x3B; lfHeight = +0x3C (the UNSCALED size -- the caller
//   rescales it, 063264b7 / 06327dbb); lfWidth/lfEscapement/lfOrientation = 0;
//   lfCharSet = 1, lfOutPrecision = 4, lfClipPrecision = 0, lfQuality = 4,
//   lfPitchAndFamily = 0; lfWeight = (-(bBold != 0) & 0x12c) + 0x190 (700 / 400);
//   strncpy(lfFaceName, Desc.Name, 0x1f) (no null guard), lfFaceName[0x1f] = 0;
//   lfItalic = bItalic != 0; lfStrikeOut = 0; lfUnderline = bUnderline != 0.
static void BuildRichTextLogFont(DeviceTRC::WinFont& Entry, TextRenderContext::FontDesc const& Desc)
{
    Entry.UnscaledSize = (int)Desc.Size;
    memset(&Entry.LogFont, 0, sizeof(Entry.LogFont));
    Entry.LogFont.lfHeight = Entry.UnscaledSize;
    Entry.LogFont.lfWidth = 0;
    Entry.LogFont.lfEscapement = 0;
    Entry.LogFont.lfOrientation = 0;
    Entry.LogFont.lfCharSet = 1;
    Entry.LogFont.lfOutPrecision = 4;
    Entry.LogFont.lfClipPrecision = 0;
    Entry.LogFont.lfQuality = 4;
    Entry.LogFont.lfPitchAndFamily = 0;
    Entry.LogFont.lfWeight = Desc.bBold != 0 ? 700 : 400;
    strncpy(Entry.LogFont.lfFaceName, Desc.Name, 0x1f);
    Entry.LogFont.lfFaceName[0x1f] = '\0';
    Entry.LogFont.lfItalic = Desc.bItalic != 0 ? 1 : 0;
    Entry.LogFont.lfStrikeOut = 0;
    Entry.LogFont.lfUnderline = Desc.bUnderline != 0 ? 1 : 0;
}

// The delete sweep InitializeRichText and ShutdownRichText both inline (06326360 / 063265d0):
// from DAT_064019a8 (mFont[0].FontHandle) in steps of 0x7C until DAT_064095a8 -- every one of
// the 256 entries -- DeleteObject the HFONT if set and null it.
static void ReleaseAllRichTextFonts(DeviceTRC::WinFont* Fonts)
{
    for (int Index = 0; Index < GMaxFontIndices * GSubEntriesPerFont; ++Index)
    {
        DeviceTRC::WinFont& Entry = Fonts[Index];
        if (Entry.FontHandle != nullptr)
        {
            DeleteObject(Entry.FontHandle);
            Entry.FontHandle = nullptr;
        }
    }
}

// One style's eight bold/italic/underline fonts. Retail inlines this loop twice, byte for
// byte: DeviceTRC::static_set_font (06327d74..06327e43) and ReinitializeRichTextFonts
// (06326470..06326542). Each pass walks the eight 0x0C-byte combination rows of
// DAT_063ed608 (until 063ed668) and builds a FontDesc on the stack whose first two dwords
// are then overwritten with Base's name and size, so only the row's third dword -- the
// bold/italic/underline bytes -- survives. Then: FUN_063277f0, DeleteObject of the old
// HFONT, lfHeight = ftol(FILD(+0x3C) * Scale), CreateFontIndirectA; on success a scratch
// DC (CreateCompatibleDC(0), not null-checked) selects the font, GetTextMetricsA into
// +0x44, restores and is deleted; if the HFONT is still null the FDUMP notice.
static void RebuildRichTextStyleFonts(DeviceTRC::WinFont* StyleFonts,
                                      TextRenderContext::FontDesc const& Base, float Scale)
{
    for (int Sub = 0; Sub < GSubEntriesPerFont; ++Sub)
    {
        TextRenderContext::FontDesc Desc = {};
        Desc.Name = Base.Name;
        Desc.Size = Base.Size;
        Desc.bBold = GFontCombinations[Sub].bBold;
        Desc.bItalic = GFontCombinations[Sub].bItalic;
        Desc.bUnderline = GFontCombinations[Sub].bUnderline;

        DeviceTRC::WinFont& Entry = StyleFonts[Sub];
        BuildRichTextLogFont(Entry, Desc);

        if (Entry.FontHandle != nullptr)
        {
            DeleteObject(Entry.FontHandle);
            Entry.FontHandle = nullptr;
        }

        // FILD dword [entry+0x3C] (signed); FMUL float Scale; ftol (truncation).
        Entry.LogFont.lfHeight = (LONG)((double)Entry.UnscaledSize * (double)Scale);

        Entry.FontHandle = CreateFontIndirectA(&Entry.LogFont);
        if (Entry.FontHandle != nullptr)
        {
            HDC TempDC = CreateCompatibleDC(nullptr);
            HGDIOBJ OldObj = SelectObject(TempDC, Entry.FontHandle);
            GetTextMetricsA(TempDC, &Entry.Metrics);
            SelectObject(TempDC, OldObj);
            DeleteDC(TempDC);
        }
        if (Entry.FontHandle == nullptr)
        {
            FDUMP(0x100003, "%s(%d) : NOTICE:General:%s",
                  "E:\\FL\\Scratch\\Source\\Common\\RichDisplayList.cpp", 0x48e,
                  "Failed to create font.\n");
        }
    }
}

void __cdecl SetStandardFont(int StyleIndex, TextRenderContext::FontDesc const& Desc)
{
    // .text:06326570: Style = DAT_06409588 + StyleIndex * 0x8c (no bounds check -- an index
    // outside 0..31 writes past the table in retail as well); safe_strcpy(Style+0xc, 0x80,
    // Desc.Name); Style.Name = Style+0xc; the three flag bytes (+8/+9/+0xa); Style.Size (+4).
    FStandardFontStyle& Style = GStandardFonts[StyleIndex];
    safe_strcpy(Style.NameBuffer, 0x80, Desc.Name);
    Style.Name = Style.NameBuffer;
    Style.bBold = Desc.bBold;
    Style.bItalic = Desc.bItalic;
    Style.bUnderline = Desc.bUnderline;
    Style.Size = Desc.Size;
}

bool __cdecl InitializeRichText(int ScreenHeight)
{
    // .text:06326340. If already initialised: release every font, clear the flag.
    if (GRichTextInitialised)
    {
        ReleaseAllRichTextFonts(DeviceTRC::mFont);
        GRichTextInitialised = false;
    }

    // FILD H; FLD 768.0f (DAT_063a3db8); FDIVR -> H/768 stored to DAT_063ed668; the H left in
    // ST0 is divided by 600.0f (DAT_063a3dbc) and stored to DAT_063ed66c. The exe passes its
    // surface height (DAT_00610854, 005b1f64).
    GRichTextScaleX = (float)((double)ScreenHeight / 768.0);
    GRichTextScaleY = (float)((double)ScreenHeight / 600.0);

    // 063263ad: FLD the stored float, FADD ST0,ST0, ftol -> both drop-shadow offsets.
    DeviceTRC::mDropShadowX = (int)((double)GRichTextScaleX + (double)GRichTextScaleX);
    DeviceTRC::mDropShadowY = DeviceTRC::mDropShadowX;

    // 063263c4: OSVERSIONINFOA on the stack (only dwOSVersionInfoSize = 0x94 set),
    // GetVersionExA; success -> DAT_0640a709 = dwMajorVersion > 4 (SETA, unsigned), failure ->
    // 0. Either way DAT_0640a708 = 1 and the result is 1. (Version-shimmed on modern Windows
    // unless the exe is manifested; any answer above NT4 is retail's answer there too.)
    OSVERSIONINFOA Version;
    memset(&Version, 0, sizeof(Version));
    Version.dwOSVersionInfoSize = sizeof(Version);
#pragma warning(push)
#pragma warning(disable : 4996)
    if (GetVersionExA(&Version) != 0)
    {
        GRichTextOsAboveNt4 = Version.dwMajorVersion > 4;
    }
    else
    {
        GRichTextOsAboveNt4 = false;
    }
#pragma warning(pop)

    GRichTextInitialised = true;
    return true;
}

void __cdecl ReinitializeRichTextFonts()
{
    // .text:06326410. For each of the 32 standard styles (DAT_06409588, 0x8C apart, paired
    // with mFont rows 0x3E0 apart until DAT_064095a8): free the style's eight fonts
    // (06326437..06326450), then rebuild them from the style's name and size at the scale
    // DAT_063ed668 (GRichTextScaleX), read once per style at 06326452.
    for (int StyleIndex = 0; StyleIndex < GMaxFontIndices; ++StyleIndex)
    {
        DeviceTRC::WinFont* const StyleFonts = &DeviceTRC::mFont[StyleIndex * GSubEntriesPerFont];
        for (int Sub = 0; Sub < GSubEntriesPerFont; ++Sub)
        {
            if (StyleFonts[Sub].FontHandle != nullptr)
            {
                DeleteObject(StyleFonts[Sub].FontHandle);
                StyleFonts[Sub].FontHandle = nullptr;
            }
        }

        FStandardFontStyle const& Style = GStandardFonts[StyleIndex];
        TextRenderContext::FontDesc Base = {};
        Base.Name = Style.Name;
        Base.Size = Style.Size;
        RebuildRichTextStyleFonts(StyleFonts, Base, GRichTextScaleX);
    }
}

void __cdecl ShutdownRichText()
{
    // .text:063265c0: release every font, clear DAT_0640a708. The OS-version flag stays.
    ReleaseAllRichTextFonts(DeviceTRC::mFont);
    GRichTextInitialised = false;
}

void __cdecl DeviceTRC::static_set_font(int Index, const FontDesc& Desc, float Scale)
{
    // .text:06327d50-06327e50. Rebuilds mFont[Index*8 .. Index*8+7] from Desc's name and size
    // and the eight flag combinations, at the caller's Scale. No bounds check, and the
    // standard-style registry (SetStandardFont's table) is not touched.
    RebuildRichTextStyleFonts(&mFont[Index * GSubEntriesPerFont], Desc, Scale);
}

static std::string GImageRoot;

void __cdecl DeviceTRC::set_image_root(const char* Path)
{
    // .text:06327e60-06327fa9: assign(Path, strlen(Path)) into the root string DAT_0640956c
    // (no null check: a null Path faults in strlen, and does here too), then append '\\'
    // unless the last character already is one. An empty Path leaves VC6's string with a null
    // buffer, so the last-character test reads address 0 + 0 - 1 (06327ef2/06327ef9) and
    // faults; that read is reproduced through a volatile pointer.
    GImageRoot = Path;
    const uintptr_t Buffer = GImageRoot.empty() ? 0 : (uintptr_t)GImageRoot.data();
    const volatile char* const Last = (const volatile char*)(Buffer + GImageRoot.size() - 1);
    if (*Last != '\\')
    {
        GImageRoot += '\\';
    }
}

void __cdecl DeviceTRC::set_drop_shadow_offset(int X, int Y)
{
    // RVA 0xC7FB0
    mDropShadowX = X;
    mDropShadowY = Y;
}

void __cdecl DeviceTRC::release_font(int Index)
{
    // .text:06327fd0 — deletes the 8 font handles of one style. No bounds check in retail.
    for (int Sub = 0; Sub < GSubEntriesPerFont; Sub++)
    {
        WinFont* Entry = &mFont[Index * GSubEntriesPerFont + Sub];
        if (Entry->FontHandle != nullptr)
        {
            DeleteObject(Entry->FontHandle);
            Entry->FontHandle = nullptr;
        }
    }
}

int DeviceTRC::get_cell_width() const
{
    // RVA 0xC8010: `MOVZX EAX, byte[this+0x1c]; IMUL EAX,EAX,0x7C; MOV EAX,[EAX+DAT_064019c4]`.
    // No bounds check in retail. mFont (32 styles x 8) is laid out row-major with the
    // 8-wide Sub dimension innermost at stride 0x7C (confirmed against
    // ReinitializeRichTextFonts' literal 8-iteration/+0x7C inner loop, Common.dll.c
    // 0x6326437-0x6326450) and the 32-wide Style dimension outermost at stride
    // 8*0x7C=0x3E0. Retail's single `FontIdx*0x7C` multiply (no added Style*0x3E0
    // term) only produces the correct address if the packed byte in
    // CurrentAttrs.Packed is itself a *flat* combined index = (Style<<3)|Sub, which
    // exactly spans a byte's full 0-255 range for 32 styles * 8 sub-entries. (An
    // earlier port revision treated the byte as a plain 0-31 style index with Sub
    // fixed at 0 and added a `>= GMaxFontIndices` guard retail doesn't have --
    // wrong: that silently returned 0 for the majority of valid combined indices.)
    // Also: DAT_064019c4 - DAT_064019ac (get_cell_height's data symbol) is 0x18
    // bytes, matching TEXTMETRICA's tmMaxCharWidth(+0x18) vs tmHeight(+0x00), not
    // tmAveCharWidth(+0x14) -- so this reads tmMaxCharWidth, not tmAveCharWidth.
    unsigned char const Combined = (unsigned char)(CurrentAttrs.Packed & 0xFF);
    return mFont[Combined].Metrics.tmMaxCharWidth;
}

int DeviceTRC::get_cell_height() const
{
    // RVA 0xC8020: same shape as get_cell_width above (DAT_064019ac, tmHeight).
    // See get_cell_width's comment for the combined-index/no-bounds-check reasoning.
    unsigned char const Combined = (unsigned char)(CurrentAttrs.Packed & 0xFF);
    return mFont[Combined].Metrics.tmHeight;
}

void DeviceTRC::get_text_visual_size(const wchar_t* Text, VisualSize& Size, int Len) const
{
    // .text:06328030-063280e1. A null Text writes only Ascent/Descent = 0 (+0x08/+0x0C) --
    // Width/Height are left as they were. Otherwise Len == -1 means wcslen; the current font
    // (mFont[low byte of CurrentAttrs].FontHandle) is selected into the DC, the extent measured
    // (failure -> Width = Height = 0), Ascent/Descent copied from the cached metrics and the
    // previous font reselected. No test of the DC.
    if (Text == nullptr)
    {
        Size.Ascent = 0;
        Size.Descent = 0;
        return;
    }
    if (Len == -1)
    {
        Len = (int)wcslen(Text);
    }

    HDC const Dc = MHdc;
    const WinFont& Entry = mFont[(unsigned char)(CurrentAttrs.Packed & 0xFF)];
    HGDIOBJ const OldFont = SelectObject(Dc, Entry.FontHandle);
    SIZE TextSize;
    if (GetTextExtentPoint32W(MHdc, Text, Len, &TextSize))
    {
        Size.Height = TextSize.cy;
        Size.Width = TextSize.cx;
    }
    else
    {
        Size.Width = 0;
        Size.Height = 0;
    }
    Size.Ascent = Entry.Metrics.tmAscent;
    Size.Descent = Entry.Metrics.tmDescent;
    SelectObject(Dc, OldFont);
}

void DeviceTRC::get_font_metrics(int& Ascent, int& Descent) const
{
    // RVA 0xC80F0. BUG FIX (2026-09-07, batch11d1): ground truth indexes the cached per-style
    // table entry directly -- byte[this+0x1c]*0x7C + DAT_064019b0/DAT_064019b4, which are
    // (DAT_064019a8's FontHandle field @ entry+0x40) + 0x08/+0x0C == Metrics(+0x44)+0x04/+0x08
    // == TEXTMETRICA::tmAscent/tmDescent -- the SAME cached-table idiom already used and
    // verified by get_cell_width/get_cell_height (tmMaxCharWidth/tmHeight) just above. It never
    // touches MHdc or issues a live GetTextMetricsW query. The port previously queried whatever
    // font happens to be currently selected on MHdc, which is wrong whenever that doesn't match
    // the current style/sub-font index (e.g. mid-call in another DeviceTRC method that
    // temporarily swaps fonts) and also silently returns 0/0 if MHdc is null, which retail can't
    // even do since it never dereferences MHdc here at all.
    unsigned char const Combined = (unsigned char)(CurrentAttrs.Packed & 0xFF);
    Ascent = mFont[Combined].Metrics.tmAscent;
    Descent = mFont[Combined].Metrics.tmDescent;
}

void DeviceTRC::get_text_fit(const wchar_t* Text, int MaxWidth, int& FitCount,
                              int*, VisualSize&, int Len) const
{
    // .text:06328120-06328235. Widths (param_4) and Size (param_5) are never touched.
    // A null Text or Len == 0 -> FitCount = 0 and nothing else (06328225). No test of the DC.
    if (Text == nullptr || Len == 0)
    {
        FitCount = 0;
        return;
    }

    // 06328140: the current font (low byte of CurrentAttrs indexes mFont) into the DC; then
    // Len == -1 means wcslen.
    HDC const Dc = MHdc;
    HGDIOBJ const OldFont = SelectObject(Dc, mFont[(unsigned char)(CurrentAttrs.Packed & 0xFF)].FontHandle);
    if (Len == -1)
    {
        Len = (int)wcslen(Text);
    }

    if (GRichTextOsAboveNt4)
    {
        // 0632817b: GetTextExtentExPointW(DC, Text, Len, MaxWidth, &FitCount, NULL, &scratch);
        // failure -> FitCount = 0.
        SIZE Extent;
        if (GetTextExtentExPointW(MHdc, Text, Len, MaxWidth, &FitCount, nullptr, &Extent) == 0)
        {
            FitCount = 0;
        }
    }
    else
    {
        // 063281b8: FitCount = 0, running total 0; per character (Len counted down to 0)
        // GetCharWidthW into a zeroed int; total + width >= MaxWidth (signed JGE) stops,
        // otherwise ++FitCount and the total advances.
        FitCount = 0;
        int TotalWidth = 0;
        int Remaining = Len;
        const wchar_t* Cur = Text;
        while (Remaining != 0)
        {
            int CharWidth = 0;
            GetCharWidthW(MHdc, *Cur, *Cur, &CharWidth);
            const int NewTotal = TotalWidth + CharWidth;
            if (NewTotal >= MaxWidth)
            {
                break;
            }
            ++FitCount;
            ++Cur;
            --Remaining;
            TotalWidth = NewTotal;
        }
    }

    SelectObject(Dc, OldFont);
}

void DeviceTRC::execute_text(const wchar_t* Text, bool bHighlight, int Len)
{
    // .text:06328240-06328367. Only a null Text returns early (no test of the DC). The current
    // font (mFont[low byte of CurrentAttrs]) is selected, Len == -1 means wcslen. With
    // bHighlight: align 0, background colour 0, TRANSPARENT, colour 0x0A0A0A, ExtTextOutW at
    // the shadow offset, then colour = the upper three bytes of CurrentAttrs (dword [+0x1d]
    // & 0xFFFFFF) and ExtTextOutW at PosH + OriginH, PosV + OriginV (ETO_CLIPPED, no rectangle).
    // Drawn or not, PosH += the measured width -- GetTextExtentPoint32W's result is not tested.
    if (Text == nullptr)
    {
        return;
    }

    HDC const Dc = MHdc;
    HGDIOBJ const OldFont = SelectObject(Dc, mFont[(unsigned char)(CurrentAttrs.Packed & 0xFF)].FontHandle);
    if (Len == -1)
    {
        Len = (int)wcslen(Text);
    }
    if (bHighlight)
    {
        SetTextAlign(MHdc, 0);
        SetBkColor(MHdc, 0);
        SetBkMode(MHdc, TRANSPARENT);
        SetTextColor(MHdc, 0x0A0A0A);
        ExtTextOutW(MHdc, PosH + OriginH + mDropShadowX, PosV + OriginV + mDropShadowY,
                    ETO_CLIPPED, nullptr, Text, Len, nullptr);
        SetTextColor(MHdc, (CurrentAttrs.Packed >> 8) & 0xFFFFFF);
        ExtTextOutW(MHdc, PosH + OriginH, PosV + OriginV, ETO_CLIPPED, nullptr, Text, Len, nullptr);
    }

    // Retail's SIZE is uninitialised stack and is added even when the call fails; the port
    // zeroes it (a failed measure advances by 0).
    SIZE TextSize = {};
    GetTextExtentPoint32W(MHdc, Text, Len, &TextSize);
    PosH += TextSize.cx;

    SelectObject(Dc, OldFont);
}


// DeviceTRC::BMPImage (RTTI .?AVBMPImage@DeviceTRC@@, vftable .rdata:063a4608, 0x18 bytes):
// the GDI bitmap behind every image DeviceTRC::create_image hands out. One virtual, the
// destructor (slot 0: scalar deleting destructor FUN_06327ad0 over the body FUN_06327af0).
class DeviceTRC::BMPImage : public TextRenderContext::Image
{
public:
    // Width (+0x04) / Height (+0x08) live in the Image base (ImageNode reads them there).
    HDC HdcSrc;       // +0x0C
    HBITMAP HBitmap;  // +0x10
    HGDIOBJ HOldBmp;  // +0x14

    // FUN_06327880 (.text:06327880-06327a95), called only from create_image (.text:063285c7).
    explicit BMPImage(const char* Filename)
    {
        // A null name: zero the five fields and stop (.text:063278c1 -> 06327a6d).
        if (Filename == nullptr)
        {
            Height = 0.0f;
            Width = 0.0f;
            HdcSrc = nullptr;
            HBitmap = nullptr;
            HOldBmp = nullptr;
            return;
        }

        // .text:063278db-0632798b. A UNC name ("\\...") or a drive-qualified one ("X:...") is
        // used as given; anything else is the image root (set_image_root keeps its trailing
        // '\\') with the name appended verbatim -- no separator is inserted here.
        std::string Path;
        const char* LoadPath = Filename;
        const bool bUnc = Filename[0] == '\\' && Filename[1] == '\\';
        if (!bUnc && Filename[1] != ':')
        {
            Path.assign(GImageRoot, 0, std::string::npos);
            Path.append(Filename);
            LoadPath = Path.c_str();
        }

        // .text:06327991-063279af: the four other fields are zeroed before the load, then
        // LoadImageA(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION).
        Height = 0.0f;
        Width = 0.0f;
        HdcSrc = nullptr;
        HOldBmp = nullptr;
        HBitmap = (HBITMAP)LoadImageA(nullptr, LoadPath, IMAGE_BITMAP, 0, 0,
                                      LR_LOADFROMFILE | LR_CREATEDIBSECTION);
        if (HBitmap == nullptr)
        {
            // .text:06327a12-06327a43: retail's own notice, formatted through FUN_06306fe0
            // (_vsnprintf into a 0x2000-byte stack buffer).
            char Buffer[0x2000];
            _snprintf(Buffer, 0x2000, "Failed to load image from \"%s\"\n", LoadPath);
            FDUMP(0x100003, "%s(%d) : NOTICE:General:%s",
                  "E:\\FL\\Scratch\\Source\\Common\\RichDisplayList.cpp", 0x43e, Buffer);
            return;
        }

        // .text:063279b4-06327a0a. The screen DC is released last, whether or not the memory
        // DC was created. GetObjectA's result is not checked: the 0x54-byte DIBSECTION is
        // zeroed first, so a failure leaves Width / Height at 0 (FILD bmWidth / bmHeight).
        HDC ScreenDC = GetDC(nullptr);
        HdcSrc = CreateCompatibleDC(ScreenDC);
        if (HdcSrc != nullptr)
        {
            DIBSECTION Dib;
            memset(&Dib, 0, sizeof(Dib));
            GetObjectA(HBitmap, sizeof(Dib), &Dib);
            Width = (float)Dib.dsBm.bmWidth;
            Height = (float)Dib.dsBm.bmHeight;
            HOldBmp = SelectObject(HdcSrc, HBitmap);
        }
        ReleaseDC(nullptr, ScreenDC);
    }

    // FUN_06327af0 (.text:06327af0-06327b3f).
    virtual ~BMPImage() override
    {
        if (HdcSrc != nullptr)
        {
            SelectObject(HdcSrc, HOldBmp);
            DeleteDC(HdcSrc);
            HdcSrc = nullptr;
            HOldBmp = nullptr;
        }
        if (HBitmap != nullptr)
        {
            DeleteObject(HBitmap);
            HBitmap = nullptr;
        }
    }
};

void DeviceTRC::execute_image(const Image* Img, bool bDraw)
{
    // .text:06328370-063283eb. With bDraw, StretchBlt the image's DC at (OriginH + PosH,
    // OriginV + PosV), destination size = ftol(ScaleY * width) x ftol(ScaleY * height) (retail
    // scales both axes by DAT_063ed66c), source = the whole ftol(width) x ftol(height) bitmap,
    // SRCCOPY. Draw or not, the cursor then advances: PosH = ftol(ScaleY * width + PosH).
    // No null checks on the image, its DC or this DC (retail has none).
    // Every image this context draws came from its own create_image.
    const BMPImage* const DevImg = (const BMPImage*)Img;
    if (bDraw)
    {
        StretchBlt(MHdc, OriginH + PosH, OriginV + PosV,
                   (int)((double)GRichTextScaleY * DevImg->Width),
                   (int)((double)GRichTextScaleY * DevImg->Height),
                   DevImg->HdcSrc, 0, 0,
                   (int)DevImg->Width, (int)DevImg->Height, SRCCOPY);
    }
    // FLD ScaleY; FMUL width; FIADD PosH; ftol.
    PosH = (int)((double)GRichTextScaleY * DevImg->Width + PosH);
}

void DeviceTRC::set_clip_rect(const RECT& NewClip)
{
    // .text:063283f0-063284c0. With a DC: an empty rectangle (bottom == top or left == right)
    // clears the clip region; otherwise a copy offset by the origin (OffsetRect(OriginH,
    // OriginV)) becomes a region that is selected (a failed select is reported) and freed.
    // A null region skips the select. Then the rectangle is cached, DC or not.
    if (MHdc != nullptr)
    {
        if (NewClip.bottom == NewClip.top || NewClip.left == NewClip.right)
        {
            SelectClipRgn(MHdc, nullptr);
        }
        else
        {
            RECT OffsetRc = NewClip;
            OffsetRect(&OffsetRc, OriginH, OriginV);
            HRGN const Hrgn = CreateRectRgnIndirect(&OffsetRc);
            if (Hrgn != nullptr)
            {
                if (SelectClipRgn(MHdc, Hrgn) == 0)
                {
                    FDUMP(0x100003, "%s(%d) : NOTICE:General:%s",
                          "E:\\FL\\Scratch\\Source\\Common\\RichDisplayList.cpp", 0x592,
                          "Failed to select clipping region into DC\n");
                }
                DeleteObject(Hrgn);
            }
        }
    }
    ClipRect = NewClip;
}

void DeviceTRC::get_clip_rect(RECT& OutRect)
{
    // .text:063284d0-06328582. With a DC: a scratch 10x10 region receives the clip region;
    // none (0) zeroes the cache, an error (-1) leaves it, otherwise its bounding box offset back
    // by -OriginH / -OriginV is cached; the region is freed. The cache is returned either way.
    if (MHdc != nullptr)
    {
        HRGN const Hrgn = CreateRectRgn(0, 0, 10, 10);
        const int Result = GetClipRgn(MHdc, Hrgn);
        if (Result == 0)
        {
            ClipRect.left = 0;
            ClipRect.top = 0;
            ClipRect.right = 0;
            ClipRect.bottom = 0;
        }
        else if (Result != -1)
        {
            RECT RgnBox;
            GetRgnBox(Hrgn, &RgnBox);
            OffsetRect(&RgnBox, -OriginH, -OriginV);
            ClipRect = RgnBox;
        }
        DeleteObject(Hrgn);
    }
    OutRect = ClipRect;
}



TextRenderContext::Image* DeviceTRC::create_image(const char* const Filename)
{
    // .text:06328590-063285ed: operator new(0x18), null -> return 0, else BMPImage(Filename)
    // (FUN_06327880). The object is returned whether or not the bitmap loaded (a failed load
    // leaves an empty image).
    return new (std::nothrow) BMPImage(Filename);
}

void DeviceTRC::destroy_image(Image* Img)
{
    // .text:063285f0-06328604: if non-null, the virtual scalar deleting destructor with flag 1.
    if (Img != nullptr)
    {
        delete Img;
    }
}

void DeviceTRC::bind_to_dc(HDC InDC)
{
    // Common.dll.c 0x466b0: *(this+0x3c) = InDC.
    MHdc = InDC;
}
