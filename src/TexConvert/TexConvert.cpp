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
        // One pixel as D3DX holds it between formats: four floats in 0..1.
        struct FColor
        {
            float R;
            float G;
            float B;
            float A;
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

        // An n-bit channel as a float, and back with round-to-nearest.
        float Unpack(std::uint32_t Value, unsigned int Bits)
        {
            return static_cast<float>(Value) / static_cast<float>((1u << Bits) - 1);
        }

        std::uint32_t Pack(float Value, unsigned int Bits)
        {
            const float Max = static_cast<float>((1u << Bits) - 1);
            const float Clamped = std::min(1.0f, std::max(0.0f, Value));
            return static_cast<std::uint32_t>(Clamped * Max + 0.5f);
        }

        FColor DecodePixel(unsigned int Format, const unsigned char* P, const unsigned char* PaletteRgb)
        {
            switch (Format)
            {
            case kP8:
            {
                const unsigned char* const Entry = PaletteRgb != nullptr ? PaletteRgb + P[0] * 3 : nullptr;
                if (Entry == nullptr)
                    return {0.0f, 0.0f, 0.0f, 1.0f};
                return {Unpack(Entry[0], 8), Unpack(Entry[1], 8), Unpack(Entry[2], 8), 1.0f};
            }
            case kR8G8B8:
                return {Unpack(P[2], 8), Unpack(P[1], 8), Unpack(P[0], 8), 1.0f};
            case kR5G6B5:
            {
                const std::uint32_t V = Read16(P);
                return {Unpack((V >> 11) & 0x1F, 5), Unpack((V >> 5) & 0x3F, 6), Unpack(V & 0x1F, 5), 1.0f};
            }
            case kX1R5G5B5:
            case kA1R5G5B5:
            {
                const std::uint32_t V = Read16(P);
                const float A = Format == kA1R5G5B5 ? static_cast<float>((V >> 15) & 1) : 1.0f;
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
                const float A = Format == kA8R8G8B8 ? Unpack(V >> 24, 8) : 1.0f;
                return {Unpack((V >> 16) & 0xFF, 8), Unpack((V >> 8) & 0xFF, 8), Unpack(V & 0xFF, 8), A};
            }
            default:
                return {0.0f, 0.0f, 0.0f, 1.0f};
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

        // DXT decoding, as D3DX 8.1 does it (TexConvertTest): endpoints widened to 8 bits by
        // repeating their top bits, the in-between entries interpolated in integers and rounded
        // down, and the three-colour mode (colour 0 <= colour 1: half-way entry and black) in every
        // DXT format, not only DXT1. Index 3 is transparent only in DXT1; the other formats take
        // alpha from their alpha half.
        void Widen565(std::uint32_t V, std::uint32_t* Out)
        {
            const std::uint32_t R = (V >> 11) & 0x1F;
            const std::uint32_t G = (V >> 5) & 0x3F;
            const std::uint32_t B = V & 0x1F;
            Out[0] = (R << 3) | (R >> 2);
            Out[1] = (G << 2) | (G >> 4);
            Out[2] = (B << 3) | (B >> 2);
        }

        void DecodeColorBlock(const unsigned char* Block, bool bDxt1, FColor* Out)
        {
            const std::uint32_t C0 = Read16(Block);
            const std::uint32_t C1 = Read16(Block + 2);
            const std::uint32_t Indices = Read32(Block + 4);
            std::uint32_t Palette[4][4];
            Widen565(C0, Palette[0]);
            Widen565(C1, Palette[1]);
            Palette[0][3] = 255;
            Palette[1][3] = 255;
            Palette[2][3] = 255;
            if (C0 > C1)
            {
                for (int Ch = 0; Ch < 3; ++Ch)
                {
                    Palette[2][Ch] = (2 * Palette[0][Ch] + Palette[1][Ch]) / 3;
                    Palette[3][Ch] = (Palette[0][Ch] + 2 * Palette[1][Ch]) / 3;
                }
                Palette[3][3] = 255;
            }
            else
            {
                for (int Ch = 0; Ch < 3; ++Ch)
                {
                    Palette[2][Ch] = (Palette[0][Ch] + Palette[1][Ch]) / 2;
                    Palette[3][Ch] = 0;
                }
                Palette[3][3] = bDxt1 ? 0 : 255;
            }
            for (int Index = 0; Index < 16; ++Index)
            {
                const std::uint32_t* const P = Palette[(Indices >> (Index * 2)) & 3];
                Out[Index] = {Unpack(P[0], 8), Unpack(P[1], 8), Unpack(P[2], 8), Unpack(P[3], 8)};
            }
        }

        // D3DX 8.1 hands DXT2 and DXT4 colour back multiplied by alpha (TexConvertTest), so the
        // decoder does the same.
        void Premultiply(FColor* Pixels)
        {
            for (int Index = 0; Index < 16; ++Index)
            {
                Pixels[Index].R *= Pixels[Index].A;
                Pixels[Index].G *= Pixels[Index].A;
                Pixels[Index].B *= Pixels[Index].A;
            }
        }

        void DecodeBlock(unsigned int Format, const unsigned char* Block, FColor* Out)
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
                if (Format == kDxt2)
                    Premultiply(Out);
                return;
            }
            // DXT4 / DXT5: two alpha endpoints and 3-bit indices, interpolated in integers.
            const std::uint32_t A0 = Block[0];
            const std::uint32_t A1 = Block[1];
            std::uint32_t Alpha[8] = {A0, A1};
            if (A0 > A1)
            {
                for (std::uint32_t Step = 1; Step <= 6; ++Step)
                    Alpha[Step + 1] = ((7 - Step) * A0 + Step * A1) / 7;
            }
            else
            {
                for (std::uint32_t Step = 1; Step <= 4; ++Step)
                    Alpha[Step + 1] = ((5 - Step) * A0 + Step * A1) / 5;
                Alpha[6] = 0;
                Alpha[7] = 255;
            }
            std::uint64_t Bits = 0;
            for (int Byte = 0; Byte < 6; ++Byte)
                Bits |= static_cast<std::uint64_t>(Block[2 + Byte]) << (Byte * 8);
            for (int Index = 0; Index < 16; ++Index)
                Out[Index].A = Unpack(Alpha[(Bits >> (Index * 3)) & 7], 8);
            if (Format == kDxt4)
                Premultiply(Out);
        }

        std::uint32_t Encode565(const float* C)
        {
            return (Pack(C[0], 5) << 11) | (Pack(C[1], 6) << 5) | Pack(C[2], 5);
        }

        float Distance(const float* A, const float* B)
        {
            const float R = A[0] - B[0];
            const float G = A[1] - B[1];
            const float Bl = A[2] - B[2];
            return R * R + G * G + Bl * Bl;
        }

        // A range-fit colour block: the bounding box's corners as endpoints, each pixel to the
        // nearest palette entry. DXT1 uses the three-colour mode when a pixel is transparent.
        void EncodeColorBlock(const FColor* In, bool bDxt1, unsigned char* Block)
        {
            float Min[3] = {1.0f, 1.0f, 1.0f};
            float Max[3] = {0.0f, 0.0f, 0.0f};
            bool bTransparent = false;
            for (int Index = 0; Index < 16; ++Index)
            {
                const float C[3] = {In[Index].R, In[Index].G, In[Index].B};
                if (bDxt1 && In[Index].A < 0.5f)
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
                Min[0] = Min[1] = Min[2] = 0.0f;
                Max[0] = Max[1] = Max[2] = 0.0f;
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
            float Palette[4][3];
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
                if (bThreeColor && In[Index].A < 0.5f)
                {
                    Best = 3;
                }
                else
                {
                    const float C[3] = {In[Index].R, In[Index].G, In[Index].B};
                    float BestDistance = Distance(C, Palette[0]);
                    const std::uint32_t Count = bThreeColor ? 3u : 4u;
                    for (std::uint32_t Entry = 1; Entry < Count; ++Entry)
                    {
                        const float D = Distance(C, Palette[Entry]);
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

        void EncodeBlock(unsigned int Format, const FColor* In, unsigned char* Block)
        {
            if (Format == kDxt1)
            {
                EncodeColorBlock(In, true, Block);
                return;
            }
            if (Format == kDxt2 || Format == kDxt4)
            {
                FColor Premultiplied[16];
                for (int Index = 0; Index < 16; ++Index)
                    Premultiplied[Index] = {In[Index].R * In[Index].A, In[Index].G * In[Index].A,
                                            In[Index].B * In[Index].A, In[Index].A};
                EncodeColorBlock(Premultiplied, false, Block + 8);
            }
            else
            {
                EncodeColorBlock(In, false, Block + 8);
            }
            if (Format == kDxt2 || Format == kDxt3)
            {
                std::memset(Block, 0, 8);
                for (int Index = 0; Index < 16; ++Index)
                    Block[Index / 2] |= static_cast<unsigned char>(Pack(In[Index].A, 4) << ((Index & 1) * 4));
                return;
            }
            // DXT4 / DXT5: the alpha range in eight steps.
            float AMin = 1.0f;
            float AMax = 0.0f;
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
            float Alpha[8] = {Unpack(A0, 8), Unpack(A1, 8)};
            for (int Step = 1; Step <= 6; ++Step)
                Alpha[Step + 1] = ((7 - Step) * Alpha[0] + Step * Alpha[1]) / 7.0f;
            std::uint64_t Bits = 0;
            for (int Index = 0; Index < 16; ++Index)
            {
                std::uint64_t Best = 0;
                float BestDistance = 2.0f;
                for (std::uint64_t Entry = 0; Entry < 8; ++Entry)
                {
                    const float D = std::abs(In[Index].A - Alpha[Entry]);
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

        std::vector<FColor> DecodeImage(const FSource& Source)
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
                        DecodeBlock(Source.Format, Base + static_cast<std::size_t>(By) * Source.Pitch + Bx * Bytes, Block);
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
                            static_cast<float>(Sum[0] / Weight), static_cast<float>(Sum[1] / Weight),
                            static_cast<float>(Sum[2] / Weight), static_cast<float>(Sum[3] / Weight)};
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

        if (!IsKnown(Destination.Format) || !IsKnown(Source.Format) || Destination.Format == kP8 ||
            Destination.Bits == nullptr || Source.Bits == nullptr || Destination.Width == 0 ||
            Destination.Height == 0 || Source.Width == 0 || Source.Height == 0)
            return EResult::Failed;
        if (Source.Format == kP8 && Source.PaletteRgb == nullptr)
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

        std::vector<FColor> Pixels = DecodeImage(Source);
        if (!bSameSize)
            Pixels = Resize(Pixels, Source.Width, Source.Height, Destination.Width, Destination.Height);
        EncodeImage(Destination, Pixels);
        return (bSameSize && !IsBlock(Destination.Format)) ? EResult::Exact : EResult::Filtered;
    }
}
