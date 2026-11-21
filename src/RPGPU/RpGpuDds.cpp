// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#include "RpGpuDds.h"

#include "RpGpuCommon.h" // MaxMipLevels

#include <algorithm>
#include <cstring>

using namespace RpGpuDetail;

namespace
{
    // The byte size of a DDS mip chain (all faces of a cube), for the payload check.
    std::uint64_t DdsPayloadBytes(std::uint32_t Width, std::uint32_t Height, std::uint32_t Levels,
                                  std::uint32_t BlockBytes, std::uint32_t BitsPerPixel, std::uint32_t Faces)
    {
        std::uint64_t Total = 0;
        for (std::uint32_t Level = 0; Level < Levels; ++Level)
        {
            const std::uint64_t W = std::max<std::uint32_t>(1u, Width >> Level);
            const std::uint64_t H = std::max<std::uint32_t>(1u, Height >> Level);
            if (BlockBytes != 0)
                Total += ((W + 3) / 4) * ((H + 3) / 4) * BlockBytes;
            else
                Total += W * H * BitsPerPixel / 8;
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
    // A volume's depth halves per level; its slices are not counted, so the check is a minimum.
    const std::uint32_t Faces = bCube ? 6u : 1u;
    const std::uint32_t Levels = Mips != 0 ? Mips : 1u;
    if (Levels > MaxMipLevels(Width, Height) || Width == 0 || Height == 0)
        return false;
    std::uint64_t Needed = 128 + DdsPayloadBytes(Width, Height, Levels, BlockBytes, PixelBits, Faces);
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
