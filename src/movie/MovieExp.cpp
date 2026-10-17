// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "MovieGraph.h"
#include "MovieMod.h"
#include "MovieWide.h"
#include "MovieWindow.h"

// Callees defined in other movie translation units. Address names stay until those lanes name them.

extern int GMovieGraphCode;

void* __stdcall Movie_AllocateBlock(unsigned int Size);
void __stdcall Movie_FreeBlock(void* Block);
unsigned int __stdcall ShiftLimbs(unsigned int* Source, unsigned int BitCount, unsigned int* Dest, int Count);
long __stdcall SubtractLimbs(unsigned int* Minuend, unsigned int* Subtrahend, int Difference, int Count);
long __stdcall AddLimbs(unsigned int* Left, int LeftCount, unsigned int* Right, int RightCount, int Sum);
long __stdcall Movie_AddLimbsCarry(unsigned int* Left, unsigned int* Right, int Dest, int Count);
long __stdcall CompareLimbs(int Left, unsigned int LeftCount, int Right, int RightCount);

int __stdcall LimbBitLength(int Limbs, int Count);
long __stdcall StoreFieldUnit(unsigned int* Dest, int* FieldHolder);
long __stdcall SeedFieldWork(unsigned int* Value, unsigned int* Work, int* FieldHolder, unsigned int* Upper);
long __stdcall MultiplyFieldLimbs(unsigned int* Left, unsigned int* Right, int* FieldHolder, unsigned int* Scratch);
long __stdcall Movie_LoadFieldOne(unsigned int* Work, int* FieldHolder, unsigned int* Scratch);
long __stdcall Movie_FieldElementIsUnit(int Limbs, int* FieldHolder);
long __stdcall Movie_QueueOrCommitFieldLimbs(unsigned int* Limbs, unsigned int Slot, unsigned int* Context, unsigned int Sign);
void __stdcall Movie_RecordWindowSumDiff(unsigned int* SumSlot, unsigned int* DiffSlot, int First, int Second, int* State);
long __stdcall Movie_SeedAndMultiplyLimbs(unsigned int* Left, unsigned int* Value, unsigned int* Dest, unsigned int* FieldHolder, unsigned int* Scratch);
long __stdcall Movie_MultiplyPairedLimbs(unsigned int* Left, unsigned int* Right, unsigned int* Dest, unsigned int* FieldHolder, unsigned int* Scratch);
long __stdcall Movie_ScalePairByTail(unsigned int* First, unsigned int* Second, int* FieldHolder, unsigned int* Scratch);

// High limb first. -1 when Left < Right, 0 when equal, 1 when Left > Right.
// SBB EAX,EAX / OR EAX,1 after the first differing limb. 06ac45fb and 06ac481d.
static int CompareDescending(unsigned int* Left, unsigned int* Right, unsigned int Count)
{
    unsigned int Index = Count;
    while (Index != 0u)
    {
        unsigned int LeftLimb = Left[Index - 1u];
        unsigned int RightLimb = Right[Index - 1u];
        Index = Index - 1u;
        if (LeftLimb != RightLimb)
        {
            unsigned int Borrow = (unsigned int)(LeftLimb < RightLimb);
            return (int)((0u - Borrow) | 1u);
        }
    }
    return 0;
}

static void CopyDwords(unsigned int* Dest, unsigned int* Source, unsigned int Count)
{
    unsigned int Index;
    for (Index = 0u; Index < Count; Index = Index + 1u)
    {
        Dest[Index] = Source[Index];
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

// Walk from the high limb. 06ac4a34.
static unsigned int SignificantLimbs(unsigned int* Limbs, unsigned int Count)
{
    unsigned int Index = Count;
    while (Index != 0u)
    {
        unsigned int Limb = Limbs[Index - 1u];
        Index = Index - 1u;
        if (Limb != 0u)
        {
            return Index + 1u;
        }
    }
    return 0u;
}

// SAR / NOT / AND 1. Listing 06ac46ff. Reference is the right operand plus 0x1C4.
static unsigned int* AlignOddSlot(unsigned int* Slot, unsigned char* Reference)
{
    int Delta = (int)(Reference - (unsigned char*)Slot);
    int Bit = (~(Delta >> 2)) & 1;
    return Slot + Bit;
}

// 0x06ac45a0. Modular reduction of a long integer. RET 0x18.
// Mode 0 shifts, then three product stages off +0x4A8, +0x830 and +0x120.
// Any other mode uses the factors at +0xA74 and then +0x120.
// Count above LimbCount+2*Dispatch[0], or equal and not strictly below the modulus, stores 0x1F and returns 0.
// A nonzero carry/borrow accumulator stores 0x20. Returns 1 only when that accumulator is 0.
long __stdcall ReduceLimbsModulo(unsigned int* Source, unsigned int Count, unsigned int Mode, unsigned int* Dest, unsigned int* Modulus, void* Scratch)
{
    unsigned char* Context = (unsigned char*)Modulus;
    unsigned int* Dispatch = (unsigned int*)Modulus[0x43];
    unsigned int LimbCount = Modulus[0x40];
    unsigned int Second = Dispatch[0];
    unsigned int Shift = Modulus[0x46];
    unsigned int Limit = LimbCount + (Second * 2u);
    unsigned int Span = Second + LimbCount;
    unsigned char* Work;
    unsigned int* Input;
    unsigned int* TempA;
    unsigned int* TempB;
    unsigned int* Shifted;
    unsigned int* Product;
    unsigned int* ProductHigh;
    unsigned int* PairBase;
    unsigned int* PairTail;
    unsigned int* Active;
    unsigned int* Aligned;
    unsigned int Accumulator;

    if (Count > Limit)
    {
        GMovieGraphCode = 0x1f;
        return 0;
    }
    if (Count == Limit)
    {
        if (CompareDescending(Source + (Second * 2u), Modulus, LimbCount) >= 0)
        {
            GMovieGraphCode = 0x1f;
            return 0;
        }
    }

    Work = (unsigned char*)Scratch;
    if (Work == 0)
    {
        Work = (unsigned char*)Movie_AllocateBlock(0x231c);
        if (Work == 0)
        {
            ZeroDwords(Dest, LimbCount);
            return 0;
        }
    }

    Input = (unsigned int*)(Work + 0xa74);
    TempA = (unsigned int*)(Work + 0x1498);
    TempB = (unsigned int*)(Work + 0x1698);
    Shifted = (unsigned int*)(Work + 0x1898);
    Product = (unsigned int*)(Work + 0x1a98);
    ProductHigh = (unsigned int*)(Work + 0x1df8);
    PairBase = (unsigned int*)(Work + 0x2158);
    PairTail = (unsigned int*)(Work + 0x230c);
    CopyDwords(Input, Source, Count);
    ZeroDwords(Input + Count, Limit - Count);

    if (Mode == 0u)
    {
        unsigned int Difference;
        long Borrow;
        if (Shift == 0u)
        {
            Accumulator = 0u;
            Active = Input;
        }
        else
        {
            Accumulator = ShiftLimbs(Input, Shift, Shifted, (int)Limit);
            Active = Shifted;
        }

        Aligned = AlignOddSlot(PairBase, Context + 0x4a8 + 0x1c4);
        Movie_EvaluateWindow(Active + Span, Second, Aligned, (unsigned char*)PairTail, Dispatch);
        Movie_MultiplyWindows((unsigned int*)Aligned, (unsigned int*)PairTail, (unsigned int*)(Context + 0x4a8), (unsigned int*)(Context + 0x65c), Product, (unsigned int*)Dispatch);
        Movie_MultiplyWindows((unsigned int*)Aligned, (unsigned int*)PairTail, (unsigned int*)(Context + 0x66c), (unsigned int*)(Context + 0x820), ProductHigh, (unsigned int*)Dispatch);
        CopyDwords(TempA, Product, Second);
        AddLimbs(ProductHigh, (int)(Second * 2u), Product + Second, (int)Second, (int)(TempA + Second));
        (Input + Span)[-1] = (Input + Span)[-1] & (0xffffffffu >> (Shift & 0x1fu));
        Borrow = SubtractLimbs(Input, TempA, (int)Input, (int)Span);
        if (Borrow != 0)
        {
            long Added = Movie_AddLimbsCarry(Input + Second, Modulus, (int)(Input + Second), (int)LimbCount);
            Difference = (unsigned int)Borrow - (unsigned int)Added;
        }
        else
        {
            Difference = 0u;
            if (CompareDescending(Input + Second, Modulus, LimbCount) >= 0)
            {
                Difference = (unsigned int)SubtractLimbs(Input + Second, Modulus, (int)(Input + Second), (int)LimbCount);
            }
        }
        Accumulator = Accumulator | Difference;
        if (Shift != 0u)
        {
            Accumulator = Accumulator | ShiftLimbs(Input, Shift, Shifted, (int)Span);
        }

        Aligned = AlignOddSlot(PairBase, Context + 0x830 + 0x1c4);
        Movie_EvaluateWindow(Active + LimbCount, Second, Aligned, (unsigned char*)PairTail, Dispatch);
        Movie_MultiplyWindows((unsigned int*)Aligned, (unsigned int*)PairTail, (unsigned int*)(Context + 0x830), (unsigned int*)(Context + 0x9e4), Product, (unsigned int*)Dispatch);
        CopyDwords(TempA, Product, Second * 2u);
        Difference = (unsigned int)Movie_AddLimbsCarry(TempA, Active + LimbCount - Second, (int)TempA, (int)(Second * 2u));
        if (((Active + LimbCount)[-1] & 0x80000000u) != 0u)
        {
            Difference = Difference + (unsigned int)AddLimbs(TempA, (int)(Second * 2u), (unsigned int*)(Context + 0x9f4), (int)Second, (int)TempA);
        }
        Accumulator = Accumulator | Difference;

        Aligned = AlignOddSlot(PairBase, Context + 0x120 + 0x1c4);
        Movie_EvaluateWindow(TempA + Second, Second, Aligned, (unsigned char*)PairTail, Dispatch);
        Movie_MultiplyWindows((unsigned int*)Aligned, (unsigned int*)PairTail, (unsigned int*)(Context + 0x120), (unsigned int*)(Context + 0x2d4), Product, (unsigned int*)Dispatch);
        Movie_MultiplyWindows((unsigned int*)Aligned, (unsigned int*)PairTail, (unsigned int*)(Context + 0x2e4), (unsigned int*)(Context + 0x498), ProductHigh, (unsigned int*)Dispatch);
        CopyDwords(TempB, Product, Second);
        AddLimbs(ProductHigh, (int)(Second * 2u), Product + Second, (int)Second, (int)(TempB + Second));
        Accumulator = Accumulator | (unsigned int)SubtractLimbs(Input, TempB, (int)Input, (int)Span);
        if (CompareLimbs((int)Input, SignificantLimbs(Input, Span), (int)Modulus, (int)LimbCount) < 0)
        {
            CopyDwords(Dest, Input, LimbCount);
        }
        else
        {
            Borrow = SubtractLimbs(Input, Modulus, (int)Dest, (int)LimbCount);
            Accumulator = Accumulator | (Input[LimbCount] - (unsigned int)Borrow);
        }
    }
    else
    {
        unsigned int* Mid;
        long FirstBorrow;
        long SecondBorrow;
        Aligned = AlignOddSlot(PairBase, Context + 0xa74 + 0x1c4);
        Movie_EvaluateWindow(Input, Second, Aligned, (unsigned char*)PairTail, Dispatch);
        Movie_MultiplyWindows((unsigned int*)Aligned, (unsigned int*)PairTail, (unsigned int*)(Context + 0xa74), (unsigned int*)(Context + 0xc28), Product, (unsigned int*)Dispatch);
        Movie_MultiplyWindows((unsigned int*)Aligned, (unsigned int*)PairTail, (unsigned int*)(Context + 0xc38), (unsigned int*)(Context + 0xdec), ProductHigh, (unsigned int*)Dispatch);
        CopyDwords(TempA, Product, Second);
        AddLimbs(ProductHigh, (int)(Second * 2u), Product + Second, (int)Second, (int)(TempA + Second));
        Mid = Input + Second;
        FirstBorrow = SubtractLimbs(Mid, TempA, (int)Mid, (int)Span);
        if (Second <= 0x18u)
        {
            Movie_MultiplyLowLimbs(Mid, (unsigned int*)(Context + 0xdfc), TempB, Second);
        }
        else
        {
            Movie_EvaluateWindow(Mid, Second, PairBase, (unsigned char*)PairTail, Dispatch);
            Movie_MultiplyWindows((unsigned int*)PairBase, (unsigned int*)PairTail, (unsigned int*)(Context + 0xe7c), (unsigned int*)(Context + 0x1030), Product, (unsigned int*)Dispatch);
            CopyDwords(TempB, Product, Second * 2u);
        }
        Aligned = AlignOddSlot(PairBase, Context + 0x120 + 0x1c4);
        Movie_EvaluateWindow(TempB, Second, Aligned, (unsigned char*)PairTail, Dispatch);
        if (Mid == 0)
        {
            Movie_MultiplyWindows((unsigned int*)Aligned, (unsigned int*)PairTail, (unsigned int*)(Context + 0x120), (unsigned int*)(Context + 0x2d4), Product, (unsigned int*)Dispatch);
        }
        else
        {
            Movie_MultiplyWindowCross((unsigned int*)Aligned, (unsigned char*)PairTail, (unsigned int*)(Context + 0x120), (unsigned char*)(Context + 0x2d4), Mid, Product, Dispatch);
        }
        Movie_MultiplyWindows((unsigned int*)Aligned, (unsigned int*)PairTail, (unsigned int*)(Context + 0x2e4), (unsigned int*)(Context + 0x498), ProductHigh, (unsigned int*)Dispatch);
        CopyDwords(TempA, Product, Second);
        AddLimbs(ProductHigh, (int)(Second * 2u), Product + Second, (int)Second, (int)(TempA + Second));
        SecondBorrow = SubtractLimbs(Input + (Second * 2u), TempA + Second, (int)Dest, (int)LimbCount);
        Accumulator = (unsigned int)FirstBorrow + (unsigned int)SecondBorrow;
        if (Accumulator != 0u)
        {
            Accumulator = Accumulator - (unsigned int)Movie_AddLimbsCarry(Dest, Modulus, (int)Dest, (int)LimbCount);
            if (Accumulator != 0u)
            {
                Accumulator = Accumulator - (unsigned int)Movie_AddLimbsCarry(Dest, Modulus, (int)Dest, (int)LimbCount);
            }
        }
    }

    if (Accumulator != 0u)
    {
        GMovieGraphCode = 0x20;
    }
    if (Scratch == 0)
    {
        Movie_FreeBlock(Work);
    }
    if (Accumulator != 0u)
    {
        return 0;
    }
    return 1;
}

struct FWindowPair
{
    int First;
    int Second;
};

// Slot records are 0x204 bytes. Data sits at +0x140, the use-count at +0x200 inside the record.
static unsigned int* WindowSlot(unsigned char* Frame, int Index)
{
    return (unsigned int*)(Frame + 0x140 + (Index * 0x204));
}

static unsigned int* WindowFlag(unsigned char* Frame, int Index)
{
    return (unsigned int*)(Frame + 0x340 + (Index * 0x204));
}

// 0x06ac5610. Signed sliding-window exponentiation. RET 0x1C, seven arguments.
// Window is accepted only for 3..7. The 0x11578 frame is the chkstk block.
// State at +0x2C is the object FUN_06ac6a90, FUN_06ac7680, FUN_06ac61a0 and FUN_06ac69a0 index:
// +0 success, +4 kind-is-2-or-3, +8 refill counter, +0xC window half, +0x10 field holder.
// +0x11184 is the depth counter at state+0x11158.
long __stdcall ExponentiateSlidingWindow(void* Base, int Window, int Span, void* Exponent, int ExponentCount, void* Extra, void* FieldHolder)
{
    unsigned char Frame[0x11578];
    unsigned int* State = (unsigned int*)(Frame + 0x2c);
    unsigned int* FieldObject = *(unsigned int**)FieldHolder;
    unsigned int* ExponentLimbs = (unsigned int*)Exponent;
    int* Field = (int*)FieldHolder;
    unsigned int Half = 1u << ((Window - 1) & 0x1f);
    unsigned int LimbCount = FieldObject[0];
    unsigned int KindFlag = 0u;
    unsigned int BitLength = (unsigned int)LimbBitLength((int)Exponent, ExponentCount);
    unsigned int* BufD = (unsigned int*)(Frame + 0xd584);
    unsigned int* BufDa = (unsigned int*)(Frame + 0xda84);
    unsigned int* BufDf = (unsigned int*)(Frame + 0xdf84);
    FWindowPair* Pairs = (FWindowPair*)(Frame + 0x40);
    int PairCount = 0;
    int WindowBase;
    int Accum;
    unsigned int BitIndex;
    unsigned char* Cursor;
    unsigned int Index;

    *(unsigned int*)(Frame + 0x11184) = 0u;
    if (FieldObject[2] == 2u || FieldObject[2] == 3u)
    {
        KindFlag = 1u;
    }
    State[1] = KindFlag;
    State[2] = 0u;
    State[3] = Half;
    *(void**)(State + 4) = FieldHolder;
    State[0] = 1u;
    if ((unsigned int)Window < 3u || (unsigned int)Window > 7u)
    {
        State[0] = 0u;
    }

    if ((int)Half >= 0)
    {
        unsigned int Filled = Half + 1u;
        Index = 0u;
        while (Filled != 0u)
        {
            if (State[0] != 0u && StoreFieldUnit(WindowSlot(Frame, (int)Index), Field) != 0)
            {
                State[0] = 1u;
            }
            else
            {
                State[0] = 0u;
            }
            *WindowFlag(Frame, (int)Index) = 0u;
            Index = Index + 1u;
            Filled = Filled - 1u;
        }
    }

    if ((Window - 3) != 0 && State[0] != 0u)
    {
        int Shift = Window - 3;
        int Step = 0;
        int Limit = Window - 3;
        while (State[0] != 0u && Step != Limit)
        {
            unsigned int Power = 1u << Shift;
            unsigned int SpanCount = Power - (unsigned int)(Step & 1);
            if ((int)SpanCount >= 1)
            {
                int PairIndex = PairCount;
                unsigned int Triple = Power * 3u;
                unsigned int K = 1u;
                PairCount = PairCount + (int)SpanCount;
                while (K <= SpanCount)
                {
                    Pairs[PairIndex].First = (int)Triple;
                    Pairs[PairIndex].Second = (int)K;
                    *WindowFlag(Frame, (int)Triple) = *WindowFlag(Frame, (int)Triple) + 1u;
                    *WindowFlag(Frame, (int)K) = *WindowFlag(Frame, (int)K) + 1u;
                    PairIndex = PairIndex + 1;
                    K = K + 1u;
                }
            }
            Step = Step + 1;
            Shift = Shift - 1;
        }
    }

    WindowBase = -Window;
    Accum = 0;
    BitIndex = 0u;
    Cursor = (unsigned char*)Base;
    while ((BitIndex < BitLength || Accum != 0) && State[0] != 0u)
    {
        unsigned int Threshold;
        unsigned int SquareAt;
        if (BitIndex < BitLength)
        {
            unsigned int Word = ExponentLimbs[BitIndex >> 5];
            unsigned int Bit = (Word >> (BitIndex & 0x1fu)) & 1u;
            unsigned int Place = (BitIndex - (unsigned int)WindowBase) & 0x1fu;
            Accum = Accum + (int)(Bit << Place);
        }
        Threshold = (unsigned int)Window + (unsigned int)WindowBase;
        if (BitIndex >= Threshold)
        {
            unsigned int Mask = (Half << 2) - 1u;
            int Digit = (int)(((unsigned int)Accum + Half) & Mask) - (int)Half;
            unsigned int Bound;
            if (Digit > (int)Half)
            {
                Digit = Digit - (int)(Half << 1);
            }
            Accum = Accum - Digit;
            if (Digit != 0)
            {
                unsigned int Sign = 1u;
                unsigned int Magnitude = (unsigned int)Digit;
                if (Digit < 0)
                {
                    Sign = 0xffffffffu;
                    Magnitude = (unsigned int)(-Digit);
                }
                Movie_QueueOrCommitFieldLimbs(BufD, Magnitude, State, Sign);
            }
            if (Accum < 0)
            {
                State[0] = 0u;
            }
            else
            {
                int Aligned = Accum >> (Window & 0x1f);
                Aligned = Aligned << (Window & 0x1f);
                if (Accum != Aligned)
                {
                    State[0] = 0u;
                }
            }
            Bound = (unsigned int)Window * (unsigned int)Span;
            if (Threshold <= Bound)
            {
                WindowBase = (int)Threshold;
                Accum = Accum >> (Window & 0x1f);
                if (State[0] != 0u && SeedFieldWork((unsigned int*)Cursor, BufD, Field, BufDf) != 0)
                {
                    State[0] = 1u;
                }
                else
                {
                    State[0] = 0u;
                }
                Cursor = Cursor + (LimbCount * 8u);
            }
        }
        SquareAt = (unsigned int)Window + (unsigned int)WindowBase;
        while (BitIndex >= SquareAt)
        {
            if (((~(unsigned int)Accum) & 1u) == 0u)
            {
                break;
            }
            if (State[0] == 0u)
            {
                break;
            }
            if (BitIndex >= (BitLength - 1u) && Accum <= (int)Half)
            {
                break;
            }
            Accum = Accum / 2;
            if (MultiplyFieldLimbs(BufD, BufD, Field, BufDf) == 0)
            {
                State[0] = 0u;
            }
            else
            {
                State[0] = 1u;
            }
            WindowBase = WindowBase + 1;
            SquareAt = SquareAt + 1u;
        }
        if ((unsigned int)BitIndex < (unsigned int)WindowBase || BitIndex > ((unsigned int)Window + (unsigned int)WindowBase))
        {
            State[0] = 0u;
        }
        BitIndex = BitIndex + 1u;
    }

    if (State[0] != 0u)
    {
        unsigned int Changed = 1u;
        while (State[0] != 0u && Changed != 0u)
        {
            int Written = 0;
            int Write = 0;
            int Read = 0;
            int Previous = PairCount;
            if (PairCount != 0)
            {
                int Left = PairCount;
                while (Left != 0)
                {
                    int First = Pairs[Read].First;
                    int Second = Pairs[Read].Second;
                    int Sum = First + Second;
                    int Diff = First - Second;
                    if (*WindowFlag(Frame, Sum) == 0u && *WindowFlag(Frame, Diff) == 0u)
                    {
                        Movie_RecordWindowSumDiff(WindowSlot(Frame, Sum), WindowSlot(Frame, Diff), First, Second, (int*)State);
                        if (State[0] == 0u || StoreFieldUnit(WindowSlot(Frame, Sum), Field) == 0 || StoreFieldUnit(WindowSlot(Frame, Diff), Field) == 0)
                        {
                            State[0] = 0u;
                        }
                        else
                        {
                            State[0] = 1u;
                        }
                    }
                    else
                    {
                        Pairs[Write].First = First;
                        Pairs[Write].Second = Second;
                        Written = Written + 1;
                        Write = Write + 1;
                    }
                    Read = Read + 1;
                    Left = Left - 1;
                }
            }
            Changed = (Written != Previous) ? 1u : 0u;
            PairCount = Written;
            if (Changed == 0u && State[2] != 0u)
            {
                Changed = 1u;
                Movie_AdvanceFieldRecordStates((int*)State);
            }
        }
    }

    if ((int)Half >= 1)
    {
        unsigned int Left = Half;
        Index = 1u;
        while (Left != 0u)
        {
            if (*WindowFlag(Frame, (int)Index) != 0u && State[0] != 0u)
            {
                State[0] = 0u;
            }
            Index = Index + 1u;
            Left = Left - 1u;
        }
    }
    if (State[0] != 0u && Movie_LoadFieldOne(BufDa, Field, BufDf) != 0)
    {
        State[0] = 1u;
    }
    else
    {
        State[0] = 0u;
    }

    if (State[0] != 0u)
    {
        int PowerShift = Window - 3;
        unsigned int PowerStep = 0u;
        unsigned int PowerLimit = (unsigned int)(Window - 3);
        while (State[0] != 0u && PowerStep <= PowerLimit)
        {
            unsigned int Power = 1u << (PowerShift & 0x1f);
            unsigned int* Slot = WindowSlot(Frame, (int)(Power * 3u));
            if (MultiplyFieldLimbs(BufDa, BufDa, Field, BufDf) == 0 || Movie_SeedAndMultiplyLimbs(BufDa, Slot, BufDa, (unsigned int*)Field, BufDf) == 0 || StoreFieldUnit(Slot, Field) == 0)
            {
                State[0] = 0u;
            }
            else
            {
                State[0] = 1u;
            }
            PowerStep = PowerStep + 1u;
            PowerShift = PowerShift - 1;
        }
    }
    if (State[0] != 0u)
    {
        if (SeedFieldWork(WindowSlot(Frame, 4), BufD, Field, BufDf) != 0 && MultiplyFieldLimbs(BufD, BufD, Field, BufDf) != 0 && Movie_SeedAndMultiplyLimbs(BufD, WindowSlot(Frame, 2), BufD, (unsigned int*)Field, BufDf) != 0 && Movie_MultiplyPairedLimbs(BufD, BufDa, BufD, (unsigned int*)Field, BufDf) != 0 && Movie_SeedAndMultiplyLimbs(BufDa, WindowSlot(Frame, 1), BufDa, (unsigned int*)Field, BufDf) != 0)
        {
            State[0] = 1u;
        }
        else
        {
            State[0] = 0u;
        }
    }
    if (State[0] != 0u)
    {
        if (MultiplyFieldLimbs(BufD, BufD, Field, BufDf) != 0 && Movie_MultiplyPairedLimbs(BufD, BufDa, BufDa, (unsigned int*)Field, BufDf) != 0 && Movie_ScalePairByTail(BufDa, (unsigned int*)Extra, Field, BufDf) != 0)
        {
            State[0] = 1u;
        }
        else
        {
            State[0] = 0u;
        }
    }

    if ((int)Half >= 1)
    {
        int Slot = 1;
        while (Slot <= (int)Half)
        {
            if (State[0] == 0u)
            {
                return 0;
            }
            if (*WindowFlag(Frame, Slot) != 0u)
            {
                State[0] = 0u;
            }
            if (Slot > 4)
            {
                if (Movie_FieldElementIsUnit((int)WindowSlot(Frame, Slot), Field) == 0)
                {
                    State[0] = 0u;
                }
            }
            Slot = Slot + 1;
        }
    }
    return (long)State[0];
}
