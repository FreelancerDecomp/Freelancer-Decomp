// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "MovieCca.h"

#include "MovieAce.h"
#include "MovieField.h"

#include <stddef.h>
#include <windows.h>

// 0x06ac8d40. Defined in MovieFieldHigh.cpp.
int __stdcall LimbBitLength(int Limbs, int Count);

static_assert(sizeof(FRngState) == 0x120u, "rng state");
static_assert(sizeof(OSVERSIONINFOA) == 0x94u, "version info");
static_assert(offsetof(OSVERSIONINFOA, dwPlatformId) == 0x10u, "platform id");

// DAT_06add3c0, DAT_06add3c4, DAT_06add3c8. Initial bytes from the listing.
static unsigned int GPoolCapacity = 0x200u;
static unsigned int GPoolIndex = 0x4000u;
static unsigned int GPlatformNt = 1u;

// DAT_06ae0ad8, DAT_06ae0ae0 (0x50 bytes), DAT_06ae0b30 (RC4 box, indices at +0x100).
static unsigned int GUserNameLength = 0u;
static unsigned char GRngSeed[0x50];
static unsigned char GRc4Box[0x102];

// DAT_06ae0c34, DAT_06ae0c38, DAT_06ae0d58 (0x10), DAT_06ae0d68 (0x101).
static unsigned int GComputerNameLength = 0u;
static FRngState GRngState;
static unsigned char GComputerName[0x10];
static unsigned char GUserName[0x101];

typedef long (__stdcall* FNtQuerySystemInformation)(unsigned int Class, void* Buffer, unsigned int Length, unsigned int* Returned);
typedef long (__stdcall* FNtOpenFile)(void** Handle, unsigned int Access, void* Attributes, void* IoStatus, unsigned int Share, unsigned int Options);
typedef void (__stdcall* FRtlInitUnicodeString)(void* Destination, const wchar_t* Source);
typedef int (__stdcall* FGetCursorPos)(void* Point);
typedef long (__stdcall* FGetMessageTime)();

// DAT_06ae0e6c, DAT_06ae0e70, DAT_06ae0e74, DAT_06ae0e78, DAT_06ae0e7c.
static FNtQuerySystemInformation GNtQuerySystemInformation = 0;
static FNtOpenFile GNtOpenFile = 0;
static FRtlInitUnicodeString GRtlInitUnicodeString = 0;
static FGetCursorPos GGetCursorPos = 0;
static FGetMessageTime GGetMessageTime = 0;

// DAT_06ae0e80, DAT_06ae0e84.
static unsigned int GRngReady = 0u;
static unsigned int GPlatformReady = 0u;

// 16-byte request at 0x06acca22. Magic 0x10 lives in the first dword.
struct FRngRequest
{
    unsigned int Magic;
    unsigned int Flags;
    unsigned int Data;
    unsigned int Length;
};

static_assert(sizeof(FRngRequest) == 0x10u, "rng request");

// OBJECT_ATTRIBUTES is 0x18 bytes in the 0x06acd11d frame. UNICODE_STRING is 8.
struct FKernelUnicodeString
{
    unsigned short Length;
    unsigned short MaximumLength;
    wchar_t* Buffer;
};

struct FKernelObjectAttributes
{
    unsigned int Length;
    void* RootDirectory;
    FKernelUnicodeString* ObjectName;
    unsigned int Attributes;
    void* SecurityDescriptor;
    void* SecurityQualityOfService;
};

static_assert(sizeof(FKernelUnicodeString) == 8u, "UNICODE_STRING");
static_assert(sizeof(FKernelObjectAttributes) == 0x18u, "OBJECT_ATTRIBUTES");

// REP MOVSD then REP MOVSB. x86 stores the dwords at whatever alignment the pointer has.
static void CopyBytes(unsigned char* Dest, const unsigned char* Source, unsigned int Count)
{
    unsigned int Words = Count >> 2;
    unsigned int Tail = Count & 3u;
    unsigned int Index;
    for (Index = 0u; Index < Words; Index = Index + 1u)
    {
        reinterpret_cast<unsigned int*>(Dest)[Index] = reinterpret_cast<const unsigned int*>(Source)[Index];
    }
    Dest = Dest + (Words * 4u);
    Source = Source + (Words * 4u);
    for (Index = 0u; Index < Tail; Index = Index + 1u)
    {
        Dest[Index] = Source[Index];
    }
}

// REP STOSD then REP STOSB.
static void ZeroBytes(unsigned char* Dest, unsigned int Count)
{
    unsigned int Words = Count >> 2;
    unsigned int Tail = Count & 3u;
    unsigned int Index;
    for (Index = 0u; Index < Words; Index = Index + 1u)
    {
        reinterpret_cast<unsigned int*>(Dest)[Index] = 0u;
    }
    Dest = Dest + (Words * 4u);
    for (Index = 0u; Index < Tail; Index = Index + 1u)
    {
        Dest[Index] = 0;
    }
}

static void ZeroDwords(unsigned int* Dest, unsigned int Count)
{
    unsigned int Index;
    for (Index = 0u; Index < Count; Index = Index + 1u)
    {
        Dest[Index] = 0u;
    }
}

// (address & 3) + Payload. JA skips the advance. Equal still advances.
static bool Commit(unsigned char*& Cursor, unsigned int& Remaining, unsigned int Payload)
{
    unsigned int Stride = (reinterpret_cast<unsigned int>(Cursor) & 3u) + Payload;
    if (Stride > Remaining)
    {
        return false;
    }
    Remaining = Remaining - Stride;
    Cursor = Cursor + Stride;
    return true;
}

// Same stride. The cursor register is replaced by the stride and is not written back.
static void AccountTail(unsigned char* Cursor, unsigned int& Remaining, unsigned int Payload)
{
    unsigned int Stride = (reinterpret_cast<unsigned int>(Cursor) & 3u) + Payload;
    if (Stride <= Remaining)
    {
        Remaining = Remaining - Stride;
    }
}

static bool AppendDword(unsigned char*& Cursor, unsigned int& Remaining, unsigned int Value)
{
    *reinterpret_cast<unsigned int*>(Cursor) = Value;
    return Commit(Cursor, Remaining, 4u);
}

// Fixed-size NtQuerySystemInformation. Status < 0 leaves the cursor where it is.
// A stride that does not fit aborts the gather.
static bool AppendSystemInfo(unsigned char*& Cursor, unsigned int& Remaining, unsigned int Class, unsigned int Want)
{
    unsigned int Returned = 0u;
    long Status;
    if (Remaining < Want)
    {
        return false;
    }
    Status = GNtQuerySystemInformation(Class, Cursor, Want, &Returned);
    if (Status < 0)
    {
        return true;
    }
    return Commit(Cursor, Remaining, Returned);
}

// 0x06acd1bf. RET 0. Cached after the first call. 1 means the NT branch.
static unsigned int __stdcall QueryRngPlatform()
{
    OSVERSIONINFOA Version;
    unsigned int* Words;
    unsigned int Index;
    HMODULE Module;
    if (GPlatformReady != 0u)
    {
        return GPlatformNt;
    }
    Words = reinterpret_cast<unsigned int*>(&Version);
    for (Index = 0u; Index < 0x25u; Index = Index + 1u)
    {
        Words[Index] = 0u;
    }
    Version.dwOSVersionInfoSize = 0x94u;
#pragma warning(suppress: 4996)
    if (GetVersionExA(&Version) != 0)
    {
        if (Version.dwPlatformId == 2u)
        {
            GPlatformNt = 1u;
        }
        else
        {
            GPlatformNt = 0u;
        }
        if (GPlatformNt != 0u)
        {
            Module = GetModuleHandleW(L"ntdll.dll");
            if (Module != 0)
            {
                GNtQuerySystemInformation = reinterpret_cast<FNtQuerySystemInformation>(GetProcAddress(Module, "NtQuerySystemInformation"));
                if (GNtQuerySystemInformation != 0)
                {
                    GPoolCapacity = 0x4000u;
                }
                GNtOpenFile = reinterpret_cast<FNtOpenFile>(GetProcAddress(Module, "NtOpenFile"));
                GRtlInitUnicodeString = reinterpret_cast<FRtlInitUnicodeString>(GetProcAddress(Module, "RtlInitUnicodeString"));
            }
        }
        else
        {
            Module = LoadLibraryA("user32.dll");
            if (Module != 0)
            {
                GGetCursorPos = reinterpret_cast<FGetCursorPos>(GetProcAddress(Module, "GetCursorPos"));
                GGetMessageTime = reinterpret_cast<FGetMessageTime>(GetProcAddress(Module, "GetMessageTime"));
            }
        }
    }
    GPlatformReady = 1u;
    return GPlatformNt;
}

// 0x06acd11d. RET 0x10. Opens \Device\KsecDD and issues IOCTL 0x390008.
// Returns the DeviceIoControl result. Every miss returns 0.
static int __stdcall ReadKsecRandom(void* Input, unsigned int InputLength, void* Output, unsigned int* OutputLength)
{
    FKernelUnicodeString Name;
    FKernelObjectAttributes Attributes;
    unsigned int IoStatus[2];
    void* Handle;
    long Status;
    int Control;
    if (QueryRngPlatform() == 0u || GNtOpenFile == 0 || GRtlInitUnicodeString == 0)
    {
        return 0;
    }
    GRtlInitUnicodeString(&Name, L"\\Device\\KsecDD");
    Attributes.Length = 0x18u;
    Attributes.RootDirectory = 0;
    Attributes.ObjectName = &Name;
    Attributes.Attributes = 0x40u;
    Attributes.SecurityDescriptor = 0;
    Attributes.SecurityQualityOfService = 0;
    Status = GNtOpenFile(&Handle, 0x100001u, &Attributes, IoStatus, 7u, 0x10u);
    if (Status < 0)
    {
        return 0;
    }
    Control = DeviceIoControl(Handle, 0x390008u, Input, InputLength, Output, *OutputLength, reinterpret_cast<LPDWORD>(OutputLength), 0);
    CloseHandle(Handle);
    return Control;
}

// Bytes actually consumed in the 0xE00 gather buffer. An early exit keeps the
// remaining count at the point the listing jumps to 0x06acd08d.
static unsigned int CollectEntropy(unsigned char* Entropy, const unsigned char* Seed, unsigned int SeedLength)
{
    unsigned int Remaining = 0xE00u;
    unsigned int Bound;
    unsigned char* Cursor;
    unsigned int NameSize;
    unsigned int UserSize;
    unsigned int Returned;
    long Status;
    if (SeedLength > 0xE00u)
    {
        return Remaining;
    }
    CopyBytes(Entropy, Seed, SeedLength);
    Bound = (reinterpret_cast<unsigned int>(Entropy) & 3u) + SeedLength;
    if (Bound > 0xE00u)
    {
        return Remaining;
    }
    Cursor = Entropy + Bound;
    Remaining = 0xE00u - Bound;
    if (!AppendDword(Cursor, Remaining, GetCurrentProcessId()))
    {
        return Remaining;
    }
    if (!AppendDword(Cursor, Remaining, GetCurrentThreadId()))
    {
        return Remaining;
    }
    if (!AppendDword(Cursor, Remaining, GetTickCount()))
    {
        return Remaining;
    }
    if (Remaining < 0x10u)
    {
        return Remaining;
    }
    GetLocalTime(reinterpret_cast<LPSYSTEMTIME>(Cursor));
    if (!Commit(Cursor, Remaining, 0x10u))
    {
        return Remaining;
    }
    if (Remaining < 8u)
    {
        return Remaining;
    }
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(Cursor));
    if (!Commit(Cursor, Remaining, 8u))
    {
        return Remaining;
    }
    if (Remaining < 0x20u)
    {
        return Remaining;
    }
    *reinterpret_cast<unsigned int*>(Cursor) = 0x20u;
    GlobalMemoryStatus(reinterpret_cast<LPMEMORYSTATUS>(Cursor));
    if (!Commit(Cursor, Remaining, 0x20u))
    {
        return Remaining;
    }
    if (Remaining < 0x10u)
    {
        return Remaining;
    }
    GetDiskFreeSpaceA(0,
        reinterpret_cast<LPDWORD>(Cursor),
        reinterpret_cast<LPDWORD>(Cursor + 4),
        reinterpret_cast<LPDWORD>(Cursor + 8),
        reinterpret_cast<LPDWORD>(Cursor + 12));
    if (!Commit(Cursor, Remaining, 0x10u))
    {
        return Remaining;
    }
    if (GComputerNameLength == 0u)
    {
        NameSize = 0x10u;
        if (GetComputerNameA(reinterpret_cast<LPSTR>(GComputerName), reinterpret_cast<LPDWORD>(&NameSize)) != 0)
        {
            GComputerNameLength = NameSize;
        }
    }
    if (GComputerNameLength != 0u)
    {
        if (Remaining < GComputerNameLength)
        {
            return Remaining;
        }
        CopyBytes(Cursor, GComputerName, GComputerNameLength);
        if (!Commit(Cursor, Remaining, GComputerNameLength))
        {
            return Remaining;
        }
    }
    if (GUserNameLength == 0u)
    {
        UserSize = 0x101u;
        if (GetUserNameA(reinterpret_cast<LPSTR>(GUserName), reinterpret_cast<LPDWORD>(&UserSize)) != 0)
        {
            GUserNameLength = UserSize - 1u;
        }
    }
    if (GUserNameLength != 0u)
    {
        if (Remaining < GUserNameLength)
        {
            return Remaining;
        }
        CopyBytes(Cursor, GUserName, GUserNameLength);
        if (!Commit(Cursor, Remaining, GUserNameLength))
        {
            return Remaining;
        }
    }
    if (QueryRngPlatform() == 0u)
    {
        if (Remaining < 8u)
        {
            return Remaining;
        }
        GGetCursorPos(Cursor);
        if (!Commit(Cursor, Remaining, 8u))
        {
            return Remaining;
        }
        if (Remaining < 4u)
        {
            return Remaining;
        }
        *reinterpret_cast<unsigned int*>(Cursor) = static_cast<unsigned int>(GGetMessageTime());
        AccountTail(Cursor, Remaining, 4u);
        return Remaining;
    }
    Returned = 0x40u;
    if (Remaining < 0x40u)
    {
        return Remaining;
    }
    if (RngTryReadBlock(Cursor, &Returned) != 0)
    {
        if (!Commit(Cursor, Remaining, Returned))
        {
            return Remaining;
        }
    }
    if (GNtQuerySystemInformation == 0 || Remaining < 0x30u)
    {
        return Remaining;
    }
    if (!AppendSystemInfo(Cursor, Remaining, 8u, 0x30u))
    {
        return Remaining;
    }
    if (!AppendSystemInfo(Cursor, Remaining, 2u, 0x138u))
    {
        return Remaining;
    }
    if (!AppendSystemInfo(Cursor, Remaining, 0x21u, 0x10u))
    {
        return Remaining;
    }
    if (!AppendSystemInfo(Cursor, Remaining, 0x2du, 0x20u))
    {
        return Remaining;
    }
    Returned = Remaining;
    Status = GNtQuerySystemInformation(0x17u, Cursor, Remaining, &Returned);
    if (Status >= 0)
    {
        if (!Commit(Cursor, Remaining, Returned))
        {
            return Remaining;
        }
    }
    Returned = Remaining;
    Status = GNtQuerySystemInformation(5u, Cursor, Remaining, &Returned);
    if (Status < 0)
    {
        return Remaining;
    }
    AccountTail(Cursor, Remaining, Returned);
    return Remaining;
}

// 0x06accca6. RET 0x10.
// Ksec success returns 1. Otherwise the 0xE00 buffer is mixed into the seed,
// the previous seed keys RC4 over the new seed, and that ciphertext is written
// back. The output is then RC4'd under the new seed. The box is cleared
// (0x40 dwords and one word).
static int __stdcall GatherRngBytes(const unsigned char* Seed, unsigned int SeedLength, unsigned char* Output, unsigned int* OutputLength)
{
    unsigned char Scratch[0xE04];
    unsigned char Box[0x102];
    unsigned char Folded[0x50];
    unsigned int Remaining;
    int Mixed;
    if (ReadKsecRandom(const_cast<unsigned char*>(Seed), SeedLength, Output, OutputLength) != 0)
    {
        return 1;
    }
    Remaining = CollectEntropy(Scratch, Seed, SeedLength);
    Rc4Init(Box, 0x50u, GRngSeed);
    Mixed = MixShaBlocks(Scratch, 0xE00u - Remaining, GRngSeed);
    CopyBytes(Folded, GRngSeed, 0x50u);
    Rc4Crypt(Box, 0x50u, Folded);
    RngWriteSeed(Folded, 0x50u);
    Rc4Init(Box, 0x50u, GRngSeed);
    Rc4Crypt(Box, *OutputLength, Output);
    ZeroBytes(Box, 0x102u);
    return Mixed;
}

// 0x06accbd0. RET 8.
// Stirs Data into the pool. When the cursor is not below the capacity, refills
// the RC4 box from 0x100 gathered bytes and clears those bytes. Returns the
// byte count written, 0 on a failed stir or refill, or 1 after forcing the
// caller's count to 0 when the unsigned remainder exceeds the capacity.
static unsigned int __stdcall ReadRngChunk(unsigned char* Data, unsigned int* Count)
{
    unsigned char* Pool = 0;
    unsigned int PoolSize = 0u;
    unsigned int Chunk = 0x100u;
    unsigned char Temp[0x100];
    unsigned int Available;
    if (RngStir(&GRngState, Data, *Count) == 0)
    {
        return 0u;
    }
    if (GPoolIndex >= GPoolCapacity)
    {
        if (RngQueryBuffer(&GRngState, &Pool, &PoolSize) == 0)
        {
            return 0u;
        }
        if (GatherRngBytes(Pool, PoolSize, Temp, &Chunk) == 0)
        {
            return 0u;
        }
        Rc4Init(GRc4Box, Chunk, Temp);
        GPoolIndex = 0u;
        ZeroBytes(Temp, Chunk);
    }
    Available = GPoolCapacity - GPoolIndex;
    if (Available > GPoolCapacity)
    {
        *Count = 0u;
        return 1u;
    }
    if (*Count > Available)
    {
        *Count = Available;
    }
    Rc4Crypt(GRc4Box, *Count, Data);
    GPoolIndex = GPoolIndex + *Count;
    return *Count;
}

// 0x06accb95. RET 0xC. The first argument is unread.
static int __stdcall ReadRngStream(unsigned int, unsigned char* Output, unsigned int Length)
{
    unsigned int Done = 0u;
    if (Length != 0u)
    {
        do
        {
            unsigned int Chunk = Length - Done;
            if (ReadRngChunk(Output + Done, &Chunk) == 0u)
            {
                return 0;
            }
            Done = Done + Chunk;
        }
        while (Done < Length);
    }
    return 1;
}

// 0x06accb47. RET 8. Always returns 1. Copies min(Count, pool size) bytes
// from the pool when both arguments are non-zero and the pool query succeeds.
static int __stdcall CopyRngPrefix(void* Dest, unsigned int Count)
{
    unsigned char* Pool = 0;
    unsigned int Available = 0u;
    unsigned int Amount;
    if (Dest == 0 || Count == 0u)
    {
        return 1;
    }
    if (RngQueryBuffer(&GRngState, &Pool, &Available) == 0)
    {
        return 1;
    }
    Amount = Available;
    if (Count <= Available)
    {
        Amount = Count;
    }
    CopyBytes(reinterpret_cast<unsigned char*>(Dest), Pool, Amount);
    return 1;
}

// 0x06accaf0. RET 8. Always returns 1.
// The first call inits the global pool (step 7, mode 1, flags 0) and reads
// 0x50 seed bytes. A following call only stirs when both slots are present
// and the length dword is non-zero.
static int __stdcall StirRngSeed(unsigned int* DataSlot, unsigned int* LengthSlot)
{
    if (GRngReady == 0u)
    {
        RngInitState(&GRngState, 7u, 1u, 0u);
        RngReadSeed(GRngSeed, 0x50u);
        GRngReady = 1u;
    }
    if (DataSlot != 0 && LengthSlot != 0 && *LengthSlot != 0u)
    {
        RngStir(&GRngState, reinterpret_cast<void*>(*DataSlot), *LengthSlot);
    }
    return 1;
}

// 0x06acca22. RET 0xC. Magic other than 0x10 returns 0.
// Bit 0 of Flags selects GatherRngBytes; otherwise the pool stream is used.
// When Data and Length are both non-zero they are stirred in and then the
// pool prefix is copied over that buffer. The return is the fill result.
static int __stdcall FillRngBuffer(FRngRequest* Request, unsigned char* Output, unsigned int Length)
{
    unsigned int* DataSlot = 0;
    unsigned int* LengthSlot = 0;
    int Result;
    if (Request->Magic != 0x10u)
    {
        return 0;
    }
    if (Request->Data != 0u && Request->Length != 0u)
    {
        DataSlot = &Request->Data;
        LengthSlot = &Request->Length;
    }
    StirRngSeed(DataSlot, LengthSlot);
    if ((Request->Flags & 1u) == 0u)
    {
        Result = ReadRngStream(0u, Output, Length);
    }
    else
    {
        Result = GatherRngBytes(0, 0u, Output, &Length);
    }
    if (DataSlot != 0 && LengthSlot != 0)
    {
        CopyRngPrefix(reinterpret_cast<void*>(*DataSlot), *LengthSlot);
    }
    return Result;
}

// 0x06acca95. RET 0x10.
void __stdcall GenerateRngBytes(unsigned int* Seed, unsigned int* SeedLength, unsigned char* Output, unsigned int Length)
{
    FRngRequest Request;
    Request.Magic = 0u;
    Request.Flags = 0u;
    Request.Data = 0u;
    Request.Length = 0u;
    Request.Magic = 0x10u;
    if (Seed == 0 || SeedLength == 0)
    {
        FillRngBuffer(&Request, Output, Length);
        return;
    }
    Request.Data = *Seed;
    Request.Length = *SeedLength;
    FillRngBuffer(&Request, Output, Length);
    *SeedLength = Request.Length;
}

// 0x06acd2b0. RET 0x1C.
// Work is 0x251c bytes: the square at +0, the shift slot at +0x100, the product scratch at +0x200.
// Square becomes P*P - (Q<<2). The loop walks exponent bits from (bit length - 2) down to 0.
void __stdcall LadderLucasModulo(unsigned int* First, unsigned int* Second, unsigned int* Third, unsigned int Count, unsigned int* Fourth, unsigned int* Fifth, unsigned int* Context)
{
    int BitLength = LimbBitLength(reinterpret_cast<int>(Third), static_cast<int>(Count));
    unsigned int LimbCount = Context[0x40];
    unsigned char* Work = reinterpret_cast<unsigned char*>(Movie_AllocateBlock(0x251c));
    unsigned int ByteCount;
    unsigned int* Square;
    unsigned int* Shifted;
    unsigned int* Product;
    int BitIndex;
    if (Work == 0)
    {
        ByteCount = LimbCount * 4u;
        ZeroBytes(reinterpret_cast<unsigned char*>(Fourth), ByteCount);
        ZeroBytes(reinterpret_cast<unsigned char*>(Fifth), ByteCount);
        return;
    }
    if (BitLength == 0)
    {
        Movie_AddModulo(Context + 0x451, Context + 0x451, Fifth, Context);
        ZeroDwords(Fourth, LimbCount);
        Movie_FreeBlock(Work);
        return;
    }
    ByteCount = LimbCount * 4u;
    CopyBytes(reinterpret_cast<unsigned char*>(Fifth), reinterpret_cast<unsigned char*>(First), ByteCount);
    CopyBytes(reinterpret_cast<unsigned char*>(Fourth), reinterpret_cast<unsigned char*>(Context + 0x451), ByteCount);
    Square = reinterpret_cast<unsigned int*>(Work);
    Shifted = reinterpret_cast<unsigned int*>(Work + 0x100);
    Product = reinterpret_cast<unsigned int*>(Work + 0x200);
    Movie_ShiftModulo(Second, 2, Shifted, Context);
    Movie_MultiplyModulo(First, First, Square, Context, Product);
    Movie_SubtractModulo(Square, Shifted, Square, Context);
    BitIndex = BitLength - 1;
    if (BitIndex != 0)
    {
        do
        {
            unsigned int Index;
            unsigned int BitWord;
            BitIndex = BitIndex - 1;
            Index = static_cast<unsigned int>(BitIndex);
            Movie_MultiplyModulo(Fourth, Fourth, Shifted, Context, Product);
            Movie_MultiplyModulo(Shifted, Square, Shifted, Context, Product);
            Movie_MultiplyModulo(Fourth, Fifth, Fourth, Context, Product);
            Movie_MultiplyModulo(Fifth, Fifth, Fifth, Context, Product);
            Movie_AddModulo(Fifth, Shifted, Fifth, Context);
            Movie_ShiftModulo(Fifth, -1, Fifth, Context);
            BitWord = Third[Index >> 5];
            if (((BitWord >> (Index & 0x1fu)) & 1u) != 0u)
            {
                Movie_MultiplyModulo(Square, Fourth, Shifted, Context, Product);
                Movie_MultiplyModulo(First, Fourth, Fourth, Context, Product);
                Movie_AddModulo(Fourth, Fifth, Fourth, Context);
                Movie_ShiftModulo(Fourth, -1, Fourth, Context);
                Movie_MultiplyModulo(First, Fifth, Fifth, Context, Product);
                Movie_AddModulo(Fifth, Shifted, Fifth, Context);
                Movie_ShiftModulo(Fifth, -1, Fifth, Context);
            }
        }
        while (BitIndex != 0);
    }
    Movie_FreeBlock(Work);
}
