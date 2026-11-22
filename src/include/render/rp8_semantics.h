// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.

#pragma once

// RP8's settings and data, shared by the render pipelines (RP8, RP9, RPGPU) so they cannot
// drift apart. Pure data and pure functions: no device, no D3D or Windows type, no state.
// RP8 (src/RP8/RP8.cpp) is the retail reference; the addresses below are rp8.dll's.
//
// - The [RenderPipeline] keys: id, ini key and default of the pipeline states, the device
//   abilities, the render states and the texture-stage states.
// - Rp8IniNumber: how RP8 turns a key's text into a number.
// - Rp8FvfStride: bytes per vertex of a D3D8 flexible vertex format.
// - The pixel-format number (pfenum) tables.
//
// The static_asserts at the end pin the parser's edge cases and run in every module that
// includes this header (docs/testing.md).

#include "msvcrt_compat.h"

#include <cstddef>
#include <cstdint>

// One row of a key table: the state id (pipeline-state id, ability index, D3DRENDERSTATETYPE
// or D3DTEXTURESTAGESTATETYPE), the [RenderPipeline] key and RP8's default. Rows are in the
// order retail inserts them into its maps.
struct FRp8StateKey
{
    int Id;
    const char* Name;
    FlULong Default;
};

// FUN_06d01c68 (06d01d07..06d02032), RP8's map object+0x278: the pipeline states. Id 6,
// BROKEN_MULTITEXTURE, is read and then overwritten with the device's blend-stage count
// (FUN_06d020ab, 06d023a1..06d023ef).
inline constexpr FRp8StateKey kRp8PipelineProperties[] = {
    {0x0, "TEXTURE", 0x1},
    {0x1, "LIGHTING", 0x1},
    {0x7, "VIEWSPACE_LIGHTS", 0x1},
    {0xB, "STATE_CACHE", 0x1},
    {0x8, "CLEAR_COLOR", 0x0},
    {0x9, "CLEAR_ZDEPTH", 0x3F800000},
    {0xA, "CLEAR_STENCIL", 0xFFFFFFFF},
    {0x2, "TEXTURE_LOD", 0x1},
    {0x3, "TEXTURE_ALLOW_8BIT", 0x0},
    {0x4, "TEXTURE_ALLOW_32BIT", 0x1},
    {0x5, "TEXTURE_ALLOW_DXT", 0x1},
    {0x6, "BROKEN_MULTITEXTURE", 0x1},
    {0xC, "BROKEN_MOD2X", 0x0},
    {0xD, "BROKEN_FLIP", 0x0},
};

// FUN_06d19530, map object+0x28C: the device abilities (QueryDeviceAbility index). Disabled
// until startup finds the key in the section; a key that is present overrides the device's
// answer (FUN_06d020ab, 06d035d8..06d03768).
inline constexpr FRp8StateKey kRp8AbilityProperties[] = {
    {0x4, "DEVICE_GEOMETRY", 0x0},
    {0x5, "TEXTURE_SQUARE_ONLY", 0x0},
    {0x6, "TEXTURE_MAX_WIDTH", 0x0},
    {0x7, "TEXTURE_MAX_HEIGHT", 0x0},
    {0x8, "TEXTURE_CUBEMAPS", 0x0},
    {0x9, "DEPTH_BIAS", 0x0},
    {0xA, "FOG_VERTEX", 0x0},
    {0xB, "FOG_PIXEL", 0x0},
    {0xC, "FOG_RANGE", 0x0},
    {0xD, "FOG_W", 0x0},
    {0xE, "RASTER_ANTIALIASEDGES", 0x0},
    {0xF, "DEVICE_SUPPORT_LEVEL", 0x0},
    {0x10, "TEXTURE_TRILINEAR", 0x0},
    {0x11, "DEVICE_BAD_MODE", 0x0},
    {0x12, "DEVICE_BAD_4444", 0x0},
};

// FUN_06d176e0, map object+0x2A0: the render states (D3DRENDERSTATETYPE ids).
inline constexpr FRp8StateKey kRp8RenderStateProperties[] = {
    {0x7, "ZENABLE", 0x1},
    {0x8, "FILLMODE", 0x3},
    {0x9, "SHADEMODE", 0x2},
    {0xA, "LINEPATTERN", 0x0},
    {0xE, "ZWRITEENABLE", 0x1},
    {0xF, "ALPHATESTENABLE", 0x0},
    {0x10, "LASTPIXEL", 0x1},
    {0x13, "SRCBLEND", 0x2},
    {0x14, "DESTBLEND", 0x1},
    {0x16, "CULLMODE", 0x1},
    {0x17, "ZFUNC", 0x2},
    {0x18, "ALPHAREF", 0x0},
    {0x19, "ALPHAFUNC", 0x8},
    {0x1A, "DITHERENABLE", 0x0},
    {0x1B, "ALPHABLENDENABLE", 0x0},
    {0x1C, "FOGENABLE", 0x0},
    {0x1D, "SPECULARENABLE", 0x0},
    {0x1E, "ZVISIBLE", 0x1},
    {0x22, "FOGCOLOR", 0x0},
    {0x23, "FOGTABLEMODE", 0x1},
    {0x24, "FOGSTART", 0x0},
    {0x25, "FOGEND", 0x0},
    {0x26, "FOGDENSITY", 0x0},
    {0x28, "EDGEANTIALIAS", 0x0},
    {0x2F, "ZBIAS", 0x0},
    {0x30, "RANGEFOGENABLE", 0x0},
    {0x34, "STENCILENABLE", 0x0},
    {0x35, "STENCILFAIL", 0x1},
    {0x36, "STENCILZFAIL", 0x1},
    {0x37, "STENCILPASS", 0x1},
    {0x38, "STENCILFUNC", 0x8},
    {0x39, "STENCILREF", 0xFFFFFFFF},
    {0x3A, "STENCILMASK", 0xFFFFFFFF},
    {0x3B, "STENCILWRITEMASK", 0xFFFFFFFF},
    {0x3C, "TEXTUREFACTOR", 0x0},
    {0x80, "WRAP0", 0x0},
    {0x81, "WRAP1", 0x0},
    {0x82, "WRAP2", 0x0},
    {0x83, "WRAP3", 0x0},
    {0x84, "WRAP4", 0x0},
    {0x85, "WRAP5", 0x0},
    {0x86, "WRAP6", 0x0},
    {0x87, "WRAP7", 0x0},
    {0x88, "CLIPPING", 0x1},
    {0x89, "LIGHTING", 0x0},
    {0x8B, "AMBIENT", 0x0},
    {0x8C, "FOGVERTEXMODE", 0x0},
    {0x8D, "COLORVERTEX", 0x0},
    {0x8E, "LOCALVIEWER", 0x0},
    {0x8F, "NORMALIZENORMALS", 0x0},
    {0x91, "DIFFUSEMATERIALSOURCE", 0x1},
    {0x92, "SPECULARMATERIALSOURCE", 0x2},
    {0x93, "AMBIENTMATERIALSOURCE", 0x2},
    {0x94, "EMISSIVEMATERIALSOURCE", 0x0},
    {0x97, "VERTEXBLEND", 0x0},
    {0x98, "CLIPPLANEENABLE", 0x0},
    {0x99, "SOFTWAREVERTEXPROCESSING", 0x0},
    {0x9A, "POINTSIZE", 0x3F800000},
    {0x9B, "POINTSIZE_MIN", 0x3F800000},
    {0x9C, "POINTSPRITEENABLE", 0x0},
    {0x9D, "POINTSCALEENABLE", 0x0},
    {0x9E, "POINTSCALE_A", 0x3F800000},
    {0x9F, "POINTSCALE_B", 0x0},
    {0xA0, "POINTSCALE_C", 0x0},
    {0xA1, "MULTISAMPLEANTIALIAS", 0x1},
    {0xA2, "MULTISAMPLEMASK", 0xFFFFFFFF},
    {0xA3, "PATCHEDGESTYLE", 0x0},
    {0xA4, "PATCHSEGMENTS", 0x0},
    {0xA5, "DEBUGMONITORTOKEN", 0x0},
    {0xA6, "POINTSIZE_MAX", 0x3F800000},
    {0xA7, "INDEXEDVERTEXBLENDENABLE", 0x0},
    {0xA8, "COLORWRITEENABLE", 0xF},
    {0xAA, "TWEENFACTOR", 0x0},
    {0xAB, "BLENDOP", 0x1},
    {0xAC, "POSITIONORDER", 0x1},
    {0xAD, "NORMALORDER", 0x1},
};

// FUN_06d19c30(map, bFirstStage): 06d19c47 JZ -- a non-zero flag inserts COLOROP 4 /
// ALPHAOP 2 (MODULATE / SELECTARG1), zero inserts 1 / 1 (DISABLE); the rest is shared. RP8's
// Initialize builds stage 0 with the flag set and then rebuilds every stage, stage 0 included,
// with it clear, so every stage starts COLOROP 1 / ALPHAOP 1. The stage keys are not read from
// the ini.
inline constexpr FRp8StateKey kRp8StageOpsFirstStage[] = {
    {0x1, "COLOROP", 0x4},
    {0x4, "ALPHAOP", 0x2},
};
inline constexpr FRp8StateKey kRp8StageOpsOtherStage[] = {
    {0x1, "COLOROP", 0x1},
    {0x4, "ALPHAOP", 0x1},
};
inline constexpr FRp8StateKey kRp8StageStateProperties[] = {
    {0x2, "COLORARG1", 0x2},
    {0x3, "COLORARG2", 0x1},
    {0x5, "ALPHAARG1", 0x2},
    {0x6, "ALPHAARG2", 0x1},
    {0x7, "BUMPENVMAT00", 0x0},
    {0x8, "BUMPENVMAT01", 0x0},
    {0x9, "BUMPENVMAT10", 0x0},
    {0xA, "BUMPENVMAT11", 0x0},
    {0xB, "TEXCOORDINDEX", 0x0},
    {0xD, "ADDRESSU", 0x1},
    {0xE, "ADDRESSV", 0x1},
    {0xF, "BORDERCOLOR", 0x0},
    {0x10, "MAGFILTER", 0x2},
    {0x11, "MINFILTER", 0x2},
    {0x12, "MIPFILTER", 0x1},
    {0x13, "MIPMAPLODBIAS", 0x0},
    {0x14, "MAXMIPLEVEL", 0x0},
    {0x15, "MAXANISOTROPY", 0x1},
    {0x16, "BUMPENVLSCALE", 0x0},
    {0x17, "BUMPENVLOFFSET", 0x0},
    {0x18, "TEXTURETRANSFORMFLAGS", 0x0},
    {0x19, "ADDRESSW", 0x1},
    {0x1A, "COLORARG0", 0x1},
    {0x1B, "ALPHAARG0", 0x1},
    {0x1C, "RESULTARG", 0x1},
};

// --- the ini number ------------------------------------------------------------------------

// FUN_06d16930 (inlined into startup): the characters strspn keeps. There is no b, c or d, so
// "0x1C" is cut to "0x1" and reads 1.
inline constexpr char kRp8IniNumberChars[] = "0123456789truefalsynoxTRUEFALSYNOX+-";

namespace Rp8SemanticsDetail
{
    constexpr bool IsNumberChar(char C)
    {
        for (const char* Allowed = kRp8IniNumberChars; *Allowed != '\0'; ++Allowed)
        {
            if (*Allowed == C)
                return true;
        }
        return false;
    }

    // _stricmp(Text[0..Length), Word) == 0, Word in lower case. Only ASCII letters fold: a text
    // that is not cut starts with a character outside kRp8IniNumberChars and matches no word.
    constexpr bool EqualsNoCase(const char* Text, std::size_t Length, const char* Word)
    {
        std::size_t Index = 0;
        for (; Index < Length; ++Index)
        {
            if (Word[Index] == '\0')
                return false;
            char C = Text[Index];
            if (C >= 'A' && C <= 'Z')
                C = static_cast<char>(C - 'A' + 'a');
            if (C != Word[Index])
                return false;
        }
        return Word[Index] == '\0';
    }

    constexpr int HexDigit(char C)
    {
        if (C >= '0' && C <= '9')
            return C - '0';
        if (C >= 'a' && C <= 'f')
            return C - 'a' + 10;
        if (C >= 'A' && C <= 'F')
            return C - 'A' + 10;
        return -1;
    }

    // The CRT's strtoul(Text[0..Length), nullptr, 16) with Windows' 32-bit unsigned long:
    // whitespace, one sign, an optional 0x, hex digits. Overflow gives 0xFFFFFFFF whatever the
    // sign; otherwise a minus sign negates modulo 2^32.
    constexpr std::uint32_t StrtoulHex(const char* Text, std::size_t Length)
    {
        std::size_t Pos = 0;
        while (Pos < Length && (Text[Pos] == ' ' || (Text[Pos] >= 0x09 && Text[Pos] <= 0x0d)))
            ++Pos;
        bool bNegative = false;
        if (Pos < Length && (Text[Pos] == '-' || Text[Pos] == '+'))
        {
            bNegative = Text[Pos] == '-';
            ++Pos;
        }
        if (Pos + 1 < Length && Text[Pos] == '0' && (Text[Pos + 1] == 'x' || Text[Pos + 1] == 'X'))
            Pos += 2;
        std::uint32_t Total = 0;
        bool bOverflow = false;
        for (; Pos < Length; ++Pos)
        {
            const int Digit = HexDigit(Text[Pos]);
            if (Digit < 0)
                break;
            if (Total > 0x0FFFFFFFu)
                bOverflow = true;
            Total = Total * 16u + static_cast<std::uint32_t>(Digit);
        }
        if (bOverflow)
            return 0xFFFFFFFFu;
        return bNegative ? 0u - Total : Total;
    }
}

// FUN_06d16930's conversion of a [RenderPipeline] value (06d16a3b..06d16b1d): the text is cut
// after the leading run of kRp8IniNumberChars (a text that starts outside it is kept whole);
// then "true" / "yes" give 1, "false" / "no" give 0 (any case), a text with an x is hex
// (strtoul), anything else decimal through msvcrt's atoi, which wraps.
//
// Examples: "0x1C" -> 1, "0x1F" -> 0x1F, "yes" -> 1, "-1" -> 0xFFFFFFFF, "12abc" -> 12,
// "" -> 0.
constexpr std::uint32_t Rp8IniNumber(const char* Text)
{
    std::size_t Length = 0;
    while (Text[Length] != '\0')
        ++Length;
    std::size_t Span = 0;
    while (Span < Length && Rp8SemanticsDetail::IsNumberChar(Text[Span]))
        ++Span;
    if (Span != 0)
        Length = Span;

    if (Rp8SemanticsDetail::EqualsNoCase(Text, Length, "true") ||
        Rp8SemanticsDetail::EqualsNoCase(Text, Length, "yes"))
        return 1;
    if (Rp8SemanticsDetail::EqualsNoCase(Text, Length, "false") ||
        Rp8SemanticsDetail::EqualsNoCase(Text, Length, "no"))
        return 0;
    for (std::size_t Index = 0; Index < Length; ++Index)
    {
        if (Text[Index] == 'x' || Text[Index] == 'X')
            return Rp8SemanticsDetail::StrtoulHex(Text, Length);
    }
    // 06d16b09 also runs strspn(buf, "0123456789+-") and drops the result. atoi never reads past
    // the cut: the character there is not a digit, and a cut text starts with no whitespace.
    return static_cast<std::uint32_t>(MsvcrtAtoi(Text));
}

// --- vertex formats --------------------------------------------------------------------------

// FDirectX8::FvfStride (retail inlines it in case3, FUN_06d21450 and FUN_06d2230f): bytes per
// vertex of a D3D8 FVF. Position (XYZ 0xC, XYZRHW 0x10, 0xC as 0x1C), normal 0xC, diffuse and
// specular 4 each, then each texture coordinate set by its size code (0 two floats, 1 three,
// 2 four, 3 one).
constexpr unsigned int Rp8FvfStride(unsigned int Fvf)
{
    unsigned int Bytes = 0;
    if ((Fvf & 0xC) == 0xC)
        Bytes = 0x1C;
    else if ((Fvf & 4) == 4)
        Bytes = 0x10;
    else if ((Fvf & 2) == 2)
        Bytes = 0xC;
    if ((Fvf & 0x10) != 0)
        Bytes += 0xC;
    if ((Fvf & 0x40) != 0)
        Bytes += 4;
    if ((Fvf & 0x80) != 0)
        Bytes += 4;
    const unsigned int TexCount = (Fvf & 0xF00) >> 8;
    for (unsigned int Index = 0; Index < TexCount; ++Index)
    {
        const unsigned int Size = (Fvf >> (Index * 2 + 0x10)) & 3;
        if (Size == 0)
            Bytes += 8;
        else if (Size == 1)
            Bytes += 0xC;
        else if (Size == 2)
            Bytes += 0x10;
        else
            Bytes += 4;
    }
    return Bytes;
}

// --- pixel formats ---------------------------------------------------------------------------

// RP8's pixel-format numbers (pfenum): 1 P8, 2 R8G8B8, 3 R5G6B5, 4 X1R5G5B5, 5 A4R4G4B4,
// 6 A1R5G5B5, 7 A8R8G8B8, 8 X8R8G8B8, 10..14 DXT1..5, 15..21 the format classes.

// DAT_06d6ffa8 -- 22 dwords (FUN_06d5cc20): pfenum -> D3DFORMAT. Values at index 2 / 7 are
// 0x14 / 0x15 (also valid indices 20 / 21 = DAA4 / DAA8).
inline constexpr unsigned int kRp8PfToD3d[22] = {
    0,    0x29, 0x14, 0x17, 0x18, 0x1A, 0x19, 0x15, 0x16, 0,
    0x31545844, 0x32545844, 0x33545844, 0x34545844, 0x35545844,
    0x504F4144, 0x544F4144, 0x41414144, 0x4C414144, 0x31414144,
    0x34414144, 0x38414144};

// Bits per pixel from descriptor +4 (ximage DAT_06f841a8 index 1-8 = RP8 DAT_06d70000).
inline constexpr unsigned int kRp8PfBpp[22] = {
    0, 8, 24, 16, 16, 16, 16, 32, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// --- checks ----------------------------------------------------------------------------------

// The parser's edge cases. RP8's code before this header gave the same answers; a change here
// changes what every pipeline reads from DACOM.INI.
static_assert(Rp8IniNumber("0x1C") == 1);                  // C is not kept: "0x1"
static_assert(Rp8IniNumber("0x1F") == 0x1F);
static_assert(Rp8IniNumber("0X3F800000") == 0x3F800000);
static_assert(Rp8IniNumber("0xFFFFFFFF") == 0xFFFFFFFFu);
static_assert(Rp8IniNumber("0x100000000") == 0xFFFFFFFFu); // strtoul overflows to ULONG_MAX
static_assert(Rp8IniNumber("-0x1") == 0xFFFFFFFFu);
static_assert(Rp8IniNumber("nox") == 0);                   // hex with no digits
static_assert(Rp8IniNumber("true") == 1);
static_assert(Rp8IniNumber("TRUE") == 1);
static_assert(Rp8IniNumber("Yes") == 1);
static_assert(Rp8IniNumber("yes please") == 1);            // cut at the space
static_assert(Rp8IniNumber("false") == 0);
static_assert(Rp8IniNumber("No") == 0);
static_assert(Rp8IniNumber("true1") == 0);                 // neither a word nor a number
static_assert(Rp8IniNumber("+5") == 5);
static_assert(Rp8IniNumber("-1") == 0xFFFFFFFFu);
static_assert(Rp8IniNumber("+-5") == 0);
static_assert(Rp8IniNumber("12abc") == 12);
static_assert(Rp8IniNumber("7 ; comment") == 7);
static_assert(Rp8IniNumber(" 7") == 7);                    // not cut: atoi skips the space
static_assert(Rp8IniNumber("4294967296") == 0);            // msvcrt's atoi wraps
static_assert(Rp8IniNumber("") == 0);

static_assert(Rp8FvfStride(0x002) == 0xC);  // XYZ
static_assert(Rp8FvfStride(0x142) == 0x18); // XYZ | DIFFUSE | TEX1
static_assert(Rp8FvfStride(0x152) == 0x24); // XYZ | NORMAL | DIFFUSE | TEX1
static_assert(Rp8FvfStride(0x1C4) == 0x20); // XYZRHW | DIFFUSE | SPECULAR | TEX1
static_assert(Rp8FvfStride(0x10212) == 0x2C); // XYZ | NORMAL | TEX2, set 0 three floats
