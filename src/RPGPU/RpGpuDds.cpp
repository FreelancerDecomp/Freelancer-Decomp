// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#include "RpGpuDds.h"

#include "RpGpuCommon.h" // MaxMipLevels

#include <algorithm>
#include <cstring>

using namespace RpGpuDetail;

namespace
{
    // ReadDdsImageInfo's sizes stop at this: a file is at most 4 GB (SrcSize is 32-bit), so
    // anything larger fails the check, and a header whose sizes would overflow 64 bits cannot
    // wrap round to a small payload.
    constexpr std::uint64_t kTooBig = std::uint64_t(1) << 33;

    // The byte size of a DDS mip chain (every face of a cube, every slice of a volume), for the
    // payload check, or kTooBig.
    std::uint64_t DdsPayloadBytes(std::uint32_t Width, std::uint32_t Height, std::uint32_t Depth,
                                  std::uint32_t Levels, std::uint32_t BlockBytes,
                                  std::uint32_t BitsPerPixel, std::uint32_t Faces)
    {
        std::uint64_t Total = 0;
        for (std::uint32_t Level = 0; Level < Levels; ++Level)
        {
            const std::uint64_t W = std::max<std::uint32_t>(1u, Width >> Level);
            const std::uint64_t H = std::max<std::uint32_t>(1u, Height >> Level);
            const std::uint64_t D = std::max<std::uint32_t>(1u, Depth >> Level);
            // Blocks or pixels of one slice: below 2^64, as W and H are below 2^32.
            std::uint64_t Bytes = BlockBytes != 0 ? ((W + 3) / 4) * ((H + 3) / 4) : W * H;
            if (Bytes >= kTooBig)
                return kTooBig;
            Bytes = BlockBytes != 0 ? Bytes * BlockBytes : Bytes * BitsPerPixel / 8;
            if (Bytes >= kTooBig || Bytes > kTooBig / D)
                return kTooBig;
            Total += Bytes * D;
            if (Total >= kTooBig)
                return kTooBig;
        }
        return Total * Faces;
    }

    // DDS file header fields GetImageInfo reads (offsets from the start of the file).
    std::uint32_t ReadU32(const unsigned char* Data, unsigned int Offset)
    {
        std::uint32_t Value;
        std::memcpy(&Value, Data + Offset, sizeof(Value));
        return Value;
    }
}

// D3DXIMAGE_INFO for a DDS file, read from its header: +0 width, +4 height, +8 depth,
// +0xC mip levels, +0x10 D3DFORMAT, +0x14 resource type, +0x18 file format (4, DDS). Shading
// loads only DDS this way; anything else fails as D3DX's answer would make it.
bool ReadDdsImageInfo(FRpImageInfo* Info, unsigned int SrcSize, const void* SrcData)
{
    const unsigned char* const Data = static_cast<const unsigned char*>(SrcData);
    if (Info == nullptr || Data == nullptr || SrcSize < 128 || std::memcmp(Data, "DDS ", 4) != 0)
        return false;
    const std::uint32_t Height = ReadU32(Data, 12);
    const std::uint32_t Width = ReadU32(Data, 16);
    const std::uint32_t Depth = ReadU32(Data, 24);
    const std::uint32_t Mips = ReadU32(Data, 28);
    const std::uint32_t PfFlags = ReadU32(Data, 80);
    const std::uint32_t FourCc = ReadU32(Data, 84);
    const std::uint32_t Bits = ReadU32(Data, 88);
    const std::uint32_t RMask = ReadU32(Data, 92);
    const std::uint32_t GMask = ReadU32(Data, 96);
    const std::uint32_t AMask = ReadU32(Data, 104);
    const std::uint32_t Caps2 = ReadU32(Data, 112);

    std::uint32_t Format = 0;
    if ((PfFlags & 0x4) != 0) // DDPF_FOURCC: D3DFMT_DXT1..5 are the FourCC codes themselves
        Format = FourCc;
    else if ((PfFlags & 0x20) != 0) // DDPF_PALETTEINDEXED8
        Format = 41;                // D3DFMT_P8
    else if (Bits == 32)
        Format = AMask != 0 ? 21u : 22u; // A8R8G8B8 / X8R8G8B8
    else if (Bits == 24)
        Format = 20; // R8G8B8
    else if (Bits == 16)
    {
        if (GMask == 0x07E0)
            Format = 23; // R5G6B5
        else if (RMask == 0x0F00)
            Format = 26; // A4R4G4B4
        else
            Format = AMask != 0 ? 25u : 24u; // A1R5G5B5 / X1R5G5B5
    }
    if (Format == 0)
        return false;

    // The file must hold the pixels its header promises (D3DX rejects a truncated file).
    const bool bCube = (Caps2 & 0x200) != 0;
    const bool bVolume = (Caps2 & 0x200000) != 0 && Depth != 0;
    std::uint32_t BlockBytes = 0;
    if ((PfFlags & 0x4) != 0)
    {
        if (FourCc == 0x31545844u) // 'DXT1'
            BlockBytes = 8;
        else if (FourCc >= 0x32545844u && FourCc <= 0x35545844u) // 'DXT2'..'DXT5'
            BlockBytes = 16;
        else
            return false; // other FourCC formats: not loaded through this path
    }
    const std::uint32_t PixelBits = (PfFlags & 0x20) != 0 ? 8u : Bits;
    // A volume's depth halves per level, as its width and height do.
    const std::uint32_t Faces = bCube ? 6u : 1u;
    const std::uint32_t Slices = bVolume ? Depth : 1u;
    const std::uint32_t Levels = Mips != 0 ? Mips : 1u;
    if (Levels > MaxMipLevels(std::max(Width, Height), Slices) || Width == 0 || Height == 0)
        return false;
    std::uint64_t Needed = 128 + DdsPayloadBytes(Width, Height, Slices, Levels, BlockBytes, PixelBits, Faces);
    if ((PfFlags & 0x20) != 0)
        Needed += 256 * 4; // the palette
    if (Needed > SrcSize)
        return false;

    Info->Width = Width;
    Info->Height = Height;
    Info->Depth = (Caps2 & 0x200000) != 0 && Depth != 0 ? Depth : 1;
    Info->MipLevels = Mips != 0 ? Mips : 1;
    Info->Format = Format;
    Info->ResourceType = (Caps2 & 0x200) != 0 ? 5u : (Caps2 & 0x200000) != 0 ? 4u : 3u; // cube / volume / 2D
    Info->ImageFileFormat = 4; // D3DXIFF_DDS
    return true;
}

// The cube map's layout, after ReadDdsImageInfo has checked the header and the file's size.
bool ReadDdsCube(FDdsCube* Out, unsigned int SrcSize, const void* SrcData)
{
    FRpImageInfo Info = {};
    if (Out == nullptr || !ReadDdsImageInfo(&Info, SrcSize, SrcData))
        return false;
    if (Info.ResourceType != 5 || Info.Width != Info.Height || Info.MipLevels > 16)
        return false;
    // D3DFORMAT to RP8's pfenum.
    unsigned int Format = 0;
    switch (Info.Format)
    {
    case 0x31545844u: Format = 10; break; // DXT1
    case 0x32545844u: Format = 11; break; // DXT2
    case 0x33545844u: Format = 12; break; // DXT3
    case 0x34545844u: Format = 13; break; // DXT4
    case 0x35545844u: Format = 14; break; // DXT5
    case 20: Format = 2; break;           // R8G8B8
    case 21: Format = 7; break;           // A8R8G8B8
    case 22: Format = 8; break;           // X8R8G8B8
    case 23: Format = 3; break;           // R5G6B5
    case 24: Format = 4; break;           // X1R5G5B5
    case 25: Format = 6; break;           // A1R5G5B5
    case 26: Format = 5; break;           // A4R4G4B4
    default: return false;                // P8 and the rest: not as a cube
    }
    // An uncompressed file's masks must be that format's: an A8B8G8R8 file (D3DX 8.1 has no such
    // format) has the size of an A8R8G8B8 one but not its byte order.
    const unsigned char* const Data = static_cast<const unsigned char*>(SrcData);
    const std::uint32_t RMask = ReadU32(Data, 92);
    const std::uint32_t GMask = ReadU32(Data, 96);
    const std::uint32_t BMask = ReadU32(Data, 100);
    const std::uint32_t AMask = ReadU32(Data, 104);
    struct FMasks
    {
        unsigned int Format;
        std::uint32_t R, G, B, A;
    };
    static constexpr FMasks kMasks[] = {
        {2, 0xFF0000, 0xFF00, 0xFF, 0},         {7, 0xFF0000, 0xFF00, 0xFF, 0xFF000000},
        {8, 0xFF0000, 0xFF00, 0xFF, 0},         {3, 0xF800, 0x07E0, 0x001F, 0},
        {4, 0x7C00, 0x03E0, 0x001F, 0},         {6, 0x7C00, 0x03E0, 0x001F, 0x8000},
        {5, 0x0F00, 0x00F0, 0x000F, 0xF000},
    };
    for (const FMasks& Masks : kMasks)
    {
        if (Masks.Format == Format &&
            (RMask != Masks.R || GMask != Masks.G || BMask != Masks.B || (Masks.A != 0 && AMask != Masks.A)))
            return false;
    }
    const bool bBlock = Format >= 10;
    const unsigned int BlockBytes = Format == 10 ? 8u : 16u;
    const unsigned int PixelBytes = Format == 7 || Format == 8 ? 4u : Format == 2 ? 3u : 2u;
    Out->Edge = Info.Width;
    Out->Levels = Info.MipLevels;
    Out->Format = Format;
    for (unsigned int Level = 0; Level < Out->Levels; ++Level)
    {
        const unsigned int Size = std::max(1u, Out->Edge >> Level);
        Out->Bytes[Level] = bBlock ? ((Size + 3) / 4) * ((Size + 3) / 4) * BlockBytes : Size * Size * PixelBytes;
    }
    unsigned int Offset = 128;
    for (int Face = 0; Face < 6; ++Face)
    {
        for (unsigned int Level = 0; Level < Out->Levels; ++Level)
        {
            Out->Offset[Face][Level] = Offset;
            Offset += Out->Bytes[Level];
        }
    }
    return Offset <= SrcSize; // ReadDdsImageInfo checked this already
}
