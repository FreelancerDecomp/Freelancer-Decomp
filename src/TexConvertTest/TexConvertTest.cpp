// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// TexConvertTest.exe (Win32, port only): checks TexConvert's exact path against D3DX 8.1's
// D3DXLoadSurfaceFromMemory, which RP8's SetTextureLevelData uses (docs/re/texconvert.md).
//
// Every source format is loaded into every uncompressed target format, and every DXT format into
// itself, at the same size with D3DX_FILTER_NONE, as RP8 does when the sizes match. The source
// images are deterministic: every 16-bit value, a byte ramp, random bytes, and random DXT
// blocks. D3DX writes into a system-memory image surface of a Direct3D 8 device, TexConvert into
// a buffer; the bytes must be identical.
//
// A second set loads wide and tall DXT images with the pitch Shading's DDS loader passes
// (linear size / width in blocks), to show what D3DX does with it.
//
// Exit code: 0 all identical, 1 a difference, 2 nothing compared (no Direct3D 8 device, or no
// case could run).

#include "TexConvert.h"

// NOMINMAX: windows.h's min / max macros would break std::max.
#define NOMINMAX
#include <windows.h>

#include <d3d8.h>
#include <d3dx8.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
    D3DFORMAT ToD3d(unsigned int Format)
    {
        switch (Format)
        {
        case TexConvert::kP8: return D3DFMT_P8;
        case TexConvert::kR8G8B8: return D3DFMT_R8G8B8;
        case TexConvert::kR5G6B5: return D3DFMT_R5G6B5;
        case TexConvert::kX1R5G5B5: return D3DFMT_X1R5G5B5;
        case TexConvert::kA4R4G4B4: return D3DFMT_A4R4G4B4;
        case TexConvert::kA1R5G5B5: return D3DFMT_A1R5G5B5;
        case TexConvert::kA8R8G8B8: return D3DFMT_A8R8G8B8;
        case TexConvert::kX8R8G8B8: return D3DFMT_X8R8G8B8;
        case TexConvert::kDxt1: return D3DFMT_DXT1;
        case TexConvert::kDxt2: return D3DFMT_DXT2;
        case TexConvert::kDxt3: return D3DFMT_DXT3;
        case TexConvert::kDxt4: return D3DFMT_DXT4;
        case TexConvert::kDxt5: return D3DFMT_DXT5;
        default: return D3DFMT_UNKNOWN;
        }
    }

    const char* Name(unsigned int Format)
    {
        switch (Format)
        {
        case TexConvert::kP8: return "P8";
        case TexConvert::kR8G8B8: return "R8G8B8";
        case TexConvert::kR5G6B5: return "R5G6B5";
        case TexConvert::kX1R5G5B5: return "X1R5G5B5";
        case TexConvert::kA4R4G4B4: return "A4R4G4B4";
        case TexConvert::kA1R5G5B5: return "A1R5G5B5";
        case TexConvert::kA8R8G8B8: return "A8R8G8B8";
        case TexConvert::kX8R8G8B8: return "X8R8G8B8";
        case TexConvert::kDxt1: return "DXT1";
        case TexConvert::kDxt2: return "DXT2";
        case TexConvert::kDxt3: return "DXT3";
        case TexConvert::kDxt4: return "DXT4";
        case TexConvert::kDxt5: return "DXT5";
        default: return "?";
        }
    }

    // A fixed linear congruential sequence, so every run compares the same images.
    std::uint32_t G_Seed = 0x12345678u;
    std::uint32_t NextRandom()
    {
        G_Seed = G_Seed * 1664525u + 1013904223u;
        return G_Seed >> 8;
    }

    // Source bytes for one pattern: 0 every 16-bit value / a byte ramp, 1 random bytes.
    std::vector<unsigned char> MakeSource(unsigned int Format, unsigned int Width, unsigned int Height,
                                          int Pattern)
    {
        const std::size_t Size =
            static_cast<std::size_t>(TexConvert::RowPitch(Format, Width)) * TexConvert::RowCount(Format, Height);
        std::vector<unsigned char> Bytes(Size);
        for (std::size_t Index = 0; Index < Size; ++Index)
        {
            if (Pattern == 0)
                Bytes[Index] = static_cast<unsigned char>((Index & 1) != 0 ? (Index >> 9) + (Index >> 1) : Index >> 1);
            else
                Bytes[Index] = static_cast<unsigned char>(NextRandom());
        }
        return Bytes;
    }

    struct FDevice
    {
        IDirect3D8* D3d = nullptr;
        IDirect3DDevice8* Device = nullptr;
        HWND Window = nullptr;
    };

    bool CreateDevice(FDevice* Out)
    {
        Out->D3d = Direct3DCreate8(D3D_SDK_VERSION);
        if (Out->D3d == nullptr)
            return false;
        Out->Window = CreateWindowA("STATIC", "TexConvertTest", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr,
                                    nullptr, GetModuleHandleA(nullptr), nullptr);
        D3DDISPLAYMODE Mode;
        if (Out->D3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &Mode) < 0)
            return false;
        D3DPRESENT_PARAMETERS Params = {};
        Params.Windowed = TRUE;
        Params.SwapEffect = D3DSWAPEFFECT_DISCARD;
        Params.BackBufferFormat = Mode.Format;
        Params.hDeviceWindow = Out->Window;
        const D3DDEVTYPE Types[] = {D3DDEVTYPE_HAL, D3DDEVTYPE_REF};
        for (D3DDEVTYPE Type : Types)
        {
            if (Out->D3d->CreateDevice(D3DADAPTER_DEFAULT, Type, Out->Window,
                                       D3DCREATE_SOFTWARE_VERTEXPROCESSING, &Params, &Out->Device) >= 0)
                return true;
        }
        return false;
    }

    int G_Compared = 0;   // comparisons with D3DX
    int G_SelfChecks = 0; // checks without D3DX
    int G_Different = 0;
    int G_Skipped = 0;

    // SrcPitch 0: the format's own pitch; otherwise the pitch passed to both converters.
    void Compare(IDirect3DDevice8* Device, unsigned int SrcFormat, unsigned int DstFormat, unsigned int Width,
                 unsigned int Height, int Pattern, unsigned int SrcPitchOverride = 0)
    {
        std::vector<unsigned char> Source = MakeSource(SrcFormat, Width, Height, Pattern);
        // An overridden pitch can span more bytes than the image (Shading's pitch for a tall DXT
        // mip): D3DX reads Rows x pitch, so the buffer covers that, padded with zeros.
        if (SrcPitchOverride != 0)
            Source.resize(std::max<std::size_t>(Source.size(),
                                                static_cast<std::size_t>(SrcPitchOverride) *
                                                    TexConvert::RowCount(SrcFormat, Height)),
                          0);
        unsigned char Palette[256 * 3];
        PALETTEENTRY Entries[256];
        for (int Index = 0; Index < 256; ++Index)
        {
            for (int Ch = 0; Ch < 3; ++Ch)
                Palette[Index * 3 + Ch] = static_cast<unsigned char>(NextRandom());
            // RP8's conversion of its callers' RGB palette (SetTextureLevelData).
            Entries[Index].peRed = Palette[Index * 3];
            Entries[Index].peGreen = Palette[Index * 3 + 1];
            Entries[Index].peBlue = Palette[Index * 3 + 2];
            Entries[Index].peFlags = 0xFF;
        }
        const unsigned int SrcPitch =
            SrcPitchOverride != 0 ? SrcPitchOverride : TexConvert::RowPitch(SrcFormat, Width);

        IDirect3DSurface8* Surface = nullptr;
        if (Device->CreateImageSurface(Width, Height, ToD3d(DstFormat), &Surface) < 0)
        {
            std::printf("skip %s -> %s: no image surface\n", Name(SrcFormat), Name(DstFormat));
            ++G_Skipped;
            return;
        }
        RECT Rect = {0, 0, static_cast<LONG>(Width), static_cast<LONG>(Height)};
        const HRESULT Hr = D3DXLoadSurfaceFromMemory(Surface, nullptr, nullptr, Source.data(), ToD3d(SrcFormat),
                                                     SrcPitch, SrcFormat == TexConvert::kP8 ? Entries : nullptr,
                                                     &Rect, D3DX_FILTER_NONE, 0);
        if (Hr < 0)
        {
            std::printf("skip %s -> %s: D3DX failed 0x%08lx\n", Name(SrcFormat), Name(DstFormat),
                        static_cast<unsigned long>(Hr));
            ++G_Skipped;
            Surface->Release();
            return;
        }

        const unsigned int DstPitch = TexConvert::RowPitch(DstFormat, Width);
        const unsigned int Rows = TexConvert::RowCount(DstFormat, Height);
        std::vector<unsigned char> Ours(static_cast<std::size_t>(DstPitch) * Rows);
        TexConvert::FImage Image;
        Image.Format = DstFormat;
        Image.Width = Width;
        Image.Height = Height;
        Image.Pitch = DstPitch;
        Image.Bits = Ours.data();
        TexConvert::FSource Src;
        Src.Format = SrcFormat;
        Src.Width = Width;
        Src.Height = Height;
        Src.Pitch = SrcPitch;
        Src.Bits = Source.data();
        Src.PaletteRgb = Palette;
        const TexConvert::EResult Result = TexConvert::Load(Image, Src);

        D3DLOCKED_RECT Locked;
        if (Surface->LockRect(&Locked, nullptr, D3DLOCK_READONLY) < 0)
        {
            ++G_Skipped;
            Surface->Release();
            return;
        }
        ++G_Compared;
        int FirstRow = -1;
        unsigned int FirstByte = 0;
        int Differing = 0;
        for (unsigned int Row = 0; Row < Rows; ++Row)
        {
            const unsigned char* const Theirs = static_cast<const unsigned char*>(Locked.pBits) + Row * Locked.Pitch;
            const unsigned char* const Mine = Ours.data() + static_cast<std::size_t>(Row) * DstPitch;
            for (unsigned int Byte = 0; Byte < DstPitch; ++Byte)
            {
                if (Theirs[Byte] != Mine[Byte])
                {
                    if (FirstRow < 0)
                    {
                        FirstRow = static_cast<int>(Row);
                        FirstByte = Byte;
                    }
                    ++Differing;
                }
            }
        }
        if (Result != TexConvert::EResult::Exact || Differing != 0)
        {
            ++G_Different;
            std::printf("DIFF %s -> %s %ux%u pattern %d%s: %d bytes differ", Name(SrcFormat), Name(DstFormat),
                        Width, Height, Pattern, SrcPitchOverride != 0 ? " (Shading's pitch)" : "", Differing);
            if (FirstRow >= 0)
            {
                const unsigned char* const Theirs =
                    static_cast<const unsigned char*>(Locked.pBits) + FirstRow * Locked.Pitch + FirstByte;
                const unsigned char* const Mine = Ours.data() + static_cast<std::size_t>(FirstRow) * DstPitch + FirstByte;
                std::printf(", first at row %d byte %u: D3DX", FirstRow, FirstByte);
                for (int Index = 0; Index < 4 && FirstByte + Index < DstPitch; ++Index)
                    std::printf(" %02x", Theirs[Index]);
                std::printf(", ours");
                for (int Index = 0; Index < 4 && FirstByte + Index < DstPitch; ++Index)
                    std::printf(" %02x", Mine[Index]);
            }
            if (Result != TexConvert::EResult::Exact)
                std::printf(" (not the exact path)");
            std::printf("\n");
        }
        Surface->UnlockRect();
        Surface->Release();
    }
}

namespace
{
// One DXT block through D3DX and TexConvert into A8R8G8B8; on a difference, the block and both
// results are printed, so the decoder's rounding can be read off the log.
int G_ProbeLines = 0;
void Probe(IDirect3DDevice8* Device, unsigned int Format, const unsigned char* Block)
{
    const unsigned int Bytes = Format == TexConvert::kDxt1 ? 8u : 16u;
    IDirect3DSurface8* Surface = nullptr;
    if (Device->CreateImageSurface(4, 4, D3DFMT_A8R8G8B8, &Surface) < 0)
        return;
    RECT Rect = {0, 0, 4, 4};
    if (D3DXLoadSurfaceFromMemory(Surface, nullptr, nullptr, Block, ToD3d(Format), Bytes, nullptr, &Rect,
                                  D3DX_FILTER_NONE, 0) >= 0)
    {
        std::uint32_t Ours[16] = {};
        TexConvert::FImage Image;
        Image.Format = TexConvert::kA8R8G8B8;
        Image.Width = 4;
        Image.Height = 4;
        Image.Pitch = 16;
        Image.Bits = Ours;
        TexConvert::FSource Src;
        Src.Format = Format;
        Src.Width = 4;
        Src.Height = 4;
        Src.Pitch = Bytes;
        Src.Bits = Block;
        const TexConvert::EResult Result = TexConvert::Load(Image, Src);
        D3DLOCKED_RECT Locked;
        if (Surface->LockRect(&Locked, nullptr, D3DLOCK_READONLY) >= 0)
        {
            std::uint32_t Theirs[16];
            for (int Row = 0; Row < 4; ++Row)
                std::memcpy(Theirs + Row * 4, static_cast<const unsigned char*>(Locked.pBits) + Row * Locked.Pitch, 16);
            Surface->UnlockRect();
            ++G_Compared;
            const bool bDifferent =
                Result != TexConvert::EResult::Exact || std::memcmp(Theirs, Ours, sizeof(Ours)) != 0;
            if (bDifferent)
                ++G_Different;
            if (bDifferent && G_ProbeLines < 60)
            {
                ++G_ProbeLines;
                std::printf("PROBE %s block", Name(Format));
                for (unsigned int Index = 0; Index < Bytes; ++Index)
                    std::printf(" %02x", Block[Index]);
                std::printf(" | D3DX");
                for (int Index = 0; Index < 16; ++Index)
                    std::printf(" %08x", Theirs[Index]);
                std::printf(" | ours");
                for (int Index = 0; Index < 16; ++Index)
                    std::printf(" %08x", Ours[Index]);
                std::printf("\n");
            }
        }
    }
    Surface->Release();
}

// The filtered path into a DXT format: an A8R8G8B8 image with partial alpha, compressed by D3DX
// and by TexConvert, both decoded back with TexConvert's (D3DX-matching) decoder. The encoders
// differ by design, so the check is the largest channel difference against a tolerance.
void CompareEncode(IDirect3DDevice8* Device, unsigned int Format)
{
    const unsigned int Width = 64;
    const unsigned int Height = 64;
    std::vector<std::uint32_t> Image(Width * Height);
    for (unsigned int Y = 0; Y < Height; ++Y)
        for (unsigned int X = 0; X < Width; ++X)
            Image[Y * Width + X] = ((X * 4u) << 24) | ((Y * 4u) << 16) | (((X + Y) * 2u) << 8) | (255u - X * 3u);
    IDirect3DSurface8* Surface = nullptr;
    if (Device->CreateImageSurface(Width, Height, ToD3d(Format), &Surface) < 0)
    {
        ++G_Skipped;
        return;
    }
    RECT Rect = {0, 0, static_cast<LONG>(Width), static_cast<LONG>(Height)};
    if (D3DXLoadSurfaceFromMemory(Surface, nullptr, nullptr, Image.data(), D3DFMT_A8R8G8B8, Width * 4, nullptr,
                                  &Rect, D3DX_FILTER_NONE, 0) < 0)
    {
        ++G_Skipped;
        Surface->Release();
        return;
    }
    const unsigned int Pitch = TexConvert::RowPitch(Format, Width);
    const unsigned int Rows = TexConvert::RowCount(Format, Height);
    std::vector<unsigned char> Theirs(static_cast<std::size_t>(Pitch) * Rows);
    D3DLOCKED_RECT Locked;
    if (Surface->LockRect(&Locked, nullptr, D3DLOCK_READONLY) < 0)
    {
        ++G_Skipped;
        Surface->Release();
        return;
    }
    for (unsigned int Row = 0; Row < Rows; ++Row)
        std::memcpy(Theirs.data() + static_cast<std::size_t>(Row) * Pitch,
                    static_cast<const unsigned char*>(Locked.pBits) + Row * Locked.Pitch, Pitch);
    Surface->UnlockRect();
    Surface->Release();

    std::vector<unsigned char> Ours(Theirs.size());
    TexConvert::FImage Encoded;
    Encoded.Format = Format;
    Encoded.Width = Width;
    Encoded.Height = Height;
    Encoded.Pitch = Pitch;
    Encoded.Bits = Ours.data();
    TexConvert::FSource Straight;
    Straight.Format = TexConvert::kA8R8G8B8;
    Straight.Width = Width;
    Straight.Height = Height;
    Straight.Pitch = Width * 4;
    Straight.Bits = Image.data();
    TexConvert::Load(Encoded, Straight);

    // Both compressed images decoded the same way.
    std::vector<std::uint32_t> Back[2] = {std::vector<std::uint32_t>(Width * Height),
                                          std::vector<std::uint32_t>(Width * Height)};
    const std::vector<unsigned char>* Compressed[2] = {&Theirs, &Ours};
    for (int Side = 0; Side < 2; ++Side)
    {
        TexConvert::FImage Decoded;
        Decoded.Format = TexConvert::kA8R8G8B8;
        Decoded.Width = Width;
        Decoded.Height = Height;
        Decoded.Pitch = Width * 4;
        Decoded.Bits = Back[Side].data();
        TexConvert::FSource Source;
        Source.Format = Format;
        Source.Width = Width;
        Source.Height = Height;
        Source.Pitch = Pitch;
        Source.Bits = Compressed[Side]->data();
        TexConvert::Load(Decoded, Source);
    }
    int Largest = 0;
    for (std::size_t Index = 0; Index < Back[0].size(); ++Index)
    {
        for (int Shift = 0; Shift < 32; Shift += 8)
        {
            const int A = static_cast<int>((Back[0][Index] >> Shift) & 0xFF);
            const int B = static_cast<int>((Back[1][Index] >> Shift) & 0xFF);
            Largest = std::max(Largest, A > B ? A - B : B - A);
        }
    }
    const int Tolerance = 24;
    ++G_Compared;
    std::printf("ENCODE A8R8G8B8 -> %s: largest channel difference %d (tolerance %d)\n", Name(Format), Largest,
                Tolerance);
    if (Largest > Tolerance)
        ++G_Different;
}

// Checks without D3DX: re-encoding DXT2 / DXT4 into DXT2 / DXT4 must not darken the image,
// neither through repeated resizes nor from DXT2 into DXT4.
std::vector<std::uint32_t> DecodeToArgb(unsigned int Format, const std::vector<unsigned char>& Bits,
                                        unsigned int Width, unsigned int Height)
{
    std::vector<std::uint32_t> Pixels(Width * Height);
    TexConvert::FImage Image;
    Image.Format = TexConvert::kA8R8G8B8;
    Image.Width = Width;
    Image.Height = Height;
    Image.Pitch = Width * 4;
    Image.Bits = Pixels.data();
    TexConvert::FSource Source;
    Source.Format = Format;
    Source.Width = Width;
    Source.Height = Height;
    Source.Pitch = TexConvert::RowPitch(Format, Width);
    Source.Bits = Bits.data();
    TexConvert::Load(Image, Source);
    return Pixels;
}

std::vector<unsigned char> Reencode(unsigned int SrcFormat, const std::vector<unsigned char>& Bits,
                                    unsigned int SrcW, unsigned int SrcH, unsigned int DstFormat,
                                    unsigned int DstW, unsigned int DstH)
{
    std::vector<unsigned char> Out(static_cast<std::size_t>(TexConvert::RowPitch(DstFormat, DstW)) *
                                   TexConvert::RowCount(DstFormat, DstH));
    TexConvert::FImage Image;
    Image.Format = DstFormat;
    Image.Width = DstW;
    Image.Height = DstH;
    Image.Pitch = TexConvert::RowPitch(DstFormat, DstW);
    Image.Bits = Out.data();
    TexConvert::FSource Source;
    Source.Format = SrcFormat;
    Source.Width = SrcW;
    Source.Height = SrcH;
    Source.Pitch = TexConvert::RowPitch(SrcFormat, SrcW);
    Source.Bits = Bits.data();
    TexConvert::Load(Image, Source);
    return Out;
}

void SelfCheck(const char* What, std::uint32_t Expected, std::uint32_t Got, int Tolerance)
{
    int Largest = 0;
    for (int Shift = 0; Shift < 32; Shift += 8)
    {
        const int A = static_cast<int>((Expected >> Shift) & 0xFF);
        const int B = static_cast<int>((Got >> Shift) & 0xFF);
        Largest = std::max(Largest, A > B ? A - B : B - A);
    }
    ++G_SelfChecks;
    std::printf("SELF %s: %08x -> %08x, largest channel difference %d (tolerance %d)\n", What, Expected, Got,
                Largest, Tolerance);
    if (Largest > Tolerance)
        ++G_Different;
}

void RunSelfChecks()
{
    // A 16x16 DXT2 image of identical blocks: stored colour mid grey, alpha 8/15.
    const unsigned int Size = 16;
    std::vector<unsigned char> Dxt2(static_cast<std::size_t>(TexConvert::RowPitch(TexConvert::kDxt2, Size)) *
                                    TexConvert::RowCount(TexConvert::kDxt2, Size));
    for (std::size_t Offset = 0; Offset < Dxt2.size(); Offset += 16)
    {
        std::memset(&Dxt2[Offset], 0x88, 8);      // alpha 8 / 15 everywhere
        Dxt2[Offset + 8] = 0xEF;                  // colour 0 = 0x7BEF (mid grey)
        Dxt2[Offset + 9] = 0x7B;
        Dxt2[Offset + 10] = 0xEF;                 // colour 1 = the same
        Dxt2[Offset + 11] = 0x7B;
        std::memset(&Dxt2[Offset + 12], 0x00, 4); // every pixel colour 0
    }
    const std::uint32_t Start = DecodeToArgb(TexConvert::kDxt2, Dxt2, Size, Size)[0];
    // The baseline itself, worked out by hand so a decoder broken everywhere cannot pass: colour
    // 0x7BEF widens to R 123, G 125, B 123; alpha 8/15 is 136; DXT2 colour comes back multiplied
    // by alpha: 123 * 136 / 255 = 65.6 -> 66 (0x42), 125 * 136 / 255 = 66.7 -> 67 (0x43).
    SelfCheck("DXT2 block decoded", 0x88424342u, Start, 0);

    // Two halvings (16 to 8 to 4), each re-encoded into DXT2.
    std::vector<unsigned char> Current = Dxt2;
    unsigned int Width = Size;
    for (int Pass = 0; Pass < 2; ++Pass)
    {
        Current = Reencode(TexConvert::kDxt2, Current, Width, Width, TexConvert::kDxt2, Width / 2, Width / 2);
        Width /= 2;
    }
    SelfCheck("DXT2 resized twice into DXT2", Start, DecodeToArgb(TexConvert::kDxt2, Current, Width, Width)[0], 8);

    // DXT2 into DXT4 at the same size.
    const std::vector<unsigned char> Dxt4 = Reencode(TexConvert::kDxt2, Dxt2, Size, Size, TexConvert::kDxt4, Size, Size);
    SelfCheck("DXT2 into DXT4", Start, DecodeToArgb(TexConvert::kDxt4, Dxt4, Size, Size)[0], 8);
}

void RunProbes(IDirect3DDevice8* Device)
{
    // Endpoint pairs across the range in both orders; indices 0,1,2,3 in every row.
    const std::uint16_t Colors[] = {0x0000, 0x0001, 0x0020, 0x0800, 0x1234, 0x7bef, 0x8410, 0xa5d3, 0xf800,
                                    0x07e0, 0x001f, 0xffff, 0x39c7, 0xc618};
    const unsigned int Formats[] = {TexConvert::kDxt1, TexConvert::kDxt2, TexConvert::kDxt3,
                                    TexConvert::kDxt4, TexConvert::kDxt5};
    for (unsigned int Format : Formats)
    {
        for (std::uint16_t C0 : Colors)
        {
            for (std::uint16_t C1 : Colors)
            {
                unsigned char Block[16] = {};
                unsigned char* const Color = Format == TexConvert::kDxt1 ? Block : Block + 8;
                Color[0] = static_cast<unsigned char>(C0);
                Color[1] = static_cast<unsigned char>(C0 >> 8);
                Color[2] = static_cast<unsigned char>(C1);
                Color[3] = static_cast<unsigned char>(C1 >> 8);
                std::memset(Color + 4, 0xE4, 4);
                if (Format == TexConvert::kDxt2 || Format == TexConvert::kDxt3)
                {
                    for (int Index = 0; Index < 8; ++Index)
                        Block[Index] = static_cast<unsigned char>(0x10 * Index + 0x0F - Index);
                }
                else if (Format != TexConvert::kDxt1)
                {
                    // Alpha endpoints from the colour pair, both orders; indices 0..7 then 0..7.
                    Block[0] = static_cast<unsigned char>(C0 >> 3);
                    Block[1] = static_cast<unsigned char>(C1 >> 5);
                    const std::uint64_t Indices = 0xFAC688FAC688ull; // 0,1,2,3,4,5,6,7 twice
                    for (int Byte = 0; Byte < 6; ++Byte)
                        Block[2 + Byte] = static_cast<unsigned char>(Indices >> (Byte * 8));
                }
                Probe(Device, Format, Block);
            }
        }
    }
}
}

int main()
{
    // The self-checks need no device: they run, and count, even without one.
    RunSelfChecks();
    FDevice D3d;
    if (!CreateDevice(&D3d))
    {
        std::printf("TexConvertTest: no Direct3D 8 device; only the %d self-checks ran, %d different\n",
                    G_SelfChecks, G_Different);
        return G_Different != 0 ? 1 : 2;
    }
    RunProbes(D3d.Device);
    const unsigned int EncodeTargets[] = {TexConvert::kDxt1, TexConvert::kDxt2, TexConvert::kDxt3,
                                          TexConvert::kDxt4, TexConvert::kDxt5};
    for (unsigned int Format : EncodeTargets)
        CompareEncode(D3d.Device, Format);
    const unsigned int Sources[] = {TexConvert::kP8,       TexConvert::kR8G8B8,   TexConvert::kR5G6B5,
                                    TexConvert::kX1R5G5B5, TexConvert::kA4R4G4B4, TexConvert::kA1R5G5B5,
                                    TexConvert::kA8R8G8B8, TexConvert::kX8R8G8B8, TexConvert::kDxt1,
                                    TexConvert::kDxt2,     TexConvert::kDxt3,     TexConvert::kDxt4,
                                    TexConvert::kDxt5};
    const unsigned int Targets[] = {TexConvert::kR8G8B8,   TexConvert::kR5G6B5,   TexConvert::kX1R5G5B5,
                                    TexConvert::kA4R4G4B4, TexConvert::kA1R5G5B5, TexConvert::kA8R8G8B8,
                                    TexConvert::kX8R8G8B8};
    for (unsigned int Source : Sources)
    {
        for (int Pattern = 0; Pattern < 2; ++Pattern)
        {
            for (unsigned int Target : Targets)
                Compare(D3d.Device, Source, Target, 256, 64, Pattern);
            // A DXT image into its own format is a block copy.
            if (TexConvert::IsBlock(Source))
                Compare(D3d.Device, Source, Source, 256, 64, Pattern);
        }
    }
    // Shading's DDS pitch for wide and tall mips: linear size / width in blocks.
    const unsigned int DxtFormats[] = {TexConvert::kDxt1, TexConvert::kDxt5};
    const unsigned int Sizes[2][2] = {{256, 64}, {64, 256}};
    for (unsigned int Format : DxtFormats)
    {
        for (const auto& Size : Sizes)
        {
            const unsigned int WidthBlocks = Size[0] / 4;
            const unsigned int Linear = TexConvert::RowPitch(Format, Size[0]) * TexConvert::RowCount(Format, Size[1]);
            // The real block-row stride, the same on both sides: checks the layout of wide and tall
            // images on its own, apart from what D3DX does with Shading's pitch.
            Compare(D3d.Device, Format, TexConvert::kA8R8G8B8, Size[0], Size[1], 1);
            Compare(D3d.Device, Format, TexConvert::kA8R8G8B8, Size[0], Size[1], 1, Linear / WidthBlocks);
        }
    }

    std::printf("TexConvertTest: %d compared with D3DX, %d self-checks, %d different, %d skipped\n", G_Compared,
                G_SelfChecks, G_Different, G_Skipped);
    D3d.Device->Release();
    D3d.D3d->Release();
    if (D3d.Window != nullptr)
        DestroyWindow(D3d.Window);
    if (G_Different != 0)
        return 1;
    if (G_Compared == 0)
        return 2; // no comparison with D3DX completed
    return G_Different != 0 ? 1 : 0;
}
