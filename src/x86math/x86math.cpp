// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "x86math.h"
#include "BatchTransform.h"
#include "MatrixMath.h"
#include "TransformMath.h"
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

    // Rotate/TransformMatrix share the portable, in-place-safe batch operation.
    void MulVecByMatrix(FVector& Dst, const FMatrix& Matrix, const FVector& Source)
    {
        X86MatrixMath::TransformVector(Dst, Matrix, Source);
    }

    // InverseRotate/TransposeTransform share the portable transpose operation.
    void MulVecByTranspose(FVector& Dst, const FMatrix& Matrix, const FVector& Source)
    {
        X86MatrixMath::TransformTranspose(Dst, Matrix, Source);
    }

    void TransposeMatrix(FMatrix& Dst, const FMatrix& Source)
    {
        X86MatrixMath::Transpose(Dst, Source);
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
        X86TransformMath::Compose(Dst, A, B);
        return GR_OK;
    }

    GENRESULT DACOM_API MulMatrix(FMatrix& Dst, const FMatrix& A, const FMatrix& B) override
    {
        X86MatrixMath::Multiply(Dst, A, B);
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
        X86TransformMath::Apply(Dst, Xform, Source);
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
        X86QuaternionMath::FromMatrix(Dst, Source);
        return GR_OK;
    }

    GENRESULT DACOM_API QuaternionToMatrix(FMatrix& Dst, const FQuaternion& Quat) override
    {
        X86QuaternionMath::ToMatrix(Dst, Quat);
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
