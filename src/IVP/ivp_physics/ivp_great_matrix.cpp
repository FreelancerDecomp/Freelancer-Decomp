// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP linear-constraint solver -- Common.dll .text:06366070-063682f2.
// See ivp_great_matrix.h for the class identification. Every function below
// was transcribed from the retail listing (EXE/Disasm/Common.dll.txt), not
// from the Ghidra decompile; x87 compares are reproduced with their NaN side
// (`TEST AH,0x1; JZ` = jump when a >= b, so the fall-through is written as
// `!(a >= b)`; `TEST AH,0x41; JZ` = jump when a > b).

#include "ivp_great_matrix.h"

#include <cmath>
#include <cstdint>

namespace
{
    // `AND reg,0xfffffff0` on a float pointer.
    float* AlignDown16(float* Ptr)
    {
        return reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(Ptr) & ~static_cast<uintptr_t>(0xF));
    }

    // `(Ptr - AlignDown16(Ptr)) SAR 2`: floats between the aligned block start and Ptr.
    int FloatsAboveBlock(float const* Ptr)
    {
        return static_cast<int>(reinterpret_cast<uintptr_t>(Ptr) & 0xF) >> 2;
    }

    // The four-wide `Dst[k] = Factor * Src[k] + Dst[k]` loop shared by
    // 063660d0 / 06367bb0 / 06367d00 / 06368260: `TEST cnt; JLE` then
    // (cnt + 3) SHR 2 blocks. Product and sum stay on the FPU stack, each
    // element is stored with FSTP float.
    void AddScaledBlocks(float* Dst, float const* Src, int Count, float Factor)
    {
        if (Count <= 0)
            return;
        unsigned int Blocks = static_cast<unsigned int>(Count + 3) >> 2;
        do
        {
            for (int K = 0; K < 4; ++K)
                Dst[K] = (float)((double)(Factor) * (double)(Src[K]) + (double)(Dst[K]));
            Dst += 4;
            Src += 4;
            --Blocks;
        } while (Blocks != 0);
    }

    // The four-wide `P[k] = Factor * P[k]` loop of 063678c0 / 06367960.
    void ScaleBlocks(float* P, int Count, float Factor)
    {
        if (Count <= 0)
            return;
        unsigned int Blocks = static_cast<unsigned int>(Count + 3) >> 2;
        do
        {
            for (int K = 0; K < 4; ++K)
                P[K] = (float)((double)(Factor) * (double)(P[K]));
            P += 4;
            --Blocks;
        } while (Blocks != 0);
    }

    // The four-wide swap loop of 06366220 / 06367a60 (MOV + FLD/FSTP).
    void SwapBlocks(float* A, float* B, int Count)
    {
        if (Count <= 0)
            return;
        unsigned int Blocks = static_cast<unsigned int>(Count + 3) >> 2;
        do
        {
            for (int K = 0; K < 4; ++K)
            {
                float Tmp = A[K];
                A[K] = B[K];
                B[K] = Tmp;
            }
            A += 4;
            B += 4;
            --Blocks;
        } while (Blocks != 0);
    }

    // `XOR EAX,EAX; REP STOSD` with ECX = (Count + 4) & 0x3ffffffc, guarded
    // by `TEST Count; JL` (06367de6-06367dfe, 06367eff-06367f1c).
    void ZeroBlocks(float* P, int Count)
    {
        if (Count < 0)
            return;
        unsigned int Dwords = static_cast<unsigned int>(Count + 4) & 0x3ffffffcu;
        for (unsigned int I = 0; I < Dwords; ++I)
            P[I] = 0.0f;
    }
}

// ============================================================================
// IVP_Great_Matrix_Many_Zero
// ============================================================================

// 06366400: MatrixEps = 0x3089705f (1e-9); +0x0c/+0x10/+0x14 = 0; +0x04 = 0.
// AlignedRowLen (+0x08) is not written.
IVP_Great_Matrix_Many_Zero::IVP_Great_Matrix_Many_Zero()
{
    MatrixEps = 1e-9f;
    MatrixValues = nullptr;
    DesiredVector = nullptr;
    ResultVector = nullptr;
    Columns = 0;
}

// 06366070-063660cc (RET 4)
int IVP_Great_Matrix_Many_Zero::TestResultRow(int Row) const
{
    double Sum = 0.0;
    float const* RowPtr = MatrixValues + AlignedRowLen * Row;
    for (int I = 0; I < Columns; ++I)
        Sum = (double)(ResultVector[I]) * (double)(RowPtr[I]) + Sum;

    double Lhs = std::fabs((double)(DesiredVector[Row]) * (double)(1e-5f)) + Sum;
    if (!(Lhs >= (double)(DesiredVector[Row]))) // FCOMP [d]; TEST AH,1; JNZ -> 0
        return 0;
    return 1;
}

// 063660d0-06366175 (RET 0xc)
void IVP_Great_Matrix_Many_Zero::AddMultipleLine(int First, int Second, float Factor)
{
    float* Src = MatrixValues + (AlignedRowLen + 1) * First;
    float* DstBlock = AlignDown16(MatrixValues + AlignedRowLen * Second + First);
    int Count = FloatsAboveBlock(Src) + Columns - First;
    AddScaledBlocks(DstBlock, AlignDown16(Src), Count, Factor);

    DesiredVector[Second] = (float)((double)(Factor) * (double)(DesiredVector[First]) +
                                               (double)(DesiredVector[Second]));
}

// 06366180-06366190
int IVP_Great_Matrix_Many_Zero::SolveGreatMatrixManyZero()
{
    TransformToLowerNullTriangle();
    return SolveLowerNullMatrix();
}

// 063661a0-063661ac
void IVP_Great_Matrix_Many_Zero::AlignMatrixValues()
{
    MatrixValues = reinterpret_cast<float*>((reinterpret_cast<uintptr_t>(MatrixValues) + 0xc) & ~static_cast<uintptr_t>(0xF));
}

// 063661b0-06366216 (RET 4)
void IVP_Great_Matrix_Many_Zero::FindPivotInColumn(int Col)
{
    float Best = std::fabs(MatrixValues[(AlignedRowLen + 1) * Col]); // FABS; FSTP float
    int Pivot = -1;
    for (int Row = Columns - 1; Row > Col; --Row)
    {
        float Value = std::fabs(MatrixValues[AlignedRowLen * Row + Col]);
        if (Value > Best) // TEST AH,0x41; JNZ -> keep
        {
            Best = Value;
            Pivot = Row;
        }
    }
    if (Pivot >= 0)
        ExchangeRows(Col, Pivot);
}

// 06366220-06366298 (RET 8)
void IVP_Great_Matrix_Many_Zero::ExchangeRows(int RowA, int RowB)
{
    SwapBlocks(MatrixValues + AlignedRowLen * RowA, MatrixValues + AlignedRowLen * RowB, Columns);
    float Tmp = DesiredVector[RowA];
    DesiredVector[RowA] = DesiredVector[RowB];
    DesiredVector[RowB] = Tmp;
}

// 063662a0-06366335
void IVP_Great_Matrix_Many_Zero::TransformToLowerNullTriangle()
{
    for (int Col = 0; Col < Columns; ++Col)
    {
        FindPivotInColumn(Col);
        float Pivot = MatrixValues[(AlignedRowLen + 1) * Col];
        if (!(std::fabs(Pivot) >= MatrixEps)) // TEST AH,1; JNZ LAB_06366327
            continue;
        float Inv = (float)((double)(-1.0f) / (double)(Pivot)); // FDIV; FSTP float local_4
        for (int Row = Col + 1; Row < Columns; ++Row)
        {
            float Value = MatrixValues[AlignedRowLen * Row + Col];
            if (std::fabs(Value) > MatrixEps) // TEST AH,0x41; JNZ skip
                AddMultipleLine(Col, Row, (float)((double)(Value) * (double)(Inv)));
        }
    }
}

// 06366340-063663f3
int IVP_Great_Matrix_Many_Zero::SolveLowerNullMatrix()
{
    for (int Row = Columns - 1; Row >= 0; --Row)
    {
        double Sum = DesiredVector[Row];
        float const* RowPtr = MatrixValues + AlignedRowLen * Row;
        for (int K = Columns - 1; K > Row; --K)
            Sum = Sum - (double)(DesiredVector[K]) * (double)(RowPtr[K]);

        float Diag = RowPtr[Row];
        if (std::fabs(Diag) >= MatrixEps) // TEST AH,1; JZ LAB_063663a8
        {
            DesiredVector[Row] = (float)(Sum / (double)(Diag));
            continue;
        }
        if (!((double)(MatrixEps) * (double)(1000.0f) > std::fabs(Sum))) // TEST AH,0x41; JNZ fail
        {
            for (int I = Columns - 1; I >= 0; --I)
                ResultVector[I] = 0.0f;
            return 0;
        }
        DesiredVector[Row] = 0.0f;
    }
    for (int I = 0; I < Columns; ++I)
        ResultVector[I] = DesiredVector[I];
    return 1;
}

// 06366420-0636653b (RET 0xc)
void IVP_Great_Matrix_Many_Zero::FillFromBiggerMatrix(IVP_Great_Matrix_Many_Zero const* Big, int const* Indices, int Count)
{
    if (Count == 0)
        return;
    if (Indices[Count - 1] == Count - 1)
    {
        // Identity prefix: whole aligned rows are copied in blocks of four.
        for (int I = 0; I < Count; ++I)
        {
            float const* Src = Big->MatrixValues + Indices[I] * Big->AlignedRowLen;
            float* Dst = MatrixValues + AlignedRowLen * I;
            if (AlignedRowLen - 1 >= 0)
            {
                unsigned int Blocks = static_cast<unsigned int>(AlignedRowLen - 1 + 4) >> 2;
                do
                {
                    for (int K = 0; K < 4; ++K)
                        Dst[K] = Src[K];
                    Dst += 4;
                    Src += 4;
                    --Blocks;
                } while (Blocks != 0);
            }
            DesiredVector[I] = Big->DesiredVector[Indices[I]];
        }
        return;
    }
    for (int I = 0; I < Count; ++I)
    {
        float* Dst = MatrixValues + AlignedRowLen * I;
        float const* Src = Big->MatrixValues + Indices[I] * Big->AlignedRowLen;
        for (int J = 0; J < Count; ++J)
            Dst[J] = Src[Indices[J]];
        DesiredVector[I] = Big->DesiredVector[Indices[I]];
    }
}

// 06366540-06366584
void IVP_Great_Matrix_Many_Zero::MultAlignedMatrix()
{
    for (int Row = 0; Row < Columns; ++Row)
    {
        double Sum = 0.0;
        float const* RowPtr = MatrixValues + Row * AlignedRowLen;
        for (int K = 0; K < Columns; ++K)
            Sum = (double)(DesiredVector[K]) * (double)(RowPtr[K]) + Sum;
        ResultVector[Row] = (float)(Sum);
    }
}

// ============================================================================
// IVP_Incr_L_U_Matrix
// ============================================================================

// 063678c0-06367955 (RET 4)
int IVP_Incr_L_U_Matrix::NormalizeRowL(int Row)
{
    float* RowPtr = LMatrix + AlignedRowLen * Row;
    float Diag = RowPtr[Row];
    if (!(std::fabs(Diag) >= MatrixEps))
        return 0;
    float Inv = (float)((double)(1.0f) / (double)(Diag));
    ScaleBlocks(AlignDown16(RowPtr), FloatsAboveBlock(RowPtr) + NSub, Inv);
    RowPtr[Row] = 1.0f;
    return 1;
}

// 06367960-06367a51 (RET 4)
int IVP_Incr_L_U_Matrix::NormalizeRowU(int Row)
{
    int DiagIndex = AlignedRowLen * Row + Row;
    float Diag = UMatrix[DiagIndex];
    if (!(std::fabs(Diag) >= MatrixEps))
        return 0;
    float Inv = (float)((double)(1.0f) / (double)(Diag));

    float* LRow = LMatrix + AlignedRowLen * Row;
    ScaleBlocks(AlignDown16(LRow), FloatsAboveBlock(LRow) + NSub, Inv);

    float* URight = UMatrix + DiagIndex + 1;
    ScaleBlocks(AlignDown16(URight), FloatsAboveBlock(URight) - Row + NSub - 1, Inv);

    UMatrix[DiagIndex] = 1.0f;
    return 1;
}

// 06367a60-06367b3c (RET 8)
void IVP_Incr_L_U_Matrix::ExchangeRowsLU(int RowA, int RowB)
{
    float* UA = UMatrix + AlignedRowLen * RowA + RowA;
    SwapBlocks(AlignDown16(UA), AlignDown16(UMatrix + RowA + AlignedRowLen * RowB), FloatsAboveBlock(UA) - RowA + NSub);

    float* LA = LMatrix + AlignedRowLen * RowA;
    SwapBlocks(AlignDown16(LA), AlignDown16(LMatrix + AlignedRowLen * RowB), FloatsAboveBlock(LA) + NSub);
}

// 06367b40-06367ba5 (RET 4)
void IVP_Incr_L_U_Matrix::PivotInColumnU(int Col)
{
    float Best = std::fabs(UMatrix[(AlignedRowLen + 1) * Col]);
    int Pivot = Col;
    for (int Row = NSub - 1; Row > Col; --Row)
    {
        float Value = std::fabs(UMatrix[AlignedRowLen * Row + Col]);
        if (Value > Best) // TEST AH,0x41; JNZ -> keep
        {
            Best = Value;
            Pivot = Row;
        }
    }
    if (Pivot != Col)
        ExchangeRowsLU(Col, Pivot);
}

// 06367bb0-06367cf4 (RET 0xc)
void IVP_Incr_L_U_Matrix::SubtractRowLU(int Src, int Dst, float Factor)
{
    float NegFactor = -Factor; // FCHS; FST float

    float* USrc = UMatrix + AlignedRowLen * Src + Src + 1;
    AddScaledBlocks(AlignDown16(UMatrix + AlignedRowLen * Dst + Src + 1), AlignDown16(USrc),
                    FloatsAboveBlock(USrc) - Src + NSub - 1, NegFactor);

    float* LSrc = LMatrix + AlignedRowLen * Src;
    AddScaledBlocks(AlignDown16(LMatrix + AlignedRowLen * Dst), AlignDown16(LSrc), FloatsAboveBlock(LSrc) + NSub, NegFactor);

    UMatrix[AlignedRowLen * Dst + Src] = 0.0f;
}

// 06367d00-06367da4 (RET 0xc)
void IVP_Incr_L_U_Matrix::SubtractRowLZero(int Src, int Dst, float Factor)
{
    float NegFactor = -Factor;
    float* LSrc = LMatrix + AlignedRowLen * Src;
    float* LDst = LMatrix + AlignedRowLen * Dst;
    AddScaledBlocks(AlignDown16(LDst), AlignDown16(LSrc), FloatsAboveBlock(LSrc) + NSub, NegFactor);
    LDst[Src] = 0.0f;
}

// 06367db0-06367e8f
int IVP_Incr_L_U_Matrix::FactorFull()
{
    if (NSub == 0)
        return 1;

    // L = identity on the NSub x NSub block (each row zeroed from its aligned start).
    for (int Row = NSub - 1; Row >= 0; --Row)
    {
        float* LRow = LMatrix + AlignedRowLen * Row;
        ZeroBlocks(AlignDown16(LRow), FloatsAboveBlock(LRow) + NSub - 1);
        LRow[Row] = 1.0f;
    }

    for (int Next = 1; Next < NSub; ++Next)
    {
        int Col = Next - 1;
        PivotInColumnU(Col);
        if (NormalizeRowU(Col) == 0)
            return 0;
        for (int Row = NSub - 1; Row >= Next; --Row)
        {
            float Value = UMatrix[AlignedRowLen * Row + Col];
            if (Value < 0.0f || Value > 0.0f) // TEST AH,0x40; JNZ skip (equal or unordered)
                SubtractRowLU(Col, Row, Value);
        }
    }
    return NormalizeRowU(NSub - 1) != 0 ? 1 : 0; // NEG/SBB/NEG
}

// 06367e90-06367f7a
int IVP_Incr_L_U_Matrix::AddNewRowAndColumn()
{
    for (int I = NSub - 1; I >= 0; --I)
        MultVec[I] = InputVec[I];
    MultVecWithL();
    for (int I = NSub - 1; I >= 0; --I)
        UMatrix[AlignedRowLen * I + NSub] = TempVec[I];
    for (int I = NSub - 1; I >= 0; --I)
        LMatrix[AlignedRowLen * I + NSub] = 0.0f;

    float* NewLRow = LMatrix + AlignedRowLen * NSub;
    ZeroBlocks(AlignDown16(NewLRow), FloatsAboveBlock(NewLRow) + NSub - 1);
    LMatrix[(AlignedRowLen + 1) * NSub] = 1.0f;

    int NewRowOffset = AlignedRowLen * NSub;
    NSub = NSub + 1;
    for (int I = 0; I < NSub - 1; ++I)
        SubtractRowLU(I, NSub - 1, UMatrix[NewRowOffset + I]); // read fresh each pass (FLD [EBX+EDX])

    return NormalizeRowU(NSub - 1);
}

// 06367f80-063680d1 (RET 4)
int IVP_Incr_L_U_Matrix::RemoveRowAndColumn(int Index)
{
    ExchangeColumnsL(Index, NSub - 1);
    ExchangeColumnsU(Index, NSub - 1);

    for (int Row = NSub - 2; Row > Index; --Row)
    {
        float Value = UMatrix[AlignedRowLen * Row + Index];
        if (Value < 0.0f || Value > 0.0f)
            SubtractRowL(NSub - 1, Row, Value);
        UMatrix[AlignedRowLen * Row + Index] = 0.0f;
    }

    if (NormalizeRowU(Index) != 1)
    {
        SubtractRowL(NSub - 1, Index, -1.0f);
        UMatrix[(AlignedRowLen + 1) * Index] = 1.0f;
    }

    for (int Col = Index; Col < NSub - 1; ++Col)
    {
        float Value = UMatrix[AlignedRowLen * (NSub - 1) + Col];
        if (Value < 0.0f || Value > 0.0f)
            SubtractRowLU(Col, NSub - 1, Value);
    }

    if (NormalizeRowL(NSub - 1) != 1)
    {
        NSub = NSub - 1;
        return 0;
    }

    int Last = NSub - 1;
    for (int Row = Last - 1; Row >= 0; --Row)
    {
        float Value = LMatrix[AlignedRowLen * Row + Last];
        if (Value < 0.0f || Value > 0.0f)
            SubtractRowLZero(Last, Row, Value);
    }
    NSub = NSub - 1;
    return 1;
}

// 063680e0-06368128 (RET 8)
void IVP_Incr_L_U_Matrix::ExchangeColumnsL(int ColA, int ColB)
{
    for (int Row = 0; Row < NSub; ++Row)
    {
        float Tmp = LMatrix[Row * AlignedRowLen + ColA];
        LMatrix[Row * AlignedRowLen + ColA] = LMatrix[Row * AlignedRowLen + ColB];
        LMatrix[Row * AlignedRowLen + ColB] = Tmp;
    }
}

// 06368130-06368178 (RET 8)
void IVP_Incr_L_U_Matrix::ExchangeColumnsU(int ColA, int ColB)
{
    for (int Row = 0; Row < NSub; ++Row)
    {
        float Tmp = UMatrix[Row * AlignedRowLen + ColA];
        UMatrix[Row * AlignedRowLen + ColA] = UMatrix[Row * AlignedRowLen + ColB];
        UMatrix[Row * AlignedRowLen + ColB] = Tmp;
    }
}

// 06368180-063681c5
void IVP_Incr_L_U_Matrix::MultVecWithL()
{
    for (int Row = NSub - 1; Row >= 0; --Row)
    {
        double Sum = 0.0;
        float const* LRow = LMatrix + Row * AlignedRowLen;
        for (int K = NSub - 1; K >= 0; --K)
            Sum = (double)(LRow[K]) * (double)(MultVec[K]) + Sum;
        TempVec[Row] = (float)(Sum);
    }
}

// 063681d0-06368225
void IVP_Incr_L_U_Matrix::SolveUpperTriangle()
{
    for (int Row = NSub - 1; Row >= 0; --Row)
    {
        double Sum = 0.0;
        float const* URow = UMatrix + AlignedRowLen * Row;
        for (int K = NSub - 1; K > Row; --K)
            Sum = (double)(TempVec[K]) * (double)(URow[K]) + Sum;
        float Value = (float)((double)(TempVec[Row]) - Sum); // FSUBR; FST float; FSTP float
        TempVec[Row] = Value;
        OutVec[Row] = Value;
    }
}

// 06368230-06368259
void IVP_Incr_L_U_Matrix::SolveLU()
{
    for (int I = NSub - 1; I >= 0; --I)
        MultVec[I] = InputVec[I];
    MultVecWithL();
    SolveUpperTriangle();
}

// 06368260-063682f2 (RET 0xc)
void IVP_Incr_L_U_Matrix::SubtractRowL(int Src, int Dst, float Factor)
{
    float NegFactor = -Factor;
    float* LSrc = LMatrix + AlignedRowLen * Src;
    AddScaledBlocks(AlignDown16(LMatrix + AlignedRowLen * Dst), AlignDown16(LSrc), FloatsAboveBlock(LSrc) + NSub, NegFactor);
}

// ============================================================================
// IVP_Linear_Constraint_Solver
// ============================================================================

// 06366590-06366609
void IVP_Linear_Constraint_Solver::UpdateDeltaAccelOfInactives()
{
    for (int I = RActives; I < NVariables; ++I)
    {
        int Var = ActivesInactivesIgnored[I];
        float const* Row = FullAMatrix.MatrixValues + AlignedSize * Var;
        double Sum = 0.0;
        for (int K = 0; K < RActives; ++K)
        {
            int Active = ActivesInactivesIgnored[K];
            Sum = (double)(DeltaF[Active]) * (double)(Row[Active]) + Sum;
        }
        DeltaAccel[Var] = (float)(Sum + (double)(Row[ActivesInactivesIgnored[IgnoredPos]]));
    }
}

// 06366610-06366664
void IVP_Linear_Constraint_Solver::ComputeResidual()
{
    for (int I = 0; I < NVariables; ++I)
    {
        double Sum = 0.0;
        float const* Row = FullA + AlignedSize * I;
        for (int K = NVariables - 1; K >= 0; --K)
            Sum = (double)(Row[K]) * (double)(FullX[K]) + Sum;
        Residual[I] = (float)(Sum - (double)(FullB[I]));
    }
}

// 06366670-063666e5
int IVP_Linear_Constraint_Solver::SolutionIsCorrect()
{
    ComputeResidual();
    for (int I = 0; I < RActives; ++I)
    {
        if (std::fabs(Residual[ActivesInactivesIgnored[I]]) > TestEps) // TEST AH,0x41; JZ -> 0
            return 0;
    }
    for (int I = RActives; I < NVariables; ++I)
    {
        int Var = ActivesInactivesIgnored[I];
        if (std::fabs((double)(Residual[Var]) - (double)(Accel[Var])) > (double)(TestEps))
            return 0;
    }
    return 1;
}

// 063666f0-063669a5 (RET 4). The first twelve blocks are the GetMem sequence
// inlined; the rest call FUN_06347380 itself. Order kept.
void IVP_Linear_Constraint_Solver::AllocateMemory(IVP_U_Memory* Mem)
{
    ActivesInactivesIgnored = static_cast<int*>(Mem->GetMem(AlignedSize * 4));
    VariableIsFoundAt = static_cast<int*>(Mem->GetMem(AlignedSize * 4));
    Accel = static_cast<float*>(Mem->GetMem(AlignedSize * 4));
    DeltaAccel = static_cast<float*>(Mem->GetMem(AlignedSize * 4));
    DeltaF = static_cast<float*>(Mem->GetMem(AlignedSize * 4));
    ResetX = static_cast<float*>(Mem->GetMem(AlignedSize * 4));
    ResetAccel = static_cast<float*>(Mem->GetMem(AlignedSize * 4));
    LuMatrix.LMatrix = static_cast<float*>(Mem->GetMem(AlignedSize * NVariables * 4));
    LuMatrix.UMatrix = static_cast<float*>(Mem->GetMem(AlignedSize * NVariables * 4));
    LuMatrix.InputVec = static_cast<float*>(Mem->GetMem(AlignedSize * 4));
    LuMatrix.OutVec = static_cast<float*>(Mem->GetMem(AlignedSize * 4));
    LuMatrix.TempVec = static_cast<float*>(Mem->GetMem(AlignedSize * 4));
    LuMatrix.MultVec = static_cast<float*>(Mem->GetMem(AlignedSize * 4));
    LuSubSolver.MatrixValues = static_cast<float*>(Mem->GetMem(AlignedSize * NVariables * 4));
    LuSubSolver.DesiredVector = static_cast<float*>(Mem->GetMem(AlignedSize * 4));
    LuSubSolver.ResultVector = static_cast<float*>(Mem->GetMem(AlignedSize * 4));
    SubSolverMat.MatrixValues = static_cast<float*>(Mem->GetMem(AlignedSize * NVariables * 4));
    SubSolverMat.DesiredVector = static_cast<float*>(Mem->GetMem(AlignedSize * 4));
    SubSolverMat.ResultVector = static_cast<float*>(Mem->GetMem(AlignedSize * 4));
}

// 063669b0-06366b03 (RET 0x18). Returns FullSolveLc's EAX (the zeroing
// stores after it use EDI).
int IVP_Linear_Constraint_Solver::InitAndSolveLc(float* A, float* B, float* X, int N, int NumActives, IVP_U_Memory* Mem)
{
    NVariables = N;
    AlignedSize = (N + 3) & ~3;
    AllocateMemory(Mem);

    RActives = 0;
    IgnoredPos = 0;
    SolverEps = 1e-7f; // 0x33d6bf95
    GaussEps = 1e-7f;
    FullAMatrix.Columns = N;
    FullAMatrix.MatrixValues = A;
    TestEps = SolverEps * 1000.0f; // FLD; FMUL 1000.0f; FSTP float
    SubSolverStatus = 0;
    MaxStepLen = 10000.0f; // 0x461c4000
    LuMatrix.AlignedRowLen = AlignedSize;
    LuMatrix.NSub = 0;
    LuMatrix.MatrixEps = SolverEps * 10.0f;
    Residual = LuMatrix.TempVec;
    LuSubSolver.MatrixEps = GaussEps;
    FullAMatrix.DesiredVector = DeltaF;
    SubSolverMat.MatrixEps = SolverEps;
    FullAMatrix.MatrixEps = SolverEps;
    FullAMatrix.ResultVector = DeltaAccel;
    FullAMatrix.AlignedRowLen = AlignedSize;

    // Runs to AlignedSize, not N (06366a3d / 06366a8f): the caller's B / X
    // buffers are AlignedSize long.
    for (int I = 0; I < AlignedSize; ++I)
    {
        ActivesInactivesIgnored[I] = I;
        VariableIsFoundAt[I] = I;
        X[I] = 0.0f;
        Accel[I] = -B[I];
    }

    FullB = B;
    FullA = A;
    FullX = X;
    FirstPermuteIndex = 0;
    SecondPermuteIndex = 0;
    FirstPermuteInactive = 0;
    SecondPermuteInactive = 0;
    SubSolverCalls = 0;
    Field_0x50 = 0;

    StartWithActives(NumActives);
    int Result = FullSolveLc();

    FullAMatrix.ResultVector = nullptr;
    FullAMatrix.DesiredVector = nullptr;
    FullAMatrix.MatrixValues = nullptr;
    LuSubSolver.ResultVector = nullptr;
    LuSubSolver.DesiredVector = nullptr;
    LuSubSolver.MatrixValues = nullptr;
    SubSolverMat.ResultVector = nullptr;
    SubSolverMat.DesiredVector = nullptr;
    SubSolverMat.MatrixValues = nullptr;
    return Result;
}

// 06366b10-06366b3e (RET 4)
void IVP_Linear_Constraint_Solver::RemoveVariableFromLU(int Index)
{
    if (SubSolverStatus == 0)
    {
        if (LuMatrix.RemoveRowAndColumn(Index) != 1)
            SubSolverStatus = 2;
        return;
    }
    LuMatrix.NSub = LuMatrix.NSub - 1;
}

// 06366b50-06366bf1
void IVP_Linear_Constraint_Solver::AddVariableToLU()
{
    int NewVar = ActivesInactivesIgnored[RActives - 1];
    if (SubSolverStatus != 0)
    {
        LuMatrix.NSub = LuMatrix.NSub + 1;
        return;
    }
    for (int I = 0; I < RActives; ++I)
        LuMatrix.UMatrix[LuMatrix.AlignedRowLen * LuMatrix.NSub + I] = FullA[AlignedSize * NewVar + ActivesInactivesIgnored[I]];
    for (int I = 0; I < RActives - 1; ++I)
        LuMatrix.InputVec[I] = FullA[ActivesInactivesIgnored[I] * AlignedSize + NewVar];
    if (LuMatrix.AddNewRowAndColumn() != 1)
        SubSolverStatus = 2;
}

// 06366c00-06366ca7
void IVP_Linear_Constraint_Solver::DoALittleRandomPermutation()
{
    if (RActives < 2)
        return;
    FirstPermuteIndex = FirstPermuteIndex + 1;
    SecondPermuteIndex = SecondPermuteIndex + 2;
    while (FirstPermuteIndex >= RActives)
        FirstPermuteIndex = FirstPermuteIndex - RActives;
    while (SecondPermuteIndex >= RActives)
        SecondPermuteIndex = SecondPermuteIndex - RActives;
    ExchangeActivesInactives(FirstPermuteIndex, SecondPermuteIndex);

    int Ignored = IgnoredPos;
    SecondPermuteInactive = SecondPermuteInactive + 2;
    FirstPermuteInactive = FirstPermuteInactive + 1;
    int NumTail = NVariables - Ignored - 1;
    if (NumTail < 2)
        return;
    while (FirstPermuteInactive >= NumTail)
        FirstPermuteInactive = FirstPermuteInactive - NumTail;
    while (SecondPermuteInactive >= NumTail)
        SecondPermuteInactive = SecondPermuteInactive - NumTail;
    ExchangeActivesInactives(FirstPermuteInactive + Ignored + 1, SecondPermuteInactive + Ignored + 1);
}

// 06366cb0-06366d02
void IVP_Linear_Constraint_Solver::MoveNotNecessaryActivesToInactives()
{
    for (int I = 0; I < RActives; ++I)
    {
        float* X = &FullX[ActivesInactivesIgnored[I]];
        if (*X >= SolverEps) // TEST AH,1; JZ skip
            continue;
        *X = 0.0f;
        ExchangeActivesInactives(I, RActives - 1);
        RActives = RActives - 1;
        RemoveVariableFromLU(I);
        --I;
    }
}

// 06366d10-06366d16 (RET 4; ECX unused)
void IVP_Linear_Constraint_Solver::IncreaseStepCount(int* StepCount)
{
    *StepCount = *StepCount + 1;
}

// 06366d20-0636712e. Labels carry the retail LAB_ names.
int IVP_Linear_Constraint_Solver::FullSolveLc()
{
    float Eps = SolverEps;   // [ESP+0x14]
    int StepCount = 0;       // [ESP+0x1c]
    int SmallStepCount = 0;  // [ESP+0x20]
    int DidRealStep = 0;     // [ESP+0x24]
    int TestCountdown = 7;   // [ESP+0x18]
    float StepLen = 0.0f;    // [ESP+0x10]
    float NegEps = 0.0f;     // [ESP+0x28]
    int Limit = 0;           // EBP
    int Var = 0;             // EDI at 06366d9b

LoopTop: // LAB_06366d45
    if (DidRealStep != 0)
        IncreaseStepCount(&StepCount);
    if (StepCount > 0xfa)
        return 0;
    if (TestCountdown == 0)
    {
        if (SolutionIsCorrect() != 0)
            goto ResetCountdown;
        goto RestartSolve;
    }
    TestCountdown = TestCountdown - 1;

NextVariable: // LAB_06366d83
    if (!(IgnoredPos < NVariables))
        return 1;
    Var = ActivesInactivesIgnored[IgnoredPos];

    if (SubSolverStatus > 0)
    {
        if (SubSolverStatus == 1 && DidRealStep != 0)
        {
            DoALittleRandomPermutation();
            if (SetupLUFromActives() == 1)
                SubSolverStatus = 0;
        }
        else
        {
            SubSolverStatus = 1; // LAB_06366dc8
        }
    }

    if (!(std::fabs(Accel[Var]) >= Eps)) // TEST AH,1; JZ LAB_06366e04
    {
        Limit = IgnoredPos;
        if (!(std::fabs(FullX[Var]) >= Eps))
            goto MoveIgnoredToInactive; // LAB_063670dd
        goto MoveIgnoredToActive;       // LAB_06367083
    }
    if (Accel[Var] >= 0.0f) // LAB_06366e04: TEST AH,1; JZ LAB_063670dd
        goto MoveIgnoredToInactive;

    if (GetFDelta() == 0)
        goto RestartSolve;
    DeltaF[Var] = 1.0f;
    UpdateDeltaAccelOfInactives();
    for (int I = 0; I < RActives; ++I)
        DeltaAccel[ActivesInactivesIgnored[I]] = 0.0f;

    if (!((double)(MaxStepLen) * (double)(DeltaAccel[Var]) > -(double)(Accel[Var])))
    {
        Limit = -1; // LAB_06366e7f
        StepLen = 1e20f; // 0x60ad78ec
    }
    else
    {
        Limit = IgnoredPos;
        StepLen = (float)(-((double)(Accel[Var]) / (double)(DeltaAccel[Var])));
    }

    // LAB_06366e8a: actives that would reach zero force.
    {
        int NumActives = RActives; // ECX
        if (NumActives > 0)
        {
            NegEps = -Eps;
            for (int I = 0; I < NumActives; ++I)
            {
                int Active = ActivesInactivesIgnored[I];
                float Df = DeltaF[Active];
                if (Df >= NegEps) // FCOM; TEST AH,1; JZ LAB_06366ef5
                    continue;
                double Step = -((double)(FullX[Active]) / (double)(Df));
                if (!(std::fabs(Step) >= (double)(Eps)) && !(FullX[Active] >= Eps))
                {
                    StepLen = (float)(Step); // LAB_06366f44
                    Limit = I;
                    goto StepChosen;
                }
                if (!(Step >= (double)(StepLen) + (double)(Eps))) // LAB_06366eda
                {
                    StepLen = (float)(Step);
                    Limit = I;
                }
            }
        }

        // LAB_06366eff: inactives whose acceleration would reach zero.
        int Ignored = IgnoredPos; // EBX
        if (NumActives < Ignored)
        {
            double NegSolverEps = -(double)(SolverEps); // FLD [ESI]; FCHS, kept on the stack
            for (int I = NumActives; I < Ignored; ++I)
            {
                int Inactive = ActivesInactivesIgnored[I];
                float Da = DeltaAccel[Inactive];
                if ((double)(Da) >= NegSolverEps) // FCOM ST1; TEST AH,1; JZ LAB_06366f4c
                    continue;
                double Step = -((double)(Accel[Inactive]) / (double)(Da));
                if (!(Step >= (double)(StepLen) - (double)(Eps)))
                {
                    StepLen = (float)(Step);
                    Limit = I;
                }
            }
        }
    }

    // LAB_06366f58
    if (!(StepLen >= 0.0f))
        StepLen = 0.0f;
    if (Limit < 0)
        return 0;

StepChosen: // LAB_06366f79
    if (StepLen > MaxStepLen) // TEST AH,0x41; JZ LAB_06366fa9
        goto RestartSolve;
    if (StepLen >= SolverEps) // TEST AH,1; JZ LAB_06366fe5
    {
        SmallStepCount = 0;
    }
    else
    {
        SmallStepCount = SmallStepCount + 1;
        if (!(SmallStepCount <= (NVariables >> 1) + 2))
            goto RestartSolve;
    }

    // LAB_06366fed
    DidRealStep = 1;
    UpdateStepVectors(StepLen);
    if (Limit < RActives)
    {
        FullX[ActivesInactivesIgnored[Limit]] = 0.0f;
        RActives = RActives - 1;
        ExchangeActivesInactives(RActives, Limit);
        RemoveVariableFromLU(Limit);
        goto LoopTop;
    }
    if (Limit < IgnoredPos) // LAB_06367037
    {
        if (StepLen > SolverEps) // TEST AH,0x41; JNZ skip
            MoveNotNecessaryActivesToInactives();
        Accel[ActivesInactivesIgnored[Limit]] = 0.0f;
        ExchangeActivesInactives(RActives, Limit);
        RActives = RActives + 1;
        AddVariableToLU();
        goto LoopTop;
    }

MoveIgnoredToActive: // LAB_06367083
    MoveNotNecessaryActivesToInactives();
    Var = ActivesInactivesIgnored[Limit];
    Accel[Var] = 0.0f;
    if (!(FullX[Var] >= 0.0f))
        FullX[Var] = 0.0f;
    ExchangeActivesInactives(RActives, Limit);
    IgnoredPos = IgnoredPos + 1;
    RActives = RActives + 1;
    if (IgnoredPos >= NVariables)
        goto AllVariablesPlaced;
    AddVariableToLU();
    goto LoopTop;

MoveIgnoredToInactive: // LAB_063670dd
    Var = ActivesInactivesIgnored[IgnoredPos];
    FullX[Var] = 0.0f;
    DidRealStep = 0;
    if (!(Accel[Var] >= 0.0f))
        Accel[Var] = 0.0f;
    IgnoredPos = IgnoredPos + 1;
    if (IgnoredPos < NVariables)
        goto LoopTop;

AllVariablesPlaced: // LAB_0636711c
    TestCountdown = 0;
    goto LoopTop;

RestartSolve: // LAB_06366fab
    IncreaseStepCount(&StepCount);
    if (StepCount > 0xfa)
        return 0;
    SmallStepCount = 0;
    if (Restart() == 0)
        return 0;

ResetCountdown: // LAB_06366fd8
    TestCountdown = 7;
    goto NextVariable;
}

// 06367130-0636715c (RET 8)
void IVP_Linear_Constraint_Solver::ExchangeActivesInactives(int IndexA, int IndexB)
{
    int VarB = ActivesInactivesIgnored[IndexB];
    int VarA = ActivesInactivesIgnored[IndexA];
    ActivesInactivesIgnored[IndexA] = VarB;
    ActivesInactivesIgnored[IndexB] = VarA;
    VariableIsFoundAt[VarB] = IndexA;
    VariableIsFoundAt[VarA] = IndexB;
}

// 06367160-06367200 (RET 4)
void IVP_Linear_Constraint_Solver::UpdateStepVectors(float StepLen)
{
    for (int I = 0; I < NVariables; ++I)
    {
        Accel[I] = (float)((double)(StepLen) * (double)(DeltaAccel[I]) + (double)(Accel[I]));
        FullX[I] = (float)((double)(StepLen) * (double)(DeltaF[I]) + (double)(FullX[I]));
    }
    for (int I = RActives; I < IgnoredPos; ++I)
    {
        float* A = &Accel[ActivesInactivesIgnored[I]];
        if (!(*A >= 0.0f))
            *A = 0.0f;
    }
    for (int I = 0; I < RActives; ++I)
    {
        float* X = &FullX[ActivesInactivesIgnored[I]];
        if (!(*X >= 0.0f))
            *X = 0.0f;
    }
}

// 06367210-0636739f
int IVP_Linear_Constraint_Solver::GetFDelta()
{
    for (int I = 0; I < AlignedSize; ++I)
        DeltaF[I] = 0.0f;
    if (RActives == 0)
        return 1;

    int Result;
    if (SubSolverStatus == 0)
    {
        int Col = ActivesInactivesIgnored[IgnoredPos];
        for (int I = 0; I < RActives; ++I)
            LuMatrix.InputVec[I] = -FullA[ActivesInactivesIgnored[I] * AlignedSize + Col];
        LuMatrix.SolveLU();
        Result = 1;
    }
    else
    {
        LuSubSolver.Columns = RActives;
        SubSolverCalls = SubSolverCalls + 1;
        LuSubSolver.AlignedRowLen = (RActives + 3) & ~3;
        int Col = ActivesInactivesIgnored[IgnoredPos];
        for (int I = 0; I < RActives; ++I)
        {
            int RowVar = ActivesInactivesIgnored[I];
            LuSubSolver.DesiredVector[I] = -FullA[RowVar * AlignedSize + Col];
            for (int J = 0; J < RActives; ++J)
                LuSubSolver.MatrixValues[LuSubSolver.AlignedRowLen * I + J] = FullA[ActivesInactivesIgnored[J] + RowVar * AlignedSize];
        }
        Result = LuSubSolver.SolveGreatMatrixManyZero();
        for (int I = 0; I < RActives; ++I)
            LuMatrix.OutVec[I] = LuSubSolver.ResultVector[I];
    }

    for (int I = 0; I < RActives; ++I)
        DeltaF[ActivesInactivesIgnored[I]] = LuMatrix.OutVec[I];
    DeltaF[ActivesInactivesIgnored[IgnoredPos]] = 1.0f;
    return Result;
}

// 063673a0-06367407 (tail JMP into FactorFull)
int IVP_Linear_Constraint_Solver::SetupLUFromActives()
{
    for (int I = 0; I < RActives; ++I)
    {
        int Var = ActivesInactivesIgnored[I];
        LuMatrix.InputVec[I] = FullB[Var];
        float const* Row = FullA + AlignedSize * Var;
        float* URow = LuMatrix.UMatrix + I * LuMatrix.AlignedRowLen;
        for (int J = 0; J < RActives; ++J)
            URow[J] = Row[ActivesInactivesIgnored[J]];
    }
    return LuMatrix.FactorFull();
}

// 06367410-0636747f: insertion sort of the actives, largest X first.
void IVP_Linear_Constraint_Solver::SortActivesByX()
{
    for (int I = 0; I + 1 < RActives; ++I)
    {
        if (FullX[ActivesInactivesIgnored[I]] >= FullX[ActivesInactivesIgnored[I + 1]]) // TEST AH,1; JZ next
            continue;
        ExchangeActivesInactives(I, I + 1);
        for (int K = I; K > 0; --K)
        {
            if (FullX[ActivesInactivesIgnored[K - 1]] >= FullX[ActivesInactivesIgnored[K]])
                break;
            ExchangeActivesInactives(K, K - 1);
        }
    }
}

// 06367480-0636751b
void IVP_Linear_Constraint_Solver::StartFromResetX()
{
    FullAMatrix.DesiredVector = ResetX;
    FullAMatrix.ResultVector = ResetAccel;
    FullAMatrix.MultAlignedMatrix();
    FullAMatrix.DesiredVector = DeltaF;
    FullAMatrix.ResultVector = DeltaAccel;

    for (int I = 0; I < NVariables; ++I)
        ResetAccel[I] = ResetAccel[I] - FullB[I]; // FLD; FSUB float; FSTP float
    for (int I = 0; I < RActives; ++I)
        ResetAccel[ActivesInactivesIgnored[I]] = 0.0f;
    for (int I = NVariables - 1; I >= 0; --I)
    {
        Accel[I] = ResetAccel[I];
        FullX[I] = ResetX[I];
    }
}

// 06367520-06367653 (RET 4)
void IVP_Linear_Constraint_Solver::StartWithActives(int NumActives)
{
    AlignedSubSize = (NumActives + 3) & ~3;
    RActives = NumActives;
    IgnoredPos = NumActives;
    LuMatrix.NSub = NumActives;
    for (int I = 0; I < AlignedSize; ++I)
        ResetX[I] = 0.0f;

    if (SetupLUFromActives() == 1)
    {
        SubSolverStatus = 0;
        for (;;)
        {
            LuMatrix.SolveLU(); // LAB_0636756e
            for (int I = 0; I < RActives; ++I)
                ResetX[ActivesInactivesIgnored[I]] = LuMatrix.OutVec[I];
            StartFromResetX();

            int Index = RActives;
            int Var = 0;
            for (;;) // LAB_063675a5
            {
                --Index;
                if (Index < 0)
                    return;
                Var = ActivesInactivesIgnored[Index];
                if (!(FullX[Var] >= 0.0f))
                    break;
            }
            FullX[Var] = 0.0f;
            ResetX[Var] = 0.0f;
            RActives = RActives - 1;
            IgnoredPos = IgnoredPos - 1;
            ExchangeActivesInactives(RActives, Index);
            RemoveVariableFromLU(Index);
            if (SubSolverStatus > 0)
            {
                for (int I = 0; I < NVariables; ++I) // LAB_06367626
                {
                    Accel[I] = 0.0f;
                    FullX[I] = 0.0f;
                }
                break;
            }
            for (int I = 0; I < RActives; ++I)
                LuMatrix.InputVec[I] = FullB[ActivesInactivesIgnored[I]];
        }
    }
    RActives = 0; // LAB_06367643
    IgnoredPos = 0;
    LuMatrix.NSub = 0;
}

// 06367660-063677c8
int IVP_Linear_Constraint_Solver::Restart()
{
    for (;;)
    {
        LuMatrix.NSub = RActives; // LAB_06367667
        for (int I = 0; I < AlignedSize; ++I)
            ResetX[I] = 0.0f;
        DoALittleRandomPermutation();
        if (SetupLUFromActives() == 1)
        {
            SubSolverStatus = 0;
            LuMatrix.SolveLU();
            for (int I = 0; I < RActives; ++I)
                ResetX[ActivesInactivesIgnored[I]] = LuMatrix.OutVec[I];
        }
        else
        {
            SubSolverStatus = 2; // LAB_063676dc
            SortActivesByX();
            DoALittleRandomPermutation();
            LuSubSolver.Columns = RActives;
            LuSubSolver.AlignedRowLen = (RActives + 3) & ~3;
            for (int I = 0; I < RActives; ++I)
            {
                int RowVar = ActivesInactivesIgnored[I];
                LuSubSolver.DesiredVector[I] = FullB[RowVar];
                for (int J = 0; J < RActives; ++J)
                    LuSubSolver.MatrixValues[LuSubSolver.AlignedRowLen * I + J] = FullA[AlignedSize * RowVar + ActivesInactivesIgnored[J]];
            }
            int Result = LuSubSolver.SolveGreatMatrixManyZero();
            if (Result != 1)
                return Result;
            for (int I = 0; I < RActives; ++I)
                ResetX[ActivesInactivesIgnored[I]] = LuSubSolver.ResultVector[I];
        }
        StartFromResetX(); // LAB_063677a8
        if (!(RemoveInvalidActives() > 0))
            return 1;
    }
}

// 063677d0-063677f9 (RET 4)
void IVP_Linear_Constraint_Solver::MoveVariableToEnd(int Index)
{
    for (int Next = Index + 1, Prev = Index; Next < NVariables; ++Next, ++Prev)
        ExchangeActivesInactives(Prev, Next);
}

// 06367800-063678bc
int IVP_Linear_Constraint_Solver::RemoveInvalidActives()
{
    int Removed = 0;
    for (int I = 0; I < RActives; ++I)
    {
        float* X = &FullX[ActivesInactivesIgnored[I]];
        if (*X >= SolverEps) // TEST AH,1; JZ LAB_06367876
            continue;
        if (-SolverEps >= *X) // FLD eps; FCHS; FCOMP [x]; TEST AH,1; JZ LAB_0636784a
        {
            MoveVariableToEnd(I);
            Removed = Removed + 1;
            IgnoredPos = IgnoredPos - 1;
        }
        else
        {
            *X = 0.0f;
            ExchangeActivesInactives(I, RActives - 1);
        }
        RActives = RActives - 1; // LAB_0636785a
        LuMatrix.NSub = LuMatrix.NSub - 1;
        --I;
        SubSolverStatus = 1;
    }
    for (int I = RActives; I < IgnoredPos; ++I)
    {
        if (!(Accel[ActivesInactivesIgnored[I]] >= 0.0f))
        {
            MoveVariableToEnd(I);
            --I;
            IgnoredPos = IgnoredPos - 1;
        }
    }
    return Removed;
}
