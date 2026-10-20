// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "x86math.h"
#include "BatchTransform.h"
#include "ScalarMath.h"
#include "QuaternionMath.h"

// DACOM-wide result codes (see the note in x86math.h); kept local until dacom.h owns them.

#include <cmath>
#include <cstring>
#include <new>

int CallDacomStricmp(const char* Left, const char* Right);

namespace
{
    const char G_ComponentName[] = "3DMathEngine";
    const char* G_ImplementationId = "x86";
    const float G_MinDet = 1e-8f;
    const unsigned int G_RegisterPriority = 0x40000000u;
    // LAB_06f71170: CMPSB.REPE ECX=0xD — 12 chars + NUL, not CRT strcmp.
    const unsigned int G_ComponentNameBytes = 13;
    static_assert(sizeof(G_ComponentName) == G_ComponentNameBytes, "CMPSB ECX=0xD");

    // LAB_06f71830: Rotate/TransformMatrix literal shared x87 body.
    // Stores precede the next component source reads, including aliased calls.
    void MulVecByMatrix(FVector& Dst, const FMatrix& Matrix, const FVector& Source)
    {
        float* ResultVector = &Dst.X;
        const float* MatrixValues = &Matrix.D[0][0];
        const float* SourceVector = &Source.X;
        __asm
        {
            mov ecx, MatrixValues
            mov eax, SourceVector
            mov edx, ResultVector
            fld dword ptr [ecx + 8]
            fmul dword ptr [eax + 8]
            fld dword ptr [ecx + 4]
            fmul dword ptr [eax + 4]
            faddp st(1), st(0)
            fld dword ptr [ecx]
            fmul dword ptr [eax]
            faddp st(1), st(0)
            fstp dword ptr [edx]
            fld dword ptr [ecx + 014h]
            fmul dword ptr [eax + 8]
            fld dword ptr [ecx + 0ch]
            fmul dword ptr [eax]
            faddp st(1), st(0)
            fld dword ptr [ecx + 010h]
            fmul dword ptr [eax + 4]
            faddp st(1), st(0)
            fstp dword ptr [edx + 4]
            fld dword ptr [ecx + 020h]
            fmul dword ptr [eax + 8]
            fld dword ptr [ecx + 018h]
            fmul dword ptr [eax]
            faddp st(1), st(0)
            fld dword ptr [ecx + 01ch]
            fmul dword ptr [eax + 4]
            xor eax, eax
            faddp st(1), st(0)
            fstp dword ptr [edx + 8]
        }
    }

    // LAB_06f718f0: shared InverseRotate/TransposeTransform body. Each
    // output float is stored before the next component reads the source.
    void MulVecByTranspose(FVector& Dst, const FMatrix& Matrix, const FVector& Source)
    {
        float* ResultVector = &Dst.X;
        const float* MatrixValues = &Matrix.D[0][0];
        const float* SourceVector = &Source.X;
        __asm
        {
            mov ecx, MatrixValues
            mov eax, SourceVector
            mov edx, ResultVector
            fld dword ptr [ecx + 018h]
            fmul dword ptr [eax + 8]
            fld dword ptr [ecx + 0ch]
            fmul dword ptr [eax + 4]
            faddp st(1), st(0)
            fld dword ptr [ecx]
            fmul dword ptr [eax]
            faddp st(1), st(0)
            fstp dword ptr [edx]
            fld dword ptr [ecx + 01ch]
            fmul dword ptr [eax + 8]
            fld dword ptr [ecx + 4]
            fmul dword ptr [eax]
            faddp st(1), st(0)
            fld dword ptr [ecx + 010h]
            fmul dword ptr [eax + 4]
            faddp st(1), st(0)
            fstp dword ptr [edx + 4]
            fld dword ptr [ecx + 020h]
            fmul dword ptr [eax + 8]
            fld dword ptr [ecx + 8]
            fmul dword ptr [eax]
            faddp st(1), st(0)
            fld dword ptr [ecx + 014h]
            fmul dword ptr [eax + 4]
            xor eax, eax
            faddp st(1), st(0)
            fstp dword ptr [edx + 8]
        }
    }

    void TransposeMatrix(FMatrix& Dst, const FMatrix& Source)
    {
        Dst.D[0][0] = Source.D[0][0];
        Dst.D[0][1] = Source.D[1][0];
        Dst.D[0][2] = Source.D[2][0];
        Dst.D[1][0] = Source.D[0][1];
        Dst.D[1][1] = Source.D[1][1];
        Dst.D[1][2] = Source.D[2][1];
        Dst.D[2][0] = Source.D[0][2];
        Dst.D[2][1] = Source.D[1][2];
        Dst.D[2][2] = Source.D[2][2];
    }

    void WriteAdjugate(FMatrix& Dst, const FMatrix& Source, float Scale)
    {
        Dst.D[0][0] = (Source.D[1][1] * Source.D[2][2] - Source.D[1][2] * Source.D[2][1]) * Scale;
        Dst.D[1][0] = -(Source.D[1][0] * Source.D[2][2] - Source.D[1][2] * Source.D[2][0]) * Scale;
        Dst.D[2][0] = (Source.D[1][0] * Source.D[2][1] - Source.D[1][1] * Source.D[2][0]) * Scale;
        Dst.D[0][1] = -(Source.D[0][1] * Source.D[2][2] - Source.D[0][2] * Source.D[2][1]) * Scale;
        Dst.D[1][1] = (Source.D[0][0] * Source.D[2][2] - Source.D[0][2] * Source.D[2][0]) * Scale;
        Dst.D[2][1] = -(Source.D[0][0] * Source.D[2][1] - Source.D[0][1] * Source.D[2][0]) * Scale;
        Dst.D[0][2] = (Source.D[0][1] * Source.D[1][2] - Source.D[0][2] * Source.D[1][1]) * Scale;
        Dst.D[1][2] = -(Source.D[0][0] * Source.D[1][2] - Source.D[0][2] * Source.D[1][0]) * Scale;
        Dst.D[2][2] = (Source.D[0][0] * Source.D[1][1] - Source.D[0][1] * Source.D[1][0]) * Scale;
    }


}

struct FMathEngine final : I3DMathEngine, IComponentFactory
{
    GENRESULT DACOM_API QueryInterface(const char* /*Iid*/, void** Out) override
    {
        if (Out != nullptr)
            *Out = nullptr;
        return GR_GENERIC;
    }

    unsigned long DACOM_API AddRef() override { return 1; }
    unsigned long DACOM_API Release() override { return 1; }

    GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override
    {
        if (Instance == nullptr)
            return GR_INVALID_PARMS;
        *Instance = nullptr;

        FMathDesc* Info = reinterpret_cast<FMathDesc*>(Desc);
        if (Info == nullptr || Info->InterfaceName == nullptr)
            return GR_INVALID_PARMS;

        if (Info->Size != sizeof(FMathDesc) ||
            std::memcmp(Info->InterfaceName, G_ComponentName, G_ComponentNameBytes) != 0)
            return GR_INTERFACE_UNSUPPORTED;

        if (Info->Implementation != nullptr && CallDacomStricmp(Info->Implementation, G_ImplementationId) != 0)
            return GR_INTERFACE_UNSUPPORTED;

        AddRef();
        *Instance = static_cast<I3DMathEngine*>(this);
        return GR_OK;
    }

    GENRESULT DACOM_API Inverse(FMatrix& Dst, const FMatrix& Source) override
    {
        const float Determinant = Det(Source);
        if (std::fabs(Determinant) > G_MinDet)
        {
            WriteAdjugate(Dst, Source, 1.0f / Determinant);
            return GR_OK;
        }
        WriteAdjugate(Dst, Source, 1.0f);
        return GR_INVALID_PARMS;
    }

    GENRESULT DACOM_API Scale(FMatrix& Dst, const FMatrix& Source, float Scale) override
    {
        for (int Row = 0; Row < 3; ++Row)
        {
            for (int Col = 0; Col < 3; ++Col)
                Dst.D[Row][Col] = Source.D[Row][Col] * Scale;
        }
        return GR_OK;
    }

    float DACOM_API Det(const FMatrix& Source) override
    {
        return Source.D[0][0] * Source.D[1][1] * Source.D[2][2] +
               Source.D[0][1] * Source.D[1][2] * Source.D[2][0] +
               Source.D[0][2] * Source.D[1][0] * Source.D[2][1] -
               Source.D[0][0] * Source.D[1][2] * Source.D[2][1] -
               Source.D[0][1] * Source.D[1][0] * Source.D[2][2] -
               Source.D[0][2] * Source.D[1][1] * Source.D[2][0];
    }

    GENRESULT DACOM_API MulQuat(FQuaternion& Dst, const FQuaternion& A, const FQuaternion& B) override
    {
        Dst.W = A.W * B.W - (A.X * B.X + A.Y * B.Y + A.Z * B.Z);
        Dst.X = A.W * B.X + B.W * A.X + (A.Y * B.Z - A.Z * B.Y);
        Dst.Y = A.W * B.Y + B.W * A.Y + (A.Z * B.X - A.X * B.Z);
        Dst.Z = A.W * B.Z + B.W * A.Z + (A.X * B.Y - A.Y * B.X);
        return GR_OK;
    }

    GENRESULT DACOM_API MulTransform(FTransform& Dst, const FTransform& A, const FTransform& B) override
    {
        // LAB_06f716f0: literal row/translation interleave and x87 sum order.
        // Input words are read after each output store, including aliased calls.
        float* ResultTransform = &Dst.Rotation.D[0][0];
        const float* LeftTransform = &A.Rotation.D[0][0];
        const float* RightTransform = &B.Rotation.D[0][0];
        __asm
        {
            mov eax, LeftTransform
            mov ecx, RightTransform
            mov edx, ResultTransform
            fld dword ptr [eax]
            fmul dword ptr [ecx]
            fld dword ptr [eax + 4]
            fmul dword ptr [ecx + 0ch]
            faddp st(1), st(0)
            fld dword ptr [ecx + 018h]
            fmul dword ptr [eax + 8]
            faddp st(1), st(0)
            fstp dword ptr [edx]
            fld dword ptr [eax + 4]
            fmul dword ptr [ecx + 010h]
            fld dword ptr [ecx + 01ch]
            fmul dword ptr [eax + 8]
            faddp st(1), st(0)
            fld dword ptr [ecx + 4]
            fmul dword ptr [eax]
            faddp st(1), st(0)
            fstp dword ptr [edx + 4]
            fld dword ptr [eax + 4]
            fmul dword ptr [ecx + 014h]
            fld dword ptr [ecx + 020h]
            fmul dword ptr [eax + 8]
            faddp st(1), st(0)
            fld dword ptr [ecx + 8]
            fmul dword ptr [eax]
            faddp st(1), st(0)
            fstp dword ptr [edx + 8]
            fld dword ptr [eax + 4]
            fmul dword ptr [ecx + 028h]
            fld dword ptr [ecx + 02ch]
            fmul dword ptr [eax + 8]
            faddp st(1), st(0)
            fld dword ptr [ecx + 024h]
            fmul dword ptr [eax]
            faddp st(1), st(0)
            fadd dword ptr [eax + 024h]
            fstp dword ptr [edx + 024h]
            fld dword ptr [eax + 010h]
            fmul dword ptr [ecx + 0ch]
            fld dword ptr [eax + 0ch]
            fmul dword ptr [ecx]
            faddp st(1), st(0)
            fld dword ptr [eax + 014h]
            fmul dword ptr [ecx + 018h]
            faddp st(1), st(0)
            fstp dword ptr [edx + 0ch]
            fld dword ptr [eax + 014h]
            fmul dword ptr [ecx + 01ch]
            fld dword ptr [eax + 010h]
            fmul dword ptr [ecx + 010h]
            faddp st(1), st(0)
            fld dword ptr [eax + 0ch]
            fmul dword ptr [ecx + 4]
            faddp st(1), st(0)
            fstp dword ptr [edx + 010h]
            fld dword ptr [eax + 014h]
            fmul dword ptr [ecx + 020h]
            fld dword ptr [eax + 010h]
            fmul dword ptr [ecx + 014h]
            faddp st(1), st(0)
            fld dword ptr [eax + 0ch]
            fmul dword ptr [ecx + 8]
            faddp st(1), st(0)
            fstp dword ptr [edx + 014h]
            fld dword ptr [ecx + 02ch]
            fmul dword ptr [eax + 014h]
            fld dword ptr [ecx + 028h]
            fmul dword ptr [eax + 010h]
            faddp st(1), st(0)
            fld dword ptr [ecx + 024h]
            fmul dword ptr [eax + 0ch]
            faddp st(1), st(0)
            fadd dword ptr [eax + 028h]
            fstp dword ptr [edx + 028h]
            fld dword ptr [eax + 01ch]
            fmul dword ptr [ecx + 0ch]
            fld dword ptr [eax + 020h]
            fmul dword ptr [ecx + 018h]
            faddp st(1), st(0)
            fld dword ptr [ecx]
            fmul dword ptr [eax + 018h]
            faddp st(1), st(0)
            fstp dword ptr [edx + 018h]
            fld dword ptr [eax + 020h]
            fmul dword ptr [ecx + 01ch]
            fld dword ptr [eax + 01ch]
            fmul dword ptr [ecx + 010h]
            faddp st(1), st(0)
            fld dword ptr [ecx + 4]
            fmul dword ptr [eax + 018h]
            faddp st(1), st(0)
            fstp dword ptr [edx + 01ch]
            fld dword ptr [eax + 020h]
            fmul dword ptr [ecx + 020h]
            fld dword ptr [eax + 01ch]
            fmul dword ptr [ecx + 014h]
            faddp st(1), st(0)
            fld dword ptr [ecx + 8]
            fmul dword ptr [eax + 018h]
            faddp st(1), st(0)
            fstp dword ptr [edx + 020h]
            fld dword ptr [eax + 020h]
            fmul dword ptr [ecx + 02ch]
            fld dword ptr [eax + 01ch]
            fmul dword ptr [ecx + 028h]
            faddp st(1), st(0)
            fld dword ptr [ecx + 024h]
            fmul dword ptr [eax + 018h]
            faddp st(1), st(0)
            fadd dword ptr [eax + 02ch]
            xor eax, eax
            fstp dword ptr [edx + 02ch]
        }
        return GR_OK;
    }

    GENRESULT DACOM_API MulMatrix(FMatrix& Dst, const FMatrix& A, const FMatrix& B) override
    {
        // 71670..716e1: literal x87 loop, preserving product/add/store order
        // and immediate alias visibility between individual result writes.
        float* ResultMatrix = &Dst.D[0][0];
        const float* LeftMatrix = &A.D[0][0];
        const float* RightMatrix = &B.D[0][0];
        __asm
        {
            mov edx, ResultMatrix
            mov ecx, RightMatrix
            mov edi, LeftMatrix
            mov esi, edx
            sub esi, edi
            lea eax, [edi + 4]
            mov edi, 3
        MatrixRowLoop:
            fld dword ptr [eax + 4]
            fmul dword ptr [ecx + 18h]
            fld dword ptr [ecx + 0ch]
            fmul dword ptr [eax]
            add eax, 0ch
            add edx, 0ch
            dec edi
            faddp st(1), st(0)
            fld dword ptr [eax - 10h]
            fmul dword ptr [ecx]
            faddp st(1), st(0)
            fstp dword ptr [edx - 0ch]
            fld dword ptr [ecx + 4]
            fmul dword ptr [eax - 10h]
            fld dword ptr [eax - 8]
            fmul dword ptr [ecx + 1ch]
            faddp st(1), st(0)
            fld dword ptr [ecx + 10h]
            fmul dword ptr [eax - 0ch]
            faddp st(1), st(0)
            fstp dword ptr [esi + eax - 0ch]
            fld dword ptr [ecx + 8]
            fmul dword ptr [eax - 10h]
            fld dword ptr [eax - 8]
            fmul dword ptr [ecx + 20h]
            faddp st(1), st(0)
            fld dword ptr [ecx + 14h]
            fmul dword ptr [eax - 0ch]
            faddp st(1), st(0)
            fstp dword ptr [edx - 4]
            jnz MatrixRowLoop
        }
        return GR_OK;
    }

    GENRESULT DACOM_API TransformQuat(FVector& Dst, const FQuaternion& Quat, const FVector& Source) override
    {
        const float DotVv = Quat.X * Quat.X + Quat.Y * Quat.Y + Quat.Z * Quat.Z;
        const float Scale = Quat.W * Quat.W - DotVv;
        const float DotQv = Quat.X * Source.X + Quat.Y * Source.Y + Quat.Z * Source.Z;
        const float CrossX = Quat.Y * Source.Z - Quat.Z * Source.Y;
        const float CrossY = Quat.Z * Source.X - Quat.X * Source.Z;
        const float CrossZ = Quat.X * Source.Y - Quat.Y * Source.X;
        Dst.X = Source.X * Scale + Quat.X * (2.0f * DotQv) + CrossX * (Quat.W * 2.0f);
        Dst.Y = Source.Y * Scale + Quat.Y * (2.0f * DotQv) + CrossY * (Quat.W * 2.0f);
        Dst.Z = Source.Z * Scale + Quat.Z * (2.0f * DotQv) + CrossZ * (Quat.W * 2.0f);
        return GR_OK;
    }

    GENRESULT DACOM_API TransformXform(FVector& Dst, const FTransform& Xform, const FVector& Source) override
    {
        // 71890..718ed: translation is added while each dot remains extended,
        // then that component is stored before the next source read.
        float* ResultVector = &Dst.X;
        const float* TransformValues = &Xform.Rotation.D[0][0];
        const float* SourceVector = &Source.X;
        __asm
        {
            mov eax, TransformValues
            mov ecx, SourceVector
            mov edx, ResultVector
            fld dword ptr [eax + 8]
            fmul dword ptr [ecx + 8]
            fld dword ptr [eax + 4]
            fmul dword ptr [ecx + 4]
            faddp st(1), st(0)
            fld dword ptr [eax]
            fmul dword ptr [ecx]
            faddp st(1), st(0)
            fadd dword ptr [eax + 24h]
            fstp dword ptr [edx]
            fld dword ptr [eax + 14h]
            fmul dword ptr [ecx + 8]
            fld dword ptr [eax + 0ch]
            fmul dword ptr [ecx]
            faddp st(1), st(0)
            fld dword ptr [eax + 10h]
            fmul dword ptr [ecx + 4]
            faddp st(1), st(0)
            fadd dword ptr [eax + 28h]
            fstp dword ptr [edx + 4]
            fld dword ptr [eax + 20h]
            fmul dword ptr [ecx + 8]
            fld dword ptr [eax + 18h]
            fmul dword ptr [ecx]
            faddp st(1), st(0)
            fld dword ptr [eax + 1ch]
            fmul dword ptr [ecx + 4]
            faddp st(1), st(0)
            fadd dword ptr [eax + 2ch]
            fstp dword ptr [edx + 8]
        }
        return GR_OK;
    }

    GENRESULT DACOM_API TransformMatrix(FVector& Dst, const FMatrix& Matrix, const FVector& Source) override
    {
        MulVecByMatrix(Dst, Matrix, Source);
        return GR_OK;
    }

    GENRESULT DACOM_API Rotate(FVector& Dst, const FTransform& Xform, const FVector& Source) override
    {
        MulVecByMatrix(Dst, Xform.Rotation, Source);
        return GR_OK;
    }

    GENRESULT DACOM_API InverseRotate(FVector& Dst, const FTransform& Xform, const FVector& Source) override
    {
        MulVecByTranspose(Dst, Xform.Rotation, Source);
        return GR_OK;
    }

    GENRESULT DACOM_API TransposeTransform(FVector& Dst, const FMatrix& Matrix, const FVector& Source) override
    {
        MulVecByTranspose(Dst, Matrix, Source);
        return GR_OK;
    }

    GENRESULT DACOM_API InverseTransform(FVector& Dst, const FTransform& Xform, const FVector& Source) override
    {
        const FVector Delta = {Source.X - Xform.Translation.X, Source.Y - Xform.Translation.Y,
                               Source.Z - Xform.Translation.Z};
        MulVecByTranspose(Dst, Xform.Rotation, Delta);
        return GR_OK;
    }

    GENRESULT DACOM_API TransformListXform(FVector* Dst, const FTransform& Xform, const FVector* Source,
                                           int Count) override
    {
        X86BatchMath::TransformList(Dst, Xform.Rotation, Source, Count, &Xform.Translation);
        return GR_OK;
    }

    GENRESULT DACOM_API TransformListMatrix(FVector* Dst, const FMatrix& Matrix, const FVector* Source,
                                            int Count) override
    {
        X86BatchMath::TransformList(Dst, Matrix, Source, Count);
        return GR_OK;
    }

    GENRESULT DACOM_API TransposeTransformList(FVector* Dst, const FMatrix& Matrix, const FVector* Source,
                                               int Count) override
    {
        FMatrix Transposed;
        TransposeMatrix(Transposed, Matrix);
        return TransformListMatrix(Dst, Transposed, Source, Count);
    }

    GENRESULT DACOM_API InverseTransformList(FVector* Dst, const FTransform& Xform, const FVector* Source,
                                             int Count) override
    {
        FVector NegTranslation = {-Xform.Translation.X, -Xform.Translation.Y, -Xform.Translation.Z};
        FVector InvTranslation;
        MulVecByTranspose(InvTranslation, Xform.Rotation, NegTranslation);

        FTransform Inverse = {};
        TransposeMatrix(Inverse.Rotation, Xform.Rotation);
        Inverse.Translation = InvTranslation;
        return TransformListXform(Dst, Inverse, Source, Count);
    }

    GENRESULT DACOM_API MatrixToQuaternion(FQuaternion& Dst, const FMatrix& Source) override
    {
        const FMatrix* SourceMatrix = &Source;
        FQuaternion* DestinationQuat = &Dst;
        const float Zero = 0.0f;
        const float One = 1.0f;
        const float Half = 0.5f;
        static const int NextAxis[3] = {1, 2, 0};
        // LAB_06f72290..723b1 keeps the trace, root and reciprocal on x87;
        // only the four destination writes round to float. AND EAX,4100/JNZ
        // takes the not-greater (including unordered) branch in all three tests.
        // Keep the branch-specific store/read order for aliased source/output.
        __asm
        {
            mov ecx, SourceMatrix
            mov edi, DestinationQuat
            fld dword ptr [ecx + 10h]
            fadd dword ptr [ecx]
            fadd dword ptr [ecx + 20h]
            fcom Zero
            fnstsw ax
            and eax, 4100h
            jnz MatrixQuatNotPositive
            fadd One
            fsqrt
            fld Half
            fmul st(0), st(1)
            fstp dword ptr [edi]
            fdivr Half
            fld dword ptr [ecx + 1ch]
            fsub dword ptr [ecx + 14h]
            fmul st(0), st(1)
            fstp dword ptr [edi + 4]
            fld dword ptr [ecx + 8]
            fsub dword ptr [ecx + 18h]
            fmul st(0), st(1)
            fstp dword ptr [edi + 8]
            fld dword ptr [ecx + 0ch]
            fsub dword ptr [ecx + 4]
            fmul st(0), st(1)
            fstp dword ptr [edi + 0ch]
            fstp st(0)
            jmp MatrixQuatDone
        MatrixQuatNotPositive:
            fstp st(0)
            fld dword ptr [ecx + 10h]
            fcomp dword ptr [ecx]
            xor edx, edx
            fnstsw ax
            and eax, 4100h
            jnz MatrixQuatSecondDiagonal
            mov edx, 1
        MatrixQuatSecondDiagonal:
            fld dword ptr [ecx + 20h]
            mov eax, edx
            shl eax, 4
            fcomp dword ptr [ecx + eax]
            fnstsw ax
            and eax, 4100h
            jnz MatrixQuatIndices
            mov edx, 2
        MatrixQuatIndices:
            mov eax, dword ptr NextAxis[edx * 4]
            mov esi, dword ptr NextAxis[eax * 4]
            mov ebx, esi
            shl ebx, 4
            fld dword ptr [ecx + ebx]
            mov ebx, eax
            shl ebx, 4
            fadd dword ptr [ecx + ebx]
            mov ebx, edx
            shl ebx, 4
            fsubr dword ptr [ecx + ebx]
            fadd One
            fsqrt
            fld Half
            fmul st(0), st(1)
            fstp dword ptr [edi + edx * 4 + 4]
            fdivr Half
            lea ebx, [esi + esi * 2]
            add ebx, eax
            fld dword ptr [ecx + ebx * 4]
            lea ebx, [eax + eax * 2]
            add ebx, esi
            fsub dword ptr [ecx + ebx * 4]
            fmul st(0), st(1)
            fstp dword ptr [edi]
            lea ebx, [edx + edx * 2]
            add ebx, eax
            fld dword ptr [ecx + ebx * 4]
            lea ebx, [eax + eax * 2]
            add ebx, edx
            fadd dword ptr [ecx + ebx * 4]
            fmul st(0), st(1)
            fstp dword ptr [edi + eax * 4 + 4]
            lea ebx, [edx + edx * 2]
            add ebx, esi
            fld dword ptr [ecx + ebx * 4]
            lea ebx, [esi + esi * 2]
            add ebx, edx
            fadd dword ptr [ecx + ebx * 4]
            fmul st(0), st(1)
            fstp dword ptr [edi + esi * 4 + 4]
            fstp st(0)
        MatrixQuatDone:
        }
        return GR_OK;
    }

    GENRESULT DACOM_API QuaternionToMatrix(FMatrix& Dst, const FQuaternion& Quat) override
    {
        // 723c0..724c4: retain S/Xs and selected products on x87; the
        // asymmetric float scratch stores are part of the arithmetic contract.
        float Scratch[5];
        float Zs;
        const float One = 1.0f;
        const float Two = 2.0f;
        // Named reference operands have different MSVC/Clang inline-asm
        // semantics. Explicit pointers keep both compilers on the same ABI.
        const FQuaternion* const QuatPtr = &Quat;
        FMatrix* const DstPtr = &Dst;
        __asm
        {
            mov eax, QuatPtr
            lea edx, Scratch
            fld dword ptr [eax + 0ch]
            fld dword ptr [eax + 8]
            fld dword ptr [eax + 4]
            fld dword ptr [eax]
            fld st(0)
            fmul st(0), st(1)
            fld st(2)
            fmul st(0), st(3)
            faddp st(1), st(0)
            fld st(3)
            fmul st(0), st(4)
            faddp st(1), st(0)
            fld st(4)
            fmul st(0), st(5)
            faddp st(1), st(0)
            fdivr Two
            fstp st(4)
            fstp st(0)
            fstp st(0)
            fstp st(0)
            fld st(0)
            fmul dword ptr [eax + 4]
            fld st(1)
            fmul dword ptr [eax + 8]
            fstp dword ptr [edx]
            fxch st(1)
            fmul dword ptr [eax + 0ch]
            fstp Zs
            fld st(0)
            fmul dword ptr [eax]
            fstp dword ptr [edx + 0ch]
            fld dword ptr [edx]
            fmul dword ptr [eax]
            fstp dword ptr [edx + 8]
            fld Zs
            fmul dword ptr [eax]
            fstp dword ptr [edx + 4]
            fmul dword ptr [eax + 4]
            fld dword ptr [edx]
            fmul dword ptr [eax + 4]
            fld Zs
            fmul dword ptr [eax + 4]
            fld dword ptr [edx]
            fmul dword ptr [eax + 8]
            fstp dword ptr [edx + 10h]
            fld Zs
            fmul dword ptr [eax + 8]
            fstp dword ptr [edx]
            fld Zs
            fmul dword ptr [eax + 0ch]
            fld dword ptr [edx + 10h]
            // Every quaternion input read precedes the first output write.
            mov eax, DstPtr
            fadd st(0), st(1)
            fsubr One
            fstp dword ptr [eax]
            fld st(2)
            fsub dword ptr [edx + 4]
            fstp dword ptr [eax + 4]
            fld st(1)
            fadd dword ptr [edx + 8]
            fstp dword ptr [eax + 8]
            fxch st(2)
            fadd dword ptr [edx + 4]
            fstp dword ptr [eax + 0ch]
            fxch st(1)
            fadd st(0), st(2)
            fsubr One
            fstp dword ptr [eax + 10h]
            fld dword ptr [edx]
            fsub dword ptr [edx + 0ch]
            fstp dword ptr [eax + 14h]
            fsub dword ptr [edx + 8]
            fstp dword ptr [eax + 18h]
            fld dword ptr [edx]
            fadd dword ptr [edx + 0ch]
            fstp dword ptr [eax + 1ch]
            fld dword ptr [edx + 10h]
            fadd st(0), st(1)
            fsubr One
            fstp dword ptr [eax + 20h]
            fstp st(0)
        }
        return GR_OK;
    }

    GENRESULT DACOM_API QuatNlerp(FQuaternion& Dst, const FQuaternion& A, const FQuaternion& B, float T) override
    {
        return QuatNlerpValue(Dst, A, B, &T);
    }

    GENRESULT DACOM_API QuatNlerpValue(FQuaternion& Dst, const FQuaternion& A, const FQuaternion& B, const float* T) override
    {
        X86QuaternionMath::Nlerp(Dst, A, B, T);
        return GR_OK;
    }

    GENRESULT DACOM_API GeneralInverse(FTransform& Dst, float& W, const FTransform& Source) override
    {
        const float Determinant = Source.Rotation.D[0][0] * (Source.Rotation.D[1][1] * Source.Rotation.D[2][2] -
                                                             Source.Rotation.D[2][1] * Source.Rotation.D[1][2]) -
                                  Source.Rotation.D[0][1] * (Source.Rotation.D[1][0] * Source.Rotation.D[2][2] -
                                                             Source.Rotation.D[2][0] * Source.Rotation.D[1][2]) +
                                  Source.Rotation.D[0][2] * (Source.Rotation.D[1][0] * Source.Rotation.D[2][1] -
                                                             Source.Rotation.D[2][0] * Source.Rotation.D[1][1]);

        GENRESULT Result = GR_OK;
        float InvDet = 1.0f;
        if (std::fabs(Determinant) > G_MinDet)
            InvDet = 1.0f / Determinant;
        else
            Result = GR_INVALID_PARMS;

        WriteAdjugate(Dst.Rotation, Source.Rotation, InvDet);

        const FMatrix& M = Source.Rotation;
        const FVector& T = Source.Translation;
        Dst.Translation.X = -(M.D[0][1] * M.D[1][2] * T.Z + M.D[1][1] * M.D[2][2] * T.X + M.D[2][1] * M.D[0][2] * T.Y -
                              M.D[0][1] * M.D[2][2] * T.Y - M.D[1][1] * M.D[0][2] * T.Z - M.D[2][1] * M.D[1][2] * T.X) *
                            InvDet;
        Dst.Translation.Y = (M.D[0][0] * M.D[1][2] * T.Z + M.D[1][0] * M.D[2][2] * T.X + M.D[2][0] * M.D[0][2] * T.Y -
                             M.D[0][0] * M.D[2][2] * T.Y - M.D[1][0] * M.D[0][2] * T.Z - M.D[2][0] * M.D[1][2] * T.X) *
                            InvDet;
        Dst.Translation.Z = -(M.D[0][0] * M.D[1][1] * T.Z + M.D[1][0] * M.D[2][1] * T.X + M.D[2][0] * M.D[0][1] * T.Y -
                              M.D[0][0] * M.D[2][1] * T.Y - M.D[1][0] * M.D[0][1] * T.Z - M.D[2][0] * M.D[1][1] * T.X) *
                            InvDet;

        W = (M.D[0][0] * M.D[1][1] * M.D[2][2] + M.D[1][0] * M.D[2][1] * M.D[0][2] + M.D[2][0] * M.D[0][1] * M.D[1][2] -
             M.D[2][0] * M.D[1][1] * M.D[0][2] - M.D[0][0] * M.D[2][1] * M.D[1][2] - M.D[1][0] * M.D[0][1] * M.D[2][2]) *
            InvDet;
        return Result;
    }

    GENRESULT DACOM_API InvSqrt(float& Dst, float X) override
    {
        Dst = X86ScalarMath::InvSqrt(X);
        return GR_OK;
    }

    GENRESULT DACOM_API Sqrt(float& Dst, float X) override
    {
        Dst = X86ScalarMath::Sqrt(X);
        return GR_OK;
    }
};

static_assert(sizeof(FMathEngine) == 8, "retail DllMain allocates operator_new(8) — two vfptrs");

FMathEngine* G_Engine = nullptr;

BOOL WINAPI DllMain(HINSTANCE Module, DWORD Reason, LPVOID /*Reserved*/)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Module);
        // FUN_06f71090: PUSH 8 / operator_new. Null returns TRUE without registration.
        void* Memory = ::operator new(8, std::nothrow);
        if (Memory == nullptr)
        {
            G_Engine = nullptr;
            return TRUE;
        }
        G_Engine = new (Memory) FMathEngine();
        // 06f71107: DACOM_Acquire; JZ 06f71145 skips registration silently (no log in retail).
        ICOManager* Dacom = DACOM_Acquire();
        if (Dacom != nullptr)
        {
            Dacom->RegisterComponent(static_cast<IComponentFactory*>(G_Engine), G_ComponentName,
                                     reinterpret_cast<void*>(G_RegisterPriority));
        }
    }
    return TRUE;
}
