// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "MovieWide.h"

#include "MovieField.h"
#include "MovieFieldMid.h"

// DAT_06ae097c. Defined in MovieGraph.cpp.
extern int GMovieGraphCode;

long __stdcall AddLimbs(unsigned int* Left, int LeftCount, unsigned int* Right, int RightCount, int Sum);
long __stdcall SubtractLimbs(unsigned int* Minuend, unsigned int* Subtrahend, int Difference, int Count);
long __stdcall CompareLimbs(int Left, unsigned int LeftCount, int Right, int RightCount);

static unsigned int Movie_SignificantLimbs(unsigned int* Limbs, unsigned int Count)
{
    while (Count != 0u)
    {
        if (Limbs[Count - 1u] != 0u)
        {
            return Count;
        }
        Count = Count - 1u;
    }
    return 0u;
}

static void Movie_CopyLimbs(unsigned int* Dest, unsigned int* Source, unsigned int Count)
{
    unsigned int Index;
    for (Index = 0u; Index < Count; Index = Index + 1u)
    {
        Dest[Index] = Source[Index];
    }
}

static void Movie_ZeroBytes(void* Dest, unsigned int ByteCount)
{
    unsigned int Words = ByteCount >> 2;
    unsigned int Tail = ByteCount & 3u;
    unsigned int* Dwords = (unsigned int*)Dest;
    unsigned char* Bytes;
    unsigned int Index;
    for (Index = 0u; Index < Words; Index = Index + 1u)
    {
        Dwords[Index] = 0u;
    }
    Bytes = (unsigned char*)Dest + (Words * 4u);
    for (Index = 0u; Index < Tail; Index = Index + 1u)
    {
        Bytes[Index] = 0u;
    }
}

static unsigned int* Movie_BlockAt(unsigned int* Block, unsigned int ByteOffset)
{
    return (unsigned int*)((unsigned char*)Block + ByteOffset);
}

static unsigned int* Movie_BlockSlot(unsigned int* Block, unsigned int Slot)
{
    return Movie_BlockAt(Block, Slot << 8);
}

static unsigned int Movie_FrameWord(unsigned int* Frame, unsigned int ByteOffset)
{
    return Frame[ByteOffset >> 2];
}

static unsigned int Movie_SubBorrow(unsigned int Minuend, unsigned int Subtrahend, unsigned int Borrow, unsigned int* Difference)
{
    unsigned long long Need = (unsigned long long)Subtrahend + Borrow;
    *Difference = Minuend - (unsigned int)Need;
    return (Minuend < Need) ? 1u : 0u;
}

// 0x06ac86a0
// MUL, then ADD the running carry. The high half is the next carry. Count 0 returns 0.
unsigned int __stdcall Movie_StoreScaledLimbs(int Source, unsigned int Digit, unsigned int* Dest, int Count)
{
    unsigned int* Limbs = (unsigned int*)Source;
    unsigned int Carry = 0u;
    int Left = Count;
    int Index = 0;
    if (Count == 0)
    {
        return 0u;
    }
    do
    {
        unsigned long long Wide = (unsigned long long)Limbs[Index] * (unsigned long long)Digit + Carry;
        Dest[Index] = (unsigned int)Wide;
        Carry = (unsigned int)(Wide >> 32);
        Index = Index + 1;
        Left = Left - 1;
    }
    while (Left != 0);
    return Carry;
}

// 0x06ac86f0
// Count <= 2 writes the low limbs with 32-bit IMUL and drops the add carry (06ac8888).
// Count >= 3 fills the 0x428-byte stack image: diagonal products at ESP+0x10, limb copies
// at ESP+0x220 and ESP+0x324. Cross terms are |Li-L0|*|Rj-R0| added at a sign-adjusted index.
// The odd and even epilogues subtract adjacent buffer limbs into Count outputs.
void __stdcall Movie_MultiplyLowLimbs(unsigned int* Left, unsigned int* Right, unsigned int* Dest, unsigned int Count)
{
    unsigned long long Wide;
    unsigned int Lo;
    unsigned int Hi;
    if (Count == 0u)
    {
        return;
    }
    Wide = (unsigned long long)Left[0] * (unsigned long long)Right[0];
    if ((int)Count <= 2)
    {
        Dest[0] = (unsigned int)Wide;
        if (Count == 2u)
        {
            unsigned int High = (unsigned int)(Wide >> 32);
            High = High + (Left[1] * Right[0]);
            High = High + (Left[0] * Right[1]);
            Dest[1] = High;
        }
        return;
    }

    unsigned int Frame[0x10a] = {};
    unsigned int Index = 1u;
    unsigned int RegB;
    unsigned int RegC;
    unsigned int Edi;
    unsigned int Esi;
    Frame[0x88] = Left[0];
    Frame[0xc9] = Right[0];
    Lo = (unsigned int)Wide;
    Hi = (unsigned int)(Wide >> 32);
    while (Index != Count)
    {
        unsigned int Sum = Lo + Hi;
        unsigned int CarryBit = (Sum < Lo) ? 1u : 0u;
        unsigned int LimbL = Left[Index];
        unsigned int LimbR = Right[Index];
        unsigned int ProdLo;
        unsigned int ProdHi;
        unsigned int Sum2;
        unsigned int Carry2;
        Frame[Index * 2u + 4u] = Lo;
        Frame[Index * 2u + 5u] = 0u;
        Frame[0x88u + Index] = LimbL;
        Frame[0xc9u + Index] = LimbR;
        Index = Index + 1u;
        Wide = (unsigned long long)LimbL * (unsigned long long)LimbR;
        ProdLo = (unsigned int)Wide;
        ProdHi = (unsigned int)(Wide >> 32);
        Sum2 = Sum + ProdLo;
        Carry2 = (Sum2 < Sum) ? 1u : 0u;
        Lo = Sum2;
        Hi = CarryBit + ProdHi;
        Hi = Hi + Carry2;
    }
    Frame[Count * 2u + 4u] = Lo;
    Frame[Count * 2u + 5u] = 0u;
    Frame[Count * 2u + 6u] = 0u;
    Frame[Count * 2u + 7u] = 0u;

    RegB = Frame[0x88];
    RegC = Frame[0xc9];
    Edi = Count * 2u - 2u;
    Esi = 0u;
    for (;;)
    {
        unsigned int TermL = Movie_FrameWord(Frame, 0x220u + Edi * 2u);
        unsigned int TermR = Movie_FrameWord(Frame, 0x324u + Edi * 2u);
        unsigned int Cross = (TermL - RegB) * (TermR - RegC);
        unsigned int Next;
        Frame[Count * 2u + 5u] = Frame[Count * 2u + 5u] + Cross;
        Esi = Esi + 2u;
        for (;;)
        {
            unsigned int RawL = Movie_FrameWord(Frame, 0x21cu + Edi * 2u);
            unsigned int RawR = Movie_FrameWord(Frame, 0x320u + Edi * 2u);
            unsigned int LeftDelta = RawL - RegB;
            unsigned int LeftMask = (RawL < RegB) ? 0xffffffffu : 0u;
            unsigned int RightDelta = RawR - RegC;
            unsigned int RightMask = (RawR < RegC) ? 0xffffffffu : 0u;
            unsigned int AbsL = (LeftDelta + LeftMask) ^ LeftMask;
            unsigned int AbsR = (RightDelta + RightMask) ^ RightMask;
            unsigned int Pos = Edi + Esi - 3u + (LeftMask ^ RightMask);
            unsigned int ProdLo;
            unsigned int ProdHi;
            unsigned int Cur;
            unsigned int NextLimb;
            unsigned int Sum;
            unsigned int Carry;
            Wide = (unsigned long long)AbsL * (unsigned long long)AbsR;
            ProdLo = (unsigned int)Wide;
            ProdHi = (unsigned int)(Wide >> 32);
            Cur = Frame[Pos + 6u];
            NextLimb = Frame[Pos + 8u];
            Sum = Cur + ProdLo;
            Carry = (Sum < Cur) ? 1u : 0u;
            Frame[Pos + 6u] = Sum;
            Edi = Edi - 2u;
            do
            {
                unsigned long long Step;
                Pos = Pos + 2u;
                Step = (unsigned long long)NextLimb + ProdHi + Carry;
                Frame[Pos + 6u] = (unsigned int)Step;
                Carry = (unsigned int)(Step >> 32);
                ProdHi = 0u;
                NextLimb = Frame[Pos + 8u];
            }
            while (Carry != 0u);
            if (Esi == Edi)
            {
                break;
            }
            RegB = Movie_FrameWord(Frame, 0x21cu + Esi * 2u);
            RegC = Movie_FrameWord(Frame, 0x320u + Esi * 2u);
        }
        RegB = Movie_FrameWord(Frame, 0x220u + Esi * 2u);
        RegC = Movie_FrameWord(Frame, 0x324u + Esi * 2u);
        Next = Esi + 2u;
        Edi = (Edi ^ 0xfffffffeu) + Count * 2u;
        if ((int)Next >= (int)Edi)
        {
            break;
        }
    }

    if ((Count & 1u) != 0u)
    {
        unsigned int Half = Count >> 1;
        unsigned int At = 6u;
        unsigned int Borrow = 0u;
        unsigned int Diff;
        unsigned int* Out = Dest;
        Borrow = Movie_SubBorrow(Frame[At], Frame[At + 1u], 0u, &Diff);
        do
        {
            unsigned int Low;
            unsigned int High;
            unsigned int Pair0 = Frame[At + 2u];
            unsigned int Pair1 = Frame[At + 3u];
            unsigned int Next0 = Frame[At + 4u];
            unsigned int Next1 = Frame[At + 5u];
            Out[0] = Diff;
            Out = Out + 2;
            Borrow = Movie_SubBorrow(Pair0, Pair1, Borrow, &Low);
            Borrow = Movie_SubBorrow(Next0, Next1, Borrow, &High);
            At = At + 4u;
            Half = Half - 1u;
            Out[-1] = Low;
            Diff = High;
        }
        while (Half != 0u);
        Out[0] = Diff;
        return;
    }

    {
        unsigned int Half = Count >> 1;
        unsigned int Fold = (Frame[0x88u + Half] - RegB) * (Frame[0xc9u + Half] - RegC);
        unsigned int At = 6u;
        unsigned int Borrow = 0u;
        unsigned int First;
        unsigned int Second;
        unsigned int* Out = Dest;
        Borrow = Movie_SubBorrow(Frame[At], Frame[At + 1u], 0u, &First);
        Borrow = Movie_SubBorrow(Frame[At + 2u], Frame[At + 3u], Borrow, &Second);
        Half = Half - 1u;
        Out[0] = First;
        do
        {
            unsigned int Low;
            unsigned int High;
            unsigned int Pair0 = Frame[At + 4u];
            unsigned int Pair1 = Frame[At + 5u];
            unsigned int Next0 = Frame[At + 6u];
            unsigned int Next1 = Frame[At + 7u];
            Out[1] = Second;
            Out = Out + 2;
            Borrow = Movie_SubBorrow(Pair0, Pair1, Borrow, &Low);
            Borrow = Movie_SubBorrow(Next0, Next1, Borrow, &High);
            At = At + 4u;
            Half = Half - 1u;
            Out[0] = Low;
            Second = High;
        }
        while (Half != 0u);
        Out[1] = Second - Fold;
    }
}

// 0x06ac8910
// The listing pairs limbs with SBB, then propagates the borrow. A cleared borrow copies
// the untouched tail when Dest is not Left. The final borrow is 0 or 1.
long __stdcall Movie_SubtractLimbsWide(unsigned int* Left, int LeftCount, unsigned int* Right, int RightCount, int Sum)
{
    unsigned int* Diff = (unsigned int*)Sum;
    unsigned int Borrow = 0u;
    int Index = 0;
    int Remain;
    if (RightCount != 0)
    {
        for (Index = 0; Index < RightCount; Index = Index + 1)
        {
            unsigned long long Need = (unsigned long long)Right[Index] + Borrow;
            unsigned int Limb = Left[Index];
            Diff[Index] = Limb - (unsigned int)Need;
            Borrow = (Limb < Need) ? 1u : 0u;
        }
    }
    Remain = LeftCount - RightCount;
    while (Remain != 0)
    {
        unsigned int Limb = Left[Index];
        Diff[Index] = Limb - Borrow;
        Borrow = (Limb < Borrow) ? 1u : 0u;
        Index = Index + 1;
        Remain = Remain - 1;
        if (Borrow == 0u && Remain != 0)
        {
            break;
        }
    }
    if (Remain == 0)
    {
        return (long)Borrow;
    }
    if (Diff != Left)
    {
        while (Remain != 0)
        {
            Diff[Index] = Left[Index];
            Index = Index + 1;
            Remain = Remain - 1;
        }
    }
    return 0;
}

// 0x06ac8a40
// The longer vector is the left operand of AddLimbs. A nonzero carry becomes one more limb.
unsigned int __stdcall Movie_AddLimbsLength(unsigned int* Left, unsigned int LeftCount, unsigned int* Right, unsigned int RightCount, int Dest)
{
    long Carry;
    unsigned int Length;
    if (LeftCount < RightCount)
    {
        Carry = AddLimbs(Right, (int)RightCount, Left, (int)LeftCount, Dest);
        Length = RightCount;
    }
    else
    {
        Carry = AddLimbs(Left, (int)LeftCount, Right, (int)RightCount, Dest);
        Length = LeftCount;
    }
    if (Carry != 0)
    {
        ((unsigned int*)Dest)[Length] = (unsigned int)Carry;
        Length = Length + 1u;
    }
    return Length;
}

// 0x06ac8d90
// The longer vector is scaled once per limb of the shorter. Carries land at Dest[LongCount + k].
void __stdcall Movie_MultiplySchoolbook(unsigned int* Left, unsigned int LeftCount, unsigned int* Right, unsigned int RightCount, unsigned int* Dest)
{
    unsigned int* ShortLimbs;
    unsigned int* LongLimbs;
    unsigned int ShortCount;
    unsigned int LongCount;
    unsigned int Index;
    if (LeftCount > RightCount)
    {
        LongLimbs = Left;
        ShortLimbs = Right;
        ShortCount = RightCount;
        LongCount = LeftCount;
    }
    else
    {
        ShortLimbs = Left;
        LongLimbs = Right;
        ShortCount = LeftCount;
        LongCount = RightCount;
    }
    if (ShortCount == 0u)
    {
        for (Index = 0u; Index < LongCount; Index = Index + 1u)
        {
            Dest[Index] = 0u;
        }
        return;
    }
    Dest[LongCount] = Movie_StoreScaledLimbs((int)LongLimbs, ShortLimbs[0], Dest, (int)LongCount);
    Index = 1u;
    while (Index != ShortCount)
    {
        Dest[Index + LongCount] = Movie_AddScaledLimbs((int)LongLimbs, ShortLimbs[Index], Dest + Index, (int)LongCount);
        Index = Index + 1u;
    }
}

// 0x06ac8e40
// Workspace is 0x808 bytes: slots of 0x100 hold the two operands and the two coefficients,
// the quotient sits at +0x400, and the remainder at +0x608. Status 0x27, 0x25, or 0x26.
// 0x06ac91ab returns the gcd length without freeing the block.
unsigned int __stdcall Movie_ExtendedEuclidLimbs(unsigned int* OperandA, unsigned int CountA, unsigned int* OperandB, unsigned int CountB, unsigned int* Coefficient, unsigned int* QuotientOut, unsigned int* Gcd, unsigned int* Product)
{
    unsigned int SigA;
    unsigned int SigB;
    unsigned int* Block;
    unsigned int Lens[4];
    unsigned int Selector;
    unsigned int ResultCount;
    unsigned int* Saved;
    SigA = Movie_SignificantLimbs(OperandA, CountA);
    SigB = Movie_SignificantLimbs(OperandB, CountB);
    Block = (unsigned int*)Movie_AllocateBlock(0x808u);
    if (GMovieGraphCode != 0 || Block == 0)
    {
        if (Coefficient != 0)
        {
            Movie_ZeroBytes(Coefficient, CountB << 2);
        }
        if (QuotientOut != 0)
        {
            Movie_ZeroBytes(QuotientOut, CountA << 2);
        }
        if (Product != 0)
        {
            Movie_ZeroBytes(Product, (CountA + CountB) << 2);
        }
        if (Block != 0)
        {
            Movie_FreeBlock(Block);
        }
        return 0u;
    }

    Movie_CopyLimbs(Block, OperandA, SigA);
    Movie_CopyLimbs(Movie_BlockAt(Block, 0x100u), OperandB, SigB);
    Lens[0] = SigA;
    Lens[1] = SigB;
    if (SigA == 0u || SigB == 0u)
    {
        GMovieGraphCode = 0x27;
        ResultCount = Movie_AddLimbsLength(OperandA, SigA, OperandB, SigB, (int)Gcd);
        Movie_FreeBlock(Block);
        return ResultCount;
    }

    Movie_BlockAt(Block, 0x200u)[0] = 1u;
    Lens[2] = 1u;
    Lens[3] = 0u;
    Selector = (CompareLimbs((int)Movie_BlockAt(Block, 0x100u), SigB, (int)Block, SigA) > 0) ? 1u : 0u;
    if (Lens[1u - Selector] != 0u)
    {
        do
        {
            unsigned int Other;
            unsigned int QuotientWidth;
            unsigned int* Dividend;
            unsigned int* Divisor;
            int RemainderLen;
            unsigned int QuotientLen;
            unsigned int ProductWidth;
            unsigned int ProductLen;
            if (GMovieGraphCode != 0)
            {
                break;
            }
            Other = 1u - Selector;
            QuotientWidth = Lens[Selector] - Lens[Other] + 1u;
            Dividend = Movie_BlockSlot(Block, Selector);
            Divisor = Movie_BlockSlot(Block, Other);
            RemainderLen = Movie_DivideLimbs(
                Dividend,
                Lens[Selector],
                Divisor,
                (int)Lens[Other],
                0,
                (unsigned int)Movie_BlockAt(Block, 0x400u),
                Movie_BlockAt(Block, 0x608u));
            Lens[Selector] = (unsigned int)RemainderLen;
            Movie_CopyLimbs(Dividend, Movie_BlockAt(Block, 0x608u), (unsigned int)RemainderLen);
            QuotientLen = Movie_SignificantLimbs(Movie_BlockAt(Block, 0x400u), QuotientWidth);
            Movie_MultiplySchoolbook(
                Movie_BlockAt(Block, 0x400u),
                QuotientLen,
                Movie_BlockSlot(Block, Other + 2u),
                Lens[Other + 2u],
                Movie_BlockAt(Block, 0x608u));
            ProductWidth = Lens[Other + 2u] + QuotientLen;
            ProductLen = Movie_SignificantLimbs(Movie_BlockAt(Block, 0x608u), ProductWidth);
            Lens[Selector + 2u] = Movie_AddLimbsLength(
                Movie_BlockAt(Block, 0x608u),
                ProductLen,
                Movie_BlockSlot(Block, Selector + 2u),
                Lens[Selector + 2u],
                (int)Movie_BlockSlot(Block, Selector + 2u));
            Selector = Other;
        }
        while (Lens[1u - Selector] != 0u);
    }

    ResultCount = Lens[Selector];
    Movie_CopyLimbs(Gcd, Movie_BlockSlot(Block, Selector), ResultCount);
    Saved = Movie_BlockSlot(Block, 3u - Selector);
    if (CompareLimbs((int)OperandB, CountB, (int)Saved, (int)Lens[3u - Selector]) < 0)
    {
        GMovieGraphCode = 0x25;
        return ResultCount;
    }
    if (CompareLimbs((int)Saved, Lens[3u - Selector], (int)Movie_BlockSlot(Block, Selector + 2u), (int)Lens[Selector + 2u]) < 0)
    {
        GMovieGraphCode = 0x25;
        return ResultCount;
    }

    Movie_ZeroBytes(Movie_BlockAt(Block, 0x200u + Lens[2] * 4u), (CountB - Lens[2]) << 2);
    Movie_ZeroBytes(Movie_BlockAt(Block, 0x300u + Lens[3] * 4u), (CountB - Lens[3]) << 2);
    if (Selector == 0u)
    {
        Movie_CopyLimbs(Coefficient, Movie_BlockAt(Block, 0x200u), CountB);
    }
    else
    {
        SubtractLimbs(Movie_BlockAt(Block, 0x200u), Movie_BlockAt(Block, 0x300u), (int)Coefficient, (int)CountB);
    }
    if (Product != 0)
    {
        Movie_MultiplySchoolbook(OperandA, CountA, Saved, CountB, Product);
    }
    if (QuotientOut == 0)
    {
        Movie_FreeBlock(Block);
        return ResultCount;
    }

    SubtractLimbs(Saved, Coefficient, (int)Saved, (int)CountB);
    Movie_MultiplySchoolbook(OperandA, CountA, Saved, CountB, Movie_BlockAt(Block, 0x608u));
    AddLimbs(
        Movie_BlockAt(Block, 0x608u),
        (int)(CountA + CountB),
        Gcd,
        (int)ResultCount,
        (int)Movie_BlockAt(Block, 0x608u));
    if (Movie_DivideLimbs(
            Movie_BlockAt(Block, 0x608u),
            SigB + CountA,
            OperandB,
            (int)SigB,
            0,
            (unsigned int)Movie_BlockAt(Block, 0x400u),
            Saved) != 0)
    {
        GMovieGraphCode = 0x26;
    }
    Movie_CopyLimbs(QuotientOut, Movie_BlockAt(Block, 0x400u), CountA);
    Movie_FreeBlock(Block);
    return ResultCount;
}
