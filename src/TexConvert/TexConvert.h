// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// TexConvert: the port's replacement for what D3DX 8.1's D3DXLoadSurfaceFromMemory does for
// RP8's SetTextureLevelData (docs/re/texconvert.md). It converts one image between RP8's pixel
// formats, and resizes it when the sizes differ. No Direct3D, no platform headers: RPGPU uses
// it on Win32 and x64, and TexConvertTest checks it against D3DX8 on Win32.
//
// Same size: D3DX_FILTER_NONE, a per-pixel format conversion. This path is meant to match D3DX
// 8.1 byte for byte, and a DXT image loaded into the same DXT format is copied block by block.
// Different size, or a DXT target from another format: our own box filter and DXT encoder,
// which match D3DX's triangle filter and compressor only within a tolerance.

#include <cstddef>

namespace TexConvert
{
    // RP8 pixel-format numbers (pfenum), as RP8's callers pass them.
    enum EFormat : unsigned int
    {
        kP8 = 1,
        kR8G8B8 = 2,
        kR5G6B5 = 3,
        kX1R5G5B5 = 4,
        kA4R4G4B4 = 5,
        kA1R5G5B5 = 6,
        kA8R8G8B8 = 7,
        kX8R8G8B8 = 8,
        kDxt1 = 10,
        kDxt2 = 11,
        kDxt3 = 12,
        kDxt4 = 13,
        kDxt5 = 14,
    };

    bool IsKnown(unsigned int Format);
    bool IsBlock(unsigned int Format);

    // Bytes per row of pixels, or per row of 4x4 blocks for DXT.
    unsigned int RowPitch(unsigned int Format, unsigned int Width);
    // Rows of pixels, or rows of blocks for DXT.
    unsigned int RowCount(unsigned int Format, unsigned int Height);

    struct FImage
    {
        unsigned int Format = 0;
        unsigned int Width = 0;
        unsigned int Height = 0;
        unsigned int Pitch = 0; // bytes per row (of blocks, for DXT)
        void* Bits = nullptr;
    };

    struct FSource
    {
        unsigned int Format = 0;
        unsigned int Width = 0;
        unsigned int Height = 0;
        unsigned int Pitch = 0;
        const void* Bits = nullptr;
        // P8 only: 256 RGB triplets, as RP8's callers pass them (alpha is opaque).
        const unsigned char* PaletteRgb = nullptr;
    };

    enum class EResult
    {
        Failed,
        Exact,    // the D3DX_FILTER_NONE path (or a DXT block copy)
        Filtered, // resized or DXT-encoded: within a tolerance of D3DX
    };

    // Loads Source into the whole of Destination.
    EResult Load(const FImage& Destination, const FSource& Source);
}
