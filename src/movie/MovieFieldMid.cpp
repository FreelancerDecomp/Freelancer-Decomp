// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "MovieFieldMid.h"

#include "MovieField.h"
#include "MovieGraph.h"
#include "MovieMod.h"
#include "MovieWindow.h"

#include <intrin.h>

// Callees defined in other movie translation units.
long __stdcall Movie_ZeroFieldGroups(unsigned int* Dest, int Groups, int* Field);
long __stdcall Movie_CopyFieldGroups(unsigned int* First, unsigned int* Second, int Groups, int* Field);
long __stdcall StoreFieldUnit(unsigned int* Dest, int* FieldHolder);
long __stdcall CompareLimbs(int Left, unsigned int LeftCount, int Right, int RightCount);
long __stdcall ReduceLimbsBelow(int Source, unsigned char* Dest, unsigned int Count);

// 0x06ac52f0
// One-limb division. A leading dividend limb below the divisor is the starting remainder
// and, when Quotient is nonzero, a zero digit. Count below 2 with no reciprocal uses
// 0x06ac5050. Otherwise the reciprocal (built by 0x06ac54d0 when absent) estimates each digit.
// The slow path's quotient slot keeps its previous value when 0x06ac5050 returns on status 9.
unsigned int __stdcall Movie_DivideByLimb(unsigned int* Dividend, unsigned int DivisorLimb, unsigned int* Reciprocal, unsigned int Quotient, int DividendCount)
{
    unsigned int Remainder = 0u;
    int Count = DividendCount;
    unsigned int* QuotientLimbs = (unsigned int*)Quotient;
    if (DividendCount != 0)
    {
        unsigned int Top = Dividend[DividendCount - 1];
        if (Top < DivisorLimb)
        {
            Remainder = Top;
            Count = DividendCount - 1;
            if (Quotient != 0u)
            {
                QuotientLimbs[Count] = 0u;
            }
        }
    }
    if (Reciprocal == 0 && (unsigned int)Count < 2u)
    {
        if (Count - 1 != -1)
        {
            int DigitSlot = DividendCount;
            int Index = Count;
            do
            {
                unsigned int Limb = Dividend[Index - 1];
                Movie_DivideDoubleWord(Limb, Remainder, DivisorLimb, &DigitSlot, (int*)&Remainder);
                if (Quotient != 0u)
                {
                    QuotientLimbs[Index - 1] = (unsigned int)DigitSlot;
                }
                Index = Index - 1;
            }
            while (Index != 0);
        }
        return Remainder;
    }
    unsigned int LocalReciprocal[2];
    unsigned int* Inverse = Reciprocal;
    if (Inverse == 0)
    {
        StoreLeadingReciprocal((int)&DivisorLimb, 1u, LocalReciprocal);
        Inverse = LocalReciprocal;
    }
    if (Count - 1 == -1)
    {
        return Remainder;
    }
    int Index = Count - 1;
    int Left = Count;
    do
    {
        unsigned int Limb = Dividend[Index];
        unsigned int Shift = Inverse[1];
        unsigned int ShiftBits = Shift & 0x1fu;
        unsigned int InverseShift = (0x1fu - Shift) & 0x1fu;
        unsigned int High = ((Limb >> 1) >> InverseShift) | (Remainder << ShiftBits);
        unsigned int Low = Limb << ShiftBits;
        unsigned int InverseLimb = Inverse[0];
        unsigned long long Wide = (unsigned long long)InverseLimb * (unsigned long long)High;
        Wide = Wide + (unsigned long long)Low + ((unsigned long long)High << 32);
        if ((Low & 0x80000000u) != 0u)
        {
            Wide = Wide + (unsigned long long)(InverseLimb >> 1);
        }
        unsigned int Estimate = (unsigned int)(Wide >> 32);
        unsigned int Wrapped = 0xffffffffu - Estimate;
        unsigned int RemainderGap = Remainder - DivisorLimb;
        unsigned long long Step = (unsigned long long)DivisorLimb * (unsigned long long)Wrapped;
        Step = Step + (unsigned long long)Limb + ((unsigned long long)RemainderGap << 32);
        unsigned int StepLow = (unsigned int)Step;
        unsigned int StepHigh = (unsigned int)(Step >> 32);
        Remainder = (StepHigh & DivisorLimb) + StepLow;
        if (Quotient != 0u)
        {
            QuotientLimbs[Index] = StepHigh - Wrapped;
        }
        Index = Index - 1;
        Left = Left - 1;
    }
    while (Left != 0);
    return Remainder;
}

// 0x06ac7570
// Field+0x12c is the odd constant installed by 0x06ac32b0. Two copies, then three zero groups.
long __stdcall Movie_LoadFieldOne(unsigned int* Work, int* FieldHolder, unsigned int* Scratch)
{
    int* Field = (int*)FieldHolder[0];
    int LimbCount = Field[0];
    unsigned int* Constant = (unsigned int*)((unsigned char*)Field + 0x12c);
    (void)Scratch;
    if (Movie_CopyFieldGroups(Constant, Work, 1, Field) == 0)
    {
        return 0;
    }
    if (Movie_CopyFieldGroups(Constant, Work + LimbCount, 1, Field) == 0)
    {
        return 0;
    }
    if (Movie_ZeroFieldGroups(Work + (LimbCount * 2), 3, Field) == 0)
    {
        return 0;
    }
    return 1;
}

// 0x06ac75d0
// NEG / SBB / NEG on the unit path folds any nonzero 0x06ac8190 result to 1.
long __stdcall Movie_ScalePairByTail(unsigned int* First, unsigned int* Second, int* FieldHolder, unsigned int* Scratch)
{
    int* Field = (int*)FieldHolder[0];
    int LimbCount = Field[0];
    unsigned int* FieldWords = (unsigned int*)Field;
    unsigned int* Tail = First + (LimbCount * 2);
    if (Movie_FieldLimbsAreZero((int)Tail, Field) != 0)
    {
        if (StoreFieldUnit(Second, FieldHolder) != 0)
        {
            return 1;
        }
        return 0;
    }
    unsigned int* ScratchHigh = Scratch + LimbCount;
    if (Movie_FieldUnaryByKind(Tail, Scratch, FieldWords) == 0)
    {
        return 0;
    }
    if (Movie_MultiplyFieldLimbs(Scratch, Scratch, ScratchHigh, FieldWords) == 0)
    {
        return 0;
    }
    if (Movie_MultiplyFieldLimbs(First, ScratchHigh, Second, FieldWords) == 0)
    {
        return 0;
    }
    if (Movie_MultiplyFieldLimbs(Scratch, ScratchHigh, ScratchHigh, FieldWords) == 0)
    {
        return 0;
    }
    if (Movie_MultiplyFieldLimbs(First + LimbCount, ScratchHigh, Second + LimbCount, FieldWords) == 0)
    {
        return 0;
    }
    return 1;
}

// 0x06ac7680
// Counters live at State[Index * 0x81 + 0xC5]. JLE rejects a counter that is not positive.
// State[4] is the field holder passed to 0x06ac7e60. State[2] > 0x12 (unsigned) runs 0x06ac61a0.
void __stdcall Movie_RecordWindowSumDiff(unsigned int* SumSlot, unsigned int* DiffSlot, int First, int Second, int* State)
{
    int* FieldHolder = (int*)State[4];
    if (State[0] == 0)
    {
        return;
    }
    if (First <= 0 || Second <= 0 || First > State[3] || Second > State[3])
    {
        State[0] = 0;
        return;
    }
    int* FirstCounter = State + (First * 0x81 + 0xC5);
    if (*FirstCounter <= 0)
    {
        State[0] = 0;
        return;
    }
    int* SecondCounter = State + (Second * 0x81 + 0xC5);
    if (*SecondCounter <= 0)
    {
        State[0] = 0;
        return;
    }
    *FirstCounter = *FirstCounter - 1;
    *SecondCounter = *SecondCounter - 1;
    if (Movie_FieldElementIsUnit((int)SumSlot, FieldHolder) == 0)
    {
        if (Movie_FieldElementIsUnit((int)DiffSlot, FieldHolder) == 0)
        {
            if ((unsigned int)State[2] > 0x12u)
            {
                Movie_AdvanceFieldRecordStates(State);
            }
        }
    }
    Movie_AssignWindowSlot(SumSlot, DiffSlot, First, State, 1);
    Movie_AssignWindowSlot(SumSlot, DiffSlot, Second, State, -1);
}

// 0x06ac7760
// Limb count is captured before 0x06ac6b70. A failed copy still writes the record header and increments the slot counter.
// The saved State[0] is replaced by 0/1 only on the record path. Early rejects leave State[0] unchanged.
void __stdcall Movie_AssignWindowSlot(unsigned int* First, unsigned int* Second, int Slot, int* State, int Sign)
{
    int* FieldHolder = (int*)State[4];
    int* Field = (int*)FieldHolder[0];
    int LimbCount = Field[0];
    int SavedState = State[0];
    if (Slot < 0 || Slot > State[3] || SavedState == 0)
    {
        return;
    }
    if (Movie_FieldElementIsUnit((int)First, FieldHolder) != 0)
    {
        Movie_CommitFieldSlot(Second, Slot, (unsigned int*)State, Sign);
    }
    else if (Movie_FieldElementIsUnit((int)Second, FieldHolder) != 0)
    {
        Movie_CommitFieldSlot(First, Slot, (unsigned int*)State, 1);
    }
    else
    {
        int RecordIndex = Movie_AllocateFieldRecord(State);
        unsigned char* Record = (unsigned char*)State + (unsigned int)RecordIndex * 0x410u;
        unsigned int* Pair = (unsigned int*)(Record + 0x8418);
        int Stored = 0;
        Field = (int*)FieldHolder[0];
        if (Movie_CopyFieldGroups(First, Pair, 2, Field) != 0 &&
            Movie_CopyOrNegatePair(Second, Pair + (LimbCount * 2), FieldHolder, Sign) != 0)
        {
            Stored = 1;
        }
        *(int*)(Record + 0x8818) = 1;
        *(int*)(Record + 0x881c) = 2;
        *(int*)(Record + 0x8820) = Slot;
        int CounterIndex = Slot * 0x81 + 0xC5;
        State[CounterIndex] = (int)((unsigned int)State[CounterIndex] + 1u);
        SavedState = Stored;
    }
    if (State[0] != 0 && SavedState != 0)
    {
        State[0] = 1;
        return;
    }
    State[0] = 0;
}

// NEG / SBB / NEG. Any nonzero EAX becomes 1.
static long Movie_Flag(long Value)
{
    if (Value != 0)
    {
        return 1;
    }
    return 0;
}

// 0x06ac7a3d. Kind 2/3 epilogue. The XOR runs even when the multiplies returned 0,
// reading whatever the stack buffers hold.
static long Movie_MixBinaryElements(unsigned int* Left, unsigned int* Right, unsigned int* Dest, unsigned int* FieldHolder, int* Field, int Count, unsigned int* TempA, unsigned int* TempB, unsigned int* TempC, int Succeeded)
{
    int Keep = 0;
    unsigned int* FieldWords = (unsigned int*)Field;
    if (Succeeded != 0 && Movie_MultiplyFieldLimbs(TempB, TempB, TempA, FieldWords) != 0)
    {
        Keep = 1;
    }
    if (Count != 0)
    {
        unsigned int* HolderWords = FieldHolder + 1;
        int Index = 0;
        do
        {
            TempA[Index] = TempA[Index] ^ Right[Index] ^ HolderWords[Index] ^ TempB[Index];
            Index = Index + 1;
        }
        while (Index != Count);
    }
    if (Keep != 0 && Movie_MultiplyFieldLimbs(TempB, TempA, TempC, FieldWords) != 0)
    {
        Keep = 1;
    }
    else
    {
        Keep = 0;
    }
    if (Count == 0)
    {
        return Keep;
    }
    unsigned int* LeftHigh = Left + Count;
    unsigned int* DestHigh = Dest + Count;
    int Index = 0;
    do
    {
        unsigned int Low = Left[Index] ^ TempA[Index];
        unsigned int High = LeftHigh[Index] ^ TempC[Index] ^ Low;
        DestHigh[Index] = High;
        Dest[Index] = Low;
        Index = Index + 1;
    }
    while (Index != Count);
    return Keep;
}

// 0x06ac7d67. Kind 1 tail after the common product is in TempB.
static long Movie_FinishKind1Add(unsigned int* Left, unsigned int* Right, unsigned int* Dest, int* Field, int Count, unsigned int* TempA, unsigned int* TempB, unsigned int* TempC)
{
    unsigned int* FieldWords = (unsigned int*)Field;
    unsigned int* LeftHigh = Left + Count;
    unsigned int* DestHigh = Dest + Count;
    if (Movie_MultiplyFieldLimbs(TempB, TempB, TempA, FieldWords) == 0)
    {
        return 0;
    }
    if (Movie_SubtractFieldLimbs(TempA, Left, TempA, Field) == 0)
    {
        return 0;
    }
    if (Movie_SubtractFieldLimbs(TempA, Right, TempA, Field) == 0)
    {
        return 0;
    }
    if (Movie_SubtractFieldLimbs(Left, TempA, TempC, Field) == 0)
    {
        return 0;
    }
    if (Movie_CopyFieldGroups(TempA, Dest, 1, Field) == 0)
    {
        return 0;
    }
    if (Movie_MultiplyFieldLimbs(TempB, TempC, TempA, FieldWords) == 0)
    {
        return 0;
    }
    if (Movie_SubtractFieldLimbs(TempA, LeftHigh, DestHigh, Field) == 0)
    {
        return 0;
    }
    return 1;
}

// 0x06ac78a0
long __stdcall Movie_AddFieldElements(unsigned int* Left, unsigned int* Right, unsigned int* Dest, int Sign, unsigned int* FieldHolder)
{
    int* Field = (int*)FieldHolder[0];
    int Kind = Field[2];
    int Kind23 = 0;
    int Count;
    unsigned int* LeftHigh;
    unsigned int* RightHigh;
    unsigned int TempA[64];
    unsigned int TempB[64];
    unsigned int TempC[64];
    int* Holder = (int*)FieldHolder;
    if (Kind == 2 || Kind == 3)
    {
        Kind23 = 1;
    }
    Count = Field[0];
    LeftHigh = Left + Count;
    RightHigh = Right + Count;
    if (Kind != 1 && Kind23 == 0)
    {
        return 0;
    }
    if (Sign != 1 && Sign != -1)
    {
        if (Kind23 == 0)
        {
            return 0;
        }
        return Movie_MixBinaryElements(Left, Right, Dest, FieldHolder, Field, Count, TempA, TempB, TempC, 0);
    }
    if (Movie_FieldElementIsUnit((int)Right, Holder) != 0)
    {
        return Movie_Flag(Movie_CopyFieldGroups(Left, Dest, 2, Field));
    }
    if (Movie_FieldElementIsUnit((int)Left, Holder) != 0)
    {
        if (Sign == 1)
        {
            return Movie_Flag(Movie_CopyFieldGroups(Right, Dest, 2, Field));
        }
        return Movie_Flag(Movie_NegateFieldPair(Right, Dest, Holder));
    }
    if (Sign == -1)
    {
        if (Movie_CopyFieldGroups(RightHigh, TempA, 1, Field) == 0)
        {
            if (Kind23 == 0)
            {
                return 0;
            }
            return Movie_MixBinaryElements(Left, Right, Dest, FieldHolder, Field, Count, TempA, TempB, TempC, 0);
        }
    }
    else if (Kind23 != 0)
    {
        if (Movie_AddFieldLimbs(Right, RightHigh, TempA, Field) == 0)
        {
            return Movie_MixBinaryElements(Left, Right, Dest, FieldHolder, Field, Count, TempA, TempB, TempC, 0);
        }
    }
    else if (Movie_NegateFieldLimbs(RightHigh, TempA, Field) == 0)
    {
        return 0;
    }
    if (Movie_FieldLimbsEqual((int)Left, (int)Right, Field) != 0)
    {
        if (Movie_FieldLimbsEqual((int)LeftHigh, (int)TempA, Field) != 0)
        {
            return Movie_Flag(StoreFieldUnit(Dest, Holder));
        }
        if (Kind23 == 0)
        {
            unsigned int* Factor = FieldHolder + 1;
            if (Movie_AddFieldLimbs(LeftHigh, LeftHigh, TempA, Field) == 0)
            {
                return 0;
            }
            if (Movie_MultiplyFieldLimbs(Left, Left, TempB, (unsigned int*)Field) == 0)
            {
                return 0;
            }
            if (Movie_AddFieldLimbs(TempB, Factor, TempC, Field) == 0)
            {
                return 0;
            }
            if (Movie_AddFieldLimbs(TempB, TempC, TempC, Field) == 0)
            {
                return 0;
            }
            if (Movie_AddFieldLimbs(TempB, TempC, TempC, Field) == 0)
            {
                return 0;
            }
            if (Movie_MultiplyAfterUnary(TempC, TempA, TempB, (unsigned int*)Field) == 0)
            {
                return 0;
            }
            return Movie_FinishKind1Add(Left, Right, Dest, Field, Count, TempA, TempB, TempC);
        }
        if (Movie_MultiplyAfterUnary(LeftHigh, Left, TempB, (unsigned int*)Field) == 0)
        {
            return Movie_MixBinaryElements(Left, Right, Dest, FieldHolder, Field, Count, TempA, TempB, TempC, 0);
        }
        if (Movie_AddFieldLimbs(Left, TempB, TempB, Field) == 0)
        {
            return Movie_MixBinaryElements(Left, Right, Dest, FieldHolder, Field, Count, TempA, TempB, TempC, 0);
        }
        return Movie_MixBinaryElements(Left, Right, Dest, FieldHolder, Field, Count, TempA, TempB, TempC, 1);
    }
    if (Movie_AddFieldLimbs(LeftHigh, TempA, TempC, Field) == 0)
    {
        if (Kind23 == 0)
        {
            return 0;
        }
        return Movie_MixBinaryElements(Left, Right, Dest, FieldHolder, Field, Count, TempA, TempB, TempC, 0);
    }
    if (Kind23 != 0 && Movie_AddFieldLimbs(TempC, Right, TempC, Field) == 0)
    {
        return Movie_MixBinaryElements(Left, Right, Dest, FieldHolder, Field, Count, TempA, TempB, TempC, 0);
    }
    if (Movie_SubtractFieldLimbs(Left, Right, TempA, Field) == 0)
    {
        if (Kind23 == 0)
        {
            return 0;
        }
        return Movie_MixBinaryElements(Left, Right, Dest, FieldHolder, Field, Count, TempA, TempB, TempC, 0);
    }
    if (Movie_MultiplyAfterUnary(TempC, TempA, TempB, (unsigned int*)Field) == 0)
    {
        if (Kind23 == 0)
        {
            return 0;
        }
        return Movie_MixBinaryElements(Left, Right, Dest, FieldHolder, Field, Count, TempA, TempB, TempC, 0);
    }
    if (Kind23 == 0)
    {
        return Movie_FinishKind1Add(Left, Right, Dest, Field, Count, TempA, TempB, TempC);
    }
    return Movie_MixBinaryElements(Left, Right, Dest, FieldHolder, Field, Count, TempA, TempB, TempC, 1);
}

// 0x06ac7e60
// Kind 1 compares each half with Field[4] from the top limb (CMP / SBB / OR 1, then reject any difference).
// Any other kind returns 1 only when all 2*Count limbs are 0 (SETZ).
long __stdcall Movie_FieldElementIsUnit(int Limbs, int* FieldHolder)
{
    int* Field = (int*)FieldHolder[0];
    int Kind = Field[2];
    int LimbCount = Field[0];
    unsigned int* Words = (unsigned int*)Limbs;
    int Index;
    if (Kind != 1)
    {
        Index = LimbCount + LimbCount;
        while (Index != 0)
        {
            unsigned int Limb = Words[Index - 1];
            Index = Index - 1;
            if (Limb != 0u)
            {
                return 0;
            }
        }
        return 1;
    }
    unsigned int* Reference = (unsigned int*)Field[4];
    Index = LimbCount;
    while (Index != 0)
    {
        unsigned int LeftLimb = Words[Index - 1];
        unsigned int RightLimb = Reference[Index - 1];
        Index = Index - 1;
        if (LeftLimb != RightLimb)
        {
            return 0;
        }
    }
    Words = Words + LimbCount;
    Index = LimbCount;
    while (Index != 0)
    {
        unsigned int LeftLimb = Words[Index - 1];
        unsigned int RightLimb = Reference[Index - 1];
        Index = Index - 1;
        if (LeftLimb != RightLimb)
        {
            return 0;
        }
    }
    return 1;
}

// 0x06ac7f20
// NEG / SBB / NEG on both call results. Sign is compared with CMP, so only +1 and -1 match.
long __stdcall Movie_CopyOrNegatePair(unsigned int* Source, unsigned int* Dest, int* FieldHolder, int Sign)
{
    if (Sign == 1)
    {
        return Movie_Flag(Movie_CopyFieldGroups(Source, Dest, 2, (int*)FieldHolder[0]));
    }
    if (Sign == -1)
    {
        return Movie_Flag(Movie_NegateFieldPair(Source, Dest, FieldHolder));
    }
    return 0;
}

// 0x06ac7f80
// A unit source copies its high half. Kind 1 otherwise negates the high half.
// Any other kind adds the low half to the high half. The low half is then copied.
long __stdcall Movie_NegateFieldPair(unsigned int* Source, unsigned int* Dest, int* FieldHolder)
{
    int* Field = (int*)FieldHolder[0];
    int LimbCount = Field[0];
    unsigned int* SourceHigh = Source + LimbCount;
    unsigned int* DestHigh = Dest + LimbCount;
    long Step;
    if (Movie_FieldElementIsUnit((int)Source, FieldHolder) != 0)
    {
        Step = Movie_CopyFieldGroups(SourceHigh, DestHigh, 1, Field);
    }
    else if (Field[2] == 1)
    {
        Step = Movie_NegateFieldLimbs(SourceHigh, DestHigh, Field);
    }
    else
    {
        Step = Movie_AddFieldLimbs(Source, SourceHigh, DestHigh, Field);
    }
    if (Step == 0)
    {
        return 0;
    }
    if (Movie_CopyFieldGroups(Source, Dest, 1, Field) == 0)
    {
        return 0;
    }
    return 1;
}

// 0x06ac8200
// Kind 1 builds 0x1B * Right^3 and (Left+Left)^2, then Left * that sum + the cube, and rejects a zero.
// Kinds 2 and 3 reject a zero Right. Success copies both inputs over those temporaries.
// Record[0] = Field is stored before the copies and is left in place if a copy returns 0.
long __stdcall Movie_CommitFieldPair(unsigned int* Left, unsigned int* Right, unsigned int* FieldWords, unsigned int* Record)
{
    int* Field = (int*)FieldWords;
    int Kind = Field[2];
    unsigned int* Low = Record + 1;
    unsigned int* High = Record + 0x41;
    if (Kind == 1)
    {
        if (Movie_SetFieldFromSigned(0x1bu, High, Field) == 0)
        {
            return 0;
        }
        if (Movie_MultiplyFieldLimbs(High, Right, High, FieldWords) == 0)
        {
            return 0;
        }
        if (Movie_MultiplyFieldLimbs(High, Right, High, FieldWords) == 0)
        {
            return 0;
        }
        if (Movie_MultiplyFieldLimbs(High, Right, High, FieldWords) == 0)
        {
            return 0;
        }
        if (Movie_AddFieldLimbs(Left, Left, Low, Field) == 0)
        {
            return 0;
        }
        if (Movie_MultiplyFieldLimbs(Low, Low, Low, FieldWords) == 0)
        {
            return 0;
        }
        if (Movie_MultiplyThenAddField(Left, Low, High, Low, FieldWords) == 0)
        {
            return 0;
        }
        if (Movie_FieldLimbsAreZero((int)Low, Field) != 0)
        {
            return 0;
        }
    }
    else if (Kind <= 1 || Kind > 3)
    {
        return 0;
    }
    else if (Movie_FieldLimbsAreZero((int)Right, Field) != 0)
    {
        return 0;
    }
    Record[0] = (unsigned int)FieldWords;
    if (Movie_CopyFieldGroups(Left, Low, 1, Field) == 0)
    {
        return 0;
    }
    if (Movie_CopyFieldGroups(Right, High, 1, Field) == 0)
    {
        return 0;
    }
    return 1;
}

// 0x06ac8300
// BSR of (gap | 1), which cannot be zero. 0x06aca450 rewrites that dword until the top bits fit in the gap.
unsigned int __stdcall Movie_DrawBoundedLimb(unsigned int Minuend, unsigned int Subtrahend)
{
    unsigned int Gap;
    unsigned int Mixed;
    unsigned long BitIndex;
    unsigned int Shift;
    unsigned int Drawn;
    if (Subtrahend < Minuend)
    {
        GMovieGraphCode = 0x2d;
        return Minuend;
    }
    Gap = Subtrahend - Minuend;
    Mixed = Gap | 1u;
    BitIndex = 0;
    _BitScanReverse(&BitIndex, Mixed);
    Shift = 0x20u - ((unsigned int)BitIndex + 1u);
    do
    {
        Movie_FillByteBuffer((unsigned char*)&Mixed, 4u);
        Drawn = Mixed >> Shift;
    }
    while (Drawn > Gap);
    return Drawn + Minuend;
}

// 0x06ac8410
// 0x06ac8a90 against the dword 1. A non-positive result, or a zero top limb, zeros Dest and sets status 0x30.
// Otherwise 0x06ac8360 runs until Dest has a nonzero limb.
void __stdcall Movie_FillRandomBelow(int Source, void* Dest, unsigned int Count)
{
    unsigned int One = 1u;
    unsigned int* Words = (unsigned int*)Dest;
    unsigned int Index;
    if (CompareLimbs(Source, Count, (int)&One, 1) <= 0 || ((unsigned int*)Source)[Count - 1u] == 0u)
    {
        GMovieGraphCode = 0x30;
        for (Index = 0u; Index < Count; Index = Index + 1u)
        {
            Words[Index] = 0u;
        }
        return;
    }
    do
    {
        ReduceLimbsBelow(Source, (unsigned char*)Dest, Count);
        Index = Count;
        while (Index != 0u)
        {
            unsigned int Limb = Words[Index - 1u];
            Index = Index - 1u;
            if (Limb != 0u)
            {
                return;
            }
        }
    }
    while (Index == 0u);
}

// 0x06ac8490
// MUL, then ADD the buffer limb, then ADD the carry. The two ADC-from-zero steps fit in 64 bits.
unsigned int __stdcall Movie_AddScaledLimbs(int Source, unsigned int Scale, unsigned int* Buffer, int Count)
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
        unsigned long long Wide = (unsigned long long)Limbs[Index] * (unsigned long long)Scale;
        Wide = Wide + Buffer[Index];
        Wide = Wide + Carry;
        Buffer[Index] = (unsigned int)Wide;
        Carry = (unsigned int)(Wide >> 32);
        Index = Index + 1;
        Left = Left - 1;
    }
    while (Left != 0);
    return Carry;
}

// 0x06ac85c0
// SAR of -Count selects the odd limb. Pairs then use ADC. The second right limb of a pair is read
// before the first limb is stored. Count 0 returns 0. The final ADC EAX,EAX is the carry, 0 or 1.
long __stdcall Movie_AddLimbsCarry(unsigned int* Left, unsigned int* Right, int Dest, int Count)
{
    int Steps;
    int Half;
    unsigned int* Cursor;
    unsigned int* RightPast;
    int Delta;
    unsigned int Sum;
    unsigned int Other;
    unsigned int Carry;
    unsigned long long Wide;
    if (Count == 0)
    {
        return 0;
    }
    Steps = (int)(0u - (unsigned int)Count);
    Half = Steps >> 1;
    Cursor = Left;
    RightPast = Right + Count;
    Delta = (int)((unsigned char*)Dest - (unsigned char*)Left);
    Sum = Cursor[0];
    Other = Right[0];
    Carry = 0u;
    if ((Steps & 1) != 0)
    {
        Cursor = Cursor + 1;
        Wide = (unsigned long long)Sum + Other;
        Sum = (unsigned int)Wide;
        Carry = (unsigned int)(Wide >> 32);
        Half = Half + 1;
        if (Half == 0)
        {
            *(unsigned int*)((unsigned char*)Cursor + Delta - 4) = Sum;
            return (long)Carry;
        }
        *(unsigned int*)((unsigned char*)Cursor + Delta - 4) = Sum;
        Sum = Cursor[0];
        Other = *(RightPast + (Half * 2));
    }
    for (;;)
    {
        Cursor = Cursor + 2;
        Wide = (unsigned long long)Sum + Other + Carry;
        Sum = (unsigned int)Wide;
        Carry = (unsigned int)(Wide >> 32);
        Other = *(RightPast + (Half * 2) + 1);
        *(unsigned int*)((unsigned char*)Cursor + Delta - 8) = Sum;
        Sum = Cursor[-1];
        Wide = (unsigned long long)Sum + Other + Carry;
        Sum = (unsigned int)Wide;
        Carry = (unsigned int)(Wide >> 32);
        Half = Half + 1;
        if (Half == 0)
        {
            break;
        }
        *(unsigned int*)((unsigned char*)Cursor + Delta - 4) = Sum;
        Sum = Cursor[0];
        Other = *(RightPast + (Half * 2));
    }
    *(unsigned int*)((unsigned char*)Cursor + Delta - 4) = Sum;
    return (long)Carry;
}

// 0x06ac8630
// SUB / SBB the product, then SUB / SBB the running borrow. NEG of that high dword is the next borrow.
unsigned int __stdcall Movie_SubtractScaledLimbs(int Source, unsigned int Digit, unsigned int* Buffer, int Count)
{
    unsigned int* Limbs = (unsigned int*)Source;
    unsigned int Borrow = 0u;
    int Left = Count;
    int Index = 0;
    if (Count == 0)
    {
        return 0u;
    }
    do
    {
        unsigned long long Product = (unsigned long long)Limbs[Index] * (unsigned long long)Digit;
        unsigned int ProductLow = (unsigned int)Product;
        unsigned int ProductHigh = (unsigned int)(Product >> 32);
        unsigned int BufferLimb = Buffer[Index];
        unsigned int Diff = BufferLimb - ProductLow;
        unsigned int BorrowBit = (BufferLimb < ProductLow) ? 1u : 0u;
        unsigned int High = 0u - ProductHigh - BorrowBit;
        unsigned int BorrowBit2 = (Diff < Borrow) ? 1u : 0u;
        Diff = Diff - Borrow;
        High = High - BorrowBit2;
        Buffer[Index] = Diff;
        Borrow = 0u - High;
        Index = Index + 1;
        Left = Left - 1;
    }
    while (Left != 0);
    return Borrow;
}
