// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_ini.h"
#include "msvcrt_atof.h"

#include "DALib.h"
#include "dacom.h"
#include "msvcrt_compat.h" // MsvcrtAtoi: retail's MSVCRT.DLL atoi wraps, the UCRT saturates

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>

// common_dacom_stricmp.cpp: DACOM.DLL!stricmp, the import retail's StringCache tree and every
// INI_Reader name compare (is_header / is_value / get_value_bool) call.
int CallDacomStricmp(const char* Left, const char* Right);

static_assert(sizeof(INI_Reader::State) == 0xC08, "text INI_Reader::State is cursor+line+3x0x400");
static_assert(offsetof(INI_Reader::State, Text) == 4 && offsetof(INI_Reader::State, Bini) == 4,
              "both State views start after the cursor");
static_assert(offsetof(INI_Reader::State::FBini, ValueIndex) == 4 &&
                  offsetof(INI_Reader::State::FBini, ValueOffset) == 8 &&
                  offsetof(INI_Reader::State::FBini, ParamsOffset) == 0xc,
              "BINI get_state writes +4 dword, +8 word, +0xc dword, +0x10 dword");
static_assert(offsetof(FBiniParam, Int) == 1, "BINI param payload follows the type byte");
static_assert(sizeof(LineParser32) == 0x408, "LineParser32 is Buffer[0x400] + Length + Position");
static_assert(sizeof(Vector) == 12, "Vector is 3 floats");
static_assert(sizeof(BINI_Header) == 0xC, "BINI_Header is 0xC");
static_assert(sizeof(BINI_Block) == 4, "BINI_Block is 4");
static_assert(sizeof(BINI_Value) == 3, "BINI_Value is 3");
static_assert(sizeof(FBiniParam) == 5, "BINI param is type + 4-byte payload");

namespace
{
    const char IniSource[] = "E:\\FL\\Scratch\\Source\\Common\\Ini.cpp";
    const char ParseSource[] = "E:\\FL\\Scratch\\Source\\Common\\Parse.cpp";
    const unsigned int BiniMagic = 0x494E4942; // 'BINI'
    const unsigned int BiniHeaderBytes = 0xC;
    const unsigned int BiniParamBytes = sizeof(FBiniParam); // 5
    const char EmptyC[] = ""; // .rdata:063997b0

    char GStringIndexScratch[0x400];

    // DAT_063fd198: the text-mode get_line_ptr buffer. Retail's is 0x100 bytes and is filled
    // by an unbounded sprintf("%s = %s") -- a line past 0xff chars overruns it (undefined
    // behaviour). Name and parameters are each < 0x400 chars, so this size holds any line.
    char GTextLineScratch[0x808];

    // DAT_0658e800: the BINI get_line_ptr buffer, 0x100 bytes in retail and likewise
    // overrun by long values (undefined behaviour); the port truncates past this size.
    char GBiniLineScratch[0x2000];

    // DAT_0658e900 (0x100 bytes): the "%d" / "%g" text of a numeric BINI parameter.
    char GBiniNumScratch[0x100];

    // DAT_063fcf98 (0x100 wchar_t): the BINI get_value_wstring result, written by
    // mbstowcs(..., 0x100). The extra element is never written and keeps the result
    // terminated when mbstowcs fills all 0x100 (which in retail needs a join that has
    // already overrun its 0x100-byte buffer).
    wchar_t GBiniWideScratch[0x101];

    ILoadingScreen* GLoadingScreen = nullptr;

    // .data:063ed4a8 (128 bytes, character classes) immediately followed by .data:063ed528
    // (128 bytes, digit values), copied from the PE. Class bits: 0x01 letter, 0x02 upper,
    // 0x04 lower, 0x08 decimal digit, 0x10 hex digit, 0x20 identifier, 0x40 white space
    // (\t \n \r ' '), 0x80 operator. Retail indexes the class table with (c & 0x7f), and the
    // digit table with the SIGNED char (MOVSX, 0631bd9e) - a byte >= 0x80 reads the class
    // table that precedes it; GParseTables keeps both in one array so that read is the same.
    unsigned char const GParseTables[256] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x40, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x40, 0x80, 0x00, 0x20, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x80, 0x00, 0x80, 0x00, 0x80,
        0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x00, 0x00, 0x80, 0x80, 0x80, 0x00,
        0x00, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23,
        0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x00, 0x00, 0x00, 0x80, 0x20,
        0x00, 0x35, 0x35, 0x35, 0x35, 0x35, 0x35, 0x25, 0x25, 0x25, 0x25, 0x25, 0x25, 0x25, 0x25, 0x25,
        0x25, 0x25, 0x25, 0x25, 0x25, 0x25, 0x25, 0x25, 0x25, 0x25, 0x25, 0x00, 0x80, 0x00, 0x80, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    unsigned char const ParseClassSpace = 0x40;
    unsigned char const ParseClassDigit = 0x08;
    unsigned char const ParseClassHex = 0x10;

    // TEST byte [ (c & 0x7f) + DAT_063ed4a8 ], Bits
    bool HasParseClass(char Ch, unsigned char Bits)
    {
        return (GParseTables[Ch & 0x7f] & Bits) != 0;
    }

    // MOVSX EDX,AL; MOVZX EAX,byte [EDX + DAT_063ed528]
    int ParseDigitValue(char Ch)
    {
        return GParseTables[0x80 + (signed char)Ch];
    }

    // ParseNumber's base-N accumulation (0631bd7d-0631be15): Frac starts at 0.0; '.' sets
    // Frac = Base; a digit value >= Base (signed JGE) prints the warning and stops; while
    // Frac is not above 0 (FCOM, TEST AH,0x41, JNZ) Value = Value * Base + digit, else
    // Value += digit / Frac and Frac *= Base. The sign is applied by the caller. Retail keeps
    // Value on the x87 stack for the whole loop (80-bit); the port accumulates in double.
    double ParseBaseN(char const* Begin, char const* End, int Base, char const* Original)
    {
        double Value = 0.0;
        double Frac = 0.0;
        for (char const* Cursor = Begin; Cursor < End; Cursor += 1)
        {
            if (*Cursor == '.')
            {
                Frac = Base;
                continue;
            }
            int const Digit = ParseDigitValue(*Cursor);
            if (Digit >= Base)
            {
                FDUMP(0x100002,
                      "%s(%d) : *** WARNING: ParseNumber: illegal BASE-N digit(s) in argument! '%s'",
                      ParseSource, 0x14e, Original);
                break;
            }
            if (!(Frac > 0.0))
            {
                Value = Value * Base + Digit;
            }
            else
            {
                Value = Value + Digit / Frac;
                Frac = Frac * Base;
            }
        }
        return Value;
    }

    // MSVCRT tolower in the "C" locale, on the sign-extended char retail passes (MOVSX).
    int ParseToLower(char Ch)
    {
        int const Value = (signed char)Ch;
        if (Value >= 'A' && Value <= 'Z')
            return Value + ('a' - 'A');
        return Value;
    }

    // strcat into a fixed buffer, truncating at its end. Stands in for retail's unbounded
    // strcat into the 0x100-byte join / line buffers (see the buffer notes above).
    void AppendBounded(char* Dest, unsigned int DestChars, char const* Src)
    {
        size_t const Used = strlen(Dest);
        size_t Add = strlen(Src);
        if (Used + Add >= DestChars)
            Add = DestChars - 1u - Used;
        memcpy(Dest + Used, Src, Add);
        Dest[Used + Add] = '\0';
    }
}

char const* __cdecl StringIndex(char const* Text, unsigned int Index)
{
    // Common.dll.txt 0631be30-0631bedd. Per field: skip white space (class bit 0x40 of
    // c & 0x7f: \t \n \r ' '), find the next ','. While Index != 0: no comma -> "" (only
    // byte 0 of DAT_06401350 is written), else continue after it with Index - 1. At the
    // wanted field the end is the comma or the terminator, trailing white space is trimmed
    // (end > start, unsigned), the bytes are copied into DAT_06401350 and terminated. No
    // null check of Text. Retail's copy is unbounded (a field of 0x400+ bytes overflows the
    // buffer - undefined behaviour); the port copies at most 0x3ff bytes.
    char const* Cursor = Text;
    unsigned int Remain = Index;
    for (;;)
    {
        while (HasParseClass(*Cursor, ParseClassSpace))
            Cursor += 1;

        char const* const Comma = strchr(Cursor, ',');
        if (Remain != 0)
        {
            if (Comma == nullptr)
            {
                GStringIndexScratch[0] = '\0';
                return GStringIndexScratch;
            }
            Cursor = Comma + 1;
            Remain -= 1;
            continue;
        }

        char const* End = Comma;
        if (End == nullptr)
            End = Cursor + strlen(Cursor);
        while (End > Cursor && HasParseClass(End[-1], ParseClassSpace))
            End -= 1;
        size_t Copy = End - Cursor;
        if (Copy > sizeof(GStringIndexScratch) - 1u)
            Copy = sizeof(GStringIndexScratch) - 1u;
        memcpy(GStringIndexScratch, Cursor, Copy);
        GStringIndexScratch[Copy] = '\0';
        return GStringIndexScratch;
    }
}

double __cdecl ParseNumber(char const* Text)
{
    // Common.dll.txt 0631bb70-0631be2a, branch by branch:
    //   '+' skipped, then '-' sets Sign = -1 and is skipped. No null check of Text.
    //   A leading '0': the whole zero run is skipped (Start lands on the first non-'0');
    //   tolower of that char == 'x' -> skip it, Base = 16.
    //   Base 16: the span is hex digits, an optional '.', hex digits -> base-N.
    //   Otherwise: '\'' -> (double)(signed char)Start[1], returned WITHOUT the sign;
    //   '\0' -> atof(Start); else the hex-digit run (class 0x10): run > 1 ending in 'd' or
    //   'b' (tolower) drops that char with Base 10 / 2; run == 1 followed by a lowercase 'h'
    //   gives Base 16. Then at the end of the span: '\0' -> Base 10 atof(Start) else base-N;
    //   anything else: an optional '.' plus decimal digits extends the span, then Base 10 ->
    //   atof(Start) (the 'e' exponent scan that follows in retail only advances a pointer
    //   atof never sees), else base-N. The sign is applied at the end (FCHS).
    int Sign = 1;
    int Base = 10;
    char const* Start = Text;
    if (*Start == '+')
        Start += 1;
    if (*Start == '-')
    {
        Sign = -1;
        Start += 1;
    }

    if (*Start == '0')
    {
        char Next;
        do
        {
            Next = Start[1];
            Start += 1;
        } while (Next == '0');
        if (ParseToLower(Next) == 'x')
        {
            Start += 1;
            Base = 16;
        }
    }

    double Value = 0.0;
    char const* End = Start;
    if (Base == 16)
    {
        while (HasParseClass(*End, ParseClassHex))
            End += 1;
        if (*End == '.')
            End += 1;
        while (HasParseClass(*End, ParseClassHex))
            End += 1;
        Value = ParseBaseN(Start, End, Base, Text);
    }
    else if (*Start == '\'')
    {
        return (signed char)Start[1];
    }
    else if (*Start == '\0')
    {
        Value = MsvcrtAtof(Start);
    }
    else
    {
        while (HasParseClass(*End, ParseClassHex))
            End += 1;
        ptrdiff_t const Run = End - Start;
        if (Run > 0)
        {
            int const Last = ParseToLower(End[-1]);
            if (Run > 1)
            {
                if (Last == 'd')
                {
                    End -= 1;
                    Base = 10;
                }
                else if (Last == 'b')
                {
                    End -= 1;
                    Base = 2;
                }
            }
            else if (*End == 'h')
            {
                Base = 16;
            }
        }

        if (*End == '\0')
        {
            if (Base == 10)
                Value = MsvcrtAtof(Start);
            else
                Value = ParseBaseN(Start, End, Base, Text);
        }
        else
        {
            if (*End == '.')
            {
                End += 1;
                while (HasParseClass(*End, ParseClassDigit))
                    End += 1;
            }
            if (Base == 10)
                Value = MsvcrtAtof(Start);
            else
                Value = ParseBaseN(Start, End, Base, Text);
        }
    }

    if (Sign < 0)
        Value = -Value;
    return Value;
}

double __cdecl ValueNum(char const* Text, unsigned int Index)
{
    // Common.dll.txt:0631bee0. Two instructions of substance: StringIndex then the CRT atof.
    // Deliberately NOT ParseNumber -- retail calls [MSVCRT.DLL::atof] directly here, so "0x10"
    // reads back as 0 and the base-N suffixes ParseNumber understands are not honoured.
    return MsvcrtAtof(StringIndex(Text, Index));
}

void __cdecl RegisterLoadingScreen(ILoadingScreen* Screen)
{
    // Common.dll.c:148708 / .txt:06311420 — DAT_063fd2a4 = param.
    GLoadingScreen = Screen;
}

void __cdecl RefreshLoadingProgress()
{
    // Common.dll.c:148720 / .txt:06311430 — thiscall vtable slot 0; JMP if non-null.
    if (GLoadingScreen == nullptr)
        return;
    GLoadingScreen->Refresh();
}


// ---------------------------------------------------------------------------------------------
// INI_Reader's BINI half (+0x1428)
// ---------------------------------------------------------------------------------------------

INI_Reader::FBiniFile::FBiniFile()
    : FileHeader(nullptr)
    , StringBase(nullptr)
    , Block(nullptr)
    , Value(nullptr)
{
    // INI_Reader ctor .text:0630f690..0630f6d2: the FileMap part (+0x1428..+0x1448), the
    // header (+0x144c), string base (+0x1450), block (+0x1454) and value (+0x145c) pointers.
    // The value index (+0x1458), the params pointer (+0x1460) and the join buffer are not
    // written.
}

bool INI_Reader::FBiniFile::Init()
{
    // FUN_0630fbd0: seek(0) inlined (0630fbd9), FileMap::read_ptr(BINI_Header*&) inlined
    // (0630fbe9..0630fc19: take min(Size - Cursor, 0xc) bytes, advance, accept only a full
    // 0xc read with the 'BINI' magic and StringTableOffset < Size), failure -> seek(0) when
    // a view exists. The header pointer (+0x24) is stored either way; on success block
    // (+0x2c) and value (+0x34) are cleared and the string base (+0x28) = header +
    // StringTableOffset. The value index (+0x30), the params pointer (+0x38) and the BINI
    // flag (+0x1564, set by open) are not touched here.
    BINI_Header const* Candidate = nullptr;
    if (View != nullptr)
    {
        Cursor = 0;
        unsigned int const Start = Cursor;
        if (Start < Size)
        {
            unsigned int Take = Size - Start;
            if (Take > BiniHeaderBytes)
                Take = BiniHeaderBytes;
            BINI_Header const* const Read = (BINI_Header const*)(View + Start);
            Cursor = Start + Take;
            if (Take == BiniHeaderBytes && Read->Magic == BiniMagic && Read->StringTableOffset < Size)
                Candidate = Read;
        }
        if (Candidate == nullptr)
            Cursor = 0;
    }

    FileHeader = Candidate;
    if (Candidate == nullptr)
        return false;
    StringBase = (char const*)Candidate + Candidate->StringTableOffset;
    Block = nullptr;
    Value = nullptr;
    return true;
}

char const* INI_Reader::FBiniFile::String(unsigned int Offset) const
{
    // Every BINI name/string read is `string base (+0x28) + offset` with no check.
    return StringBase + Offset;
}

FBiniParam const* INI_Reader::FBiniFile::ParamAt(unsigned int Index) const
{
    // `LEA reg,[Index + Index*4]; ADD reg,[+0x38]`: no null check of the params pointer,
    // which read_value leaves null when the file ends inside a value (a read through it
    // faults, as in retail).
    return &Params[Index];
}

void INI_Reader::FBiniFile::ReportMissingParam(unsigned int Index) const
{
    // The out-of-range branch every BINI getter inlines (0630ffde, 063106b5, 06310867,
    // 0631099d, 06310ae9, 06310c3c, 06310dec): sprintf into a 0x100 stack buffer with the
    // block name (string base + word [Block]) and the value name (string base + word
    // [Value]), then MessageBoxA(0, Buffer, "ERROR", 0). Block is read without a check.
    // Retail's sprintf is unbounded -- names longer than the buffer overrun the stack
    // (undefined behaviour); the port truncates instead.
    char Buffer[0x100];
    _snprintf(Buffer, sizeof(Buffer), "header [%s] value %s missing parameter %u",
              String(Block->NameOffset), String(Value->NameOffset), Index);
    Buffer[sizeof(Buffer) - 1u] = '\0';
    MessageBoxA(nullptr, Buffer, "ERROR", 0);
}

char const* INI_Reader::FBiniFile::FormatParam(unsigned int Index)
{
    // FUN_0630ff40 (also the inlined body of get_value_string(unsigned) 06310a30): a null
    // value (+0x34) returns "" without a report; Index >= ParamCount (unsigned JNC) reports
    // and returns "". Type 0 (zero-extended byte) and 1 (int) go through "%d", type 2 (float
    // widened to double) through "%g", into DAT_0658e900; type 3 is a string-table offset;
    // any other type (JA caseD_4) returns DAT_0658e900 as it stands.
    if (Value == nullptr)
        return EmptyC;
    if (Index >= Value->ParamCount)
    {
        ReportMissingParam(Index);
        return EmptyC;
    }

    FBiniParam const* const Param = ParamAt(Index);
    switch (Param->Type)
    {
    case BiniParamByte:
        sprintf(GBiniNumScratch, "%d", Param->Byte);
        return GBiniNumScratch;
    case BiniParamInt:
        sprintf(GBiniNumScratch, "%d", Param->Int);
        return GBiniNumScratch;
    case BiniParamFloat:
        sprintf(GBiniNumScratch, "%g", (double)Param->Float);
        return GBiniNumScratch;
    case BiniParamString:
        return String(Param->StringOffset);
    default:
        return GBiniNumScratch;
    }
}

char const* INI_Reader::FBiniFile::JoinParams()
{
    // The loop get_line_ptr (0630fe52..0630fed1), get_value_string() (06310b90..06310ce6) and
    // get_value_wstring (06310d40..06310e96) share: the join buffer (+0x3c) is emptied; with
    // a value, for Index = 0 .. ParamCount-1 (signed JL, the count re-read every pass) the
    // formatted parameter is taken first, then ", " is appended when Index > 0, then the
    // parameter. Retail appends with unbounded strcat (see JoinChars).
    Joined[0] = '\0';
    if (Value == nullptr)
        return Joined;
    for (int Index = 0; Index < Value->ParamCount; Index += 1)
    {
        char const* const Piece = FormatParam(Index);
        if (Index > 0)
            AppendBounded(Joined, JoinChars, ", ");
        AppendBounded(Joined, JoinChars, Piece);
    }
    return Joined;
}

// ---------------------------------------------------------------------------------------------
// INI_Reader
// ---------------------------------------------------------------------------------------------

INI_Reader::INI_Reader()
{
    // .text:0630f660: Text's FileMap part, Text.LineNum (+0x24) = 0, Header[0] (+0x28) = 0,
    // the BINI half (see FBiniFile's ctor), bBini (+0x1564) = 0. ValueName, ValueParams and
    // ValueWide are not written.
    Text.LineNum = 0;
    Header[0] = '\0';
    bBini = false;
}

// .text:062634f0 / 06263650: every member copied / assigned in declaration order (both
// FileMaps through their own copy / assignment, the buffers byte for byte, bBini last).
INI_Reader::INI_Reader(INI_Reader const& Other) = default;
INI_Reader& INI_Reader::operator=(INI_Reader const& Other) = default;

INI_Reader::~INI_Reader()
{
    // .text:06263860: the BINI FileMap is destroyed (close inlined, string freed), then the
    // text one -- the member destructors, in reverse declaration order.
}

bool INI_Reader::open(char const* Path, bool bFatal)
{
    // .text:0630f9b0. A local FileMap opens the file (FileMap::open inlined: close, name =
    // Path with no null check, CreateFileA / CreateFileMappingA / GetFileSize / MapViewOfFile,
    // Cursor = 0). Failure: FatalErrorFn(200, Path) when bFatal (called through the DALib
    // pointer without a check), return false. Neither of the reader's own maps is closed
    // first: whatever they still hold is overwritten.
    RefreshLoadingProgress();

    FileMap Temp;
    if (!Temp.open(Path))
    {
        if (bFatal)
            FatalErrorFn(200, Path);
        return false;
    }

    // 0630fa9a..0630fac4: seek(0), read_ptr(BINI_Header*&); a full header with the 'BINI'
    // magic and StringTableOffset < Size selects the BINI half.
    Temp.seek(0);
    BINI_Header* FileHeader = nullptr;
    if (Temp.read_ptr(FileHeader) && FileHeader->Magic == BiniMagic &&
        FileHeader->StringTableOffset < Temp.Size)
    {
        // 0630fb37: FileMap::operator= into the BINI half, the local forgets its view and
        // handles, FUN_0630fbd0. Success sets bBini; failure falls through to the text path
        // below with the local's handles already given away (so the text map gets no view).
        Bini.FileMap::operator=(Temp);
        Temp.View = nullptr;
        Temp.MappingHandle = nullptr;
        Temp.FileHandle = INVALID_HANDLE_VALUE;
        if (Bini.Init())
        {
            bBini = true;
            return true;
        }
    }
    else
    {
        Temp.seek(0);
    }

    // 0630fb67: text. bBini = 0; the text map takes the local's name and five dwords and the
    // local forgets its view and handles (handoff_to inlined); LineNum = 0, Header[0] = 0,
    // Cursor = 0 when there is a view. The result stays true.
    bBini = false;
    Temp.handoff_to(Text);
    Text.LineNum = 0;
    Header[0] = '\0';
    Text.seek(0);
    return true;
}

bool INI_Reader::open_memory(char const* Buffer, unsigned int Bytes)
{
    // .text:0630fc50: a null Buffer or zero Bytes returns false without touching the reader.
    // Otherwise a local FileMap gets a malloc'd copy (FileMap::open_memory inlined), bBini = 0,
    // and the text map takes it over exactly as open's text path does (the previous text map
    // is not closed). BINI data in memory is never recognised.
    FileMap Temp;
    if (!Temp.open_memory(Buffer, Bytes))
        return false;

    bBini = false;
    Temp.handoff_to(Text);
    Text.LineNum = 0;
    Header[0] = '\0';
    Text.seek(0);
    return true;
}

void INI_Reader::close()
{
    // .text:0630f770: FileMap::close inlined on the map the flag selects (name erased,
    // view / mapping / file released, a malloc'd view freed, Cursor = 0). The flag, the
    // name buffers, LineNum and the BINI pointers are left as they are.
    if (bBini)
        Bini.close();
    else
        Text.close();
}

void INI_Reader::reset()
{
    // .text:0630f920. BINI: block and value pointers cleared, then seek(0xc) inlined (past the
    // header; min(Size, 0xc) when there is a view). The value index and params pointer stay.
    // Text: LineNum = 0, Header[0] = 0, seek(0) inlined.
    if (bBini)
    {
        Bini.Block = nullptr;
        Bini.Value = nullptr;
        Bini.seek(BiniHeaderBytes);
        return;
    }

    Text.LineNum = 0;
    Header[0] = '\0';
    Text.seek(0);
}

void INI_Reader::seek(unsigned int Pos)
{
    // .text:0630f6f0: FileMap::seek inlined on the map the flag selects.
    if (bBini)
        Bini.seek(Pos);
    else
        Text.seek(Pos);
}

unsigned int INI_Reader::tell() const
{
    // .text:0630f740: FileMap::tell inlined on the map the flag selects.
    if (bBini)
        return Bini.tell();
    return Text.tell();
}

int INI_Reader::get_line_num() const
{
    // .text:0630f970: BINI has no lines -- FDUMP warning (line 0x1f6), 0.
    if (bBini)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", IniSource, 0x1f6,
              "INI_Reader behavior here differs from traditional behavior.");
        return 0;
    }
    return Text.LineNum;
}

bool INI_Reader::is_end() const
{
    // .text:06310460 (SBB / INC = "not below"). BINI: the end is the string table --
    // !(Cursor < StringTableOffset) with a view, else !(0 < StringTableOffset); the header
    // pointer is read without a check. Text: !(Cursor < Size), else !(0 < Size).
    if (bBini)
    {
        unsigned int const Table = Bini.FileHeader->StringTableOffset;
        if (Bini.View != nullptr)
            return !(Bini.Cursor < Table);
        return !(0u < Table);
    }
    if (Text.View != nullptr)
        return !(Text.Cursor < Text.Size);
    return !(0u < Text.Size);
}

bool INI_Reader::ReadTextHeader()
{
    // FUN_0630f3d0 (read_header / find_header text path). A local LineParser32 (Buffer[0],
    // Length, Position = 0). Lines are read until one whose first non-blank char is '['
    // (skip_white inlined; Position < Length; the char is neither 0 nor ';' and is '['):
    // Position past '[', read_terminator(Header, ']') inlined; a name that starts with ';'
    // is a commented-out section and the scan goes on. No more lines: Header[0] = 0, false.
    LineParser32 Line;
    if (!Text.read_line(Line))
    {
        Header[0] = '\0';
        return false;
    }

    for (;;)
    {
        Line.skip_white();
        if (Line.Position < Line.Length && Line.Buffer[Line.Position] == '[')
        {
            Line.Position += 1;
            Line.read_terminator(Header, ']');
            if (Header[0] != ';')
                return true;
        }
        if (!Text.read_line(Line))
        {
            Header[0] = '\0';
            return false;
        }
    }
}

bool INI_Reader::ReadBiniHeader()
{
    // read_header's BINI branch (.text:06310082-06310166). With a current block, the values
    // not read yet are skipped (index < ValueCount, signed: MOVSX index vs MOVZX count): a
    // 3-byte value record is requested (request_pointer inlined); a short read leaves the value
    // null, otherwise the cursor moves over its ParamCount * 5 bytes (seek inlined); the index
    // is incremented either way. Then block and value = null (the params pointer stays). At or
    // past the string table (header pointer read without a check) -> false. A 4-byte block
    // record is requested: short -> block = null, false; else index = 0, true.
    if (Bini.Block != nullptr)
    {
        while (Bini.ValueIndex < Bini.Block->ValueCount)
        {
            unsigned int Got = 3;
            Bini.Value = (BINI_Value const*)Bini.request_pointer(Got);
            if (Got != 3)
                Bini.Value = nullptr;
            else
                Bini.seek(Bini.tell() + Bini.Value->ParamCount * BiniParamBytes);
            ++Bini.ValueIndex;
        }
    }

    Bini.Block = nullptr;
    Bini.Value = nullptr;
    if (!(Bini.tell() < Bini.FileHeader->StringTableOffset))
        return false;

    unsigned int Got = 4;
    Bini.Block = (BINI_Block const*)Bini.request_pointer(Got);
    if (Got != 4)
    {
        Bini.Block = nullptr;
        return false;
    }
    Bini.ValueIndex = 0;
    return true;
}

bool INI_Reader::read_header()
{
    // .text:06310070.
    if (bBini)
        return ReadBiniHeader();
    return ReadTextHeader();
}

bool INI_Reader::ReadTextValue()
{
    // LAB_0630f4e0 (read_value's text path). Per line: the line number and tell() are saved
    // and a fresh LineParser32 is read. No more lines: ValueName[0] = 0, false. Blank lines
    // (Position >= Length after skip_white) and lines starting with 0 or ';' are skipped. A
    // '[' line belongs to the next section: LineNum and the cursor are restored (seek
    // inlined: nothing to restore without a view) and false is returned. Otherwise
    // read_terminator2(ValueName, '=', ';'); without an '=' ValueParams[0] = 0, true; with
    // one, Position past it, skip_white, read_terminator(ValueParams, ';') inlined, true.
    LineParser32 Line;
    for (;;)
    {
        unsigned int const SavedLine = Text.LineNum;
        unsigned int const SavedCursor = Text.tell();
        Line.clear();
        if (!Text.read_line(Line))
        {
            ValueName[0] = '\0';
            return false;
        }

        Line.skip_white();
        if (!(Line.Position < Line.Length))
            continue;
        char const Ch = Line.Buffer[Line.Position];
        if (Ch == '\0' || Ch == ';')
            continue;
        if (Ch == '[')
        {
            Text.LineNum = SavedLine;
            Text.seek(SavedCursor);
            return false;
        }

        Line.read_terminator2(ValueName, '=', ';');
        if (!(Line.Position < Line.Length) || Line.Buffer[Line.Position] != '=')
        {
            ValueParams[0] = '\0';
            return true;
        }

        Line.Position += 1;
        Line.skip_white();
        Line.read_terminator(ValueParams, ';');
        return true;
    }
}

bool INI_Reader::read_value()
{
    // .text:06310170. BINI: the block pointer is read without a check; index >= ValueCount
    // (signed) -> false. A 3-byte value record is requested: short -> value = null, false.
    // Then ParamCount * 5 bytes are requested the same way (request_pointer inlined): the
    // params pointer is View + Cursor, or null when the file has no bytes left; the index is
    // incremented and the result is true either way.
    if (!bBini)
        return ReadTextValue();

    if (!(Bini.ValueIndex < Bini.Block->ValueCount))
        return false;

    unsigned int Got = 3;
    Bini.Value = (BINI_Value const*)Bini.request_pointer(Got);
    if (Got != 3)
    {
        Bini.Value = nullptr;
        return false;
    }

    unsigned int ParamBytes = Bini.Value->ParamCount * BiniParamBytes;
    Bini.Params = (FBiniParam const*)Bini.request_pointer(ParamBytes);
    ++Bini.ValueIndex;
    return true;
}

bool INI_Reader::find_header(char const* Name)
{
    // .text:06310250: reset() inlined, then read_header() (BINI loop / FUN_0630f3d0, the flag
    // re-tested every pass) until the section name compares equal under DACOM stricmp.
    reset();
    while (read_header())
    {
        if (is_header(Name))
            return true;
    }
    return false;
}

bool INI_Reader::is_header(char const* Name)
{
    // .text:063103c0: DACOM stricmp(section name, Name) == 0. BINI reads the block pointer
    // without a check.
    if (bBini)
        return CallDacomStricmp(Bini.String(Bini.Block->NameOffset), Name) == 0;
    return CallDacomStricmp(Header, Name) == 0;
}

bool INI_Reader::is_value(char const* Name)
{
    // .text:06310410: DACOM stricmp(value name, Name) == 0. BINI reads the value pointer
    // without a check.
    if (bBini)
        return CallDacomStricmp(Bini.String(Bini.Value->NameOffset), Name) == 0;
    return CallDacomStricmp(ValueName, Name) == 0;
}

char const* INI_Reader::get_header_ptr()
{
    // .text:063104c0: BINI string base + word [block], no check; text Header.
    if (bBini)
        return Bini.String(Bini.Block->NameOffset);
    return Header;
}

char const* INI_Reader::get_name_ptr()
{
    // .text:063104e0: BINI string base + word [value], no check; text ValueName.
    if (bBini)
        return Bini.String(Bini.Value->NameOffset);
    return ValueName;
}

char const* INI_Reader::get_file_name() const
{
    // .text:06310040: the name of the map the flag selects (the string's buffer, or the
    // library's empty string).
    if (bBini)
        return Bini.get_file_name();
    return Text.get_file_name();
}

char const* INI_Reader::get_name() const
{
    // .text:06263450: JMP get_file_name.
    return get_file_name();
}

char const* INI_Reader::get_line_ptr()
{
    // .text:0630fe10. BINI: no value -> ""; else sprintf(DAT_0658e800, "%s = ", value name),
    // the joined parameters (FUN_0630ff40 per parameter, ", " between) are built in the join
    // buffer and appended. Text: sprintf(DAT_063fd198, "%s = %s", ValueName, ValueParams).
    if (bBini)
    {
        if (Bini.Value == nullptr)
            return EmptyC;
        _snprintf(GBiniLineScratch, sizeof(GBiniLineScratch), "%s = ",
                  Bini.String(Bini.Value->NameOffset));
        GBiniLineScratch[sizeof(GBiniLineScratch) - 1u] = '\0';
        AppendBounded(GBiniLineScratch, sizeof(GBiniLineScratch), Bini.JoinParams());
        return GBiniLineScratch;
    }

    _snprintf(GTextLineScratch, sizeof(GTextLineScratch), "%s = %s", ValueName, ValueParams);
    GTextLineScratch[sizeof(GTextLineScratch) - 1u] = '\0';
    return GTextLineScratch;
}

unsigned int INI_Reader::get_num_parameters() const
{
    // .text:06310510. BINI: ParamCount of the value, read without a check. Text: 0 for an
    // empty ValueParams, else 1 + the number of ',' (strchr loop).
    if (bBini)
        return Bini.Value->ParamCount;

    char const* Cursor = ValueParams;
    unsigned int Count = 0;
    if (*Cursor != '\0')
    {
        for (;;)
        {
            Count += 1;
            Cursor = strchr(Cursor, ',');
            if (Cursor == nullptr)
                break;
            Cursor += 1;
        }
    }
    return Count;
}

bool INI_Reader::is_value_empty(unsigned int Index)
{
    // .text:06310560. BINI: false only when a value exists and Index < ParamCount. Text: the
    // StringIndex field is empty.
    if (bBini)
    {
        if (Bini.Value != nullptr && Index < Bini.Value->ParamCount)
            return false;
        return true;
    }
    return StringIndex(ValueParams, Index)[0] == '\0';
}

bool INI_Reader::get_value_bool(unsigned int Index)
{
    // .text:063105b0. BINI: no value -> false; Index >= ParamCount -> report, false. Type 0
    // returns the byte itself (non-zero = true), 1 int != 0, 2 float compared with 0.0
    // (FCOMP double 0639c430; TEST AH,0x44; JNP false: only "equal" (0x40) has PF=0, so
    // less, greater and unordered are true), 3 the string: DACOM stricmp "true" -> true,
    // "false" -> false, else atoi != 0; other types false.
    // Text: the StringIndex field under DACOM stricmp: "true" / "1" true, "false" / "0" false;
    // anything else prints the FDUMP error (line 0x126, the text map's file name) and is false.
    if (bBini)
    {
        if (Bini.Value == nullptr)
            return false;
        if (Index >= Bini.Value->ParamCount)
        {
            Bini.ReportMissingParam(Index);
            return false;
        }
        FBiniParam const* const Param = Bini.ParamAt(Index);
        switch (Param->Type)
        {
        case BiniParamByte:
            return Param->Byte != 0;
        case BiniParamInt:
            return Param->Int != 0;
        case BiniParamFloat:
            return !(Param->Float == 0.0);
        case BiniParamString:
        {
            char const* const Field = Bini.String(Param->StringOffset);
            if (CallDacomStricmp(Field, "true") == 0)
                return true;
            if (CallDacomStricmp(Field, "false") == 0)
                return false;
            return MsvcrtAtoi(Field) != 0;
        }
        default:
            return false;
        }
    }

    char const* const Field = StringIndex(ValueParams, Index);
    if (CallDacomStricmp(Field, "true") == 0)
        return true;
    if (CallDacomStricmp(Field, "1") == 0)
        return true;
    if (CallDacomStricmp(Field, "false") == 0)
        return false;
    if (CallDacomStricmp(Field, "0") == 0)
        return false;

    FDUMP(0x100001, "%s(%d) : *** ERROR: Invalid bool value %s in .ini file %s\n", IniSource, 0x126,
          Field, Text.get_file_name());
    return false;
}

int INI_Reader::get_value_int(unsigned int Index)
{
    // .text:063107c0. BINI: no value -> 0; Index >= ParamCount -> report, 0. Type 0 the
    // zero-extended byte, 1 the int, 2 the float through _ftol (FLD + CALL _ftol, EAX kept:
    // the low 32 bits of the 64-bit truncation, 2^32 -> 0), 3 MSVCRT atoi of the string,
    // other types 0. Text: MSVCRT atoi of the StringIndex field.
    if (bBini)
    {
        if (Bini.Value == nullptr)
            return 0;
        if (Index >= Bini.Value->ParamCount)
        {
            Bini.ReportMissingParam(Index);
            return 0;
        }
        FBiniParam const* const Param = Bini.ParamAt(Index);
        switch (Param->Type)
        {
        case BiniParamByte:
            return Param->Byte;
        case BiniParamInt:
            return Param->Int;
        case BiniParamFloat:
            // _ftol truncates to 64 bits; EAX keeps the low half.
            return (int)(long long)Param->Float;
        case BiniParamString:
            return MsvcrtAtoi(Bini.String(Param->StringOffset));
        default:
            return 0;
        }
    }
    return MsvcrtAtoi(StringIndex(ValueParams, Index));
}

double INI_Reader::ValueAsNumber(unsigned int Index)
{
    // get_value_float's body (.text:063108f0); the result stays on the x87 stack, so
    // value_num (JMP get_value_float) sees it unrounded. BINI: the default is the float
    // DAT_06399408 (0.0), returned for no value and, after the report, for Index >=
    // ParamCount and unknown types. Type 0 FILD of the zero-extended byte, 1 FILD of the int,
    // 2 the float, 3 MSVCRT atof of the string. Text: ParseNumber of the StringIndex field.
    if (bBini)
    {
        if (Bini.Value == nullptr)
            return 0.0;
        if (Index >= Bini.Value->ParamCount)
        {
            Bini.ReportMissingParam(Index);
            return 0.0;
        }
        FBiniParam const* const Param = Bini.ParamAt(Index);
        switch (Param->Type)
        {
        case BiniParamByte:
            return Param->Byte;
        case BiniParamInt:
            return Param->Int;
        case BiniParamFloat:
            return Param->Float;
        case BiniParamString:
            return MsvcrtAtof(Bini.String(Param->StringOffset));
        default:
            return 0.0;
        }
    }
    return ParseNumber(StringIndex(ValueParams, Index));
}

double INI_Reader::get_value_float(unsigned int Index)
{
    // .text:063108f0: the x87 result is returned unrounded; callers either FSTP it as a float
    // (an implicit double -> float conversion in the port) or use the full value (FSTP double,
    // _ftol, FCOMP double).
    return ValueAsNumber(Index);
}

char const* INI_Reader::get_value_string(unsigned int Index)
{
    // .text:06310a30: BINI is FUN_0630ff40's body inlined; text the StringIndex field.
    if (bBini)
        return Bini.FormatParam(Index);
    return StringIndex(ValueParams, Index);
}

char const* INI_Reader::get_value_string()
{
    // .text:06310b70: BINI the joined parameters (join buffer), text ValueParams.
    if (bBini)
        return Bini.JoinParams();
    return ValueParams;
}

wchar_t const* INI_Reader::get_value_wstring()
{
    // .text:06310d20. BINI: the joined parameters through mbstowcs(DAT_063fcf98, join,
    // 0x100). Text: mbstowcs(ValueWide (+0xc28), ValueParams, 0x400). No terminator is added
    // by either (ValueParams always fits).
    if (bBini)
    {
        mbstowcs(GBiniWideScratch, Bini.JoinParams(), 0x100);
        return GBiniWideScratch;
    }
    mbstowcs(ValueWide, ValueParams, NameChars);
    return ValueWide;
}

char const* INI_Reader::get_value_ptr()
{
    // .text:062634c0: JMP get_value_string().
    return get_value_string();
}

char const* INI_Reader::get_indexed_value(unsigned int Index)
{
    // .text:062634d0: JMP get_value_string(unsigned).
    return get_value_string(Index);
}

bool INI_Reader::get_bool(unsigned int Index)
{
    // .text:062634e0: JMP get_value_bool.
    return get_value_bool(Index);
}

double INI_Reader::value_num(unsigned int Index)
{
    // .text:062634b0: JMP get_value_float -- the unrounded x87 result.
    return ValueAsNumber(Index);
}

Vector INI_Reader::get_vector()
{
    // .text:06263460: get_value_float(0), (1), (2) in that order.
    Vector Result;
    Result.x = get_value_float(0);
    Result.y = get_value_float(1);
    Result.z = get_value_float(2);
    return Result;
}

void INI_Reader::get_state(State& Out) const
{
    // .text:06310f00. BINI writes five fields over the start of State: +0 tell(), +4 block -
    // view, +8 the word value index, +0xc value - view, +0x10 params - view (null pointers
    // give negative offsets, kept as such); nothing else in State is touched. Text: tell(),
    // LineNum, and the three 0x400 buffers (REP MOVSD 0x100 dwords each).
    if (bBini)
    {
        // Pointer -> offset as integer arithmetic: a null pointer gives a negative offset.
        std::uintptr_t const Base = (std::uintptr_t)Bini.View;
        Out.Cursor = Bini.tell();
        Out.Bini.BlockOffset = (int)((std::uintptr_t)Bini.Block - Base);
        Out.Bini.ValueIndex = Bini.ValueIndex;
        Out.Bini.ValueOffset = (int)((std::uintptr_t)Bini.Value - Base);
        Out.Bini.ParamsOffset = (int)((std::uintptr_t)Bini.Params - Base);
        return;
    }

    Out.Cursor = Text.tell();
    Out.Text.LineNum = Text.LineNum;
    memcpy(Out.Text.Header, Header, NameChars);
    memcpy(Out.Text.ValueName, ValueName, NameChars);
    memcpy(Out.Text.ValueParams, ValueParams, NameChars);
}

void INI_Reader::set_state(State const& In)
{
    // .text:06310fc0. BINI: seek(+0) inlined, then block / value / params = view + the saved
    // offsets (even without a view: the offsets become the pointers) and the word index.
    // Text: seek(+0) inlined, LineNum, the three buffers.
    if (bBini)
    {
        // Offset -> pointer as integer arithmetic (also without a view, as in retail).
        std::uintptr_t const Base = (std::uintptr_t)Bini.View;
        Bini.seek(In.Cursor);
        Bini.Block = (BINI_Block const*)(Base + In.Bini.BlockOffset);
        Bini.ValueIndex = In.Bini.ValueIndex;
        Bini.Value = (BINI_Value const*)(Base + In.Bini.ValueOffset);
        Bini.Params = (FBiniParam const*)(Base + In.Bini.ParamsOffset);
        return;
    }

    Text.seek(In.Cursor);
    Text.LineNum = In.Text.LineNum;
    memcpy(Header, In.Text.Header, NameChars);
    memcpy(ValueName, In.Text.ValueName, NameChars);
    memcpy(ValueParams, In.Text.ValueParams, NameChars);
}

void INI_Reader::log_link(char const* Link) const
{
    // .text:062ed730: a bare `RET 4` (ICF-shared with CostumeDescriptions::warn). The
    // retail build compiled the link logging out; nothing happens.
    (void)Link;
}

bool INI_Reader::is_number(char const* Candidate) const
{
    // .text:06311080. No null check. An optional '-' / '+', then an optional '.' (counts as
    // the one decimal point). A quote is stepped over; tolower of the next char in 'a'..'z'
    // followed by a closing quote is a number; otherwise the checks below continue AFTER the
    // quote. "0x" / "0X": the rest must be decimal digits or tolower 'a'..'f' (an empty rest
    // is a number). Otherwise the first char must be a decimal digit; after it: digits, one
    // sign but only right after the exponent marker, one '.', one 'e' / 'E' (which re-allows
    // a sign); anything else is not a number. tolower is MSVCRT's on the sign-extended char.
    char const* Cursor = Candidate;
    bool bSawExp = false;
    bool bSawDot = false;
    bool bSignLocked = true;

    if (*Cursor == '-' || *Cursor == '+')
        Cursor += 1;
    if (*Cursor == '.')
    {
        Cursor += 1;
        bSawDot = true;
    }
    if (*Cursor == '\'')
    {
        Cursor += 1;
        int const Lower = ParseToLower(*Cursor);
        if (Lower >= 'a' && Lower <= 'z' && Cursor[1] == '\'')
            return true;
    }

    if (*Cursor == '0' && ParseToLower(Cursor[1]) == 'x')
    {
        Cursor += 2;
        for (char Ch = *Cursor; Ch != '\0'; Cursor += 1, Ch = *Cursor)
        {
            if (Ch >= '0' && Ch <= '9')
                continue;
            int const Lower = ParseToLower(Ch); // -128..127: the byte compare retail does
            if (Lower < 'a' || Lower > 'f')
                return false;
        }
        return true;
    }

    if (*Cursor < '0' || *Cursor > '9')
        return false;
    Cursor += 1;
    for (char Ch = *Cursor; Ch != '\0'; Cursor += 1, Ch = *Cursor)
    {
        if (Ch >= '0' && Ch <= '9')
            continue;
        if (!bSignLocked && (Ch == '+' || Ch == '-'))
        {
            bSignLocked = true;
            continue;
        }
        if (!bSawDot && Ch == '.')
        {
            bSawDot = true;
            continue;
        }
        if (bSawExp)
            return false;
        if (ParseToLower(Ch) != 'e')
            return false;
        bSawExp = true;
        bSignLocked = false;
    }
    return true;
}
