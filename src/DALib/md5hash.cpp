// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "md5hash.h"

#include <stdio.h>
#include <string.h>

namespace
{
    inline unsigned long RotateLeft(unsigned long Value, unsigned long Bits)
    {
        return (Value << Bits) | (Value >> (32 - Bits));
    }

    inline unsigned long F(unsigned long X, unsigned long Y, unsigned long Z)
    {
        return (X & Y) | (~X & Z);
    }

    inline unsigned long G(unsigned long X, unsigned long Y, unsigned long Z)
    {
        return (X & Z) | (Y & ~Z);
    }

    inline unsigned long H(unsigned long X, unsigned long Y, unsigned long Z)
    {
        return X ^ Y ^ Z;
    }

    inline unsigned long I(unsigned long X, unsigned long Y, unsigned long Z)
    {
        return Y ^ (X | ~Z);
    }
}

MD5Hash::MD5Hash()
{
    State[0] = 0x67452301;
    State[1] = 0xEFCDAB89;
    State[2] = 0x98BADCFE;
    State[3] = 0x10325476;
    BitCountLow = 0;
    BitCountHigh = 0;
    StringValue[0] = '\0';
}

// Header-defined copy members and GetStatePtr are emitted by the exported class.

MD5Hash::~MD5Hash() = default;

// .text:065c67d0 (RET 0xc): Length is a BYTE count. Length 0 -> nothing (JBE); otherwise one
// word per step, low byte first, while the byte offset (a multiple of 4) is below Length --
// a Length that is not a multiple of 4 still writes the whole last word, as retail does.
void MD5Hash::Encode(unsigned char* Output, unsigned long* Input, unsigned long Length)
{
    for (unsigned long Offset = 0; Offset < Length; Offset += 4)
    {
        const unsigned long Word = Input[Offset / 4];
        Output[Offset] = (unsigned char)(Word & 0xFF);
        Output[Offset + 1] = (unsigned char)((Word >> 8) & 0xFF);
        Output[Offset + 2] = (unsigned char)((Word >> 16) & 0xFF);
        Output[Offset + 3] = (unsigned char)((Word >> 24) & 0xFF);
    }
}

// .text:065c6820 (RET 0xc): Length is a BYTE count, the same loop shape as Encode -- one
// little-endian word per 4 input bytes while the byte offset is below Length.
void MD5Hash::Decode(unsigned long* Output, unsigned char* Input, unsigned long Length)
{
    for (unsigned long Offset = 0; Offset < Length; Offset += 4)
    {
        Output[Offset / 4] = (unsigned long)(Input[Offset]) |
                             ((unsigned long)(Input[Offset + 1]) << 8) |
                             ((unsigned long)(Input[Offset + 2]) << 16) |
                             ((unsigned long)(Input[Offset + 3]) << 24);
    }
}

// Decode one 64-byte block, mix all MD5 rounds and accumulate the four state words.
void MD5Hash::Transform(unsigned char* Block)
{
    unsigned long A = State[0];
    unsigned long B = State[1];
    unsigned long C = State[2];
    unsigned long D = State[3];
    unsigned long X[16];
    Decode(X, Block, 64);

#define STEP(Func, ARef, BRef, CRef, DRef, XIndex, Sin, Shift) \
    ARef += Func(BRef, CRef, DRef) + X[XIndex] + Sin;         \
    ARef = RotateLeft(ARef, Shift) + BRef

    STEP(F, A, B, C, D, 0, 0xD76AA478, 7);
    STEP(F, D, A, B, C, 1, 0xE8C7B756, 12);
    STEP(F, C, D, A, B, 2, 0x242070DB, 17);
    STEP(F, B, C, D, A, 3, 0xC1BDCEEE, 22);
    STEP(F, A, B, C, D, 4, 0xF57C0FAF, 7);
    STEP(F, D, A, B, C, 5, 0x4787C62A, 12);
    STEP(F, C, D, A, B, 6, 0xA8304613, 17);
    STEP(F, B, C, D, A, 7, 0xFD469501, 22);
    STEP(F, A, B, C, D, 8, 0x698098D8, 7);
    STEP(F, D, A, B, C, 9, 0x8B44F7AF, 12);
    STEP(F, C, D, A, B, 10, 0xFFFF5BB1, 17);
    STEP(F, B, C, D, A, 11, 0x895CD7BE, 22);
    STEP(F, A, B, C, D, 12, 0x6B901122, 7);
    STEP(F, D, A, B, C, 13, 0xFD987193, 12);
    STEP(F, C, D, A, B, 14, 0xA679438E, 17);
    STEP(F, B, C, D, A, 15, 0x49B40821, 22);

    STEP(G, A, B, C, D, 1, 0xF61E2562, 5);
    STEP(G, D, A, B, C, 6, 0xC040B340, 9);
    STEP(G, C, D, A, B, 11, 0x265E5A51, 14);
    STEP(G, B, C, D, A, 0, 0xE9B6C7AA, 20);
    STEP(G, A, B, C, D, 5, 0xD62F105D, 5);
    STEP(G, D, A, B, C, 10, 0x02441453, 9);
    STEP(G, C, D, A, B, 15, 0xD8A1E681, 14);
    STEP(G, B, C, D, A, 4, 0xE7D3FBC8, 20);
    STEP(G, A, B, C, D, 9, 0x21E1CDE6, 5);
    STEP(G, D, A, B, C, 14, 0xC33707D6, 9);
    STEP(G, C, D, A, B, 3, 0xF4D50D87, 14);
    STEP(G, B, C, D, A, 8, 0x455A14ED, 20);
    STEP(G, A, B, C, D, 13, 0xA9E3E905, 5);
    STEP(G, D, A, B, C, 2, 0xFCEFA3F8, 9);
    STEP(G, C, D, A, B, 7, 0x676F02D9, 14);
    STEP(G, B, C, D, A, 12, 0x8D2A4C8A, 20);

    STEP(H, A, B, C, D, 5, 0xFFFA3942, 4);
    STEP(H, D, A, B, C, 8, 0x8771F681, 11);
    STEP(H, C, D, A, B, 11, 0x6D9D6122, 16);
    STEP(H, B, C, D, A, 14, 0xFDE5380C, 23);
    STEP(H, A, B, C, D, 1, 0xA4BEEA44, 4);
    STEP(H, D, A, B, C, 4, 0x4BDECFA9, 11);
    STEP(H, C, D, A, B, 7, 0xF6BB4B60, 16);
    STEP(H, B, C, D, A, 10, 0xBEBFBC70, 23);
    STEP(H, A, B, C, D, 13, 0x289B7EC6, 4);
    STEP(H, D, A, B, C, 0, 0xEAA127FA, 11);
    STEP(H, C, D, A, B, 3, 0xD4EF3085, 16);
    STEP(H, B, C, D, A, 6, 0x04881D05, 23);
    STEP(H, A, B, C, D, 9, 0xD9D4D039, 4);
    STEP(H, D, A, B, C, 12, 0xE6DB99E5, 11);
    STEP(H, C, D, A, B, 15, 0x1FA27CF8, 16);
    STEP(H, B, C, D, A, 2, 0xC4AC5665, 23);

    STEP(I, A, B, C, D, 0, 0xF4292244, 6);
    STEP(I, D, A, B, C, 7, 0x432AFF97, 10);
    STEP(I, C, D, A, B, 14, 0xAB9423A7, 15);
    STEP(I, B, C, D, A, 5, 0xFC93A039, 21);
    STEP(I, A, B, C, D, 12, 0x655B59C3, 6);
    STEP(I, D, A, B, C, 3, 0x8F0CCC92, 10);
    STEP(I, C, D, A, B, 10, 0xFFEFF47D, 15);
    STEP(I, B, C, D, A, 1, 0x85845DD1, 21);
    STEP(I, A, B, C, D, 8, 0x6FA87E4F, 6);
    STEP(I, D, A, B, C, 15, 0xFE2CE6E0, 10);
    STEP(I, C, D, A, B, 6, 0xA3014314, 15);
    STEP(I, B, C, D, A, 13, 0x4E0811A1, 21);
    STEP(I, A, B, C, D, 4, 0xF7537E82, 6);
    STEP(I, D, A, B, C, 11, 0xBD3AF235, 10);
    STEP(I, C, D, A, B, 2, 0x2AD7D2BB, 15);
    STEP(I, B, C, D, A, 9, 0xEB86D391, 21);

#undef STEP

    State[0] += A;
    State[1] += B;
    State[2] += C;
    State[3] += D;
}

// Retail .text:065c6870..6926. No null/zero-length early-out (the port used to have one; retail
// runs the counter update and a zero-byte copy for Length 0, and would fault on a null Data).
bool MD5Hash::AddData(void* const Data, unsigned long Length)
{
    unsigned char* const Input = (unsigned char*)Data;
    unsigned long BitCount = BitCountLow;
    BitCountLow += Length << 3;
    if (BitCountLow < BitCount)
        BitCountHigh += 1;
    BitCountHigh += Length >> 29;

    unsigned long BufferIndex = (BitCount >> 3) & 0x3F;
    const unsigned long PartLength = 64 - BufferIndex;
    // Index = EDI in retail: bytes already consumed from Input.
    unsigned long Index = 0;
    if (Length >= PartLength)
    {
        memcpy(Buffer + BufferIndex, Input, PartLength);
        Transform(Buffer);
        // .text:065c68d4..68f9: ESI = Index + 0x3f, loop while ESI < Length.
        for (Index = PartLength; Index + 63 < Length; Index += 64)
            Transform(Input + Index);
        BufferIndex = 0;
    }

    // .text:065c6905..691e: the tail starts at Input + Index (after the whole 64-byte blocks),
    // not at Input + PartLength -- the port used PartLength here, which re-buffered already
    // transformed blocks for any input longer than PartLength + 63 bytes.
    memcpy(Buffer + BufferIndex, Input + Index, Length - Index);
    return true;
}

// DAT_065ca068: initialised .data, 0x80 then zeros (the only reader is CalcValue).
static unsigned char G_Md5Padding[64] = {0x80};

// Append padding and the original 64-bit bit count.
bool MD5Hash::CalcValue()
{
    unsigned char Bits[8];
    Encode(Bits, &BitCountLow, 8);

    const unsigned long BufferIndex = (BitCountLow >> 3) & 0x3F;
    const unsigned long PadLength = (BufferIndex < 56) ? (56 - BufferIndex) : (120 - BufferIndex);
    AddData(G_Md5Padding, PadLength);
    AddData(Bits, 8);
    return true;
}

// Retail .text:065c69b0: sprintf(this+0x5c, "%08x-%08x-%08x-%08x", +4, +8, +0xc, +0x10);
// returns this+0x5c.
const char* MD5Hash::AsString()
{
    sprintf(StringValue, "%08x-%08x-%08x-%08x", State[0], State[1], State[2], State[3]);
    return StringValue;
}

// Retail .text:065c69e0: sscanf(Text, "%x-%x-%x-%x", &+4, &+8, &+0xc, &+0x10) -- no NULL check.
void MD5Hash::FromString(const char* Text)
{
    sscanf(Text, "%x-%x-%x-%x", &State[0], &State[1], &State[2], &State[3]);
}

// .text:065c6a10 (RET 4): REPE CMPSD over the four state words of both objects, SETZ.
bool MD5Hash::Compare(MD5Hash& Other)
{
    return State[0] == Other.State[0] && State[1] == Other.State[1] &&
           State[2] == Other.State[2] && State[3] == Other.State[3];
}
