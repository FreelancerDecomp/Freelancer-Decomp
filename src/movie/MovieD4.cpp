// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "MovieD4.h"

// Carry helpers. A 32x32 unsigned MUL is the retail MUL; the high half is EDX.

static unsigned int AddCarry(unsigned int Left, unsigned int Right, unsigned int Carry, unsigned int* Sum)
{
    unsigned long long Wide = (unsigned long long)Left + (unsigned long long)Right + (unsigned long long)Carry;
    *Sum = (unsigned int)Wide;
    return (unsigned int)(Wide >> 32);
}

static unsigned int SubBorrow(unsigned int Left, unsigned int Right, unsigned int Borrow, unsigned int* Difference)
{
    unsigned long long Need = (unsigned long long)Right + (unsigned long long)Borrow;
    *Difference = Left - (unsigned int)Need;
    return (Left < Need) ? 1u : 0u;
}

static void Product32(unsigned int Left, unsigned int Right, unsigned int* Lo, unsigned int* Hi)
{
    unsigned long long Wide = (unsigned long long)Left * (unsigned long long)Right;
    *Lo = (unsigned int)Wide;
    *Hi = (unsigned int)(Wide >> 32);
}

// SUB; SBB reg,reg; ADD; XOR. Sign is 0, or 0xFFFFFFFF when Left < Right.
static unsigned int AbsoluteWord(unsigned int Left, unsigned int Right, unsigned int* Sign)
{
    unsigned int Difference;
    unsigned int Borrow = SubBorrow(Left, Right, 0u, &Difference);
    unsigned int Mask = 0u - Borrow;
    unsigned int Adjusted;
    AddCarry(Difference, Mask, 0u, &Adjusted);
    *Sign = Mask;
    return Adjusted ^ Mask;
}

// 1 when (LowA, HighA) < (LowB, HighB) as a little-endian 64-bit value.
// Retail compares the high words first, then the low words (0x06acdd18, 0x06acdaf0).
static unsigned int HalfLess(unsigned int LowA, unsigned int HighA, unsigned int LowB, unsigned int HighB)
{
    if (HighA != HighB)
    {
        return (HighA < HighB) ? 1u : 0u;
    }
    return (LowA < LowB) ? 1u : 0u;
}

// 0x06acd4c0. Square body 0x06acd4cf, multiply body 0x06acd503.
void __stdcall MultiplyOneLimbPairs(unsigned int* Left, unsigned int* Right, unsigned int* Product, unsigned int Count)
{
    unsigned int Index;
    unsigned int Lo;
    unsigned int Hi;
    if (Left == Right)
    {
        if (Count == 0u)
        {
            return;
        }
        for (Index = 0u; Index < Count; Index = Index + 1u)
        {
            Product32(Left[Index], Left[Index], &Lo, &Hi);
            Product[Index * 2u] = Lo;
            Product[Index * 2u + 1u] = Hi;
        }
        return;
    }
    if (Count == 0u)
    {
        return;
    }
    for (Index = 0u; Index < Count; Index = Index + 1u)
    {
        Product32(Left[Index], Right[Index], &Lo, &Hi);
        Product[Index * 2u] = Lo;
        Product[Index * 2u + 1u] = Hi;
    }
}

// 0x06acd574. Both limbs are loaded before the first store.
static void SquareTwoLimbs(unsigned int A0, unsigned int A1, unsigned int* Product)
{
    unsigned int Lo00;
    unsigned int Hi00;
    unsigned int Lo01;
    unsigned int Hi01;
    unsigned int Mid;
    unsigned int Carry;
    unsigned int CrossHi;
    unsigned int Out1;
    unsigned int HiCross;
    unsigned int Lo11;
    unsigned int Hi11;
    unsigned int Sum;
    unsigned int High;
    unsigned int Out2;
    unsigned int Out3;
    Product32(A0, A0, &Lo00, &Hi00);
    Product32(A1, A0, &Lo01, &Hi01);
    Carry = AddCarry(Hi00, Lo01, 0u, &Mid);
    AddCarry(Hi01, 0u, Carry, &CrossHi);
    Carry = AddCarry(Lo01, Mid, 0u, &Out1);
    AddCarry(Hi01, 0u, Carry, &HiCross);
    Product32(A1, A1, &Lo11, &Hi11);
    Carry = AddCarry(Lo11, HiCross, 0u, &Sum);
    AddCarry(Hi11, 0u, Carry, &High);
    Carry = AddCarry(Sum, CrossHi, 0u, &Out2);
    AddCarry(High, 0u, Carry, &Out3);
    Product[0] = Lo00;
    Product[1] = Out1;
    Product[2] = Out2;
    Product[3] = Out3;
}

// 0x06acd5f8. b0*a0, b1*a0, b0*a1, b1*a1, folded into four limbs.
static void MultiplyTwoLimbs(unsigned int A0, unsigned int A1, unsigned int B0, unsigned int B1, unsigned int* Product)
{
    unsigned int Lo00;
    unsigned int Hi00;
    unsigned int LoB1A0;
    unsigned int HiB1A0;
    unsigned int Mixed;
    unsigned int Carry;
    unsigned int CrossHi;
    unsigned int LoB0A1;
    unsigned int HiB0A1;
    unsigned int Out1;
    unsigned int MidHi;
    unsigned int Lo11;
    unsigned int Hi11;
    unsigned int Sum;
    unsigned int High;
    unsigned int Out2;
    unsigned int Out3;
    Product32(B0, A0, &Lo00, &Hi00);
    Product32(B1, A0, &LoB1A0, &HiB1A0);
    Carry = AddCarry(LoB1A0, Hi00, 0u, &Mixed);
    AddCarry(HiB1A0, 0u, Carry, &CrossHi);
    Product32(B0, A1, &LoB0A1, &HiB0A1);
    Carry = AddCarry(LoB0A1, Mixed, 0u, &Out1);
    AddCarry(HiB0A1, 0u, Carry, &MidHi);
    Product32(B1, A1, &Lo11, &Hi11);
    Carry = AddCarry(Lo11, CrossHi, 0u, &Sum);
    AddCarry(Hi11, 0u, Carry, &High);
    Carry = AddCarry(Sum, MidHi, 0u, &Out2);
    AddCarry(High, 0u, Carry, &Out3);
    Product[0] = Lo00;
    Product[1] = Out1;
    Product[2] = Out2;
    Product[3] = Out3;
}

// 0x06acd540.
void __stdcall MultiplyTwoLimbPairs(unsigned int* Left, unsigned int* Right, unsigned int* Product, unsigned int Count)
{
    unsigned int Index;
    if (Left == Right)
    {
        if (Count == 0u)
        {
            return;
        }
        for (Index = 0u; Index < Count; Index = Index + 1u)
        {
            SquareTwoLimbs(Left[Index * 2u], Left[Index * 2u + 1u], Product + Index * 4u);
        }
        return;
    }
    if (Count == 0u)
    {
        return;
    }
    for (Index = 0u; Index < Count; Index = Index + 1u)
    {
        MultiplyTwoLimbs(Left[Index * 2u], Left[Index * 2u + 1u], Right[Index * 2u], Right[Index * 2u + 1u], Product + Index * 4u);
    }
}

// 0x06acd72c. Reads are repeated after stores so an overlapping product sees them.
static void SquareThreeLimbs(unsigned int* Value, unsigned int* Product)
{
    unsigned int Lo;
    unsigned int Hi;
    unsigned int Limb1;
    unsigned int Limb2;
    unsigned int Limb3;
    unsigned int Carry;
    unsigned int A0;
    unsigned int A1;
    unsigned int A2;
    A0 = Value[0];
    Product32(A0, A0, &Lo, &Hi);
    Product[0] = Lo;
    Limb1 = Hi;
    A1 = Value[1];
    Product32(A1, A1, &Lo, &Hi);
    Limb2 = Lo;
    Limb3 = Hi;
    A0 = Value[0];
    A1 = Value[1];
    Product32(A0, A1, &Lo, &Hi);
    Carry = AddCarry(Limb1, Lo, 0u, &Limb1);
    Carry = AddCarry(Limb2, Hi, Carry, &Limb2);
    Carry = AddCarry(Limb3, 0u, Carry, &Limb3);
    Carry = AddCarry(Limb1, Lo, 0u, &Limb1);
    Carry = AddCarry(Limb2, Hi, Carry, &Limb2);
    Carry = AddCarry(Limb3, 0u, Carry, &Limb3);
    A2 = Value[2];
    A0 = Value[0];
    Product32(A2, A0, &Lo, &Hi);
    Carry = AddCarry(Limb2, Lo, 0u, &Limb2);
    Carry = AddCarry(Limb3, Hi, Carry, &Limb3);
    Product[1] = Limb1;
    Limb1 = Carry;
    Carry = AddCarry(Limb2, Lo, 0u, &Limb2);
    Carry = AddCarry(Limb3, Hi, Carry, &Limb3);
    Carry = AddCarry(Limb1, 0u, Carry, &Limb1);
    A2 = Value[2];
    A1 = Value[1];
    Product32(A1, A2, &Lo, &Hi);
    Carry = AddCarry(Limb3, Lo, 0u, &Limb3);
    Carry = AddCarry(Limb1, Hi, Carry, &Limb1);
    Product[2] = Limb2;
    Limb2 = Carry;
    Carry = AddCarry(Limb3, Lo, 0u, &Limb3);
    Carry = AddCarry(Limb1, Hi, Carry, &Limb1);
    Carry = AddCarry(Limb2, 0u, Carry, &Limb2);
    A2 = Value[2];
    Product32(A2, A2, &Lo, &Hi);
    Product[3] = Limb3;
    Carry = AddCarry(Limb1, Lo, 0u, &Limb1);
    Carry = AddCarry(Limb2, Hi, Carry, &Limb2);
    Product[4] = Limb1;
    Product[5] = Limb2;
}

// 0x06acd7c8. Diagonal products, then (ai-aj)*(bi-bj).
// ADC EAX, ESP at 0x06acd908 and SBB EAX, ESP at 0x06acd92a cancel, so each is a carry of 0.
static void MultiplyThreeLimbs(unsigned int* Left, unsigned int* Right, unsigned int* Product)
{
    unsigned int A0;
    unsigned int A1;
    unsigned int A2;
    unsigned int B0;
    unsigned int B1;
    unsigned int B2;
    unsigned int Lo;
    unsigned int Hi;
    unsigned int SavedHi;
    unsigned int Acc;
    unsigned int Carry;
    unsigned int SignA;
    unsigned int SignB;
    unsigned int Sign01;
    unsigned int Sign12;
    unsigned int Sign20;
    unsigned int P01Lo;
    unsigned int P01Hi;
    unsigned int P12Lo;
    unsigned int P12Hi;
    unsigned int P20Lo;
    unsigned int P20Hi;
    unsigned int AbsA;
    unsigned int AbsB;
    unsigned int Borrow;
    unsigned int Extension;
    unsigned int R1;
    unsigned int R2;
    unsigned int R3;
    unsigned int R4;
    unsigned int R5;
    A0 = Left[0];
    B0 = Right[0];
    Product32(A0, B0, &Lo, &Hi);
    Product[0] = Lo;
    Acc = Hi;
    A1 = Left[1];
    B1 = Right[1];
    Product32(A1, B1, &Lo, &Hi);
    Carry = AddCarry(Acc, Lo, 0u, &Acc);
    SavedHi = Hi;
    A2 = Left[2];
    B2 = Right[2];
    Carry = AddCarry(SavedHi, 0u, Carry, &SavedHi);
    Product[3] = Acc;
    Product32(A2, B2, &Lo, &Hi);
    Carry = AddCarry(SavedHi, Lo, 0u, &SavedHi);
    R1 = Product[0];
    Carry = AddCarry(Hi, 0u, Carry, &Hi);
    Carry = AddCarry(R1, Acc, 0u, &R1);
    Carry = AddCarry(Acc, SavedHi, Carry, &Acc);
    Product[4] = SavedHi;
    Carry = AddCarry(SavedHi, Hi, Carry, &SavedHi);
    Product[5] = Hi;
    Product[1] = R1;
    R1 = Product[0];
    Carry = AddCarry(Hi, 0u, Carry, &Hi);
    Carry = AddCarry(Acc, R1, 0u, &Acc);
    R1 = Product[3];
    Product[2] = Acc;
    Carry = AddCarry(SavedHi, R1, Carry, &SavedHi);
    R1 = Product[4];
    R5 = Product[5];
    Product[3] = SavedHi;
    Carry = AddCarry(Hi, R1, Carry, &Hi);
    A1 = Left[1];
    Carry = AddCarry(R5, 0u, Carry, &R5);
    A0 = Left[0];
    Product[4] = Hi;
    Product[5] = R5;

    B1 = Right[1];
    B0 = Right[0];
    AbsA = AbsoluteWord(A1, A0, &SignA);
    AbsB = AbsoluteWord(B1, B0, &SignB);
    Sign01 = SignA ^ SignB;
    Product32(AbsA, AbsB, &P01Lo, &P01Hi);
    P01Lo ^= Sign01;
    P01Hi ^= Sign01;

    A2 = Left[2];
    A1 = Left[1];
    B2 = Right[2];
    B1 = Right[1];
    AbsA = AbsoluteWord(A2, A1, &SignA);
    AbsB = AbsoluteWord(B2, B1, &SignB);
    Sign12 = SignA ^ SignB;
    Product32(AbsA, AbsB, &P12Lo, &P12Hi);
    P12Lo ^= Sign12;
    P12Hi ^= Sign12;

    A0 = Left[0];
    A2 = Left[2];
    B0 = Right[0];
    B2 = Right[2];
    R2 = Product[2];
    AbsA = AbsoluteWord(A2, A0, &SignA);
    AbsB = AbsoluteWord(B2, B0, &SignB);
    Sign20 = SignA ^ SignB;
    Product32(AbsA, AbsB, &P20Lo, &P20Hi);
    P20Lo ^= Sign20;
    P20Hi ^= Sign20;
    R3 = Product[3];
    Borrow = SubBorrow(P20Lo, Sign20, 0u, &P20Lo);
    Borrow = SubBorrow(P20Hi, Sign20, Borrow, &P20Hi);
    R4 = Product[4];
    Extension = 0u - Borrow;
    Borrow = SubBorrow(R2, P20Lo, 0u, &R2);
    Borrow = SubBorrow(R3, P20Hi, Borrow, &R3);
    R5 = Product[5];
    Borrow = SubBorrow(R4, Extension, Borrow, &R4);
    R1 = Product[1];
    Borrow = SubBorrow(R5, Extension, Borrow, &R5);

    Carry = AddCarry(R1, Sign01, 0u, &R1);
    Carry = AddCarry(R2, Sign01, Carry, &R2);
    Carry = AddCarry(R3, Sign12, Carry, &R3);
    Carry = AddCarry(R4, Sign12, Carry, &R4);
    AddCarry(R5, 0u, Carry, &R5);

    Borrow = SubBorrow(R1, P01Lo, 0u, &R1);
    Borrow = SubBorrow(R2, P01Hi, Borrow, &R2);
    Product[1] = R1;
    Borrow = SubBorrow(R3, P12Lo, Borrow, &R3);
    Product[2] = R2;
    Borrow = SubBorrow(R4, P12Hi, Borrow, &R4);
    SubBorrow(R5, 0u, Borrow, &R5);
    Product[3] = R3;
    Product[4] = R4;
    Product[5] = R5;
}

// 0x06acd6e0.
void __stdcall MultiplyThreeLimbPairs(unsigned int* Left, unsigned int* Right, unsigned int* Product, unsigned int Count)
{
    unsigned int Index;
    if (Count == 0u)
    {
        return;
    }
    if (Left == Right)
    {
        for (Index = 0u; Index < Count; Index = Index + 1u)
        {
            SquareThreeLimbs(Left + Index * 3u, Product + Index * 6u);
        }
        return;
    }
    for (Index = 0u; Index < Count; Index = Index + 1u)
    {
        MultiplyThreeLimbs(Left + Index * 3u, Right + Index * 3u, Product + Index * 6u);
    }
}

// 0x06acd9a9. Square four limbs into eight.
static void SquareFourLimbs(unsigned int* Value, unsigned int* Product)
{
    unsigned int Lo;
    unsigned int Hi;
    unsigned int Acc0;
    unsigned int Acc1;
    unsigned int Acc2;
    unsigned int Acc3;
    unsigned int Carry;
    unsigned int A0;
    unsigned int A1;
    unsigned int A2;
    unsigned int A3;
    unsigned int SavedA3;
    unsigned int R6;
    unsigned int R7;
    unsigned int DiffLo;
    unsigned int DiffHi;
    unsigned int Sign;
    unsigned int Borrow;
    unsigned int Sq0;
    unsigned int Sq1;
    unsigned int Sq2;
    unsigned int Sq3;
    unsigned int CrossLo;
    unsigned int CrossHi;
    A0 = Value[0];
    Product32(A0, A0, &Lo, &Hi);
    Product[0] = Lo;
    Acc1 = Hi;
    Acc0 = 0u;
    A0 = Value[0];
    A1 = Value[1];
    Product32(A0, A1, &Lo, &Hi);
    Carry = AddCarry(Acc1, Lo, 0u, &Acc1);
    Carry = AddCarry(Acc0, Hi, Carry, &Acc0);
    Carry = AddCarry(Acc1, Lo, 0u, &Acc1);
    Carry = AddCarry(Acc0, Hi, Carry, &Acc0);
    Product[1] = Acc1;
    Acc2 = Carry;
    A1 = Value[1];
    Product32(A1, A1, &Lo, &Hi);
    Carry = AddCarry(Acc0, Lo, 0u, &Acc0);
    A2 = Value[2];
    Carry = AddCarry(Acc2, Hi, Carry, &Acc2);
    Acc3 = 0u;
    Product32(A2, A2, &Lo, &Hi);
    Carry = AddCarry(Acc0, Lo, 0u, &Acc0);
    A2 = Value[2];
    Carry = AddCarry(Acc2, Hi, Carry, &Acc2);
    A3 = Value[3];
    Acc3 = Carry;
    SavedA3 = A3;
    Product32(A2, A3, &Lo, &Hi);
    Carry = AddCarry(Acc2, Lo, 0u, &Acc2);
    Carry = AddCarry(Acc3, Hi, Carry, &Acc3);
    Carry = AddCarry(Acc2, Lo, 0u, &Acc2);
    Carry = AddCarry(Acc3, Hi, Carry, &Acc3);
    Acc1 = Carry;
    Product32(SavedA3, SavedA3, &Lo, &Hi);
    Carry = AddCarry(Lo, Acc3, 0u, &Lo);
    Acc3 = Product[0];
    Carry = AddCarry(Hi, Acc1, Carry, &Hi);
    Carry = AddCarry(Acc3, Acc0, 0u, &Acc3);
    Product[2] = Acc3;
    Acc3 = Product[1];
    Carry = AddCarry(Acc3, Acc2, Carry, &Acc3);
    Carry = AddCarry(Acc0, Lo, Carry, &Acc0);
    Product[3] = Acc3;
    Carry = AddCarry(Acc2, Hi, Carry, &Acc2);
    Product[4] = Acc0;
    Carry = AddCarry(Lo, 0u, Carry, &Lo);
    Product[5] = Acc2;
    Carry = AddCarry(Hi, 0u, Carry, &Hi);
    R6 = Lo;
    R7 = Hi;
    A0 = Value[0];
    A2 = Value[2];
    A1 = Value[1];
    A3 = Value[3];
    Borrow = SubBorrow(A0, A2, 0u, &DiffLo);
    Borrow = SubBorrow(A1, A3, Borrow, &DiffHi);
    Product[7] = R7;
    Sign = 0u - Borrow;
    Product[6] = R6;
    DiffLo ^= Sign;
    DiffHi ^= Sign;
    Borrow = SubBorrow(DiffLo, Sign, 0u, &DiffLo);
    Borrow = SubBorrow(DiffHi, Sign, Borrow, &DiffHi);
    Product32(DiffLo, DiffLo, &Sq0, &Sq1);
    Product32(DiffLo, DiffHi, &CrossLo, &CrossHi);
    Carry = AddCarry(Sq1, CrossLo, 0u, &Sq1);
    Sq2 = 0u;
    Carry = AddCarry(Sq2, CrossHi, Carry, &Sq2);
    Carry = AddCarry(Sq1, CrossLo, 0u, &Sq1);
    Carry = AddCarry(Sq2, CrossHi, Carry, &Sq2);
    Sq3 = Carry;
    Product32(DiffHi, DiffHi, &Lo, &Hi);
    Carry = AddCarry(Sq2, Lo, 0u, &Sq2);
    Carry = AddCarry(Sq3, Hi, Carry, &Sq3);
    A0 = Product[2];
    Borrow = SubBorrow(A0, Sq0, 0u, &A0);
    A1 = Product[3];
    Product[2] = A0;
    Borrow = SubBorrow(A1, Sq1, Borrow, &A1);
    A2 = Product[4];
    Borrow = SubBorrow(A2, Sq2, Borrow, &A2);
    Product[3] = A1;
    A3 = Product[5];
    Product[4] = A2;
    Borrow = SubBorrow(A3, Sq3, Borrow, &A3);
    R6 = Product[6];
    R7 = Product[7];
    Borrow = SubBorrow(R6, 0u, Borrow, &R6);
    Borrow = SubBorrow(R7, 0u, Borrow, &R7);
    Product[5] = A3;
    Product[6] = R6;
    Product[7] = R7;
}

// 0x06acdb14 and 0x06acdd72. Product of two positive 64-bit halves, then the
// absolute (hi-lo) cross is subtracted out of limbs 1..3. EDI on that subtract
// is added back by the following ADC, so it contributes only a carry.
static void ProductOfHalves(unsigned int DiffA0, unsigned int DiffA1, unsigned int DiffB0, unsigned int DiffB1, unsigned int* Piece0, unsigned int* Piece1, unsigned int* Piece2, unsigned int* Piece3)
{
    unsigned int Lo;
    unsigned int Hi;
    unsigned int Lo2;
    unsigned int Hi2;
    unsigned int Sum;
    unsigned int Carry;
    unsigned int P0;
    unsigned int P1;
    unsigned int P2;
    unsigned int P3;
    unsigned int SignA;
    unsigned int SignB;
    unsigned int Sign;
    unsigned int AbsA;
    unsigned int AbsB;
    unsigned int CrossLo;
    unsigned int CrossHi;
    unsigned int Borrow;
    unsigned int Adjusted;
    Product32(DiffB0, DiffA0, &Lo, &Hi);
    P0 = Lo;
    Product32(DiffB1, DiffA1, &Lo2, &Hi2);
    Carry = AddCarry(Lo2, Hi, 0u, &Sum);
    Carry = AddCarry(Hi2, 0u, Carry, &Hi);
    Carry = AddCarry(P0, Sum, 0u, &P1);
    Carry = AddCarry(Sum, Hi, Carry, &P2);
    AddCarry(Hi, 0u, Carry, &P3);
    AbsA = AbsoluteWord(DiffA1, DiffA0, &SignA);
    AbsB = AbsoluteWord(DiffB1, DiffB0, &SignB);
    Sign = SignA ^ SignB;
    Product32(AbsA, AbsB, &CrossLo, &CrossHi);
    CrossLo ^= Sign;
    CrossHi ^= Sign;
    Borrow = SubBorrow(P1, CrossLo, 0u, &P1);
    Borrow = SubBorrow(P2, CrossHi, Borrow, &Adjusted);
    Borrow = SubBorrow(P3, 0u, Borrow, &P3);
    Carry = AddCarry(P1, Sign, 0u, &P1);
    Carry = AddCarry(Sign, Adjusted, Carry, &P2);
    AddCarry(P3, 0u, Carry, &P3);
    *Piece0 = P0;
    *Piece1 = P1;
    *Piece2 = P2;
    *Piece3 = P3;
}

// 0x06acdb00 adds the half product at limb 2. 0x06acdd5e subtracts it.
static void ApplyHalfProduct(unsigned int* Product, unsigned int DiffA0, unsigned int DiffA1, unsigned int DiffB0, unsigned int DiffB1, unsigned int bSubtract)
{
    unsigned int Piece0;
    unsigned int Piece1;
    unsigned int Piece2;
    unsigned int Piece3;
    unsigned int Limb2;
    unsigned int Limb3;
    unsigned int Limb4;
    unsigned int Limb5;
    unsigned int Limb6;
    unsigned int Limb7;
    unsigned int Carry;
    ProductOfHalves(DiffA0, DiffA1, DiffB0, DiffB1, &Piece0, &Piece1, &Piece2, &Piece3);
    Limb2 = Product[2];
    Limb3 = Product[3];
    if (bSubtract != 0u)
    {
        Carry = SubBorrow(Limb2, Piece0, 0u, &Limb2);
        Carry = SubBorrow(Limb3, Piece1, Carry, &Limb3);
        Limb4 = Product[4];
        Product[2] = Limb2;
        Product[3] = Limb3;
        Carry = SubBorrow(Limb4, Piece2, Carry, &Limb4);
        Limb5 = Product[5];
        Carry = SubBorrow(Limb5, Piece3, Carry, &Limb5);
        Limb6 = Product[6];
        Limb7 = Product[7];
        Product[4] = Limb4;
        Carry = SubBorrow(Limb6, 0u, Carry, &Limb6);
        Carry = SubBorrow(Limb7, 0u, Carry, &Limb7);
        Product[5] = Limb5;
        Product[6] = Limb6;
        Product[7] = Limb7;
        return;
    }
    Carry = AddCarry(Limb2, Piece0, 0u, &Limb2);
    Carry = AddCarry(Limb3, Piece1, Carry, &Limb3);
    Limb4 = Product[4];
    Product[2] = Limb2;
    Product[3] = Limb3;
    Carry = AddCarry(Limb4, Piece2, Carry, &Limb4);
    Limb5 = Product[5];
    Limb6 = Product[6];
    Limb7 = Product[7];
    Product[4] = Limb4;
    Carry = AddCarry(Limb5, Piece3, Carry, &Limb5);
    Carry = AddCarry(Limb6, 0u, Carry, &Limb6);
    Carry = AddCarry(Limb7, 0u, Carry, &Limb7);
    Product[5] = Limb5;
    Product[6] = Limb6;
    Product[7] = Limb7;
}

// One 2-limb product used inside the 4-limb multiply. Stores are interleaved
// with the second read of each limb (0x06acdbcf and 0x06acdc50).
static void PlaceTwoByTwo(unsigned int* Left, unsigned int* Right, unsigned int* Product)
{
    unsigned int Lo;
    unsigned int Hi;
    unsigned int A0;
    unsigned int A1;
    unsigned int B0;
    unsigned int B1;
    unsigned int Sum;
    unsigned int Carry;
    unsigned int Limb3;
    unsigned int SignA;
    unsigned int SignB;
    unsigned int Sign;
    unsigned int AbsA;
    unsigned int AbsB;
    unsigned int CrossLo;
    unsigned int CrossHi;
    unsigned int Borrow;
    unsigned int Mid;
    A0 = Left[0];
    B0 = Right[0];
    Product32(A0, B0, &Lo, &Hi);
    A1 = Left[1];
    B1 = Right[1];
    Product32(A1, B1, &CrossLo, &CrossHi);
    Carry = AddCarry(CrossLo, Hi, 0u, &Sum);
    Product[0] = Lo;
    Carry = AddCarry(CrossHi, 0u, Carry, &CrossHi);
    Carry = AddCarry(Lo, Sum, 0u, &Mid);
    Carry = AddCarry(Sum, CrossHi, Carry, &Sum);
    Product[1] = Mid;
    Carry = AddCarry(CrossHi, 0u, Carry, &Limb3);
    Product[2] = Sum;
    B1 = Right[1];
    B0 = Right[0];
    Product[3] = Limb3;
    AbsB = AbsoluteWord(B1, B0, &SignB);
    A0 = Left[0];
    A1 = Left[1];
    AbsA = AbsoluteWord(A1, A0, &SignA);
    Sign = SignA ^ SignB;
    Product32(AbsB, AbsA, &CrossLo, &CrossHi);
    CrossLo ^= Sign;
    CrossHi ^= Sign;
    Mid = Product[1];
    Borrow = SubBorrow(Mid, CrossLo, 0u, &Mid);
    Sum = Product[2];
    Borrow = SubBorrow(Sum, CrossHi, Borrow, &Sum);
    Limb3 = Product[3];
    Borrow = SubBorrow(Limb3, 0u, Borrow, &Limb3);
    Carry = AddCarry(Mid, Sign, 0u, &Mid);
    Carry = AddCarry(Sign, Sum, Carry, &Sum);
    Carry = AddCarry(Limb3, 0u, Carry, &Limb3);
    Product[1] = Mid;
    Product[2] = Sum;
    Product[3] = Limb3;
}

// 0x06acdbcf. Low pair, high pair folded into it, then the half-difference product.
static void MultiplyFourLimbs(unsigned int* Left, unsigned int* Right, unsigned int* Product)
{
    unsigned int A0;
    unsigned int A1;
    unsigned int A2;
    unsigned int A3;
    unsigned int B0;
    unsigned int B1;
    unsigned int B2;
    unsigned int B3;
    unsigned int Lo;
    unsigned int Hi;
    unsigned int Sum;
    unsigned int Carry;
    unsigned int Limb3;
    unsigned int SignA;
    unsigned int SignB;
    unsigned int Sign;
    unsigned int AbsA;
    unsigned int AbsB;
    unsigned int CrossLo;
    unsigned int CrossHi;
    unsigned int Borrow;
    unsigned int Mid;
    unsigned int R4;
    unsigned int Fold0;
    unsigned int Fold1;
    unsigned int bHighLess;
    unsigned int DiffA0;
    unsigned int DiffA1;
    unsigned int DiffB0;
    unsigned int DiffB1;
    PlaceTwoByTwo(Left, Right, Product);

    A2 = Left[2];
    B2 = Right[2];
    Product32(A2, B2, &Lo, &Hi);
    A3 = Left[3];
    B3 = Right[3];
    Product32(A3, B3, &CrossLo, &CrossHi);
    Carry = AddCarry(CrossLo, Hi, 0u, &Sum);
    Product[4] = Lo;
    Carry = AddCarry(CrossHi, 0u, Carry, &CrossHi);
    Carry = AddCarry(Lo, Sum, 0u, &Mid);
    Carry = AddCarry(Sum, CrossHi, Carry, &Sum);
    Product[5] = Mid;
    Carry = AddCarry(CrossHi, 0u, Carry, &Limb3);
    Product[6] = Sum;
    B3 = Right[3];
    B2 = Right[2];
    Product[7] = Limb3;
    AbsB = AbsoluteWord(B3, B2, &SignB);
    A2 = Left[2];
    A3 = Left[3];
    AbsA = AbsoluteWord(A3, A2, &SignA);
    Sign = SignA ^ SignB;
    Product32(AbsB, AbsA, &CrossLo, &CrossHi);
    CrossLo ^= Sign;
    CrossHi ^= Sign;
    Mid = Product[5];
    Borrow = SubBorrow(Mid, CrossLo, 0u, &Mid);
    Sum = Product[6];
    Borrow = SubBorrow(Sum, CrossHi, Borrow, &Sum);
    Limb3 = Product[7];
    Borrow = SubBorrow(Limb3, 0u, Borrow, &Limb3);
    Carry = AddCarry(Mid, Sign, 0u, &Mid);
    Carry = AddCarry(Sign, Sum, Carry, &Sum);
    R4 = Product[4];
    Carry = AddCarry(Limb3, 0u, Carry, &Limb3);

    Fold0 = Product[2];
    Carry = AddCarry(R4, Fold0, 0u, &R4);
    Fold0 = Product[3];
    Carry = AddCarry(Mid, Fold0, Carry, &Mid);
    Fold0 = Product[0];
    Carry = AddCarry(Sum, 0u, Carry, &Sum);
    Fold1 = Product[1];
    Carry = AddCarry(Limb3, 0u, Carry, &Limb3);
    Carry = AddCarry(Fold0, R4, 0u, &Fold0);
    Carry = AddCarry(Fold1, Mid, Carry, &Fold1);
    Product[2] = Fold0;
    Carry = AddCarry(R4, Sum, Carry, &R4);
    Product[3] = Fold1;
    Carry = AddCarry(Mid, Limb3, Carry, &Mid);
    Carry = AddCarry(Sum, 0u, Carry, &Sum);
    Product[4] = R4;
    Product[5] = Mid;
    Carry = AddCarry(Limb3, 0u, Carry, &Limb3);
    Product[6] = Sum;

    A1 = Left[1];
    A3 = Left[3];
    Product[7] = Limb3;
    A2 = Left[2];
    A0 = Left[0];
    bHighLess = HalfLess(A2, A3, A0, A1);
    B0 = Right[0];
    B1 = Right[1];
    B2 = Right[2];
    B3 = Right[3];
    if (bHighLess != 0u)
    {
        Borrow = SubBorrow(A0, A2, 0u, &DiffA0);
        SubBorrow(A1, A3, Borrow, &DiffA1);
        if (HalfLess(B0, B1, B2, B3) == 0u)
        {
            Borrow = SubBorrow(B0, B2, 0u, &DiffB0);
            SubBorrow(B1, B3, Borrow, &DiffB1);
            ApplyHalfProduct(Product, DiffA0, DiffA1, DiffB0, DiffB1, 1u);
        }
        else
        {
            Borrow = SubBorrow(B2, B0, 0u, &DiffB0);
            SubBorrow(B3, B1, Borrow, &DiffB1);
            ApplyHalfProduct(Product, DiffA0, DiffA1, DiffB0, DiffB1, 0u);
        }
        return;
    }
    Borrow = SubBorrow(A2, A0, 0u, &DiffA0);
    SubBorrow(A3, A1, Borrow, &DiffA1);
    if (HalfLess(B2, B3, B0, B1) != 0u)
    {
        Borrow = SubBorrow(B0, B2, 0u, &DiffB0);
        SubBorrow(B1, B3, Borrow, &DiffB1);
        ApplyHalfProduct(Product, DiffA0, DiffA1, DiffB0, DiffB1, 0u);
        return;
    }
    Borrow = SubBorrow(B2, B0, 0u, &DiffB0);
    SubBorrow(B3, B1, Borrow, &DiffB1);
    ApplyHalfProduct(Product, DiffA0, DiffA1, DiffB0, DiffB1, 1u);
}

// 0x06acd960. ADD EAX,EAX / JZ skips Count 0 and Count 0x80000000.
void __stdcall MultiplyFourLimbPairs(unsigned int* Left, unsigned int* Right, unsigned int* Product, unsigned int Count)
{
    unsigned int Index;
    if ((Count << 1) == 0u)
    {
        return;
    }
    if (Left == Right)
    {
        for (Index = 0u; Index < Count; Index = Index + 1u)
        {
            SquareFourLimbs(Left + Index * 4u, Product + Index * 8u);
        }
        return;
    }
    for (Index = 0u; Index < Count; Index = Index + 1u)
    {
        MultiplyFourLimbs(Left + Index * 4u, Right + Index * 4u, Product + Index * 8u);
    }
}

// One source limb times Factor. The high half of the last MUL is stored at Dest[Count].
// First multiply row: 0x06acde92, 0x06ace162, 0x06ace482.
static void MultiplyByLimb(unsigned int* Dest, unsigned int* Source, unsigned int Count, unsigned int Factor)
{
    unsigned int Index;
    unsigned int Lo;
    unsigned int Hi;
    unsigned int Carry;
    unsigned int Overflow;
    Carry = 0u;
    for (Index = 0u; Index < Count; Index = Index + 1u)
    {
        Product32(Source[Index], Factor, &Lo, &Hi);
        Overflow = AddCarry(Lo, Carry, 0u, &Dest[Index]);
        Carry = Hi + Overflow;
    }
    Dest[Count] = Carry;
}

// Same product added into Dest[0..Count-1]. Dest[Count] is the outgoing high half, stored.
// Later multiply rows: 0x06acdef1, 0x06ace1df, 0x06ace53b.
static void AddMultiplyByLimb(unsigned int* Dest, unsigned int* Source, unsigned int Count, unsigned int Factor)
{
    unsigned int Index;
    unsigned int Lo;
    unsigned int Hi;
    unsigned int Sum;
    unsigned int Carry;
    unsigned int Overflow;
    Carry = 0u;
    for (Index = 0u; Index < Count; Index = Index + 1u)
    {
        Product32(Source[Index], Factor, &Lo, &Hi);
        Overflow = AddCarry(Lo, Dest[Index], 0u, &Sum);
        Overflow = AddCarry(Hi, 0u, Overflow, &Hi);
        Overflow = AddCarry(Sum, Carry, 0u, &Dest[Index]);
        Carry = Hi + Overflow;
    }
    Dest[Count] = Carry;
}

// Schoolbook product. Right[0] is written with MultiplyByLimb; each later limb is added.
static void MultiplyEqualLimbs(unsigned int* Left, unsigned int* Right, unsigned int* Product, unsigned int Width)
{
    unsigned int Row;
    MultiplyByLimb(Product, Left, Width, Right[0]);
    for (Row = 1u; Row < Width; Row = Row + 1u)
    {
        AddMultiplyByLimb(Product + Row, Left, Width, Right[Row]);
    }
}

// Square at 0x06acdfa2 / 0x06ace2be / 0x06ace676.
// Cross terms are a[0]*a[1..] at Product[1], then a[k]*a[k+1..] added two limbs higher.
// Just before a[0]*a[0] (0x06ace091, 0x06ace3ad, 0x06ace765) every limb matches that
// walk except limb 0 and the top limb, which retail has not stored yet. The diagonal
// replaces both. Clearing the product first is that zero cross term, then the bytes
// double the cross terms and add a[i]*a[i] at limb 2*i.
static void SquareEqualLimbs(unsigned int* Value, unsigned int* Product, unsigned int Width)
{
    unsigned int Index;
    unsigned int Max;
    unsigned int Step;
    unsigned int Steps;
    unsigned int Here;
    unsigned int From;
    unsigned int Run;
    unsigned int Lo;
    unsigned int Hi;
    unsigned int Carry;
    Max = Width * 2u;
    for (Index = 0u; Index < Max; Index = Index + 1u)
    {
        Product[Index] = 0u;
    }
    MultiplyByLimb(Product + 1u, Value + 1u, Width - 1u, Value[0]);
    Steps = Width - 2u;
    for (Step = 0u; Step < Steps; Step = Step + 1u)
    {
        Here = 3u + Step * 2u;
        From = 2u + Step;
        Run = Steps - Step;
        AddMultiplyByLimb(Product + Here, Value + From, Run, Value[From - 1u]);
    }
    Carry = 0u;
    for (Index = 0u; Index < Max; Index = Index + 1u)
    {
        Carry = AddCarry(Product[Index], Product[Index], Carry, &Product[Index]);
    }
    Carry = 0u;
    for (Index = 0u; Index < Width; Index = Index + 1u)
    {
        Product32(Value[Index], Value[Index], &Lo, &Hi);
        Carry = AddCarry(Product[Index * 2u], Lo, Carry, &Product[Index * 2u]);
        Carry = AddCarry(Product[Index * 2u + 1u], Hi, Carry, &Product[Index * 2u + 1u]);
    }
}

// StrideBytes is the IMUL immediate: the byte length of one 2*Width-limb product.
// The pair walk is LEA end,[product+bytecount] / ADD product,stride / CMP, so a
// Count whose unsigned Count*stride wraps still runs bytecount/stride pairs.
static void WalkLimbPairs(unsigned int* Left, unsigned int* Right, unsigned int* Product, unsigned int Count, unsigned int Width, unsigned int StrideBytes)
{
    unsigned int ByteCount;
    unsigned int Pairs;
    unsigned int Index;
    ByteCount = Count * StrideBytes;
    if (ByteCount == 0u)
    {
        return;
    }
    Pairs = ByteCount / StrideBytes;
    if (Left == Right)
    {
        for (Index = 0u; Index < Pairs; Index = Index + 1u)
        {
            SquareEqualLimbs(Left + Index * Width, Product + Index * (Width * 2u), Width);
        }
        return;
    }
    for (Index = 0u; Index < Pairs; Index = Index + 1u)
    {
        MultiplyEqualLimbs(Left + Index * Width, Right + Index * Width, Product + Index * (Width * 2u), Width);
    }
}

// 0x06acde50. IMUL EAX,EAX,0x30.
void __stdcall MultiplySixLimbPairs(unsigned int* Left, unsigned int* Right, unsigned int* Product, unsigned int Count)
{
    WalkLimbPairs(Left, Right, Product, Count, 6u, 0x30u);
}

// 0x06ace120. IMUL EAX,EAX,0x40.
void __stdcall MultiplyEightLimbPairs(unsigned int* Left, unsigned int* Right, unsigned int* Product, unsigned int Count)
{
    WalkLimbPairs(Left, Right, Product, Count, 8u, 0x40u);
}

// 0x06ace440. IMUL EAX,EAX,0x60.
void __stdcall MultiplyTwelveLimbPairs(unsigned int* Left, unsigned int* Right, unsigned int* Product, unsigned int Count)
{
    WalkLimbPairs(Left, Right, Product, Count, 12u, 0x60u);
}
