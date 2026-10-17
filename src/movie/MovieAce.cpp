// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "MovieAce.h"

#include "MovieSha.h"

#include <stddef.h>
#include <windows.h>

static_assert(offsetof(FRngState, Buffer) == 0x20u, "rng pool");
static_assert(offsetof(FRngState, Mode) == 0x14u, "rng mode");
static_assert(offsetof(FMd4Context, Buffer) == 0x18u, "md4 buffer");
static_assert(offsetof(FMd4Context, Digest) == 0x58u, "md4 digest");
static_assert(sizeof(FSha1Context) == 0x74u, "sha context scrubbed as 29 dwords");

// 0x06ada920 through 0x06ada95f. First byte 0x80, the other 63 bytes are 0.
// Md4Final copies at most 0x40 of these.
static const unsigned char Md4Padding[64] = { 0x80 };

static unsigned int RotateLeft(unsigned int Value, unsigned int Bits)
{
    return (Value << Bits) | (Value >> (32u - Bits));
}

// REP MOVSD then REP MOVSB. Forward copy, same split as 0x06acedc0.
static void CopyDwordsThenBytes(unsigned char* Dest, const unsigned char* Source, unsigned int Count)
{
    unsigned int Words = Count >> 2;
    unsigned int Tail = Count & 3u;
    unsigned int Index;
    for (Index = 0u; Index < Words; Index = Index + 1u)
    {
        *reinterpret_cast<unsigned int*>(Dest + Index * 4u) = *reinterpret_cast<const unsigned int*>(Source + Index * 4u);
    }
    for (Index = 0u; Index < Tail; Index = Index + 1u)
    {
        Dest[Words * 4u + Index] = Source[Words * 4u + Index];
    }
}

static unsigned int Md4F(unsigned int B, unsigned int C, unsigned int D)
{
    return ((C ^ D) & B) ^ D;
}

static unsigned int Md4G(unsigned int B, unsigned int C, unsigned int D)
{
    return ((B | C) & D) | (B & C);
}

static unsigned int Md4H(unsigned int B, unsigned int C, unsigned int D)
{
    return B ^ C ^ D;
}

// One unrolled step. After it, A holds the previous D and B holds the rotated sum.
static void Md4Step(unsigned int& A, unsigned int& B, unsigned int& C, unsigned int& D, unsigned int Sum, unsigned int Shift)
{
    unsigned int NextA = D;
    unsigned int Updated = RotateLeft(A + Sum, Shift);
    D = C;
    C = B;
    B = Updated;
    A = NextA;
}

// 0x06acef00. No branches. Round words and rotates were read off the unrolled body
// through 0x06acf366. Round 1 has no constant. Round 2 adds 0x5A827999.
// Round 3 adds 0x6ED9EBA1. The four state words are added back at 0x06acf346.
void __stdcall Md4Transform(unsigned int* State, const unsigned int* Block)
{
    static const unsigned int Round2Index[16] = {
        0u, 4u, 8u, 12u, 1u, 5u, 9u, 13u, 2u, 6u, 10u, 14u, 3u, 7u, 11u, 15u
    };
    static const unsigned int Round3Index[16] = {
        0u, 8u, 4u, 12u, 2u, 10u, 6u, 14u, 1u, 9u, 5u, 13u, 3u, 11u, 7u, 15u
    };
    static const unsigned int Round1Shift[4] = { 3u, 7u, 11u, 19u };
    static const unsigned int Round2Shift[4] = { 3u, 5u, 9u, 13u };
    static const unsigned int Round3Shift[4] = { 3u, 9u, 11u, 15u };
    unsigned int A = State[0];
    unsigned int B = State[1];
    unsigned int C = State[2];
    unsigned int D = State[3];
    unsigned int Step;

    for (Step = 0u; Step < 16u; Step = Step + 1u)
    {
        Md4Step(A, B, C, D, Md4F(B, C, D) + Block[Step], Round1Shift[Step & 3u]);
    }
    for (Step = 0u; Step < 16u; Step = Step + 1u)
    {
        Md4Step(A, B, C, D, Md4G(B, C, D) + Block[Round2Index[Step]] + 0x5a827999u, Round2Shift[Step & 3u]);
    }
    for (Step = 0u; Step < 16u; Step = Step + 1u)
    {
        Md4Step(A, B, C, D, Md4H(B, C, D) + Block[Round3Index[Step]] + 0x6ed9eba1u, Round3Shift[Step & 3u]);
    }

    State[0] = State[0] + A;
    State[1] = State[1] + B;
    State[2] = State[2] + C;
    State[3] = State[3] + D;
}

// 0x06aced90.
void __stdcall Md4Init(FMd4Context* Context)
{
    Context->BitCountLow = 0u;
    Context->BitCountHigh = 0u;
    Context->State[0] = 0x67452301u;
    Context->State[1] = 0xefcdab89u;
    Context->State[2] = 0x98badcfeu;
    Context->State[3] = 0x10325476u;
}

// 0x06acedc0. Index is (old bit count >> 3) & 0x3f. The high count increments
// when the low count wraps, then Length>>29 is added either way.
void __stdcall Md4Update(FMd4Context* Context, const unsigned char* Data, unsigned int Length)
{
    unsigned int CountLow = Context->BitCountLow;
    unsigned int Index = (CountLow >> 3) & 0x3fu;
    unsigned int LengthBits = Length * 8u;
    unsigned int Sum = CountLow + LengthBits;
    const unsigned char* Cursor = Data;
    unsigned int Remaining = Length;

    Context->BitCountLow = Sum;
    if (Sum < LengthBits)
    {
        Context->BitCountHigh = Context->BitCountHigh + 1u;
    }
    Context->BitCountHigh = Context->BitCountHigh + (Length >> 29);

    if (Index != 0u)
    {
        unsigned int Filled = Index + Length;
        if (Filled >= 0x40u)
        {
            unsigned int Need = 0x40u - Index;
            CopyDwordsThenBytes(Context->Buffer + Index, Cursor, Need);
            Remaining = Filled - 0x40u;
            Cursor = Cursor + Need;
            Md4Transform(Context->State, reinterpret_cast<const unsigned int*>(Context->Buffer));
            Index = 0u;
        }
    }

    while (Remaining >= 0x40u)
    {
        Md4Transform(Context->State, reinterpret_cast<const unsigned int*>(Cursor));
        Cursor = Cursor + 0x40u;
        Remaining = Remaining - 0x40u;
    }

    if (Remaining != 0u)
    {
        CopyDwordsThenBytes(Context->Buffer + Index, Cursor, Remaining);
    }
}

// 0x06aceea0. Pad length is 0x38-index, or 0x78-index when index >= 0x38.
// The 64-bit bit count is appended, then the four state words land at +0x58.
void __stdcall Md4Final(FMd4Context* Context)
{
    unsigned int Index = (Context->BitCountLow >> 3) & 0x3fu;
    unsigned int PadLength = 0x38u;
    unsigned int* Tail = reinterpret_cast<unsigned int*>(Context->Digest);

    if (Index >= 0x38u)
    {
        PadLength = 0x78u;
    }
    PadLength = PadLength - Index;

    Tail[0] = Context->BitCountLow;
    Tail[1] = Context->BitCountHigh;
    Md4Update(Context, Md4Padding, PadLength);
    Md4Update(Context, Context->Digest, 8u);
    Tail[0] = Context->State[0];
    Tail[1] = Context->State[1];
    Tail[2] = Context->State[2];
    Tail[3] = Context->State[3];
}

// 0x06ace800.
int __stdcall RngInitState(FRngState* State, unsigned int Step, unsigned int Mode, unsigned int Flags)
{
    if (State == 0)
    {
        return 0;
    }
    State->Index = 0u;
    State->Flags = Flags;
    State->Step = Step;
    State->Magic = 0x1423u;
    State->Size = 0x100u;
    State->Mode = Mode;
    return 1;
}

// 0x06ace834. Does not test Buffer or Size for null.
int __stdcall RngQueryBuffer(FRngState* State, unsigned char** Buffer, unsigned int* Size)
{
    if (State == 0 || State->Magic != 0x1423u)
    {
        return 0;
    }
    *Buffer = State->Buffer;
    *Size = State->Size;
    return 1;
}

// 0x06ace860. Bit 0 of the flags byte also absorbs the pool into the digest.
// The stored index is (Step + old index) mod Size, not the cursor used for the XOR.
int __stdcall RngStir(FRngState* State, const void* Data, unsigned int Length)
{
    unsigned int SavedIndex;
    unsigned int Size;
    unsigned int DigestLength;
    unsigned int Running;
    unsigned int ByteIndex;
    const unsigned char* DigestBytes;
    FMd4Context Md4;
    FSha1Context Sha;
    unsigned int ShaDigest[5];

    if (State == 0 || State->Magic != 0x1423u)
    {
        return 0;
    }

    SavedIndex = State->Index;
    Size = State->Size;
    if (State->Mode == 1u)
    {
        DigestLength = 0x10u;
        Md4Init(&Md4);
        Md4Update(&Md4, static_cast<const unsigned char*>(Data), Length);
        if ((State->Flags & 1u) != 0u)
        {
            Md4Update(&Md4, State->Buffer, State->Size);
        }
        Md4Final(&Md4);
        DigestBytes = Md4.Digest;
    }
    else
    {
        DigestLength = 0x14u;
        Sha1Init(&Sha);
        Sha1UpdateUnswapped(&Sha, static_cast<const unsigned char*>(Data), Length);
        if ((State->Flags & 1u) != 0u)
        {
            Sha1UpdateUnswapped(&Sha, State->Buffer, State->Size);
        }
        Sha1Final(&Sha, ShaDigest);
        DigestBytes = reinterpret_cast<const unsigned char*>(ShaDigest);
    }

    Running = SavedIndex;
    ByteIndex = 0u;
    if (DigestLength != 0u)
    {
        do
        {
            if (Running >= Size)
            {
                Running = 0u;
            }
            State->Buffer[Running] = static_cast<unsigned char>(State->Buffer[Running] ^ DigestBytes[ByteIndex]);
            Running = Running + 1u;
            ByteIndex = ByteIndex + 1u;
        } while (ByteIndex < DigestLength);
    }

    State->Index = (State->Step + SavedIndex) % Size;
    return 1;
}

// 0x06ace9f8. lpdwDisposition is a local; retail points it at the PHKEY slot.
int __stdcall RngOpenKey(unsigned int Access, void** Key)
{
    DWORD Disposition = 0;
    LSTATUS Status = RegCreateKeyExA(
        HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Cryptography\\RNG",
        0,
        0,
        0,
        Access,
        0,
        reinterpret_cast<PHKEY>(Key),
        &Disposition);
    if (Status == 0)
    {
        return 1;
    }
    return 0;
}

// 0x06ace96e. Access is 1 (KEY_QUERY_VALUE). Value name is "Seed".
int __stdcall RngReadSeed(unsigned char* Data, unsigned int Size)
{
    HKEY Key = 0;
    int Opened = RngOpenKey(1u, reinterpret_cast<void**>(&Key));
    DWORD Type = 0;
    LSTATUS Status;
    if (Opened == 0)
    {
        return 0;
    }
    Status = RegQueryValueExA(Key, "Seed", 0, &Type, Data, reinterpret_cast<LPDWORD>(&Size));
    RegCloseKey(Key);
    if (Status == 0)
    {
        return 1;
    }
    return 0;
}

// 0x06ace9b5. Access is 2 (KEY_SET_VALUE). Type immediate is 3.
int __stdcall RngWriteSeed(const unsigned char* Data, unsigned int Size)
{
    HKEY Key = 0;
    int Opened = RngOpenKey(2u, reinterpret_cast<void**>(&Key));
    LSTATUS Status;
    if (Opened == 0)
    {
        return 0;
    }
    Status = RegSetValueExA(Key, "Seed", 0, 3u, Data, Size);
    RegCloseKey(Key);
    if (Status == 0)
    {
        return 1;
    }
    return 0;
}

// 0x06acea30.
void __stdcall Rc4Crypt(unsigned char* Box, unsigned int Length, unsigned char* Data)
{
    unsigned int IndexI = Box[0x100];
    unsigned int IndexJ = Box[0x101];
    while (Length != 0u)
    {
        unsigned int Swap;
        unsigned int Other;
        unsigned int Mix;
        IndexI = (IndexI + 1u) & 0xffu;
        Swap = Box[IndexI];
        IndexJ = (IndexJ + Swap) & 0xffu;
        Other = Box[IndexJ];
        Box[IndexI] = static_cast<unsigned char>(Other);
        Box[IndexJ] = static_cast<unsigned char>(Swap);
        Mix = (Swap + Other) & 0xffu;
        *Data = static_cast<unsigned char>(*Data ^ Box[Mix]);
        Data = Data + 1;
        Length = Length - 1u;
    }
    Box[0x100] = static_cast<unsigned char>(IndexI);
    Box[0x101] = static_cast<unsigned char>(IndexJ);
}

// 0x06acea91. The 0x40 dwords are 0x03020100, then +0x04040404 each.
void __stdcall Rc4Init(unsigned char* Box, unsigned int KeyLength, const unsigned char* Key)
{
    unsigned int Pattern = 0x03020100u;
    unsigned int Group;
    unsigned char* Cursor = Box;
    unsigned int Accumulator = 0u;
    unsigned int KeyIndex = 0u;
    unsigned int Index;

    for (Group = 0u; Group < 0x40u; Group = Group + 1u)
    {
        *reinterpret_cast<unsigned int*>(Cursor) = Pattern;
        Cursor = Cursor + 4;
        Pattern = Pattern + 0x04040404u;
    }
    Box[0x100] = 0;
    Box[0x101] = 0;

    for (Index = 0u; Index < 0x100u; Index = Index + 1u)
    {
        unsigned int Swap = Box[Index];
        Accumulator = (Accumulator + Key[KeyIndex] + Swap) & 0xffu;
        KeyIndex = KeyIndex + 1u;
        Box[Index] = Box[Accumulator];
        Box[Accumulator] = static_cast<unsigned char>(Swap);
        if (KeyIndex == KeyLength)
        {
            KeyIndex = 0u;
        }
    }
}

// 0x06aceb0f. Quarter is Length>>2 bytes. Returns 0 when Blocks, Data, or Quarter is 0.
// Each of the eight hashes is SHA-1 init / update-unswapped / final (0x06ac9250, 0x06ac9360, 0x06ac9430).
// After the fold, 29 dwords of the SHA context and 20 dwords of the intermediate digests are cleared.
int __stdcall MixShaBlocks(const unsigned char* Data, unsigned int Length, unsigned char* Blocks)
{
    unsigned int Quarter;
    const unsigned char* Slice0;
    const unsigned char* Slice1;
    const unsigned char* Slice2;
    const unsigned char* Slice3;
    unsigned char* Block0;
    unsigned char* Block1;
    unsigned char* Block2;
    unsigned char* Block3;
    FSha1Context Sha;
    unsigned int Folded[20];
    unsigned int* Fold0;
    unsigned int* Fold1;
    unsigned int* Fold2;
    unsigned int* Fold3;
    unsigned int Word;
    unsigned int* ContextWords;

    if (Blocks == 0 || Data == 0)
    {
        return 0;
    }
    Quarter = Length >> 2;
    if (Quarter == 0u)
    {
        return 0;
    }

    Slice0 = Data;
    Slice1 = Data + Quarter;
    Slice2 = Slice1 + Quarter;
    Slice3 = Slice2 + Quarter;
    Block0 = Blocks;
    Block1 = Blocks + 0x14;
    Block2 = Blocks + 0x28;
    Block3 = Blocks + 0x3c;
    Fold0 = Folded;
    Fold1 = Folded + 5;
    Fold2 = Folded + 10;
    Fold3 = Folded + 15;

    Sha1Init(&Sha);
    Sha1UpdateUnswapped(&Sha, Block0, 0x14u);
    Sha1UpdateUnswapped(&Sha, Slice0, Quarter);
    Sha1UpdateUnswapped(&Sha, Block1, 0x14u);
    Sha1UpdateUnswapped(&Sha, Slice1, Quarter);
    Sha1Final(&Sha, Fold0);

    Sha1Init(&Sha);
    Sha1UpdateUnswapped(&Sha, Block1, 0x14u);
    Sha1UpdateUnswapped(&Sha, Slice1, Quarter);
    Sha1UpdateUnswapped(&Sha, Block0, 0x14u);
    Sha1UpdateUnswapped(&Sha, Slice0, Quarter);
    Sha1Final(&Sha, Fold1);

    Sha1Init(&Sha);
    Sha1UpdateUnswapped(&Sha, Block2, 0x14u);
    Sha1UpdateUnswapped(&Sha, Slice2, Quarter);
    Sha1UpdateUnswapped(&Sha, Block3, 0x14u);
    Sha1UpdateUnswapped(&Sha, Slice3, Quarter);
    Sha1Final(&Sha, Fold2);

    Sha1Init(&Sha);
    Sha1UpdateUnswapped(&Sha, Block3, 0x14u);
    Sha1UpdateUnswapped(&Sha, Slice3, Quarter);
    Sha1UpdateUnswapped(&Sha, Block2, 0x14u);
    Sha1UpdateUnswapped(&Sha, Slice2, Quarter);
    Sha1Final(&Sha, Fold3);

    Sha1Init(&Sha);
    Sha1UpdateUnswapped(&Sha, reinterpret_cast<const unsigned char*>(Fold0), 0x14u);
    Sha1UpdateUnswapped(&Sha, reinterpret_cast<const unsigned char*>(Fold2), 0x14u);
    Sha1Final(&Sha, reinterpret_cast<unsigned int*>(Block0));

    Sha1Init(&Sha);
    Sha1UpdateUnswapped(&Sha, reinterpret_cast<const unsigned char*>(Fold1), 0x14u);
    Sha1UpdateUnswapped(&Sha, reinterpret_cast<const unsigned char*>(Fold3), 0x14u);
    Sha1Final(&Sha, reinterpret_cast<unsigned int*>(Block1));

    Sha1Init(&Sha);
    Sha1UpdateUnswapped(&Sha, reinterpret_cast<const unsigned char*>(Fold2), 0x14u);
    Sha1UpdateUnswapped(&Sha, reinterpret_cast<const unsigned char*>(Fold0), 0x14u);
    Sha1Final(&Sha, reinterpret_cast<unsigned int*>(Block2));

    Sha1Init(&Sha);
    Sha1UpdateUnswapped(&Sha, reinterpret_cast<const unsigned char*>(Fold3), 0x14u);
    Sha1UpdateUnswapped(&Sha, reinterpret_cast<const unsigned char*>(Fold1), 0x14u);
    Sha1Final(&Sha, reinterpret_cast<unsigned int*>(Block3));

    ContextWords = reinterpret_cast<unsigned int*>(&Sha);
    for (Word = 0u; Word < 0x1du; Word = Word + 1u)
    {
        ContextWords[Word] = 0u;
    }
    for (Word = 0u; Word < 20u; Word = Word + 1u)
    {
        Folded[Word] = 0u;
    }
    return 1;
}

// 0x06aced89. Caller at 0x06accf59 pushes a buffer and a DWORD* (0x40) and tests EAX.
int __stdcall RngTryReadBlock(void*, unsigned int*)
{
    return 0;
}
