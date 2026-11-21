// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#include "TexConvert.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace TexConvert
{
    namespace
    {
        // One pixel as D3DX holds it between formats: four channels in 0..1. D3DX 8.1 computes on
        // the x87, whose extra precision decides ties (TexConvertTest's probes), so TexConvert
        // keeps doubles, not floats.
        struct FColor
        {
            double R;
            double G;
            double B;
            double A;
        };

        std::uint16_t Read16(const unsigned char* P)
        {
            return static_cast<std::uint16_t>(P[0] | (P[1] << 8));
        }

        std::uint32_t Read32(const unsigned char* P)
        {
            return static_cast<std::uint32_t>(P[0]) | (static_cast<std::uint32_t>(P[1]) << 8) |
                   (static_cast<std::uint32_t>(P[2]) << 16) | (static_cast<std::uint32_t>(P[3]) << 24);
        }

        void Write16(unsigned char* P, std::uint32_t V)
        {
            P[0] = static_cast<unsigned char>(V);
            P[1] = static_cast<unsigned char>(V >> 8);
        }

        void Write32(unsigned char* P, std::uint32_t V)
        {
            P[0] = static_cast<unsigned char>(V);
            P[1] = static_cast<unsigned char>(V >> 8);
            P[2] = static_cast<unsigned char>(V >> 16);
            P[3] = static_cast<unsigned char>(V >> 24);
        }

        // An n-bit channel as a number in 0..1, and back rounded to nearest (half up).
        double Unpack(std::uint32_t Value, unsigned int Bits)
        {
            return static_cast<double>(Value) / static_cast<double>((1u << Bits) - 1);
        }

        std::uint32_t Pack(double Value, unsigned int Bits)
        {
            const double Max = static_cast<double>((1u << Bits) - 1);
            const double Scaled = std::min(1.0, std::max(0.0, Value)) * Max;
            return static_cast<std::uint32_t>(std::floor(Scaled + 0.5));
        }

        // A value D3DX keeps in a 32-bit float (its D3DXCOLOR channels), widened again.
        double AsFloat(double Value)
        {
            return static_cast<double>(static_cast<float>(Value));
        }

        FColor DecodePixel(unsigned int Format, const unsigned char* P, const unsigned char* PaletteRgb)
        {
            switch (Format)
            {
            case kP8:
            {
                const unsigned char* const Entry = PaletteRgb != nullptr ? PaletteRgb + P[0] * 3 : nullptr;
                if (Entry == nullptr)
                    return {0.0, 0.0, 0.0, 1.0};
                return {Unpack(Entry[0], 8), Unpack(Entry[1], 8), Unpack(Entry[2], 8), 1.0};
            }
            case kR8G8B8:
                return {Unpack(P[2], 8), Unpack(P[1], 8), Unpack(P[0], 8), 1.0};
            case kR5G6B5:
            {
                const std::uint32_t V = Read16(P);
                return {Unpack((V >> 11) & 0x1F, 5), Unpack((V >> 5) & 0x3F, 6), Unpack(V & 0x1F, 5), 1.0};
            }
            case kX1R5G5B5:
            case kA1R5G5B5:
            {
                const std::uint32_t V = Read16(P);
                const double A = Format == kA1R5G5B5 ? static_cast<double>((V >> 15) & 1) : 1.0;
                return {Unpack((V >> 10) & 0x1F, 5), Unpack((V >> 5) & 0x1F, 5), Unpack(V & 0x1F, 5), A};
            }
            case kA4R4G4B4:
            {
                const std::uint32_t V = Read16(P);
                return {Unpack((V >> 8) & 0xF, 4), Unpack((V >> 4) & 0xF, 4), Unpack(V & 0xF, 4),
                        Unpack((V >> 12) & 0xF, 4)};
            }
            case kA8R8G8B8:
            case kX8R8G8B8:
            {
                const std::uint32_t V = Read32(P);
                const double A = Format == kA8R8G8B8 ? Unpack(V >> 24, 8) : 1.0;
                return {Unpack((V >> 16) & 0xFF, 8), Unpack((V >> 8) & 0xFF, 8), Unpack(V & 0xFF, 8), A};
            }
            default:
                return {0.0, 0.0, 0.0, 1.0};
            }
        }

        void EncodePixel(unsigned int Format, unsigned char* P, const FColor& C)
        {
            switch (Format)
            {
            case kR8G8B8:
                P[0] = static_cast<unsigned char>(Pack(C.B, 8));
                P[1] = static_cast<unsigned char>(Pack(C.G, 8));
                P[2] = static_cast<unsigned char>(Pack(C.R, 8));
                break;
            case kR5G6B5:
                Write16(P, (Pack(C.R, 5) << 11) | (Pack(C.G, 6) << 5) | Pack(C.B, 5));
                break;
            case kX1R5G5B5:
                // D3DX 8.1 writes the unused bit as 0 (TexConvertTest).
                Write16(P, (Pack(C.R, 5) << 10) | (Pack(C.G, 5) << 5) | Pack(C.B, 5));
                break;
            case kA1R5G5B5:
                Write16(P, (Pack(C.A, 1) << 15) | (Pack(C.R, 5) << 10) | (Pack(C.G, 5) << 5) | Pack(C.B, 5));
                break;
            case kA4R4G4B4:
                Write16(P, (Pack(C.A, 4) << 12) | (Pack(C.R, 4) << 8) | (Pack(C.G, 4) << 4) | Pack(C.B, 4));
                break;
            case kA8R8G8B8:
                Write32(P, (Pack(C.A, 8) << 24) | (Pack(C.R, 8) << 16) | (Pack(C.G, 8) << 8) | Pack(C.B, 8));
                break;
            case kX8R8G8B8:
                // D3DX 8.1 writes the unused byte as 0 (TexConvertTest).
                Write32(P, (Pack(C.R, 8) << 16) | (Pack(C.G, 8) << 8) | Pack(C.B, 8));
                break;
            default:
                break;
            }
        }

        unsigned int BytesPerPixel(unsigned int Format)
        {
            switch (Format)
            {
            case kP8:
                return 1;
            case kR8G8B8:
                return 3;
            case kR5G6B5:
            case kX1R5G5B5:
            case kA4R4G4B4:
            case kA1R5G5B5:
                return 2;
            default:
                return 4;
            }
        }

        unsigned int BlockBytes(unsigned int Format)
        {
            return Format == kDxt1 ? 8u : 16u;
        }

        // --- DXT ---------------------------------------------------------------------------------

        // DXT decoding, as D3DX 8.1 does it (TexConvertTest's single-block probes): the endpoints
        // become floats (value / 31 or / 63), the in-between entries are interpolated in float,
        // and every channel is rounded to nearest when it is written. The three-colour mode (colour
        // 0 <= colour 1: a half-way entry and black) applies in every DXT format, not only DXT1.
        // Index 3 is transparent only in DXT1; the other formats take alpha from their alpha half.
        void DecodeColorBlock(const unsigned char* Block, bool bDxt1, FColor* Out)
        {
            const std::uint32_t C0 = Read16(Block);
            const std::uint32_t C1 = Read16(Block + 2);
            const std::uint32_t Indices = Read32(Block + 4);
            // The endpoints are 32-bit floats; the entries between them are computed in double
            // from those floats, the thirds as a + (b - a) / 3 (82 of 83 probes; the one left is
            // a midpoint D3DX rounds up where the same sum rounds down elsewhere).
            const double E0[3] = {AsFloat(Unpack((C0 >> 11) & 0x1F, 5)), AsFloat(Unpack((C0 >> 5) & 0x3F, 6)),
                                  AsFloat(Unpack(C0 & 0x1F, 5))};
            const double E1[3] = {AsFloat(Unpack((C1 >> 11) & 0x1F, 5)), AsFloat(Unpack((C1 >> 5) & 0x3F, 6)),
                                  AsFloat(Unpack(C1 & 0x1F, 5))};
            FColor Palette[4];
            Palette[0] = {E0[0], E0[1], E0[2], 1.0};
            Palette[1] = {E1[0], E1[1], E1[2], 1.0};
            if (C0 > C1)
            {
                Palette[2] = {E0[0] + (E1[0] - E0[0]) / 3.0, E0[1] + (E1[1] - E0[1]) / 3.0,
                              E0[2] + (E1[2] - E0[2]) / 3.0, 1.0};
                Palette[3] = {E1[0] + (E0[0] - E1[0]) / 3.0, E1[1] + (E0[1] - E1[1]) / 3.0,
                              E1[2] + (E0[2] - E1[2]) / 3.0, 1.0};
            }
            else
            {
                Palette[2] = {(E0[0] + E1[0]) / 2.0, (E0[1] + E1[1]) / 2.0, (E0[2] + E1[2]) / 2.0, 1.0};
                Palette[3] = {0.0, 0.0, 0.0, bDxt1 ? 0.0 : 1.0};
            }
            for (int Index = 0; Index < 16; ++Index)
                Out[Index] = Palette[(Indices >> (Index * 2)) & 3];
        }

        // D3DX 8.1 hands DXT2 and DXT4 back as straight alpha: colour divided by alpha (clamped to
        // 1), and black where alpha is 0 (TexConvertTest).
        void Unpremultiply(FColor* Pixels)
        {
            for (int Index = 0; Index < 16; ++Index)
            {
                const double A = Pixels[Index].A;
                if (A <= 0.0)
                {
                    Pixels[Index].R = 0.0;
                    Pixels[Index].G = 0.0;
                    Pixels[Index].B = 0.0;
                    continue;
                }
                Pixels[Index].R = std::min(1.0, AsFloat(Pixels[Index].R / A));
                Pixels[Index].G = std::min(1.0, AsFloat(Pixels[Index].G / A));
                Pixels[Index].B = std::min(1.0, AsFloat(Pixels[Index].B / A));
            }
        }

        // bKeepStored: DXT2 / DXT4 colour as stored, not multiplied by alpha, for re-encoding into
        // DXT2 / DXT4 (multiplying on every pass would darken the image each time).
        void DecodeBlock(unsigned int Format, const unsigned char* Block, FColor* Out, bool bKeepStored)
        {
            if (Format == kDxt1)
            {
                DecodeColorBlock(Block, true, Out);
                return;
            }
            DecodeColorBlock(Block + 8, false, Out);
            if (Format == kDxt2 || Format == kDxt3)
            {
                for (int Index = 0; Index < 16; ++Index)
                    Out[Index].A = Unpack((Block[Index / 2] >> ((Index & 1) * 4)) & 0xF, 4);
                if (Format == kDxt2 && !bKeepStored)
                    Unpremultiply(Out);
                return;
            }
            // DXT4 / DXT5: two alpha endpoints and 3-bit indices, interpolated in float like the
            // colour.
            const double A0 = Unpack(Block[0], 8);
            const double A1 = Unpack(Block[1], 8);
            double Alpha[8] = {A0, A1};
            if (Block[0] > Block[1])
            {
                for (int Step = 1; Step <= 6; ++Step)
                    Alpha[Step + 1] = ((7 - Step) * A0 + Step * A1) / 7.0;
            }
            else
            {
                for (int Step = 1; Step <= 4; ++Step)
                    Alpha[Step + 1] = ((5 - Step) * A0 + Step * A1) / 5.0;
                Alpha[6] = 0.0;
                Alpha[7] = 1.0;
            }
            std::uint64_t Bits = 0;
            for (int Byte = 0; Byte < 6; ++Byte)
                Bits |= static_cast<std::uint64_t>(Block[2 + Byte]) << (Byte * 8);
            for (int Index = 0; Index < 16; ++Index)
                Out[Index].A = Alpha[(Bits >> (Index * 3)) & 7];
            if (Format == kDxt4 && !bKeepStored)
                Unpremultiply(Out);
        }

        std::uint32_t Encode565(const double* C)
        {
            return (Pack(C[0], 5) << 11) | (Pack(C[1], 6) << 5) | Pack(C[2], 5);
        }

        double Distance(const double* A, const double* B)
        {
            const double R = A[0] - B[0];
            const double G = A[1] - B[1];
            const double Bl = A[2] - B[2];
            return R * R + G * G + Bl * Bl;
        }

        // A range-fit colour block: the bounding box's corners as endpoints, each pixel to the
        // nearest palette entry. DXT1 uses the three-colour mode when a pixel is transparent.
        void EncodeColorBlock(const FColor* In, bool bDxt1, unsigned char* Block)
        {
            double Min[3] = {1.0, 1.0, 1.0};
            double Max[3] = {0.0, 0.0, 0.0};
            bool bTransparent = false;
            for (int Index = 0; Index < 16; ++Index)
            {
                const double C[3] = {In[Index].R, In[Index].G, In[Index].B};
                if (bDxt1 && In[Index].A < 0.5)
                {
                    bTransparent = true;
                    continue;
                }
                for (int Ch = 0; Ch < 3; ++Ch)
                {
                    Min[Ch] = std::min(Min[Ch], C[Ch]);
                    Max[Ch] = std::max(Max[Ch], C[Ch]);
                }
            }
            if (Min[0] > Max[0])
            {
                Min[0] = Min[1] = Min[2] = 0.0;
                Max[0] = Max[1] = Max[2] = 0.0;
            }
            std::uint32_t C0 = Encode565(Max);
            std::uint32_t C1 = Encode565(Min);
            const bool bThreeColor = bDxt1 && bTransparent;
            if (bThreeColor ? C0 > C1 : C0 < C1)
                std::swap(C0, C1);
            if (!bThreeColor && C0 == C1)
            {
                // Four-colour mode needs colour 0 > colour 1.
                if (C1 > 0)
                    --C1;
                else
                    ++C0;
            }
            FColor Decoded[16];
            unsigned char Endpoints[8] = {};
            Write16(Endpoints, C0);
            Write16(Endpoints + 2, C1);
            // The four palette entries, decoded the same way as the decoder above.
            std::memset(Endpoints + 4, 0xE4, 4); // indices 0,1,2,3 in the first row
            DecodeColorBlock(Endpoints, bDxt1, Decoded);
            double Palette[4][3];
            for (int Entry = 0; Entry < 4; ++Entry)
            {
                Palette[Entry][0] = Decoded[Entry].R;
                Palette[Entry][1] = Decoded[Entry].G;
                Palette[Entry][2] = Decoded[Entry].B;
            }
            std::uint32_t Indices = 0;
            for (int Index = 0; Index < 16; ++Index)
            {
                std::uint32_t Best = 0;
                if (bThreeColor && In[Index].A < 0.5)
                {
                    Best = 3;
                }
                else
                {
                    const double C[3] = {In[Index].R, In[Index].G, In[Index].B};
                    double BestDistance = Distance(C, Palette[0]);
                    const std::uint32_t Count = bThreeColor ? 3u : 4u;
                    for (std::uint32_t Entry = 1; Entry < Count; ++Entry)
                    {
                        const double D = Distance(C, Palette[Entry]);
                        if (D < BestDistance)
                        {
                            BestDistance = D;
                            Best = Entry;
                        }
                    }
                }
                Indices |= Best << (Index * 2);
            }
            Write16(Block, C0);
            Write16(Block + 2, C1);
            Write32(Block + 4, Indices);
        }

        // D3DX 8.1 stores colour into DXT2 / DXT4 as it is given, without multiplying by alpha
        // (TexConvertTest's encode comparison), so every DXT2..5 target encodes its colour the same.
        void EncodeBlock(unsigned int Format, const FColor* In, unsigned char* Block)
        {
            if (Format == kDxt1)
            {
                EncodeColorBlock(In, true, Block);
                return;
            }
            EncodeColorBlock(In, false, Block + 8);
            if (Format == kDxt2 || Format == kDxt3)
            {
                std::memset(Block, 0, 8);
                for (int Index = 0; Index < 16; ++Index)
                    Block[Index / 2] |= static_cast<unsigned char>(Pack(In[Index].A, 4) << ((Index & 1) * 4));
                return;
            }
            // DXT4 / DXT5: the alpha range in eight steps.
            double AMin = 1.0;
            double AMax = 0.0;
            for (int Index = 0; Index < 16; ++Index)
            {
                AMin = std::min(AMin, In[Index].A);
                AMax = std::max(AMax, In[Index].A);
            }
            std::uint32_t A0 = Pack(AMax, 8);
            std::uint32_t A1 = Pack(AMin, 8);
            if (A0 == A1)
            {
                if (A1 > 0)
                    --A1;
                else
                    ++A0;
            }
            Block[0] = static_cast<unsigned char>(A0);
            Block[1] = static_cast<unsigned char>(A1);
            double Alpha[8] = {Unpack(A0, 8), Unpack(A1, 8)};
            for (int Step = 1; Step <= 6; ++Step)
                Alpha[Step + 1] = ((7 - Step) * Alpha[0] + Step * Alpha[1]) / 7.0;
            std::uint64_t Bits = 0;
            for (int Index = 0; Index < 16; ++Index)
            {
                std::uint64_t Best = 0;
                double BestDistance = 2.0;
                for (std::uint64_t Entry = 0; Entry < 8; ++Entry)
                {
                    const double D = std::abs(In[Index].A - Alpha[Entry]);
                    if (D < BestDistance)
                    {
                        BestDistance = D;
                        Best = Entry;
                    }
                }
                Bits |= Best << (Index * 3);
            }
            for (int Byte = 0; Byte < 6; ++Byte)
                Block[2 + Byte] = static_cast<unsigned char>(Bits >> (Byte * 8));
        }

        // --- whole images ------------------------------------------------------------------------

        std::vector<FColor> DecodeImage(const FSource& Source, bool bKeepStored)
        {
            std::vector<FColor> Pixels(static_cast<std::size_t>(Source.Width) * Source.Height);
            const unsigned char* const Base = static_cast<const unsigned char*>(Source.Bits);
            if (IsBlock(Source.Format))
            {
                const unsigned int Bytes = BlockBytes(Source.Format);
                for (unsigned int By = 0; By < RowCount(Source.Format, Source.Height); ++By)
                {
                    for (unsigned int Bx = 0; Bx < (Source.Width + 3) / 4; ++Bx)
                    {
                        FColor Block[16];
                        DecodeBlock(Source.Format, Base + static_cast<std::size_t>(By) * Source.Pitch + Bx * Bytes, Block,
                                    bKeepStored);
                        for (unsigned int Index = 0; Index < 16; ++Index)
                        {
                            const unsigned int X = Bx * 4 + Index % 4;
                            const unsigned int Y = By * 4 + Index / 4;
                            if (X < Source.Width && Y < Source.Height)
                                Pixels[static_cast<std::size_t>(Y) * Source.Width + X] = Block[Index];
                        }
                    }
                }
                return Pixels;
            }
            const unsigned int Bpp = BytesPerPixel(Source.Format);
            for (unsigned int Y = 0; Y < Source.Height; ++Y)
            {
                const unsigned char* const Row = Base + static_cast<std::size_t>(Y) * Source.Pitch;
                for (unsigned int X = 0; X < Source.Width; ++X)
                    Pixels[static_cast<std::size_t>(Y) * Source.Width + X] =
                        DecodePixel(Source.Format, Row + X * Bpp, Source.PaletteRgb);
            }
            return Pixels;
        }

        void EncodeImage(const FImage& Destination, const std::vector<FColor>& Pixels)
        {
            unsigned char* const Base = static_cast<unsigned char*>(Destination.Bits);
            if (IsBlock(Destination.Format))
            {
                const unsigned int Bytes = BlockBytes(Destination.Format);
                for (unsigned int By = 0; By < RowCount(Destination.Format, Destination.Height); ++By)
                {
                    for (unsigned int Bx = 0; Bx < (Destination.Width + 3) / 4; ++Bx)
                    {
                        // Pixels outside the image repeat the nearest edge pixel.
                        FColor Block[16];
                        for (unsigned int Index = 0; Index < 16; ++Index)
                        {
                            const unsigned int X = std::min(Bx * 4 + Index % 4, Destination.Width - 1);
                            const unsigned int Y = std::min(By * 4 + Index / 4, Destination.Height - 1);
                            Block[Index] = Pixels[static_cast<std::size_t>(Y) * Destination.Width + X];
                        }
                        EncodeBlock(Destination.Format, Block,
                                    Base + static_cast<std::size_t>(By) * Destination.Pitch + Bx * Bytes);
                    }
                }
                return;
            }
            const unsigned int Bpp = BytesPerPixel(Destination.Format);
            for (unsigned int Y = 0; Y < Destination.Height; ++Y)
            {
                unsigned char* const Row = Base + static_cast<std::size_t>(Y) * Destination.Pitch;
                for (unsigned int X = 0; X < Destination.Width; ++X)
                    EncodePixel(Destination.Format, Row + X * Bpp,
                                Pixels[static_cast<std::size_t>(Y) * Destination.Width + X]);
            }
        }

        // An area-weighted (box) resample: each destination pixel averages the source area it
        // covers, partial pixels by their covered fraction.
        std::vector<FColor> Resize(const std::vector<FColor>& In, unsigned int InW, unsigned int InH,
                                   unsigned int OutW, unsigned int OutH)
        {
            std::vector<FColor> Out(static_cast<std::size_t>(OutW) * OutH);
            const double ScaleX = static_cast<double>(InW) / OutW;
            const double ScaleY = static_cast<double>(InH) / OutH;
            for (unsigned int Y = 0; Y < OutH; ++Y)
            {
                const double Y0 = Y * ScaleY;
                const double Y1 = Y0 + ScaleY;
                for (unsigned int X = 0; X < OutW; ++X)
                {
                    const double X0 = X * ScaleX;
                    const double X1 = X0 + ScaleX;
                    double Sum[4] = {};
                    double Weight = 0.0;
                    for (unsigned int Sy = static_cast<unsigned int>(Y0); Sy < InH && Sy < Y1; ++Sy)
                    {
                        const double Wy = std::min<double>(Sy + 1, Y1) - std::max<double>(Sy, Y0);
                        for (unsigned int Sx = static_cast<unsigned int>(X0); Sx < InW && Sx < X1; ++Sx)
                        {
                            const double W = Wy * (std::min<double>(Sx + 1, X1) - std::max<double>(Sx, X0));
                            const FColor& C = In[static_cast<std::size_t>(Sy) * InW + Sx];
                            Sum[0] += C.R * W;
                            Sum[1] += C.G * W;
                            Sum[2] += C.B * W;
                            Sum[3] += C.A * W;
                            Weight += W;
                        }
                    }
                    if (Weight > 0.0)
                    {
                        Out[static_cast<std::size_t>(Y) * OutW + X] = {
                            Sum[0] / Weight, Sum[1] / Weight, Sum[2] / Weight, Sum[3] / Weight};
                    }
                }
            }
            return Out;
        }
    }

    bool IsKnown(unsigned int Format)
    {
        return (Format >= kP8 && Format <= kX8R8G8B8) || (Format >= kDxt1 && Format <= kDxt5);
    }

    bool IsBlock(unsigned int Format)
    {
        return Format >= kDxt1 && Format <= kDxt5;
    }

    unsigned int RowPitch(unsigned int Format, unsigned int Width)
    {
        if (IsBlock(Format))
            return std::max(1u, (Width + 3) / 4) * BlockBytes(Format);
        return Width * BytesPerPixel(Format);
    }

    unsigned int RowCount(unsigned int Format, unsigned int Height)
    {
        return IsBlock(Format) ? std::max(1u, (Height + 3) / 4) : Height;
    }

    EResult Load(const FImage& Destination, const FSource& Original)
    {
        // A DXT source is read with its own block-row stride, whatever pitch the caller passed:
        // Shading's DDS loader passes linear size / width in blocks, which is the stride only for
        // a square block grid (TexConvertTest compares that case with D3DX).
        FSource Source = Original;
        if (IsBlock(Source.Format))
            Source.Pitch = RowPitch(Source.Format, Source.Width);

        if (!IsKnown(Destination.Format) || !IsKnown(Source.Format) || Destination.Bits == nullptr || Source.Bits == nullptr || Destination.Width == 0 ||
            Destination.Height == 0 || Source.Width == 0 || Source.Height == 0)
            return EResult::Failed;
        // The working buffer holds one FColor per source and per destination pixel: sizes whose
        // pixel count does not fit (Win32's 32-bit size_t) are refused rather than overflowing.
        const std::size_t MaxPixels = static_cast<std::size_t>(-1) / sizeof(FColor);
        if (Source.Width > MaxPixels / Source.Height || Destination.Width > MaxPixels / Destination.Height)
            return EResult::Failed;
        // An uncompressed source must hold a whole row per pitch: a smaller pitch would make rows
        // overlap and read past the row being converted. Padded rows are fine.
        if (!IsBlock(Source.Format) && Source.Pitch < RowPitch(Source.Format, Source.Width))
            return EResult::Failed;

        const bool bSameSize = Source.Width == Destination.Width && Source.Height == Destination.Height;

        // The same format at the same size: the bytes as they are, unused bits included, as D3DX
        // 8.1 copies them (TexConvertTest); for DXT, the blocks.
        if (bSameSize && Source.Format == Destination.Format)
        {
            const unsigned int RowBytes = RowPitch(Source.Format, Source.Width);
            for (unsigned int Row = 0; Row < RowCount(Source.Format, Source.Height); ++Row)
                std::memcpy(static_cast<unsigned char*>(Destination.Bits) + static_cast<std::size_t>(Row) * Destination.Pitch,
                            static_cast<const unsigned char*>(Source.Bits) + static_cast<std::size_t>(Row) * Source.Pitch,
                            RowBytes);
            return EResult::Exact;
        }

        // A P8 target is only filled by a P8 source of the same size (the copy above): there is no
        // palette quantizer. Expanding P8 indices into colours needs the palette.
        if (Destination.Format == kP8 || (Source.Format == kP8 && Source.PaletteRgb == nullptr))
            return EResult::Failed;

        // DXT2 / DXT4 into DXT2 / DXT4: the stored (premultiplied) colour passes through as it is.
        // Every other read of DXT2 / DXT4 divides colour by alpha, as D3DX does.
        const bool bSourcePremultiplied = Source.Format == kDxt2 || Source.Format == kDxt4;
        const bool bTargetPremultiplied = Destination.Format == kDxt2 || Destination.Format == kDxt4;
        const bool bKeepStored = bSourcePremultiplied && bTargetPremultiplied;
        std::vector<FColor> Pixels = DecodeImage(Source, bKeepStored);
        if (!bSameSize)
            Pixels = Resize(Pixels, Source.Width, Source.Height, Destination.Width, Destination.Height);
        // DXT2 / DXT4 colour is stored as given (D3DX does not multiply it by alpha on encode).
        EncodeImage(Destination, Pixels);
        return (bSameSize && !IsBlock(Destination.Format)) ? EResult::Exact : EResult::Filtered;
    }
}
