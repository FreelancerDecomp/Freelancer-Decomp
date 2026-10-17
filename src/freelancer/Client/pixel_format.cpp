// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "pixel_format.h"

namespace
{
    // DAT_00615e70 (.data, 0x58 bytes): D3DFORMAT per id; ids 10..21 are FourCC codes
    // ('DXT1'..'DXT5' first). Read verbatim from the PE bytes.
    unsigned long G_PixelFormatD3D[0x16] = {
        0,          41,         20,         23,         24,         26,
        25,         21,         22,         0,          827611204,  844388420,
        861165636,  877942852,  894720068,  1347371332, 1414480196, 1094795588,
        1279344964, 826360132,  876691780,  943800644};

    // DAT_00615ec8 (.data, 15 x 0x4c bytes), read verbatim from the PE bytes.
    FPixelFormatDesc G_PixelFormatDescs[15] = {
        {0, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
        {1, 8, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
        // R8G8B8
        {2, 0x18, {8, 0xff0000, 0x10, 0}, {8, 0xff00, 8, 0}, {8, 0xff, 0, 0}, {0, 0, 0, 0}, 0},
        // R5G6B5
        {3, 0x10, {5, 0xf800, 0xb, 3}, {6, 0x7e0, 5, 2}, {5, 0x1f, 0, 3}, {0, 0, 0, 0}, 0},
        // X1R5G5B5
        {4, 0x10, {5, 0x7c00, 0xa, 3}, {5, 0x3e0, 5, 3}, {5, 0x1f, 0, 3}, {0, 0, 0, 0}, 0},
        // A4R4G4B4
        {5, 0x10, {4, 0xf00, 8, 4}, {4, 0xf0, 4, 4}, {4, 0xf, 0, 4}, {4, 0xf000, 0xc, 4}, 1},
        // A1R5G5B5
        {6, 0x10, {5, 0x7c00, 0xa, 3}, {5, 0x3e0, 5, 3}, {5, 0x1f, 0, 3}, {1, 0x8000, 0xf, 7}, 1},
        // A8R8G8B8
        {7, 0x20, {8, 0xff0000, 0x10, 0}, {8, 0xff00, 8, 0}, {8, 0xff, 0, 0},
         {8, 0xff000000, 0x18, 0}, 1},
        // X8R8G8B8
        {8, 0x20, {8, 0xff0000, 0x10, 0}, {8, 0xff00, 8, 0}, {8, 0xff, 0, 0}, {0, 0, 0, 0}, 0},
        {9, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
        {0xa, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {1, 0, 0, 0}, 1},
        {0xb, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, 1},
        {0xc, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, 1},
        {0xd, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, 1},
        {0xe, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, 1}};
}

unsigned long* PixelFormatGetD3DFormats()
{
    // FUN_005b7000: MOV EAX,DAT_00615e70.
    return G_PixelFormatD3D;
}

int PixelFormatFromD3D(unsigned long D3DFormat)
{
    // FUN_005b7010: linear scan of the 0x16 entries, the index on a match, else 0.
    unsigned long const* const Formats = PixelFormatGetD3DFormats();
    for (int Index = 0; Index < 0x16; ++Index)
    {
        if (Formats[Index] == D3DFormat)
            return Index;
    }
    return 0;
}

FPixelFormatDesc* PixelFormatGetDesc(int Id)
{
    // FUN_005b7030: CMP EAX,0xe / JLE (signed) -> &DAT_00615ec8[Id], else &DAT_00615ec8[0].
    if (Id > 0xe)
        return &G_PixelFormatDescs[0];
    return &G_PixelFormatDescs[Id];
}

bool PixelFormatFind(unsigned long* OutId, unsigned long Bpp, unsigned long RedBits,
                     unsigned long GreenBits, unsigned long BlueBits, unsigned long AlphaBits)
{
    // FUN_005b7050: walk descriptors 0..8 from FUN_005b7030(0), stride 0x4c.
    FPixelFormatDesc const* Desc = PixelFormatGetDesc(0);
    for (int Index = 0; Index < 9; ++Index, ++Desc)
    {
        if (Desc->Bpp != Bpp)
            continue;
        // 005b707c..005b708a: the low byte of +0x48 against SETNZ (AlphaBits != 0).
        unsigned long const HasAlpha = static_cast<unsigned char>(Desc->HasAlpha);
        if ((AlphaBits != 0 ? 1u : 0u) != HasAlpha)
            continue;
        if (Desc->Red.Bits != RedBits)
            continue;
        if (Desc->Green.Bits != GreenBits)
            continue;
        // 005b709c: retail compares the blue argument with the RED width (ECX = [EAX+8]).
        if (Desc->Red.Bits != BlueBits)
            continue;
        if (Desc->Alpha.Bits != AlphaBits)
            continue;
        *OutId = Desc->Id;
        return true;
    }
    *OutId = static_cast<unsigned long>(PixelFormatFromD3D(0));
    return false;
}
