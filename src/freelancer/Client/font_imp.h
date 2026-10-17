// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// FontImp -- retail `.?AUFontImp@@` (Font.cpp, vtable .rdata:005c8dec, 0x170 bytes). The one
// IFont implementation: a GDI face on its own memory DC, re-realized at a resolution-dependent
// pixel height whenever the layout generation DAT_006105b0 moves. Every font lives in the list
// DAT_0061651c, filled from fonts.ini [TrueType] sections by FUN_00417800 and searched by
// nickname with FUN_004177b0.

#include "common_tstring.h"
#include "font.h"

#include <windows.h>

#include <map>

struct IFileSystem;

// The by-value font description FUN_00415a80 reads (its only builder is FUN_00417800,
// 0x00417a2b..0x00417a4d, which clears all three flags).
struct FFontDesc
{
    char const* Face;  // +0x00 GDI face name
    float FixedHeight; // +0x04 fixed_height (fraction of the screen height)
    bool bBold;        // +0x08 lfWeight 700 instead of 400
    bool bItalic;      // +0x09 lfItalic
    bool bUnderline;   // +0x0a lfUnderline
};

class FontImp : public IFont
{
public:
    // FUN_00417d50: the three strings empty, the scale cache empty, HFONT / realized height /
    // DC / fixed_height / +0xd4 / generation zero. The LOGFONTA and TEXTMETRICA are left as they
    // are (FUN_00415a80 and FUN_00415ea0 write them before any read).
    FontImp();
    // FUN_00418cd0 is the member-wise copy (three 0x44-byte strings, the scalars, both GDI
    // records, the handles and a deep copy of the cache tree) -- the implicit copy constructor.
    // The list insert FUN_00418bc0 copies the stack temporary of FUN_00417800 into its node.
    FontImp(FontImp const& Other) = default;
    FontImp& operator=(FontImp const& Other) = delete;
    // FUN_00416490: the HFONT and DC are only forgotten, never deleted (retail leaks both), the
    // generation is zeroed and the cache tree is freed. Not virtual: retail's vtable has no
    // destructor slot (11 entries), the list destroys FontImp by value.
    ~FontImp();

    // FUN_00417ca0 (RET 4): the id takes the nickname only while it is still empty, the name
    // always takes it (both are the TString<64> assignment).
    void SetName(char const* Nickname);
    // FUN_00415a80 (RET 4): builds the LOGFONTA from the description, creates the DC, runs the
    // autoscale prelude and snapshots tmHeight into +0xd4. Always returns true.
    bool Init(FFontDesc const& Desc);

    char const* Slot00() override;
    char const* GetName() override;
    float GetFixedHeight() override;
    HFONT__* Slot03() override;
    LOGFONTA const* GetLogFont() override;
    float GetGlyphAdvance(wchar_t Ch) override;
    int GetLineHeight() override;
    int Slot07() override;
    void MeasureString(int* OutWH, wchar_t const* Text, int Len) override;
    int ProbeString(int MaxWidth, wchar_t const* Text, int Len) override;
    int GetCharWidths(wchar_t const* Text, int Len, int* OutWidths) override;

private:
    // FUN_00415ea0 (RET 4): (re)create the face at Height into +0x118, select it into the DC and
    // read TEXTMETRICA; false (and an FDUMP) when CreateFontIndirectA fails.
    bool Realize(int Height);
    // FUN_00415e40 (RET 8): realize at Height, measure the calibration pangram, return
    // width / Width (FILD qword / FIDIV, kept on the FPU stack).
    double MeasurePangramRatio(int Height, int Width);
    // FUN_00416f50 (RET 0x10): step the pixel height from round(Fixed * ScreenH) one pixel at a
    // time until the pangram-width ratio crosses Target; returns the accumulated step.
    int SearchScaleDelta(float Fixed, int ScreenW, int ScreenH, float Target);
    // FUN_00416110: the cached pixel delta for the current surface width, searched and cached
    // on a miss with the target pangramWidth(fixed*600) * 0.00125f. Retail inlines the same body
    // into FUN_00415a80, LAB_00416570, LAB_00416820 and LAB_00416af0.
    int ComputeScaleDelta();
    // The variant inlined into LAB_00416dc0, LAB_004170e0 and LAB_004172b0: the same cache, but
    // the target is FUN_00415e40(round(fixed*600), 800) and the search is the out-of-line
    // FUN_00416f50.
    int ComputeScaleDeltaByRatio();
    // The prelude at the head of every face slot: when +0x158 differs from DAT_006105b0, take
    // the new generation, base = GetFixedHeight() * DAT_00610854 (float), delta from one of the
    // two searches above, realize at round(delta + base).
    void RefreshScale(bool bRatioTarget);

    TString<64> Name;                 // +0x04 nickname (GetName)
    TString<64> Id;                   // +0x48 "%s,%d" id (Slot00)
    TString<64> Face;                 // +0x8c GDI face name
    float FixedHeight;                // +0xd0
    int InitLineHeight;               // +0xd4 tmHeight snapshot of FUN_00415a80 (Slot07)
    HDC Dc;                           // +0xd8 CreateCompatibleDC(0)
    LOGFONTA LogFont;                 // +0xdc
    HFONT Font;                       // +0x118
    int RealizedHeight;               // +0x11c lfHeight of the realized face
    TEXTMETRICA Metrics;              // +0x120
    int LayoutGeneration;             // +0x158 vs DAT_006105b0
    std::map<int, int> ScaleDeltaByWidth; // +0x15c surface width -> pixel delta
};

// FUN_00417800 (cdecl): read the [TrueType] sections of Path (resolved through Filesys) into
// the font list; returns the number of fonts whose FUN_00415a80 succeeded.
int LoadFonts(char const* Path, IFileSystem* Filesys);

// FUN_004177b0: the first font in the list whose GetName() matches Nickname (DACOM stricmp),
// else null.
IFont* FindMenuFontImp(char const* Nickname);

// FUN_004174a0: the font subsystem teardown from WinMain's shutdown (0x005b229a) -- the glyph
// scratch, the font list, the atlas page pool, every number face's glyph string and the screen
// quad batch cache.
void FontShutdown();
