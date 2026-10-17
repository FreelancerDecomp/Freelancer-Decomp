// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "MovieSha.h"

#include <stddef.h>
#include <windows.h>

static_assert(sizeof(FSha1Context) == 0x74u, "SHA-1 context is 0x74 bytes");
static_assert(offsetof(FSha1Context, State) == 0x18u, "SHA-1 state");
static_assert(offsetof(FSha1Context, ByteCountHigh) == 0x2cu, "SHA-1 count high");
static_assert(offsetof(FSha1Context, ByteCountLow) == 0x30u, "SHA-1 count low");
static_assert(offsetof(FSha1Context, Block) == 0x34u, "SHA-1 partial block");

static unsigned int RotateLeft(unsigned int Value, unsigned int Bits)
{
    return (Value << Bits) | (Value >> (32u - Bits));
}

static unsigned int RotateRight(unsigned int Value, unsigned int Bits)
{
    return (Value >> Bits) | (Value << (32u - Bits));
}

// 32 trips, two words each. Listing at 06acb3b0 / 06acbf05:
// W[i] = ROL1(W[i-16] ^ W[i-14] ^ W[i-8] ^ W[i-3]), i = 16..79.
static void ExpandSchedule(unsigned int* Schedule)
{
    unsigned int Round;
    for (Round = 16u; Round < 80u; Round = Round + 2u)
    {
        unsigned int First = Schedule[Round - 16u] ^ Schedule[Round - 14u] ^ Schedule[Round - 8u] ^ Schedule[Round - 3u];
        unsigned int Second = Schedule[Round - 15u] ^ Schedule[Round - 13u] ^ Schedule[Round - 7u] ^ Schedule[Round - 2u];
        Schedule[Round] = RotateLeft(First, 1u);
        Schedule[Round + 1u] = RotateLeft(Second, 1u);
    }
}

// Rounds 0..79. The listing unrolls them; both retail bodies are the same
// 988 instructions from the state load through the instruction before ADD ESP.
// Ch / Parity / Majority and the four K constants change on the phase edges.
// Each round does ROL5(A), ROR2(B) as two ROR1, then A,B,C,D,E rotate.
static void CompressSchedule(unsigned int* State, const unsigned int* Schedule)
{
    unsigned int A = State[0];
    unsigned int B = State[1];
    unsigned int C = State[2];
    unsigned int D = State[3];
    unsigned int E = State[4];
    unsigned int Round;

    for (Round = 0u; Round < 20u; Round = Round + 1u)
    {
        unsigned int Choice = (B & (C ^ D)) ^ D;
        unsigned int Sum = RotateLeft(A, 5u) + Choice + E + Schedule[Round] + 0x5A827999u;
        E = D;
        D = C;
        C = RotateRight(B, 2u);
        B = A;
        A = Sum;
    }
    for (Round = 20u; Round < 40u; Round = Round + 1u)
    {
        unsigned int Parity = B ^ C ^ D;
        unsigned int Sum = RotateLeft(A, 5u) + Parity + E + Schedule[Round] + 0x6ED9EBA1u;
        E = D;
        D = C;
        C = RotateRight(B, 2u);
        B = A;
        A = Sum;
    }
    for (Round = 40u; Round < 60u; Round = Round + 1u)
    {
        unsigned int Majority = (B & C) | ((B | C) & D);
        unsigned int Sum = RotateLeft(A, 5u) + Majority + E + Schedule[Round] + 0x8F1BBCDCu;
        E = D;
        D = C;
        C = RotateRight(B, 2u);
        B = A;
        A = Sum;
    }
    for (Round = 60u; Round < 80u; Round = Round + 1u)
    {
        unsigned int Parity = B ^ C ^ D;
        unsigned int Sum = RotateLeft(A, 5u) + Parity + E + Schedule[Round] + 0xCA62C1D6u;
        E = D;
        D = C;
        C = RotateRight(B, 2u);
        B = A;
        A = Sum;
    }

    State[0] = State[0] + A;
    State[1] = State[1] + B;
    State[2] = State[2] + C;
    State[3] = State[3] + D;
    State[4] = State[4] + E;
}

// 0x06acb360. RET 8.
void __stdcall Sha1CompressSwapped(unsigned int* State, const unsigned int* Block)
{
    unsigned int Schedule[80];
    unsigned int Round;
    for (Round = 0u; Round < 16u; Round = Round + 1u)
    {
        unsigned int Value = Block[Round];
        unsigned int High = Value & 0xff00ff00u;
        unsigned int Low = Value & 0x00ff00ffu;
        Schedule[Round] = RotateLeft(High, 8u) ^ RotateRight(Low, 8u);
    }
    ExpandSchedule(Schedule);
    CompressSchedule(State, Schedule);
}

// 0x06acbedd. RET 8.
void __stdcall Sha1CompressUnswapped(unsigned int* State, const unsigned int* Block)
{
    unsigned int Schedule[80];
    unsigned int Round;
    for (Round = 0u; Round < 16u; Round = Round + 1u)
    {
        Schedule[Round] = Block[Round];
    }
    ExpandSchedule(Schedule);
    CompressSchedule(State, Schedule);
}

static void CopyBytes(unsigned char* Dest, const unsigned char* Source, unsigned int Count)
{
    unsigned int Index;
    for (Index = 0u; Index < Count; Index = Index + 1u)
    {
        Dest[Index] = Source[Index];
    }
}

// x86 MOV dword, including when the address is not 4-aligned.
static void StoreWord(unsigned char* Dest, unsigned int Value)
{
    Dest[0] = static_cast<unsigned char>(Value & 0xffu);
    Dest[1] = static_cast<unsigned char>((Value >> 8) & 0xffu);
    Dest[2] = static_cast<unsigned char>((Value >> 16) & 0xffu);
    Dest[3] = static_cast<unsigned char>((Value >> 24) & 0xffu);
}

// 0x06ac9250. RET 4.
void __stdcall Sha1Init(FSha1Context* Context)
{
    Context->ByteCountHigh = 0u;
    Context->ByteCountLow = 0u;
    Context->State[0] = 0x67452301u;
    Context->State[1] = 0xefcdab89u;
    Context->State[2] = 0x98badcfeu;
    Context->State[3] = 0x10325476u;
    Context->State[4] = 0xc3d2e1f0u;
}

// 0x06ac9290. RET 0xC. The 195-byte body matches 0x06ac9360 except the two
// calls, which are Sha1CompressSwapped (0x06acb360).
void __stdcall Sha1UpdateSwapped(FSha1Context* Context, const unsigned char* Data, unsigned int Length)
{
    unsigned int NewLow = Context->ByteCountLow + Length;
    unsigned int Offset = Context->ByteCountLow & 0x3fu;
    Context->ByteCountLow = NewLow;
    if (NewLow < Length)
    {
        Context->ByteCountHigh = Context->ByteCountHigh + 1u;
    }

    if (Offset != 0u)
    {
        unsigned int Filled = Offset + Length;
        if (Filled >= 0x40u)
        {
            unsigned int Need = 0x40u - Offset;
            CopyBytes(Context->Block + Offset, Data, Need);
            Length = Filled - 0x40u;
            Data = Data + Need;
            Sha1CompressSwapped(Context->State, reinterpret_cast<const unsigned int*>(Context->Block));
            Offset = 0u;
        }
    }

    if (Length >= 0x40u)
    {
        unsigned int Blocks = Length >> 6;
        do
        {
            Sha1CompressSwapped(Context->State, reinterpret_cast<const unsigned int*>(Data));
            Data = Data + 0x40u;
            Length = Length - 0x40u;
            Blocks = Blocks - 1u;
        } while (Blocks != 0u);
    }

    if (Length != 0u)
    {
        CopyBytes(Context->Block + Offset, Data, Length);
    }
}

// 0x06ac9360. RET 0xC. Same body as 0x06ac9290. Both calls are
// Sha1CompressUnswapped (0x06acbedd).
void __stdcall Sha1UpdateUnswapped(FSha1Context* Context, const unsigned char* Data, unsigned int Length)
{
    unsigned int NewLow = Context->ByteCountLow + Length;
    unsigned int Offset = Context->ByteCountLow & 0x3fu;
    Context->ByteCountLow = NewLow;
    if (NewLow < Length)
    {
        Context->ByteCountHigh = Context->ByteCountHigh + 1u;
    }

    if (Offset != 0u)
    {
        unsigned int Filled = Offset + Length;
        if (Filled >= 0x40u)
        {
            unsigned int Need = 0x40u - Offset;
            CopyBytes(Context->Block + Offset, Data, Need);
            Length = Filled - 0x40u;
            Data = Data + Need;
            Sha1CompressUnswapped(Context->State, reinterpret_cast<const unsigned int*>(Context->Block));
            Offset = 0u;
        }
    }

    if (Length >= 0x40u)
    {
        unsigned int Blocks = Length >> 6;
        do
        {
            Sha1CompressUnswapped(Context->State, reinterpret_cast<const unsigned int*>(Data));
            Data = Data + 0x40u;
            Length = Length - 0x40u;
            Blocks = Blocks - 1u;
        } while (Blocks != 0u);
    }

    if (Length != 0u)
    {
        CopyBytes(Context->Block + Offset, Data, Length);
    }
}

// 0x06ac9430. RET 8. The padding block is absorbed by the swapped update.
void __stdcall Sha1Final(FSha1Context* Context, unsigned int* Digest)
{
    unsigned int Low = Context->ByteCountLow;
    unsigned int Index = Low & 0x3fu;
    unsigned int PadLength = 0x40u - Index;
    unsigned int ZeroCount;
    unsigned int Zero;
    unsigned char Pad[0x48];
    unsigned int BitHigh;
    unsigned int BitLow;

    if (PadLength <= 8u)
    {
        PadLength = PadLength + 0x40u;
    }

    ZeroCount = PadLength - 6u;
    for (Zero = 0u; Zero < ZeroCount; Zero = Zero + 1u)
    {
        Pad[Zero] = 0;
    }
    Pad[0] = 0x80u;

    BitHigh = (Context->ByteCountHigh << 3) | (Low >> 29);
    BitLow = Low * 8u;
    StoreWord(Pad + (PadLength - 8u), BitHigh);
    StoreWord(Pad + (PadLength - 4u), BitLow);

    Sha1UpdateSwapped(Context, Pad, PadLength);

    Digest[0] = Context->State[0];
    Digest[1] = Context->State[1];
    Digest[2] = Context->State[2];
    Digest[3] = Context->State[3];
    Digest[4] = Context->State[4];

    Sha1Init(Context);
}
