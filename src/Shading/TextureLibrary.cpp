// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "portable_integer_conversion.h"
#include "portable_vector_load.h"
#include "TextureLibrary.h"
#include "msvcrt_compat.h" // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <d3dx8tex.h>

namespace
{
    static const unsigned long kFourCcDxt1 = 0x31545844u;
    static const unsigned long kFourCcDxt2 = 0x32545844u;
    static const unsigned long kFourCcDxt3 = 0x33545844u;
    static const unsigned long kFourCcDxt4 = 0x34545844u;
    static const unsigned long kFourCcDxt5 = 0x35545844u;
    static const unsigned long kFourCcDaop = 0x504F4144u;
    static const unsigned long kFourCcDaot = 0x544F4144u;
    static const unsigned long kFourCcDaa1 = 0x31414144u;
    static const unsigned long kFourCcDaa4 = 0x34414144u;
    static const unsigned long kFourCcDaa8 = 0x38414144u;

    // DAT_06ed57c4 stride 0x34. FUN_06ebea90 pairs (value, mask=-1 if set).
    struct FTextureFormatMap
    {
        unsigned long Value[6];
        unsigned long Mask[6];
        unsigned long FourCc;
    };

    std::vector<FTextureFormatMap> GFormatMaps;

    // Profile numbers accept a narrower syntax than general C integer parsing.
    char GProfileValue[0x401]; // DAT_06ed5860 shared profile scratch.

    unsigned long ParseRetailProfileInteger(char* Value)
    {
        const size_t Span = std::strspn(Value, "0123456789truefalsynoxTRUEFALSYNOX+-");
        if (Span != 0)
            Value[Span] = '\0';
        if (_stricmp(Value, "true") == 0 || _stricmp(Value, "yes") == 0)
            return 1;
        if (_stricmp(Value, "false") == 0 || _stricmp(Value, "no") == 0)
            return 0;
        if (std::strchr(Value, 'x') != nullptr || std::strchr(Value, 'X') != nullptr)
            return std::strtoul(Value, nullptr, 16);
        return (unsigned long)MsvcrtAtoi(Value);
    }

    // FUN_06ec09a0: the supplied output survives a failed parser acquisition.
    unsigned long ReadProfileInteger(ICOManager* Manager, IProfileParser* SuppliedParser,
                                     const char* SectionName, const char* Key,
                                     unsigned long DefaultValue, unsigned long* Out)
    {
        unsigned long LocalValue;
        unsigned long* Value = Out != nullptr ? Out : &LocalValue;
        IProfileParser* Parser = SuppliedParser;
        if (Parser != nullptr)
            Parser->AddRef();
        else if (Manager->QueryInterface(Iids::ProfileParser,
                                        (void**)&Parser) != GR_OK)
        {
            const unsigned long Result = *Value;
            if (Parser != nullptr)
                Parser->Release();
            return Result;
        }
        *Value = DefaultValue;
        void* Section = Parser->OpenSection(SectionName, 3);
        if (Section != nullptr)
        {
            if (Parser->GetString(Section, Key, GProfileValue, 0x400) != 0)
                *Value = ParseRetailProfileInteger(GProfileValue);
            Parser->CloseSection(Section);
        }
        const unsigned long Result = *Value;
        if (Parser != nullptr)
            Parser->Release();
        return Result;
    }

    void FormatMapClear()
    {
        GFormatMaps.clear();
    }

    void FormatMapAppend(int Field0, int Field1, int Field2, int Field3, int Field4, int Field5,
                         unsigned long FourCc)
    {
        FTextureFormatMap Entry;
        std::memset(&Entry, 0, sizeof(Entry));
        const int Fields[6] = {Field0, Field1, Field2, Field3, Field4, Field5};
        for (int Index = 0; Index < 6; ++Index)
        {
            if (Fields[Index] >= 0)
            {
                Entry.Value[Index] = (unsigned long)Fields[Index];
                Entry.Mask[Index] = 0xFFFFFFFFu;
            }
        }
        Entry.FourCc = FourCc;
        GFormatMaps.push_back(Entry);
    }

    void FormatMapLoadDefaults()
    {
        // FUN_06ebec10 hardcoded rows before IProfileParser "TextureFormatMaps".
        FormatMapClear();
        FormatMapAppend(8, 8, 8, 8, -1, 0, kFourCcDaop);
        FormatMapAppend(-1, -1, -1, -1, 1, 0, kFourCcDaa1);
        FormatMapAppend(-1, -1, -1, -1, 4, 0, kFourCcDaa4);
        FormatMapAppend(-1, -1, -1, -1, 8, 0, kFourCcDaa8);
        FormatMapAppend(-1, -1, -1, -1, -1, 0, kFourCcDaot);
        FormatMapAppend(-1, -1, -1, -1, -1, 1, kFourCcDaa1);
        FormatMapAppend(-1, -1, -1, -1, -1, 4, kFourCcDaa4);
        FormatMapAppend(-1, -1, -1, -1, -1, -1, kFourCcDaa8);
    }

    void SkipFormatMapCharacters(char*& Cursor, const char* Characters)
    {
        while (*Cursor != '\0' && std::strchr(Characters, *Cursor) != nullptr)
            ++Cursor;
    }

    int ParseFormatMapField(char*& Cursor, const char* Whitespace, bool bAdvance)
    {
        // ec10's six inlined scans retain token start before skipping whitespace.
        char* Start = Cursor;
        SkipFormatMapCharacters(Cursor, Whitespace);
        SkipFormatMapCharacters(Cursor, "1234567890*");
        *Cursor = '\0';
        if (bAdvance)
            ++Cursor; // Includes a NUL terminator: retail does not restore it.
        return std::strchr(Start, '*') == nullptr ? MsvcrtAtoi(Start) : -1;
    }

    void FormatMapParseLine(char* Line)
    {
        char* Semi = std::strchr(Line, ';');
        if (Semi != nullptr)
            *Semi = '\0';
        char* Key = Line;
        SkipFormatMapCharacters(Key, " \t");
        if (_strnicmp(Key, "clear", 3) == 0)
        {
            FormatMapClear();
            return;
        }
        char* Value = Key;
        while (*Value != '\0' && *Value != '=')
            ++Value;
        *Value = '\0';
        ++Value;
        SkipFormatMapCharacters(Value, " \t");
        if (*Value == '\0')
            return;

        char* Field = Key;
        const int F0 = ParseFormatMapField(Field, " \t", true);
        const int F1 = ParseFormatMapField(Field, " \t:", true);
        const int F2 = ParseFormatMapField(Field, " \t:", true);
        const int F3 = ParseFormatMapField(Field, " \t:", true);
        const int F4 = ParseFormatMapField(Field, " \t:", true);
        const int F5 = ParseFormatMapField(Field, " \t:", false);
        const unsigned long FourCc = (unsigned char)Value[0] |
            ((unsigned long)(unsigned char)Value[1] << 8) |
            ((unsigned long)(unsigned char)Value[2] << 16) |
            ((unsigned long)(unsigned char)Value[3] << 24);
        FormatMapAppend(F0, F1, F2, F3, F4, F5, FourCc);
    }

    void FormatMapLoadFromParser(IProfileParser* Parser)
    {
        void* Section = Parser->OpenSection("TextureFormatMaps", 3);
        if (Section == nullptr)
            return;
        char Line[1024]; // Retail ReadLine buffer is not cleared between reads.
        unsigned long LineIndex = 0;
        while (Parser->ReadLine(Section, LineIndex, Line, sizeof(Line)) != 0)
        {
            ++LineIndex;
            FormatMapParseLine(Line);
        }
        Parser->CloseSection(Section);
    }

    GENRESULT InitializeFormatMaps()
    {
        // FUN_06ebec10: defaults precede the independent profile-parser QI.
        FormatMapLoadDefaults();
        ICOManager* Manager = DACOM_Acquire();
        IProfileParser* Parser = nullptr;
        if (Manager->QueryInterface(Iids::ProfileParser,
                                    (void**)&Parser) >= 0)
            FormatMapLoadFromParser(Parser);
        if (Parser != nullptr)
            Parser->Release();
        return GR_OK;
    }

    int FourCcToFormatId(unsigned long FourCc)
    {
        // FUN_06ed0b80: scan DAT_06ed51d8 ids 9…0x15, miss → 0.
        for (int Index = 9; Index < kPixelFormatFourCcCount; ++Index)
        {
            if (kPixelFormatFourCc[Index] == FourCc)
                return Index;
        }
        return 0;
    }

    struct FTextureFormatChannel
    {
        unsigned long BitCount;
        unsigned long Mask;
        unsigned long Shift;
        unsigned long ExpansionShift;
    };

    // Full private DAT_06ed5230 record, including masks and shifts. This is
    // texture conversion data, not a VC6 container or a cross-DLL structure.
    struct FTextureFormatDescriptor
    {
        unsigned long Id;
        unsigned long BitsPerPixel;
        FTextureFormatChannel Red;
        FTextureFormatChannel Green;
        FTextureFormatChannel Blue;
        FTextureFormatChannel Alpha;
        unsigned char bHasAlpha;
        unsigned char Padding[3];
    };
    static_assert(sizeof(FTextureFormatDescriptor) == 0x4c, "Texture format descriptor");

    const FTextureFormatDescriptor GTextureFormatDescriptors[15] =
    {
        {0u, 0u, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, 0, {0, 0, 0}},
        {1u, 8u, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, 0, {0, 0, 0}},
        {2u, 24u, {8u, 16711680u, 16u, 0u}, {8u, 65280u, 8u, 0u}, {8u, 255u, 0u, 0u}, {0u, 0u, 0u, 0u}, 0, {0, 0, 0}},
        {3u, 16u, {5u, 63488u, 11u, 3u}, {6u, 2016u, 5u, 2u}, {5u, 31u, 0u, 3u}, {0u, 0u, 0u, 0u}, 0, {0, 0, 0}},
        {4u, 16u, {5u, 31744u, 10u, 3u}, {5u, 992u, 5u, 3u}, {5u, 31u, 0u, 3u}, {0u, 0u, 0u, 0u}, 0, {0, 0, 0}},
        {5u, 16u, {4u, 3840u, 8u, 4u}, {4u, 240u, 4u, 4u}, {4u, 15u, 0u, 4u}, {4u, 61440u, 12u, 4u}, 1, {0, 0, 0}},
        {6u, 16u, {5u, 31744u, 10u, 3u}, {5u, 992u, 5u, 3u}, {5u, 31u, 0u, 3u}, {1u, 32768u, 15u, 7u}, 1, {0, 0, 0}},
        {7u, 32u, {8u, 16711680u, 16u, 0u}, {8u, 65280u, 8u, 0u}, {8u, 255u, 0u, 0u}, {8u, 4278190080u, 24u, 0u}, 1, {0, 0, 0}},
        {8u, 32u, {8u, 16711680u, 16u, 0u}, {8u, 65280u, 8u, 0u}, {8u, 255u, 0u, 0u}, {0u, 0u, 0u, 0u}, 0, {0, 0, 0}},
        {9u, 0u, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, 0, {0, 0, 0}},
        {10u, 0u, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, {1u, 0u, 0u, 0u}, 1, {0, 0, 0}},
        {11u, 0u, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, 1, {0, 0, 0}},
        {12u, 0u, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, 1, {0, 0, 0}},
        {13u, 0u, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, 1, {0, 0, 0}},
        {14u, 0u, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}, 1, {0, 0, 0}},
    };

    const FTextureFormatDescriptor* TextureFormatDescriptor(int FormatId)
    {
        // FUN_06ed0ba0: signed JLE keeps all indices <=14; no lower guard.
        return &GTextureFormatDescriptors[FormatId > 14 ? 0 : FormatId];
    }

    void PixelFormatMatchFields(int FormatId, unsigned long* Out)
    {
        const FTextureFormatDescriptor* Descriptor = TextureFormatDescriptor(FormatId);
        Out[0] = Descriptor->BitsPerPixel;
        Out[1] = Descriptor->Red.BitCount;
        Out[2] = Descriptor->Green.BitCount;
        Out[3] = Descriptor->Blue.BitCount;
        Out[4] = Descriptor->Alpha.BitCount;
    }

    bool FormatMapRowMatches(const FTextureFormatMap& Row, int SrcId)
    {
        // FUN_06ebf110. Id==1 uses constant 8 on the first four pairs; else DAT_06ed5230.
        if (SrcId == 1)
        {
            if ((Row.Mask[0] & 8) != Row.Value[0])
                return false;
            if ((Row.Mask[1] & 8) != Row.Value[1])
                return false;
            if ((Row.Mask[2] & 8) != Row.Value[2])
                return false;
            if ((Row.Mask[3] & 8) != Row.Value[3])
                return false;
            if (Row.Value[4] != 0)
                return false;
            unsigned long Fields[5];
            PixelFormatMatchFields(SrcId, Fields);
            return (Row.Mask[5] & Fields[4]) == Row.Value[5];
        }
        unsigned long Fields[5];
        PixelFormatMatchFields(SrcId, Fields);
        if ((Row.Mask[0] & Fields[0]) != Row.Value[0])
            return false;
        if ((Row.Mask[1] & Fields[1]) != Row.Value[1])
            return false;
        if ((Row.Mask[2] & Fields[2]) != Row.Value[2])
            return false;
        if ((Row.Mask[3] & Fields[3]) != Row.Value[3])
            return false;
        if ((Row.Mask[4] & Fields[4]) != Row.Value[4])
            return false;
        return (Row.Mask[5] & Fields[4]) == Row.Value[5];
    }

    unsigned short ReadU16(const unsigned char* Bytes, unsigned long Offset)
    {
        return (unsigned short)(Bytes[Offset] | (Bytes[Offset + 1] << 8));
    }

    bool NameIsDotOrDotDot(const char* Name)
    {
        if (Name == nullptr || Name[0] == '\0')
            return true;
        if (Name[0] == '.' && Name[1] == '\0')
            return true;
        if (Name[0] == '.' && Name[1] == '.' && Name[2] == '\0')
            return true;
        return false;
    }

    bool MatchSourcePixelFormat(unsigned long* OutFormat, unsigned long Bits,
                                unsigned long Red, unsigned long Green, unsigned long Blue,
                                unsigned long Alpha)
    {
        // FUN_06ed0bc0: nine physical descriptors, independent of profile remaps.
        for (int Index = 0; Index < 9; ++Index)
        {
            const FTextureFormatDescriptor* Descriptor = TextureFormatDescriptor(Index);
            if (Bits == Descriptor->BitsPerPixel &&
                (unsigned long)(Alpha != 0) == Descriptor->bHasAlpha &&
                Red == Descriptor->Red.BitCount && Green == Descriptor->Green.BitCount &&
                Blue == Descriptor->Red.BitCount && Alpha == Descriptor->Alpha.BitCount)
            {
                *OutFormat = Index;
                return true;
            }
        }
        *OutFormat = 0; // FUN_06ed0b60(D3DFMT_UNKNOWN).
        return false;
    }

    bool GetTgaSourceFormat(const unsigned char* Blob, unsigned long* OutFormat)
    {
        const unsigned char Type = Blob[2] & 7;
        if (Type == 1 && Blob[1] == 1)
            return MatchSourcePixelFormat(OutFormat, 8, 0, 0, 0, 0);
        if (Blob[16] == 16)
            return MatchSourcePixelFormat(OutFormat, 16, 5, 5, 5, 0);
        if (Type == 3)
            return MatchSourcePixelFormat(OutFormat, 8, 8, 0, 0, 0);
        const unsigned char Bits = Type == 1 ? Blob[7] : Blob[16];
        return Bits == 32 ? MatchSourcePixelFormat(OutFormat, 32, 8, 8, 8, 8)
                          : MatchSourcePixelFormat(OutFormat, 24, 8, 8, 8, 0);
    }

    bool IsTgaHeaderSupported(const unsigned char* Blob)
    {
        // f2b0/f670 accept bit3 but do not decompress run-length packets.
        const unsigned char Bits = Blob[16];
        return Blob[1] <= 1 && (Blob[2] & 0xf7) <= 3 &&
            (Bits == 8 || Bits == 15 || Bits == 16 || Bits == 24 || Bits == 32);
    }

    bool DecodeTgaTexture(const unsigned char* Blob, unsigned long* OutFormat,
                          unsigned char* Palette, unsigned char** OutPixels)
    {
        // FUN_06ebf2b0: native source pixels, palette and byte-oriented origin branches.
        if (!IsTgaHeaderSupported(Blob))
            return false;
        GetTgaSourceFormat(Blob, OutFormat);
        const unsigned char Type = Blob[2] & 7;
        if (Type == 1)
        {
            const unsigned int EntryBytes = Blob[7] >> 3;
            const unsigned char* SourcePalette = Blob + Blob[0] + 18;
            for (unsigned int Entry = 0; Entry < 256; ++Entry)
            {
                for (unsigned int Byte = 0; Byte < EntryBytes; ++Byte)
                    Palette[Entry * EntryBytes + Byte] =
                        SourcePalette[Entry * EntryBytes + EntryBytes - Byte - 1];
            }
        }
        const int Width = (short)ReadU16(Blob, 12);
        const int Height = (short)ReadU16(Blob, 14);
        const unsigned long Pitch = ((TextureFormatDescriptor(*OutFormat)->BitsPerPixel + 7) >> 3) * Width;
        unsigned char* Pixels = new unsigned char[Height * Pitch];
        *OutPixels = Pixels;
        const unsigned char* Source = Blob + Blob[0] + 18;
        if (Type == 1)
            Source += ((short)ReadU16(Blob, 3) +
                       (short)ReadU16(Blob, 5)) * 3;
        const bool bBit5 = (Blob[17] & 0x20) != 0;
        const bool bBit4 = (Blob[17] & 0x10) != 0;
        if (!bBit5 && !bBit4)
            std::memcpy(Pixels, Source, Height * Pitch);
        else if (!bBit4)
        {
            for (int Row = 0; Row < Height; ++Row)
                std::memcpy(Pixels + (Height - Row - 1) * Pitch, Source + Row * Pitch, Pitch);
        }
        else if (*OutFormat == 1)
        {
            // Bit5 SET takes the same-row branch; CLEAR also reverses rows.
            for (int Row = 0; Row < Height; ++Row)
            {
                const int DestinationRow = bBit5 ? Row : Height - Row - 1;
                for (int Column = 0; Column < Width; ++Column)
                    Pixels[DestinationRow * Pitch + Width - Column - 1] = Source[Row * Pitch + Column];
            }
        }
        else
        {
            // Retail reverses each byte, not each multi-byte pixel.
            for (int Row = 0; Row < Height; ++Row)
            {
                const int DestinationRow = bBit5 ? Row : Height - Row - 1;
                for (unsigned long Byte = 0; Byte < Pitch; ++Byte)
                    Pixels[DestinationRow * Pitch + Pitch - Byte - 1] = Source[Row * Pitch + Byte];
            }
        }
        return true;
    }

    GENRESULT PipeDestroyTexture(IRenderPipeline* Pipeline, void* Handle)
    {
        if (Pipeline == nullptr)
            return GR_GENERIC;
        return Pipeline->DestroyTexture(Handle);
    }

    const FTextureEntry* PlaybackSource(const FTexturePlayback* Entry)
    {
        return Entry->Source;
    }

    float EffectivePlaybackRate(const FTexturePlayback* Entry)
    {
        return Entry->bUseLocalRate ? Entry->LocalRate : PlaybackSource(Entry)->TileRate;
    }

    double PlaybackDuration(unsigned int Count, float Rate)
    {
        const unsigned long long IntegerCount = Count;
        const double Epsilon = 1e-6;
        double Duration;
        __asm
        {
            fild IntegerCount
            fdiv Rate
            fsub Epsilon
            fstp Duration
        }
        return Duration;
    }

    void InitializePlayback(FTexturePlayback* Entry, FTextureEntry* Source)
    {
        // FUN_06ebe5d0: active rate initially aliases the source TileRate.
        Entry->Source = Source;
        Entry->bUseLocalRate = false;
        Entry->PlaybackDirection = 1.0f;
        Entry->CurrentFrame = 0;
        Entry->FrameCount = (unsigned int)Source->Frames.size();
        Entry->ElapsedTime = 0.0;
        Entry->TotalDuration = 0.0;
        Entry->WrapMode = 1;
        const float Rate = Source->TileRate;
        if (std::fabs(Rate) > 1e-5f)
            Entry->TotalDuration = PlaybackDuration(Entry->FrameCount, Rate);
    }

    void RecomputePlaybackDuration(FTexturePlayback* Entry)
    {
        const float Rate = EffectivePlaybackRate(Entry);
        if (std::fabs(Rate) > 1e-5f)
            Entry->TotalDuration = PlaybackDuration((unsigned int)Entry->Source->Frames.size(), Rate);
    }

    GENRESULT SetPlaybackFrame(FTexturePlayback* Entry, unsigned int Index)
    {
        // FUN_06ebe940: query the live source count; no cached-count update.
        if (Entry->Source->Frames.data() == nullptr || Index >= Entry->Source->Frames.size())
            return GR_GENERIC;
        Entry->CurrentFrame = Index;
        return GR_OK;
    }

    unsigned int TruncatePlaybackFrame(double Value)
    {
        return static_cast<unsigned int>(PortableIntegerConversion::ToInt64Bits(&Value));
    }

    void SetPlaybackElapsed(FTexturePlayback* Entry, float Seconds)
    {
        // FUN_06ebe8c0 computes fmod even for an empty source; it writes the
        // result before calling the bounds setter and discards that return.
        Entry->ElapsedTime = (double)Seconds;
        const double Count = (double)Entry->Source->Frames.size();
        const double CyclicFrame =
            std::fmod((double)EffectivePlaybackRate(Entry) * Entry->ElapsedTime, Count);
        Entry->CurrentFrame = TruncatePlaybackFrame(std::floor(CyclicFrame));
        SetPlaybackFrame(Entry, Entry->CurrentFrame);
    }

    void AdvancePlayback(FTexturePlayback* Entry, float Milliseconds)
    {
        // FUN_06ebe660 retains x87 precision across FST double and later comparisons.
        if (Entry->FrameCount == 0)
            Entry->FrameCount = (unsigned int)Entry->Source->Frames.size();
        if (Entry->FrameCount <= 1)
            return;
        const float SecondsPerMillisecond = 0.001f;
        const double Zero = 0.0;
        double Delta;
        __asm
        {
            fld Milliseconds
            fmul SecondsPerMillisecond
            fstp Delta
        }
        double* Elapsed = &Entry->ElapsedTime;
        const double* Duration = &Entry->TotalDuration;
        float* Direction = &Entry->PlaybackDirection;
        float Seconds = 0.0f;
        switch (Entry->WrapMode)
        {
        case 0:
            __asm
            {
                mov ecx, Elapsed
                mov edx, Duration
                fld Delta
                fadd qword ptr [ecx]
                fst qword ptr [ecx]
                fld qword ptr [edx]
                fld st(1)
                fcompp
                fnstsw ax
                test ah, 5
                jnp ClampLower
                fstp st(0)
                fld qword ptr [edx]
            ClampLower:
                fcom Zero
                fnstsw ax
                test ah, 0x41
                jz ClampStore
                fstp st(0)
                fld Zero
            ClampStore:
                fstp Seconds
            }
            break;
        case 1:
        {
            double RoundedNext;
            __asm
            {
                mov ecx, Elapsed
                fld Delta
                fadd qword ptr [ecx]
                fst RoundedNext
                fstp qword ptr [ecx]
            }
            Seconds = (float)std::fmod(RoundedNext, *Duration);
            break;
        }
        case 2:
        {
            double RoundedNext;
            int Reflection = 0;
            __asm
            {
                mov ecx, Elapsed
                mov edx, Direction
                fld dword ptr [edx]
                fmul Delta
                fadd qword ptr [ecx]
                fst RoundedNext
                fst qword ptr [ecx]
                fld dword ptr [edx]
                fcomp Zero
                fnstsw ax
                test ah, 5
                jp ForwardTest
                fcom Zero
                fnstsw ax
                test ah, 5
                jp ForwardTest
                mov Reflection, 1
                fstp st(0)
                jmp ReflectionDone
            ForwardTest:
                fld dword ptr [edx]
                fcomp Zero
                fnstsw ax
                test ah, 0x41
                jnz KeepExtended
                mov ecx, Duration
                fcom qword ptr [ecx]
                fnstsw ax
                test ah, 0x41
                jnz KeepExtended
                mov Reflection, 2
                fstp st(0)
                jmp ReflectionDone
            KeepExtended:
                fstp Seconds
            ReflectionDone:
            }
            if (Reflection != 0)
            {
                const double Remainder = std::fmod(RoundedNext, *Duration);
                if (Reflection == 1)
                {
                    __asm
                    {
                        fld Remainder
                        fchs
                        fstp Seconds
                    }
                }
                else
                {
                    __asm
                    {
                        mov ecx, Duration
                        fld Remainder
                        fsubr qword ptr [ecx]
                        fstp Seconds
                    }
                }
                __asm
                {
                    mov ecx, Direction
                    fld dword ptr [ecx]
                    fchs
                    fstp dword ptr [ecx]
                }
            }
            break;
        }
        default:
            break;
        }
        SetPlaybackElapsed(Entry, Seconds);
    }

}

FTexLibAggregate::FTexLibAggregate() : Owner(nullptr) {}

GENRESULT DACOM_API FTexLibAggregate::QueryInterface(const char* Iid, void** Out)
{
    return Owner->QueryInterface(Iid, Out);
}

unsigned long DACOM_API FTexLibAggregate::AddRef()
{
    return Owner->AddRef();
}

unsigned long DACOM_API FTexLibAggregate::Release()
{
    return Owner->Release();
}

FTextureLibraryController::FTextureLibraryController(FTextureLibrary* InOwner) : Owner(InOwner) {}

GENRESULT DACOM_API FTextureLibraryController::QueryInterface(const char* Iid, void** Out)
{
    // LAB_06ec6880: strict IID map; matched face AddRef precedes publishing Out.
    IDAComponent* Face = nullptr;
    if (std::strcmp(Iid, Iids::TextureLibraryA) == 0 ||
        std::strcmp(Iid, Iids::TextureLibrary2) == 0)
        Face = Owner;
    else if (std::strcmp(Iid, Iids::AggregateComponent) == 0)
        Face = &Owner->Aggregate;
    else
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }
    Face->AddRef();
    *Out = Face;
    return GR_OK;
}

unsigned long DACOM_API FTextureLibraryController::AddRef()
{
    return ++Owner->RefCount;
}

unsigned long DACOM_API FTextureLibraryController::Release()
{
    // LAB_06ec6940 is this non-delegating face, not public Release.
    // This embedded controller is constructed with its mandatory owner.
    if (Owner->RefCount != 0)
        --Owner->RefCount;
    const unsigned long Next = Owner->RefCount;
    if (Next == 0)
    {
        FTextureLibrary* DeleteOwner = Owner;
        DeleteOwner->RefCount = 1;
        delete DeleteOwner;
    }
    return Next;
}

GENRESULT DACOM_API FTexLibAggregate::Initialize()
{
    // The incoming face is retail object+4: +2c/+30/+34 here are
    // the owner's three ability dwords, not its readiness byte at +2c.
    Owner->bSquareTexturesOnly = 0;
    Owner->MaxTextureWidth = 0;
    Owner->MaxTextureHeight = 0;
    const GENRESULT PipelineResult = Owner->AggregateOwner->QueryInterface(
        Iids::RenderPipeline8B, (void**)&Owner->Pipeline);
    if (PipelineResult < 0)
    {
        // .text:06ec071a .. 06ec073c: FDUMP(0x100001, "%s(%d) : ERROR:General:%s", __FILE__, 0x308, msg).
        FDUMP(0x100001, "%s(%d) : ERROR:General:%s",
              "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Shading\\TextureLibrary\\TextureLibrary.cpp", 0x308,
              "TextureLibrary: init: unable to acquire IID_IRenderPipeline");
        return GR_GENERIC;
    }
    Owner->Pipeline->Release(); // borrowed for the aggregate lifetime.

    InitializeFormatMaps();

    ICOManager* Manager = DACOM_Acquire();
    IProfileParser* Parser = nullptr;
    if (Manager->QueryInterface(Iids::ProfileParser,
                                (void**)&Parser) >= 0)
    {
        IProfileParser* LocalParser = Parser;
        GENRESULT LocalResult = GR_OK;
        if (LocalParser != nullptr)
            LocalParser->AddRef();
        else
            LocalResult = Manager->QueryInterface(Iids::ProfileParser,
                                                   (void**)&LocalParser);
        unsigned long Value;
        if (LocalResult == GR_OK)
        {
            Value = 1;
            void* Section = LocalParser->OpenSection("TextureLibrary", 3);
            if (Section != nullptr)
            {
                if (LocalParser->GetString(Section, "TEXTURE_LOD_LOAD", GProfileValue, 0x400) != 0)
                    Value = ParseRetailProfileInteger(GProfileValue);
                LocalParser->CloseSection(Section);
            }
        }
        if (LocalParser != nullptr)
            LocalParser->Release();
        Owner->SetGenericSlot(0, Value); // EBP was reset to zero at 06ec0936.
        ReadProfileInteger(Manager, Parser, "TextureLibrary", "TEXTURE_LOD_LOAD_MIN", 0, &Value);
        Owner->SetGenericSlot(4, Value);
    }
    if (Parser != nullptr)
        Parser->Release();
    return GR_OK;
}

FTextureLibrary::FTextureLibrary()
    : RefCount(1), AggregateOwner(nullptr), Outer(nullptr), Controller(this), Pipeline(nullptr), bCapabilitiesCached(false), bSquareTexturesOnly(1), MaxTextureWidth(256), MaxTextureHeight(256), NextPlaybackId(1), NextId(1)
{
    Aggregate.Owner = this;
    std::memset(GenericSlots, 0, sizeof(GenericSlots));
}

GENRESULT FTextureLibrary::RefreshDeviceAbilities()
{
    // FUN_06ec0b80: refresh all three out fields regardless of HRESULT.
    Pipeline->QueryDeviceAbility(5, &bSquareTexturesOnly);
    Pipeline->QueryDeviceAbility(6, &MaxTextureWidth);
    Pipeline->QueryDeviceAbility(7, &MaxTextureHeight);
    bCapabilitiesCached = true;
    return GR_OK;
}

void FTextureLibrary::DestroyEntry(FTextureEntry* Entry)
{
    if (Entry == nullptr)
        return;
    // Frames select borrowed children. Only the canonical record owns its handle.
    if (Entry->GpuHandle != 0)
        PipeDestroyTexture(Pipeline, (void*)Entry->GpuHandle);
    delete Entry;
}

int FTextureLibrary::ReleaseEntryReference(FTextureEntry* Entry, bool bCleanup)
{
    // FUN_06ec0bd0: recursion precedes the signed decrement and propagates flag.
    if (Entry->Frames.data() != nullptr && Entry->Frames.size() > 1)
    {
        for (size_t Index = 0; Index < Entry->Children.size(); ++Index)
            ReleaseEntryReference(Entry->Children[Index], bCleanup);
    }
    Entry->RefCount = (int)((unsigned int)Entry->RefCount - 1u);
    const int Remaining = Entry->RefCount;
    if (Remaining > 0 || !bCleanup)
        return Remaining;

    const unsigned long Key = DACOM_CRC::GetCRC32(Entry->GetName());
    const std::map<unsigned long, FTextureEntry*>::iterator Found = CanonicalRecords.find(Key);
    FTextureEntry* Indexed = Found->second; // Retail trusts the name-map lookup.
    DestroyEntry(Indexed);
    CanonicalRecords.erase(Found);
    return Remaining;
}

void FTextureLibrary::StoreByNameCrc(FTextureEntry* Entry)
{
    // FUN_06ec0e50: unique unsigned CRC key; duplicate insertion leaves payload.
    Entry->Crc = DACOM_CRC::GetCRC32(Entry->GetName());
    CanonicalRecords.insert(std::make_pair(Entry->Crc, Entry));
}

bool FTextureLibrary::CreateTextureRecord(unsigned long GpuHandle, const char* Name,
                                          FTextureEntry** Out, bool bRetain)
{
    // FUN_06ec0f10 publishes before filling the two owned vectors.
    FTextureEntry* Entry = new FTextureEntry();
    Entry->SetName(Name);
    *Out = Entry;
    Entry->GpuHandle = GpuHandle;
    FTextureFrame Frame = {};
    Frame.Uv[2] = 1.0f;
    Frame.Uv[3] = 1.0f;
    // Both 06ec38e0 call sites are here: fresh empty Frames resize to 1;
    // the second size<1 check is false after the first succeeds.
    Entry->Frames.push_back(Frame);
    Entry->Children.push_back(Entry);
    StoreByNameCrc(Entry);
    if (bRetain)
        AddRefEntryRecursive(Entry);
    return true;
}

FTextureLibrary::~FTextureLibrary()
{
    // FUN_06ec0540 clears format maps and containers; canonical payloads are
    // released by GarbageCollect, not by the tree-node destructor (06ec4870).
    Pipeline = nullptr;
    AggregateOwner = nullptr;
    FormatMapClear();
    ActivePlayback.clear();
    PlaybackRecords.clear();
    // Retail's shared VC6 allocator cache is private container storage; the
    // native maps release their own storage without reproducing that cache.
    CanonicalRecords.clear();
    GroupRecords.clear();
}

GENRESULT FTextureLibrary::InitializeFromDescriptor(const FSystemAggDesc* Desc)
{
    // FUN_06ec0bc0: no QI, allocation or aggregate Initialize call.
    AggregateOwner = Desc->Outer;
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::QueryInterface(const char* Iid, void** Out)
{
    IDAComponent* Controlling = Outer != nullptr ? Outer : &Controller;
    return Controlling->QueryInterface(Iid, Out);
}

unsigned long DACOM_API FTextureLibrary::AddRef()
{
    IDAComponent* Controlling = Outer != nullptr ? Outer : &Controller;
    return Controlling->AddRef();
}

unsigned long DACOM_API FTextureLibrary::Release()
{
    IDAComponent* Controlling = Outer != nullptr ? Outer : &Controller;
    return Controlling->Release();
}

namespace
{
    FFileSystemDesc MakeTextureDescriptor(const char* Name)
    {
        FFileSystemDesc Desc = {};
        Desc.Size = 0x34;
        Desc.ComponentName = "FileSystem";
        Desc.Path = Name;
        Desc.Access = GENERIC_READ;
        Desc.ShareMode = FILE_SHARE_READ;
        Desc.Creation = OPEN_EXISTING;
        Desc.Flags = 0x08000080;
        Desc.Field30 = 0xffffffff;
        return Desc;
    }

    bool ReadTextureProperty(IFileSystem* FileSystem, const char* Name, void* Buffer,
                             unsigned long Size)
    {
        FFileSystemDesc Desc = MakeTextureDescriptor(Name);
        const int Handle = FileSystem->OpenByDesc(&Desc);
        if (Handle == -1)
            return false;
        unsigned long BytesRead = 0;
        const int Result = FileSystem->Read((unsigned long)Handle, Buffer,
                                            Size, &BytesRead, 0);
        FileSystem->CloseFile((unsigned long)Handle);
        return Result != 0 && BytesRead == Size;
    }
}

GENRESULT FTextureLibrary::LoadLegacyTextureRecord(IFileSystem* RootFileSystem,
                                                  IFileSystem* TextureFileSystem,
                                                  const char* Name, FTextureEntry** Out,
                                                  bool bRetain)
{
    unsigned long TextureCount = 0;
    if (!ReadTextureProperty(TextureFileSystem, "Texture count", &TextureCount, 4))
        return (GENRESULT)-8;
    std::vector<FTextureEntry*> Children;
    Children.reserve(TextureCount);
    for (unsigned long Index = 0; Index < TextureCount; ++Index)
    {
        const std::string ChildName = std::string(Name) + "_" + std::to_string(Index);
        FFileSystemDesc Desc = MakeTextureDescriptor(ChildName.c_str());
        FTextureEntry* Child;
        if (LoadTextureRecord(RootFileSystem, &Desc, &Child, true) < 0)
        {
            for (size_t ChildIndex = 0; ChildIndex < Children.size(); ++ChildIndex)
            {
                if (Children[ChildIndex] != nullptr)
                    ReleaseEntryReference(Children[ChildIndex], true);
            }
            return GR_GENERIC;
        }
        Children.push_back(Child);
    }
    float FramesPerSecond = 24.0f;
    float StoredFramesPerSecond;
    if (ReadTextureProperty(TextureFileSystem, "FPS", &StoredFramesPerSecond, 4))
        FramesPerSecond = StoredFramesPerSecond;
    unsigned long FrameCount = 0;
    // Retail's late-property failures deliberately do not release earlier child refs.
    if (!ReadTextureProperty(TextureFileSystem, "Frame count", &FrameCount, 4))
        return GR_GENERIC;
    FTextureFrame DefaultFrame = {};
    DefaultFrame.Uv[2] = 1.0f;
    DefaultFrame.Uv[3] = 1.0f;
    std::vector<FTextureFrame> Frames(FrameCount, DefaultFrame);
    unsigned long FrameBytes = FrameCount * 20u;
    FFileSystemDesc RectDesc = MakeTextureDescriptor("Frame rects");
    const int RectHandle = TextureFileSystem->OpenByDesc(&RectDesc);
    if (RectHandle == -1)
        return GR_GENERIC;
    if (FrameBytes == 0)
        FrameBytes = TextureFileSystem->GetSize((unsigned long)RectHandle, 0);
    unsigned long BytesRead = 0;
    const int ReadResult = TextureFileSystem->Read((unsigned long)RectHandle,
                                                  Frames.data(), FrameBytes, &BytesRead, 0);
    TextureFileSystem->CloseFile((unsigned long)RectHandle);
    if (ReadResult == 0 || BytesRead != FrameBytes)
        return GR_GENERIC;
    FTextureEntry* Entry = new FTextureEntry();
    Entry->SetName(Name);
    *Out = Entry;
    Entry->Frames.resize(FrameCount, DefaultFrame);
    for (unsigned long Index = 0; Index < FrameCount; ++Index)
    {
        const FTextureFrame* Source = &Frames[Index];
        FTextureFrame* Destination = &Entry->Frames[Index];
        Destination->ChildIndex = Source->ChildIndex;
        PortableVectorLoad::CopyFour(Destination->Uv, Source->Uv);
    }
    Entry->TileRate = FramesPerSecond;
    Entry->Children.resize(TextureCount, nullptr);
    for (unsigned long Index = 0; Index < TextureCount; ++Index)
    {
        if (Children[Index] != nullptr)
        {
            Entry->Children[Index] = Children[Index];
            ReleaseEntryReference(Children[Index], false);
        }
    }
    StoreByNameCrc(Entry);
    if (bRetain && *Out != nullptr)
        AddRefEntryRecursive(*Out);
    return GR_OK;
}

namespace
{
    struct FTextureFileMap
    {
        IFileSystem* FileSystem;
        int File;
        int Mapping;
        unsigned long Size;
        unsigned char* View;

        FTextureFileMap(IFileSystem* InFileSystem, const char* Name)
            : FileSystem(InFileSystem), File(-1), Mapping(-1), Size(0), View(nullptr)
        {
            FileSystem->AddRef();
            FFileSystemDesc Desc = MakeTextureDescriptor(Name);
            File = FileSystem->OpenByDesc(&Desc);
            if (File != -1)
            {
                Size = FileSystem->GetSize(File, 0);
                Mapping = FileSystem->CreateMapping(File, 0, 2, 0, 0, 0);
                if (Mapping != -1)
                    View = (unsigned char*)FileSystem->MapFileView(Mapping, 4, 0, 0, 0);
            }
        }

        ~FTextureFileMap()
        {
            // FUN_06ec02b0: mapping and file handles are both CloseFile resources.
            if (File != -1)
            {
                if (Mapping != -1)
                {
                    if (View != nullptr)
                        FileSystem->UnmapFileView((unsigned long)View);
                    FileSystem->CloseFile(Mapping);
                }
                FileSystem->CloseFile(File);
            }
            FileSystem->Release();
        }
    };

    struct FTextureDdsCursor
    {
        const unsigned char* Pixels;
        unsigned long Bytes;
        unsigned long MinimumBlock;
        unsigned long Width;
        unsigned long Height;

        void Advance()
        {
            // FUN_06ec0310: no dimension floor and no block-grid recalculation.
            Pixels += Bytes;
            Bytes >>= 2;
            if (Bytes < MinimumBlock)
                Bytes = MinimumBlock;
            Width >>= 1;
            Height >>= 1;
        }
    };

    void TextureDdsWarning(int Line, const char* Format, const char* Name, int Mip = 0)
    {
        char Message[8192];
        std::sprintf(Message, Format, Name, Mip);
        FDUMP(0x100002, "%s(%d) : WARNING:General:%s",
              "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Shading\\TextureLibrary\\DX8TexLoader_inl.h",
              Line, Message);
    }

    unsigned long D3dFormatToTextureFormat(unsigned long Format)
    {
        // FUN_06ed0b60 searches all 22 entries, including ordinary RGB formats.
        for (int Index = 0; Index < kPixelFormatFourCcCount; ++Index)
            if (kPixelFormatFourCc[Index] == Format)
                return Index;
        return 0;
    }

    unsigned long RoundTexturePowerOfTwo(unsigned long Value)
    {
        if ((Value & (Value - 1)) == 0)
            return Value;
        unsigned int Shift = 0;
        while (Value != 1)
        {
            Value >>= 1;
            ++Shift;
        }
        return 1UL << ((Shift + 1) & 31);
    }
}

unsigned long FTextureLibrary::SelectTextureMipLevels(unsigned long* Width, unsigned long* Height)
{
    // FUN_06ec1110, unused argument omitted from this internal native helper.
    unsigned long Count = 0;
    const unsigned long Limit = GenericSlots[4];
    if (Limit != 0)
    {
        while (*Width > Limit || *Height > Limit)
        {
            *Width >>= 1;
            *Height >>= 1;
            ++Count;
        }
    }
    return Count;
}

GENRESULT FTextureLibrary::SelectTextureDimensions(unsigned long Width, unsigned long Height,
                                                  unsigned long* OutWidth, unsigned long* OutHeight)
{
    // FUN_06ec0d50: publish inputs before zero rejection, then power-of-two/square policy.
    *OutWidth = Width;
    *OutHeight = Height;
    if (Width == 0 || Height == 0)
        return GR_GENERIC;
    if (GenericSlots[3] == 1 || GenericSlots[3] == 2)
    {
        *OutWidth = RoundTexturePowerOfTwo(*OutWidth);
        *OutHeight = RoundTexturePowerOfTwo(*OutHeight);
    }
    bool bSquare = GenericSlots[2] == 2;
    if (GenericSlots[2] == 1)
    {
        RefreshDeviceAbilities();
        bSquare = *OutWidth != *OutHeight && bSquareTexturesOnly != 0;
    }
    if (bSquare)
    {
        const unsigned long Maximum = *OutWidth > *OutHeight ? *OutWidth : *OutHeight;
        *OutWidth = Maximum;
        *OutHeight = Maximum;
    }
    return GR_OK;
}

GENRESULT FTextureLibrary::LoadDdsTexture(IFileSystem* FileSystem, const char* Name, void** OutHandle)
{
    // FUN_06ebfd20. Keep the original source format and native mapped DDS mip cursor.
    FTextureFileMap Map(FileSystem, "MIPS");
    if (Map.File == -1 || Map.Mapping == -1 || Map.View == nullptr)
        return -8;
    struct FTextureReference
    {
        IRPTexture* Pointer;
        FTextureReference() : Pointer(nullptr) {}
        ~FTextureReference() { if (Pointer != nullptr) Pointer->Release(); }
    } Texture;
    if (Pipeline->QueryInterface(Iids::RPTexture, (void**)&Texture.Pointer) != GR_OK)
    {
        TextureDdsWarning(0x20d, "load_dds_mips: %s: couldnt acquire IRPTexture!", Name);
        return GR_GENERIC;
    }
    D3DXIMAGE_INFO Info;
    if (Texture.Pointer->GetImageInfo(&Info, Map.Size, Map.View) < 0 || Info.ImageFileFormat != D3DXIFF_DDS)
    {
        TextureDdsWarning(0x215, "load_dds_mips: %s: couldnt load dds info!", Name);
        return GR_GENERIC;
    }
    unsigned long SourceFormat = D3dFormatToTextureFormat(Info.Format);
    const unsigned long* Header = (const unsigned long*)Map.View;
    const bool bPitch = (Header[2] & 8) != 0;
    unsigned long MinimumBlock = 1;
    if (!bPitch)
    {
        switch (Header[0x54 / 4])
        {
        case kFourCcDxt1: MinimumBlock = 8; break;
        case kFourCcDxt2:
        case kFourCcDxt3:
        case kFourCcDxt4:
        case kFourCcDxt5: MinimumBlock = 16; break;
        default:
            TextureDdsWarning(0x220, "load_dds_mips: %s: unknown dds format!", Name);
            return GR_GENERIC;
        }
    }
    unsigned long Width = Info.Width;
    unsigned long Height = Info.Height;
    unsigned long Skip = 0;
    unsigned long Levels = 1;
    if (GenericSlots[0] != 0)
    {
        Skip = SelectTextureMipLevels(&Width, &Height);
        Levels = Info.MipLevels - Skip;
        if ((int)Levels <= 0)
            return GR_GENERIC;
    }
    unsigned long DestinationWidth;
    unsigned long DestinationHeight;
    SelectTextureDimensions(Width, Height, &DestinationWidth, &DestinationHeight);
    unsigned long TargetFormat = D3dFormatToTextureFormat(0);
    PickGpuFormat(&SourceFormat, &TargetFormat);
    unsigned int GpuFormat = TargetFormat;
    void* Handle = nullptr;
    if (Pipeline->CreateTexture(DestinationWidth, DestinationHeight, &GpuFormat, Levels, 0, &Handle) < 0)
    {
        TextureDdsWarning(0x24d, "load_dds_mips: %s: failed to create texture!", Name);
        return GR_GENERIC;
    }
    FTextureDdsCursor Cursor = {Map.View + 0x80,
        bPitch ? Header[0x14 / 4] * Header[0x0c / 4] : Header[0x14 / 4],
        MinimumBlock, Header[0x10 / 4], Header[0x0c / 4]};
    for (unsigned long Index = 0; Index < Skip; ++Index)
        Cursor.Advance();
    unsigned int UploadFormat = SourceFormat;
    for (unsigned long Level = 0; Level < Levels; ++Level)
    {
        // Retail divides the linear byte size by the WIDTH block count.
        const unsigned long Pitch = bPitch ? (Header[0x58 / 4] >> 3) * Cursor.Width
            : Cursor.Bytes / ((Cursor.Width + 3) >> 2);
        if (Pipeline->SetTextureLevelData(Handle, Level, Cursor.Width, Cursor.Height, Pitch,
                                          &UploadFormat, Cursor.Pixels, nullptr, nullptr) < 0)
        {
            TextureDdsWarning(0x25f, "load_dds_mips: %s: dx couldnt load mip %d !", Name,
                              (int)(Skip + Level));
            return GR_GENERIC;
        }
        Cursor.Advance();
    }
    // Upload errors do not destroy the allocated handle in this retail implementation.
    *OutHandle = Handle;
    return Handle != nullptr ? GR_OK : GR_GENERIC;
}

namespace
{
    struct FTextureTgaMap
    {
        int File;
        unsigned long Size;
        int Mapping;
        unsigned char* View;
        IFileSystem* FileSystem;

        FTextureTgaMap() : File(-1), Size(0), Mapping(0), View(nullptr), FileSystem(nullptr) {}

        void CloseResources()
        {
            if (File != -1)
            {
                if (Mapping != -1)
                {
                    if (View != nullptr)
                    {
                        FileSystem->UnmapFileView((unsigned long)View);
                        View = nullptr;
                    }
                    FileSystem->CloseFile(Mapping);
                }
                FileSystem->CloseFile(File);
                File = -1;
            }
            if (FileSystem != nullptr)
            {
                FileSystem->Release();
                FileSystem = nullptr;
            }
        }

        ~FTextureTgaMap()
        {
            // FUN_06ebfca0: array teardown visits all ten records in reverse.
            CloseResources();
        }

        void Open(IFileSystem* Source, const char* Name, int FindHandle)
        {
            if (FileSystem != Source)
            {
                if (FileSystem != nullptr)
                    FileSystem->Release();
                FileSystem = Source;
                Source->AddRef();
            }
            FFileSystemDesc Desc = MakeTextureDescriptor(Name);
            Desc.Field30 = FindHandle;
            File = FileSystem->OpenByDesc(&Desc);
            if (File != -1)
            {
                Size = FileSystem->GetSize(File, 0);
                Mapping = FileSystem->CreateMapping(File, 0, 2, 0, 0, 0);
                if (Mapping != -1)
                    View = (unsigned char*)FileSystem->MapFileView(Mapping, 4, 0, 0, 0);
            }
        }
    };

    FTextureTgaMap* TgaMappedResources()
    {
        // DAT_06ed56f8..06ed57bf: lazy persistent records, including failed mappings.
        static FTextureTgaMap Records[10];
        return Records;
    }

    void TextureTgaWarning(int Line, const char* Format, ...)
    {
        char Message[8192];
        va_list Arguments;
        va_start(Arguments, Format);
        std::vsprintf(Message, Format, Arguments);
        va_end(Arguments);
        FDUMP(0x100002, "%s(%d) : WARNING:General:%s",
              "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Shading\\TextureLibrary\\DX8TexLoader_inl.h",
              Line, Message);
    }
}

GENRESULT FTextureLibrary::LoadTgaTexture(IFileSystem* FileSystem, const char* Name, void** OutHandle)
{
    // FUN_06ebf670: wildcard enumeration and source-format uploads, not sequential MIP0.
    WIN32_FIND_DATAA FindData;
    const int FindHandle = FileSystem->FindFirst("MIP*",
                                             &FindData);
    if (FindHandle == -1 || (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return -8;
    FTextureTgaMap* Maps = TgaMappedResources();
    int Count = 0;
    int LargestIndex = 0;
    do
    {
        if (FindData.cFileName[4] != '\0')
        {
            TextureTgaWarning(0x9d, "load_tga_mips: %s: too many mips; switch file to dxtn", Name);
            return -8; // Retail retains mappings and the find handle on this early return.
        }
        const int Index = (signed char)FindData.cFileName[3] - '0';
        if (Index >= LargestIndex)
            LargestIndex = Index;
        FTextureTgaMap& Map = Maps[Index];
        Map.Open(FileSystem, FindData.cFileName, FindHandle);
        if (Map.File == -1 || Map.Mapping == -1 || Map.View == nullptr)
            TextureTgaWarning(0xad, "load_tga_mips: %s: mip %d invalid", Name, Index);
        else
            ++Count;
    } while (FileSystem->FindNext(FindHandle, &FindData) != 0);
    FileSystem->FindClose(FindHandle);
    if (LargestIndex + 1 != Count)
        TextureTgaWarning(0xbd,
            "load_tga_mips: %s: detected missing mips; expected %d mips; largest found %d",
            Name, Count, LargestIndex);

    void* Handle = nullptr;
    unsigned long SourceFormat = 0;
    unsigned long Skip = 0;
    int Levels = 0;
    const unsigned char* Header = Maps[0].View;
    if (GetTgaSourceFormat(Header, &SourceFormat))
    {
        unsigned long Width = (short)ReadU16(Header, 12);
        unsigned long Height = (short)ReadU16(Header, 14);
        Levels = 1;
        if (GenericSlots[0] != 0)
        {
            Skip = SelectTextureMipLevels(&Width, &Height);
            Levels = Count - Skip;
            if (Levels <= 0)
            {
                Levels = 1;
                if ((int)Skip >= Count)
                    Skip = Count - 1;
            }
        }
        unsigned long TargetFormat = 0;
        PickGpuFormat(&SourceFormat, &TargetFormat);
        unsigned long DestinationWidth;
        unsigned long DestinationHeight;
        SelectTextureDimensions(Width, Height, &DestinationWidth, &DestinationHeight);
        unsigned int GpuFormat = TargetFormat;
        if (Pipeline->CreateTexture(DestinationWidth, DestinationHeight, &GpuFormat,
                                     Levels, 0, &Handle) < 0)
            TextureTgaWarning(0x104, "load_tga_mips: %s: failed to create texture!", Name);
    }
    for (int Level = 0; Level < Levels; ++Level, ++Skip)
    {
        const unsigned char* Blob = Maps[Skip].View;
        if (Blob == nullptr)
            continue;
        if (!IsTgaHeaderSupported(Blob))
        {
            TextureTgaWarning(0x11b, "load_tga_mips: %s: mip invalid!", Name);
            continue;
        }
        unsigned long UploadFormat = 0;
        unsigned char Palette[256 * (0xff >> 3)]; // Byte-sized palette depth: at most 31 bytes/entry.
        unsigned char* Pixels = nullptr;
        DecodeTgaTexture(Blob, &UploadFormat, Palette, &Pixels);
        const int Width = (short)ReadU16(Blob, 12);
        const int Height = (short)ReadU16(Blob, 14);
        const unsigned long Pitch = ((TextureFormatDescriptor(UploadFormat)->BitsPerPixel + 7) >> 3) * Width;
        unsigned int Format = UploadFormat;
        const GENRESULT Result = Pipeline->SetTextureLevelData(Handle, Level, Width, Height, Pitch,
            &Format, Pixels, nullptr, UploadFormat == 1 ? Palette : nullptr);
        delete[] Pixels;
        if (Result < 0)
            TextureTgaWarning(0x125, "load_tga_mips: %s: dx couldnt load mip %d !", Name, Skip);
    }
    for (int Index = 0; Index < 10; ++Index)
    {
        // Normal completion cleans only mapped records; atexit handles failed records.
        if (Maps[Index].View != nullptr)
            Maps[Index].CloseResources();
    }
    *OutHandle = Handle;
    return Handle != nullptr ? GR_OK : GR_GENERIC;
}

GENRESULT FTextureLibrary::ProbeTextureCubemap(IFileSystem* FileSystem, void** OutHandle)
{
    // FUN_06ec0360, IRPTexture slot7; only unsupported-format -7 becomes loader -8.
    IRPTexture* Texture = nullptr;
    GENRESULT Result = GR_GENERIC;
    if (Pipeline->QueryInterface(Iids::RPTexture, (void**)&Texture) == GR_OK)
    {
        Result = Texture->LoadCubemap(FileSystem, "CUBE", OutHandle);
        if (Result == -7)
            Result = -8;
    }
    if (Texture != nullptr)
        Texture->Release();
    return Result;
}

GENRESULT FTextureLibrary::LoadTextureRecord(IFileSystem* RootFileSystem, FFileSystemDesc* Desc,
                                            FTextureEntry** Out, bool bRetain)
{
    const char* ChildName = Desc->Path;
    FTextureEntry* Existing = FindEntryByCrc(DACOM_CRC::GetCRC32(ChildName));
    if (Existing != nullptr)
    {
        *Out = Existing;
        if (bRetain)
            AddRefEntryRecursive(Existing);
        return GR_OK;
    }
    IFileSystem* TextureFileSystem = nullptr;
    const GENRESULT CreateResult = RootFileSystem->CreateInstance((AGGDESC*)Desc,
                                             (void**)&TextureFileSystem);
    if (CreateResult < 0)
    {
        if (TextureFileSystem != nullptr)
            TextureFileSystem->Release();
        return GR_GENERIC;
    }
    void* Handle = nullptr;
    GENRESULT Result = LoadDdsTexture(TextureFileSystem, ChildName, &Handle);
    if (Result == -8)
        Result = LoadTgaTexture(TextureFileSystem, ChildName, &Handle);
    if (Result == -8)
        Result = ProbeTextureCubemap(TextureFileSystem, &Handle);
    if (Result == -8)
        Result = LoadLegacyTextureRecord(RootFileSystem, TextureFileSystem, ChildName, Out, bRetain);
    if (Result == -8)
    {
        char Message[8192];
        std::sprintf(Message, "load texture: file contains an old or unknown format, ignoring\n%s", ChildName);
        FDUMP(0x100002, "%s(%d) : WARNING:General:%s",
              "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Shading\\TextureLibrary\\TextureLibrary.cpp",
              0x54b, Message);
    }
    if (Handle != nullptr && Result >= 0)
    {
        if (!CreateTextureRecord((unsigned long)Handle, ChildName, Out, bRetain))
        {
            Pipeline->DestroyTexture(Handle);
            Result = GR_GENERIC;
        }
    }
    if (TextureFileSystem != nullptr)
        TextureFileSystem->Release();
    return Result;
}

GENRESULT DACOM_API FTextureLibrary::LoadTextureLibrary(void* FileSystem, const char* Name)
{
    RefreshDeviceAbilities();
    IFileSystem* Source = AsFileSystem(FileSystem);
    const int Opened = Source->OpenNode("Texture library");
    FFileSystemDesc Desc = MakeTextureDescriptor(Name);
    FTextureEntry* Entry;
    const GENRESULT Result = LoadTextureRecord(Source, &Desc, &Entry, false);
    if (Opened != 0)
        Source->OpenNode("..");
    return Result;
}

GENRESULT DACOM_API FTextureLibrary::LoadTextureGroup(void* FileSystem, int* OutGroupId)
{
    IFileSystem* Source = AsFileSystem(FileSystem);
    if (Source == nullptr)
    {
        FDUMP(0x100002, "%s(%d) : WARNING:General:%s",
              "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Shading\\TextureLibrary\\TextureLibrary.cpp",
              0x58e, "IFileSystem argument is NULL in load_library");
        return GR_GENERIC; // Output is unchanged on this early return.
    }
    RefreshDeviceAbilities();
    std::vector<int> Members;
    const bool bOpened = Source->OpenNode("Texture library") != 0;
    if (bOpened)
    {
        WIN32_FIND_DATAA FindData;
        const int FindHandle = Source->FindFirst("*.*", &FindData);
        if (FindHandle != -1)
        {
            do
            {
                if (FindData.cFileName[0] != '.')
                {
                    FFileSystemDesc Desc = MakeTextureDescriptor(FindData.cFileName);
                    Desc.Field30 = (unsigned long)FindHandle;
                    FTextureEntry* Entry;
                    const GENRESULT Result = LoadTextureRecord(Source, &Desc, &Entry, false);
                    if (Result >= 0)
                    {
                        int PlaybackId;
                        if (CreatePlaybackGroup(Entry, &PlaybackId) >= 0)
                            Members.push_back(PlaybackId);
                    }
                    else if (Result != -7)
                    {
                        char Message[0x2000];
                        _snprintf(Message, sizeof(Message), "failed to load '%s'", FindData.cFileName);
                        FDUMP(0x100002, "%s(%d) : WARNING:General:%s",
                              "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Shading\\TextureLibrary\\TextureLibrary.cpp",
                              0x5b4, Message);
                    }
                }
            } while (Source->FindNext((unsigned long)FindHandle, &FindData) != 0);
            Source->FindClose((unsigned long)FindHandle);
        }
        Source->OpenNode("..");
    }
    if (OutGroupId != nullptr)
    {
        if (bOpened)
        {
            FTextureGroup& Group = GroupRecords.try_emplace(NextId).first->second;
            Group.SetName(nullptr);
            Group.Members = Members;
            *OutGroupId = NextId;
            NextId = (int)((unsigned int)NextId + 1u);
        }
        else
            *OutGroupId = -1;
    }
    return bOpened ? GR_OK : GR_GENERIC;
}

FTexturePlayback* FTextureLibrary::FindPlayback(int Id)
{
    if (Id == -1)
        return nullptr;
    const std::map<int, FTexturePlayback>::iterator Found = PlaybackRecords.find(Id);
    return Found == PlaybackRecords.end() ? nullptr : &Found->second;
}

FTextureGroup* FTextureLibrary::FindGroup(int Id)
{
    const std::map<int, FTextureGroup>::iterator Found = GroupRecords.find(Id);
    return Found == GroupRecords.end() ? nullptr : &Found->second;
}

FTextureEntry* FTextureLibrary::FindEntryByCrc(unsigned long Crc)
{
    const std::map<unsigned long, FTextureEntry*>::iterator Found = CanonicalRecords.find(Crc);
    return Found == CanonicalRecords.end() ? nullptr : Found->second;
}

void FTextureLibrary::AddRefEntryRecursive(FTextureEntry* Entry)
{
    // FUN_06ebe560 retains the record first, then all children of multi-frame records.
    Entry->RefCount = (int)((unsigned int)Entry->RefCount + 1u);
    if (Entry->Frames.data() == nullptr || Entry->Frames.size() <= 1)
        return;
    for (size_t Index = 0; Index < Entry->Children.size(); ++Index)
        AddRefEntryRecursive(Entry->Children[Index]);
}

static void WriteEmptyFrame(unsigned long* Out)
{
    Out[0] = 0;
    Out[1] = 0;
    Out[2] = 0;
    Out[3] = 0x3f800000;
    Out[4] = 0x3f800000;
}

static GENRESULT ReadPlaybackFrame(FTexturePlayback* Playback, unsigned int Frame, unsigned long* Out)
{
    // FUN_06ebe7f0: cache count lazily, then select the child via the frame's first word.
    if (Playback->FrameCount == 0)
        Playback->FrameCount = (unsigned int)Playback->Source->Frames.size();
    const unsigned int Count = Playback->FrameCount;
    if (Count == 0)
    {
        WriteEmptyFrame(Out);
        return GR_GENERIC;
    }
    unsigned int Selected = Frame;
    if (Selected == 0xffffffffu)
        Selected = Count <= 1 ? 0 : Playback->CurrentFrame;
    else if (Selected >= Count)
        Selected = Count - 1;
    const FTextureFrame& Value = Playback->Source->Frames[Selected];
    Out[0] = Playback->Source->Children[Value.ChildIndex]->GpuHandle;
    std::memcpy(Out + 1, Value.Uv, sizeof(Value.Uv));
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::GetFrame(int Id, int Frame, unsigned long* Out)
{
    // FUN_06ec3310 ignores the frame core's return for a valid ID.
    FTexturePlayback* Playback = FindPlayback(Id);
    if (Playback == nullptr)
        return GR_GENERIC;
    ReadPlaybackFrame(Playback, (unsigned int)Frame, Out);
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::GetTextureFrame(void* Tex, int Frame, unsigned long* Out)
{
    // LAB_06ec2ee0: canonical source; only -2 selects the last frame.
    FTextureEntry* Source = (FTextureEntry*)Tex;
    if (Source != nullptr)
    {
        const unsigned int Count = (unsigned int)Source->Frames.size();
        unsigned int Selected = (unsigned int)Frame;
        if (Selected == 0xfffffffeu)
            Selected = Count - 1;
        if (Selected < Count)
        {
            const FTextureFrame& Value = Source->Frames[Selected];
            Out[0] = Source->Children[Value.ChildIndex]->GpuHandle;
            std::memcpy(Out + 1, Value.Uv, sizeof(Value.Uv));
            return GR_OK;
        }
    }
    WriteEmptyFrame(Out);
    return GR_GENERIC;
}

GENRESULT DACOM_API FTextureLibrary::FindTextureByCrc(void* Query, void** OutTex)
{
    // LAB_06ec2810 RET 0xC: CRC at Query+4 → map this+0x54 (FUN_06ecaa00).
    // Hit: FUN_06ebe560(tex) then *OutTex = node+0x10. Miss: EAX=-1.
    const unsigned long Crc = *(unsigned long*)((unsigned char*)Query + 4);
    FTextureEntry* Entry = FindEntryByCrc(Crc);
    if (Entry == nullptr)
        return GR_GENERIC;
    // A returned record owns a reference only when the caller requests it.
    if (OutTex != nullptr)
    {
        AddRefEntryRecursive(Entry);
        *OutTex = Entry;
    }
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::PickGpuFormat(const unsigned long* Src, unsigned long* OutId)
{
    // LAB_06ec37f0 directly forwards to FUN_06ebf110.
    const int SrcId = (int)*Src;
    if (SrcId >= 10 && SrcId <= 14)
    {
        *OutId = (unsigned long)SrcId;
        return GR_OK;
    }
    for (size_t Index = 0; Index < GFormatMaps.size(); ++Index)
    {
        if (FormatMapRowMatches(GFormatMaps[Index], SrcId))
        {
            *OutId = (unsigned long)FourCcToFormatId(GFormatMaps[Index].FourCc);
            return GR_OK;
        }
    }
    return GR_GENERIC; // Empty/miss leaves mandatory output unchanged.
}

// --- Cycle 6 high-confidence accessors -----------------

GENRESULT DACOM_API FTextureLibrary::SetGenericSlot(int Index, unsigned long Value)
{
    // LAB_06ec0d10: no bounds branch in the indexed write.
    GenericSlots[Index] = Value;
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::GetGenericSlot(int Index, unsigned long* Out)
{
    // LAB_06ec0d30: no bounds or output-null branch.
    *Out = GenericSlots[Index];
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::GarbageCollect(int GroupId)
{
    if (GroupId != 0)
    {
        const std::map<int, FTextureGroup>::iterator Group = GroupRecords.find(GroupId);
        if (Group != GroupRecords.end())
        {
            for (size_t Index = 0; Index < Group->second.Members.size(); ++Index)
                ReleasePlaybackReference(Group->second.Members[Index]);
            GroupRecords.erase(Group);
        }
    }
    for (std::map<unsigned long, FTextureEntry*>::iterator It = CanonicalRecords.begin();
         It != CanonicalRecords.end();)
    {
        FTextureEntry* Entry = It->second;
        if (Entry->RefCount <= 0)
        {
            DestroyEntry(Entry);
            It = CanonicalRecords.erase(It);
        }
        else
            ++It;
    }
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::TickAllPlayback(int Unused, float DeltaSeconds)
{
    // LAB_06ec2060: signed-ID order, shared rounded millisecond argument.
    if (Unused != 0)
        return GR_OK;
    if (!ActivePlayback.empty())
    {
        const float Milliseconds = (float)((double)DeltaSeconds * 1000.0);
        for (std::map<int, FTexturePlayback*>::iterator It = ActivePlayback.begin();
             It != ActivePlayback.end(); ++It)
        {
            AdvancePlayback(It->second, Milliseconds);
        }
    }
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::GetGroupPixelFormat(void* Record, void* Unused, int* OutFormat)
{
    (void)Unused;
    FTextureEntry* Entry = (FTextureEntry*)Record;
    if (Entry != nullptr && !Entry->Frames.empty())
    {
        const unsigned long Handle = Entry->Children[0]->GpuHandle;
        if (Handle != 0)
            return PipeGetTextureFormat(Pipeline, Handle, OutFormat);
    }
    *OutFormat = 0;
    return GR_GENERIC;
}

GENRESULT DACOM_API FTextureLibrary::SetGroupName(int Id, const char* Name)
{
    if (Id == -1 || Id == 0)
        return GR_GENERIC;
    FTextureGroup* Group = FindGroup(Id);
    if (Group == nullptr)
        return GR_GENERIC;
    Group->SetName(Name);
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::GetGroupName(int Id, char* Buf, unsigned long Size)
{
    if (Id == -1 || Id == 0)
        return GR_GENERIC;
    FTextureGroup* Group = FindGroup(Id);
    if (Group == nullptr)
        return GR_GENERIC;
    if (Group->Name != nullptr)
    {
        std::strncpy(Buf, Group->Name.get(), Size);
        Buf[Size - 1u] = '\0';
    }
    else
        *Buf = '\0';
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::FindOrCreateByName(const char* Name, void** OutTex)
{
    // LAB_06ec2760 returns -1 on an existing CRC; only a miss creates then queries.
    const unsigned long NameCrc = DACOM_CRC::GetCRC32(Name);
    if (FindEntryByCrc(NameCrc) != nullptr)
        return GR_GENERIC;
    FTextureEntry* Entry;
    CreateTextureRecord(0, Name, &Entry, false);
    const unsigned long Query[2] = { 0, DACOM_CRC::GetCRC32(Name) };
    // The query stays read-only; the shared lookup has an opaque mutable signature.
    return FindTextureByCrc(const_cast<unsigned long*>(Query), OutTex);
}

GENRESULT DACOM_API FTextureLibrary::CreatePlaybackGroup(void* SourceRecord, int* OutId)
{
    // LAB_06ec3050: canonical retention precedes optional ID allocation.
    if (OutId != nullptr)
        *OutId = -1;
    FTextureEntry* Source = (FTextureEntry*)SourceRecord;
    if (Source == nullptr)
        return GR_GENERIC;
    AddRefEntryRecursive(Source);
    if (OutId == nullptr)
        return GR_OK;

    const int Id = NextPlaybackId;
    NextPlaybackId = (int)((unsigned int)NextPlaybackId + 1u);
    FTexturePlayback& Playback = PlaybackRecords.try_emplace(Id).first->second;
    InitializePlayback(&Playback, Source);
    ++Playback.RefCount;
    if (Playback.FrameCount > 1)
        ActivePlayback[Id] = &Playback;
    *OutId = Id;
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::ReleaseTextureRecord(void* Record)
{
    // LAB_06ec2870: Record==0 → −1; else FUN_06ec0bd0(Record, flag=1) and return 0.
    // Arg is the texture record pointer (Slot12 OutTex), not an integer id.
    if (Record == nullptr)
        return GR_GENERIC;
    ReleaseEntryReference((FTextureEntry*)Record);
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::AddRefById(int Id)
{
    // FUN_06ec3140: the ID record owns its own count; source count is unchanged.
    FTexturePlayback* Playback = FindPlayback(Id);
    if (Playback == nullptr)
        return GR_GENERIC;
    ++Playback->RefCount;
    return GR_OK;
}

int FTextureLibrary::ReleasePlaybackReference(int Id)
{
    // FUN_06ec31a0: unsigned decrement is unconditional, including zero.
    const std::map<int, FTexturePlayback>::iterator Found = PlaybackRecords.find(Id);
    if (Id == -1 || Found == PlaybackRecords.end())
        return -1;
    const unsigned int Remaining = --Found->second.RefCount;
    if (Remaining == 0)
    {
        ReleaseEntryReference(Found->second.Source);
        PlaybackRecords.erase(Found);
        ActivePlayback.erase(Id);
    }
    return (int)Remaining;
}

GENRESULT DACOM_API FTextureLibrary::DecRefById(int Id)
{
    // LAB_06ec3260 maps exactly -1 to failure; other signed counts return 0.
    return ReleasePlaybackReference(Id) == -1 ? GR_GENERIC : GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::CopyName(void* Node, char* Buf, unsigned long Size)
{
    // LAB_06ec2890: clear first, then copy only min(strlen, unsigned(Size-1)).
    *Buf = '\0';
    FTextureEntry* Entry = (FTextureEntry*)Node;
    if (Entry == nullptr)
        return GR_GENERIC;
    const char* Source = Entry->GetName();
    const unsigned long Length = (unsigned long)std::strlen(Source);
    const unsigned long Capacity = Size - 1u;
    const unsigned long Count = Length < Capacity ? Length : Capacity;
    std::memcpy(Buf, Source, Count);
    Buf[Count] = '\0';
    return GR_OK;
}

static FTextureFrame* FindMutableFrame(FTextureEntry* Entry, int Index)
{
    if (Entry == nullptr)
        return nullptr;
    const unsigned int Count = (unsigned int)Entry->Frames.size();
    unsigned int Selected = (unsigned int)Index;
    if (Selected == 0xfffffffeu)
        Selected = Count - 1;
    if (Selected >= Count)
        return nullptr;
    return &Entry->Frames[Selected];
}

GENRESULT DACOM_API FTextureLibrary::SetFrameHandle(void* Record, int Index, unsigned long Handle)
{
    // LAB_06ec2bf0: the first word is a child index, never a GPU handle.
    FTextureFrame* Frame = FindMutableFrame((FTextureEntry*)Record, Index);
    if (Frame == nullptr)
        return GR_GENERIC;
    Frame->ChildIndex = Handle;
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::SetFrameRect(void* Record, int Index, float U0, float V0,
                                                  float U1, float V1)
{
    FTextureFrame* Frame = FindMutableFrame((FTextureEntry*)Record, Index);
    if (Frame == nullptr)
        return GR_GENERIC;
    Frame->Uv[0] = U0;
    Frame->Uv[1] = V0;
    Frame->Uv[2] = U1;
    Frame->Uv[3] = V1;
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::SetTileRate(void* Record, float Value)
{
    // LAB_06ec2fc0: record+0xc = value.
    FTextureEntry* Entry = (FTextureEntry*)Record;
    if (Entry == nullptr)
        return GR_GENERIC;
    Entry->TileRate = Value;
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::GetTileRate(void* Record, float* Out)
{
    // LAB_06ec2fe0: *out=record+0xc.
    *Out = 0.0f;
    FTextureEntry* Entry = (FTextureEntry*)Record;
    if (Entry == nullptr)
        return GR_GENERIC;
    *Out = Entry->TileRate;
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::GetFrameCount(void* Record, int* Out)
{
    // LAB_06ec3000: *out=(end-begin)/0x14.
    *Out = 0;
    FTextureEntry* Entry = (FTextureEntry*)Record;
    if (Entry == nullptr)
        return GR_GENERIC;
    // LAB_06ec3016: null begin returns -1; an allocated empty range succeeds.
    if (Entry->Frames.data() == nullptr)
        return GR_GENERIC;
    *Out = (int)Entry->Frames.size();
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::SetElapsedTime(int Id, float Seconds)
{
    // FUN_06ebe8c0 via slot 29: absolute set, then recompute current frame.
    FTexturePlayback* Entry = FindPlayback(Id);
    if (Entry == nullptr)
        return GR_GENERIC;
    SetPlaybackElapsed(Entry, Seconds);
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::GetElapsedTime(int Id, float* Out)
{
    // Retail ID miss leaves Out untouched, including Id == -1.
    if (Id == -1)
        return GR_GENERIC;
    FTexturePlayback* Entry = FindPlayback(Id);
    if (Entry == nullptr)
        return GR_GENERIC;
    *Out = (float)Entry->ElapsedTime;
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::SetFrameIndex(int Id, unsigned int Index)
{
    // FUN_06ebe940: bounds-checked write of the current frame index.
    FTexturePlayback* Entry = FindPlayback(Id);
    if (Entry == nullptr)
        return GR_GENERIC;
    SetPlaybackFrame(Entry, Index);
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::GetFrameIndex(int Id, unsigned int* Out)
{
    // Retail ID miss leaves Out untouched, including Id == -1.
    if (Id == -1)
        return GR_GENERIC;
    FTexturePlayback* Entry = FindPlayback(Id);
    if (Entry == nullptr)
        return GR_GENERIC;
    *Out = Entry->CurrentFrame;
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::SetLocalRate(int Id, float Value)
{
    // FUN_06ebe980: store a local rate and redirect the active-rate alias to it.
    // This only recomputes duration; elapsed/current-frame remain untouched.
    FTexturePlayback* Entry = FindPlayback(Id);
    if (Entry == nullptr)
        return GR_GENERIC;
    Entry->LocalRate = Value;
    Entry->bUseLocalRate = true;
    RecomputePlaybackDuration(Entry);
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::GetEffectiveRate(int Id, float* Out)
{
    // Retail ID miss leaves Out untouched, including Id == -1.
    if (Id == -1)
        return GR_GENERIC;
    FTexturePlayback* Entry = FindPlayback(Id);
    if (Entry == nullptr)
        return GR_GENERIC;
    *Out = EffectivePlaybackRate(Entry);
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::SetWrapMode(int Id, int Mode)
{
    // LAB_06ec3620: record+0x44 = mode (0 clamp, 1 loop, 2 ping-pong). The
    // setter does not recompute elapsed time or the current frame.
    FTexturePlayback* Entry = FindPlayback(Id);
    if (Entry == nullptr)
        return GR_GENERIC;
    Entry->WrapMode = Mode;
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::GetWrapMode(int Id, int* Out)
{
    FTexturePlayback* Entry = FindPlayback(Id);
    if (Entry == nullptr)
        return GR_GENERIC;
    *Out = Entry->WrapMode;
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::AdvanceTime(int Id, float DeltaSeconds)
{
    // FUN_06ec3700 rounds seconds*1000 to float before the millisecond core.
    FTexturePlayback* Entry = FindPlayback(Id);
    if (Entry == nullptr)
        return GR_GENERIC;
    const float Milliseconds = (float)((double)DeltaSeconds * 1000.0);
    AdvancePlayback(Entry, Milliseconds);
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::PeekNextGroupId(unsigned long* Out)
{
    // LAB_06ec2240 reads tree size +0x4c, not allocation counter +0x50.
    *Out = (unsigned long)GroupRecords.size() + 1u;
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::GetSubCount(int Id, int* Out)
{
    if (Id == 0)
    {
        // Retail +0x64 is the size of the canonical CRC tree +0x54.
        *Out = (int)CanonicalRecords.size();
        return GR_OK;
    }
    FTextureGroup* Group = FindGroup(Id);
    if (Group == nullptr)
        return GR_GENERIC;
    *Out = (int)Group->Members.size();
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::EnumTexture(int Id, int Index, void** OutRecord)
{
    if (Id == 0)
    {
        std::map<unsigned long, FTextureEntry*>::iterator It = CanonicalRecords.begin();
        unsigned int Ordinal = 0;
        while (It != CanonicalRecords.end() && Ordinal < (unsigned int)Index)
        {
            ++It;
            ++Ordinal;
        }
        if (It == CanonicalRecords.end())
            return GR_GENERIC;
        if (OutRecord != nullptr)
        {
            AddRefEntryRecursive(It->second);
            *OutRecord = It->second;
        }
        // 06ec23ee resets ESI before returning, independent of the ordinal.
        return GR_OK;
    }
    FTextureGroup* Group = FindGroup(Id);
    if (Group == nullptr || (unsigned int)Index >= Group->Members.size())
        return GR_GENERIC;
    ResolveTextureRecord(Group->Members[(unsigned int)Index], OutRecord);
    return GR_OK; // Retail discards the resolver HRESULT.
}

GENRESULT DACOM_API FTextureLibrary::AddSubTexture(int GroupId, void* Record)
{
    if (GroupId == 0)
        return GR_GENERIC;
    FTextureGroup* Group = FindGroup(GroupId);
    if (Group == nullptr)
        return GR_GENERIC;
    int PlaybackId;
    if (CreatePlaybackGroup(Record, &PlaybackId) < 0)
        return GR_GENERIC;
    Group->Members.push_back(PlaybackId);
    return GR_OK;
}

GENRESULT DACOM_API FTextureLibrary::RemoveSubTexture(int GroupId, void* Record)
{
    if (GroupId == 0)
        return GR_GENERIC;
    FTextureGroup* Group = FindGroup(GroupId);
    if (Group == nullptr)
        return GR_GENERIC;
    size_t Index = 0;
    while (Index != Group->Members.size())
    {
        void* Resolved;
        if (ResolveTextureRecord(Group->Members[Index], &Resolved) >= 0)
        {
            ReleaseTextureRecord(Resolved);
            if (Resolved == Record)
            {
                DecRefById(Group->Members[Index]);
                Group->Members.erase(Group->Members.begin() + Index);
                return GR_OK;
            }
            ++Index;
        }
        // 06ec26f0 jumps to the end comparison without increment on failure.
    }
    return GR_GENERIC;
}

GENRESULT DACOM_API FTextureLibrary::AllocateNewTextureId(int* OutId)
{
    // FUN_06ec1950 creates an empty named-group payload, no canonical texture.
    FTextureGroup& Group = GroupRecords.try_emplace(NextId).first->second;
    Group.SetName(nullptr);
    Group.Members.clear();
    *OutId = NextId;
    NextId = (int)((unsigned int)NextId + 1u);
    return GR_OK;
}

// --- TextureLibrary slots recovered from the retail vtable/listing ---

GENRESULT DACOM_API FTextureLibrary::SetRecordName(void* Record, const char* Name)
{
    // LAB_06ec28e0 probes the incoming name, then erases that node before renaming Record.
    const unsigned long Key = DACOM_CRC::GetCRC32(Name);
    const std::map<unsigned long, FTextureEntry*>::iterator Found = CanonicalRecords.find(Key);
    FTextureEntry* Entry = (FTextureEntry*)Record;
    if (Found == CanonicalRecords.end() || Entry == nullptr)
        return GR_GENERIC;
    CanonicalRecords.erase(Found);
    Entry->SetName(Name);
    StoreByNameCrc(Entry);
    return GR_OK;
}
GENRESULT DACOM_API FTextureLibrary::SetSubTexture(void* Record, int Index, int TextureId)
{
    FTextureEntry* Parent = (FTextureEntry*)Record;
    if (Parent == nullptr)
        return GR_GENERIC;
    FTextureEntry* Child = nullptr;
    const unsigned long Handle = (unsigned long)TextureId;
    for (std::map<unsigned long, FTextureEntry*>::const_iterator It = CanonicalRecords.begin();
         It != CanonicalRecords.end(); ++It)
    {
        if (It->second->GpuHandle == Handle)
        {
            Child = It->second;
            break;
        }
    }
    const unsigned int ChildIndex = (unsigned int)Index;
    const unsigned int RequiredCount = ChildIndex + 1u;
    if (Child != nullptr)
    {
        if (ChildIndex < Parent->Children.size())
            ReleaseEntryReference(Parent->Children[ChildIndex], true);
        // 2a5c sets ECX=EBP: retail retains the parent, not the replacement child.
        if (Parent != Child)
            AddRefEntryRecursive(Parent);
        if (Parent->Children.size() < RequiredCount)
            Parent->Children.resize(RequiredCount, nullptr);
        Parent->Children[ChildIndex] = Child;
    }
    else
    {
        char AnonymousName[64];
        std::sprintf(AnonymousName, "anon%i", TextureId);
        if (!CreateTextureRecord(Handle, AnonymousName, &Child, false))
            return GR_GENERIC;
        if (Child->Children.size() < RequiredCount)
            Child->Children.resize(RequiredCount, nullptr);
        Child->Children[ChildIndex] = Child;
    }
    return GR_OK;
}
GENRESULT DACOM_API FTextureLibrary::ResolveTextureRecord(int Id, void** Out)
{
    // FUN_06ec3290 retains Source before proving it is indexed by name.
    FTexturePlayback* Playback = FindPlayback(Id);
    if (Playback == nullptr)
        return GR_GENERIC;
    FTextureEntry* Source = Playback->Source;
    AddRefEntryRecursive(Source);
    for (std::map<unsigned long, FTextureEntry*>::const_iterator It = CanonicalRecords.begin();
         It != CanonicalRecords.end(); ++It)
    {
        if (It->second == Source)
        {
            *Out = Source;
            return GR_OK;
        }
    }
    return GR_GENERIC;
}
GENRESULT DACOM_API FTextureLibrary::EnumGroup(int Index, int* OutId)
{
    if ((unsigned int)Index >= (unsigned int)GroupRecords.size() + 1u)
        return GR_GENERIC;
    if (Index == 0)
    {
        *OutId = 0;
        return GR_OK;
    }
    std::map<int, FTextureGroup>::iterator It = GroupRecords.begin();
    for (int Ordinal = 1; Ordinal < Index; ++Ordinal)
        ++It;
    *OutId = It->first;
    return GR_OK;
}
