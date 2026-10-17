// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Mgsdb.h"
#include "msvcrt_compat.h"

#include <windows.h>
#include <winver.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#pragma comment(lib, "version.lib")

// Hardware-quirk database scanner and matching passes. RP9 copy of src/RP8/Mgsdb.cpp: every
// function here is the same-named one there, in the same order (mirror RP8 fixes here). Only
// the identifier/caps types differ (Rp9D3d8Types.h, D3D8 layout).

namespace
{
    // DAT_06d70748: the 0x100-byte global that FUN_06d23639 copies a quoted string into.
    char G_MgsdbQuoted[0x100];

    // "true" result of FUN_06d23e0a (MOV EAX,1); null is "false", anything else is a message.
    const char* const kMgsdbTrue = (const char*)1;

    // FUN_06d23c7f: the characters that may follow a keyword.
    bool MgsdbIsSeparator(char C)
    {
        return C == '>' || C == '<' || C == '!' || C == '=' || C == ' ' || C == '\r' || C == '\t';
    }

    // FUN_06d23e0a's caps keys: object+0x488 + offset in D3DCAPS8 (06d23e4a..06d24b8e). RP8
    // lists the raw offsets; here they are the FRp9Caps8 members at those offsets (pinned by
    // the static_asserts in Rp9D3d8Types.h). "MaxTextureHeight" (0x5C) has no key, as in RP8.
    struct FMgsdbCapsKey
    {
        const char* Name;
        unsigned int CapsOffset;
    };

    const FMgsdbCapsKey kMgsdbCapsKeys[] = {
        {"Caps", offsetof(FRp9Caps8, Caps)},
        {"Caps2", offsetof(FRp9Caps8, Caps2)},
        {"Caps3", offsetof(FRp9Caps8, Caps3)},
        {"PresentationIntervals", offsetof(FRp9Caps8, PresentationIntervals)},
        {"CursorCaps", offsetof(FRp9Caps8, CursorCaps)},
        {"DevCaps", offsetof(FRp9Caps8, DevCaps)},
        {"PrimitiveMiscCaps", offsetof(FRp9Caps8, PrimitiveMiscCaps)},
        {"RasterCaps", offsetof(FRp9Caps8, RasterCaps)},
        {"ZCmpCaps", offsetof(FRp9Caps8, ZCmpCaps)},
        {"SrcBlendCaps", offsetof(FRp9Caps8, SrcBlendCaps)},
        {"DestBlendCaps", offsetof(FRp9Caps8, DestBlendCaps)},
        {"AlphaCmpCaps", offsetof(FRp9Caps8, AlphaCmpCaps)},
        {"ShadeCaps", offsetof(FRp9Caps8, ShadeCaps)},
        {"TextureCaps", offsetof(FRp9Caps8, TextureCaps)},
        {"TextureFilterCaps", offsetof(FRp9Caps8, TextureFilterCaps)},
        {"CubeTextureFilterCaps", offsetof(FRp9Caps8, CubeTextureFilterCaps)},
        {"VolumeTextureFilterCaps", offsetof(FRp9Caps8, VolumeTextureFilterCaps)},
        {"TextureAddressCaps", offsetof(FRp9Caps8, TextureAddressCaps)},
        {"VolumeTextureAddressCaps", offsetof(FRp9Caps8, VolumeTextureAddressCaps)},
        {"LineCaps", offsetof(FRp9Caps8, LineCaps)},
        {"MaxTextureWidth", offsetof(FRp9Caps8, MaxTextureWidth)},
        {"MaxVolumeExtent", offsetof(FRp9Caps8, MaxVolumeExtent)},
        {"MaxTextureRepeat", offsetof(FRp9Caps8, MaxTextureRepeat)},
        {"MaxTextureAspectRatio", offsetof(FRp9Caps8, MaxTextureAspectRatio)},
        {"MaxAnisotropy", offsetof(FRp9Caps8, MaxAnisotropy)},
        {"StencilCaps", offsetof(FRp9Caps8, StencilCaps)},
        {"FVFCaps", offsetof(FRp9Caps8, FVFCaps)},
        {"TextureOpCaps", offsetof(FRp9Caps8, TextureOpCaps)},
        {"MaxTextureBlendStages", offsetof(FRp9Caps8, MaxTextureBlendStages)},
        {"MaxSimultaneousTextures", offsetof(FRp9Caps8, MaxSimultaneousTextures)},
        {"VertexProcessingCaps", offsetof(FRp9Caps8, VertexProcessingCaps)},
        {"MaxActiveLights", offsetof(FRp9Caps8, MaxActiveLights)},
        {"MaxUserClipPlanes", offsetof(FRp9Caps8, MaxUserClipPlanes)},
        {"MaxVertexBlendMatrices", offsetof(FRp9Caps8, MaxVertexBlendMatrices)},
        {"MaxVertexBlendMatrixIndex", offsetof(FRp9Caps8, MaxVertexBlendMatrixIndex)},
        {"MaxPrimitiveCount", offsetof(FRp9Caps8, MaxPrimitiveCount)},
        {"MaxVertexIndex", offsetof(FRp9Caps8, MaxVertexIndex)},
        {"MaxStreams", offsetof(FRp9Caps8, MaxStreams)},
        {"MaxStreamStride", offsetof(FRp9Caps8, MaxStreamStride)},
        {"VertexShaderVersion", offsetof(FRp9Caps8, VertexShaderVersion)},
        {"MaxVertexShaderConst", offsetof(FRp9Caps8, MaxVertexShaderConst)},
        {"PixelShaderVersion", offsetof(FRp9Caps8, PixelShaderVersion)},
    };
}

// ---- record set ----

FMgsdbRecordSet::FMgsdbRecordSet(FMgsdbDatabase* InOwner) : Owner(InOwner)
{
}

void FMgsdbRecordSet::Insert(const char* Name, const char* Value)
{
    for (size_t Index = 0; Index < Records.size(); ++Index)
    {
        if (_stricmp(Records[Index].Name.c_str(), Name) == 0)
        {
            Records[Index].Value = Value;
            return;
        }
    }
    FMgsdbRecord Record;
    Record.Name = Name;
    Record.Value = Value;
    Records.push_back(Record);
}

bool FMgsdbRecordSet::Find(const char* Name, char* Out, size_t OutSize) const
{
    for (size_t Index = 0; Index < Records.size(); ++Index)
    {
        if (_stricmp(Records[Index].Name.c_str(), Name) == 0)
        {
            strncpy(Out, Records[Index].Value.c_str(), OutSize);
            return true;
        }
    }
    return false;
}

void MgsdbReplay(FMgsdbRecordSet* Target, const FMgsdbRecordSet* Source)
{
    for (size_t Index = 0; Index < Source->Records.size(); ++Index)
        Target->Insert(Source->Records[Index].Name.c_str(), Source->Records[Index].Value.c_str());
}

// ---- object ----

FMgsdbDatabase* FMgsdbDatabase::Create()
{
    return new (std::nothrow) FMgsdbDatabase();
}

// FUN_06d25e9a leaves the scanner fields, the three counters and the two copied structs
// unwritten (Load writes all of them before anything reads them); they start zeroed here.
FMgsdbDatabase::FMgsdbDatabase()
    : Buffer_(nullptr), Cursor_(nullptr), End_(nullptr), LineStart_(nullptr), Line_(0),
      Records_(nullptr), bError_(false), CpuSpeed_(0), Ram_(0), VideoRam_(0)
{
    memset(&Ident_, 0, sizeof(Ident_));
    memset(&Caps_, 0, sizeof(Caps_));
}

// FUN_06d26172: the live record set, then every propertyset's record set; the detail map only
// aliases sets owned elsewhere. The strings and maps release themselves.
FMgsdbDatabase::~FMgsdbDatabase()
{
    delete Records_;
    for (std::map<std::string, FMgsdbRecordSet*>::iterator It = PropertySets_.begin();
         It != PropertySets_.end(); ++It)
    {
        delete It->second;
    }
}

void FMgsdbDatabase::Destroy()
{
    delete this;
}

FMgsdbRecordSet* FMgsdbDatabase::GetRecords()
{
    return Records_;
}

FMgsdbRecordSet* FMgsdbDatabase::FindPropertySet(const char* Name)
{
    std::map<std::string, FMgsdbRecordSet*>::iterator It = PropertySets_.find(std::string(Name));
    if (It == PropertySets_.end())
        return nullptr;
    return It->second;
}

const char* FMgsdbDatabase::GetDeviceName() const
{
    return DeviceName_.c_str();
}

const char* FMgsdbDatabase::GetVendorName() const
{
    return VendorName_.c_str();
}

bool FMgsdbDatabase::HasError() const
{
    return bError_;
}

const char* FMgsdbDatabase::GetError() const
{
    return Error_.c_str();
}

// The scanner buffer ends with one CR sentinel; remaining slack is uninitialized.
bool FMgsdbDatabase::Load(const char* Path, const FRp9Identifier8* Ident,
                          const FRp9Caps8* Caps, unsigned long Ram, unsigned long VideoRam,
                          unsigned long CpuSpeed)
{
    if (Records_ == nullptr)
        Records_ = new (std::nothrow) FMgsdbRecordSet(this);
    CpuSpeed_ = CpuSpeed;
    Ram_ = Ram;
    VideoRam_ = VideoRam;
    memcpy(&Ident_, Ident, sizeof(Ident_));
    memcpy(&Caps_, Caps, sizeof(Caps_));
    bError_ = false;

    if ((Ident_.DriverVersion.HighPart | Ident_.DriverVersion.LowPart) == 0)
    {
        DWORD Handle = 0;
        const DWORD Size = GetFileVersionInfoSizeA(Ident_.Driver, &Handle);
        if (Size != 0)
        {
            unsigned char* Block = new unsigned char[Size];
            BOOL bOk = GetFileVersionInfoA(Ident_.Driver, Handle, Size, Block);
            if (bOk != 0)
            {
                void* Fixed = nullptr;
                UINT FixedSize = 0;
                bOk = VerQueryValueA(Block, "\\", &Fixed, &FixedSize);
                if (bOk != 0)
                {
                    unsigned long Info[13];
                    memcpy(Info, Fixed, sizeof(Info));
                    Ident_.DriverVersion.HighPart = (LONG)Info[2];
                    Ident_.DriverVersion.LowPart = Info[3];
                }
            }
            delete[] Block;
        }
    }

    const HANDLE File = CreateFileA(Path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    0, nullptr);
    if (File == INVALID_HANDLE_VALUE)
    {
        Error_ = "Cannot find file";
        return false;
    }
    const DWORD Size = GetFileSize(File, nullptr);
    Buffer_ = new char[Size + 0x10];
    DWORD Read = 0;
    ReadFile(File, Buffer_, Size, &Read, nullptr);
    CloseHandle(File);
    Buffer_[Size] = '\r';
    Cursor_ = Buffer_;
    LineStart_ = Cursor_;
    Line_ = 1;
    End_ = Buffer_ + Size;

    bool bResult = false;
    if (ReadPropertySets() && ReadLeadingApplyToAll() && ReadVendorDevice() &&
        ReadTrailingApplyToAll())
    {
        bResult = true;
    }
    delete[] Buffer_;
    return bResult;
}

// ---- scanner ----

void FMgsdbDatabase::SkipLine()
{
    do
    {
        ++Cursor_;
        if (Cursor_[-1] == '\r')
            break;
    } while (Cursor_ < End_);
    if (Cursor_ < End_ && *Cursor_ == '\n')
        ++Cursor_;
    LineStart_ = Cursor_;
    ++Line_;
}

void FMgsdbDatabase::SkipWhite()
{
    while (*Cursor_ == ' ' || *Cursor_ == '\t')
        ++Cursor_;
}

// Only the first error is kept: "<message> on line <n> - '<first 0x24 chars of the line>'".
void FMgsdbDatabase::SetError(const char* Message)
{
    if (bError_)
        return;
    char Snippet[0x28];
    const char* Source = LineStart_;
    char* Dest = Snippet;
    while (*Source != '\r' && Dest != Snippet + 0x24)
        *Dest++ = *Source++;
    if (Dest == Snippet + 0x24)
    {
        *Dest++ = '.';
        *Dest++ = '.';
        *Dest++ = '.';
    }
    *Dest = '\0';
    char Text[0x100];
    _snprintf(Text, 0x100, "%s on line %d - '%s'", Message, (int)Line_, Snippet);
    Error_ = Text;
    bError_ = true;
}

int FMgsdbDatabase::ReadHexDigit()
{
    const char C = *Cursor_;
    if (C >= '0' && C <= '9')
    {
        ++Cursor_;
        return C - '0';
    }
    if (C >= 'a' && C <= 'f')
    {
        ++Cursor_;
        return C - 0x57;
    }
    if (C > '@' && C < 'G')
    {
        ++Cursor_;
        return C - 0x37;
    }
    return -1;
}

unsigned long FMgsdbDatabase::ReadNumber()
{
    SkipWhite();
    if (Cursor_[0] == '0' && Cursor_[1] == 'x')
    {
        Cursor_ += 2;
        int Digit = ReadHexDigit();
        if (Digit == -1)
        {
            SetError("Number expected");
            return 0xFFFFFFFF;
        }
        unsigned long Value = 0;
        unsigned long Count = 0;
        do
        {
            if (Count > 7)
            {
                SetError("Number too large");
                return 0xFFFFFFFF;
            }
            Value = Value * 0x10 + (unsigned long)Digit;
            ++Count;
            Digit = ReadHexDigit();
        } while (Digit != -1);
        SkipWhite();
        return Value;
    }
    if (*Cursor_ < '0' || *Cursor_ > '9')
    {
        SetError("Number expected");
        return 0xFFFFFFFF;
    }
    unsigned long Value = (unsigned long)ReadHexDigit();
    if (Value == 0xFFFFFFFF)
    {
        SetError("Number expected");
        return 0xFFFFFFFF;
    }
    for (;;)
    {
        if (!(*Cursor_ > '/' && *Cursor_ < ':'))
            break;
        const int Digit = ReadHexDigit();
        if (Digit == -1)
            break;
        Value = Value * 10 + (unsigned long)Digit;
    }
    SkipWhite();
    return Value;
}

// No end-of-line stop: up to 0xFF characters until the closing quote.
const char* FMgsdbDatabase::ReadQuoted()
{
    SkipWhite();
    const char First = *Cursor_++;
    if (First != '"')
    {
        SetError("Expecting ");
        return nullptr;
    }
    char* Out = G_MgsdbQuoted;
    do
    {
        if (*Cursor_ == '"')
        {
            *Out = '\0';
            ++Cursor_;
            SkipWhite();
            return G_MgsdbQuoted;
        }
        *Out++ = *Cursor_++;
    } while (Out <= G_MgsdbQuoted + 0xFF);
    SetError("String too long");
    return nullptr;
}

// "name" or "name = value": the name and an unquoted value are lowercased, a quoted value is
// kept with both quotes (the cursor stays on the closing one). Returns an error message or null.
const char* FMgsdbDatabase::ReadFlag(FMgsdbRecordSet* Target)
{
    char Name[0x100];
    char* Pos = Name;
    while (*Cursor_ != ' ' && *Cursor_ != '=' && *Cursor_ != '\r')
    {
        char C = *Cursor_++;
        if (C > '@' && C < '[')
            C = (char)(C + ' ');
        *Pos++ = C;
        if (Pos == Name + 0xFE)
            return "Flag too long";
    }
    *Pos = '\0';
    SkipWhite();

    char Value[0x100];
    Pos = Value;
    if (*Cursor_ != '\r')
    {
        if (*Cursor_ != '=')
            return "flag = xxx expected";
        ++Cursor_;
        SkipWhite();
        if (*Cursor_ == '"')
        {
            do
            {
                *Pos++ = *Cursor_++;
                if (Pos == Value + 0xFE)
                    return "Flag too long";
            } while (*Cursor_ != '"' && *Cursor_ != '\r');
            if (*Cursor_ != '"')
                return "Missing Quote";
            *Pos++ = '"';
            if (Pos == Value + 0xFE)
                return "Flag too long";
        }
        else
        {
            while (*Cursor_ != ' ' && *Cursor_ != '\r')
            {
                char C = *Cursor_++;
                if (C > '@' && C < '[')
                    C = (char)(C + ' ');
                *Pos++ = C;
                if (Pos == Value + 0xFE)
                    return "Flag too long";
            }
        }
    }
    *Pos = '\0';
    Target->Insert(Name, Value);
    if (_stricmp("OverallGraphicDetail", Name) == 0)
        DetailSets_[std::string(Value)] = Target;
    return nullptr;
}

unsigned long FMgsdbDatabase::ReadGuidWord()
{
    const int A = ReadHexDigit();
    if (A == -1)
        return 0xFFFFFFFF;
    const int B = ReadHexDigit();
    if (B == -1)
        return 0xFFFFFFFF;
    const int C = ReadHexDigit();
    if (C == -1)
        return 0xFFFFFFFF;
    const unsigned long D = (unsigned long)ReadHexDigit();
    if (D == 0xFFFFFFFF)
        return 0xFFFFFFFF;
    return ((unsigned long)A << 12) | ((unsigned long)B << 8) |
           ((unsigned long)C << 4) | D;
}

unsigned long FMgsdbDatabase::ReadGuidWordSwapped()
{
    const unsigned long Word = ReadGuidWord();
    if (Word == 0xFFFFFFFF)
        return 0xFFFFFFFF;
    return ((Word & 0xFF00) >> 8) + (Word & 0xFF) * 0x100;
}

// Does not move the cursor.
bool FMgsdbDatabase::Match(const char* Keyword) const
{
    const size_t Length = strlen(Keyword);
    return _strnicmp(Cursor_, Keyword, Length) == 0 && MgsdbIsSeparator(Cursor_[Length]);
}

// "<key> <op> <value>": kMgsdbTrue / null for true / false, otherwise the error message.
// Operators: == = -> 0, != <> -> 1, > -> 2, < -> 3, >= => -> 4, <= =< -> 5, & -> 6.
const char* FMgsdbDatabase::EvaluateIf()
{
    unsigned long Current = 0;
    int Kind = 0; // 0 number, 1 guid, 2 driver, 3 os
    SkipWhite();
    if (Match("cpuspeed"))
    {
        Current = CpuSpeed_;
        Cursor_ += strlen("cpuspeed");
    }
    else if (Match("ram"))
    {
        Current = Ram_;
        Cursor_ += strlen("ram");
    }
    else
    {
        bool bFound = false;
        for (size_t Index = 0; Index < sizeof(kMgsdbCapsKeys) / sizeof(kMgsdbCapsKeys[0]); ++Index)
        {
            if (Match(kMgsdbCapsKeys[Index].Name))
            {
                Current = *(const unsigned long*)((const unsigned char*)&Caps_ + kMgsdbCapsKeys[Index].CapsOffset);
                Cursor_ += strlen(kMgsdbCapsKeys[Index].Name);
                bFound = true;
                break;
            }
        }
        if (!bFound)
        {
            if (Match("videoram"))
            {
                Current = VideoRam_;
                Cursor_ += 8;
            }
            else if (Match("subsysid"))
            {
                Current = Ident_.SubSysId;
                Cursor_ += 8;
            }
            else if (Match("revision"))
            {
                Current = Ident_.Revision;
                Cursor_ += 8;
            }
            else if (Match("guid"))
            {
                Kind = 1;
                Cursor_ += 4;
            }
            else if (Match("driver"))
            {
                Kind = 2;
                Cursor_ += 6;
            }
            else if (Match("os"))
            {
                OSVERSIONINFOA Version;
                Version.dwOSVersionInfoSize = 0x94;
#pragma warning(push)
#pragma warning(disable : 4996)
                GetVersionExA(&Version);
#pragma warning(pop)
                if (Version.dwPlatformId == 2)
                {
                    Current = 5;
                    if (Version.dwMajorVersion == 5 && Version.dwBuildNumber < 0xA28)
                        Current = 4;
                }
                else
                {
                    Current = 3;
                    if ((Version.dwBuildNumber & 0xFFFF) < 0x8AF)
                        Current = 2;
                    if ((Version.dwBuildNumber & 0xFFFF) < 1999)
                        Current = 1;
                    if ((Version.dwBuildNumber & 0xFFFF) < 0x3B7)
                        Current = 0;
                }
                Kind = 3;
                Cursor_ += 2;
            }
            else
            {
                return "Unknown value";
            }
        }
    }

    SkipWhite();
    unsigned long Op = 0;
    if (Cursor_[0] == '=' && Cursor_[1] == '=')
    {
        Cursor_ += 2;
        Op = 0;
    }
    else if (Cursor_[0] == '!' && Cursor_[1] == '=')
    {
        Cursor_ += 2;
        Op = 1;
    }
    else if (Cursor_[0] == '<' && Cursor_[1] == '>')
    {
        Cursor_ += 2;
        Op = 1;
    }
    else if (Cursor_[0] == '=' && Cursor_[1] == '>')
    {
        Cursor_ += 2;
        Op = 4;
    }
    else if (Cursor_[0] == '=' && Cursor_[1] == '<')
    {
        Cursor_ += 2;
        Op = 5;
    }
    else if (Cursor_[0] == '<' && Cursor_[1] == '=')
    {
        Cursor_ += 2;
        Op = 5;
    }
    else if (Cursor_[0] == '>' && Cursor_[1] == '=')
    {
        Cursor_ += 2;
        Op = 4;
    }
    else if (*Cursor_ == '=')
    {
        ++Cursor_;
        Op = 0;
    }
    else if (*Cursor_ == '>')
    {
        ++Cursor_;
        Op = 2;
    }
    else if (*Cursor_ == '<')
    {
        ++Cursor_;
        Op = 3;
    }
    else
    {
        if (*Cursor_ != '&')
            return "Unknown operator";
        ++Cursor_;
        Op = 6;
    }
    SkipWhite();

    if (Kind == 1)
    {
        if (Op >= 2)
            return "Only == or != allowed";
        unsigned long Guid[4];
        const unsigned long W1 = ReadGuidWord();
        if (W1 == 0xFFFFFFFF)
            return "Invalid GUID";
        unsigned long W2 = ReadGuidWord();
        if (W2 == 0xFFFFFFFF)
            return "Invalid GUID";
        Guid[0] = W1 * 0x10000 + W2;
        if (*Cursor_++ != '-')
            return "Invalid GUID";
        const unsigned long W3 = ReadGuidWord();
        if (W3 == 0xFFFFFFFF)
            return "Invalid GUID";
        if (*Cursor_++ != '-')
            return "Invalid GUID";
        const unsigned long W4 = ReadGuidWord();
        if (W4 == 0xFFFFFFFF)
            return "Invalid GUID";
        Guid[1] = W4 * 0x10000 + W3;
        if (*Cursor_++ != '-')
            return "Invalid GUID";
        const unsigned long B1 = ReadGuidWordSwapped();
        if (B1 == 0xFFFFFFFF)
            return "Invalid GUID";
        if (*Cursor_++ != '-')
            return "Invalid GUID";
        const unsigned long B2 = ReadGuidWordSwapped();
        if (B2 == 0xFFFFFFFF)
            return "Invalid GUID";
        Guid[2] = B2 * 0x10000 + B1;
        const unsigned long B3 = ReadGuidWordSwapped();
        if (B3 == 0xFFFFFFFF)
            return "Invalid GUID";
        const unsigned long B4 = ReadGuidWordSwapped();
        if (B4 == 0xFFFFFFFF)
            return "Invalid GUID";
        Guid[3] = B4 * 0x10000 + B3;
        const bool bEqual = memcmp(Guid, &Ident_.DeviceIdentifier, 0x10) == 0;
        if (Op == 0)
            return bEqual ? kMgsdbTrue : nullptr;
        return bEqual ? nullptr : kMgsdbTrue;
    }

    if (Kind == 2)
    {
        // A.B.C.D against DriverVersion: HighPart = A<<16 + B (signed), LowPart = C<<16 + D.
        // As in retail, the second and fourth checks test A and C again, not B and D.
        const unsigned long A = ReadNumber();
        if (A == 0xFFFFFFFF || *Cursor_++ != '.')
            return "Invalid driver number";
        const unsigned long B = ReadNumber();
        const long RhsHi = (long)(A * 0x10000 + B);
        if (A == 0xFFFFFFFF || *Cursor_++ != '.')
            return "Invalid driver number";
        const unsigned long C = ReadNumber();
        if (C == 0xFFFFFFFF || *Cursor_++ != '.')
            return "Invalid driver number";
        const unsigned long D = ReadNumber();
        const unsigned long RhsLo = C * 0x10000 + D;
        if (C == 0xFFFFFFFF)
            return "Invalid driver number";
        const unsigned long Lo = Ident_.DriverVersion.LowPart;
        const long Hi = Ident_.DriverVersion.HighPart;
        switch (Op)
        {
        case 0:
            return (Lo == RhsLo && Hi == RhsHi) ? kMgsdbTrue : nullptr;
        case 1:
            return (Lo == RhsLo && Hi == RhsHi) ? nullptr : kMgsdbTrue;
        case 2:
            return (Hi < RhsHi || (Hi <= RhsHi && Lo <= RhsLo)) ? nullptr : kMgsdbTrue;
        case 3:
            return (RhsHi < Hi || (RhsHi <= Hi && RhsLo <= Lo)) ? nullptr : kMgsdbTrue;
        case 4:
            return (Hi < RhsHi || (Hi <= RhsHi && Lo < RhsLo)) ? nullptr : kMgsdbTrue;
        case 5:
            return (RhsHi < Hi || (RhsHi <= Hi && RhsLo < Lo)) ? nullptr : kMgsdbTrue;
        default:
            return "Invalid";
        }
    }

    unsigned long Rhs = 0;
    if (Kind == 3)
    {
        if (Match("win95"))
        {
            Rhs = 0;
            Cursor_ += 5;
        }
        else if (Match("win98"))
        {
            Rhs = 1;
            Cursor_ += 5;
        }
        else if (Match("win98se"))
        {
            Rhs = 2;
            Cursor_ += 7;
        }
        else if (Match("winme"))
        {
            Rhs = 3;
            Cursor_ += 5;
        }
        else if (Match("win2k"))
        {
            Rhs = 4;
            Cursor_ += 5;
        }
        else if (Match("winxp"))
        {
            Rhs = 5;
            Cursor_ += 5;
        }
        else
        {
            return "Unknown OS";
        }
    }
    else
    {
        Rhs = ReadNumber();
        if (Rhs == 0xFFFFFFFF)
            return "Number expected";
    }
    switch (Op)
    {
    case 0:
        return Current == Rhs ? kMgsdbTrue : nullptr;
    case 1:
        return Current == Rhs ? nullptr : kMgsdbTrue;
    case 2:
        return Rhs < Current ? kMgsdbTrue : nullptr;
    case 3:
        return Current < Rhs ? kMgsdbTrue : nullptr;
    case 4:
        return Current < Rhs ? nullptr : kMgsdbTrue;
    case 5:
        return Rhs < Current ? nullptr : kMgsdbTrue;
    case 6:
        return (Current & Rhs) == 0 ? nullptr : kMgsdbTrue;
    default:
        return "Invalid";
    }
}

// ---- passes ----

// Pass 1: "propertyset = "name"" definitions, each body read into its own record set, until
// "vendor" or "applytoall".
bool FMgsdbDatabase::ReadPropertySets()
{
    for (;;)
    {
        if (Match("propertyset"))
        {
            Cursor_ += 11;
            SkipWhite();
            if (*Cursor_ != '=')
            {
                SetError("Missing =");
                return false;
            }
            ++Cursor_;
            const char* Quoted = ReadQuoted();
            if (Quoted == nullptr)
                return false;
            const std::string Name(Quoted);
            SkipLine();
            FMgsdbRecordSet* Set = new (std::nothrow) FMgsdbRecordSet(this);
            if (!ReadBlock(Set))
            {
                delete Set;
                return false;
            }
            // A second definition of the same name replaces the pointer; the old set is not
            // freed (retail stores the node value over it).
            PropertySets_[Name] = Set;
        }
        else if (Match("vendor") || Match("applytoall"))
        {
            return true;
        }
        SkipLine();
        if (End_ <= Cursor_)
            return true;
    }
}

// Pass 2: every "applytoall" block into the live set until "vendor". The line the block
// stopped on is skipped as well.
bool FMgsdbDatabase::ReadLeadingApplyToAll()
{
    do
    {
        if (Match("applytoall"))
        {
            SkipLine();
            if (!ReadBlock(Records_))
                return false;
        }
        else if (Match("vendor"))
        {
            break;
        }
        SkipLine();
    } while (Cursor_ < End_);
    return true;
}

// Pass 3: the first "vendor = <id> "name"" (or "vendor = unknown") whose id is the adapter's,
// its vendor-wide lines, then the first "<id> = "name"" (or "unknown = ...") device line whose
// id is the adapter's and that device's block, all into the live set.
bool FMgsdbDatabase::ReadVendorDevice()
{
    unsigned long Vendor = 0xFFFFFFFF;
    do
    {
        if (Match("vendor"))
        {
            Cursor_ += 6;
            SkipWhite();
            if (*Cursor_ == '=')
            {
                ++Cursor_;
                if (Match("unknown"))
                {
                    // The cursor stays on "unknown" (06d26b86..06d26b92).
                    Vendor = Ident_.VendorId;
                    break;
                }
                const unsigned long Id = ReadNumber();
                if (Id == Ident_.VendorId)
                {
                    Vendor = Id;
                    break;
                }
            }
        }
        else if (Match("applytoall"))
        {
            break;
        }
        SkipLine();
    } while (Cursor_ < End_);
    if (Vendor == 0xFFFFFFFF)
        return true;

    const char* Quoted = ReadQuoted();
    if (Quoted == nullptr)
        return false;
    VendorName_ = Quoted;

    do
    {
        SkipLine();
        if (!Match("vendor"))
        {
            SkipWhite();
            if (*Cursor_ > '/' && *Cursor_ < ':')
                break;
            if (*Cursor_ != '\r' && !(Cursor_[0] == '/' && Cursor_[1] == '/') &&
                !ReadBlock(Records_))
            {
                return false;
            }
        }
    } while (Cursor_ < End_);

    unsigned long Device = 0xFFFFFFFF;
    do
    {
        if (Match("vendor"))
            break;
        if (Match("unknown"))
        {
            Device = Ident_.DeviceId;
            Cursor_ += strlen("unknown");
            SkipWhite();
            break;
        }
        SkipWhite();
        if (*Cursor_ > '/' && *Cursor_ < ':')
        {
            const unsigned long Id = ReadNumber();
            if (Id == Ident_.DeviceId)
            {
                Device = Id;
                break;
            }
        }
        SkipLine();
    } while (Cursor_ < End_);
    if (Device == 0xFFFFFFFF)
        return true;

    if (*Cursor_ != '=')
    {
        SetError("xxx = Device Name expected");
        return false;
    }
    ++Cursor_;
    Quoted = ReadQuoted();
    if (Quoted == nullptr)
        return false;
    DeviceName_ = Quoted;
    SkipLine();
    return ReadBlock(Records_);
}

// Pass 4: every remaining "applytoall" block into the live set.
bool FMgsdbDatabase::ReadTrailingApplyToAll()
{
    do
    {
        if (Match("applytoall"))
        {
            SkipLine();
            if (!ReadBlock(Records_))
                return false;
        }
        SkipLine();
    } while (Cursor_ < End_);
    return true;
}

// Reads flags and property sets while tracking nested IF states.
bool FMgsdbDatabase::ReadBlock(FMgsdbRecordSet* Target)
{
    int Stack[0x100];
    int Depth = 0;
    int State = 0;
    bool bFirst = true;
    do
    {
        if (!bFirst)
            SkipLine();
        else
            bFirst = false;
        if (Match("vendor"))
            return true;
        SkipWhite();
        if (*Cursor_ == '\r' || (Cursor_[0] == '/' && Cursor_[1] == '/') ||
            (*Cursor_ >= '0' && *Cursor_ <= '9') || Match("unknown"))
        {
            continue;
        }
        if (Match("break"))
            break;
        if (Match("MaxOverallGraphicDetail"))
        {
            Cursor_ += strlen("MaxOverallGraphicDetail");
            SkipWhite();
            if (*Cursor_ != '=')
            {
                SetError("Expecting '=', didn't get it");
                return false;
            }
            ++Cursor_;
            SkipWhite();
            const unsigned long Max = ReadNumber();
            if (Max == 0xFFFFFFFF)
            {
                SetError("MaxOverallGraphicDetail did not specify a number!");
                return false;
            }
            char Detail[0x10];
            memset(Detail, 0, sizeof(Detail));
            Detail[0] = '0';
            char MaxText[0x10];
            sprintf(MaxText, "%d", (int)Max);
            if (Records_->Find("OverallGraphicDetail", Detail, 0x10) &&
                (unsigned long)MsvcrtAtoi(Detail) > Max)
            {
                std::map<std::string, FMgsdbRecordSet*>::iterator It =
                    DetailSets_.find(std::string(MaxText));
                if (It == DetailSets_.end())
                {
                    SetError("Unrecognized graphic detail");
                    return false;
                }
                MgsdbReplay(Records_, It->second);
            }
            continue;
        }
        if (Match("if"))
        {
            Cursor_ += 2;
            const char* Result = EvaluateIf();
            Stack[Depth] = State;
            ++Depth;
            if (Depth == 0x10)
            {
                SetError("IF's nested too deep");
                return false;
            }
            if (Result == kMgsdbTrue)
            {
                if (State != 2)
                    State = 1;
            }
            else if (Result == nullptr)
            {
                State = 2;
            }
            else
            {
                SetError(Result);
                return false;
            }
            continue;
        }
        if (Match("endif"))
        {
            if (Depth == 0)
            {
                SetError("Unexpected ENDIF");
                return false;
            }
            --Depth;
            State = Stack[Depth];
            continue;
        }
        if (State != 0 && State != 1)
            continue;
        if (Match("propertyset"))
        {
            Cursor_ += 11;
            SkipWhite();
            if (*Cursor_ != '=')
            {
                SetError("Missing =");
                return false;
            }
            ++Cursor_;
            const char* Quoted = ReadQuoted();
            if (Quoted == nullptr)
                return false;
            std::map<std::string, FMgsdbRecordSet*>::iterator It =
                PropertySets_.find(std::string(Quoted));
            if (It == PropertySets_.end())
            {
                SetError("Unrecognized property set");
                return false;
            }
            MgsdbReplay(Target, It->second);
            continue;
        }
        const char* Message = ReadFlag(Target);
        if (Message != nullptr)
        {
            SetError(Message);
            return false;
        }
    } while (Cursor_ < End_);
    if (State == 0 && Depth == 0)
        return true;
    SetError("Bad IF/ENDIF");
    return false;
}
