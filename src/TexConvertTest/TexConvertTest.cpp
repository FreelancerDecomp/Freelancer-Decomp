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

#include <windows.h>

#include <d3d8.h>
#include <d3dx8.h>

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

    int G_Compared = 0;
    int G_Different = 0;
    int G_Skipped = 0;

    // SrcPitch 0: the format's own pitch; otherwise the pitch passed to both converters.
    void Compare(IDirect3DDevice8* Device, unsigned int SrcFormat, unsigned int DstFormat, unsigned int Width,
                 unsigned int Height, int Pattern, unsigned int SrcPitchOverride = 0)
    {
        const std::vector<unsigned char> Source = MakeSource(SrcFormat, Width, Height, Pattern);
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

int main()
{
    FDevice D3d;
    if (!CreateDevice(&D3d))
    {
        std::printf("TexConvertTest: no Direct3D 8 device; nothing compared\n");
        return 2;
    }
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
            Compare(D3d.Device, Format, TexConvert::kA8R8G8B8, Size[0], Size[1], 1, Linear / WidthBlocks);
        }
    }

    std::printf("TexConvertTest: %d compared, %d different, %d skipped\n", G_Compared, G_Different, G_Skipped);
    D3d.Device->Release();
    D3d.D3d->Release();
    if (D3d.Window != nullptr)
        DestroyWindow(D3d.Window);
    if (G_Compared == 0)
        return 2;
    return G_Different != 0 ? 1 : 0;
}
