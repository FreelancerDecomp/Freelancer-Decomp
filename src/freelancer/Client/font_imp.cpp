// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "font_imp.h"

#include "common_ini.h"
#include "common_misc.h" // GetFileSysPath
#include "dacom.h"
#include "font_atlas.h"
#include "font_string.h"
#include "globals.h"
#include "widescreen.h"   // port-only (FREELANCER_WIDESCREEN_SUPPORT)
#include "screen_quad_state.h"

#include <cmath>
#include <cstring>
#include <cwchar>
#include <list>
#include <stdarg.h>
#include <stdio.h>

namespace
{
char const kFontCpp[] = "E:\\FL\\Scratch\\Source\\Client\\cmn\\Font.cpp";

// .rdata:005c8d38 -- the calibration pangram every autoscale search measures (copied onto the
// stack with REP MOVSD ECX=0x1b, i.e. 53 characters and the terminator).
wchar_t const kPangram[] = L"In the beginning the Earth was without form and void.";

// .rdata:005c8d30 -- the reference width factor (1/800) of the A-variant target.
float const kPangramWidthScale = 0.00125f;

// DAT_0061651c / DAT_00616520 -- the font list (std::list<FontImp>, node 0x178 = links + 0x170).
// Built by the XC thunk LAB_004163e0 (head node, size 0), torn down by its atexit thunk
// LAB_00416420, which runs FUN_00416490 on every node left.
std::list<FontImp> G_FontList;

// DAT_006105b4 -- "measure the whole string in one call" (PE init 1). The first MeasureString
// settles it from the OS version: set when dwMajorVersion > 4, cleared when GetVersionExA fails.
bool G_bFontWholeStringExtent = true;
// DAT_0061669b -- once-guard of that probe (BSS 0), set before GetVersionExA is called.
bool G_bFontOsVersionProbed = false;

// FUN_004183c0 (cdecl, varargs): TString<64>::format -- vsprintf into a 0x40-byte stack buffer,
// then the TString<64> assignment (strlen clamped to 0x3f, copy, terminator, length).
void FontIdFormat(TString<64>* Dst, char const* Fmt, ...)
{
    char Buffer[0x40];
    va_list Args;
    va_start(Args, Fmt);
#pragma warning(push)
#pragma warning(disable : 4996) // vsprintf is deprecated; retail calls it here.
    vsprintf(Buffer, Fmt, Args);
#pragma warning(pop)
    va_end(Args);
    *Dst = Buffer;
}
} // namespace

FontImp::FontImp()
    : FixedHeight(0.0f)
    , InitLineHeight(0)
    , Dc(nullptr)
    , Font(nullptr)
    , RealizedHeight(0)
    , LayoutGeneration(0)
{
    // FUN_00417d50: vptr of IFont (005c8e1c), the three TString<64> emptied (+0x04/+0x08,
    // +0x48/+0x4c, +0x8c/+0x90), the tree at +0x15c built empty, then +0x118, +0x11c, +0xd8,
    // +0xd0, +0xd4 and +0x158 zeroed and the FontImp vptr (005c8dec) installed.
    Name.Length = 0;
    Name.Data[0] = '\0';
    Id.Length = 0;
    Id.Data[0] = '\0';
    Face.Length = 0;
    Face.Data[0] = '\0';
}

FontImp::~FontImp()
{
    // FUN_00416490: the handles are only forgotten -- no DeleteObject / DeleteDC.
    if (Font != nullptr)
    {
        Font = nullptr; // 004164a8
    }
    if (Dc != nullptr)
    {
        Dc = nullptr; // 004164b6
    }
    LayoutGeneration = 0; // 004164c2
    // 004164bc..0041653e: the tree at +0x15c is cleared and its head/nil nodes freed (the
    // std::map member's destructor).
}

void FontImp::SetName(char const* Nickname)
{
    // FUN_00417ca0
    if (Id.Length == 0)
    {
        Id = Nickname; // 00417cb1..00417cf2
    }
    Name = Nickname; // 00417cf5..00417d3d
}

bool FontImp::Realize(int Height)
{
    // FUN_00415ea0
    if (Font != nullptr)
    {
        if (RealizedHeight == Height || Height == Metrics.tmHeight)
        {
            return true; // 00415eb8 / 00415ec0
        }
        DeleteObject(Font);
        Font = nullptr;
    }
    LogFont.lfHeight = Height; // 00415eda
    Font = CreateFontIndirectA(&LogFont);
    if (Font == nullptr)
    {
        // 00415f26: FDUMP(0x100001, ..., Font.cpp, 0x9ae, name +0x8, height).
        FDUMP(0x100001, "%s(%d) : *** ERROR: failed to create font %s height=%d\n", kFontCpp, 0x9ae,
              Name.Data, Height);
        return false;
    }
    RealizedHeight = Height; // 00415ef4
    SelectObject(Dc, Font);
    GetTextMetricsA(Dc, &Metrics);
    LayoutGeneration = G_FontLayoutGeneration; // 00415f19
    return true;
}

double FontImp::MeasurePangramRatio(int Height, int Width)
{
    // FUN_00415e40: realize, measure through the vtable (+0x20), then FILD qword {width, 0}
    // (the width read unsigned) FIDIV Width.
    Realize(Height);
    int Extent[2];
    MeasureString(Extent, kPangram, -1);
    return static_cast<double>(static_cast<unsigned>(Extent[0])) / static_cast<double>(Width);
}

int FontImp::SearchScaleDelta(float Fixed, int ScreenW, int ScreenH, float Target)
{
    // FUN_00416f50
    // 00416f56: FILD ScreenH, FMUL Fixed, FADD 0.5, floor, ftol.
    int const StartHeight = static_cast<int>(
        std::floor(static_cast<double>(ScreenH) * static_cast<double>(Fixed) + 0.5));
    int Delta = 0;
    Realize(StartHeight);
    int Extent[2];
    MeasureString(Extent, kPangram, -1);
    // 00416fb5: the width becomes a float once, every ratio divides by it.
    float const WidthF = static_cast<float>(ScreenW);
    double Ratio = static_cast<double>(static_cast<unsigned>(Extent[0])) / WidthF;
    // 00416fd0..00416fea: -1, or 1 when the ratio is not above the target (TEST AH,0x41; JZ
    // keeps -1 only for "greater").
    int Step = -1;
    if (!(Ratio > Target))
    {
        Step = 1;
    }
    // 00416ff2..0041700a: nothing to do while |ratio - target| <= 0.001 (DAT_005c75f8, double).
    if (std::fabs(Ratio - Target) > 0.001)
    {
        for (;;)
        {
            // 00417010: vtable +0x18, compared unsigned (JA) with the current height.
            if (static_cast<unsigned>(GetLineHeight()) > static_cast<unsigned>(StartHeight + Delta))
            {
                break;
            }
            Realize(StartHeight + Step + Delta);
            MeasureString(Extent, kPangram, -1);
            Ratio = static_cast<double>(static_cast<unsigned>(Extent[0])) / WidthF;
            if (Step > 0)
            {
                // 0041707e: growing -- stop before taking a step that overshoots.
                if (Ratio > Target)
                {
                    break;
                }
                Delta += Step;
            }
            else
            {
                // 00417087: shrinking -- take the step, stop once at or below the target (JNP) or
                // at a height of 4 or less.
                Delta += Step;
                if (Ratio <= Target)
                {
                    break;
                }
                if (StartHeight + Delta <= 4)
                {
                    break;
                }
            }
            // 00417099..004170b1: loop only while |ratio - target| > 0.001.
            if (!(std::fabs(Ratio - Target) > 0.001))
            {
                break;
            }
        }
    }
    return Delta;
}

namespace
{
    // DAT_00610850 as both scale-delta searches read it. Port-only under
    // FREELANCER_WIDESCREEN_SUPPORT: the width a 4:3 surface of this height would have, so the
    // pangram is fitted to the same share of the (Hor+, height-scaled) interface as on 4:3 --
    // the full width would grow every font by the Hor+ factor. It is also the cache key, which
    // then tells 1280x720 from 1280x960.
    int FontSurfaceWidth()
    {
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        return static_cast<int>(std::floor(WidescreenEquivalentSurfaceWidth() + 0.5));
#else
        return static_cast<int>(G_RenderSurfaceWidth);
#endif
    }
}

int FontImp::ComputeScaleDelta()
{
    // FUN_00416110
    // 0041611a: fixed_height == 0 -> 0 (TEST AH,0x44; JNP is taken on "equal").
    if (FixedHeight == 0.0f)
    {
        return 0;
    }
    // 00416136..00416179: lower_bound on the surface width DAT_00610850, a hit only when the key
    // is not above it -- find.
    int const SurfaceWidth = FontSurfaceWidth();
    std::map<int, int>::iterator const Hit = ScaleDeltaByWidth.find(SurfaceWidth);
    if (Hit != ScaleDeltaByWidth.end())
    {
        return Hit->second; // 0041617b
    }
    bool const bHadFont = Font != nullptr;  // 004161a9
    int const OldHeight = RealizedHeight;   // 004161b1
    // 00416188..004161f2: realize at round(fixed * 600) (DAT_005c8da4) and measure.
    Realize(static_cast<int>(std::floor(static_cast<double>(FixedHeight) * 600.0 + 0.5)));
    int Extent[2];
    MeasureString(Extent, kPangram, -1);
    // 0041620a..0041621f: FILD qword {width, 0} FMUL 0.00125f, stored as a float.
    float const Target = static_cast<float>(static_cast<double>(static_cast<unsigned>(Extent[0])) *
                                            static_cast<double>(kPangramWidthScale));
    // 00416223..0041637b: the height search, the same body as FUN_00416f50 inlined with the
    // surface size read from DAT_00610850 / DAT_00610854.
    int const Delta = SearchScaleDelta(FixedHeight, SurfaceWidth,
                                       static_cast<int>(G_RenderSurfaceHeight), Target);
    // 0041637d..004163ae: insert {DAT_00610850, 0} (FUN_00402790), then the node's value =
    // delta, whether or not the insert found the key.
    ScaleDeltaByWidth[SurfaceWidth] = Delta;
    // 004163a4: put the previous face back when there was one.
    if (bHadFont)
    {
        Realize(OldHeight);
    }
    return Delta;
}

int FontImp::ComputeScaleDeltaByRatio()
{
    // The prelude body of LAB_00416dc0 (00416df9..00416eca), LAB_004170e0 (00417119..004171ea)
    // and LAB_004172b0 (004172e9..004173ba).
    if (FixedHeight == 0.0f)
    {
        return 0;
    }
    // FUN_00418b30 (find) on DAT_00610850.
    int const SurfaceWidth = FontSurfaceWidth();
    std::map<int, int>::iterator const Hit = ScaleDeltaByWidth.find(SurfaceWidth);
    if (Hit != ScaleDeltaByWidth.end())
    {
        return Hit->second;
    }
    bool const bHadFont = Font != nullptr;
    int const OldHeight = RealizedHeight;
    // FUN_00415e40(round(fixed * 600), 0x320), FSTP float.
    float const Target = static_cast<float>(MeasurePangramRatio(
        static_cast<int>(std::floor(static_cast<double>(FixedHeight) * 600.0 + 0.5)), 0x320));
    // FUN_00416f50(fixed_height, DAT_00610850, DAT_00610854, target).
    int const Delta = SearchScaleDelta(FixedHeight, SurfaceWidth,
                                       static_cast<int>(G_RenderSurfaceHeight), Target);
    // FUN_00418430 (operator[]) on DAT_00610850, then the value = delta.
    ScaleDeltaByWidth[SurfaceWidth] = Delta;
    if (bHadFont)
    {
        Realize(OldHeight);
    }
    return Delta;
}

void FontImp::RefreshScale(bool bRatioTarget)
{
    // The prelude of every face slot (e.g. LAB_00416570 00416570..0041680d).
    if (LayoutGeneration == G_FontLayoutGeneration)
    {
        return;
    }
    LayoutGeneration = G_FontLayoutGeneration;
    // vtable +0x08 FIMUL DAT_00610854, FSTP float.
    float const Base = static_cast<float>(static_cast<double>(GetFixedHeight()) *
                                          static_cast<double>(static_cast<int>(G_RenderSurfaceHeight)));
    int const Delta = bRatioTarget ? ComputeScaleDeltaByRatio() : ComputeScaleDelta();
    // FILD delta, FADD base, FADD 0.5, floor, ftol.
    Realize(static_cast<int>(
        std::floor(static_cast<double>(Delta) + static_cast<double>(Base) + 0.5)));
}

bool FontImp::Init(FFontDesc const& Desc)
{
    // FUN_00415a80
    // 00415a8b..00415ab3: the previous handles are only forgotten; the generation is zeroed.
    if (Font != nullptr)
    {
        Font = nullptr;
    }
    if (Dc != nullptr)
    {
        Dc = nullptr;
    }
    LayoutGeneration = 0;
    // 00415ab9..00415acc: FUN_004183c0(&Id, "%s,%d", Desc.Face) -- retail pushes three
    // arguments only, so the %d reads the next stack slot (the caller's saved EDI). The call is
    // kept exactly as retail makes it.
    FontIdFormat(&Id, "%s,%d", Desc.Face);
    // 00415aca..00415b20: the TString<64> assignment of the face (null -> empty).
    Face = Desc.Face;
    // 00415b22..00415bc1: the LOGFONTA.
    memset(&LogFont, 0, sizeof(LogFont));
    LogFont.lfWidth = 0;
    LogFont.lfEscapement = 0;
    LogFont.lfOrientation = 0;
    LogFont.lfWeight = Desc.bBold ? 700 : 400; // NEG / SBB / AND 0x12c / ADD 0x190
    LogFont.lfCharSet = 1;                     // DEFAULT_CHARSET
    LogFont.lfOutPrecision = 4;                // OUT_TT_PRECIS
    LogFont.lfClipPrecision = 0;
    LogFont.lfQuality = 4;                     // ANTIALIASED_QUALITY
    LogFont.lfPitchAndFamily = 0;
    strncpy(LogFont.lfFaceName, Face.Data, 0x1f);
    LogFont.lfFaceName[0x1f] = '\0';
    LogFont.lfItalic = Desc.bItalic ? 1 : 0;
    LogFont.lfUnderline = Desc.bUnderline ? 1 : 0;
    LogFont.lfStrikeOut = 0;
    // 00415bb9..00415bd1
    if (Dc == nullptr)
    {
        Dc = CreateCompatibleDC(nullptr);
    }
    FixedHeight = Desc.FixedHeight; // 00415be0
    // 00415be6..00415e1c: the autoscale prelude inlined (the generation is 0 here and
    // DAT_006105b0 never is, so it always runs).
    RefreshScale(false);
    InitLineHeight = Metrics.tmHeight; // 00415e29
    return true;
}

char const* FontImp::Slot00()
{
    // LAB_00416550 -- LEA EAX,[ECX+0x4c]
    return Id.Data;
}

char const* FontImp::GetName()
{
    // LAB_00416560 -- LEA EAX,[ECX+0x8]
    return Name.Data;
}

float FontImp::GetFixedHeight()
{
    // LAB_00416ad0 -- FLD [ECX+0xd0]
    return FixedHeight;
}

HFONT__* FontImp::Slot03()
{
    // LAB_00416570
    RefreshScale(false);
    return Font; // 0041680e
}

LOGFONTA const* FontImp::GetLogFont()
{
    // LAB_00416820
    RefreshScale(false);
    return &LogFont; // 00416abe
}

float FontImp::GetGlyphAdvance(wchar_t Ch)
{
    // LAB_00416dc0
    RefreshScale(true);
    // 00416ef9..00416f3c: select the face, GetCharWidthW (FUN_005b7445) into a zeroed int whose
    // result is not checked, restore the previous object.
    HDC const SelectedDc = Dc;
    HGDIOBJ const Previous = SelectObject(SelectedDc, Font);
    UINT const Glyph = static_cast<unsigned short>(Ch);
    INT Width = 0;
    GetCharWidthW(Dc, Glyph, Glyph, &Width);
    float const Advance = static_cast<float>(Width);
    SelectObject(SelectedDc, Previous);
    return Advance;
}

int FontImp::GetLineHeight()
{
    // LAB_00416af0
    RefreshScale(false);
    if (Font != nullptr)
    {
        return Metrics.tmHeight; // 00416d98
    }
    return Slot07(); // 00416dab, vtable +0x1c
}

int FontImp::Slot07()
{
    // LAB_00416ae0 -- MOV EAX,[ECX+0xd4]
    return InitLineHeight;
}

void FontImp::MeasureString(int* OutWH, wchar_t const* Text, int Len)
{
    // LAB_00415f60 (vtable slot 8). OutWH is the SIZE the caller passes by pointer; retail
    // writes both halves on every path.

    // 00415f6f..00415fbf: the autoscale prelude, with the out-of-line FUN_00416110.
    RefreshScale(false);

    int Width = 0;
    int Height = 0;
    // 00415fc4: a null string or a zero length measures as 0 x 0 without touching the DC.
    if (Text != nullptr && Len != 0)
    {
        HDC const SelectedDc = Dc;                                // +0xd8
        HGDIOBJ const Previous = SelectObject(SelectedDc, Font); // +0x118
        if (Len == -1)
        {
            Len = static_cast<int>(wcslen(Text)); // 00416004
        }

        // 0041600f: the one-time OS probe (OSVERSIONINFOA, size 0x94).
        if (!G_bFontOsVersionProbed)
        {
            G_bFontOsVersionProbed = true;
            OSVERSIONINFOA Version;
            Version.dwOSVersionInfoSize = 0x94;
#pragma warning(push)
#pragma warning(disable : 4996) // GetVersionExA is deprecated; retail calls it here.
            BOOL const bVersion = GetVersionExA(&Version);
#pragma warning(pop)
            if (bVersion)
            {
                G_bFontWholeStringExtent = Version.dwMajorVersion > 4; // 0041603c SETA
            }
            else
            {
                G_bFontWholeStringExtent = false; // 00416062
            }
        }

        SIZE Extent;
        BOOL bOk = FALSE;
        if (G_bFontWholeStringExtent)
        {
            // 0041605a: FUN_005b746a, the delay-load stub of gdi32!GetTextExtentPointW.
            bOk = GetTextExtentPointW(SelectedDc, Text, Len, &Extent);
        }
        else
        {
            // 00416069: per-glyph sum through FUN_005b7445 (gdi32!GetCharWidthW); the height is
            // the realized TEXTMETRIC's tmHeight (+0x120). The count is compared unsigned (JBE /
            // JC), and the first failing glyph zeroes the result.
            Extent.cx = 0;
            Extent.cy = Metrics.tmHeight;
            unsigned const Count = static_cast<unsigned>(Len);
            for (unsigned Index = 0; Index < Count; ++Index)
            {
                UINT const Glyph = static_cast<unsigned short>(Text[Index]);
                INT GlyphWidth = 0;
                bOk = GetCharWidthW(SelectedDc, Glyph, Glyph, &GlyphWidth);
                if (!bOk)
                {
                    break;
                }
                Extent.cx += GlyphWidth;
            }
        }

        // 004160c1: any failure (or an empty loop) reports 0 x 0.
        if (bOk)
        {
            Width = Extent.cx;
            Height = Extent.cy;
        }
        SelectObject(SelectedDc, Previous); // 004160dd
    }
    OutWH[0] = Width;
    OutWH[1] = Height;
}

int FontImp::ProbeString(int MaxWidth, wchar_t const* Text, int Len)
{
    // LAB_004172b0 (vtable slot 9, RET 0xc).
    RefreshScale(true);
    // 004173e9..00417405: count, width sum and index start at 0; Len == -1 -> wcslen.
    int Count = 0;
    unsigned Sum = 0;
    unsigned Index = 0;
    if (Len == -1)
    {
        Len = static_cast<int>(wcslen(Text));
    }
    // 00417408..00417424: select the face into the DC, the previous object kept.
    HDC const SelectedDc = Dc;
    HGDIOBJ const Previous = SelectObject(SelectedDc, Font);
    // 00417428..00417478: stop once the sum reaches MaxWidth (JNC) or the next character would
    // pass it (JA); both compares are unsigned.
    unsigned const Limit = static_cast<unsigned>(MaxWidth);
    if (static_cast<unsigned>(Len) > 0u)
    {
        do
        {
            if (Sum >= Limit)
            {
                break;
            }
            UINT const Glyph = static_cast<unsigned short>(Text[Index]);
            INT Width = 0;
            GetCharWidthW(Dc, Glyph, Glyph, &Width);
            unsigned const Next = static_cast<unsigned>(Width) + Sum;
            if (Next > Limit)
            {
                break;
            }
            ++Count;
            Sum = Next;
            ++Index;
        } while (Index < static_cast<unsigned>(Len));
    }
    // 0041747e..00417484: restore the previous object.
    SelectObject(SelectedDc, Previous);
    return Count;
}

int FontImp::GetCharWidths(wchar_t const* Text, int Len, int* OutWidths)
{
    // LAB_004170e0 (vtable slot 10, RET 0xc).
    RefreshScale(true);

    // 00417219..0041722d: SelectObject(+0xd8 DC, +0x118 HFONT), the previous object kept.
    HDC const SelectedDc = Dc;
    HGDIOBJ const Previous = SelectObject(SelectedDc, Font);
    // 00417237..0041724e: Len == -1 -> wcslen.
    if (Len == -1)
    {
        Len = static_cast<int>(wcslen(Text));
    }
    // 00417250..0041728a: unsigned index loop; the width slot is zeroed before every
    // GetCharWidthW (FUN_005b7445 is its delay-load thunk) and its result is not checked.
    int Sum = 0;
    for (unsigned Index = 0; Index < static_cast<unsigned>(Len); ++Index)
    {
        UINT const Glyph = static_cast<unsigned short>(Text[Index]);
        INT Width = 0;
        GetCharWidthW(Dc, Glyph, Glyph, &Width);
        Sum += Width;
        OutWidths[Index] = Width;
    }
    // 0041728c..00417296: restore the previous object; the sum is returned.
    SelectObject(SelectedDc, Previous);
    return Sum;
}

int LoadFonts(char const* Path, IFileSystem* Filesys)
{
    // FUN_00417800 (from FUN_005b0c30 0x005b0d0e with DALib::Data).
    char FullPath[MAX_PATH];
    int Count = 0;
    GetFileSysPath(FullPath, Path, Filesys);
    INI_Reader Reader;
    if (Reader.open(FullPath, false) && Reader.read_header())
    {
        do
        {
            if (!Reader.is_header("TrueType"))
            {
                continue; // 004178b4 -> the next header
            }
            // 004178ba..004178d7: the section's scratch -- two {length, char[0x80]} strings and
            // fixed_height.
            TString<0x80> Nickname;
            Nickname.Length = 0;
            Nickname.Data[0] = '\0';
            TString<0x80> FontName;
            FontName.Length = 0;
            FontName.Data[0] = '\0';
            float FixedHeight = 0.0f;
            while (Reader.read_value())
            {
                if (Reader.is_value("nickname"))
                {
                    char const* const Value = Reader.get_value_string();
                    if (Value == nullptr)
                    {
                        Nickname.Length = 0;
                        Nickname.Data[0] = '\0';
                    }
                    else
                    {
                        Nickname.Length = CopyBoundedString(Nickname.Data, 0x80, Value);
                    }
                }
                else if (Reader.is_value("font"))
                {
                    char const* const Value = Reader.get_value_string(0);
                    if (Value == nullptr)
                    {
                        FontName.Length = 0;
                        FontName.Data[0] = '\0';
                    }
                    else
                    {
                        FontName.Length = CopyBoundedString(FontName.Data, 0x80, Value);
                    }
                }
                else if (Reader.is_value("fixed_height"))
                {
                    FixedHeight = Reader.get_value_float(0);
                }
            }
            // 004179b3..00417a0a: a default FontImp (FUN_00417d50) copied onto the list's end
            // (FUN_00418bc0 / FUN_00418cd0), then the stack temporary destroyed.
            {
                FontImp Temp;
                G_FontList.insert(G_FontList.end(), Temp);
            }
            // 00417a0f..00417a22: the new last node gets the nickname.
            FontImp* const Imp = &G_FontList.back();
            Imp->SetName(Nickname.Data);
            // 00417a27: retail's own test of the node pointer (never null).
            if (Imp != nullptr)
            {
                // 00417a2b..00417a4d: {face, fixed_height, not bold, not italic, not underlined}.
                FFontDesc Desc;
                Desc.Face = FontName.Data;
                Desc.FixedHeight = FixedHeight;
                Desc.bBold = false;
                Desc.bItalic = false;
                Desc.bUnderline = false;
                if (Imp->Init(Desc))
                {
                    ++Count;
                }
                else
                {
                    FDUMP(0x100001, "%s(%d) : *** ERROR: failed to create FONT font %s", kFontCpp,
                          0xae4, Nickname.Data);
                }
            }
        } while (Reader.read_header());
    }
    return Count;
}

IFont* FindMenuFontImp(char const* Nickname)
{
    // FUN_004177b0: first list entry whose GetName() (vtable +0x04) matches, DACOM stricmp.
    for (std::list<FontImp>::iterator It = G_FontList.begin(); It != G_FontList.end(); ++It)
    {
        if (_stricmp(It->GetName(), Nickname) == 0)
        {
            return &*It; // 004177ed LEA EAX,[ESI+0x8]
        }
    }
    return nullptr;
}

void FontShutdown()
{
    // FUN_004174a0
    // 004174a0..004174e6: free the DIB block, put the old bitmap back, delete bitmap and DC.
    FontAtlasReleaseScratch();
    // 004174ec..00417543: erase every font (FUN_00418fa0 -> FUN_00416490, node freed).
    std::list<FontImp>::iterator It = G_FontList.begin();
    while (It != G_FontList.end())
    {
        It = G_FontList.erase(It);
    }
    // 00417545..004175c0: destroy every pooled atlas page.
    FontAtlasReleasePagePool();
    // 004175c2..00417612: every number face drops its glyph string (FUN_00415050 inlined).
    NumberFaceResetAll();
    // 0041761a: tail jump to FUN_00402d50.
    ScreenQuadResetDrawBatchCache();
}
