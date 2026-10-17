// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ids_strings.h"
#include "msvcrt_compat.h" // MsvcrtWtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates

#include "common_text.h"
#include "dacom.h"
#include "globals.h"
#include "misc_util.h" // FormatGroupedNumber (FUN_004779a0)

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <vector>

namespace
{
    // DAT_0067c3fc (the allocator byte) / DAT_0067c400..DAT_0067c408: the ids resource DLLs,
    // a std::vector<HMODULE>. LAB_0057d7b0 (static init) zeroes it and registers LAB_0057d7e0,
    // which frees the buffer, through atexit. An ids value's high word indexes it.
    std::vector<HMODULE> GIdsModules;

    // FUN_0057d980, cdecl(&ids): the module the high word of *Ids names. In range (unsigned
    // JNC against the element count; an unallocated vector is out of range) the high word is
    // cleared and the element returned -- a registered null handle included; otherwise null
    // and *Ids untouched.
    HMODULE TakeIdsModule(unsigned int* Ids)
    {
        unsigned int const Index = *Ids >> 16;
        if (Index >= GIdsModules.size())
            return nullptr;
        *Ids &= 0xffffu;
        return GIdsModules[Index];
    }

    // FUN_0057d9c0 (.text:0057d9c0-0057da3a), cdecl(module, id, &data, &words): both outputs
    // zeroed first, then the resource of type 0x17 named by the numeric id. False on a missing
    // resource or a failed load / lock; otherwise the word count (SizeofResource >> 1) and the
    // locked pointer.
    bool LoadRawTextResource(HMODULE Module, unsigned int Id, wchar_t const** OutData,
                              unsigned int* OutWordCount)
    {
        *OutData = nullptr;
        *OutWordCount = 0;

        HRSRC ResourceInfo = FindResourceExA(Module, MAKEINTRESOURCEA(0x17), MAKEINTRESOURCEA(Id), 0);
        if (ResourceInfo == nullptr)
            return false;

        DWORD SizeBytes = SizeofResource(Module, ResourceInfo);
        HGLOBAL ResourceData = LoadResource(Module, ResourceInfo);
        if (ResourceData == nullptr)
            return false;

        LPVOID Data = LockResource(ResourceData);
        if (Data == nullptr)
            return false;

        *OutWordCount = SizeBytes >> 1;
        *OutData = static_cast<wchar_t const*>(Data);
        return true;
    }
}

unsigned int RegisterIdsResourceModule(HMODULE Module)
{
    // FUN_0057d800 (.text:0057d800-0057d97d), cdecl(module): the element count before the
    // insert, shifted into the high word (the ids base of this DLL), then push_back -- no
    // null test and no duplicate test (0x0057d925 in-place path, 0x0057d86c grow path).
    unsigned int const Base = static_cast<unsigned int>(GIdsModules.size()) << 16;
    GIdsModules.push_back(Module);
    return Base;
}

HMODULE GetIdsResourceModule(unsigned int Ids)
{
    return TakeIdsModule(&Ids);
}

unsigned int GetIdsStringW(unsigned int Ids, wchar_t* Dest, int DestChars)
{
    // FUN_00434720 (.text:00434720-004347d2), cdecl(unused, ids, dest, count); the first
    // argument is never read. FUN_0057d980 runs on the ids copy first, so the block and entry
    // below come from the low word only when the high word named a registered module; a null
    // module (out of range, or a registered null handle) goes to FindResourceExA as is, which
    // searches the process image.
    HMODULE const Module = TakeIdsModule(&Ids);
    if (Dest != nullptr && DestChars < 1)
        return 0;

    // 0x0043474b..0x00434765: RT_STRING block (ids >> 4) + 1 as a WORD, entry ids & 0xf.
    unsigned short const Block = static_cast<unsigned short>((Ids >> 4) + 1);
    unsigned int Entry = Ids & 0xfu;
    HRSRC const ResourceInfo =
        FindResourceExA(Module, MAKEINTRESOURCEA(6), MAKEINTRESOURCEA(Block), 0);
    unsigned short const* Cursor = nullptr;
    if (ResourceInfo != nullptr)
        Cursor = static_cast<unsigned short const*>(
            LockResource(LoadResource(Module, ResourceInfo)));
    if (Cursor == nullptr)
    {
        // 0x00434782.
        if (Dest != nullptr)
            Dest[0] = L'\0';
        return 0;
    }

    // 0x00434792..0x0043479e: step over the entries before this one.
    while (Entry != 0)
    {
        --Entry;
        Cursor = Cursor + Cursor[0] + 1;
    }

    unsigned int const Length = Cursor[0];
    if (Dest == nullptr)
        return Length;

    // 0x004347ae..0x004347cd: min(count - 1, length), unsigned (JC keeps count - 1).
    unsigned int Count = static_cast<unsigned int>(DestChars - 1);
    if (!(Count < Length))
        Count = Length;
    wcsncpy(Dest, reinterpret_cast<wchar_t const*>(Cursor + 1), Count);
    Dest[Count] = L'\0';
    return Count;
}

unsigned int GetIdsStringA(unsigned int Ids, char* Dest, int DestChars)
{
    if (Dest != nullptr && DestChars < 1)
        return 0;

    wchar_t Wide[512];
    unsigned int WideCount = GetIdsStringW(Ids, Wide, 512);
    if (WideCount == 0)
    {
        if (Dest != nullptr)
            Dest[0] = '\0';
        return 0;
    }

    if (Dest == nullptr)
        return static_cast<unsigned int>(WideCharToMultiByte(CP_ACP, 0, Wide, -1, nullptr, 0,
                                                             nullptr, nullptr));

    int Converted = WideCharToMultiByte(CP_ACP, 0, Wide, -1, Dest, DestChars, nullptr, nullptr);
    if (Converted <= 0)
    {
        Dest[0] = '\0';
        return 0;
    }
    return static_cast<unsigned int>(Converted - 1);
}

unsigned int GetIdsStringWWithRawFallback(unsigned int Ids, wchar_t* Dest, int DestChars)
{
    // FUN_004347e0 (.text:004347e0-00434872), cdecl(unused, ids, dest, count). The STRINGTABLE
    // lookup FUN_00434720 first; only a zero result falls back to the raw resource.
    unsigned int const Copied = GetIdsStringW(Ids, Dest, DestChars);
    if (Copied != 0)
        return Copied;

    // 0x00434808..0x00434835: FUN_0057d980 on the ids copy, then FUN_0057d9c0 with its low
    // word -- the module is not tested (a null one searches the process image).
    HMODULE const Module = TakeIdsModule(&Ids);
    wchar_t const* RawData = nullptr;
    unsigned int RawWordCount = 0;
    if (!LoadRawTextResource(Module, Ids & 0xffffu, &RawData, &RawWordCount))
        return 0;

    // 0x00434837..0x00434864: min(words - 1, count - 1), unsigned (JC keeps words - 1); the text
    // starts one word into the resource (ADD ESI,2); REP MOVS, then the terminator. Dest is
    // written without a null test.
    unsigned int Count = RawWordCount - 1;
    unsigned int const Room = static_cast<unsigned int>(DestChars - 1);
    if (!(Count < Room))
        Count = Room;
    memcpy(Dest, RawData + 1, Count * sizeof(wchar_t));
    Dest[Count] = L'\0';
    return Count;
}

FmtStr::Val const* FmtStrFindValue(FmtStr const* Fmt, unsigned char Index, unsigned short Tag)
{
    // Retail FUN_004ba830 (NN_StoryStar.cpp, cdecl): the first Val of Fmt's list whose TypeTag
    // (+0x08) equals Tag and whose Flags byte (+0x0a) equals Index; null when none does.
    for (FmtStr::Val const* Node = Fmt->Head; Node != nullptr; Node = Node->Next)
    {
        if (Node->TypeTag == Tag && Node->Flags == Index)
            return Node;
    }
    return nullptr;
}

namespace
{
    // The escape switch's "Override" term, max(Override, 0): SETLE / DEC / AND (0x004baf99..
    // 0x004bafa1 for 'I', 0x004bafe5..0x004bafed for 'L').
    inline unsigned int PositiveOverride(int Override)
    {
        return Override > 0 ? static_cast<unsigned int>(Override) : 0u;
    }
}

int FormatFmtStrMadLib(FmtStr const* Fmt, wchar_t* Dest, int DestChars, bool bIncludeMission)
{
    // Retail FUN_004ba860 (NN_StoryStar.cpp, cdecl, 0x20dc-byte frame). Fmt->MadLibId names an
    // ids string that is the TEMPLATE; its '%' escapes are expanded against Fmt's Val list.
    // Every ids load below is FUN_004347e0 into the cursor with the room left, the FDUMP
    // "Failed to load ids" at line 0x2b3 on a zero result, and the end-of-buffer rewrite --
    // FUN_004bb140's body (AppendIdsStringOrWarn), written out inline at most sites.
    //
    // 0x004ba86a..0x004ba8a1: End = Dest + DestChars; no template id -> *Dest = 0, return 0.
    wchar_t* const End = Dest + DestChars;
    if (Fmt->MadLibId == 0)
    {
        *Dest = L'\0';
        return 0;
    }

    // 0x004ba8a2..0x004ba8c0: the template into the 0x1000-character frame buffer. The result
    // is not tested.
    wchar_t Template[0x1000];
    GetIdsStringWWithRawFallback(Fmt->MadLibId, Template, 0x1000);

    wchar_t* Cursor = Dest;              // [ESP+0x20f4]
    wchar_t const* Src = Template;       // EBP
    bool bEscape = false;                // CL
    wchar_t Ch = *Src;                   // DI
    while (Ch != L'\0')
    {
        // 0x004ba8e0: step past the character.
        ++Src;
        if (Ch == L'%')
        {
            // 0x004ba8ed: the next character is an escape code; "%%" sets the flag again.
            bEscape = true;
            Ch = *Src;
            continue;
        }
        if (!bEscape)
        {
            // 0x004bb05d..0x004bb078: a literal, re-terminated after every character; no room
            // test.
            *Cursor = Ch;
            ++Cursor;
            *Cursor = L'\0';
            Ch = *Src;
            continue;
        }

        // 0x004ba8fc..0x004ba92c: up to 64 decimal digits after the code, _wtoi'd into the
        // byte index.
        wchar_t Digits[0x41];
        int DigitCount = 0;
        while (*Src >= L'0' && *Src <= L'9' && DigitCount < 0x40)
        {
            Digits[DigitCount] = *Src;
            ++Src;
            ++DigitCount;
        }
        Digits[DigitCount] = L'\0';
        unsigned char const Index = static_cast<unsigned char>(MsvcrtWtoi(Digits));

        // 0x004ba941..0x004ba966: "v<digit>" -- the digit is the LOW BYTE of the next wchar
        // (MOV CL,[EBP+2], signed JL / JG against '0' / '9').
        int Override = -1;
        if (*Src == L'v')
        {
            signed char const Next = static_cast<signed char>(Src[1] & 0xff);
            if (Next >= '0' && Next <= '9')
            {
                Override = Next - '0';
                Src += 2;
            }
        }

        // 0x004ba969..0x004ba981: switch (Code - 0x21) through the byte table 0x004bb0ec.
        FmtStr::Val const* Found = nullptr;
        switch (Ch)
        {
        case L'!':
            // caseD_21 0x004bb00d: FUN_004ba830, then the nested FmtStr into the rest of the
            // buffer with the same flag.
            Found = FmtStrFindValue(Fmt, Index, L'!');
            if (Found != nullptr)
            {
                Cursor += FormatFmtStrMadLib(Found->Subtree, Cursor,
                                             static_cast<int>(End - Cursor), bIncludeMission);
            }
            break;

        case L'M':
            // caseD_4d 0x004ba988: only with the flag, then the generic path below.
            if (!bIncludeMission)
                break;
            // fall through
        case L'B':
        case L'G':
        case L'R':
        case L'S':
        case L's':
            // caseD_47 0x004ba997: the value's ids, skipped when it is 2.
            Found = FmtStrFindValue(Fmt, Index, static_cast<unsigned short>(Ch));
            if (Found != nullptr && Found->Value != 2)
                AppendIdsStringOrWarn(&Cursor, End, Found->Value);
            break;

        case L'F':
            // caseD_46 0x004baa44: the faction's ids, or with an override the faction's
            // Override-th alternative name (Data + Override * 100 + 0x202fa).
            Found = FmtStrFindValue(Fmt, Index, L'F');
            if (Found != nullptr)
            {
                if (Override < 0)
                    AppendIdsStringOrWarn(&Cursor, End, Found->Value);
                else
                    AppendIdsStringOrWarn(&Cursor, End,
                                          Found->Value + Override * 0x64 + 0x202fa);
            }
            break;

        case L'd':
            // caseD_64 0x004baa8c: FUN_004779a0(Number, Data, 0x1f) and wcscat when the text
            // fits before End.
            Found = FmtStrFindValue(Fmt, Index, L'd');
            if (Found != nullptr)
            {
                wchar_t Number[0x20];
                FormatGroupedNumber(Number, static_cast<int>(Found->Value), 0x1f);
                size_t const Length = wcslen(Number);
                if (Cursor + Length < End)
                {
                    wcscat(Cursor, Number);
                    Cursor += Length;
                }
            }
            break;

        case L'Z':
            // caseD_5a 0x004bab0d: a zone id below 0x40 is a sector -- ids 0x331b ("sector"),
            // ids 1, the column letter (Data & 7) + 0x514 and the row digit ((Data >> 3) & 7) +
            // 0x506; anything else is the zone's own ids. With an override the sector head is
            // Override * 200 + 0x50fa0 and a named zone Data + Override * 200 + 0x11349.
            Found = FmtStrFindValue(Fmt, Index, L'Z');
            if (Found != nullptr)
            {
                unsigned int const Zone = Found->Value;
                if (Override < 0)
                {
                    if (Zone >= 0x40)
                    {
                        AppendIdsStringOrWarn(&Cursor, End, Zone);
                    }
                    else
                    {
                        AppendIdsStringOrWarn(&Cursor, End, 0x331b);
                        AppendIdsStringOrWarn(&Cursor, End, 1);
                        AppendIdsStringOrWarn(&Cursor, End, (Zone & 7) + 0x514);
                        AppendIdsStringOrWarn(&Cursor, End, ((Zone >> 3) & 7) + 0x506);
                    }
                }
                else
                {
                    if (Zone >= 0x40)
                    {
                        AppendIdsStringOrWarn(&Cursor, End, Zone + Override * 0xc8 + 0x11349);
                    }
                    else
                    {
                        AppendIdsStringOrWarn(&Cursor, End, Override * 0xc8 + 0x50fa0);
                        AppendIdsStringOrWarn(&Cursor, End, 1);
                        AppendIdsStringOrWarn(&Cursor, End, (Zone & 7) + 0x514);
                        AppendIdsStringOrWarn(&Cursor, End, ((Zone >> 3) & 7) + 0x506);
                    }
                }
            }
            break;

        case L'I':
            // caseD_49 0x004baf6d: FUN_004bb140 with Data + max(Override, 0) * 100 + 0x507d0.
            Found = FmtStrFindValue(Fmt, Index, L'I');
            if (Found != nullptr)
            {
                AppendIdsStringOrWarn(&Cursor, End,
                                      Found->Value + PositiveOverride(Override) * 0x64 +
                                          0x507d0);
            }
            break;

        case L'L':
            // caseD_4c 0x004bafc4: FUN_004ba830, then FUN_004bb140 with Data + max(Override, 0)
            // * 100 + 0x50bb8.
            Found = FmtStrFindValue(Fmt, Index, L'L');
            if (Found != nullptr)
            {
                AppendIdsStringOrWarn(&Cursor, End,
                                      Found->Value + PositiveOverride(Override) * 0x64 +
                                          0x50bb8);
            }
            break;

        default:
            // caseD_22: every other code emits nothing.
            break;
        }

        // caseD_23 0x004bb07d: back to plain characters.
        bEscape = false;
        Ch = *Src;
    }

    // 0x004bb094..0x004bb0bd: a cursor on End is pulled back one and terminated; the count is
    // the cursor's distance from Dest.
    if (Cursor == End)
    {
        --Cursor;
        *Cursor = L'\0';
    }
    return static_cast<int>(Cursor - Dest);
}

void AppendIdsStringOrWarn(wchar_t** pCursor, wchar_t* pEnd, unsigned int Ids)
{
    // Retail FUN_004bb140 (cdecl).
    wchar_t* Cursor = *pCursor;
    unsigned int RemainingChars = static_cast<unsigned int>(pEnd - Cursor);
    unsigned int Copied = GetIdsStringWWithRawFallback(Ids, Cursor, static_cast<int>(RemainingChars));
    if (Copied == 0)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: Failed to load ids: %u",
              "E:\\FL\\Scratch\\Source\\Client\\nn\\NN_StoryStar.cpp", 0x2b3, Ids);
    }
    Cursor += Copied;
    if (Cursor == pEnd)
    {
        // Buffer completely filled: retail rewinds one wchar, zero-terminates it, then restores
        // the cursor to pEnd so the caller still sees the buffer as "full".
        Cursor -= 1;
        *Cursor = L'\0';
        Cursor += 1;
    }
    *pCursor = Cursor;
}
