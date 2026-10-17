// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "msvcrt_atof.h"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <locale.h>

namespace
{
    struct FExtendedDecimal
    {
        unsigned short Mantissa[5]; // Explicit 80-bit significand, least significant word first.
        int Exponent;              // Bias 0x3fff; sign is applied after conversion.
    };

    // MSVCRT's 12-byte decimal power tables (SysWOW64 file RVAs 0xb5410/0xb5570).
    // Five words of mantissa followed by the biased exponent. The table is indexed
    // in groups of seven: 10^(1..7), 10^(8,16..56), then powers of 64,512,4096.
    static unsigned short const GPositivePowers[29][6] = {
        {0x0000, 0x0000, 0x0000, 0x0000, 0xa000, 0x4002},
        {0x0000, 0x0000, 0x0000, 0x0000, 0xc800, 0x4005},
        {0x0000, 0x0000, 0x0000, 0x0000, 0xfa00, 0x4008},
        {0x0000, 0x0000, 0x0000, 0x0000, 0x9c40, 0x400c},
        {0x0000, 0x0000, 0x0000, 0x0000, 0xc350, 0x400f},
        {0x0000, 0x0000, 0x0000, 0x0000, 0xf424, 0x4012},
        {0x0000, 0x0000, 0x0000, 0x8000, 0x9896, 0x4016},
        {0x0000, 0x0000, 0x0000, 0x2000, 0xbebc, 0x4019},
        {0x0000, 0x0000, 0x0400, 0xc9bf, 0x8e1b, 0x4034},
        {0x0000, 0xa100, 0xcced, 0x1bce, 0xd3c2, 0x404e},
        {0xf020, 0xb59e, 0x2b70, 0xada8, 0x9dc5, 0x4069},
        {0x5dd0, 0x25fd, 0x1ae5, 0x4f8e, 0xeb19, 0x4083},
        {0x9671, 0x95d7, 0x0e43, 0x8d05, 0xaf29, 0x409e},
        {0xbff9, 0x44a0, 0x81ed, 0x8f12, 0x8281, 0x40b9},
        {0x3cbf, 0xa6d5, 0xffcf, 0x1f49, 0xc278, 0x40d3},
        {0xc66f, 0x8ce0, 0x80e9, 0x47c9, 0x93ba, 0x41a8},
        {0x85bc, 0x556b, 0x3927, 0xf78d, 0xe070, 0x427c},
        {0xddbc, 0xde8e, 0x9df9, 0xebfb, 0xaa7e, 0x4351},
        {0xe6a1, 0xe376, 0xf2cc, 0x2f29, 0x8184, 0x4426},
        {0x1028, 0xaa17, 0xaef8, 0xe310, 0xc4c5, 0x44fa},
        {0xa7eb, 0xf3d4, 0xebf7, 0x4ae1, 0x957a, 0x45cf},
        {0xcc65, 0x91c7, 0xa60e, 0xa0ae, 0xe319, 0x46a3},
        {0x650d, 0x0c17, 0x8175, 0x7586, 0xc976, 0x4d48},
        {0x4258, 0xa7e4, 0x3993, 0x353b, 0xb2b8, 0x53ed},
        {0xa74d, 0x5de5, 0xc53d, 0x3b5d, 0x9e8b, 0x5a92},
        {0x5dff, 0xf0a6, 0x20a1, 0x54c0, 0x8ca5, 0x6137},
        {0xfdd1, 0x5a8b, 0xd88b, 0x5d25, 0xf989, 0x67db},
        {0x95aa, 0xf3f8, 0xbf27, 0xc8a2, 0xdd5d, 0x6e80},
        {0xc94c, 0x979b, 0x8a20, 0x5202, 0xc460, 0x7525},
    };

    static unsigned short const GNegativePowers[29][6] = {
        {0xcccd, 0xcccd, 0xcccc, 0xcccc, 0xcccc, 0x3ffb},
        {0x3d71, 0xd70a, 0x70a3, 0x0a3d, 0xa3d7, 0x3ff8},
        {0x645a, 0xdf3b, 0x8d4f, 0x6e97, 0x8312, 0x3ff5},
        {0xd3c3, 0x652c, 0xe219, 0x1758, 0xd1b7, 0x3ff1},
        {0x0fd0, 0x8423, 0x1b47, 0xac47, 0xa7c5, 0x3fee},
        {0xa640, 0x69b6, 0xaf6c, 0xbd05, 0x8637, 0x3feb},
        {0x3d33, 0x42bc, 0xe57a, 0x94d5, 0xd6bf, 0x3fe7},
        {0xfdc2, 0xcefd, 0x8461, 0x7711, 0xabcc, 0x3fe4},
        {0x4c2f, 0xe15b, 0xc44d, 0x94be, 0xe695, 0x3fc9},
        {0xc492, 0x3b53, 0x4475, 0x14cd, 0x9abe, 0x3faf},
        {0x67de, 0x94ba, 0x4539, 0x1ead, 0xcfb1, 0x3f94},
        {0x2324, 0xe2c6, 0xbabc, 0x313b, 0x8b61, 0x3f7a},
        {0x5561, 0xc159, 0xb17e, 0x7c53, 0xbb12, 0x3f5f},
        {0xeed7, 0x8d2f, 0xbe06, 0x8592, 0xfb15, 0x3f44},
        {0x3f24, 0xe9a5, 0xa539, 0xea27, 0xa87f, 0x3f2a},
        {0xac7d, 0xe4a1, 0x64bc, 0x467c, 0xddd0, 0x3e55},
        {0x7b63, 0xcc06, 0x5423, 0x8377, 0x91ff, 0x3d81},
        {0xfa91, 0x193a, 0x637a, 0x4325, 0xc031, 0x3cac},
        {0x8921, 0x38d1, 0x4782, 0xb897, 0xfd00, 0x3bd7},
        {0x88dc, 0x0858, 0xb11b, 0xe3e8, 0xa686, 0x3b03},
        {0x84c6, 0x4245, 0xb607, 0x7599, 0xdb37, 0x3a2e},
        {0x7133, 0xd21c, 0xdb23, 0xee32, 0x9049, 0x395a},
        {0x87a6, 0xc0be, 0xda57, 0x82a5, 0xa2a6, 0x32b5},
        {0x68e2, 0x11b2, 0x52a7, 0x449f, 0xb759, 0x2c10},
        {0x4925, 0x2de4, 0x3436, 0x534f, 0xceae, 0x256b},
        {0x598f, 0xa404, 0xdec0, 0x7dc2, 0xe8fb, 0x1ec6},
        {0xe79e, 0x5a88, 0x9157, 0xbf3c, 0x8350, 0x1822},
        {0x4b4e, 0x6265, 0x83fd, 0xaf8f, 0x9406, 0x117d},
        {0x2de4, 0x9fde, 0xd2ce, 0x04c8, 0xa6dd, 0x0ad8},
    };

    void ShiftLeft(unsigned short* Words, unsigned int Count)
    {
        unsigned int Carry = 0;
        for (unsigned int Index = 0; Index < Count; ++Index)
        {
            unsigned int Value = (Words[Index] << 1) | Carry;
            Words[Index] = (unsigned short)Value; // keep the low 16 bits; bit 16 is the carry
            Carry = Value >> 16;
        }
    }

    void Multiply(FExtendedDecimal& Left, FExtendedDecimal const& Right)
    {
        int Exponent = Left.Exponent + Right.Exponent - 0x3ffe;
        if (Left.Exponent >= 0x7fff || Right.Exponent >= 0x7fff || Exponent >= 0x7fff)
        {
            memset(Left.Mantissa, 0, sizeof(Left.Mantissa));
            Left.Exponent = 0x7fff;
            return;
        }
        if (Exponent <= -63)
        {
            memset(Left.Mantissa, 0, sizeof(Left.Mantissa));
            Left.Exponent = 0;
            return;
        }

        // Preserve the legacy convolution: products below bit64 are not summed.
        // Rounding and the 16-bit carry increment follow the captured CRT algorithm.
        unsigned short Product[6] = {};
        for (unsigned int Column = 0; Column < 5; ++Column)
        {
            for (unsigned int Index = Column; Index < 5; ++Index)
            {
                // 16x16 -> 32-bit unsigned product (as int it could overflow).
                unsigned int Term =
                    (unsigned int)Left.Mantissa[Index] * Right.Mantissa[Column + 4 - Index];
                // Word pair as one dword; the high word is widened before the shift.
                unsigned int Previous = Product[Column] | ((unsigned int)Product[Column + 1] << 16);
                unsigned int Sum = Previous + Term;
                Product[Column] = (unsigned short)Sum; // low word
                Product[Column + 1] = Sum >> 16;
                if (Sum < Previous)
                    ++Product[Column + 2];
            }
        }
        while (Exponent > 0 && (Product[5] & 0x8000u) == 0)
        {
            ShiftLeft(Product, 6);
            --Exponent;
        }
        if (Exponent <= 0)
        {
            // Such extended subnormals are already below the double range.
            memset(Left.Mantissa, 0, sizeof(Left.Mantissa));
            Left.Exponent = 0;
            return;
        }
        if (Product[0] > 0x8000u || (Product[0] == 0x8000u && (Product[1] & 1u)))
        {
            unsigned int Index = 1;
            while (Index < 6 && ++Product[Index] == 0)
                ++Index;
            if (Index == 6)
            {
                Product[5] = 0x8000;
                ++Exponent;
            }
        }
        for (unsigned int Index = 0; Index < 5; ++Index)
            Left.Mantissa[Index] = Product[Index + 1];
        Left.Exponent = Exponent;
    }

    void MultiplyPower(FExtendedDecimal& Value, int Power)
    {
        bool const bNegative = Power < 0;
        unsigned int Remaining = bNegative ? -Power : Power;
        unsigned int Group = 0;
        while (Remaining != 0)
        {
            unsigned int Digit = Remaining & 7u;
            Remaining >>= 3;
            if (Digit != 0)
            {
                unsigned short const* Row = bNegative ? GNegativePowers[Group + Digit - 1] :
                    GPositivePowers[Group + Digit - 1];
                FExtendedDecimal Factor;
                memcpy(Factor.Mantissa, Row, sizeof(Factor.Mantissa));
                Factor.Exponent = Row[5];
                if (Factor.Mantissa[0] >= 0x8000u)
                {
                    // Words 1..2 as one dword (high word widened before the shift), minus 1.
                    unsigned int Low =
                        Factor.Mantissa[1] | ((unsigned int)Factor.Mantissa[2] << 16);
                    --Low;
                    Factor.Mantissa[1] = (unsigned short)Low; // low word
                    Factor.Mantissa[2] = Low >> 16;
                }
                Multiply(Value, Factor);
            }
            Group += 7;
        }
    }

    double ToDouble(FExtendedDecimal const& Value, bool bNegative)
    {
        uint64_t Bits = bNegative ? UINT64_C(0x8000000000000000) : 0;
        // Words 1..4 as one qword; each word is widened to 64 bits before its shift.
        uint64_t High = Value.Mantissa[1] |
            ((uint64_t)Value.Mantissa[2] << 16) |
            ((uint64_t)Value.Mantissa[3] << 32) |
            ((uint64_t)Value.Mantissa[4] << 48);
        int Exponent = Value.Exponent - 0x3fff;
        if (Value.Exponent >= 0x7fff)
        {
            Bits |= UINT64_C(0x7ff0000000000000);
        }
        else if (High != 0)
        {
            uint64_t Significand = High >> 11;
            uint64_t const Tail = High & 0x7ffu;
            // _ld12tod rounds strictly greater than halfway; an exact midpoint
            // is truncated even when its retained low bit is odd.
            if (Tail > 0x400u || (Tail == 0x400u && Value.Mantissa[0] != 0))
                ++Significand;
            if (Significand == (UINT64_C(1) << 53))
            {
                Significand >>= 1;
                ++Exponent;
            }
            if (Exponent >= 1024)
            {
                Bits |= UINT64_C(0x7ff0000000000000);
            }
            else if (Exponent >= -1022)
                Bits |= ((uint64_t)(Exponent + 1023) << 52) | // widened before the shift
                    (Significand & UINT64_C(0xfffffffffffff));
            else
            {
                // 10162f37 restores the original mantissa, shifts relative to
                // -1023, rounds to 53 bits, then packs with a 12-bit shift. The
                // final extra bit is discarded rather than rounded again.
                unsigned int Shift = -1023 - (Value.Exponent - 0x3fff);
                uint64_t Fraction = 0;
                if (Shift < 64)
                {
                    uint64_t Shifted = High >> Shift;
                    // The word shifted out below the 64 kept bits (only its low 16 bits).
                    unsigned short Extra = Shift == 0 ? Value.Mantissa[0] :
                        (Shift < 16 ? (unsigned short)((High << (16 - Shift)) |
                            (Value.Mantissa[0] >> Shift)) :
                            (unsigned short)(High >> (Shift - 16)));
                    uint64_t Rounded = Shifted >> 11;
                    uint64_t const Remainder = Shifted & 0x7ffu;
                    if (Remainder > 0x400u || (Remainder == 0x400u && Extra != 0))
                        ++Rounded;
                    Fraction = Rounded >> 1;
                }
                Bits |= Fraction;
            }
        }
        double Result;
        memcpy(&Result, &Bits, sizeof(Result));
        return Result;
    }
}

double MsvcrtAtof(char const* Text)
{
    char const* Cursor = Text;
    while (isspace((unsigned char)*Cursor)) // ctype takes the byte value, not a negative char
        ++Cursor;
    bool const bNegative = *Cursor == '-';
    if (*Cursor == '+' || *Cursor == '-')
        ++Cursor;
    unsigned char Digits[24] = {};
    int Stored = 0;
    int Significant = 0;
    int Fractional = 0;
    bool bPoint = false;
    bool bAnyDigit = false;
    unsigned int RoundDigit = 0;
    unsigned char const Point = localeconv()->decimal_point[0];
    for (;; ++Cursor)
    {
        unsigned char C = *Cursor;
        if (C >= '0' && C <= '9')
        {
            bAnyDigit = true;
            if (bPoint)
                ++Fractional;
            if (Significant != 0 || C != '0')
            {
                ++Significant;
                if (Stored < 24)
                    Digits[Stored++] = C - '0';
                else if (Significant == 25)
                    RoundDigit = C - '0';
            }
        }
        else if (!bPoint && C == Point)
            bPoint = true;
        else
            break;
    }
    if (!bAnyDigit)
        return 0.0;

    int ExplicitExponent = 0;
    if (*Cursor == 'e' || *Cursor == 'E' || *Cursor == 'd' || *Cursor == 'D')
    {
        ++Cursor;
        bool const bExponentNegative = *Cursor == '-';
        if (*Cursor == '+' || *Cursor == '-')
            ++Cursor;
        while (*Cursor >= '0' && *Cursor <= '9')
        {
            if (ExplicitExponent < 10000)
                ExplicitExponent = ExplicitExponent * 10 + (*Cursor - '0');
            ++Cursor;
        }
        if (bExponentNegative)
            ExplicitExponent = -ExplicitExponent;
    }
    if (Stored == 0)
    {
        uint64_t Bits = bNegative ? UINT64_C(0x8000000000000000) : 0;
        double Result;
        memcpy(&Result, &Bits, sizeof(Result));
        return Result;
    }

    int Power = ExplicitExponent - Fractional + Significant - Stored;
    if (RoundDigit >= 5)
        ++Digits[Stored - 1];
    FExtendedDecimal Value = {};
    for (int Index = 0; Index < Stored; ++Index)
    {
        unsigned int Carry = Digits[Index];
        for (unsigned int Word = 0; Word < 5; ++Word)
        {
            unsigned int Product = Value.Mantissa[Word] * 10u + Carry;
            Value.Mantissa[Word] = (unsigned short)Product; // low word; the rest carries on
            Carry = Product >> 16;
        }
    }
    Value.Exponent = 0x3fff + 79;
    while ((Value.Mantissa[4] & 0x8000u) == 0)
    {
        ShiftLeft(Value.Mantissa, 5);
        --Value.Exponent;
    }
    if (Power > 5200)
        Value.Exponent = 0x7fff;
    else if (Power < -5200)
    {
        memset(Value.Mantissa, 0, sizeof(Value.Mantissa));
        Value.Exponent = 0;
    }
    else
        MultiplyPower(Value, Power);
    // The default (non-12-byte) parser path rounds the temporary to 64 bits
    // before _ld12tod. Exact halfway values are truncated at this stage too.
    if (Value.Mantissa[0] > 0x8000u)
    {
        unsigned int Word = 1;
        while (Word < 5 && ++Value.Mantissa[Word] == 0)
            ++Word;
        if (Word == 5)
        {
            Value.Mantissa[4] = 0x8000;
            ++Value.Exponent;
        }
    }
    Value.Mantissa[0] = 0;
    return ToDouble(Value, bNegative);
}
