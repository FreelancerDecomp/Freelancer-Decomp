// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "MovieCca.h"
#include "MovieField.h"
#include "MovieGraph.h"
#include "MovieWindow.h"

#include <intrin.h>

// Callees this file does not define. Signatures are the pushes at the call
// sites (stdcall: the caller does not adjust ESP).

long __stdcall Movie_ZeroFieldGroups(unsigned int* Dest, int Groups, int* Field);
long __stdcall Movie_CopyFieldGroups(unsigned int* First, unsigned int* Second, int Groups, int* Field);
long __stdcall Movie_FieldElementIsUnit(int Limbs, int* FieldHolder);
long __stdcall Movie_LoadFieldOne(unsigned int* Work, int* FieldHolder, unsigned int* Scratch);
unsigned int __stdcall Movie_DrawBoundedLimb(unsigned int Minuend, unsigned int Subtrahend);
int __stdcall Movie_ExponentiateByWindow(unsigned int* Base, unsigned int* Exponent, unsigned int Count, unsigned int* Dest, unsigned int* Context);

// DAT_06ada800. Powers of three consumed by FillWindowTable.
static const unsigned int GPowersOfThree[8] =
{
    0x00000001u, 0x00000003u, 0x00000009u, 0x0000001Bu,
    0x00000051u, 0x000000F3u, 0x000002D9u, 0x0000088Bu
};

// DAT_06ae0988. Twelve records of 7 dwords. The last record's product loop
// writes two dwords past the table.
static unsigned int GWindowTable[12 * 7 + 4];

// 0x06ac8d40. Strip leading zero limbs, then BSR. Result is the bit length.
int __stdcall LimbBitLength(int Limbs, int Count)
{
    unsigned int* Words = (unsigned int*)Limbs;
    int Left = Count;
    unsigned int Limb = 0;
    while (Left != 0)
    {
        Limb = Words[Left - 1];
        Left = Left - 1;
        if (Limb != 0u)
        {
            Left = Left + 1;
            break;
        }
    }
    if (Left == 0)
    {
        return 0;
    }
    Limb = Words[Left - 1];
    unsigned long BitIndex = 0;
    _BitScanReverse(&BitIndex, Limb);
    unsigned int Length = (unsigned int)Left + 0x07ffffffu;
    Length = (Length << 5) + BitIndex + 1u;
    return (int)Length;
}

// 0x06ac8bd0. Shift limbs by a signed count in -32..32. Returns the spilled bits.
unsigned int __stdcall ShiftLimbs(unsigned int* Source, unsigned int BitCount, unsigned int* Dest, int Count)
{
    int Shift = (int)BitCount;
    int AbsShift = Shift;
    int Sign = AbsShift >> 31;
    AbsShift = (AbsShift ^ Sign) - Sign;
    if (AbsShift > 0x20)
    {
        GMovieGraphCode = 0x28;
        return 0;
    }
    if (Count == 0)
    {
        return 0;
    }
    if (Shift > 0)
    {
        if (Shift == 0x20)
        {
            unsigned int Spilled = Source[Count - 1];
            int Index = Count - 1;
            while (Index != 0)
            {
                Dest[Index] = Source[Index - 1];
                Index = Index - 1;
            }
            Dest[0] = 0;
            return Spilled;
        }
        unsigned int Carry = 0;
        int Left = Count;
        unsigned int* Cursor = Source;
        int Delta = (int)((unsigned char*)Dest - (unsigned char*)Source);
        unsigned int Inverse = (unsigned int)(0x20 - Shift);
        while (Left != 0)
        {
            unsigned int Limb = Cursor[0];
            Cursor = Cursor + 1;
            unsigned int Low = (Limb << ((unsigned int)Shift & 31u)) | Carry;
            Carry = Limb >> (Inverse & 31u);
            *(unsigned int*)((unsigned char*)Cursor + Delta - 4) = Low;
            Left = Left - 1;
        }
        return Carry;
    }
    if (Shift < 0)
    {
        if (Shift == -0x20)
        {
            unsigned int Spilled = Source[0];
            if (Count != 1)
            {
                int Left = Count - 1;
                unsigned int* Cursor = Source;
                unsigned int* Out = Dest;
                do
                {
                    Cursor = Cursor + 1;
                    Out[0] = Cursor[0];
                    Out = Out + 1;
                    Left = Left - 1;
                } while (Left != 0);
            }
            Dest[Count - 1] = 0;
            return Spilled;
        }
        unsigned int Carry = 0;
        unsigned int Amount = 0u - (unsigned int)Shift;
        unsigned int* Cursor = Source + (Count - 1);
        int Delta = (int)((unsigned char*)Dest - (unsigned char*)Source);
        int Left = Count;
        unsigned int Cross = ((unsigned int)Shift + 0x20u) & 31u;
        while (Left != 0)
        {
            unsigned int Limb = Cursor[0];
            Cursor = Cursor - 1;
            unsigned int High = (Limb >> (Amount & 31u)) | Carry;
            Carry = Limb << Cross;
            *(unsigned int*)((unsigned char*)Cursor + Delta + 4) = High;
            Left = Left - 1;
        }
        return Carry >> Cross;
    }
    int Left = Count;
    unsigned int* From = Source;
    unsigned int* To = Dest;
    while (Left != 0)
    {
        To[0] = From[0];
        From = From + 1;
        To = To + 1;
        Left = Left - 1;
    }
    return 0;
}

// 0x06ac8b30. Shift by an arbitrary bit count: bit shift, then a whole-limb slide.
void __stdcall ShiftLimbsByBits(unsigned int* Source, unsigned int BitCount, unsigned int* Dest, unsigned int Count)
{
    int Shift = (int)BitCount;
    int WordShift;
    if (Shift >= 0)
    {
        WordShift = (int)((unsigned int)Shift >> 5);
    }
    else
    {
        unsigned int Magnitude = 0u - (unsigned int)Shift;
        Magnitude = Magnitude >> 5;
        WordShift = -(int)Magnitude;
    }
    unsigned int BitShift = (unsigned int)(Shift - (WordShift << 5));
    ShiftLimbs(Source, BitShift, Dest, (int)Count);
    if (WordShift < 0)
    {
        unsigned int Words = 0u - (unsigned int)WordShift;
        unsigned int Index = 0;
        unsigned int* Read = Dest + Words;
        if (Count == 0u)
        {
            return;
        }
        while (Index != Count)
        {
            unsigned int Limb = 0;
            if (Index + Words < Count)
            {
                Limb = Read[0];
            }
            Dest[Index] = Limb;
            Read = Read + 1;
            Index = Index + 1u;
        }
        return;
    }
    if (WordShift > 0)
    {
        unsigned int Index = Count - 1u;
        if (Index == 0xffffffffu)
        {
            return;
        }
        while (Index != 0xffffffffu)
        {
            unsigned int Limb = 0;
            if (Index >= (unsigned int)WordShift)
            {
                Limb = Dest[Index - (unsigned int)WordShift];
            }
            Dest[Index] = Limb;
            Index = Index - 1u;
        }
    }
}

// 0x06ac8a90. Magnitude compare. 1 if Left is greater, -1 if Right is greater, 0 if equal.
long __stdcall CompareLimbs(int Left, unsigned int LeftCount, int Right, int RightCount)
{
    unsigned int* LeftLimbs = (unsigned int*)Left;
    unsigned int* RightLimbs = (unsigned int*)Right;
    unsigned int LeftLength = LeftCount;
    unsigned int RightLength = (unsigned int)RightCount;
    unsigned int SavedRight = RightLength;
    if (LeftLength > RightLength)
    {
        do
        {
            if (LeftLimbs[LeftLength - 1u] != 0u)
            {
                return 1;
            }
            LeftLength = LeftLength - 1u;
        } while (LeftLength > RightLength);
    }
    if (RightLength > LeftLength)
    {
        do
        {
            if (RightLimbs[SavedRight - 1u] != 0u)
            {
                return -1;
            }
            SavedRight = SavedRight - 1u;
        } while (SavedRight > LeftLength);
    }
    while (LeftLength != 0u)
    {
        unsigned int LeftLimb = LeftLimbs[LeftLength - 1u];
        unsigned int RightLimb = RightLimbs[LeftLength - 1u];
        if (LeftLimb != RightLimb)
        {
            if (RightLimb < LeftLimb)
            {
                return 1;
            }
            return -1;
        }
        LeftLength = LeftLength - 1u;
    }
    return 0;
}

// 0x06ac89d0. Subtract Count limbs. Returns the borrow.
long __stdcall SubtractLimbs(unsigned int* Minuend, unsigned int* Subtrahend, int Difference, int Count)
{
    unsigned int* Diff = (unsigned int*)Difference;
    unsigned int Borrow = 0;
    int Index;
    if (Count == 0)
    {
        return 0;
    }
    for (Index = 0; Index < Count; Index = Index + 1)
    {
        unsigned long long Need = (unsigned long long)Subtrahend[Index] + Borrow;
        unsigned int Limb = Minuend[Index];
        Diff[Index] = Limb - (unsigned int)Need;
        Borrow = (Limb < Need) ? 1u : 0u;
    }
    return (long)Borrow;
}

// 0x06ac8500. Add RightCount limbs, then propagate the carry through Left. Returns the carry.
long __stdcall AddLimbs(unsigned int* Left, int LeftCount, unsigned int* Right, int RightCount, int Sum)
{
    unsigned int* SumLimbs = (unsigned int*)Sum;
    unsigned int Carry = 0;
    int Index = 0;
    if (RightCount != 0)
    {
        for (Index = 0; Index < RightCount; Index = Index + 1)
        {
            unsigned long long Total = (unsigned long long)Left[Index] + (unsigned long long)Right[Index] + Carry;
            SumLimbs[Index] = (unsigned int)Total;
            Carry = (unsigned int)(Total >> 32);
        }
    }
    int Remain = LeftCount - RightCount;
    while (Remain != 0)
    {
        unsigned long long Total = (unsigned long long)Left[Index] + Carry;
        SumLimbs[Index] = (unsigned int)Total;
        Carry = (unsigned int)(Total >> 32);
        Index = Index + 1;
        Remain = Remain - 1;
        if (Carry == 0u && Remain != 0)
        {
            break;
        }
    }
    if (Remain == 0)
    {
        return (long)Carry;
    }
    if (SumLimbs != Left)
    {
        while (Remain != 0)
        {
            SumLimbs[Index] = Left[Index];
            Index = Index + 1;
            Remain = Remain - 1;
        }
    }
    return 0;
}

// 0x06ac54d0. Normalize the leading limbs and store the reciprocal quotient plus the shift.
void __stdcall StoreLeadingReciprocal(int Limbs, unsigned int Count, unsigned int* Reciprocal)
{
    unsigned int* Words = (unsigned int*)Limbs;
    unsigned int Top = Words[Count - 1u];
    unsigned long BitIndex = 0;
    _BitScanReverse(&BitIndex, Top);
    unsigned int Shift = 0x20u - (BitIndex + 1u);
    unsigned int Mid = 0;
    unsigned int Low = 0;
    if (Count >= 2u)
    {
        Mid = Words[Count - 2u];
    }
    if (Count >= 3u)
    {
        Low = Words[Count - 3u];
    }
    unsigned int ShiftLess = (0x1fu - Shift) & 0x1fu;
    unsigned int Norm = ((Mid >> 1) >> ShiftLess) | (Top << (Shift & 0x1fu));
    unsigned int Wide = ((Low >> 1) >> ShiftLess) | (Mid << (Shift & 0x1fu));
    unsigned int Quotient;
    unsigned int Side;
    Movie_DivideDoubleWord(~Wide, ~Norm, Norm, (int*)&Quotient, (int*)&Side);
    unsigned long long Product = (unsigned long long)Wide * (unsigned long long)Quotient;
    if ((unsigned int)(Product >> 32) > Side)
    {
        Quotient = Quotient - 1u;
    }
    unsigned int Index = Count - 1u;
    unsigned int Masked = (0xffffffffu >> (Shift & 0x1fu)) - Top;
    if (Index != 0xffffffffu)
    {
        unsigned int* Cursor = Words + Index;
        for (;;)
        {
            if (Masked >= Quotient)
            {
                break;
            }
            unsigned int Previous = 0;
            if (Index != 0u)
            {
                Previous = Cursor[-1];
            }
            unsigned int NegPrevious = 0xffffffffu - Previous;
            unsigned int Current = Cursor[0];
            unsigned long long LimbProduct = (unsigned long long)Current * (unsigned long long)Quotient;
            unsigned int ProductLow = (unsigned int)LimbProduct;
            unsigned int ProductHigh = (unsigned int)(LimbProduct >> 32);
            if (ProductHigh > Masked || (ProductHigh == Masked && ProductLow > NegPrevious))
            {
                Quotient = Quotient - 1u;
                break;
            }
            unsigned int DiffLow = NegPrevious - ProductLow;
            unsigned int Borrow = (NegPrevious < ProductLow) ? 1u : 0u;
            unsigned int DiffHigh = Masked - ProductHigh - Borrow;
            if (DiffHigh != 0u || DiffLow >= Quotient)
            {
                break;
            }
            Masked = DiffLow;
            Cursor = Cursor - 1;
            Index = Index - 1u;
            if (Index == 0xffffffffu)
            {
                break;
            }
        }
    }
    Reciprocal[0] = Quotient;
    Reciprocal[1] = Shift;
}

// 0x06aca7d0. Fill the twelve window records: doubled widths, then products by 3^n.
void __stdcall FillWindowTable(unsigned int Width)
{
    unsigned int Limit = Width;
    if (Width == 0u)
    {
        Limit = 4u;
    }
    else if (Width < 3u || Width > 0xCu)
    {
        GMovieGraphCode = 0x2c;
    }
    unsigned int* Record = GWindowTable;
    unsigned int Step = 1u;
    for (;;)
    {
        unsigned int Value = Step;
        if ((int)(Step - 4u) >= 0)
        {
            Value = Record[-14] << 1;
        }
        unsigned int Halvings = 0;
        unsigned int Product = Value;
        Record[0] = Value;
        if (Value > Limit)
        {
            do
            {
                Product = Product >> 1;
                Halvings = Halvings + 1u;
            } while (Product > Limit);
        }
        Record[1] = Halvings;
        unsigned int Factors = Halvings + 1u;
        unsigned int Power = 0;
        while (Factors != 0u)
        {
            Record[2u + Power] = GPowersOfThree[Power] * Product;
            Power = Power + 1u;
            Factors = Factors - 1u;
        }
        Record = Record + 7;
        Step = Step + 1u;
        if (Step - 1u == 0xCu)
        {
            break;
        }
    }
}

// 0x06aca860. Walk the window table downward and keep the last slot whose width is >= LimbCount.
unsigned int* __stdcall FindWindowSlot(unsigned int LimbCount)
{
    if (GWindowTable[0] == 0u)
    {
        FillWindowTable(0);
    }
    unsigned int* Result = 0;
    unsigned int* Cursor = GWindowTable + (11 * 7);
    for (;;)
    {
        if (LimbCount <= Cursor[0])
        {
            Result = Cursor;
        }
        if (Cursor == GWindowTable)
        {
            break;
        }
        Cursor = Cursor - 7;
    }
    return Result;
}

struct FShiftXorSide
{
    unsigned int Coefficient[64];
    unsigned int Value[64];
    unsigned int BitLength;
};

// 0x06ac9d20. Shift-and-XOR the longer side into the shorter until one bit length is zero.
long __stdcall ReduceLimbsShiftXor(unsigned int* Left, unsigned int LeftBits, unsigned int* Right, unsigned int RightBits, unsigned int* Coefficient, unsigned int* Remainder)
{
    unsigned int LeftWords = (LeftBits + 0x1fu) >> 5;
    unsigned int RightWords = (RightBits + 0x1fu) >> 5;
    unsigned int Words = LeftWords;
    if (LeftWords <= RightWords)
    {
        Words = RightWords;
    }
    unsigned int KeepCoefficient = (Coefficient != 0) ? 1u : 0u;
    if (Words == 0u || Words > 0x40u)
    {
        return 0;
    }
    FShiftXorSide Sides[2];
    unsigned int Temp[64];
    unsigned int Index;
    unsigned int Side;
    for (Index = 0; Index < Words; Index = Index + 1)
    {
        Sides[0].Coefficient[Index] = 0;
        Sides[0].Value[Index] = 0;
        Sides[1].Coefficient[Index] = 0;
        Sides[1].Value[Index] = 0;
    }
    Sides[0].Coefficient[0] = Sides[0].Coefficient[0] ^ 1u;
    for (Index = 0; Index < LeftWords; Index = Index + 1)
    {
        Sides[0].Value[Index] = Left[Index];
    }
    for (Index = 0; Index < RightWords; Index = Index + 1)
    {
        Sides[1].Value[Index] = Right[Index];
    }
    Sides[0].BitLength = (unsigned int)LimbBitLength((int)Left, (int)LeftWords);
    Sides[1].BitLength = (unsigned int)LimbBitLength((int)Right, (int)RightWords);
    Side = 0;
    for (;;)
    {
        unsigned int Other = Side ^ 1u;
        if (Sides[Other].BitLength == 0u)
        {
            break;
        }
        while (Sides[Other].BitLength <= Sides[Side].BitLength)
        {
            unsigned int Diff = Sides[Side].BitLength - Sides[Other].BitLength;
            ShiftLimbsByBits(Sides[Other].Value, Diff, Temp, Words);
            for (Index = 0; Index < Words; Index = Index + 1)
            {
                Sides[Side].Value[Index] = Sides[Side].Value[Index] ^ Temp[Index];
            }
            Sides[Side].BitLength = (unsigned int)LimbBitLength((int)Sides[Side].Value, (int)Words);
            if (KeepCoefficient != 0u)
            {
                ShiftLimbsByBits(Sides[Other].Coefficient, Diff, Temp, Words);
                for (Index = 0; Index < Words; Index = Index + 1)
                {
                    Sides[Side].Coefficient[Index] = Sides[Side].Coefficient[Index] ^ Temp[Index];
                }
            }
        }
        Side = 1u - Side;
    }
    if (Remainder != 0)
    {
        for (Index = 0; Index < Words; Index = Index + 1)
        {
            Remainder[Index] = Sides[Side].Value[Index];
        }
    }
    if (Coefficient != 0)
    {
        for (Index = 0; Index < RightWords; Index = Index + 1)
        {
            Coefficient[Index] = Sides[Side].Coefficient[Index];
        }
    }
    return 1;
}

// 0x06ac9f60. Bit-serial shift-XOR multiply of Left by Right, reduced by Modulus.
long __stdcall MultiplyLimbsShiftXor(int Left, int Right, unsigned int* Product, int Modulus, unsigned int ModulusBits)
{
    unsigned int Words = (ModulusBits + 0x1fu) >> 5;
    int Length = LimbBitLength(Modulus, (int)((ModulusBits >> 5) + 1u));
    long Fits = 0;
    unsigned int ProductLimbs[64];
    unsigned int Index;
    if (Length == (int)(ModulusBits + 1u))
    {
        Fits = 1;
    }
    for (Index = 0; Index < Words; Index = Index + 1)
    {
        ProductLimbs[Index] = 0;
    }
    if (Words != 0u)
    {
        unsigned int BitIndex = ModulusBits - 1u;
        if (BitIndex != 0xffffffffu)
        {
            unsigned int HighWord = BitIndex >> 5;
            unsigned int HighBit = BitIndex & 0x1fu;
            unsigned int* LeftLimbs = (unsigned int*)Left;
            unsigned int* RightLimbs = (unsigned int*)Right;
            unsigned int* ModulusLimbs = (unsigned int*)Modulus;
            do
            {
                unsigned int LeftMask = 0u - (LeftLimbs[BitIndex >> 5] >> (BitIndex & 0x1fu) & 1u);
                unsigned int HighMask = 0u - (ProductLimbs[HighWord] >> HighBit & 1u);
                unsigned int Carry = 0;
                for (Index = 0; Index < Words; Index = Index + 1)
                {
                    unsigned int Limb = ProductLimbs[Index];
                    unsigned int Mixed = (RightLimbs[Index] & LeftMask) ^ (ModulusLimbs[Index] & HighMask) ^ (Limb << 1) ^ Carry;
                    Carry = Limb >> 31;
                    ProductLimbs[Index] = Mixed;
                }
                unsigned int TopMask = 0xffffffffu >> ((Words << 5) - ModulusBits);
                ProductLimbs[Words - 1u] = ProductLimbs[Words - 1u] & TopMask;
                BitIndex = BitIndex - 1u;
            } while (BitIndex != 0xffffffffu);
        }
    }
    for (Index = 0; Index < Words; Index = Index + 1)
    {
        Product[Index] = ProductLimbs[Index];
    }
    return Fits;
}

// 0x06ac6bb0. Seed the work buffer from the field, or hand the same buffers to 0x06ac7570.
long __stdcall SeedFieldWork(unsigned int* Value, unsigned int* Work, int* FieldHolder, unsigned int* Upper)
{
    unsigned int* Field = (unsigned int*)FieldHolder[0];
    unsigned int LimbCount = Field[0];
    if (Movie_FieldElementIsUnit((int)Value, FieldHolder) != 0)
    {
        long Step = Movie_LoadFieldOne(Work, FieldHolder, Upper);
        if (Step != 0)
        {
            return 1;
        }
        return 0;
    }
    if (Movie_CopyFieldGroups(Value, Work, 2, (int*)Field) == 0)
    {
        return 0;
    }
    if (Movie_CopyFieldGroups((unsigned int*)((unsigned char*)Field + 0x12c), Work + (LimbCount * 2u), 1, (int*)Field) == 0)
    {
        return 0;
    }
    if (Movie_ZeroFieldGroups(Work + (LimbCount * 3u), 2, (int*)Field) == 0)
    {
        return 0;
    }
    return 1;
}

// 0x06ac8010. Multiply the value through the two field factors at +4 and +0x104.
long __stdcall MultiplyByFieldFactors(unsigned int* Value, int* FieldHolder)
{
    unsigned int* Field = (unsigned int*)FieldHolder[0];
    unsigned int* High = Value + Field[0];
    unsigned int TempLow[64];
    unsigned int TempHigh[64];
    if (Movie_FieldElementIsUnit((int)Value, FieldHolder) != 0)
    {
        return 1;
    }
    if (Movie_MultiplyFieldLimbs(Value, Value, TempLow, Field) == 0)
    {
        return 0;
    }
    if (Field[2] == 1u)
    {
        if (Movie_AddFieldLimbs(TempLow, (unsigned int*)((unsigned char*)FieldHolder + 4), TempLow, (int*)Field) == 0)
        {
            return 0;
        }
        if (Movie_MultiplyFieldLimbs(Value, TempLow, TempLow, Field) == 0)
        {
            return 0;
        }
        if (Movie_AddFieldLimbs(TempLow, (unsigned int*)((unsigned char*)FieldHolder + 0x104), TempLow, (int*)Field) == 0)
        {
            return 0;
        }
        if (Movie_MultiplyFieldLimbs(High, High, TempHigh, Field) == 0)
        {
            return 0;
        }
    }
    else
    {
        if (Movie_AddFieldLimbs(Value, (unsigned int*)((unsigned char*)FieldHolder + 4), TempHigh, (int*)Field) == 0)
        {
            return 0;
        }
        if (Movie_MultiplyFieldLimbs(TempLow, TempHigh, TempLow, Field) == 0)
        {
            return 0;
        }
        if (Movie_AddFieldLimbs(TempLow, (unsigned int*)((unsigned char*)FieldHolder + 0x104), TempLow, (int*)Field) == 0)
        {
            return 0;
        }
        if (Movie_AddFieldLimbs(High, Value, TempHigh, (int*)Field) == 0)
        {
            return 0;
        }
        if (Movie_MultiplyFieldLimbs(High, TempHigh, TempHigh, Field) == 0)
        {
            return 0;
        }
    }
    if (Movie_FieldLimbsEqual((int)TempLow, (int)TempHigh, (int*)Field) == 0)
    {
        return 0;
    }
    return 1;
}

// 0x06ac8190. When the field kind is 1, duplicate the constant at Field+0x10. Otherwise call 0x06ac3110.
long __stdcall StoreFieldUnit(unsigned int* Dest, int* FieldHolder)
{
    unsigned int* Field = (unsigned int*)FieldHolder[0];
    unsigned int LimbCount = Field[0];
    unsigned int Index;
    if (Field[2] == 1u)
    {
        unsigned int* Source = (unsigned int*)Field[4];
        for (Index = 0; Index < LimbCount; Index = Index + 1)
        {
            Dest[Index] = Source[Index];
        }
        for (Index = 0; Index < LimbCount; Index = Index + 1)
        {
            Dest[LimbCount + Index] = Source[Index];
        }
        return 1;
    }
    if (Movie_ZeroFieldGroups(Dest, 2, (int*)Field) != 0)
    {
        return 1;
    }
    return 0;
}

// 0x06ac7210. Field multiply of Left by Right through Scratch. Kind selects the factor chain.
long __stdcall MultiplyFieldLimbs(unsigned int* Left, unsigned int* Right, int* FieldHolder, unsigned int* Scratch)
{
    unsigned int* Field = (unsigned int*)FieldHolder[0];
    unsigned int LimbCount = Field[0];
    unsigned int* LeftOnce = Left + LimbCount;
    unsigned int* LeftTwice = Left + (LimbCount * 2u);
    unsigned int* RightOnce = Right + LimbCount;
    unsigned int* RightTwice = Right + (LimbCount * 2u);
    if (Movie_FieldLimbsAreZero((int)LeftTwice, (int*)Field) != 0)
    {
        if (Movie_LoadFieldOne(Right, FieldHolder, Scratch) != 0)
        {
            return 1;
        }
        return 0;
    }
    if ((int)Field[2] >= 2)
    {
        unsigned int* ScratchOnce = Scratch + LimbCount;
        unsigned int* ScratchTwice = Scratch + (LimbCount * 2u);
        unsigned int* ScratchThrice = Scratch + (LimbCount * 3u);
        if (Movie_MultiplyFieldLimbs(Left, Left, Scratch, Field) == 0 ||
            Movie_MultiplyFieldLimbs(LeftTwice, LeftTwice, ScratchOnce, Field) == 0 ||
            Movie_MultiplyFieldLimbs(ScratchOnce, ScratchOnce, ScratchTwice, Field) == 0 ||
            Movie_MultiplyFieldLimbs(ScratchTwice, ScratchTwice, ScratchTwice, Field) == 0 ||
            Movie_MultiplyFieldLimbs((unsigned int*)((unsigned char*)FieldHolder + 0x104), ScratchTwice, ScratchTwice, Field) == 0 ||
            Movie_MultiplyFieldLimbs(LeftOnce, LeftTwice, ScratchThrice, Field) == 0 ||
            Movie_AddFieldLimbs(Scratch, ScratchThrice, ScratchThrice, (int*)Field) == 0 ||
            Movie_MultiplyFieldLimbs(Left, ScratchOnce, RightTwice, Field) == 0 ||
            Movie_MultiplyFieldLimbs(Scratch, Scratch, Scratch, Field) == 0 ||
            Movie_AddFieldLimbs(Scratch, ScratchTwice, Right, (int*)Field) == 0 ||
            Movie_MultiplyFieldLimbs(ScratchTwice, RightTwice, ScratchTwice, Field) == 0 ||
            Movie_MultiplyFieldLimbs(ScratchThrice, Right, ScratchThrice, Field) == 0 ||
            Movie_AddFieldLimbs(ScratchTwice, ScratchThrice, RightOnce, (int*)Field) == 0)
        {
            return 0;
        }
        return 1;
    }
    {
        unsigned int* ScratchOnce = Scratch + LimbCount;
        unsigned int* ScratchTwice = Scratch + (LimbCount * 2u);
        unsigned int* ScratchThrice = Scratch + (LimbCount * 3u);
        unsigned int* ScratchFour = Scratch + (LimbCount * 4u);
        unsigned int* Factor = (unsigned int*)Field[4];
        if (Movie_MultiplyFieldLimbs(Left, Left, Scratch, Field) == 0 ||
            Movie_AddFieldLimbs(Scratch, Scratch, ScratchThrice, (int*)Field) == 0 ||
            Movie_AddFieldLimbs(Scratch, ScratchThrice, ScratchThrice, (int*)Field) == 0 ||
            Movie_MultiplyFieldLimbs(LeftTwice, LeftTwice, ScratchTwice, Field) == 0 ||
            Movie_MultiplyFieldLimbs(ScratchTwice, ScratchTwice, ScratchTwice, Field) == 0 ||
            Movie_MultiplyFieldLimbs((unsigned int*)((unsigned char*)FieldHolder + 4), ScratchTwice, ScratchTwice, Field) == 0 ||
            Movie_AddFieldLimbs(ScratchThrice, ScratchTwice, ScratchThrice, (int*)Field) == 0 ||
            Movie_AddFieldLimbs(LeftOnce, LeftOnce, ScratchFour, (int*)Field) == 0 ||
            Movie_MultiplyFieldLimbs(ScratchFour, LeftTwice, RightTwice, Field) == 0 ||
            Movie_MultiplyFieldLimbs(ScratchFour, ScratchFour, ScratchFour, Field) == 0 ||
            Movie_MultiplyFieldLimbs(Left, ScratchFour, ScratchOnce, Field) == 0 ||
            Movie_MultiplyFieldLimbs(ScratchFour, ScratchFour, ScratchFour, Field) == 0 ||
            Movie_MultiplyFieldLimbs(ScratchThrice, ScratchThrice, Right, Field) == 0 ||
            Movie_SubtractFieldLimbs(Right, ScratchOnce, Right, (int*)Field) == 0 ||
            Movie_SubtractFieldLimbs(Right, ScratchOnce, Right, (int*)Field) == 0 ||
            Movie_SubtractFieldLimbs(ScratchOnce, Right, ScratchOnce, (int*)Field) == 0 ||
            Movie_MultiplyFieldLimbs(ScratchThrice, ScratchOnce, RightOnce, Field) == 0)
        {
            return 0;
        }
        Movie_ShiftModulo(ScratchFour, -1, ScratchFour, Factor);
        if (Movie_SubtractFieldLimbs(RightOnce, ScratchFour, RightOnce, (int*)Field) == 0)
        {
            return 0;
        }
        return 1;
    }
}

// 0x06ac5d70. For each row, square the work buffer, then keep the row or store the field unit.
long __stdcall CollectSquaredRows(unsigned int* Value, int SquareSteps, int RowCount, void* Table, void* FieldHolder)
{
    unsigned int* Field = *(unsigned int**)FieldHolder;
    unsigned int LimbCount = Field[0];
    void* Allocated = Movie_AllocateBlock(0x1000);
    void* Workspace = Allocated;
    unsigned int* Rows = 0;
    unsigned int* Work = 0;
    unsigned int* Upper = 0;
    long Ok = 0;
    unsigned int Hits = 0;
    if (GMovieGraphCode == 0x2b)
    {
        Workspace = 0;
    }
    else
    {
        unsigned int RowBytes = LimbCount * (unsigned int)RowCount * 8u;
        unsigned int* AllocatedRows = (unsigned int*)Movie_AllocateBlock(RowBytes);
        if (GMovieGraphCode == 0x2b)
        {
            Rows = 0;
        }
        else
        {
            Rows = AllocatedRows;
            Upper = (unsigned int*)((unsigned char*)Workspace + 0x600);
            Work = (unsigned int*)((unsigned char*)Workspace + 0x100);
            if (MultiplyByFieldFactors(Value, (int*)FieldHolder) != 0 &&
                Movie_CopyFieldGroups(Value, (unsigned int*)Table, 2, (int*)Field) != 0 &&
                SeedFieldWork(Value, Work, (int*)FieldHolder, Upper) != 0)
            {
                Ok = 1;
            }
        }
    }
    if ((unsigned int)RowCount >= 1u)
    {
        unsigned int ByteOne = LimbCount * 4u;
        unsigned int BytePair = LimbCount * 8u;
        unsigned int WorkAddress = (unsigned int)Work;
        unsigned char* TableCursor = (unsigned char*)Table + BytePair;
        unsigned char* RowCursor = (unsigned char*)Rows;
        unsigned int Left = (unsigned int)RowCount;
        do
        {
            int Squares = SquareSteps;
            if (Squares != 0)
            {
                do
                {
                    if (Ok != 0)
                    {
                        if (MultiplyFieldLimbs(Work, Work, (int*)FieldHolder, Upper) != 0)
                        {
                            Ok = 1;
                        }
                        else
                        {
                            Ok = 0;
                        }
                    }
                    Squares = Squares - 1;
                } while (Squares != 0);
            }
            if (Movie_FieldLimbsAreZero((int)(WorkAddress + BytePair), (int*)Field) == 0)
            {
                Hits = Hits + 1u;
                if (Ok != 0)
                {
                    if (Movie_CopyFieldGroups(Work, (unsigned int*)TableCursor, 2, (int*)Field) != 0 &&
                        Movie_CopyFieldGroups((unsigned int*)(WorkAddress + BytePair), (unsigned int*)RowCursor, 1, (int*)Field) != 0)
                    {
                        Ok = 1;
                    }
                    else
                    {
                        Ok = 0;
                    }
                }
            }
            else if (Ok != 0)
            {
                if (StoreFieldUnit((unsigned int*)TableCursor, (int*)FieldHolder) != 0)
                {
                    Ok = 1;
                }
                else
                {
                    Ok = 0;
                }
            }
            RowCursor = RowCursor + ByteOne;
            TableCursor = TableCursor + BytePair;
            Left = Left - 1u;
        } while (Left != 0u);
        if (Hits > (unsigned int)RowCount)
        {
            Ok = 0;
        }
        else if (Hits != 0u)
        {
            unsigned int* Second = (unsigned int*)((unsigned int)Rows + LimbCount * (unsigned int)RowCount * 4u);
            unsigned int* Base = (unsigned int*)Workspace;
            long Flag = 0;
            unsigned int Back;
            if (Ok != 0)
            {
                if (Movie_CopyFieldGroups(Rows, Second, 1, (int*)Field) != 0)
                {
                    Flag = 1;
                }
            }
            if (Hits != 1u)
            {
                unsigned int* RowPtr = (unsigned int*)((unsigned char*)Rows + ByteOne);
                unsigned int* SecondPtr = Second;
                unsigned int Step = Hits - 1u;
                unsigned int Delta = (unsigned int)Second - (unsigned int)Rows;
                while (Step != 0u)
                {
                    if (Flag != 0)
                    {
                        unsigned int* Mixed = (unsigned int*)((unsigned int)RowPtr + Delta);
                        if (Movie_MultiplyFieldLimbs(RowPtr, SecondPtr, Mixed, Field) != 0)
                        {
                            Flag = 1;
                        }
                        else
                        {
                            Flag = 0;
                        }
                    }
                    RowPtr = (unsigned int*)((unsigned char*)RowPtr + ByteOne);
                    SecondPtr = (unsigned int*)((unsigned char*)SecondPtr + ByteOne);
                    Step = Step - 1u;
                }
            }
            if (Flag != 0)
            {
                unsigned int* Tail = (unsigned int*)((unsigned char*)Second + (Hits - 1u) * ByteOne);
                if (Movie_FieldUnaryByKind(Tail, Base, Field) != 0)
                {
                    Flag = 1;
                }
                else
                {
                    Flag = 0;
                }
            }
            Back = Hits - 1u;
            if (Back != 0u)
            {
                unsigned int* High = (unsigned int*)((unsigned char*)Second + (Back - 1u) * ByteOne);
                unsigned int* Low = (unsigned int*)((unsigned char*)Rows + Back * ByteOne);
                unsigned int Delta = (unsigned int)Second - (unsigned int)Rows;
                while (Back != 0u)
                {
                    if (Flag != 0)
                    {
                        unsigned int* Sum = (unsigned int*)((unsigned int)Low + Delta);
                        if (Movie_MultiplyFieldLimbs(Base, High, Sum, Field) != 0 &&
                            Movie_MultiplyFieldLimbs(Base, Low, Base, Field) != 0)
                        {
                            Flag = 1;
                        }
                        else
                        {
                            Flag = 0;
                        }
                    }
                    High = (unsigned int*)((unsigned char*)High - ByteOne);
                    Low = (unsigned int*)((unsigned char*)Low - ByteOne);
                    Back = Back - 1u;
                }
            }
            if (Flag != 0 && Movie_CopyFieldGroups(Base, Second, 1, (int*)Field) != 0)
            {
                Ok = 1;
            }
            else
            {
                Ok = 0;
            }
            if (Hits != 0u)
            {
                unsigned char* OutPair = (unsigned char*)Table + BytePair;
                unsigned char* OutTriple = OutPair + ByteOne;
                unsigned int* SecondCursor = Second;
                unsigned int Remain = Hits;
                while (Remain != 0u)
                {
                    if (Ok != 0)
                    {
                        if (Movie_MultiplyFieldLimbs(SecondCursor, SecondCursor, Base, Field) != 0 &&
                            Movie_MultiplyFieldLimbs((unsigned int*)OutTriple, SecondCursor, (unsigned int*)OutTriple, Field) != 0 &&
                            Movie_MultiplyFieldLimbs((unsigned int*)OutTriple, Base, (unsigned int*)OutTriple, Field) != 0 &&
                            Movie_MultiplyFieldLimbs((unsigned int*)OutPair, Base, (unsigned int*)OutPair, Field) != 0)
                        {
                            Ok = 1;
                        }
                        else
                        {
                            Ok = 0;
                        }
                    }
                    OutPair = OutPair + BytePair;
                    OutTriple = OutTriple + BytePair;
                    SecondCursor = (unsigned int*)((unsigned char*)SecondCursor + ByteOne);
                    Remain = Remain - 1u;
                }
            }
        }
    }
    if (Workspace != 0)
    {
        Movie_FreeBlock(Workspace);
    }
    if (Rows != 0)
    {
        Movie_FreeBlock(Rows);
    }
    return Ok;
}

// 0x06ac8360. Rewrite Dest until it compares below Source. Top limb comes from 0x06ac8300.
long __stdcall ReduceLimbsBelow(int Source, unsigned char* Dest, unsigned int Count)
{
    unsigned int* Reference = (unsigned int*)Source;
    unsigned int* Buffer = (unsigned int*)Dest;
    unsigned int Top = Reference[Count - 1u];
    unsigned int Index;
    if (Count == 0u || Top == 0u)
    {
        GMovieGraphCode = 0x2e;
        for (Index = 0u; Index < Count; Index = Index + 1u)
        {
            Buffer[Index] = 0u;
        }
        return 0;
    }
    for (;;)
    {
        long Relation = 0;
        Movie_FillByteBuffer(Dest, Count * 4u - 4u);
        Buffer[Count - 1u] = Movie_DrawBoundedLimb(0u, Top);
        Index = Count;
        while (Index != 0u)
        {
            unsigned int BufferLimb = Buffer[Index - 1u];
            unsigned int ReferenceLimb = Reference[Index - 1u];
            Index = Index - 1u;
            if (BufferLimb != ReferenceLimb)
            {
                if (BufferLimb < ReferenceLimb)
                {
                    Relation = -1;
                }
                else
                {
                    Relation = 1;
                }
                break;
            }
        }
        if (Relation < 0)
        {
            return Relation;
        }
    }
}

// 0x06aca0f0. Shift out factors of two and subtract until one window is zero.
long __stdcall BinaryGcdLimbs(unsigned int* Left, unsigned int* Context, unsigned int* Coefficient, unsigned int* Result)
{
    unsigned int Count = Context[0x40];
    unsigned int Keep = (Coefficient != 0) ? 1u : 0u;
    unsigned int Index = 1u;
    unsigned int Other = 0u;
    int Sign = 1;
    unsigned int* Block = (unsigned int*)Movie_AllocateBlock(0x400);
    unsigned int Cursor;
    if (Block == 0)
    {
        if (Result != 0)
        {
            for (Cursor = 0u; Cursor < Count; Cursor = Cursor + 1u)
            {
                Result[Cursor] = 1u;
            }
        }
        if (Coefficient != 0)
        {
            for (Cursor = 0u; Cursor < Count; Cursor = Cursor + 1u)
            {
                Coefficient[Cursor] = 0u;
            }
        }
        return 0;
    }
    for (Cursor = 0u; Cursor < Count; Cursor = Cursor + 1u)
    {
        Block[0x40u + Cursor] = Left[Cursor];
    }
    {
        unsigned int Length = Context[0x450];
        unsigned int* From = Context + 0x410;
        for (Cursor = 0u; Cursor < Length; Cursor = Cursor + 1u)
        {
            Block[Cursor] = From[Cursor];
        }
        for (Cursor = Length; Cursor < Count; Cursor = Cursor + 1u)
        {
            Block[Cursor] = 0u;
        }
    }
    if (Keep != 0u)
    {
        if (Context[0x44] == 0u)
        {
            unsigned int* From = Context + 0x451;
            for (Cursor = 0u; Cursor < Count; Cursor = Cursor + 1u)
            {
                Block[0xc0u + Cursor] = From[Cursor];
            }
        }
        else
        {
            Movie_ReduceModulo(Context + 0x451, Count, Block + 0xc0, Context);
        }
        for (Cursor = 0u; Cursor < Count; Cursor = Cursor + 1u)
        {
            Block[0x80u + Cursor] = 0u;
        }
    }
    if (Count == 0u || ((~Context[0] & 1u) != 0u) || ((~Context[0x410] & 1u) != 0u))
    {
        GMovieGraphCode = 0x16;
        Sign = 0;
    }
    else
    {
        for (;;)
        {
            unsigned int* Window = Block + (Index * 0x40u);
            unsigned int LeftLimbs = Count;
            while (LeftLimbs != 0u)
            {
                unsigned int Limb = Window[LeftLimbs - 1u];
                LeftLimbs = LeftLimbs - 1u;
                if (Limb != 0u)
                {
                    LeftLimbs = LeftLimbs + 1u;
                    break;
                }
            }
            if (LeftLimbs == 0u)
            {
                break;
            }
            if ((~Window[0] & 1u) != 0u)
            {
                unsigned int Forced = Window[0] | 0x80000000u;
                unsigned long Bit = 0;
                _BitScanForward(&Bit, Forced);
                if ((Bit & 1u) != 0u)
                {
                    unsigned int Probe = Block[Other * 0x40u] + 1u;
                    if ((Probe & 4u) != 0u)
                    {
                        Sign = -Sign;
                    }
                }
                ShiftLimbs(Window, 0u - Bit, Window, (int)Count);
                if (Keep != 0u)
                {
                    unsigned int* Coeff = Block + ((Index + 2u) * 0x40u);
                    Movie_ShiftModulo(Coeff, -(int)Bit, Coeff, Context);
                }
            }
            else
            {
                unsigned int* OtherWindow = Block + (Other * 0x40u);
                long Relation = 0;
                unsigned int Probe = Count;
                while (Probe != 0u)
                {
                    unsigned int CurrentLimb = Window[Probe - 1u];
                    unsigned int OtherLimb = OtherWindow[Probe - 1u];
                    Probe = Probe - 1u;
                    if (CurrentLimb != OtherLimb)
                    {
                        if (CurrentLimb < OtherLimb)
                        {
                            Relation = -1;
                        }
                        else
                        {
                            Relation = 1;
                        }
                        break;
                    }
                }
                if (Relation < 0)
                {
                    unsigned int Mixed = Block[0x40] & Block[0];
                    Other = 1u - Other;
                    Index = 1u - Index;
                    if ((Mixed & 2u) != 0u)
                    {
                        Sign = -Sign;
                    }
                }
                {
                    unsigned int* Minuend = Block + (Index * 0x40u);
                    unsigned int* Subtrahend = Block + (Other * 0x40u);
                    SubtractLimbs(Minuend, Subtrahend, (int)Minuend, (int)Count);
                }
                if (Keep != 0u)
                {
                    unsigned int* LeftCoeff = Block + ((Index + 2u) * 0x40u);
                    unsigned int* RightCoeff = Block + ((Other + 2u) * 0x40u);
                    Movie_SubtractModulo(LeftCoeff, RightCoeff, LeftCoeff, Context);
                }
            }
        }
        {
            unsigned int One = 1u;
            unsigned int* Survivor = Block + (Other * 0x40u);
            if (CompareLimbs((int)Survivor, Count, (int)&One, 1) != 0)
            {
                Sign = 0;
            }
            if (Result != 0)
            {
                for (Cursor = 0u; Cursor < Count; Cursor = Cursor + 1u)
                {
                    Result[Cursor] = Survivor[Cursor];
                }
            }
            if (Keep != 0u)
            {
                unsigned int* Coeff = Block + ((Other + 2u) * 0x40u);
                for (Cursor = 0u; Cursor < Count; Cursor = Cursor + 1u)
                {
                    Coefficient[Cursor] = Coeff[Cursor];
                }
            }
        }
    }
    Movie_FreeBlock(Block);
    return (long)Sign;
}

// 0x06aca470. Kind is the low 3 bits of Context. Every path squares Output and
// returns 1 when that square equals Input.
unsigned int __stdcall SquareLimbByKind(unsigned int* Input, unsigned int* Output, unsigned int* Context)
{
    unsigned int Count = Context[0x40];
    unsigned int Ok = 1u;
    unsigned int Flag = 0u;
    unsigned int* Block = (unsigned int*)Movie_AllocateBlock(0x291c);
    unsigned int Significant;
    unsigned int Kind;
    if (Block == 0)
    {
        unsigned int Index;
        for (Index = 0u; Index < Count; Index = Index + 1u)
        {
            Output[Index] = 0u;
        }
        return 0u;
    }
    Significant = Count;
    while (Significant != 0u)
    {
        unsigned int Limb = Input[Significant - 1u];
        Significant = Significant - 1u;
        if (Limb != 0u)
        {
            Significant = Significant + 1u;
            break;
        }
    }
    Kind = Context[0] & 7u;
    if (Significant == 0u || Kind == 0u || Kind == 2u || Kind == 4u || Kind == 6u)
    {
        unsigned int Index;
        if (Significant != 0u)
        {
            unsigned int Two = 2u;
            if (CompareLimbs((int)Context, Count, (int)&Two, 1) != 0)
            {
                Ok = 0u;
                Movie_FreeBlock(Block);
                return Ok;
            }
        }
        for (Index = 0u; Index < Count; Index = Index + 1u)
        {
            Output[Index] = Input[Index];
        }
    }
    else if (Kind == 3u || Kind == 7u)
    {
        unsigned int* Shifted = Block + 0x140;
        ShiftLimbs(Context, 0xfffffffeu, Shifted, (int)Count);
        if (Count != 0u)
        {
            if (Shifted[0] <= 0xfffffffeu)
            {
                Shifted[0] = Shifted[0] + 1u;
            }
            else
            {
                unsigned int One = 1u;
                AddLimbs(Shifted, (int)Count, &One, 1, (int)Shifted);
            }
        }
        Movie_ExponentiateByWindow(Input, Shifted, Count, Output, Context);
    }
    else if (Kind == 5u)
    {
        unsigned int* Shifted = Block + 0x140;
        unsigned int* Sum = Block + 0x40;
        ShiftLimbs(Context, 0xfffffffd, Shifted, (int)Count);
        Movie_AddModulo(Input, Input, Sum, Context);
        Movie_ExponentiateByWindow(Sum, Shifted, Count, Block, Context);
        Movie_MultiplyModulo(Input, Block, Sum, Context, Block + 0x180);
        Movie_MultiplyModulo(Sum, Block, Block, Context, Block + 0x180);
        Movie_AddModulo(Block, Block, Block, Context);
        Movie_SubtractModulo(Block, Context + 0x451, Block, Context);
        Movie_MultiplyModulo(Sum, Block, Output, Context, Block + 0x180);
    }
    else
    {
        unsigned int Index;
        Movie_ShiftModulo(Input, -2, Block + 0x80, Context);
        ShiftLimbs(Context, 0xffffffffu, Block + 0x140, (int)Count);
        Block[0x140] = Block[0x140] | 1u;
        for (;;)
        {
            if (Flag != 0u)
            {
                Movie_FreeBlock(Block);
                return Ok;
            }
            ReduceLimbsBelow((int)Context, (unsigned char*)Block, Count);
            LadderLucasModulo(Block, Block + 0x80, Block + 0x140, Count, Block + 0xc0, Block + 0x100, Context);
            Movie_MultiplyModulo(Block + 0x100, Block + 0x100, Block + 0x40, Context, Block + 0x180);
            if (GMovieGraphCode != 0)
            {
                Movie_FreeBlock(Block);
                return 0u;
            }
            if (CompareLimbs((int)(Block + 0x40), Count, (int)Input, (int)Count) == 0)
            {
                for (Index = 0u; Index < Count; Index = Index + 1u)
                {
                    Output[Index] = Block[0x100u + Index];
                }
                Flag = 1u;
            }
            else
            {
                Movie_SubtractModulo(Context, Context + 0x451, Block, Context);
                if (CompareLimbs((int)(Block + 0xc0), Count, (int)(Context + 0x451), (int)Count) != 0 &&
                    CompareLimbs((int)(Block + 0xc0), Count, (int)Block, (int)Count) != 0)
                {
                    Movie_FreeBlock(Block);
                    return 0u;
                }
            }
        }
    }
    Movie_MultiplyModulo(Output, Output, Block + 0x40, Context, Block + 0x180);
    if (CompareLimbs((int)(Block + 0x40), Count, (int)Input, (int)Count) == 0)
    {
        Ok = 1u;
    }
    else
    {
        Ok = 0u;
    }
    Movie_FreeBlock(Block);
    return Ok;
}
