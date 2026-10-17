// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstddef>

#include "../ivp_utility/ivp_u_memory.h"

// ============================================================================
// IVP linear-constraint solver (Common.dll .text:06366070-063682f2).
//
// Three classes, identified by the field offsets every function reads and by
// the one place that builds them all, the friction solver FUN_06368560:
//   - IVP_Great_Matrix_Many_Zero (0x18 bytes): a dense row-aligned matrix with
//     a right-hand side and a result vector; Gauss elimination with partial
//     pivoting (06366070-06366584). FUN_06368560 builds one on its stack with
//     the ctor 06366400.
//   - IVP_Incr_L_U_Matrix (0x2c bytes): an incrementally maintained L/U pair
//     (063678c0-063682f2), embedded in the solver at +0x6c (every caller does
//     `LEA ECX,[ESI+0x6c]`).
//   - IVP_Linear_Constraint_Solver (0xf8 bytes): the LCP solver
//     (06366590-063678bc). FUN_06368560 keeps it on its stack at ESP+0x30 and
//     runs the IVP_Great_Matrix_Many_Zero ctor on +0x98, +0xb0, +0xc8, +0xe0
//     (0636865f-0636868a) -- the solver's own ctor is inlined and does nothing
//     else.
//
// Retail is VC6/x87. Every value the listing keeps on the FPU stack is held in
// `double` here; every `FSTP float` is a float store. All buffers come from an
// IVP_U_Memory arena (32-byte aligned); several loops start at the 16-byte
// aligned-down address of a row and process whole blocks of four floats, which
// is reproduced exactly (they can touch up to three elements before/after the
// logical range, as retail does).
//
// Class / method names follow the Ipion SDK naming where the role is certain;
// methods are PascalCase per AGENTS.md.
// ============================================================================

class IVP_Great_Matrix_Many_Zero
{
public:
    float  MatrixEps;      // +0x00 (ctor: 1e-9, 0x3089705f)
    int    Columns;        // +0x04
    int    AlignedRowLen;  // +0x08 row stride in floats (multiple of 4)
    float* MatrixValues;   // +0x0C Columns rows of AlignedRowLen floats
    float* DesiredVector;  // +0x10 right-hand side (overwritten by the solve)
    float* ResultVector;   // +0x14

    // 06366400
    IVP_Great_Matrix_Many_Zero();

    // 06366070: 1 if Desired[Row] <= |Desired[Row] * 1e-5| + Row . Result.
    int TestResultRow(int Row) const;
    // 063660d0: row Second += Factor * row First (from the aligned block of
    // element [First][First]); Desired[Second] += Factor * Desired[First].
    void AddMultipleLine(int First, int Second, float Factor);
    // 06366180
    int SolveGreatMatrixManyZero();
    // 063661a0
    void AlignMatrixValues();
    // 063661b0
    void FindPivotInColumn(int Col);
    // 06366220
    void ExchangeRows(int RowA, int RowB);
    // 063662a0
    void TransformToLowerNullTriangle();
    // 06366340
    int SolveLowerNullMatrix();
    // 06366420: copy the Indices x Indices sub-matrix (and right-hand side)
    // out of Big.
    void FillFromBiggerMatrix(IVP_Great_Matrix_Many_Zero const* Big, int const* Indices, int Count);
    // 06366540: Result = Matrix * Desired.
    void MultAlignedMatrix();
};

static_assert(sizeof(IVP_Great_Matrix_Many_Zero) == 0x18, "IVP_Great_Matrix_Many_Zero is 0x18 bytes");
static_assert(offsetof(IVP_Great_Matrix_Many_Zero, MatrixValues) == 0x0C, "IVP_Great_Matrix_Many_Zero+0x0C");
static_assert(offsetof(IVP_Great_Matrix_Many_Zero, ResultVector) == 0x14, "IVP_Great_Matrix_Many_Zero+0x14");

class IVP_Incr_L_U_Matrix
{
public:
    float  MatrixEps;      // +0x00 (solver init: SolverEps * 10)
    float* LMatrix;        // +0x04 (solver +0x70)
    float* UMatrix;        // +0x08 (solver +0x74)
    int    Field_0x0C;     // +0x0C TODO: not read or written by any function of this cluster
    int    Field_0x10;     // +0x10 TODO: not read or written by any function of this cluster
    float* InputVec;       // +0x14 (solver +0x80)
    float* OutVec;         // +0x18 (solver +0x84)
    float* TempVec;        // +0x1C (solver +0x88)
    float* MultVec;        // +0x20 (solver +0x8c)
    int    AlignedRowLen;  // +0x24 (solver +0x90)
    int    NSub;           // +0x28 (solver +0x94)

    // 063678c0 / 06367960
    int NormalizeRowL(int Row);
    int NormalizeRowU(int Row);
    // 06367a60
    void ExchangeRowsLU(int RowA, int RowB);
    // 06367b40
    void PivotInColumnU(int Col);
    // 06367bb0: row Dst -= Factor * row Src in U (right of Src) and L; U[Dst][Src] = 0.
    void SubtractRowLU(int Src, int Dst, float Factor);
    // 06367d00: row Dst -= Factor * row Src in L; L[Dst][Src] = 0.
    void SubtractRowLZero(int Src, int Dst, float Factor);
    // 06367db0
    int FactorFull();
    // 06367e90
    int AddNewRowAndColumn();
    // 06367f80
    int RemoveRowAndColumn(int Index);
    // 063680e0 / 06368130
    void ExchangeColumnsL(int ColA, int ColB);
    void ExchangeColumnsU(int ColA, int ColB);
    // 06368180: Temp = L * Mult.
    void MultVecWithL();
    // 063681d0: back substitution through U, Out = Temp.
    void SolveUpperTriangle();
    // 06368230: Out = U^-1 L Input.
    void SolveLU();
    // 06368260: row Dst -= Factor * row Src in L (no zeroing).
    void SubtractRowL(int Src, int Dst, float Factor);
};

static_assert(sizeof(IVP_Incr_L_U_Matrix) == 0x2C, "IVP_Incr_L_U_Matrix is 0x2c bytes");
static_assert(offsetof(IVP_Incr_L_U_Matrix, InputVec) == 0x14, "IVP_Incr_L_U_Matrix+0x14");
static_assert(offsetof(IVP_Incr_L_U_Matrix, NSub) == 0x28, "IVP_Incr_L_U_Matrix+0x28");

class IVP_Linear_Constraint_Solver
{
public:
    float  SolverEps;             // +0x00 1e-7
    float  GaussEps;              // +0x04 1e-7
    float  TestEps;               // +0x08 SolverEps * 1000
    float  MaxStepLen;            // +0x0C 10000
    float* FullA;                 // +0x10 NVariables rows, AlignedSize stride
    float* FullB;                 // +0x14
    float* Residual;              // +0x18 = LuMatrix.TempVec
    float* FullX;                 // +0x1C
    float* DeltaF;                // +0x20
    float* Accel;                 // +0x24
    float* DeltaAccel;            // +0x28
    float* ResetX;                // +0x2C
    float* ResetAccel;            // +0x30
    int*   ActivesInactivesIgnored; // +0x34 permutation: [0,RActives) actives, [RActives,IgnoredPos) inactives
    int*   VariableIsFoundAt;     // +0x38 inverse permutation
    int    NVariables;            // +0x3C
    int    AlignedSize;           // +0x40
    int    RActives;              // +0x44
    int    AlignedSubSize;        // +0x48
    int    IgnoredPos;            // +0x4C
    int    Field_0x50;            // +0x50 TODO: zeroed by InitAndSolveLc, never read in this cluster
    int    SubSolverCalls;        // +0x54 incremented each time the Gauss sub-solver is used
    int    FirstPermuteIndex;     // +0x58
    int    SecondPermuteIndex;    // +0x5C
    int    FirstPermuteInactive;  // +0x60
    int    SecondPermuteInactive; // +0x64
    int    SubSolverStatus;       // +0x68 0 = LU valid, 1 = refactor pending, 2 = LU failed
    IVP_Incr_L_U_Matrix        LuMatrix;     // +0x6C
    IVP_Great_Matrix_Many_Zero LuSubSolver;  // +0x98
    IVP_Great_Matrix_Many_Zero FullAMatrix;  // +0xB0
    IVP_Great_Matrix_Many_Zero Field_0xC8;   // +0xC8 TODO: constructed (06368677) but unused by this cluster
    IVP_Great_Matrix_Many_Zero SubSolverMat; // +0xE0 eps/buffers set by InitAndSolveLc, no reader in this cluster

    // 063669b0
    int InitAndSolveLc(float* A, float* B, float* X, int N, int NumActives, IVP_U_Memory* Mem);

    // 06366590
    void UpdateDeltaAccelOfInactives();
    // 06366610: Residual = A * X - B.
    void ComputeResidual();
    // 06366670
    int SolutionIsCorrect();
    // 063666f0
    void AllocateMemory(IVP_U_Memory* Mem);
    // 06366b10 / 06366b50
    void RemoveVariableFromLU(int Index);
    void AddVariableToLU();
    // 06366c00
    void DoALittleRandomPermutation();
    // 06366cb0
    void MoveNotNecessaryActivesToInactives();
    // 06366d10 (thiscall; `this` unused)
    void IncreaseStepCount(int* StepCount);
    // 06366d20
    int FullSolveLc();
    // 06367130
    void ExchangeActivesInactives(int IndexA, int IndexB);
    // 06367160
    void UpdateStepVectors(float StepLen);
    // 06367210
    int GetFDelta();
    // 063673a0
    int SetupLUFromActives();
    // 06367410
    void SortActivesByX();
    // 06367480
    void StartFromResetX();
    // 06367520
    void StartWithActives(int NumActives);
    // 06367660
    int Restart();
    // 063677d0
    void MoveVariableToEnd(int Index);
    // 06367800
    int RemoveInvalidActives();
};

static_assert(offsetof(IVP_Linear_Constraint_Solver, ActivesInactivesIgnored) == 0x34, "LCS+0x34");
static_assert(offsetof(IVP_Linear_Constraint_Solver, SubSolverStatus) == 0x68, "LCS+0x68");
static_assert(offsetof(IVP_Linear_Constraint_Solver, LuMatrix) == 0x6C, "LCS+0x6C");
static_assert(offsetof(IVP_Linear_Constraint_Solver, LuSubSolver) == 0x98, "LCS+0x98");
static_assert(offsetof(IVP_Linear_Constraint_Solver, FullAMatrix) == 0xB0, "LCS+0xB0");
static_assert(offsetof(IVP_Linear_Constraint_Solver, SubSolverMat) == 0xE0, "LCS+0xE0");
static_assert(sizeof(IVP_Linear_Constraint_Solver) == 0xF8, "IVP_Linear_Constraint_Solver is 0xf8 bytes");
