// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <stdexcept>
#include <windows.h>

// movie.dll CRT helpers. Addresses are EXE/Disasm/movie.dll.txt.

static const int GMovieLockCount = 0x30;
static CRITICAL_SECTION* GMovieLockTable[GMovieLockCount];
static CRITICAL_SECTION GMovieLockIo;
static CRITICAL_SECTION GMovieLockHeap;
static CRITICAL_SECTION GMovieLockOnexit;
static CRITICAL_SECTION GMovieLockAlloc;
static int GMovieLocksReady = 0;

static HANDLE GMovieCrtHeap = nullptr;
static int GMovieHeapKind = 0;
static unsigned int GMovieSbhThreshold = 0;
static int GMovieStreamCount = 0;

static int GMovieErrorMode = 0;
static int GMovieAppType = 0;
static LPTOP_LEVEL_EXCEPTION_FILTER GMovieSavedFilter = nullptr;
static void (__cdecl* GMovieRuntimeHook)() = nullptr;
static int GMovieExitInProgress = 0;
static int GMovieExitMarked = 0;
static uint8_t GMovieExitQuick = 0;

static void (__cdecl** GMovieOnexitBegin)() = nullptr;
static void (__cdecl** GMovieOnexitEnd)() = nullptr;

static void MovieAmsgExit(int RuntimeError);
static void* MovieNhMalloc(unsigned int Size, int NhFlag);
static void MovieHeapFree(void* Block);

// FUN_06ad22e5. InitializeCriticalSection on the four static lock objects.
// Table 06add57c..06add63c. Slots: 1, 9, 0xD, 0x11.
static void MovieInitStaticLocks()
{
    if (GMovieLocksReady != 0)
    {
        return;
    }
    GMovieLockTable[1] = &GMovieLockIo;
    GMovieLockTable[9] = &GMovieLockHeap;
    GMovieLockTable[0x0D] = &GMovieLockOnexit;
    GMovieLockTable[0x11] = &GMovieLockAlloc;
    InitializeCriticalSection(GMovieLockTable[0x11]);
    InitializeCriticalSection(GMovieLockTable[0x0D]);
    InitializeCriticalSection(GMovieLockTable[9]);
    InitializeCriticalSection(GMovieLockTable[1]);
    GMovieLocksReady = 1;
}

// FUN_06ad23db. LeaveCriticalSection(table[index]).
static void MovieUnlock(int Index)
{
    LeaveCriticalSection(GMovieLockTable[Index]);
}

// FUN_06ad237a. Create the CRITICAL_SECTION on first use, then enter it.
static void MovieLock(int Index)
{
    CRITICAL_SECTION** Slot = &GMovieLockTable[Index];
    if (*Slot == nullptr)
    {
        CRITICAL_SECTION* Created = (CRITICAL_SECTION*)MovieNhMalloc(0x18, 0);
        if (Created == nullptr)
        {
            MovieAmsgExit(0x11);
        }
        MovieLock(0x11);
        if (*Slot == nullptr)
        {
            InitializeCriticalSection(Created);
            *Slot = Created;
        }
        else
        {
            MovieHeapFree(Created);
        }
        MovieUnlock(0x11);
    }
    EnterCriticalSection(*Slot);
}

// FUN_06ad157a. _lock(0xD).
static void MovieLockOnexit()
{
    MovieLock(0x0D);
}

// FUN_06ad1583. _unlock(0xD).
static void MovieUnlockOnexit()
{
    MovieUnlock(0x0D);
}

// FUN_06ad52bc. DAT_06ae1090 starts 0, so this returns 0 until a handler is stored.
static int (__cdecl* GMovieNewHandler)(unsigned int) = nullptr;
static int GMovieNewMode = 0;
static int GMovieMbCodePage = 1;
static int GMovieLocaleUseOs = 0;
static LONG GMovieLocaleRefs = 0;
static int GMovieLocaleOwned = 0;
static DWORD GMovieTlsIndex = 0xFFFFFFFFu;

static int* MovieErrnoSlot();

static int MovieCallNewHandler(unsigned int Size)
{
    if (GMovieNewHandler != nullptr && GMovieNewHandler(Size) != 0)
    {
        return 1;
    }
    return 0;
}

// Word at DAT_06add75a + byte*2. PTR_DAT_06add750 holds 06add75a.
// 0x20 is 0x48, 0x09..0x0D are 0x28, 0x00..0x1F and 0x7F are 0x20, '0'..'9' are 0x84.
// 'A'..'F' are 0x81, 'G'..'Z' are 0x01, 'a'..'f' are 0x82, 'g'..'z' are 0x02.
// 0x80..0xFF are 0. The other bytes below 0x80 are 0x10.
static uint16_t MovieCtypeWord(unsigned int Byte)
{
    if (Byte == 0x20)
    {
        return 0x48;
    }
    if (Byte >= 0x09 && Byte <= 0x0D)
    {
        return 0x28;
    }
    if (Byte <= 0x1F || Byte == 0x7F)
    {
        return 0x20;
    }
    if (Byte >= (unsigned int)'0' && Byte <= (unsigned int)'9')
    {
        return 0x84;
    }
    if (Byte >= (unsigned int)'A' && Byte <= (unsigned int)'F')
    {
        return 0x81;
    }
    if (Byte >= (unsigned int)'G' && Byte <= (unsigned int)'Z')
    {
        return 0x01;
    }
    if (Byte >= (unsigned int)'a' && Byte <= (unsigned int)'f')
    {
        return 0x82;
    }
    if (Byte >= (unsigned int)'g' && Byte <= (unsigned int)'z')
    {
        return 0x02;
    }
    if (Byte >= 0x80 && Byte <= 0xFF)
    {
        return 0;
    }
    return 0x10;
}

// FUN_06ad3817 for a byte (char+1 <= 0x100): load the ctype word, AND the mask.
// DAT_06addc10 starts at 1, so strtol's JLE path does this directly.
static unsigned int MovieCtypeMask(unsigned int Byte, unsigned int Mask)
{
    // Both the JLE path and FUN_06ad3817's byte path (char+1 <= 0x100) load this word.
    unsigned int Word = (unsigned int)MovieCtypeWord(Byte & 0xFF);
    if (GMovieMbCodePage <= 1)
    {
        return Word & Mask;
    }
    return Word & Mask;
}

// FUN_06ad7b6b. DAT_06ae1128 caches the probe. DAT_06ae0f74 is the code page, image 0.
static int GMovieLcMapKind = 0;
static unsigned int GMovieLcCodePage = 0;

// FUN_06ad7d8f. Stop at a zero or after Count bytes. A zero returns the offset; otherwise Count.
static int MovieLcMapBound(const char* Text, int Count)
{
    const char* Cursor = Text;
    if (Count != 0)
    {
        int Remain = Count - 1;
        for (;;)
        {
            if (*Cursor == 0)
            {
                break;
            }
            Cursor = Cursor + 1;
            int Mark = Remain;
            Remain = Remain - 1;
            if (Mark == 0)
            {
                break;
            }
        }
    }
    if (*Cursor != 0)
    {
        return Count;
    }
    return (int)(Cursor - Text);
}

// FUN_06ad7b6b. Mode 2 is LCMapStringA. Mode 1 (06ad7c1d) is MultiByteToWideChar, LCMapStringW, WideCharToMultiByte.
// FUN_06acf7c0 sizes the stack buffers. The filters at 06ad7c7b and 06ad7d2f return 1 and the handlers zero the pointer.
static int MovieLcMapString(unsigned int Locale, unsigned int Flags, const char* Source, int SourceCount, char* Dest, int DestCount, unsigned int CodePage, int MultiFlag)
{
    if (GMovieLcMapKind == 0)
    {
        if (LCMapStringW(0, 0x100, L"", 1, nullptr, 0) != 0)
        {
            GMovieLcMapKind = 1;
        }
        else if (LCMapStringA(0, 0x100, "", 1, nullptr, 0) != 0)
        {
            GMovieLcMapKind = 2;
        }
        else
        {
            return 0;
        }
    }
    if (SourceCount > 0)
    {
        SourceCount = MovieLcMapBound(Source, SourceCount);
    }
    if (GMovieLcMapKind == 2)
    {
        return LCMapStringA(Locale, Flags, Source, SourceCount, Dest, DestCount);
    }
    if (GMovieLcMapKind != 1)
    {
        return 0;
    }
    if (CodePage == 0)
    {
        CodePage = GMovieLcCodePage;
    }
    int MbFlags = 1;
    if (MultiFlag != 0)
    {
        MbFlags = 9;
    }
    int WideCount = MultiByteToWideChar((UINT)CodePage, (DWORD)MbFlags, Source, SourceCount, nullptr, 0);
    if (WideCount == 0)
    {
        return 0;
    }
    unsigned int WideBytes = ((unsigned int)WideCount * 2u + 3u) & ~3u;
    wchar_t* Wide = nullptr;
    __try
    {
        Wide = (wchar_t*)_alloca(WideBytes);
    }
    __except (1)
    {
        Wide = nullptr;
    }
    if (Wide == nullptr)
    {
        return 0;
    }
    if (MultiByteToWideChar((UINT)CodePage, 1, Source, SourceCount, Wide, WideCount) == 0)
    {
        return 0;
    }
    int Mapped = LCMapStringW(Locale, Flags, Wide, WideCount, nullptr, 0);
    if (Mapped == 0)
    {
        return 0;
    }
    if ((Flags & 0x400u) != 0)
    {
        if (DestCount == 0)
        {
            return Mapped;
        }
        if (Mapped > DestCount)
        {
            return 0;
        }
        if (LCMapStringW(Locale, Flags, Wide, WideCount, (wchar_t*)Dest, DestCount) == 0)
        {
            return 0;
        }
        return Mapped;
    }
    unsigned int OutBytes = ((unsigned int)Mapped * 2u + 3u) & ~3u;
    wchar_t* OutWide = nullptr;
    __try
    {
        OutWide = (wchar_t*)_alloca(OutBytes);
    }
    __except (1)
    {
        OutWide = nullptr;
    }
    if (OutWide == nullptr)
    {
        return 0;
    }
    if (LCMapStringW(Locale, Flags, Wide, WideCount, OutWide, Mapped) == 0)
    {
        return 0;
    }
    char* OutBytesDest = nullptr;
    int OutByteCount = 0;
    if (DestCount != 0)
    {
        OutBytesDest = Dest;
        OutByteCount = DestCount;
    }
    int Written = WideCharToMultiByte((UINT)CodePage, 0x220, OutWide, Mapped, OutBytesDest, OutByteCount, nullptr, nullptr);
    if (Written == 0)
    {
        return 0;
    }
    return Written;
}

// FUN_06ad8c00. C locale is ASCII. Otherwise a lower-case ctype bit reaches LCMapString.
static unsigned int MovieCrtToUpperOs(unsigned int Byte)
{
    if (GMovieLocaleUseOs == 0)
    {
        if ((int)Byte >= 0x61 && (int)Byte <= 0x7A)
        {
            return Byte - 0x20u;
        }
        return Byte;
    }
    if (Byte < 0x100u)
    {
        unsigned int Low = (unsigned int)MovieCtypeWord(Byte) & 0xFFu;
        if ((Low & 2u) == 0)
        {
            return Byte;
        }
    }
    unsigned int Lead = (unsigned int)((int)Byte >> 8);
    unsigned int High = (unsigned int)MovieCtypeWord(Lead) >> 8;
    char Bytes[4];
    Bytes[0] = 0;
    Bytes[1] = 0;
    Bytes[2] = 0;
    Bytes[3] = 0;
    int Count = 1;
    if ((High & 0x80u) != 0)
    {
        Bytes[0] = (char)Lead;
        Bytes[1] = (char)Byte;
        Count = 2;
    }
    else
    {
        Bytes[0] = (char)Byte;
    }
    char Out[4];
    Out[0] = 0;
    Out[1] = 0;
    Out[2] = 0;
    Out[3] = 0;
    int Mapped = MovieLcMapString(GMovieLocaleUseOs, 0x200u, Bytes, Count, Out, 3, 0, 1);
    if (Mapped == 0)
    {
        return Byte;
    }
    if (Mapped == 1)
    {
        return (unsigned int)(unsigned char)Out[0];
    }
    return ((unsigned int)(unsigned char)Out[1] << 8) | (unsigned int)(unsigned char)Out[0];
}

// FUN_06ad8b91. DAT_06ae0f64 == 0 subtracts 0x20 from 'a'..'z'. 06ad8baf locks and calls 06ad8c00.
static unsigned int MovieCrtToUpper(unsigned int Byte)
{
    if (GMovieLocaleUseOs == 0)
    {
        if ((int)Byte >= 0x61 && (int)Byte <= 0x7A)
        {
            return Byte - 0x20u;
        }
        return Byte;
    }
    int Held = 0;
    InterlockedIncrement(&GMovieLocaleRefs);
    if (GMovieLocaleOwned != 0)
    {
        InterlockedDecrement(&GMovieLocaleRefs);
        MovieLock(0x13);
        Held = 1;
    }
    unsigned int Result = MovieCrtToUpperOs(Byte);
    if (Held != 0)
    {
        MovieUnlock(0x13);
    }
    else
    {
        InterlockedDecrement(&GMovieLocaleRefs);
    }
    return Result;
}

// FUN_06ad641e / FUN_06ad6435. The fourth argument pushed by 06ad641e is 0.
static int MovieCrtStrtol(const char* Text, char** End, int Base)
{
    unsigned int Flags = 0;
    const char* Cursor = Text;
    unsigned int Byte = (unsigned char)*Cursor;
    Cursor = Cursor + 1;
    while (MovieCtypeMask(Byte, 8) != 0)
    {
        Byte = (unsigned char)*Cursor;
        Cursor = Cursor + 1;
    }
    if (Byte == (unsigned int)'-')
    {
        Flags = Flags | 2u;
        Byte = (unsigned char)*Cursor;
        Cursor = Cursor + 1;
    }
    else if (Byte == (unsigned int)'+')
    {
        Byte = (unsigned char)*Cursor;
        Cursor = Cursor + 1;
    }
    if (Base < 0 || Base == 1 || Base > 0x24)
    {
        if (End != nullptr)
        {
            *End = (char*)Text;
        }
        return 0;
    }
    if (Base == 0)
    {
        if (Byte != (unsigned int)'0')
        {
            Base = 10;
        }
        else if (*Cursor != 'x' && *Cursor != 'X')
        {
            Base = 8;
        }
        else
        {
            Base = 16;
        }
    }
    if (Base == 16 && Byte == (unsigned int)'0' && (*Cursor == 'x' || *Cursor == 'X'))
    {
        Byte = (unsigned char)Cursor[1];
        Cursor = Cursor + 2;
    }
    unsigned int Limit = 0xFFFFFFFFu / (unsigned int)Base;
    unsigned int Value = 0;
    const char* Start = Text;
    for (;;)
    {
        unsigned int Digit;
        if (MovieCtypeMask(Byte, 4) != 0)
        {
            Digit = Byte - 0x30;
        }
        else if (MovieCtypeMask(Byte, 0x103) == 0)
        {
            break;
        }
        else
        {
            Digit = MovieCrtToUpper(Byte) - 0x37;
        }
        if ((unsigned int)Base <= Digit)
        {
            break;
        }
        Flags = Flags | 8u;
        if (Value < Limit || (Value == Limit && Digit <= (0xFFFFFFFFu % (unsigned int)Base)))
        {
            Value = Value * (unsigned int)Base + Digit;
        }
        else
        {
            Flags = Flags | 4u;
        }
        Byte = (unsigned char)*Cursor;
        Cursor = Cursor + 1;
    }
    Cursor = Cursor - 1;
    if ((Flags & 8u) == 0)
    {
        if (End != nullptr)
        {
            Cursor = Start;
        }
        Value = 0;
    }
    else if ((Flags & 4u) != 0 || ((Flags & 1u) == 0 && (((Flags & 2u) != 0 && Value > 0x80000000u) || ((Flags & 2u) == 0 && Value > 0x7FFFFFFFu))))
    {
        *MovieErrnoSlot() = 0x22;
        if ((Flags & 1u) != 0)
        {
            Value = 0xFFFFFFFFu;
        }
        else
        {
            Value = 0x7FFFFFFFu + ((Flags & 2u) != 0 ? 1u : 0u);
        }
    }
    if (End != nullptr)
    {
        *End = (char*)Cursor;
    }
    if ((Flags & 2u) != 0)
    {
        return -(int)Value;
    }
    return (int)Value;
}

struct FMovieSbhGroup
{
    uint32_t BitmapLo;
    uint32_t BitmapHi;
    uint32_t CommitMap;
    uint8_t* Region;
    uint8_t* Header;
};

static FMovieSbhGroup* GMovieSbhGroups = nullptr;
static int GMovieSbhCount = 0;
static int GMovieSbhCapacity = 0;
static FMovieSbhGroup* GMovieSbhRover = nullptr;
static FMovieSbhGroup* GMovieSbhDecommit = nullptr;
static int GMovieSbhDecommitRegion = 0;

static FMovieSbhGroup* MovieSbhGrow();
static int MovieSbhCommit(FMovieSbhGroup* Group);

static uint32_t MovieSbhMask(int Index)
{
    return 0x80000000u >> (Index & 0x1F);
}

static void MovieSbhClearBit(FMovieSbhGroup* Group, uint8_t* Header, int Region, int Bit)
{
    uint32_t Clear = ~MovieSbhMask(Bit);
    uint8_t* CountByte = Header + 4 + Bit;
    if (Bit < 0x20)
    {
        uint32_t* Map = (uint32_t*)(Header + 0x44 + Region * 4);
        *Map = *Map & Clear;
        *CountByte = (uint8_t)(*CountByte - 1);
        if (*CountByte == 0)
        {
            Group->BitmapLo = Group->BitmapLo & Clear;
        }
    }
    else
    {
        uint32_t* Map = (uint32_t*)(Header + 0xC4 + Region * 4);
        *Map = *Map & Clear;
        *CountByte = (uint8_t)(*CountByte - 1);
        if (*CountByte == 0)
        {
            Group->BitmapHi = Group->BitmapHi & Clear;
        }
    }
}

static void MovieSbhSetBit(FMovieSbhGroup* Group, uint8_t* Header, int Region, int Bit)
{
    uint32_t Set = MovieSbhMask(Bit);
    uint8_t* CountByte = Header + 4 + Bit;
    uint8_t Previous = *CountByte;
    *CountByte = (uint8_t)(Previous + 1);
    if (Bit < 0x20)
    {
        if (Previous == 0)
        {
            Group->BitmapLo = Group->BitmapLo | Set;
        }
        uint32_t* Map = (uint32_t*)(Header + 0x44 + Region * 4);
        *Map = *Map | Set;
    }
    else
    {
        if (Previous == 0)
        {
            Group->BitmapHi = Group->BitmapHi | Set;
        }
        uint32_t* Map = (uint32_t*)(Header + 0xC4 + Region * 4);
        *Map = *Map | Set;
    }
}

// FUN_06ad4091. Allocate the group table (0x140 bytes) and set the threshold.
static int MovieSbhInit(unsigned int Threshold)
{
    GMovieSbhGroups = (FMovieSbhGroup*)HeapAlloc(GMovieCrtHeap, 0, 0x140);
    if (GMovieSbhGroups == nullptr)
    {
        return 0;
    }
    GMovieSbhDecommit = nullptr;
    GMovieSbhCount = 0;
    GMovieSbhRover = GMovieSbhGroups;
    GMovieSbhThreshold = Threshold;
    GMovieSbhCapacity = 0x10;
    return 1;
}

// FUN_06ad4736. Grow the group vector and reserve a 1MB region.
static FMovieSbhGroup* MovieSbhGrow()
{
    if (GMovieSbhCount == GMovieSbhCapacity)
    {
        uint32_t Bytes = (uint32_t)((GMovieSbhCapacity * 5 + 0x50) * 4);
        void* Grown = HeapReAlloc(GMovieCrtHeap, 0, GMovieSbhGroups, Bytes);
        if (Grown == nullptr)
        {
            return nullptr;
        }
        GMovieSbhCapacity = GMovieSbhCapacity + 0x10;
        GMovieSbhGroups = (FMovieSbhGroup*)Grown;
    }
    FMovieSbhGroup* Group = GMovieSbhGroups + GMovieSbhCount;
    Group->Header = (uint8_t*)HeapAlloc(GMovieCrtHeap, 8, 0x41C4);
    if (Group->Header == nullptr)
    {
        return nullptr;
    }
    Group->Region = (uint8_t*)VirtualAlloc(nullptr, 0x100000, 0x2000, 4);
    if (Group->Region == nullptr)
    {
        HeapFree(GMovieCrtHeap, 0, Group->Header);
        return nullptr;
    }
    Group->CommitMap = 0xFFFFFFFFu;
    Group->BitmapLo = 0;
    Group->BitmapHi = 0;
    GMovieSbhCount = GMovieSbhCount + 1;
    *(int32_t*)Group->Header = -1;
    return Group;
}

// FUN_06ad47e7. Commit one 32KB page inside the reserved region.
static int MovieSbhCommit(FMovieSbhGroup* Group)
{
    uint8_t* Header = Group->Header;
    int Region = 0;
    int Map = (int)Group->CommitMap;
    for (; Map >= 0; Map = Map << 1)
    {
        Region = Region + 1;
    }
    uint8_t* Desc = Header + Region * 0x204 + 0x144;
    uint8_t* List = Desc;
    int Left = 0x3F;
    do
    {
        *(int32_t*)(List + 8) = (int32_t)List;
        *(int32_t*)(List + 4) = (int32_t)List;
        List = List + 8;
        Left = Left - 1;
    } while (Left != 0);

    uint8_t* Page = Group->Region + Region * 0x8000;
    if (VirtualAlloc(Page, 0x8000, 0x1000, 4) == nullptr)
    {
        return -1;
    }
    uint8_t* Cursor = Page + 0x10;
    uint8_t* Limit = Page + 0x7000;
    uint8_t* Next = Cursor;
    if (Page <= Limit)
    {
        do
        {
            *(int32_t*)(Cursor - 8) |= -1;
            *(int32_t*)(Cursor + 0xFEC) |= -1;
            *(int32_t*)(Cursor - 4) = 0xFF0;
            *(int32_t*)Cursor = (int32_t)(Cursor + 0xFFC);
            *(int32_t*)(Cursor + 4) = (int32_t)(Cursor - 0x1004);
            *(int32_t*)(Cursor + 0xFE8) = 0xFF0;
            Next = Cursor + 0xFF0;
            Cursor = Cursor + 0x1000;
            if (Next > Limit)
            {
                break;
            }
        } while (Next <= Limit);
    }
    *(int32_t*)(Desc + 0x1FC) = (int32_t)(Page + 0x0C);
    *(int32_t*)(Page + 0x14) = (int32_t)(Desc + 0x1F8);
    *(int32_t*)(Desc + 0x200) = (int32_t)(Page + 0x700C);
    *(int32_t*)(Page + 0x7010) = (int32_t)(Desc + 0x1F8);
    *(uint32_t*)(Header + 0x44 + Region * 4) = 0;
    *(uint32_t*)(Header + 0xC4 + Region * 4) = 1;
    uint8_t Count = *(Header + 0x43);
    *(Header + 0x43) = (uint8_t)(Count + 1);
    if (Count == 0)
    {
        Group->BitmapHi = Group->BitmapHi | 1;
    }
    Group->CommitMap = Group->CommitMap & ~MovieSbhMask(Region);
    return Region;
}

// FUN_06ad442d. V6 small-block allocate. Prologue checked at 06ad442d-06ad44a2.
static void* MovieSbhAlloc(unsigned int Size)
{
    FMovieSbhGroup* End = GMovieSbhGroups + GMovieSbhCount;
    uint32_t Rounded = (Size + 0x17u) & 0xFFFFFFF0u;
    int Index = (int)(Rounded >> 4) - 1;
    uint32_t MaskLo;
    uint32_t MaskHi;
    if (Index < 0x20)
    {
        MaskLo = 0xFFFFFFFFu >> (Index & 0x1F);
        MaskHi = 0xFFFFFFFFu;
    }
    else
    {
        MaskHi = 0xFFFFFFFFu >> ((Index - 0x20) & 0x1F);
        MaskLo = 0;
    }

    FMovieSbhGroup* Group = GMovieSbhRover;
    while (Group < End && (Group->BitmapHi & MaskHi) == 0 && (Group->BitmapLo & MaskLo) == 0)
    {
        Group = Group + 1;
    }
    if (Group == End)
    {
        FMovieSbhGroup* Rover = GMovieSbhRover;
        Group = GMovieSbhGroups;
        while (Group < Rover && (Group->BitmapHi & MaskHi) == 0 && (Group->BitmapLo & MaskLo) == 0)
        {
            Group = Group + 1;
        }
        if (Group == Rover)
        {
            Group = GMovieSbhGroups;
            while (Group < End && Group->CommitMap == 0)
            {
                Group = Group + 1;
            }
            if (Group == End)
            {
                Group = GMovieSbhGroups;
                while (Group < Rover && Group->CommitMap == 0)
                {
                    Group = Group + 1;
                }
                if (Group == Rover)
                {
                    Group = MovieSbhGrow();
                    if (Group == nullptr)
                    {
                        return nullptr;
                    }
                }
            }
            int Region = MovieSbhCommit(Group);
            *(int32_t*)Group->Header = Region;
            if (*(int32_t*)Group->Header == -1)
            {
                return nullptr;
            }
        }
    }

    uint8_t* Header = Group->Header;
    int Region = *(int32_t*)Header;
    uint32_t* LoMaps = (uint32_t*)(Header + 0x44);
    uint32_t* HiMaps = (uint32_t*)(Header + 0xC4);
    if (Region == -1 || ((HiMaps[Region] & MaskHi) == 0 && (LoMaps[Region] & MaskLo) == 0))
    {
        Region = 0;
        if ((HiMaps[0] & MaskHi) == 0 && (LoMaps[0] & MaskLo) == 0)
        {
            do
            {
                Region = Region + 1;
            } while ((HiMaps[Region] & MaskHi) == 0 && (LoMaps[Region] & MaskLo) == 0);
        }
    }

    int32_t* Desc = (int32_t*)(Header + 0x144 + Region * 0x204);
    int Bit = 0;
    uint32_t Bits = LoMaps[Region] & MaskLo;
    if (Bits == 0)
    {
        Bits = HiMaps[Region] & MaskHi;
        Bit = 0x20;
    }
    while ((int32_t)Bits >= 0)
    {
        Bits = Bits << 1;
        Bit = Bit + 1;
    }

    int32_t* Block = (int32_t*)Desc[Bit * 2 + 1];
    int Remain = *Block - (int)Rounded;
    int NewIndex = (Remain >> 4) - 1;
    if (NewIndex > 0x3F)
    {
        NewIndex = 0x3F;
    }
    GMovieSbhRover = Group;
    if (NewIndex != Bit)
    {
        if (Block[1] == Block[2])
        {
            MovieSbhClearBit(Group, Header, Region, Bit);
        }
        *(int32_t*)(Block[2] + 4) = Block[1];
        *(int32_t*)(Block[1] + 8) = Block[2];
        if (Remain != 0)
        {
            int32_t* Head = Desc + NewIndex * 2;
            Block[1] = Head[1];
            Block[2] = (int32_t)Head;
            Head[1] = (int32_t)Block;
            *(int32_t*)(Block[1] + 8) = (int32_t)Block;
            if (Block[1] == Block[2])
            {
                MovieSbhSetBit(Group, Header, Region, NewIndex);
            }
        }
    }
    if (Remain != 0)
    {
        *Block = Remain;
        *(int32_t*)((uint8_t*)Block + Remain - 4) = Remain;
    }
    int32_t* User = (int32_t*)((uint8_t*)Block + Remain);
    *User = (int32_t)Rounded + 1;
    *(int32_t*)((uint8_t*)User + Rounded - 4) = (int32_t)Rounded + 1;
    int32_t Count = *Desc;
    *Desc = Count + 1;
    if (Count == 0 && Group == GMovieSbhDecommit && Region == GMovieSbhDecommitRegion)
    {
        GMovieSbhDecommit = nullptr;
    }
    *(int32_t*)Header = Region;
    return User + 1;
}

// FUN_06ad40d9. Group whose 1MB region contains the pointer, or null.
static FMovieSbhGroup* MovieSbhFind(void* Block)
{
    FMovieSbhGroup* Group = GMovieSbhGroups;
    FMovieSbhGroup* End = GMovieSbhGroups + GMovieSbhCount;
    while (Group < End)
    {
        uint32_t Delta = (uint32_t)((uint8_t*)Block - Group->Region);
        if (Delta < 0x100000u)
        {
            return Group;
        }
        Group = Group + 1;
    }
    return nullptr;
}

// FUN_06ad4104. Return a V6 block to its size class. Body from the decompile.
static void MovieSbhFree(FMovieSbhGroup* Group, void* Block)
{
    uint8_t* Header = Group->Header;
    int32_t* Front = (int32_t*)((uint8_t*)Block - 4);
    int Region = (int)(((uint8_t*)Block - Group->Region) >> 15);
    int32_t* Desc = (int32_t*)(Header + Region * 0x204 + 0x144);
    uint32_t HeaderWord = (uint32_t)*Front;
    uint32_t Size = HeaderWord - 1;
    if ((Size & 1u) != 0)
    {
        return;
    }
    uint32_t NextWord = *(uint32_t*)((uint8_t*)Front + Size);
    uint32_t PrevWord = *(uint32_t*)((uint8_t*)Block - 8);
    int32_t* Chunk = Front;
    int NextIndex = 0;
    if ((NextWord & 1u) == 0)
    {
        int ClassIndex = ((int)NextWord >> 4) - 1;
        if (ClassIndex > 0x3F)
        {
            ClassIndex = 0x3F;
        }
        int32_t* NextBlock = (int32_t*)((uint8_t*)Front + Size);
        if (NextBlock[1] == NextBlock[2])
        {
            MovieSbhClearBit(Group, Header, Region, ClassIndex);
        }
        Size = Size + NextWord;
        *(int32_t*)(NextBlock[2] + 4) = NextBlock[1];
        *(int32_t*)(NextBlock[1] + 8) = NextBlock[2];
        NextIndex = ((int)Size >> 4) - 1;
    }
    else
    {
        NextIndex = ((int)Size >> 4) - 1;
    }
    if (NextIndex > 0x3F)
    {
        NextIndex = 0x3F;
    }
    int PrevIndex = NextIndex;
    if ((PrevWord & 1u) == 0)
    {
        Chunk = (int32_t*)((uint8_t*)Front - PrevWord);
        PrevIndex = ((int)PrevWord >> 4) - 1;
        if (PrevIndex > 0x3F)
        {
            PrevIndex = 0x3F;
        }
        Size = Size + PrevWord;
        NextIndex = ((int)Size >> 4) - 1;
        if (NextIndex > 0x3F)
        {
            NextIndex = 0x3F;
        }
        if (PrevIndex != NextIndex)
        {
            if (Chunk[1] == Chunk[2])
            {
                MovieSbhClearBit(Group, Header, Region, PrevIndex);
            }
            *(int32_t*)(Chunk[2] + 4) = Chunk[1];
            *(int32_t*)(Chunk[1] + 8) = Chunk[2];
        }
    }
    if (((PrevWord & 1u) != 0) || (PrevIndex != NextIndex))
    {
        int32_t* Head = Desc + NextIndex * 2;
        Chunk[1] = Head[1];
        Chunk[2] = (int32_t)Head;
        Head[1] = (int32_t)Chunk;
        *(int32_t*)(Chunk[1] + 8) = (int32_t)Chunk;
        if (Chunk[1] == Chunk[2])
        {
            MovieSbhSetBit(Group, Header, Region, NextIndex);
        }
    }
    *Chunk = (int32_t)Size;
    *(int32_t*)((uint8_t*)Chunk + Size - 4) = (int32_t)Size;
    *Desc = *Desc - 1;
    if (*Desc == 0)
    {
        if (GMovieSbhDecommit != nullptr)
        {
            VirtualFree(GMovieSbhDecommit->Region + GMovieSbhDecommitRegion * 0x8000, 0x8000, 0x4000);
            GMovieSbhDecommit->CommitMap = GMovieSbhDecommit->CommitMap | MovieSbhMask(GMovieSbhDecommitRegion);
            *(uint32_t*)(GMovieSbhDecommit->Header + 0xC4 + GMovieSbhDecommitRegion * 4) = 0;
            uint8_t HiCount = *(GMovieSbhDecommit->Header + 0x43);
            *(GMovieSbhDecommit->Header + 0x43) = (uint8_t)(HiCount - 1);
            if (*(GMovieSbhDecommit->Header + 0x43) == 0)
            {
                GMovieSbhDecommit->BitmapHi = GMovieSbhDecommit->BitmapHi & 0xFFFFFFFEu;
            }
            if (GMovieSbhDecommit->CommitMap == 0xFFFFFFFFu)
            {
                VirtualFree(GMovieSbhDecommit->Region, 0, 0x8000);
                HeapFree(GMovieCrtHeap, 0, GMovieSbhDecommit->Header);
                FMovieSbhGroup* Next = GMovieSbhDecommit + 1;
                int TailBytes = (int)((GMovieSbhGroups + GMovieSbhCount) - Next) * (int)sizeof(FMovieSbhGroup);
                if (TailBytes > 0)
                {
                    memmove(GMovieSbhDecommit, Next, (size_t)TailBytes);
                }
                GMovieSbhCount = GMovieSbhCount - 1;
                if (GMovieSbhDecommit < Group)
                {
                    Group = Group - 1;
                }
                GMovieSbhRover = GMovieSbhGroups;
            }
        }
        GMovieSbhDecommit = Group;
        GMovieSbhDecommitRegion = Region;
    }
}

// FUN_06ad2b38. Byte at NT headers + 0x1A (MajorLinkerVersion).
static void MovieReadLinkerVersion(uint8_t* Out)
{
    *(uint32_t*)Out = 0;
    uint8_t* Image = (uint8_t*)GetModuleHandleA(nullptr);
    if (*(uint16_t*)Image != 0x5A4D)
    {
        return;
    }
    int32_t LfaNew = *(int32_t*)(Image + 0x3C);
    if (LfaNew == 0)
    {
        return;
    }
    uint8_t* Nt = Image + LfaNew;
    Out[0] = Nt[0x1A];
    Out[1] = Nt[0x1B];
}

static void MovieUpperAscii(char* Text)
{
    while (*Text != 0)
    {
        if (*Text >= 'a' && *Text <= 'z')
        {
            *Text = (char)(*Text - 0x20);
        }
        Text = Text + 1;
    }
}

// FUN_06ad2b65. NT major >= 5 returns 1. Else the environment override, else 2 or 3.
static int MovieChooseHeapKind()
{
    OSVERSIONINFOEXA Version;
    memset(&Version, 0, sizeof(Version));
    Version.dwOSVersionInfoSize = 0x94;
#pragma warning(suppress: 4996)
    if (GetVersionExA((OSVERSIONINFOA*)&Version) != 0)
    {
        if (Version.dwPlatformId == 2 && Version.dwMajorVersion >= 5)
        {
            return 1;
        }
    }

    char Env[0x1090];
    char Module[0x104];
    DWORD Got = GetEnvironmentVariableA("__MSVCRT_HEAP_SELECT", Env, 0x1090);
    if (Got == 0)
    {
        uint8_t Linker[4];
        MovieReadLinkerVersion(Linker);
        if (Linker[0] < 6)
        {
            return 2;
        }
        return 3;
    }
    MovieUpperAscii(Env);
    char* Value = Env;
    if (strncmp(Env, "__GLOBAL_HEAP_SELECTED", 0x16) != 0)
    {
        memset(Module, 0, sizeof(Module));
        GetModuleFileNameA(nullptr, Module, 0x104);
        if (Module[0] != 0)
        {
            MovieUpperAscii(Module);
        }
        Value = strstr(Env, Module);
        if (Value == nullptr)
        {
            uint8_t Linker[4];
            MovieReadLinkerVersion(Linker);
            if (Linker[0] < 6)
            {
                return 2;
            }
            return 3;
        }
    }
    char* Comma = strchr(Value, ',');
    if (Comma == nullptr)
    {
        uint8_t Linker[4];
        MovieReadLinkerVersion(Linker);
        if (Linker[0] < 6)
        {
            return 2;
        }
        return 3;
    }
    char* Number = Comma + 1;
    char* Scan = Number;
    while (*Scan != 0)
    {
        if (*Scan == ';')
        {
            *Scan = 0;
            break;
        }
        Scan = Scan + 1;
    }
    int Kind = MovieCrtStrtol(Number, nullptr, 10);
    if (Kind == 1 || Kind == 2 || Kind == 3)
    {
        return Kind;
    }
    uint8_t Linker[4];
    MovieReadLinkerVersion(Linker);
    if (Linker[0] < 6)
    {
        return 2;
    }
    return 3;
}

struct FMovieOldSbhDesc
{
    FMovieOldSbhDesc* Next;
    FMovieOldSbhDesc* Prev;
    uint32_t* Rover;
    uint32_t* CommitCursor;
    uint8_t* Region;
    uint8_t* RegionLimit;
    uint32_t PageInfo[0x800];
    uint32_t PastEnd[2];
};

static FMovieOldSbhDesc GMovieOldSbhFirst;
static FMovieOldSbhDesc* GMovieOldSbhRover = nullptr;
static int GMovieOldSbhFullPages = 0;
static int GMovieOldSbhImageReady = 0;

static uint8_t* MovieOldSbhSentinelValue()
{
    return (uint8_t*)(uintptr_t)0xFFFFFFFFu;
}

// Image: next/prev of 06addc50 point at themselves, and the region dword is -1.
static void MovieOldSbhImage()
{
    if (GMovieOldSbhImageReady != 0)
    {
        return;
    }
    GMovieOldSbhFirst.Next = &GMovieOldSbhFirst;
    GMovieOldSbhFirst.Prev = &GMovieOldSbhFirst;
    GMovieOldSbhFirst.Region = MovieOldSbhSentinelValue();
    GMovieOldSbhFirst.RegionLimit = MovieOldSbhSentinelValue();
    GMovieOldSbhRover = &GMovieOldSbhFirst;
    GMovieOldSbhImageReady = 1;
}

// FUN_06ad4d1c. MEM_RELEASE the region. The static descriptor's region is set back to -1.
static void MovieOldSbhRelease(FMovieOldSbhDesc* Desc)
{
    VirtualFree(Desc->Region, 0, 0x8000);
    if (GMovieOldSbhRover == Desc)
    {
        GMovieOldSbhRover = Desc->Prev;
    }
    if (Desc != &GMovieOldSbhFirst)
    {
        Desc->Prev->Next = Desc->Next;
        Desc->Next->Prev = Desc->Prev;
        HeapFree(GMovieCrtHeap, 0, Desc);
        return;
    }
    Desc->Region = MovieOldSbhSentinelValue();
}

// FUN_06ad4d72. Decommit full 4KB pages, up to Limit of them, walking the descriptor list backward.
static void MovieOldSbhTrim(int Limit)
{
    MovieOldSbhImage();
    FMovieOldSbhDesc* Desc = GMovieOldSbhFirst.Prev;
    for (;;)
    {
        if (Desc->Region != MovieOldSbhSentinelValue())
        {
            int Freed = 0;
            uint32_t* Pair = (uint32_t*)((uint8_t*)Desc + 0x2010);
            int PageOff = 0x3FF000;
            for (;;)
            {
                if (Pair[0] == 0xF0u)
                {
                    if (VirtualFree(Desc->Region + PageOff, 0x1000, 0x4000) != 0)
                    {
                        Pair[0] = 0xFFFFFFFFu;
                        GMovieOldSbhFullPages = GMovieOldSbhFullPages - 1;
                        if (Desc->CommitCursor == nullptr || Desc->CommitCursor > Pair)
                        {
                            Desc->CommitCursor = Pair;
                        }
                        Freed = Freed + 1;
                        Limit = Limit - 1;
                        if (Limit == 0)
                        {
                            break;
                        }
                    }
                }
                PageOff = PageOff - 0x1000;
                Pair = Pair - 2;
                if (PageOff < 0)
                {
                    break;
                }
            }
            FMovieOldSbhDesc* Current = Desc;
            Desc = Desc->Prev;
            if (Freed != 0 && *(uint32_t*)((uint8_t*)Current + 0x18) == 0xFFFFFFFFu)
            {
                uint32_t* Scan = (uint32_t*)((uint8_t*)Current + 0x20);
                int Count = 1;
                while (Count < 0x400 && *Scan == 0xFFFFFFFFu)
                {
                    Count = Count + 1;
                    Scan = Scan + 2;
                }
                if (Count == 0x400)
                {
                    MovieOldSbhRelease(Current);
                }
            }
        }
        if (Desc == GMovieOldSbhFirst.Prev)
        {
            return;
        }
        if (Limit <= 0)
        {
            return;
        }
    }
}

// FUN_06ad4bd8. Reserve 4MB and commit the first 64KB. Region == -1 reuses the static descriptor.
static FMovieOldSbhDesc* MovieOldSbhInit()
{
    MovieOldSbhImage();
    FMovieOldSbhDesc* Desc = nullptr;
    if (GMovieOldSbhFirst.Region == MovieOldSbhSentinelValue())
    {
        Desc = &GMovieOldSbhFirst;
    }
    else
    {
        Desc = (FMovieOldSbhDesc*)HeapAlloc(GMovieCrtHeap, 0, 0x2020);
        if (Desc == nullptr)
        {
            return nullptr;
        }
    }
    uint8_t* Region = (uint8_t*)VirtualAlloc(nullptr, 0x400000, 0x2000, 4);
    if (Region == nullptr)
    {
        if (Desc != &GMovieOldSbhFirst)
        {
            HeapFree(GMovieCrtHeap, 0, Desc);
        }
        return nullptr;
    }
    if (VirtualAlloc(Region, 0x10000, 0x1000, 4) == nullptr)
    {
        VirtualFree(Region, 0, 0x8000);
        if (Desc != &GMovieOldSbhFirst)
        {
            HeapFree(GMovieCrtHeap, 0, Desc);
        }
        return nullptr;
    }
    if (Desc == &GMovieOldSbhFirst)
    {
        if (Desc->Next == nullptr)
        {
            Desc->Next = Desc;
        }
        if (Desc->Prev == nullptr)
        {
            Desc->Prev = Desc;
        }
    }
    else
    {
        Desc->Next = &GMovieOldSbhFirst;
        Desc->Prev = GMovieOldSbhFirst.Prev;
        GMovieOldSbhFirst.Prev = Desc;
        Desc->Prev->Next = Desc;
    }
    Desc->RegionLimit = Region + 0x400000;
    uint32_t* Pair = (uint32_t*)((uint8_t*)Desc + 0x18);
    Desc->CommitCursor = (uint32_t*)((uint8_t*)Desc + 0x98);
    Desc->Region = Region;
    Desc->Rover = Pair;
    int Index = 0;
    do
    {
        int AtLeast = 0;
        if (Index >= 0x10)
        {
            AtLeast = 1;
        }
        int Value = ((AtLeast - 1) & 0xF1) - 1;
        Pair[0] = (uint32_t)Value;
        Pair[1] = 0xF1;
        Pair = Pair + 2;
        Index = Index + 1;
    } while (Index < 0x400);
    memset(Region, 0, 0x10000);
    uint8_t* Page = Region;
    while (Page < Desc->Region + 0x10000)
    {
        Page[0xF8] = (uint8_t)(Page[0xF8] | 0xFF);
        *(uint8_t**)Page = Page + 8;
        *(uint32_t*)(Page + 4) = 0xF0;
        Page = Page + 0x1000;
    }
    return Desc;
}

// FUN_06ad4e34. Descriptor and page that own the pointer, plus the class byte inside the page.
static uint8_t* MovieOldSbhFind(void* Block, FMovieOldSbhDesc** OutDesc, uint8_t** OutPage)
{
    MovieOldSbhImage();
    FMovieOldSbhDesc* Start = &GMovieOldSbhFirst;
    FMovieOldSbhDesc* Desc = Start;
    for (;;)
    {
        if ((uint8_t*)Block > Desc->Region && (uint8_t*)Block < Desc->RegionLimit)
        {
            break;
        }
        Desc = Desc->Next;
        if (Desc == Start)
        {
            return nullptr;
        }
    }
    if (((uintptr_t)Block & 0xF) != 0)
    {
        return nullptr;
    }
    uintptr_t PageBits = (uintptr_t)Block & ~(uintptr_t)0xFFF;
    if (((uintptr_t)Block & 0xFFF) < 0x100)
    {
        return nullptr;
    }
    *OutDesc = Desc;
    *OutPage = (uint8_t*)PageBits;
    int Delta = (int)((uintptr_t)Block - PageBits) - 0x100;
    return (uint8_t*)PageBits + 8 + (Delta >> 4);
}

// FUN_06ad4e8b. Add the freed class back onto the page and trim after 0x20 full pages.
static void MovieOldSbhFree(FMovieOldSbhDesc* Desc, uint8_t* Page, uint8_t* Header)
{
    int Index = (int)((Page - Desc->Region) >> 12);
    uint32_t* Pair = (uint32_t*)((uint8_t*)Desc + 0x18) + (Index * 2);
    Pair[0] = Pair[0] + *Header;
    *Header = 0;
    Pair[1] = 0xF1;
    if (Pair[0] == 0xF0u)
    {
        GMovieOldSbhFullPages = GMovieOldSbhFullPages + 1;
        if (GMovieOldSbhFullPages == 0x20)
        {
            MovieOldSbhTrim(0x10);
        }
    }
}

// 06ad51eb. User pointer from the class byte: (header + 8) << 4 - page * 15.
static uint8_t* MovieOldSbhUser(uint8_t* Page, uint8_t* Header)
{
    uintptr_t Encoded = ((uintptr_t)(Header + 8) << 4) - ((uintptr_t)Page * 15u);
    return (uint8_t*)Encoded;
}

// FUN_06ad50d8. Carve Class bytes out of one committed page. 0 means this page cannot.
static uint8_t* MovieOldSbhCarve(uint8_t* Page, unsigned int FreeCount, unsigned int Class)
{
    unsigned int Run = *(uint32_t*)(Page + 4);
    uint8_t* Cursor = *(uint8_t**)Page;
    uint8_t* Sentinel = Page + 0xF8;
    uint8_t* Start = Cursor;
    if (Run >= Class)
    {
        uint8_t* Next = Cursor + Class;
        *Cursor = (uint8_t)Class;
        if (Next < Sentinel)
        {
            *(uint8_t**)Page = Next;
            *(uint32_t*)(Page + 4) = Run - Class;
        }
        else
        {
            *(uint32_t*)(Page + 4) = 0;
            *(uint8_t**)Page = Page + 8;
        }
        return MovieOldSbhUser(Page, Cursor);
    }
    uint8_t* At = Cursor;
    if (*(Cursor + Run) != 0)
    {
        At = Cursor + Run;
    }
    while (At + Class < Sentinel)
    {
        if (*At != 0)
        {
            At = At + *At;
            continue;
        }
        unsigned int Zeros = 1;
        uint8_t* Scan = At + 1;
        while (*Scan == 0)
        {
            Scan = Scan + 1;
            Zeros = Zeros + 1;
        }
        if (Zeros >= Class)
        {
            uint8_t* End = At + Class;
            if (End < Sentinel)
            {
                *(uint8_t**)Page = End;
                *(uint32_t*)(Page + 4) = Zeros - Class;
            }
            else
            {
                *(uint32_t*)(Page + 4) = 0;
                *(uint8_t**)Page = Page + 8;
            }
            *At = (uint8_t)Class;
            return MovieOldSbhUser(Page, At);
        }
        if (At == Start)
        {
            *(uint32_t*)(Page + 4) = Zeros;
        }
        else
        {
            FreeCount = FreeCount - Zeros;
            if (FreeCount < Class)
            {
                return nullptr;
            }
        }
        At = Scan;
    }
    uint8_t* Pos = Page + 8;
    while (Pos < Start)
    {
        if (Pos + Class >= Sentinel)
        {
            return nullptr;
        }
        if (*Pos != 0)
        {
            Pos = Pos + *Pos;
            continue;
        }
        unsigned int Zeros = 1;
        uint8_t* Scan = Pos + 1;
        while (*Scan == 0)
        {
            Scan = Scan + 1;
            Zeros = Zeros + 1;
        }
        if (Zeros >= Class)
        {
            uint8_t* End = Pos + Class;
            if (End < Sentinel)
            {
                *(uint8_t**)Page = End;
                *(uint32_t*)(Page + 4) = Zeros - Class;
            }
            else
            {
                *(uint32_t*)(Page + 4) = 0;
                *(uint8_t**)Page = Page + 8;
            }
            *Pos = (uint8_t)Class;
            return MovieOldSbhUser(Page, Pos);
        }
        FreeCount = FreeCount - Zeros;
        if (FreeCount < Class)
        {
            return nullptr;
        }
        Pos = Scan;
    }
    return nullptr;
}

// FUN_06ad4ed0. Class is the size already shifted right by 4. A miss is 0.
static uint8_t* MovieOldSbhAlloc(unsigned int Class)
{
    MovieOldSbhImage();
    FMovieOldSbhDesc* Start = GMovieOldSbhRover;
    FMovieOldSbhDesc* Desc = Start;
    for (;;)
    {
        if (Desc->Region != MovieOldSbhSentinelValue())
        {
            uint32_t* End = (uint32_t*)((uint8_t*)Desc + 0x2018);
            uint32_t* Pair = Desc->Rover;
            if ((uint8_t*)Pair < (uint8_t*)End)
            {
                int ByteOff = (int)((uint8_t*)Pair - (uint8_t*)Desc) - 0x18;
                uint8_t* Page = Desc->Region + ((ByteOff >> 3) << 12);
                while ((uint8_t*)Pair < (uint8_t*)End)
                {
                    if ((int)Pair[0] >= (int)Class && Pair[1] > Class)
                    {
                        uint8_t* Block = MovieOldSbhCarve(Page, Pair[0], Class);
                        if (Block != nullptr)
                        {
                            GMovieOldSbhRover = Desc;
                            Pair[0] = Pair[0] - Class;
                            Desc->Rover = Pair;
                            return Block;
                        }
                        Pair[1] = Class;
                    }
                    Pair = Pair + 2;
                    Page = Page + 0x1000;
                }
            }
            uint32_t* Stop = Desc->Rover;
            Pair = (uint32_t*)((uint8_t*)Desc + 0x18);
            uint8_t* Page = Desc->Region;
            while ((uint8_t*)Pair < (uint8_t*)Stop)
            {
                if ((int)Pair[0] >= (int)Class && Pair[1] > Class)
                {
                    uint8_t* Block = MovieOldSbhCarve(Page, Pair[0], Class);
                    if (Block != nullptr)
                    {
                        GMovieOldSbhRover = Desc;
                        Pair[0] = Pair[0] - Class;
                        Desc->Rover = Pair;
                        return Block;
                    }
                    Pair[1] = Class;
                }
                Page = Page + 0x1000;
                Pair = Pair + 2;
            }
        }
        Desc = Desc->Next;
        if (Desc == Start)
        {
            break;
        }
    }

    FMovieOldSbhDesc* First = &GMovieOldSbhFirst;
    Desc = First;
    for (;;)
    {
        if (Desc->Region != MovieOldSbhSentinelValue() && Desc->CommitCursor != nullptr)
        {
            break;
        }
        Desc = Desc->Next;
        if (Desc == First)
        {
            FMovieOldSbhDesc* Created = MovieOldSbhInit();
            if (Created == nullptr)
            {
                return nullptr;
            }
            uint8_t* Page = Created->Region;
            Page[8] = (uint8_t)Class;
            *(uint8_t**)Page = Page + Class + 8;
            *(uint32_t*)(Page + 4) = 0xF0u - Class;
            GMovieOldSbhRover = Created;
            uint32_t* FirstPair = (uint32_t*)((uint8_t*)Created + 0x18);
            FirstPair[0] = FirstPair[0] - (unsigned int)(uint8_t)Class;
            return Page + 0x100;
        }
    }

    uint32_t* Cursor = Desc->CommitCursor;
    int Count = 0;
    int ByteOff = (int)((uint8_t*)Cursor - (uint8_t*)Desc) - 0x18;
    uint8_t* Page = Desc->Region + ((ByteOff >> 3) << 12);
    uint32_t* Scan = Cursor;
    if (*Scan == 0xFFFFFFFFu)
    {
        while (Count < 0x10)
        {
            Scan = Scan + 2;
            Count = Count + 1;
            if (*Scan != 0xFFFFFFFFu)
            {
                break;
            }
        }
    }
    unsigned int Bytes = (unsigned int)Count << 12;
    if (VirtualAlloc(Page, Bytes, 0x1000, 4) != Page)
    {
        return nullptr;
    }
    memset(Page, 0, Bytes);
    uint32_t* Info = Cursor;
    if (Count > 0)
    {
        uint8_t* Header = Page + 4;
        int Left = Count;
        do
        {
            Header[0xF4] = (uint8_t)(Header[0xF4] | 0xFF);
            *(uint8_t**)(Header - 4) = Header + 4;
            *(uint32_t*)Header = 0xF0u;
            Info[0] = 0xF0u;
            Info[1] = 0xF1u;
            Header = Header + 0x1000;
            Info = Info + 2;
            Left = Left - 1;
        } while (Left != 0);
    }
    GMovieOldSbhRover = Desc;
    uint32_t* Limit = (uint32_t*)((uint8_t*)Desc + 0x2018);
    uint32_t* NextFree = Info;
    while ((uint8_t*)NextFree < (uint8_t*)Limit)
    {
        if (*NextFree == 0xFFFFFFFFu)
        {
            break;
        }
        NextFree = NextFree + 2;
    }
    if ((uint8_t*)NextFree < (uint8_t*)Limit)
    {
        Desc->CommitCursor = NextFree;
    }
    else
    {
        Desc->CommitCursor = nullptr;
    }
    Page[8] = (uint8_t)Class;
    Desc->Rover = Cursor;
    Cursor[0] = Cursor[0] - Class;
    *(uint32_t*)(Page + 4) = *(uint32_t*)(Page + 4) - Class;
    *(uint8_t**)Page = Page + Class + 8;
    return Page + 0x100;
}

// FUN_06ad2cad. HeapCreate, then the small-block init when the kind is 2 or 3.
static int MovieHeapInit(int SerializeFlag)
{
    DWORD Options = 0;
    if (SerializeFlag == 0)
    {
        Options = 1;
    }
    HANDLE Heap = HeapCreate(Options, 0x1000, 0);
    GMovieCrtHeap = Heap;
    if (Heap == nullptr)
    {
        return 0;
    }
    int Kind = MovieChooseHeapKind();
    GMovieHeapKind = Kind;
    if (Kind == 3)
    {
        if (MovieSbhInit(0x3F8) == 0)
        {
            HeapDestroy(GMovieCrtHeap);
            return 0;
        }
    }
    else if (Kind == 2)
    {
        if (MovieOldSbhInit() == nullptr)
        {
            HeapDestroy(GMovieCrtHeap);
            return 0;
        }
    }
    return 1;
}

// FUN_06ad2d0a. Kind 3 releases each region and the group table. Kind 2 walks the descriptor ring. Then HeapDestroy.
static void MovieHeapShutdown()
{
    if (GMovieHeapKind == 3)
    {
        if (GMovieSbhCount > 0)
        {
            int Index = 0;
            while (Index < GMovieSbhCount)
            {
                FMovieSbhGroup* Group = GMovieSbhGroups + Index;
                VirtualFree(Group->Region, 0x100000, 0x4000);
                VirtualFree(Group->Region, 0, 0x8000);
                HeapFree(GMovieCrtHeap, 0, Group->Header);
                Index = Index + 1;
            }
        }
        HeapFree(GMovieCrtHeap, 0, GMovieSbhGroups);
    }
    else if (GMovieHeapKind == 2)
    {
        MovieOldSbhImage();
        FMovieOldSbhDesc* Head = &GMovieOldSbhFirst;
        FMovieOldSbhDesc* Cursor = Head;
        do
        {
            if (Cursor->Region != nullptr)
            {
                VirtualFree(Cursor->Region, 0, 0x8000);
            }
            Cursor = Cursor->Next;
        }
        while (Cursor != Head);
    }
    HeapDestroy(GMovieCrtHeap);
}

static unsigned int MovieRoundHeapSize(unsigned int Size)
{
    if (Size == 0)
    {
        Size = 1;
    }
    Size = Size + 0x0Fu;
    Size = (Size & 0xFFFFFF00u) | (Size & 0xF0u);
    return Size;
}

// FUN_06ad0991. Mode 1 (NT >= 5) is HeapAlloc. Mode 3 uses the V6 heap.
static void* MovieHeapAlloc(unsigned int Size)
{
    if (GMovieHeapKind == 3)
    {
        if (Size <= GMovieSbhThreshold)
        {
            MovieLock(9);
            void* Block = nullptr;
            __try
            {
                Block = MovieSbhAlloc(Size);
            }
            __finally
            {
                MovieUnlock(9);
            }
            if (Block != nullptr)
            {
                return Block;
            }
        }
    }
    else if (GMovieHeapKind == 2)
    {
        unsigned int Rounded = 0x10;
        if (Size != 0)
        {
            Rounded = (Size + 0x0Fu) & 0xFFFFFFF0u;
        }
        if (Rounded <= 0x1E0u)
        {
            MovieLock(9);
            void* Block = nullptr;
            __try
            {
                Block = MovieOldSbhAlloc(Rounded >> 4);
            }
            __finally
            {
                MovieUnlock(9);
            }
            if (Block != nullptr)
            {
                return Block;
            }
        }
        return HeapAlloc(GMovieCrtHeap, 0, Rounded);
    }

    Size = MovieRoundHeapSize(Size);
    return HeapAlloc(GMovieCrtHeap, 0, Size);
}

// FUN_06ad5414. calloc. Kind 3 and kind 2 are zeroed after the small-block alloc; the heap path uses flags 8.
static void* MovieCrtCalloc(unsigned int Count, unsigned int Size)
{
    unsigned int Product = Count * Size;
    unsigned int Ask = Product;
    if (Product <= 0xFFFFFFE0u)
    {
        if (Ask == 0)
        {
            Ask = 1;
        }
        Ask = (Ask + 0x0Fu) & 0xFFFFFFF0u;
    }
    for (;;)
    {
        void* Block = nullptr;
        if (Ask <= 0xFFFFFFE0u)
        {
            if (GMovieHeapKind == 3 && Product <= GMovieSbhThreshold)
            {
                MovieLock(9);
                __try
                {
                    Block = MovieSbhAlloc(Product);
                }
                __finally
                {
                    MovieUnlock(9);
                }
                if (Block != nullptr)
                {
                    memset(Block, 0, Product);
                }
            }
            else if (GMovieHeapKind == 2 && Ask <= 0x1E0u)
            {
                MovieLock(9);
                __try
                {
                    Block = MovieOldSbhAlloc(Ask >> 4);
                }
                __finally
                {
                    MovieUnlock(9);
                }
                if (Block != nullptr)
                {
                    memset(Block, 0, Ask);
                }
            }
            if (Block == nullptr)
            {
                Block = HeapAlloc(GMovieCrtHeap, 8, Ask);
            }
        }
        if (Block != nullptr || GMovieNewMode == 0)
        {
            return Block;
        }
        if (MovieCallNewHandler(Ask) == 0)
        {
            return nullptr;
        }
    }
}

// 06ad0965 __nh_malloc. Does not throw.
static void* MovieNhMalloc(unsigned int Size, int NhFlag)
{
    if (Size > 0xFFFFFFE0u)
    {
        return nullptr;
    }
    for (;;)
    {
        void* Block = MovieHeapAlloc(Size);
        if (Block != nullptr || NhFlag == 0)
        {
            return Block;
        }
        if (MovieCallNewHandler(Size) == 0)
        {
            return nullptr;
        }
    }
}

// FUN_06ad07b3. Mode 1 is HeapFree. A null pointer returns.
static void MovieHeapFree(void* Block)
{
    if (Block == nullptr)
    {
        return;
    }
    if (GMovieHeapKind == 3)
    {
        MovieLock(9);
        FMovieSbhGroup* Group = nullptr;
        __try
        {
            Group = MovieSbhFind(Block);
            if (Group != nullptr)
            {
                MovieSbhFree(Group, Block);
            }
        }
        __finally
        {
            MovieUnlock(9);
        }
        if (Group != nullptr)
        {
            return;
        }
    }
    else if (GMovieHeapKind == 2)
    {
        MovieLock(9);
        uint8_t* Header = nullptr;
        __try
        {
            FMovieOldSbhDesc* Desc = nullptr;
            uint8_t* Page = nullptr;
            Header = MovieOldSbhFind(Block, &Desc, &Page);
            if (Header != nullptr)
            {
                MovieOldSbhFree(Desc, Page, Header);
            }
        }
        __finally
        {
            MovieUnlock(9);
        }
        if (Header != nullptr)
        {
            return;
        }
    }
    HeapFree(GMovieCrtHeap, 0, Block);
}

struct FMovieRuntimeError
{
    uint32_t Code;
    const char* Text;
};

static const FMovieRuntimeError GMovieRuntimeErrors[] = {
    { 0x02, "R6002\r\n- floating point not loaded\r\n" },
    { 0x08, "R6008\r\n- not enough space for arguments\r\n" },
    { 0x09, "R6009\r\n- not enough space for environment\r\n" },
    { 0x0A, "\r\nabnormal program termination\r\n" },
    { 0x10, "R6016\r\n- not enough space for thread data\r\n" },
    { 0x11, "R6017\r\n- unexpected multithread lock error\r\n" },
    { 0x12, "R6018\r\n- unexpected heap error\r\n" },
    { 0x13, "R6019\r\n- unable to open console device\r\n" },
    { 0x18, "R6024\r\n- not enough space for _onexit/atexit table\r\n" },
    { 0x19, "R6025\r\n- pure virtual function call\r\n" },
    { 0x1A, "R6026\r\n- not enough space for stdio initialization\r\n" },
    { 0x1B, "R6027\r\n- not enough space for lowio initialization\r\n" },
    { 0x1C, "R6028\r\n- unable to initialize heap\r\n" },
    { 0x78, "DOMAIN error\r\n" },
    { 0x79, "SING error\r\n" },
    { 0x7A, "TLOSS error\r\n" },
    { 0xFC, "\r\n" },
    { 0xFF, "runtime error " }
};

// FUN_06ad6790. Load user32 MessageBoxA / GetActiveWindow / GetLastActivePopup.
static int MovieRuntimeMessageBox(const char* Text, const char* Caption, unsigned int Flags)
{
    static int (WINAPI* GMessageBoxA)(HWND, LPCSTR, LPCSTR, UINT) = nullptr;
    static HWND (WINAPI* GGetActiveWindow)() = nullptr;
    static HWND (WINAPI* GGetLastActivePopup)(HWND) = nullptr;
    if (GMessageBoxA == nullptr)
    {
        HMODULE User = LoadLibraryA("user32.dll");
        if (User == nullptr)
        {
            return 0;
        }
        GMessageBoxA = (int (WINAPI*)(HWND, LPCSTR, LPCSTR, UINT))GetProcAddress(User, "MessageBoxA");
        if (GMessageBoxA == nullptr)
        {
            return 0;
        }
        GGetActiveWindow = (HWND (WINAPI*)())GetProcAddress(User, "GetActiveWindow");
        GGetLastActivePopup = (HWND (WINAPI*)(HWND))GetProcAddress(User, "GetLastActivePopup");
    }
    HWND Owner = nullptr;
    if (GGetActiveWindow != nullptr)
    {
        Owner = GGetActiveWindow();
        if (Owner != nullptr && GGetLastActivePopup != nullptr)
        {
            Owner = GGetLastActivePopup(Owner);
        }
    }
    return GMessageBoxA(Owner, Text, Caption, Flags);
}

// FUN_06ad2df1. MessageBox, or WriteFile when the error mode asks for the console.
static void MovieRuntimeError(unsigned int Code)
{
    int Count = (int)(sizeof(GMovieRuntimeErrors) / sizeof(GMovieRuntimeErrors[0]));
    int Index = 0;
    while (Index < Count && GMovieRuntimeErrors[Index].Code != Code)
    {
        Index = Index + 1;
    }
    if (Index >= Count)
    {
        return;
    }
    const char* Text = GMovieRuntimeErrors[Index].Text;
    if (GMovieErrorMode == 1 || (GMovieErrorMode == 0 && GMovieAppType == 1))
    {
        DWORD Written = 0;
        HANDLE Err = GetStdHandle((DWORD)-12);
        WriteFile(Err, Text, (DWORD)strlen(Text), &Written, nullptr);
        return;
    }
    if (Code == 0xFCu)
    {
        return;
    }
    char Name[0x104];
    if (GetModuleFileNameA(nullptr, Name, 0x104) == 0)
    {
        strcpy(Name, "<program name unknown>");
    }
    if (strlen(Name) + 1u > 0x3Cu)
    {
        char* Tail = Name + strlen(Name) - 0x3B;
        strncpy(Tail, "...", 3);
    }
    char Message[0xA4];
    strcpy(Message, "Runtime Error!\n\nProgram: ");
    strcat(Message, Name);
    strcat(Message, "\n\n");
    strcat(Message, Text);
    MovieRuntimeMessageBox(Message, "Microsoft Visual C++ Runtime Library", 0x12010);
}

// FUN_06ad2db8. Extra report before the numbered message. Not taken while both
// mode globals stay 0.
static void MovieRuntimeErrorBanner()
{
    if (GMovieErrorMode == 1 || (GMovieErrorMode == 0 && GMovieAppType == 1))
    {
        MovieRuntimeError(0xFC);
        if (GMovieRuntimeHook != nullptr)
        {
            GMovieRuntimeHook();
        }
        MovieRuntimeError(0xFF);
    }
}

// 06ad532e. SetUnhandledExceptionFilter(saved).
static void MovieRestoreUnhandledFilter()
{
    SetUnhandledExceptionFilter(GMovieSavedFilter);
}

// 06ad890c when DAT_06ae2440 <= 0: lock 2, unlock 2. The FILE walk is not taken.
static void MovieFlushStreams()
{
    MovieLock(2);
    if (GMovieStreamCount > 0)
    {
        MovieUnlock(2);
        return;
    }
    MovieUnlock(2);
}

// 06ad5c72. Called from the slow exit range, not from __exit (param_2 != 0).
static void MovieCrtTermIo()
{
    MovieFlushStreams();
    if (GMovieExitQuick == 0)
    {
        return;
    }
}

static void MovieCallInitterm(void (__cdecl** Begin)(), void (__cdecl** End)())
{
    void (__cdecl** Cursor)() = Begin;
    while (Cursor < End)
    {
        if (*Cursor != nullptr)
        {
            (*Cursor)();
        }
        Cursor = Cursor + 1;
    }
}

static void (__cdecl* GMoviePreTerms[2])() = { nullptr, MovieCrtTermIo };
static void (__cdecl* GMovieTerms[2])() = { nullptr, MovieRestoreUnhandledFilter };

// FUN_06ad14d5. __exit passes (code, 1, 0): skip the onexit walk, run the
// second terminator, ExitProcess.
static void MovieDoExit(unsigned int Code, int SkipOnexit, int QuickFlag)
{
    MovieLockOnexit();
    if (GMovieExitInProgress == 1)
    {
        TerminateProcess(GetCurrentProcess(), Code);
    }
    GMovieExitMarked = 1;
    GMovieExitQuick = (uint8_t)QuickFlag;
    if (SkipOnexit == 0)
    {
        if (GMovieOnexitBegin != nullptr)
        {
            void (__cdecl** Cursor)() = GMovieOnexitEnd - 1;
            if (Cursor >= GMovieOnexitBegin)
            {
                for (;;)
                {
                    if (*Cursor != nullptr)
                    {
                        (*Cursor)();
                    }
                    if (Cursor <= GMovieOnexitBegin)
                    {
                        break;
                    }
                    Cursor = Cursor - 1;
                }
            }
        }
        MovieCallInitterm(&GMoviePreTerms[0], &GMoviePreTerms[2]);
    }
    MovieCallInitterm(&GMovieTerms[0], &GMovieTerms[2]);
    if (QuickFlag != 0)
    {
        MovieUnlockOnexit();
        return;
    }
    GMovieExitInProgress = 1;
    ExitProcess(Code);
}

// 06ad14b5 __exit.
static void MovieExit(int Code)
{
    MovieDoExit((unsigned int)Code, 1, 0);
}

struct FMovieXcptAct
{
    uint32_t Code;
    uint32_t Signal;
    uint32_t Handler;
};

// DAT_06adfc80. Ten records, then the separate counts at 06adfcf8 / 06adfcfc / 06adfd04.
static FMovieXcptAct GMovieXcptAct[0x0A] = {
    {0xC0000005u, 0x0Bu, 0},
    {0xC000001Du, 0x04u, 0},
    {0xC0000096u, 0x04u, 0},
    {0xC000008Du, 0x08u, 0},
    {0xC000008Eu, 0x08u, 0},
    {0xC000008Fu, 0x08u, 0},
    {0xC0000090u, 0x08u, 0},
    {0xC0000091u, 0x08u, 0},
    {0xC0000092u, 0x08u, 0},
    {0xC0000093u, 0x08u, 0},
};
static int GMovieFpeFirst = 3;
static int GMovieFpeSpan = 7;
static int GMovieXcptCount = 0x0A;
static void (__cdecl* GMovieSigInt)(int) = nullptr;
static void (__cdecl* GMovieSigBreak)(int) = nullptr;
static void (__cdecl* GMovieSigAbrt)(int) = nullptr;
static void (__cdecl* GMovieSigTerm)(int) = nullptr;

// FUN_06ad136e. The new per-thread block points at the exception-action table.
static void MovieInitPtd(uint8_t* Ptd)
{
    *(uint32_t*)(Ptd + 0x50) = (uint32_t)(uintptr_t)GMovieXcptAct;
    *(uint32_t*)(Ptd + 0x14) = 1;
}

// FUN_06ad1381. TlsGetValue of DAT_06add570. A miss allocates 0x74 bytes and stores the thread id.
static uint8_t* MovieGetPtd()
{
    DWORD Saved = GetLastError();
    uint8_t* Ptd = (uint8_t*)TlsGetValue(GMovieTlsIndex);
    if (Ptd == nullptr)
    {
        Ptd = (uint8_t*)MovieCrtCalloc(1, 0x74);
        if (Ptd == nullptr || TlsSetValue(GMovieTlsIndex, Ptd) == 0)
        {
            MovieAmsgExit(0x10);
        }
        else
        {
            MovieInitPtd(Ptd);
            *(uint32_t*)(Ptd + 4) = *(uint32_t*)(Ptd + 4) | 0xFFFFFFFFu;
            *(uint32_t*)Ptd = GetCurrentThreadId();
        }
    }
    SetLastError(Saved);
    return Ptd;
}

// FUN_06ad12fc. TlsAlloc, then the first per-thread block. 0 if either step fails.
static int MovieInitThreadData()
{
    MovieInitStaticLocks();
    DWORD Index = TlsAlloc();
    GMovieTlsIndex = Index;
    if (Index == 0xFFFFFFFFu)
    {
        return 0;
    }
    uint8_t* Ptd = (uint8_t*)MovieCrtCalloc(1, 0x74);
    if (Ptd == nullptr)
    {
        return 0;
    }
    if (TlsSetValue(GMovieTlsIndex, Ptd) == 0)
    {
        return 0;
    }
    MovieInitPtd(Ptd);
    *(uint32_t*)(Ptd + 4) = *(uint32_t*)(Ptd + 4) | 0xFFFFFFFFu;
    *(uint32_t*)Ptd = GetCurrentThreadId();
    return 1;
}

// FUN_06ad24de. errno lives at ptd + 8.
static int* MovieErrnoSlot()
{
    return (int*)(MovieGetPtd() + 8);
}

// FUN_06ad79e5. 12-byte records; the signal number is at +4. A miss is 0.
static uint8_t* MovieFindXcpt(int Signal, uint8_t* Table)
{
    int Count = GMovieXcptCount;
    uint8_t* End = Table + (Count + Count * 2) * 4;
    uint8_t* At = Table;
    if (*(uint32_t*)(Table + 4) != (uint32_t)Signal)
    {
        for (;;)
        {
            At = At + 0xC;
            if (At >= End)
            {
                break;
            }
            if (*(uint32_t*)(At + 4) == (uint32_t)Signal)
            {
                break;
            }
        }
    }
    if (At < End && *(uint32_t*)(At + 4) == (uint32_t)Signal)
    {
        return At;
    }
    return nullptr;
}

// FUN_06ad7863. _raise. 0x16 reads DAT_06ae1118. 0 is SIG_DFL and calls __exit(3). 1 is SIG_IGN.
static int MovieRaise(int Signal)
{
    void (__cdecl* Handler)(int) = nullptr;
    void (__cdecl** Slot)(int) = nullptr;
    int Locked = 0;
    uint8_t* Ptd = nullptr;
    int Sel = Signal - 2;
    if (Sel == 0)
    {
        Handler = GMovieSigInt;
        Slot = &GMovieSigInt;
    }
    else
    {
        Sel = Signal - 4;
        if (Sel == 0 || Signal == 8 || Signal == 11)
        {
            Ptd = MovieGetPtd();
            uint8_t* Found = MovieFindXcpt(Signal, *(uint8_t**)(Ptd + 0x50));
            Slot = (void (__cdecl**)(int))(Found + 8);
            Handler = *Slot;
        }
        else if (Signal == 15)
        {
            Handler = GMovieSigTerm;
            Slot = &GMovieSigTerm;
        }
        else if (Signal == 21)
        {
            Handler = GMovieSigBreak;
            Slot = &GMovieSigBreak;
        }
        else if (Signal == 22)
        {
            Handler = GMovieSigAbrt;
            Slot = &GMovieSigAbrt;
        }
        else
        {
            return -1;
        }
    }
    if (Signal != 4 && Signal != 8 && Signal != 11)
    {
        Locked = 1;
        MovieLock(1);
    }
    if (Handler == (void (__cdecl*)(int))(uintptr_t)1)
    {
        if (Locked != 0)
        {
            MovieUnlock(1);
        }
        return 0;
    }
    if (Handler == nullptr)
    {
        if (Locked != 0)
        {
            MovieUnlock(1);
        }
        MovieExit(3);
        return 0;
    }
    int Saved54 = 0;
    int Saved58 = 0;
    int ClearSlot = 1;
    if (Signal == 4 || Signal == 8 || Signal == 11)
    {
        Saved54 = *(int*)(Ptd + 0x54);
        *(uint32_t*)(Ptd + 0x54) = 0;
        if (Signal == 8)
        {
            Saved58 = *(int*)(Ptd + 0x58);
            *(uint32_t*)(Ptd + 0x58) = 0x8C;
            int Offset = GMovieFpeFirst * 12;
            int Index = GMovieFpeFirst;
            int Limit = GMovieFpeFirst + GMovieFpeSpan;
            if (Index < Limit)
            {
                uint8_t* Table = *(uint8_t**)(Ptd + 0x50);
                do
                {
                    Offset = Offset + 0xC;
                    *(uint32_t*)(Table + Offset - 4) = 0;
                    Index = Index + 1;
                    Limit = GMovieFpeFirst + GMovieFpeSpan;
                } while (Index < Limit);
            }
            ClearSlot = 0;
        }
    }
    if (ClearSlot != 0)
    {
        *Slot = nullptr;
    }
    if (Locked != 0)
    {
        MovieUnlock(1);
    }
    if (Signal == 8)
    {
        typedef void (__cdecl* FMovieFpeHandler)(int, int);
        FMovieFpeHandler Fpe = (FMovieFpeHandler)Handler;
        Fpe(8, *(int*)(Ptd + 0x58));
    }
    else
    {
        Handler(Signal);
    }
    if (Signal == 8 || Signal == 11 || Signal == 4)
    {
        *(int*)(Ptd + 0x54) = Saved54;
        if (Signal == 8)
        {
            *(int*)(Ptd + 0x58) = Saved58;
        }
    }
    return 0;
}

// FUN_06ad089c. Frame 06ad089f-06ad08b8. Filter 06ad08e4 returns 1; 06ad08e8 restores ESP and falls into 06ad08ef.
// Level 0's handler is 06ad08f8, which jumps to FUN_06ad52a5.
static void MovieCppTerminate()
{
    __try
    {
        uint8_t* Ptd = MovieGetPtd();
        if (*(uint32_t*)(Ptd + 0x60) != 0)
        {
            __try
            {
                Ptd = MovieGetPtd();
                void (__cdecl* Hook)() = (void (__cdecl*)())*(uint32_t*)(Ptd + 0x60);
                Hook();
            }
            __except (1)
            {
            }
        }
    }
    __finally
    {
        MovieRuntimeError(0x0A);
        MovieRaise(0x16);
        MovieExit(3);
    }
}

// FUN_06ad5373. 1 when IsBadCodePtr says the pointer is code.
static int MovieFilterIsCode(void* Pointer)
{
    if (IsBadCodePtr((FARPROC)Pointer) != 0)
    {
        return 0;
    }
    return 1;
}

// FUN_06ad52d7. C++ exception 0xE06D7363 / 0x19930520 jumps to 06ad089c.
static LONG WINAPI MovieUnhandledFilter(EXCEPTION_POINTERS* Pointers)
{
    EXCEPTION_RECORD* Record = Pointers->ExceptionRecord;
    if (Record->ExceptionCode == 0xE06D7363
        && Record->NumberParameters == 3
        && Record->ExceptionInformation[0] == 0x19930520)
    {
        MovieCppTerminate();
        return 0;
    }
    if (GMovieSavedFilter != nullptr && MovieFilterIsCode((void*)GMovieSavedFilter) != 0)
    {
        return GMovieSavedFilter(Pointers);
    }
    return 0;
}

// LAB_06ad531d. Install the filter and keep whatever was there before.
// .CRT$XI initializers are int(*)() and a nonzero return aborts DLL load.
static int __cdecl MovieInstallExceptionFilter()
{
    GMovieSavedFilter = SetUnhandledExceptionFilter(MovieUnhandledFilter);
    return 0;
}

// 06acff18 __amsg_exit.
static void MovieAmsgExit(int RuntimeError)
{
    if (GMovieErrorMode == 1 || (GMovieErrorMode == 0 && GMovieAppType == 1))
    {
        MovieRuntimeErrorBanner();
    }
    MovieRuntimeError((unsigned int)RuntimeError);
    MovieExit(0xFF);
}

// LAB_06acf888. malloc(0x80). Failure is __amsg_exit(0x18).
// .CRT$XI initializers are int(*)(). The pointer stored in GMovieOnexitEnd is nonzero,
// and the UCRT treats that leftover as a failed initializer.
static int __cdecl MovieInitOnexitTable()
{
    MovieInitStaticLocks();
    if (GMovieCrtHeap == nullptr)
    {
        MovieHeapInit(1);
    }
    void* Block = MovieNhMalloc(0x80, 0);
    GMovieOnexitBegin = (void (__cdecl**)())Block;
    if (Block == nullptr)
    {
        MovieAmsgExit(0x18);
    }
    GMovieOnexitBegin[0] = nullptr;
    GMovieOnexitEnd = GMovieOnexitBegin;
    return 0;
}

#pragma section(".CRT$XIC", long, read)
extern "C" __declspec(allocate(".CRT$XIC")) int (__cdecl* GMovieOnexitCrtInit)() = MovieInitOnexitTable;
#pragma comment(linker, "/include:_GMovieOnexitCrtInit")

#pragma section(".CRT$XID", long, read)
extern "C" __declspec(allocate(".CRT$XID")) int (__cdecl* GMovieFilterCrtInit)() = MovieInstallExceptionFilter;
#pragma comment(linker, "/include:_GMovieFilterCrtInit")

static unsigned int MovieMsize(void* Block)
{
    if (Block == nullptr || GMovieCrtHeap == nullptr)
    {
        return 0;
    }
    return (unsigned int)HeapSize(GMovieCrtHeap, 0, Block);
}

// FUN_06ad48e2. Grow or shrink a V6 block in place. 0 means the block was not resized.
static int MovieSbhResize(FMovieSbhGroup* Group, void* Block, unsigned int Size)
{
    uint8_t* Header = Group->Header;
    unsigned int Rounded = (Size + 0x17u) & 0xFFFFFFF0u;
    int Region = (int)(((uint8_t*)Block - Group->Region) >> 15);
    int32_t* Desc = (int32_t*)(Header + Region * 0x204 + 0x144);
    int32_t* Front = (int32_t*)((uint8_t*)Block - 4);
    uint32_t Have = (uint32_t)*Front - 1u;
    uint32_t NextWord = *(uint32_t*)((uint8_t*)Front + Have);
    int32_t* NextBlock = (int32_t*)((uint8_t*)Front + Have);
    if ((int)Have < (int)Rounded)
    {
        if ((NextWord & 1u) != 0 || (int)(NextWord + Have) < (int)Rounded)
        {
            return 0;
        }
        int ClassIndex = ((int)NextWord >> 4) - 1;
        if (ClassIndex > 0x3F)
        {
            ClassIndex = 0x3F;
        }
        if (NextBlock[1] == NextBlock[2])
        {
            MovieSbhClearBit(Group, Header, Region, ClassIndex);
        }
        *(int32_t*)(NextBlock[2] + 4) = NextBlock[1];
        *(int32_t*)(NextBlock[1] + 8) = NextBlock[2];
        int Remain = (int)(NextWord + Have - Rounded);
        if (Remain > 0)
        {
            int NewIndex = (Remain >> 4) - 1;
            int32_t* Piece = (int32_t*)((uint8_t*)Block - 4 + Rounded);
            if (NewIndex > 0x3F)
            {
                NewIndex = 0x3F;
            }
            int32_t* Head = Desc + NewIndex * 2;
            Piece[1] = Head[1];
            Piece[2] = (int32_t)Head;
            Head[1] = (int32_t)Piece;
            *(int32_t*)(Piece[1] + 8) = (int32_t)Piece;
            if (Piece[1] == Piece[2])
            {
                MovieSbhSetBit(Group, Header, Region, NewIndex);
            }
            *Piece = Remain;
            *(int32_t*)((uint8_t*)Piece + Remain - 4) = Remain;
        }
        *(uint32_t*)((uint8_t*)Block - 4) = Rounded + 1u;
        *(uint32_t*)((uint8_t*)Block + Rounded - 8) = Rounded + 1u;
    }
    else if ((int)Rounded < (int)Have)
    {
        int Remain = (int)Have - (int)Rounded;
        *(uint32_t*)((uint8_t*)Block - 4) = Rounded + 1u;
        int32_t* Piece = (int32_t*)((uint8_t*)Block - 4 + Rounded);
        int NewIndex = (Remain >> 4) - 1;
        *(uint32_t*)((uint8_t*)Piece - 4) = Rounded + 1u;
        if (NewIndex > 0x3F)
        {
            NewIndex = 0x3F;
        }
        if ((NextWord & 1u) == 0)
        {
            int NextIndex = ((int)NextWord >> 4) - 1;
            if (NextIndex > 0x3F)
            {
                NextIndex = 0x3F;
            }
            if (NextBlock[1] == NextBlock[2])
            {
                MovieSbhClearBit(Group, Header, Region, NextIndex);
            }
            *(int32_t*)(NextBlock[2] + 4) = NextBlock[1];
            *(int32_t*)(NextBlock[1] + 8) = NextBlock[2];
            Remain = Remain + (int)NextWord;
            NewIndex = (Remain >> 4) - 1;
            if (NewIndex > 0x3F)
            {
                NewIndex = 0x3F;
            }
        }
        int32_t* Head = Desc + NewIndex * 2;
        Piece[1] = Head[1];
        Piece[2] = (int32_t)Head;
        Head[1] = (int32_t)Piece;
        *(int32_t*)(Piece[1] + 8) = (int32_t)Piece;
        if (Piece[1] == Piece[2])
        {
            MovieSbhSetBit(Group, Header, Region, NewIndex);
        }
        *Piece = Remain;
        *(int32_t*)((uint8_t*)Piece + Remain - 4) = Remain;
    }
    return 1;
}

// FUN_06ad51fc. Shrink or grow an old-heap block inside its page. 0 means allocate a new one.
static int MovieOldSbhResize(FMovieOldSbhDesc* Desc, uint8_t* Page, uint8_t* Header, unsigned int Class)
{
    unsigned int Have = *Header;
    uint32_t* Pair = (uint32_t*)((uint8_t*)Desc + 0x18) + (((Page - Desc->Region) >> 12) * 2);
    if (Class < Have)
    {
        *Header = (uint8_t)Class;
        Pair[0] = Pair[0] + (Have - Class);
        Pair[1] = 0xF1;
        return 1;
    }
    if (Class <= Have)
    {
        return 0;
    }
    uint8_t* End = Header + Class;
    if (Page + 0xF8 < End)
    {
        return 0;
    }
    uint8_t* Scan = Header + Have;
    while (Scan < End && *Scan == 0)
    {
        Scan = Scan + 1;
    }
    if (Scan != End)
    {
        return 0;
    }
    *Header = (uint8_t)Class;
    if (Header <= *(uint8_t**)Page && *(uint8_t**)Page < End)
    {
        if (End < Page + 0xF8)
        {
            int Gap = 0;
            *(uint8_t**)Page = End;
            while (End[Gap] == 0)
            {
                Gap = Gap + 1;
            }
            *(uint32_t*)(Page + 4) = (uint32_t)Gap;
        }
        else
        {
            *(uint32_t*)(Page + 4) = 0;
            *(uint8_t**)Page = Page + 8;
        }
    }
    Pair[0] = Pair[0] + (Have - Class);
    return 1;
}

static unsigned int MovieHeapCopyLength(void* Block, unsigned int Size, int OldKind)
{
    unsigned int Have = 0;
    if (OldKind == 3)
    {
        Have = *(uint32_t*)((uint8_t*)Block - 4) - 1u;
    }
    else
    {
        Have = (unsigned int)(*(uint8_t*)Block) << 4;
    }
    if (Have > Size)
    {
        Have = Size;
    }
    return Have;
}

// FUN_06ad15a6. _realloc. Kind 1 is HeapReAlloc with flags 0 after the 16-byte round.
static void* MovieHeapRealloc(void* Block, unsigned int Size)
{
    if (Block == nullptr)
    {
        return MovieNhMalloc(Size, GMovieNewMode);
    }
    if (Size == 0)
    {
        MovieHeapFree(Block);
        return nullptr;
    }
    void* Result = nullptr;
    for (;;)
    {
        Result = nullptr;
        if (GMovieHeapKind == 3)
        {
            if (Size <= 0xFFFFFFE0u)
            {
                MovieLock(9);
                FMovieSbhGroup* Group = nullptr;
                __try
                {
                    Group = MovieSbhFind(Block);
                    if (Group != nullptr)
                    {
                        if (Size <= GMovieSbhThreshold)
                        {
                            if (MovieSbhResize(Group, Block, Size) == 0)
                            {
                                Result = MovieSbhAlloc(Size);
                                if (Result != nullptr)
                                {
                                    unsigned int Copy = MovieHeapCopyLength(Block, Size, 3);
                                    memmove(Result, Block, Copy);
                                    Group = MovieSbhFind(Block);
                                    MovieSbhFree(Group, Block);
                                }
                            }
                            else
                            {
                                Result = Block;
                            }
                        }
                        if (Result == nullptr)
                        {
                            unsigned int Rounded = Size;
                            if (Rounded == 0)
                            {
                                Rounded = 1;
                            }
                            Rounded = (Rounded + 0x0Fu) & 0xFFFFFFF0u;
                            Result = HeapAlloc(GMovieCrtHeap, 0, Rounded);
                            if (Result != nullptr)
                            {
                                unsigned int Copy = MovieHeapCopyLength(Block, Rounded, 3);
                                memmove(Result, Block, Copy);
                                MovieSbhFree(Group, Block);
                            }
                        }
                    }
                }
                __finally
                {
                    MovieUnlock(9);
                }
                if (Group == nullptr)
                {
                    unsigned int Rounded = Size;
                    if (Rounded == 0)
                    {
                        Rounded = 1;
                    }
                    Rounded = (Rounded + 0x0Fu) & 0xFFFFFFF0u;
                    Size = Rounded;
                    Result = HeapReAlloc(GMovieCrtHeap, 0, Block, Rounded);
                }
            }
        }
        else if (GMovieHeapKind == 2)
        {
            unsigned int Rounded = Size;
            if (Size <= 0xFFFFFFE0u)
            {
                if (Size == 0)
                {
                    Rounded = 0x10;
                }
                else
                {
                    Rounded = (Size + 0x0Fu) & 0xFFFFFFF0u;
                }
                Size = Rounded;
            }
            if (Rounded <= 0xFFFFFFE0u)
            {
                MovieLock(9);
                FMovieOldSbhDesc* Desc = nullptr;
                uint8_t* Page = nullptr;
                uint8_t* Header = nullptr;
                __try
                {
                    Header = MovieOldSbhFind(Block, &Desc, &Page);
                    if (Header == nullptr)
                    {
                        Result = HeapReAlloc(GMovieCrtHeap, 0, Block, Rounded);
                    }
                    else
                    {
                        if (Rounded < 0x1E0u)
                        {
                            unsigned int Class = Rounded >> 4;
                            if (MovieOldSbhResize(Desc, Page, Header, Class) == 0)
                            {
                                Result = MovieOldSbhAlloc(Class);
                                if (Result != nullptr)
                                {
                                    unsigned int Copy = (unsigned int)(*Header) << 4;
                                    if (Copy > Rounded)
                                    {
                                        Copy = Rounded;
                                    }
                                    memmove(Result, Block, Copy);
                                    MovieOldSbhFree(Desc, Page, Header);
                                }
                            }
                            else
                            {
                                Result = Block;
                            }
                        }
                        if (Result == nullptr)
                        {
                            Result = HeapAlloc(GMovieCrtHeap, 0, Rounded);
                            if (Result != nullptr)
                            {
                                unsigned int Copy = (unsigned int)(*Header) << 4;
                                if (Copy > Rounded)
                                {
                                    Copy = Rounded;
                                }
                                memmove(Result, Block, Copy);
                                MovieOldSbhFree(Desc, Page, Header);
                            }
                        }
                    }
                }
                __finally
                {
                    MovieUnlock(9);
                }
            }
        }
        else
        {
            if (Size <= 0xFFFFFFE0u)
            {
                if (Size == 0)
                {
                    Size = 1;
                }
                Size = (Size + 0x0Fu) & 0xFFFFFFF0u;
                Result = HeapReAlloc(GMovieCrtHeap, 0, Block, Size);
            }
        }
        if (Result != nullptr || GMovieNewMode == 0)
        {
            return Result;
        }
        if (MovieCallNewHandler(Size) == 0)
        {
            return nullptr;
        }
    }
}

// FUN_06acf7f8. Lock 0xD around the table update.
static void (__cdecl* MovieOnexitRegister(void (__cdecl* Callback)()))()
{
    MovieLockOnexit();
    unsigned int Have = MovieMsize(GMovieOnexitBegin);
    unsigned int Used = (unsigned int)((unsigned char*)GMovieOnexitEnd - (unsigned char*)GMovieOnexitBegin);
    unsigned int Need = Used + 4u;
    void (__cdecl** Slot)() = GMovieOnexitEnd;
    void (__cdecl* Result)() = Callback;
    if (Have < Need)
    {
        unsigned int NewBytes = MovieMsize(GMovieOnexitBegin) + 0x10u;
        void (__cdecl** Grown)() = (void (__cdecl**)())MovieHeapRealloc(GMovieOnexitBegin, NewBytes);
        if (Grown == nullptr)
        {
            Result = nullptr;
        }
        else
        {
            int Delta = (int)((unsigned char*)GMovieOnexitEnd - (unsigned char*)GMovieOnexitBegin);
            GMovieOnexitBegin = Grown;
            Slot = (void (__cdecl**)())((unsigned char*)Grown + (((unsigned int)Delta >> 2) * 4u));
            GMovieOnexitEnd = Slot;
        }
    }
    if (Result != nullptr)
    {
        *Slot = Callback;
        GMovieOnexitEnd = Slot + 1;
        Result = Callback;
    }
    MovieUnlockOnexit();
    return Result;
}

// FUN_06acf3d0. PUSH arg; CALL FUN_06ad07b3; POP ECX; RET.
void __cdecl MovieOperatorDelete(void* Block) // formerly Movie_06acf3d0
{
    MovieHeapFree(Block);
}

// 06acf3db. PUSH 1; PUSH size; CALL __nh_malloc; POP; POP; RET.
void* __cdecl MovieOperatorNew(unsigned int Size) // formerly Movie_06acf3db
{
    return MovieNhMalloc(Size, 1);
}

// FUN_06ad970e. Throw of length_error("string too long"). No in-DLL typed catch.
void __cdecl MovieThrowStringTooLong() // formerly Movie_06ad970e
{
    throw std::length_error("string too long");
}

// FUN_06acf876. PUSH callback; CALL FUN_06acf7f8; (result != 0) - 1.
int __cdecl MovieRegisterAtexit(void (__cdecl* Callback)()) // formerly Movie_06acf876
{
    void (__cdecl* Stored)() = MovieOnexitRegister(Callback);
    return (Stored != nullptr) - 1;
}

// LAB_06acf7ef. PUSH 0x19; CALL __amsg_exit; POP ECX; RET. RET is not reached.
void __cdecl MovieAbortRuntime() // formerly Movie_06acf7ef
{
    MovieAmsgExit(0x19);
}

// FUN_06acfda2. PROCESS_ATTACH calls FUN_06ad12fc. A zero result calls FUN_06ad2d0a and returns 0.
BOOL WINAPI DllMain(HINSTANCE Module, DWORD Reason, LPVOID Reserved)
{
    (void)Module;
    (void)Reserved;
    if (Reason == 1)
    {
        if (MovieInitThreadData() == 0)
        {
            MovieHeapShutdown();
            return FALSE;
        }
    }
    return TRUE;
}
