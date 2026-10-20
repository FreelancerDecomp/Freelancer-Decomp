// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Thorn.h"
#include "SplineMath.h"
#include "SplineQuaternionMath.h"
#include "SplineSamplingMath.h"
#include "SceneGeometryMath.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <vector>

// CV_CRSplinePath / CV_CROrientationSplinePath Р В Р вЂ Р В РІР‚С™Р Р†Р вЂљРЎСљ docs/re/thorn.md
// Position: LAB_06f22dc0 Р В Р вЂ Р Р†Р вЂљР’В Р Р†Р вЂљРІвЂћСћ FUN_06f230b0 (two FUN_06f21000 + lerp).
// Tangent: LAB_06f22d70 Р В Р вЂ Р Р†Р вЂљР’В Р Р†Р вЂљРІвЂћСћ FUN_06f23230 (two FUN_06f213d0 + lerp).
// Orientation: LAB_06f22ec0 Р В Р вЂ Р Р†Р вЂљР’В Р Р†Р вЂљРІвЂћСћ FUN_06f233b0 (two FUN_06f21770 + I3DMathEngine+0x58).
// Look-at LAB_06f34180: X = -tangent, Y from existing out Y column, no identity fill.
// IMotionPath slot 5: position LAB_06f34420 (tangent Р В Р вЂ Р Р†Р вЂљР’В Р Р†Р вЂљРІвЂћСћ quat); orientation LAB_06f35530
// (inner vtable+0x08). FSplinePath's IMotionPath overrides branch on bOrientation_ (retail: two
// vptrs, one per class).

static const int G_MathMulQuatSlot = 0x18 / 4;
static const int G_MathSlerpSlot = 0x58 / 4;
static const int G_MathQuatToMatrixSlot = 0x54 / 4;
static const float G_QuatSinEps = 0.001f;
static const int G_CrSamples = 16;              // inner +0x8
static const float G_CrStep = 0.0625f;          // inner +0xC = 0x3d800000
static const int G_SamplesPerSegment = G_CrSamples + 1;

namespace
{
    // DAT_06f61e18: wrappers share this lazy cache; retail has no Release
    // consumer. The quaternion interpolation core uses DAT_06f61dd0.
    I3DMathEngine* G_SplineMathEngine = nullptr;

    I3DMathEngine* AcquireSplineMathEngine()
    {
        if (G_SplineMathEngine == nullptr)
            G_SplineMathEngine = ThornAcquire3DMathEngine();
        return G_SplineMathEngine;
    }

    // DAT_06f61dd0 is a distinct cache for quaternion interpolation.
    I3DMathEngine* G_SplineCoreMathEngine = nullptr;

    I3DMathEngine* AcquireSplineCoreMathEngine()
    {
        if (G_SplineCoreMathEngine == nullptr)
            G_SplineCoreMathEngine = ThornAcquire3DMathEngine();
        return G_SplineCoreMathEngine;
    }

    // FUN_06f249c0: the foreign result is distinct, even if Dst aliases A/B.
    void CoreQuatMul(FQuat& Dst, const FQuat& A, const FQuat& B)
    {
        I3DMathEngine* Math = AcquireSplineCoreMathEngine();
        FQuaternion InputA;
        FQuaternion InputB;
        FQuaternion Product;
        // The actual factory precedes all foreign operand reads. The sole
        // MulQuat producer is callback-free, and writes a separate scratch.
        std::memcpy(&InputA, &A, sizeof(InputA));
        std::memcpy(&InputB, &B, sizeof(InputB));
        Math->MulQuat(Product, InputA, InputB);
        std::memcpy(&Dst, &Product, sizeof(Dst));
    }

    void CoreQuatNlerpWithMath(I3DMathEngine* Math, FQuat& Dst,
        const FQuat& A, const FQuat& B, const float* T)
    {
        FQuaternion InputA;
        FQuaternion InputB;
        FQuaternion Result;
        std::memcpy(&InputA, &A, sizeof(InputA));
        std::memcpy(&InputB, &B, sizeof(InputB));
        Math->QuatNlerpValue(Result, InputA, InputB, T);
        std::memcpy(&Dst, &Result, sizeof(Dst));
    }

    void CoreQuatNlerp(FQuat& Dst, const FQuat& A, const FQuat& B, const float* T)
    {
        I3DMathEngine* Math = AcquireSplineCoreMathEngine();
        CoreQuatNlerpWithMath(Math, Dst, A, B, T);
    }

    // Inlined FUN_06f21770: inverse squared norm, narrowed once to float.
    void QuatInverseScaleRetail(const FQuat& Q, float& Scale)
    {
        Scale = ThornSplineQuaternion::InverseScale(Q);
    }

    // Squad blend weight: 2*t*(1-t), with a final float result.
    float SquadBlendRetail(const float* T)
    {
        const float One = 1.0f;
        float Blend;
        __asm
        {
            mov ecx, T
            fld One
            fsub dword ptr [ecx]
            fmul dword ptr [ecx]
            fadd st(0), st(0)
            fstp Blend
        }
        return Blend;
    }

    // LAB_06f34420: tangent basis with explicit float normalization boundaries.
    void BuildSlot5MatrixRetail(const FVec3& Tangent, float* Mat9)
    {
        float Scratch[27];
        std::memcpy(Scratch + 3, &Tangent, sizeof(Tangent));
        const float One = 1.0f;
        const float Zero = 0.0f;
        __asm
        {
            lea edx, Scratch
            fld dword ptr [edx + 14h]
            fmul dword ptr [edx + 14h]
            fld dword ptr [edx + 10h]
            fmul dword ptr [edx + 10h]
            faddp st(1), st(0)
            fld dword ptr [edx + 0ch]
            fmul dword ptr [edx + 0ch]
            faddp st(1), st(0)
            fsqrt
            fdivr One
            fld dword ptr [edx + 0ch]
            fmul st(0), st(1)
            fstp dword ptr [edx + 0ch]
            fld dword ptr [edx + 10h]
            fmul st(0), st(1)
            fstp dword ptr [edx + 10h]
            fld dword ptr [edx + 14h]
            fmul st(0), st(1)
            fstp dword ptr [edx + 14h]
            fstp st(0)
            fld dword ptr [edx + 0ch]
            fchs
            fstp dword ptr [edx + 0ch]
            fld dword ptr [edx + 10h]
            fchs
            fstp dword ptr [edx + 10h]
            fld dword ptr [edx + 14h]
            fchs
            fstp dword ptr [edx + 14h]
            fld dword ptr [edx + 10h]
            fmul Zero
            fld dword ptr [edx + 14h]
            fsub st(0), st(1)
            fstp dword ptr [edx + 18h]
            fld dword ptr [edx + 0ch]
            fmul Zero
            fld dword ptr [edx + 14h]
            fmul Zero
            fsubp st(1), st(0)
            fstp dword ptr [edx + 1ch]
            fsub dword ptr [edx + 0ch]
            fst dword ptr [edx + 20h]
            fmul dword ptr [edx + 20h]
            fld dword ptr [edx + 1ch]
            fmul dword ptr [edx + 1ch]
            faddp st(1), st(0)
            fld dword ptr [edx + 18h]
            fmul dword ptr [edx + 18h]
            faddp st(1), st(0)
            fsqrt
            fdivr One
            fstp dword ptr [edx + 68h]
            fld dword ptr [edx + 18h]
            fmul dword ptr [edx + 68h]
            fld dword ptr [edx + 1ch]
            fmul dword ptr [edx + 68h]
            fld dword ptr [edx + 20h]
            fmul dword ptr [edx + 68h]
            fstp dword ptr [edx + 20h]
            fld dword ptr [edx + 10h]
            fmul dword ptr [edx + 20h]
            fld dword ptr [edx + 14h]
            fmul st(0), st(2)
            fsubp st(1), st(0)
            fld dword ptr [edx + 14h]
            fmul st(0), st(3)
            fld dword ptr [edx + 0ch]
            fmul dword ptr [edx + 20h]
            fsubp st(1), st(0)
            fstp dword ptr [edx + 28h]
            fld dword ptr [edx + 0ch]
            fmul st(0), st(2)
            fld dword ptr [edx + 10h]
            fmul st(0), st(4)
            fsubp st(1), st(0)
            fst dword ptr [edx + 2ch]
            fmul dword ptr [edx + 2ch]
            fld dword ptr [edx + 28h]
            fmul dword ptr [edx + 28h]
            faddp st(1), st(0)
            fld st(1)
            fmul st(0), st(2)
            faddp st(1), st(0)
            fsqrt
            fdivr One
            fstp dword ptr [edx + 68h]
            fmul dword ptr [edx + 68h]
            fld dword ptr [edx + 28h]
            fmul dword ptr [edx + 68h]
            fld dword ptr [edx + 2ch]
            fmul dword ptr [edx + 68h]
            fstp dword ptr [edx + 5ch]
            fxch st(1)
            fstp dword ptr [edx + 44h]
            fstp dword ptr [edx + 50h]
            fxch st(1)
            fstp dword ptr [edx + 40h]
            fstp dword ptr [edx + 4ch]
        }
        std::memcpy(Scratch + 18, Scratch + 3, sizeof(float));
        std::memcpy(Scratch + 21, Scratch + 4, sizeof(float));
        std::memcpy(Scratch + 24, Scratch + 5, sizeof(float));
        std::memcpy(Scratch + 22, Scratch + 8, sizeof(float));
        std::memcpy(Mat9, Scratch + 16, 9 * sizeof(float));
    }

    void CatmullRomVec3(const FVec3& P0, const FVec3& P1, const FVec3& P2, const FVec3& P3, float U,
                        FVec3& Out)
    {
        ThornSplineMath::CatmullRom3D(&P0.X, &P1.X, &P2.X, &P3.X, U, &Out.X);
    }

    void CatmullRomTangent(const FVec3& P0, const FVec3& P1, const FVec3& P2, const FVec3& P3,
                           float U, FVec3& Out)
    {
        ThornSplineMath::CatmullRomTangent3D(&P0.X, &P1.X, &P2.X, &P3.X, U, &Out.X);
    }

    // FUN_06f34bc0 / FUN_06f35580 / LAB_06f34ca0 / LAB_06f356c0
    static const char G_PersistOpen[] = "OPEN";
    static const char G_PersistClosed[] = "CLOSED";
    static const char G_PersistFlagFmt[] = "%s, ";
    static const char G_PersistVec3Fmt[] = "{%f,%f,%f}, ";
    static const char G_PersistQuatFmt[] = "{%f,%f,%f,%f}, ";
    static const char G_PersistDigits[] = "0123456789.";
    static const char G_PersistNumberChars[] = "-0123456789.";
    static const unsigned long G_PersistPrefixBytes = 10;
    static const unsigned long G_PersistVec3Bytes = 36;       // LAB_06f34ca0: count*9*4
    static const unsigned long G_PersistOrientVec3Bytes = 40; // LAB_06f356c0: count*5*8
    static const unsigned long G_PersistQuatBytes = 51;       // LAB_06f356c0: count*0x33

    // FUN_06f34a10 (Needed=3) / FUN_06f34ae0 (Needed=4).
    bool ParsePersistFloats(const char** Cursor, float* Out, int Needed)
    {
        float Local[4];
        int Index = 0;
        do
        {
            const char* Number = strpbrk(*Cursor, G_PersistNumberChars);
            if (Number == nullptr)
            {
                if (Index == 0)
                    *Cursor = nullptr;
                return false;
            }
            strspn(Number, G_PersistDigits);
            const unsigned int Span =
                static_cast<unsigned int>(strspn(Number, G_PersistNumberChars));
            if (Span == 0)
                return false;
            Local[Index] = static_cast<float>(atof(Number));
            const char* After = Number + Span;
            *Cursor = After;
            if (*After == '}' && Index != Needed - 1)
            {
                *Cursor = nullptr;
                return false;
            }
            After += 1;
            Index += 1;
            *Cursor = After;
        } while (Index < Needed);

        if (Index != Needed)
            return false;
        if (Needed == 3)
        {
            // The third float is loaded before the first output word, but
            // its explicit float store follows that first publication.
            volatile float X = Local[0];
            volatile float Y = Local[1];
            const double Z = static_cast<double>(Local[2]);
            std::memcpy(Out, const_cast<const float*>(&X), sizeof(float));
            volatile float StoredZ = static_cast<float>(Z);
            std::memcpy(Out + 1, const_cast<const float*>(&Y), sizeof(float));
            std::memcpy(Out + 2, const_cast<const float*>(&StoredZ), sizeof(float));
            return true;
        }
        volatile float W = Local[0];
        volatile float X = Local[1];
        const double Y = static_cast<double>(Local[2]);
        std::memcpy(Out, const_cast<const float*>(&W), sizeof(float));
        volatile float StoredY = static_cast<float>(Y);
        const double Z = static_cast<double>(Local[3]);
        std::memcpy(Out + 1, const_cast<const float*>(&X), sizeof(float));
        volatile float StoredZ = static_cast<float>(Z);
        std::memcpy(Out + 2, const_cast<const float*>(&StoredY), sizeof(float));
        std::memcpy(Out + 3, const_cast<const float*>(&StoredZ), sizeof(float));
        return true;
    }

    // Retail FUN_06f36460 constructs a sentinel-backed ordered tree.  The
    // parser uses it as an index -> point map; the VC6 node layout is not an
    // ABI contract, so std::map preserves the observable ordering directly.
    // FUN_06f34f30 (listing 41198-41287) and FUN_06f35b40 (listing
    // 42454-42543) are this tree's own manual teardown -- confirmed
    // byte-for-byte identical in shape (same two-mode "linear sentinel vs.
    // generic node walk+delete" dispatch), each called from a different
    // PersistSlot3Load-family cleanup path (FUN_06f34f30 from 06f58a23/
    // 06f58ac3; FUN_06f35b40 from 06f58acb) with a different per-node
    // release callback (FUN_06f36420/FUN_06f35cc0 vs FUN_06f36b00/
    // FUN_06f35d80) -- consistent with one tree holding parsed points and a
    // second, differently-typed tree holding parsed quaternions during
    // orientation LoadPersist. This struct's std::map and LoadPersist's
    // std::vector<FQuat> (below) are both stack-scoped RAII containers, so
    // neither manual teardown routine has a modern counterpart to port --
    // the compiler-generated destructor call is unconditional and automatic.
    struct FPersistPointTree
    {
        std::map<unsigned int, FVec3> Entries;
    };

    // FUN_06f36460: initialize the two retail sentinel nodes and size=0.
    void ThornPersistTreeInit(FPersistPointTree& Tree)
    {
        Tree.Entries.clear();
    }

    // FUN_06f35e40 with the retail tree in unique-key mode (local_14 == 0).
    // The returned node is the existing value for a duplicate key, otherwise
    // the newly inserted value; insertion is ordered by the uint key.
    bool ThornPersistTreeInsert(FPersistPointTree& Tree, unsigned int Key,
                                const FVec3& Point, FVec3*& OutPoint)
    {
        std::map<unsigned int, FVec3>::iterator Found = Tree.Entries.lower_bound(Key);
        if (Found != Tree.Entries.end() && Found->first == Key)
        {
            OutPoint = &Found->second;
            return false;
        }
        std::pair<std::map<unsigned int, FVec3>::iterator, bool> Result =
            Tree.Entries.insert(std::make_pair(Key, Point));
        if (!Result.second)
        {
            OutPoint = &Result.first->second;
            return false;
        }
        OutPoint = &Result.first->second;
        return true;
    }

    // FUN_06f35cc0: return the first in-order node (retail head->next).
    bool ThornPersistTreeBegin(FPersistPointTree& Tree, FVec3*& OutPoint)
    {
        std::map<unsigned int, FVec3>::iterator Found = Tree.Entries.begin();
        if (Found == Tree.Entries.end())
        {
            OutPoint = nullptr;
            return false;
        }
        OutPoint = &Found->second;
        return true;
    }

    // FUN_06f22030: publish cumulative length before reading the global offset.
    void AccumulateSegmentSampleRetail(FVec3& Prev, const FVec3& Cur,
                                       const float& GlobalOffset, float& SegmentLength,
                                       float& SampleTime)
    {
        // One addressable argument block avoids exhausting clang-cl's inline
        // asm operand registers; the x87 instruction sequence is unchanged.
        const void* const Args[] = {&Prev, &Cur, &GlobalOffset, &SegmentLength, &SampleTime};
        __asm
        {
            lea esi, Args
            mov edx, dword ptr [esi + 4]
            mov ecx, dword ptr [esi]
            fld dword ptr [edx]
            fsub dword ptr [ecx]
            mov eax, dword ptr [edx]
            fld dword ptr [edx + 4]
            mov ebx, dword ptr [edx + 8]
            fsub dword ptr [ecx + 4]
            mov edi, dword ptr [edx + 4]
            fld dword ptr [edx + 8]
            mov dword ptr [ecx], eax
            fsub dword ptr [ecx + 8]
            fld st(0)
            mov dword ptr [ecx + 8], ebx
            fmul st(0), st(1)
            fld st(2)
            fmul st(0), st(3)
            mov dword ptr [ecx + 4], edi
            faddp st(1), st(0)
            fld st(3)
            fmul st(0), st(4)
            faddp st(1), st(0)
            fsqrt
            fstp st(3)
            fstp st(0)
            fstp st(0)
            mov eax, dword ptr [esi + 12]
            fadd dword ptr [eax]
            fst dword ptr [eax]
            mov eax, dword ptr [esi + 8]
            fadd dword ptr [eax]
            mov edx, dword ptr [esi + 16]
            fstp dword ptr [edx]
        }
    }

    // Observe pending exceptions after Total is stored and before Time is published.
    __declspec(noinline) float ReadPublishedTotal(const float& Total)
    {
        return Total;
    }

    // FUN_06f222d0: one reciprocal shared by 17 float sample stores.
    void NormalizeSegmentSamplesRetail(float* Table, float Total)
    {
        const float One = 1.0f;
        const int Count = G_SamplesPerSegment;
        __asm
        {
            mov edx, Table
            mov ecx, Count
            fld One
            fdiv Total
        NormalizeSample:
            fld st(0)
            fmul dword ptr [edx]
            fstp dword ptr [edx]
            add edx, 4
            dec ecx
            jnz NormalizeSample
            fstp st(0)
        }
    }

    void LerpVec3(const FVec3& A, const FVec3& B, float F, FVec3& Out)
    {
        ThornSplineMath::Lerp3D(&A.X, &B.X, F, &Out.X);
    }

    void IdentityQuat(FQuat& Q)
    {
        Q.W = 1.0f;
        Q.X = 0.0f;
        Q.Y = 0.0f;
        Q.Z = 0.0f;
    }

    // LAB_06f34180: normalize then negate the tangent, normalize
    // ExistingY x Forward, then negate normalize(RawFirstCross x ExistingX).
    // The two input columns are read before rotational output publication.
    void NormalizeLookAtVector(FVec3& Vector)
    {
        ThornSceneGeometry::Normalize(Vector.X, Vector.Y, Vector.Z, true);
    }

    void CrossLookAtVector(FVec3& Result, const FVec3& A, const FVec3& B, bool bReverseOrder)
    {
        ThornSceneGeometry::LookAtCross(Result, A, B, bReverseOrder);
    }

    void BuildLookAtFromTangent(const FVec3& Tangent, float* OutXform)
    {
        ThornSceneGeometry::BuildLookAtFrame(Tangent, OutXform);
    }

    void QuatAdd(FQuat& Dst, const FQuat& A, const FQuat& B)
    {
        // Ordinary double temporaries spill to qword in Debug. At PC64,
        // 1 + 2^-60 then traps on that spill before the retail W publication.
        FQuat* const DstPtr = &Dst;
        const FQuat* const APtr = &A;
        const FQuat* const BPtr = &B;
        __asm
        {
            mov edx, APtr
            mov ecx, BPtr
            fld dword ptr [edx + 12]
            fadd dword ptr [ecx + 12]
            fld dword ptr [edx + 8]
            fadd dword ptr [ecx + 8]
            fld dword ptr [edx + 4]
            fadd dword ptr [ecx + 4]
            fld dword ptr [edx]
            fadd dword ptr [ecx]
            mov edx, DstPtr
            fstp dword ptr [edx]
            fstp dword ptr [edx + 4]
            fstp dword ptr [edx + 8]
            fstp dword ptr [edx + 12]
        }
    }

    void QuatScale(FQuat& Dst, const FQuat& Q, float S)
    {
        float W;
        float X;
        float Y;
        float Z;
        // Release C++ retained S and reversed the first three FMUL operands:
        // the differential probe found NaN payload and denormal flag changes.
        // Explicit pointer operands avoid compiler-specific reference dereferencing.
        auto* const DstAsmPtr = &Dst;
        auto* const QAsmPtr = &Q;
        __asm
        {
            mov edx, QAsmPtr
            fld S
            fmul dword ptr [edx]
            fstp W
            fld S
            fmul dword ptr [edx + 4]
            fstp X
            fld S
            fmul dword ptr [edx + 8]
            fstp Y
            fld S
            fmul dword ptr [edx + 12]
            mov edx, DstAsmPtr
            mov eax, W
            mov dword ptr [edx], eax
            mov eax, X
            mov dword ptr [edx + 4], eax
            fstp Z
            mov eax, Y
            mov dword ptr [edx + 8], eax
            mov eax, Z
            mov dword ptr [edx + 12], eax
        }
    }

    // FUN_06f24070 is conjugation, with a raw retained W and negated XYZ.
    void QuatConjugate(FQuat& Dst, const FQuat& Q)
    {
        const float X = -Q.X;
        unsigned int W;
        std::memcpy(&W, &Q.W, sizeof(W));
        const float Y = -Q.Y;
        const double Z = -static_cast<double>(Q.Z);
        std::memcpy(&Dst.W, &W, sizeof(W));
        std::memcpy(&Dst.X, &X, sizeof(float));
        const float StoredZ = static_cast<float>(Z);
        std::memcpy(&Dst.Y, &Y, sizeof(float));
        std::memcpy(&Dst.Z, &StoredZ, sizeof(float));
    }

    // Standalone log retains a double scale; control variants narrow it to float.
#pragma intrinsic(acos)
    __declspec(noinline) bool IsQuatLogAccepted(const float& W)
    {
        // An inline gate lets Release retain W for acos and pop it on the
        // fallback. That waiting pop precedes the retail raw W/XYZ stores.
        return std::fabs(static_cast<double>(W)) < 1.0;
    }

    void QuatLogRetail(FQuat& Dst, const FQuat& Q, bool bRoundScale)
    {
        if (!IsQuatLogAccepted(Q.W))
        {
            const unsigned int Zero = 0;
            std::memcpy(&Dst.W, &Zero, sizeof(Zero));
            std::memcpy(&Dst.X, &Q.X, sizeof(float));
            std::memcpy(&Dst.Y, &Q.Y, sizeof(float));
            std::memcpy(&Dst.Z, &Q.Z, sizeof(float));
            return;
        }

        // The mandatory modern std::acos call publishes a double result.
        // That published double is the input to the owned arithmetic below.
        const double Angle = std::acos(static_cast<double>(Q.W));
        const float Epsilon = G_QuatSinEps;
        float Scale;
        float X;
        float Y;
        float Z;
        bool bFallback = false;
        // The intrinsic candidate spills Z before publishing X/Y: with a
        // denormal Z and unmasked DE, both builds trap before those stores.
        // 241ce..241f0 publishes X/Y between the Z product and its float store.
        // Explicit pointer operands avoid compiler-specific reference dereferencing.
        auto* const DstAsmPtr = &Dst;
        auto* const QAsmPtr = &Q;
        __asm
        {
            fld Angle
            fld st(0)
            fsin
            fcom Epsilon
            fnstsw ax
            test ah, 1
            jnz LogCopyInput
            fxch st(1)
            fdiv st(0), st(1)
            cmp bRoundScale, 0
            je LogRetainedProducts
            // 218e9 stores Scale before discarding the retained sine.
            fstp Scale
            fstp st(0)
            jmp LogArithmeticDone
        LogRetainedProducts:
            fxch st(1)
            fstp st(0)
            mov edx, QAsmPtr
            fld st(0)
            fmul dword ptr [edx + 4]
            fstp X
            fld st(0)
            fmul dword ptr [edx + 8]
            fstp Y
            fmul dword ptr [edx + 12]
            mov ecx, DstAsmPtr
            mov eax, X
            mov dword ptr [ecx + 4], eax
            mov eax, Y
            mov dword ptr [ecx + 8], eax
            fstp Z
            mov dword ptr [ecx], 0
            mov eax, Z
            mov dword ptr [ecx + 12], eax
            jmp LogArithmeticDone
        LogCopyInput:
            fstp st(0)
            fstp st(0)
            mov bFallback, 1
        LogArithmeticDone:
        }
        if (bFallback)
        {
            const unsigned int Zero = 0;
            std::memcpy(&Dst.W, &Zero, sizeof(Zero));
            std::memcpy(&Dst.X, &Q.X, sizeof(float));
            std::memcpy(&Dst.Y, &Q.Y, sizeof(float));
            std::memcpy(&Dst.Z, &Q.Z, sizeof(float));
            return;
        }
        if (bRoundScale)
        {
            FVec3 Scaled;
            ThornSplineMath::Scale(&Scaled.X, &Q.X, Scale);
            const unsigned int Zero = 0;
            std::memcpy(&Dst.W, &Zero, sizeof(Zero));
            std::memcpy(&Dst.X, &Scaled.X, sizeof(float));
            std::memcpy(&Dst.Y, &Scaled.Y, sizeof(float));
            std::memcpy(&Dst.Z, &Scaled.Z, sizeof(float));
        }
    }

    void QuatLog(FQuat& Dst, const FQuat& Q)
    {
        QuatLogRetail(Dst, Q, false);
    }

    // The first inline exp21970..219d2 differs from standalone24670:
    // retain norm for FSIN, but publish a float scale before Scale3D.
    void QuatFirstControlExp(FQuat& Dst, const FQuat& Q)
    {
        const float Epsilon = G_QuatSinEps;
        float Norm;
        float Scale;
        bool bScaleVector = false;
        // Intrinsic C++ changes FSIN/FCOS results and drops the float scale
        // boundary in Release; the root PC/RC probe confirms numeric cases.
        // Explicit pointer operands avoid compiler-specific reference dereferencing.
        auto* const QAsmPtr = &Q;
        __asm
        {
            mov edx, QAsmPtr
            fld dword ptr [edx + 4]
            fmul dword ptr [edx + 4]
            fld dword ptr [edx + 8]
            fmul dword ptr [edx + 8]
            faddp st(1), st(0)
            fld dword ptr [edx + 12]
            fmul dword ptr [edx + 12]
            faddp st(1), st(0)
            fsqrt
            fst Norm
            fsin
            fcom Epsilon
            fnstsw ax
            test ah, 1
            jnz FirstExpCopyVector
            fdiv Norm
            fstp Scale
            mov bScaleVector, 1
            jmp FirstExpVectorReady
        FirstExpCopyVector:
            fstp st(0)
        FirstExpVectorReady:
        }
        FQuat Result;
        if (bScaleVector)
            ThornSplineMath::Scale(&Result.X, &Q.X, Scale);
        else
        {
            std::memcpy(&Result.X, &Q.X, sizeof(float));
            std::memcpy(&Result.Y, &Q.Y, sizeof(float));
            std::memcpy(&Result.Z, &Q.Z, sizeof(float));
        }
        // FCOS follows Scale3D, and consumes its explicit float norm.
        __asm
        {
            fld Norm
            fcos
            fstp Result.W
        }
        std::memcpy(&Dst, &Result, sizeof(Result));
    }

    // FUN_06f24670 rounds the norm before FSIN. The first inline exp
    // in FUN_06f21770 keeps it extended for FSIN, but division/FCOS
    // still use its explicit float copy in both paths.
    void QuatExpRetail(FQuat& Dst, const FQuat& Q, bool bKeepNormForSin)
    {
        const float Epsilon = G_QuatSinEps;
        float Norm;
        FQuat Result;
        Result.X = Q.X;
        Result.Y = Q.Y;
        Result.Z = Q.Z;
        // Explicit pointer operands avoid compiler-specific reference dereferencing.
        auto* const QAsmPtr = &Q;
        __asm
        {
            mov edx, QAsmPtr
            fld dword ptr [edx + 4]
            fmul st(0), st(0)
            fld dword ptr [edx + 8]
            fmul st(0), st(0)
            faddp st(1), st(0)
            fld dword ptr [edx + 12]
            fmul st(0), st(0)
            faddp st(1), st(0)
            fsqrt
            fst Norm
            cmp bKeepNormForSin, 0
            jne ExpSin
            fstp st(0)
            fld Norm
        ExpSin:
            fsin
            fcom Epsilon
            fnstsw ax
            test ah, 01h
            jnz ExpPopSin
            fdiv Norm
            lea ecx, Result
            fld st(0)
            fmul dword ptr [edx + 4]
            fstp dword ptr [ecx + 4]
            fld st(0)
            fmul dword ptr [edx + 8]
            fstp dword ptr [ecx + 8]
            fmul dword ptr [edx + 12]
            fstp dword ptr [ecx + 12]
            jmp ExpCos
        ExpPopSin:
            fstp st(0)
            // Standalone fallback copies via FLD/FSTP; the inline first
            // control copies raw words. Preserve even signaling NaN handling.
            cmp bKeepNormForSin, 0
            jne ExpCos
            lea ecx, Result
            fld dword ptr [edx + 12]
            fld dword ptr [edx + 8]
            fld dword ptr [edx + 4]
            fld Norm
            fcos
            fstp dword ptr [ecx]
            fstp dword ptr [ecx + 4]
            fstp dword ptr [ecx + 8]
            fstp dword ptr [ecx + 12]
            jmp ExpDone
        ExpCos:
            lea ecx, Result
            fld Norm
            fcos
            fstp dword ptr [ecx]
        ExpDone:
        }
        Dst = Result;
    }

    void QuatExp(FQuat& Dst, const FQuat& Q)
    {
        QuatExpRetail(Dst, Q, false);
    }

    // Retail (thorn.dll.txt:10494-10530, FUN_06f249c0, inlined at 0x06f21826/
    // 0x06f218a4) calls I3DMathEngine vtbl+0x18 and consumes the quaternion
    // product unconditionally -- it never tests the returned GENRESULT before
    // taking log() of it.
    void QuatLogDiff(FQuat& Dst, const FQuat& InvCenter, const FQuat& Neighbor, bool bRoundScale)
    {
        FQuat Product;
        CoreQuatMul(Product, InvCenter, Neighbor);
        if (bRoundScale)
            QuatLogRetail(Dst, Product, true);
        else
            QuatLog(Dst, Product);
    }

#pragma float_control(except, on, push)
    __declspec(noinline) void AddFirstControlComponent(float* Output, const float* Prev, const float* Next)
    {
        // Ordinary inline addition reverses the loads in Release: loading
        // a denormal Next first adds DE even when a qNaN Prev suppresses it.
        *Output = *Prev + *Next;
    }
#pragma float_control(pop)

    // Inner squad control: q * exp(-1/4 (log(q^{-1} q_prev) + log(q^{-1} q_next)))
    // Retail never checks any of the three underlying GENRESULTs (see
    // QuatLogDiff and the final vtbl+0x18 call at thorn.dll.txt:10494-10530,
    // called from FUN_06f21770 at 0x06f21826/0x06f218a4) -- it always
    // proceeds to the next step with whatever the math engine wrote.
    void QuatSquadControl(FQuat& Dst, const FQuat& Prev, const FQuat& Center,
                          const FQuat& Next, const FQuat& InvCenter, bool bFirstControl)
    {
        FQuat LogPrev;
        FQuat LogNext;
        QuatLogDiff(LogPrev, InvCenter, Prev, true);
        QuatLogDiff(LogNext, InvCenter, Next, bFirstControl);

        FQuat Sum;
        if (bFirstControl)
        {
            // 21914..2194a inlines four rounded sums in Z/Y/X/W order.
            AddFirstControlComponent(&Sum.Z, &LogPrev.Z, &LogNext.Z);
            AddFirstControlComponent(&Sum.Y, &LogPrev.Y, &LogNext.Y);
            AddFirstControlComponent(&Sum.X, &LogPrev.X, &LogNext.X);
            AddFirstControlComponent(&Sum.W, &LogPrev.W, &LogNext.W);
        }
        else
            QuatAdd(Sum, LogPrev, LogNext);
        FQuat Scaled;
        QuatScale(Scaled, Sum, -0.25f);

        FQuat ExpQ;
        if (bFirstControl)
            QuatFirstControlExp(ExpQ, Scaled);
        else
            QuatExp(ExpQ, Scaled);
        CoreQuatMul(Dst, Center, ExpQ);
    }

    // FUN_06f248c0: squad(q1,a,b,q2,t) = nlerp(nlerp(q1,q2,t), nlerp(a,b,t), 2t(1-t))
    // Retail (thorn.dll.txt:10393-10488) calls I3DMathEngine vtbl+0x58 three
    // times and never tests any of the three GENRESULTs -- the blend and
    // final nlerp always consume whatever the previous call wrote, and the
    // four output dwords are always stored.
    void QuatSquad(FQuat& Dst, const FQuat& Q1, const FQuat& A, const FQuat& B,
                   const FQuat& Q2, const float* T)
    {
        FQuat NlerpAb;
        FQuat NlerpQ;
        CoreQuatNlerp(NlerpAb, A, B, T);
        CoreQuatNlerp(NlerpQ, Q1, Q2, T);
        // 24952..24969 acquires the third cache BEFORE reading T for Blend.
        I3DMathEngine* Math = AcquireSplineCoreMathEngine();
        const float Blend = SquadBlendRetail(T);
        CoreQuatNlerpWithMath(Math, Dst, NlerpQ, NlerpAb, &Blend);
    }

    // FUN_06f21770: Catmull-Rom quaternion on Q0..Q3 at U. Retail
    // (thorn.dll.txt:4396-4754) never tests the GENRESULT of any inner
    // QuatMul/QuatNlerp vtbl call and unconditionally stores the result;
    // it always falls through to `XOR EAX,EAX / RET`, i.e. forced success.
    bool EvalCrOrientation(const FQuat& Q0, const FQuat& Q1, const FQuat& Q2,
                           const FQuat& Q3, float U, FQuat& Out)
    {
        // Both center inverses precede the first callback-capable math call.
        float ScaleA;
        QuatInverseScaleRetail(Q1, ScaleA);
        FQuat InverseA;
        QuatConjugate(InverseA, Q1);
        QuatScale(InverseA, InverseA, ScaleA);
        float ScaleB;
        QuatInverseScaleRetail(Q2, ScaleB);
        FQuat InverseB;
        QuatConjugate(InverseB, Q2);
        QuatScale(InverseB, InverseB, ScaleB);
        FQuat ControlA;
        FQuat ControlB;
        QuatSquadControl(ControlA, Q0, Q1, Q2, InverseA, true);
        QuatSquadControl(ControlB, Q1, Q2, Q3, InverseB, false);
        QuatSquad(Out, Q1, ControlA, ControlB, Q2, &U);
        return true;
    }

    struct FSplineCoreStorage
    {
        // Construction/unwind order: Points, Quats, Times, sample rows.
        std::vector<FVec3> Points;
        std::vector<FQuat> Quats;
        std::vector<float> Times;
        std::vector<std::vector<float>> SampleRows;
    };

    // Three real bases, as retail: +0 ICRSpline, +4 IMotionPath, +8 IPersistString (the
    // {IID, offset} table .rdata:06f5acd4 answers offsets 0 / 4 / 8). Until 2026-10-16 the
    // two facets were hand-filled `void*[]` tables with `this - 4` / `this - 8` adjustors, so
    // callers holding an IMotionPath* / IPersistString* (thorn_iface.h) reached an object
    // that did not derive from either.
    class FSplinePath final : public ICRSpline, public IMotionPath, public IPersistString
    {
    public:
        FSplinePath(bool bOrientation)
            : Outer_(nullptr),
              Inner_(this, &FSplinePath::InnerQueryInterface, &FSplinePath::InnerDestroy),
              Points_(nullptr), Times_(nullptr),
              Quats_(nullptr), LiveCount_(0), PointCount_(0), FirstLive_(1), LastLive_(0),
              SampleTableRows_(0), TotalLength_(0.0f), bOrientation_(bOrientation),
              bOrientationReady_(false), bClosed_(false), bInnerAllocated_(false)
        {
            // LAB_06f37400: [obj+0x70] = &obj->inner until the factory
            // installs Desc->Outer (see Aggregate()).
            Outer_ = &Inner_;
        }

        // FUN_06f34ff0 / FUN_06f34850 (~CV_CROrientationSplinePath /
        // ~CV_CRSplinePath): reset the vtables, then -- if the inner spline
        // core at +0xc exists -- run FUN_06f34890 on it (the core's own
        // member-wise teardown: per-segment FUN_06f23610 vector<float> frees
        // over the +0x48 sample-table array, then the +0x38 / +0x28 / +0x18
        // bulk frees), free the core and zero +0xc.  The port folds the core
        // into this object, so FreePath() is that whole sequence.
        ~FSplinePath() { FreePath(); }

        // LAB_06f37400 aggregation step: the controlling unknown is the
        // caller's Outer when aggregated, else the embedded inner unknown.
        // Returns the inner unknown (what retail writes to *Desc->InnerOut).
        IDAComponent* Aggregate(IDAComponent* Outer)
        {
            Outer_ = (Outer != nullptr) ? Outer : static_cast<IDAComponent*>(&Inner_);
            return &Inner_;
        }

        // FUN_06f378d0 / FUN_06f378e0 / FUN_06f378f0 (PTR_FUN_06f5ab88 slots
        // 0-2, also reached from the IMotionPath / IPersistString facets via
        // LAB_06f37a90.. / LAB_06f37ac0.. after the this-adjust): pure
        // forwards to the controlling unknown at [this+0x70].
        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
        {
            return Outer_->QueryInterface(Iid, Out);
        }

        unsigned long DACOM_API AddRef() override { return Outer_->AddRef(); }

        unsigned long DACOM_API Release() override { return Outer_->Release(); }

        // LAB_06f377c0 (thorn.dll.txt:46136-46227), the embedded inner
        // unknown's NON-delegating QueryInterface: walks the {IID, facet
        // offset} table at .rdata:06f5acd4 ({"1.11_ICRSpline",0},
        // {"1.11_IMotionPath",4}, {"1.11_IPersistString",8}) with a plain
        // byte-for-byte CMP loop -- no case-folding instruction anywhere in
        // it, so strcmp, not ThornNameEqual.  On a hit it computes
        // Owner+offset, calls that facet's OWN vtbl+4 (i.e. the delegating
        // AddRef above, so an aggregated outer sees the AddRef), stores the
        // facet and returns 0; otherwise *Out = 0 and -3.
        GENRESULT NonDelegatingQueryInterface(const char* Iid, void** Out)
        {
            void* SelectedInterface;
            if (std::strcmp(Iid, Iids::CRSpline) == 0)
                SelectedInterface = static_cast<ICRSpline*>(this);
            else if (std::strcmp(Iid, Iids::MotionPath) == 0)
                SelectedInterface = static_cast<IMotionPath*>(this);
            else if (std::strcmp(Iid, Iids::PersistString) == 0)
                SelectedInterface = static_cast<IPersistString*>(this);
            else
            {
                *Out = nullptr;
                return GR_INTERFACE_UNSUPPORTED;
            }

            // The selected facet is retained across the delegating AddRef.
            AddRef();
            *Out = SelectedInterface;
            return GR_OK;
        }

        static GENRESULT InnerQueryInterface(void* Owner, const char* Iid, void** Out)
        {
            return static_cast<FSplinePath*>(Owner)->NonDelegatingQueryInterface(Iid, Out);
        }

        // LAB_06f37890 at zero: FUN_06f34850 (dtor) + FUN_06f57e26 (free).
        static void InnerDestroy(void* Owner) { delete static_cast<FSplinePath*>(Owner); }

        // FUN_06f34950: outer wrapper rejects a missing inner object or an
        // index >= LastLive (FUN_06f224f0), then forwards to FUN_06f22880.
        GENRESULT DACOM_API Slot3(unsigned int Index, float* OutPoint) override
        {
            if (!bInnerAllocated_ || Index >= static_cast<unsigned int>(LastLive_))
                return static_cast<GENRESULT>(-1);
            return SetPointInternal(static_cast<int>(Index), OutPoint);
        }

        GENRESULT DACOM_API Slot4LoadPoints(const float* Pts, unsigned long Count) override
        {
            return LoadPoints(Pts, static_cast<int>(Count));
        }

        // FUN_06f22880: update one live point, repair the endpoint ghosts, and
        // rebuild the arc-length samples. The public wrapper validates the
        // live-point index before reaching this mutator.
        GENRESULT SetPointInternal(int Index, const float* Point)
        {
            // Retail +0x10 is the complete allocated point count (N+3).
            // Slot3 already restricts Index < LastLive, so Index+1 is valid.
            if (static_cast<unsigned int>(Index) >= static_cast<unsigned int>(PointCount_))
                return static_cast<GENRESULT>(-2);
            int StorageIndex = static_cast<int>(static_cast<unsigned int>(Index) + 1u);
            // Retail redirects the closing duplicate itself to FirstLive;
            // the final real point at LastLive-1 remains a separate record.
            if (bClosed_ && StorageIndex == LastLive_)
                StorageIndex = FirstLive_;

            std::memcpy(&Points_[StorageIndex].X, Point, sizeof(float));
            std::memcpy(&Points_[StorageIndex].Y, Point + 1, sizeof(float));
            std::memcpy(&Points_[StorageIndex].Z, Point + 2, sizeof(float));

            if (!bClosed_)
            {
                Points_[0] = Points_[1];
                Points_[LastLive_ + 1] = Points_[LastLive_];
            }
            else
            {
                Points_[LastLive_] = Points_[FirstLive_];
                Points_[LastLive_ + 1] = Points_[FirstLive_ + 1];
                Points_[0] = Points_[LastLive_ - 1];
            }

            RebuildSampleTables();
            return GR_OK;
        }

        // LAB_06f34930 -> FUN_06f224f0: return inner+0x58 (LastLive).
        // FUN_06f22500 sets it to N+1 for a closed path, N for an open path.
        unsigned long DACOM_API Slot5() override
        {
            if (!bInnerAllocated_)
                return 0;
            return static_cast<unsigned long>(LastLive_);
        }

        // FUN_06f34990 / FUN_06f349d0: wrapper-level failure is -1; the
        // inner accessors use -2 only after a valid wrapper dispatch.
        GENRESULT DACOM_API Slot6(unsigned int Index, float* OutPoint) override
        {
            if (!bInnerAllocated_ || Index > static_cast<unsigned int>(LastLive_))
                return static_cast<GENRESULT>(-1);
            return GetPointRetail(Index, OutPoint);
        }

        GENRESULT DACOM_API Slot7(unsigned int Index, float* OutValue) override
        {
            if (!bInnerAllocated_ || Index > static_cast<unsigned int>(LastLive_))
                return static_cast<GENRESULT>(-1);
            return GetTimeRetail(Index, OutValue);
        }
        // LAB_06f347a0: forward the requested open/closed byte to the inner
        // path, repair its ghost points and rebuild normalized arc lengths.
        GENRESULT DACOM_API Slot8(unsigned char bClosed) override
        {
            bClosed_ = bClosed;
            ApplyPersistGhosts();
            RebuildSampleTables();
            return GR_OK;
        }
        // Retail LAB_06f347c0 forwards to FUN_06f225f0, which returns the
        // closed-state byte (not a GENRESULT success code).
        GENRESULT DACOM_API Slot9() override
        {
            return static_cast<GENRESULT>(bClosed_);
        }
        // 351e0: missing inner / unsigned Index >= LastLive -> -1;
        // otherwise return the actual orientation setter result.
        // Position-only slot10 (347d0) returns -7 without reading inputs.
        GENRESULT DACOM_API Slot10(unsigned int Index, float* Orientation) override
        {
            if (!bOrientation_)
                return static_cast<GENRESULT>(-7);
            if (!bInnerAllocated_ || Index >= static_cast<unsigned int>(LastLive_))
                return static_cast<GENRESULT>(-1);
            return SetOrientationInternal(Index, Orientation);
        }
        // 35220: require an inner and Count == its LastLive snapshot.
        // Each four-word source record is forwarded; ignore setter returns.
        // Position-only slot11 (347e0) returns -7 without reading inputs.
        GENRESULT DACOM_API Slot11(unsigned int Count, float* Records) override
        {
            if (!bOrientation_)
                return static_cast<GENRESULT>(-7);
            if (!bInnerAllocated_ || Count != static_cast<unsigned int>(LastLive_))
                return static_cast<GENRESULT>(-1);
            for (unsigned int Index = 0; Index < Count; ++Index)
                SetOrientationInternal(Index, Records + Index * 4);
            return GR_OK;
        }
        // 353d0: missing inner / unsigned Index > LastLive fail; delegate raw getter.
        // Position-only slot12 (34810) returns -7 independently.
        GENRESULT DACOM_API Slot12(unsigned int Index, float* OutOrientation) override
        {
            if (!bOrientation_)
                return static_cast<GENRESULT>(-7);
            if (!bInnerAllocated_ || Index > static_cast<unsigned int>(LastLive_))
                return static_cast<GENRESULT>(-1);
            return GetOrientationRetail(Index, OutOrientation);
        }
        // Retail LAB_06f347f0 (the POSITION-ONLY "CV_CRSplinePath" class's
        // own Slot13, vtable head PTR_LAB_06f5aa78) unconditionally returns
        // 0xfffffff9 (-7); matched here via the !bOrientation_ branch below.
        // The ORIENTATION variant ("CV_CROrientationSplinePath", vtable head
        // PTR_LAB_06f5aaf0) has a COMPLETELY DIFFERENT, real implementation
        // at LAB_06f35270 (listing 41574-41623, RET 0xc = this + 2 stack
        // args). FULLY RETRACED 2026-09-10 (this pass, instruction-by-
        // instruction, resolving the two follow-up indirect calls an
        // earlier pass left open): signature is (Index, MatrixRecords),
        // where MatrixRecords points to an array of 0x24-byte (9-float/3x3
        // rotation matrix) records with that fixed stride. It lazy-inits the
        // shared I3DMathEngine singleton (DAT_06f61e18, same pattern as
        // ThornAcquire3DMathEngine) and converts MatrixRecords[0] (the
        // record AT MatrixRecords, no offset) to a quaternion via the
        // engine's slot+0x50 (ThornMatrixToQuat). It then steps to
        // MatrixRecords+0x24 (record index 1, i.e. MatrixRecords+9 floats)
        // and reads ONLY that record's first 3 floats as a position vector
        // -- the remaining 6 floats of record 1 are never read. It calls
        // this object's own vtable+0xc (Slot3, the position setter) with
        // (Index, &Position); if that fails, retail returns -1.
        // Only on success does it call this object's own vtable+0x28
        // (Slot10 -- confirmed above to be the per-index ORIENTATION
        // setter) with (Index, &Quat) to store the converted quaternion,
        // and returns Slot10's result unchanged (352d0..352d8).
        // No loop: one call
        // processes exactly one Index using exactly 2 consecutive
        // MatrixRecords entries.
        GENRESULT DACOM_API Slot13(unsigned int Index, const float* MatrixRecords) override
        {
            if (!bOrientation_)
                return static_cast<GENRESULT>(-7);
            I3DMathEngine* Math = AcquireSplineMathEngine();
            FMatrix MatrixInput;
            FQuaternion QuaternionResult;
            std::memcpy(&MatrixInput, MatrixRecords, sizeof(MatrixInput));
            Math->MatrixToQuaternion(QuaternionResult, MatrixInput);
            FQuat Quat;
            std::memcpy(&Quat, &QuaternionResult, sizeof(Quat));

            const float* NextRecord = MatrixRecords + 9;
            float Position[3];
            std::memcpy(Position, NextRecord, sizeof(Position));
            GENRESULT PositionResult = Slot3(Index, Position);
            if (PositionResult != GR_OK)
                return static_cast<GENRESULT>(-1);

            return Slot10(Index, &Quat.W);
        }
        // 352f0: raw position snapshot precedes math acquisition; the foreign
        // converter writes directly into the allocated quaternion buffer.
        // Dispatch both bulk setters, ignore results, then free in order.
        // Position-only variant 34800 returns -7 independently.
        GENRESULT DACOM_API Slot14(unsigned int Count, const float* TransformRecords) override
        {
            if (!bOrientation_)
                return static_cast<GENRESULT>(-7);
            if (!bInnerAllocated_)
                return static_cast<GENRESULT>(-1);

            float* PositionBuffer = new float[Count * 3];
            float* QuatBuffer = new float[Count * 4];
            for (unsigned int Index = 0; Index < Count; ++Index)
            {
                const float* Record = TransformRecords + Index * 12;
                std::memcpy(PositionBuffer + Index * 3, Record + 9, 3 * sizeof(float));

                I3DMathEngine* Math = AcquireSplineMathEngine();
                FMatrix MatrixInput;
                FQuaternion QuaternionResult;
                std::memcpy(&MatrixInput, Record, sizeof(MatrixInput));
                Math->MatrixToQuaternion(QuaternionResult, MatrixInput);
                std::memcpy(QuatBuffer + Index * 4, &QuaternionResult, sizeof(QuaternionResult));
            }

            Slot4LoadPoints(PositionBuffer, Count);
            Slot11(Count, QuatBuffer);

            delete[] PositionBuffer;
            delete[] QuatBuffer;
            return GR_OK;
        }
        // Position-only variant: LAB_06f34820 (.rdata 06f5aab4 / 06f5abc4)
        // unconditionally returns 0xfffffff9 (-7) -- the !bOrientation_
        // branch below.
        // Orientation variant: LAB_06f35410 (listing 41776-41827, .rdata
        // 06f5ab2c / 06f5acc4 = slot 15 of both orientation ICRSpline
        // tables; RET 0xc = (this, Index, OutTransform)).  Fully traced
        // 2026-09-10: missing inner core -> -1; FUN_06f224f0(inner) < Index
        // (JC, so Index == count is accepted, exactly like Slot12) -> -1;
        // FUN_06f22b70(inner, Index, quat4) reads Quats_[Index+1]
        // (16-byte stride, -8 when !orientation, -2 when Index+1 is past
        // [inner+0x10] = PointCount_) and FUN_06f229b0(inner, Index, vec3)
        // reads Points_[Index+1] (12-byte stride, same -2 bound) -- both
        // return values are DISCARDED; the vec3 is stored at OutTransform
        // +0x24 (floats 9..11), the shared I3DMathEngine (DAT_06f61e18,
        // lazily created via FUN_06f57de0) converts the quat through
        // vtbl+0x54 (ThornQuatToMatrix) into a 9-float scratch that is then
        // REP MOVSD'd into OutTransform[0..8].  The common exit is
        // `OR EAX,-1`, so the function returns -1 even after writing the
        // record.  This is the exact inverse of Slot13's input record.
        GENRESULT DACOM_API Slot15(unsigned int Index, float* OutTransform) override
        {
            if (!bOrientation_)
                return static_cast<GENRESULT>(-7);
            if (!bInnerAllocated_ || static_cast<unsigned int>(LastLive_) < Index)
                return static_cast<GENRESULT>(-1);
            FQuat Quat;
            FVec3 Position;
            // 35436/35444 discard both getter results and consume their outputs.
            GetOrientationRetail(Index, &Quat.W);
            GetPointRetail(Index, &Position.X);
            std::memcpy(OutTransform + 9, &Position, sizeof(Position));

            I3DMathEngine* Math = AcquireSplineMathEngine();
            FQuaternion QuaternionInput;
            FMatrix MatrixResult;
            std::memcpy(&QuaternionInput, &Quat, sizeof(QuaternionInput));
            Math->QuaternionToMatrix(MatrixResult, QuaternionInput);
            std::memcpy(OutTransform, &MatrixResult, sizeof(MatrixResult));
            return static_cast<GENRESULT>(-1);
        }

        // LAB_06f34670 (IMotionPath slot 7) -> FUN_06f224e0: `FLD [inner+4]`.
        // inner+0x4 is the un-normalised total arc length: FUN_06f22630
        // zeroes it, accumulates every segment's sampled chord length into
        // it (`FADD [ESI+4]; FST [ESI+4]`), and FUN_06f22600 then divides
        // the +0x38 time table by it -- i.e. RebuildSampleTables' `Total`.
        GENRESULT PathLength(float* OutLength) const
        {
            if (!bInnerAllocated_)
                return static_cast<GENRESULT>(-1);
            *OutLength = GetCoreLength();
            return GR_OK;
        }

        float GetCoreLength() const
        {
            return TotalLength_;
        }

        // Retail FUN_06f229b0/FUN_06f229f0/FUN_06f22b70 are the raw spline
        // accessors used by the path vtable.  Keep their bounds and error
        // values here even though the public interface uses higher-level
        // evaluation methods.
        GENRESULT GetPointRetail(unsigned int Index, float* Out) const
        {
            if (Index >= static_cast<unsigned int>(PointCount_))
                return static_cast<GENRESULT>(-2);
            const FVec3& Point = Points_[Index + 1];
            // 229c3..229d4 copies one DWORD at a time, preserving overlap.
            std::memcpy(Out, &Point.X, sizeof(float));
            std::memcpy(Out + 1, &Point.Y, sizeof(float));
            std::memcpy(Out + 2, &Point.Z, sizeof(float));
            return GR_OK;
        }

        GENRESULT GetTimeRetail(unsigned int Index, float* Out) const
        {
            if (Index >= static_cast<unsigned int>(PointCount_))
                return static_cast<GENRESULT>(-2);
            std::memcpy(Out, Times_ + Index + 1, sizeof(float));
            return GR_OK;
        }

        GENRESULT GetOrientationRetail(unsigned int Index, float* Out) const
        {
            if (!bOrientationReady_)
                return static_cast<GENRESULT>(-8);
            if (Index >= static_cast<unsigned int>(PointCount_))
                return static_cast<GENRESULT>(-2);
            const FQuat& Quaternion = Quats_[Index + 1];
            std::memcpy(Out, &Quaternion.W, sizeof(float));
            std::memcpy(Out + 1, &Quaternion.X, sizeof(float));
            std::memcpy(Out + 2, &Quaternion.Y, sizeof(float));
            std::memcpy(Out + 3, &Quaternion.Z, sizeof(float));
            return GR_OK;
        }

        // Retail has TWO Slot4LoadPoints implementations: the position-only
        // variant's (not separately addressed here) and the orientation
        // variant's own FUN_06f350d0 (confirmed via vtable slot-index
        // arithmetic: XREF .rdata:06f5ab00 == orientation ICRSpline vtable
        // head 06f5aaf0 + 0x10 == slot 4). FUN_06f350d0's body (listing
        // 41379-41477) frees the SAME four raw [ptr,end,cap] blocks at
        // +0x48/+0x38/+0x28/+0x18 as FThornScene's teardown (this class's
        // own FreePath() equivalent), then operator-news and constructs a
        // fresh 0x60-byte inner spline-core object from the caller's
        // (Pts, Count) via FUN_06f22d30, followed by a FUN_06f22ca0
        // call on it -- matching this method's FreePath() + AllocStorage()
        // + point copy + ApplyCtorGhosts() + FillIdentityQuats() sequence
        // for normal nonempty input, with a separately allocated modern core.
        // Empty/one-point producer paths still differ (tracker 22ec0/233b0).
        GENRESULT LoadPoints(const float* Pts, int Count)
        {
            FreePath();
            CoreStorage_ = std::make_unique<FSplineCoreStorage>();
            struct FConstructionCleanup
            {
                FSplinePath* Owner;
                bool bComplete;
                ~FConstructionCleanup()
                {
                    if (!bComplete)
                        Owner->FreePath();
                }
            } Cleanup = {this, false};
            bClosed_ = false;
            const unsigned int UnsignedCount = static_cast<unsigned int>(Count);
            if (UnsignedCount != 0)
            {
                LiveCount_ = Count;
                FirstLive_ = 1;
                LastLive_ = Count;
                PointCount_ = static_cast<int>(UnsignedCount + 3u);
                AllocStorage();
                for (unsigned int Index = 0; Index < UnsignedCount; ++Index)
                    std::memcpy(Points_ + FirstLive_ + Index, Pts + Index * 3u, sizeof(FVec3));
                ApplyCtorGhosts();
                RebuildSampleTables();
            }
            // The outer +0xc pointer is published only after construction.
            bInnerAllocated_ = true;
            Cleanup.bComplete = true;
            if (bOrientation_)
                FillIdentityQuats();
            return GR_OK;
        }

        bool HasInnerSpline() const { return bInnerAllocated_; }

        GENRESULT EvaluatePosition(const float* T, float* OutVec3) const
        {
            EvalPosition(T, OutVec3);
            return GR_OK;
        }

        // LAB_06f34640: inner slot 0 is the tangent evaluator, not the
        // composite transform slot. Both core vtables dispatch three words.
        GENRESULT EvaluateLookAt(const float* T, float* OutVec3) const
        {
            if (!bInnerAllocated_)
                return static_cast<GENRESULT>(-1);
            EvalTangent(T, OutVec3);
            return GR_OK;
        }

        GENRESULT EvaluateTransform(const float* T, float* OutXform)
        {
            if (!bInnerAllocated_)
                return static_cast<GENRESULT>(-1);
            FVec3 Position;
            if (bOrientation_)
            {
                EvalPosition(T, &Position.X);
                std::memcpy(OutXform + 9, &Position, sizeof(Position));
                FQuat Orientation;
                EvalOrientation(T, &Orientation.W);
                I3DMathEngine* Math = AcquireSplineMathEngine();
                FQuaternion QuaternionInput;
                std::memcpy(&QuaternionInput, &Orientation, sizeof(QuaternionInput));
                FMatrix MatrixResult;
                Math->QuaternionToMatrix(MatrixResult, QuaternionInput);
                std::memcpy(OutXform, &MatrixResult, sizeof(MatrixResult));
                return GR_OK;
            }

            // Allocated valid rows cannot fail these evaluations; the guards
            // cover only retail's missing-row/no-output storage domains.
            if (!EvalPosition(T, &Position.X))
                return GR_GENERIC;
            std::memcpy(OutXform + 9, &Position, sizeof(Position));
            FVec3 Tangent;
            if (!EvalTangent(T, &Tangent.X))
                return GR_GENERIC;
            BuildLookAtFromTangent(Tangent, OutXform);
            return GR_OK;
        }

        // LAB_06f34420 (PTR_LAB_06f5ab68) / LAB_06f35530 (PTR_LAB_06f5aad0). Out = quat WXYZ.
        GENRESULT EvaluateSlot5(const float* T, float* Out)
        {
            // Both concrete slot-5 bodies test the logical inner owner first.
            if (!bInnerAllocated_)
                return static_cast<GENRESULT>(-1);
            if (bOrientation_)
            {
                // 35530 forwards the actual caller output and discards GR.
                EvalOrientation(T, Out);
                return GR_OK;
            }

            FVec3 Tangent;
            // A failed inner path has no defined retail tangent storage:
            // empty/open-one cores or segment zero read outside its row
            // or neighbor array. Preserve the defined output path below.
            if (!EvalTangent(T, &Tangent.X))
                return static_cast<GENRESULT>(-1);
            float Mat9[9];
            BuildSlot5MatrixRetail(Tangent, Mat9);
            I3DMathEngine* Math = AcquireSplineMathEngine();
            FMatrix MatrixInput;
            std::memcpy(&MatrixInput, Mat9, sizeof(MatrixInput));
            FQuaternion QuaternionResult;
            Math->MatrixToQuaternion(QuaternionResult, MatrixInput);
            std::memcpy(Out, &QuaternionResult, sizeof(QuaternionResult));
            return GR_OK;
        }

        unsigned long PersistEstimateSize() const
        {
            const unsigned long PositionCount = bInnerAllocated_
                ? static_cast<unsigned long>(LastLive_) : 0ul;
            if (bOrientation_)
            {
                const unsigned long OrientationCount = bInnerAllocated_
                    ? static_cast<unsigned long>(LastLive_) : 0ul;
                return PositionCount * G_PersistOrientVec3Bytes
                    + OrientationCount * G_PersistQuatBytes + G_PersistPrefixBytes;
            }
            return PositionCount * G_PersistVec3Bytes + G_PersistPrefixBytes;
        }

        GENRESULT LoadPersist(const char* Text)
        {
            if (Text == nullptr)
                return GR_GENERIC;

            FPersistPointTree Tree;
            ThornPersistTreeInit(Tree);
            std::optional<std::vector<FQuat>> ParsedQuats;
            if (bOrientation_)
                ParsedQuats.emplace();

            // Both retail owners precede the six-byte CLOSED comparison.
            const bool bClosed =
                (strncmp(Text, G_PersistClosed, sizeof(G_PersistClosed) - 1) == 0);
            unsigned int PointIndex = 0;
            bool bReadQuaternions = bOrientation_;
            bool bMissingQuaternion = false;
            const char* Cursor = Text;
            while (Cursor != nullptr)
            {
                FVec3 Point;
                if (!ParsePersistFloats(&Cursor, &Point.X, 3))
                {
                    // The parser's cursor controls retry, including a failed
                    // later number which leaves that cursor nonnull.
                    continue;
                }
                FVec3* StoredPoint = nullptr;
                ThornPersistTreeInsert(Tree, PointIndex, Point, StoredPoint);
                std::memcpy(StoredPoint, &Point, sizeof(Point));
                if (bReadQuaternions)
                {
                    const char* SavedCursor = Cursor;
                    FQuat Quat;
                    if (ParsePersistFloats(&Cursor, &Quat.W, 4))
                        ParsedQuats->push_back(Quat);
                    else
                    {
                        Cursor = SavedCursor;
                        bReadQuaternions = false;
                        bMissingQuaternion = true;
                    }
                }
                PointIndex += 1;
            }

            // Retail: if (count <= 1) skip slot 4 / FUN_06f22500, return -1.
            if (PointIndex <= 1)
                return GR_GENERIC;

            const unsigned int Count = PointIndex;
            float* Packed = new float[Count * 3];
            float* PackedQuats = nullptr;
            if (bOrientation_)
                PackedQuats = new float[Count * 4];

            // FUN_06f35cc0 + repeated FUN_06f35e40 lookups: enumerate the
            // ordered tree by keys 0..count-1, matching retail's packed array.
            FVec3* FirstPoint = nullptr;
            ThornPersistTreeBegin(Tree, FirstPoint);
            unsigned int Written = 0;
            for (std::map<unsigned int, FVec3>::const_iterator It = Tree.Entries.begin();
                 It != Tree.Entries.end(); ++It)
            {
                std::memcpy(Packed + Written * 3, &It->second, sizeof(FVec3));
                if (bOrientation_)
                {
                    if (bMissingQuaternion)
                    {
                        const unsigned int IdentityWords[4] = {0x3f800000u, 0u, 0u, 0u};
                        std::memcpy(PackedQuats + Written * 4, IdentityWords, sizeof(IdentityWords));
                    }
                    else
                        std::memcpy(PackedQuats + Written * 4, &(*ParsedQuats)[Written], sizeof(FQuat));
                }
                Written++;
            }

            LoadPoints(Packed, static_cast<int>(Written));
            if (bOrientation_)
                Slot11(Written, PackedQuats);
            bClosed_ = bClosed;
            ApplyPersistGhosts();
            RebuildSampleTables();
            if (bOrientation_)
            {
                delete[] Packed;
                delete[] PackedQuats;
            }
            // Position34dc5/34e3e allocates/copies private XYZ input. Neither
            // normal tree frees34eec/34f06 nor tree EH frees that allocation;
            // its lifetime is retained here, with our modern allocator.
            return GR_OK;
        }

        GENRESULT SavePersist(char* Buffer, unsigned long Size)
        {
            // FUN_06f34bc0: ICRSpline slot 5 = LastLive, then persist slot 5 size check.
            const unsigned long SavedCount = Slot5();
            const unsigned long RequiredSize = PersistEstimateSize();
            if (SavedCount == 0 || RequiredSize > Size)
                return GR_GENERIC;

            const bool bClosedToken = bClosed_;
            const int Used =
                sprintf(Buffer, G_PersistFlagFmt, bClosedToken ? G_PersistClosed : G_PersistOpen);

            unsigned long Offset = static_cast<unsigned long>(Used);

            // FUN_06f22500(inner, 0) Р В Р вЂ Р В РІР‚С™Р Р†Р вЂљРЎСљ force OPEN ghosts, then emit SavedCount points via slot 6.
            bClosed_ = false;
            ApplyPersistGhosts();
            RebuildSampleTables();

            unsigned int Written = 0;
            while (Written < SavedCount)
            {
                FVec3 Point;
                GetPointRetail(Written, &Point.X);
                volatile double PointZ = static_cast<double>(Point.Z);
                volatile double PointY = static_cast<double>(Point.Y);
                volatile double PointX = static_cast<double>(Point.X);
                const int VecWrote =
                    sprintf(Buffer + Offset, G_PersistVec3Fmt, PointX, PointY, PointZ);

                Offset += static_cast<unsigned long>(VecWrote);

                if (bOrientation_)
                {
                    FQuat Quat;
                    GetOrientationRetail(Written, &Quat.W);
                    volatile double QuatZ = static_cast<double>(Quat.Z);
                    volatile double QuatY = static_cast<double>(Quat.Y);
                    volatile double QuatX = static_cast<double>(Quat.X);
                    volatile double QuatW = static_cast<double>(Quat.W);
                    const int QuatWrote =
                        sprintf(Buffer + Offset, G_PersistQuatFmt, QuatW, QuatX, QuatY, QuatZ);

                    Offset += static_cast<unsigned long>(QuatWrote);
                }

                if (Offset >= Size)
                    break;
                Written += 1;
            }
            if (Written != SavedCount)
                return GR_GENERIC;
            return GR_OK;
        }

        // IMotionPath facet (+4; final table .rdata:06f5ab68 position / 06f5ac68 orientation).
        // QueryInterface / AddRef / Release are the overrides above (LAB_06f37a90..06f37ab0
        // and LAB_06f37af0..06f37b10: this -= 4, then the object's own slot). No facet slot
        // tests its `this`; the inner-core tests live in the helpers they call.
        // Slot 3, LAB_06f34180 (position) / LAB_06f354a0 (orientation).
        GENRESULT DACOM_API Slot3Transform(float T, float* OutXform) override
        {
            return EvaluateTransform(&T, OutXform);
        }

        // Slot 4, LAB_06f343f0: 343f0 checks the inner, not the still-live public facade.
        GENRESULT DACOM_API Slot4Evaluate(float T, float* OutVec3) override
        {
            if (!HasInnerSpline())
                return static_cast<GENRESULT>(-1);
            EvaluatePosition(&T, OutVec3);
            return GR_OK;
        }

        // Slot 5: LAB_06f34420 (position) / LAB_06f35530 (orientation). 34420 owns the
        // position-mode return; 35530 forces success only after its inner exists.
        // EvaluateSlot5 preserves both contracts.
        GENRESULT DACOM_API Slot5(float T, float* Out) override
        {
            return EvaluateSlot5(&T, Out);
        }

        // Slot 6, LAB_06f34640.
        GENRESULT DACOM_API Slot6LookAt(float T, float* OutVec3) override
        {
            return EvaluateLookAt(&T, OutVec3);
        }

        // Slot 7, LAB_06f34670 (retail motion vtable slot 7, .rdata:06f5aa74 / 06f5ab84 /
        // 06f5ac84, listing 40164-40176; RET 0x8 = MotionThis + float* Out):
        // `MOV ECX,[MotionThis+8]` is the inner spline core at object+0xc (the motion facet
        // sits at object+4); when it is NULL -> -1, otherwise FUN_06f224e0
        // (`FLD [inner+4]; RET`) is FSTP'd to *Out and 0 is returned. inner+0x4 is the
        // total un-normalised arc length that FUN_06f22630 accumulates -- see PathLength.
        GENRESULT DACOM_API Slot7Length(float* OutLength) override
        {
            return PathLength(OutLength);
        }

        // IPersistString facet (+8; final tables .rdata:06f5ab50 position / 06f5ac50
        // orientation). QI / AddRef / Release: LAB_06f37ac0..06f37ae0 (this -= 8, then the
        // object's own slot).
        // Slot 3, FUN_06f34cc0 (position).
        GENRESULT DACOM_API Slot3Load(const char* Text) override { return LoadPersist(Text); }
        // Slot 4, FUN_06f34bc0 (position) / FUN_06f35580 (orientation).
        GENRESULT DACOM_API Slot4Save(char* Buffer, unsigned long Size) override
        {
            return SavePersist(Buffer, Size);
        }
        // Slot 5, LAB_06f34ca0 (position: this -= 8, ICRSpline slot 5 count * 0x24 + 10,
        // RET 4, no test) / LAB_06f356c0 (orientation).
        unsigned long DACOM_API Slot5EstimateSize() override { return PersistEstimateSize(); }

    private:
        // The real core owns Points, Quats, Times and individual sample rows.
        // Both normal destruction and constructor unwind release rows first,
        // then Times, Quats and Points. No contiguous replacement row block.
        void FreePath()
        {
            if (CoreStorage_ != nullptr)
            {
                FSplineCoreStorage& Storage = *CoreStorage_;
                // Each row is destroyed in forward order before its container.
                for (std::size_t Index = 0; Index < Storage.SampleRows.size(); ++Index)
                    std::vector<float>().swap(Storage.SampleRows[Index]);
                std::vector<std::vector<float>>().swap(Storage.SampleRows);
                std::vector<float>().swap(Storage.Times);
                std::vector<FQuat>().swap(Storage.Quats);
                std::vector<FVec3>().swap(Storage.Points);
                CoreStorage_.reset();
            }
            Points_ = nullptr;
            Times_ = nullptr;
            Quats_ = nullptr;
            LiveCount_ = 0;
            PointCount_ = 0;
            LastLive_ = 0;
            SampleTableRows_ = 0;
            TotalLength_ = 0.0f;
            bInnerAllocated_ = false;
            bOrientationReady_ = false;
        }

        void AllocStorage()
        {
            FSplineCoreStorage& Storage = *CoreStorage_;
            const unsigned int Size = static_cast<unsigned int>(PointCount_);
            Storage.Points.resize(Size);
            Points_ = Storage.Points.data();
            Storage.Times.resize(Size);
            Times_ = Storage.Times.data();
            Storage.SampleRows.resize(Size);
            SampleTableRows_ = LastLive_;
            Storage.Quats.resize(Size);
            Quats_ = Storage.Quats.data();
        }

        void FillIdentityQuats()
        {
            // 22ca0 snapshots LastLive once and invokes the ordinary setter.
            const unsigned int Last = static_cast<unsigned int>(LastLive_);
            if (Last == 0)
                return;
            bOrientationReady_ = true;
            const unsigned int IdentityWords[4] = {0x3f800000u, 0u, 0u, 0u};
            FQuat Identity;
            std::memcpy(&Identity, IdentityWords, sizeof(Identity));
            for (unsigned int Index = 0; Index < Last; ++Index)
                SetOrientationInternal(Index, &Identity.W);
        }

        // Copy each DWORD before reading the next, as 22a20's MOV pairs do.
        // This also preserves partial overlap and signaling-NaN payload bits.
        static void CopyQuaternionWords(FQuat* Destination, const float* Source)
        {
            std::memcpy(&Destination->W, Source, sizeof(float));
            std::memcpy(&Destination->X, Source + 1, sizeof(float));
            std::memcpy(&Destination->Y, Source + 2, sizeof(float));
            std::memcpy(&Destination->Z, Source + 3, sizeof(float));
        }

        // FUN_06f22a20: indexed orientation write followed by endpoint ghosts.
        GENRESULT SetOrientationInternal(unsigned int Index, const float* Orientation)
        {
            if (!bOrientationReady_)
                return static_cast<GENRESULT>(-8);
            if (Index >= static_cast<unsigned int>(PointCount_))
                return static_cast<GENRESULT>(-1);

            unsigned int StorageIndex = Index + 1;
            if (bClosed_)
            {
                if (StorageIndex == static_cast<unsigned int>(LastLive_))
                    StorageIndex = static_cast<unsigned int>(FirstLive_);
                CopyQuaternionWords(Quats_ + StorageIndex, Orientation);
                CopyQuaternionWords(Quats_ + LastLive_, &Quats_[FirstLive_].W);
                CopyQuaternionWords(Quats_ + LastLive_ + 1, &Quats_[FirstLive_ + 1].W);
                CopyQuaternionWords(Quats_, &Quats_[LastLive_ - 1].W);
            }
            else
            {
                CopyQuaternionWords(Quats_ + StorageIndex, Orientation);
                CopyQuaternionWords(Quats_, &Quats_[1].W);
                CopyQuaternionWords(Quats_ + LastLive_ + 1, &Quats_[LastLive_].W);
            }
            return GR_OK;
        }

        // 21f45/21f5b: snapshot differences before publishing the ghost vector.
        static void SubtractGhostVector(FVec3& OutputVector, const FVec3& A, const FVec3& B)
        {
            // Explicit pointer operands avoid compiler-specific reference dereferencing.
            auto* const OutputVectorAsmPtr = &OutputVector;
            auto* const AAsmPtr = &A;
            auto* const BAsmPtr = &B;
            __asm
            {
                mov eax, AAsmPtr
                mov ecx, BAsmPtr
                mov edx, OutputVectorAsmPtr
                fld dword ptr [eax + 8]
                fsub dword ptr [ecx + 8]
                fld dword ptr [eax + 4]
                fsub dword ptr [ecx + 4]
                fld dword ptr [eax]
                fsub dword ptr [ecx]
                fstp dword ptr [edx]
                fstp dword ptr [edx + 4]
                fstp dword ptr [edx + 8]
            }
        }

        // OPEN construction uses CR at 0.1/0.9, with different endpoint staging.
        void ApplyCtorGhosts()
        {
            LastLive_ = LiveCount_;
            CatmullRomVec3(Points_[1], Points_[1], Points_[2], Points_[2], 0.1f, Points_[0]);
            FVec3 Difference;
            SubtractGhostVector(Difference, Points_[0], Points_[1]);
            FVec3 StartGhost;
            SubtractGhostVector(StartGhost, Points_[1], Difference);
            std::memcpy(&Points_[0].X, &StartGhost.X, sizeof(float));
            std::memcpy(&Points_[0].Y, &StartGhost.Y, sizeof(float));
            std::memcpy(&Points_[0].Z, &StartGhost.Z, sizeof(float));

            const int EndGhost = LiveCount_ + 1;
            CatmullRomVec3(Points_[LiveCount_ - 1], Points_[LiveCount_ - 1], Points_[LiveCount_],
                           Points_[LiveCount_], 0.9f, Points_[EndGhost]);
            const FVec3* LastPoint = &Points_[LiveCount_];
            FVec3* GhostPoint = &Points_[EndGhost];
            float DifferenceZ;
            float X;
            float Y;
            float Z;
            // Release reassociates the ordinary C++ additions and selects
            // different NaN payloads (58 concrete probe mismatches).
            // Keep only this arithmetic/publication sequence bounded here.
            __asm
            {
                mov ecx, LastPoint
                mov edx, GhostPoint
                fld dword ptr [ecx]
                fsub dword ptr [edx]
                fld dword ptr [ecx + 4]
                fsub dword ptr [edx + 4]
                fld dword ptr [ecx + 8]
                fsub dword ptr [edx + 8]
                fstp DifferenceZ
                fxch st(1)
                fadd dword ptr [ecx]
                fstp X
                fadd dword ptr [ecx + 4]
                fstp Y
                fld DifferenceZ
                fadd dword ptr [ecx + 8]
                mov eax, X
                mov dword ptr [edx], eax
                mov eax, Y
                mov dword ptr [edx + 4], eax
                fstp Z
                mov eax, Z
                mov dword ptr [edx + 8], eax
            }
        }

        void ApplyLoadedQuats(const FQuat* Quats, int Count)
        {
            if (!bOrientation_ || Quats_ == nullptr || Quats == nullptr)
                return;
            for (int Index = 0; Index < Count; ++Index)
            {
                const float Values[4] = {Quats[Index].W, Quats[Index].X, Quats[Index].Y,
                                         Quats[Index].Z};
                SetOrientationInternal(static_cast<unsigned int>(Index), Values);
            }
        }

        // FUN_06f22500 after IPersistString Load: OPEN duplicates ends; CLOSED wrap-around.
        void ApplyPersistGhosts()
        {
            // A zero-point constructor produces no point storage. Ghost
            // copies apply to the successfully constructed nonempty core.
            if (LiveCount_ <= 0)
                return;
            if (bClosed_)
            {
                LastLive_ = static_cast<int>(static_cast<unsigned int>(PointCount_) - 2u);
                Points_[LastLive_] = Points_[FirstLive_];
                Points_[LastLive_ + 1] = Points_[FirstLive_ + 1];
                Points_[0] = Points_[LastLive_ - 1];
                return;
            }

            LastLive_ = static_cast<int>(static_cast<unsigned int>(PointCount_) - 3u);
            Points_[0] = Points_[FirstLive_];
            Points_[LastLive_ + 1] = Points_[LastLive_];
        }

        float* SegmentTable(int Seg)
        {
            return CoreStorage_->SampleRows[Seg].data();
        }

        const float* SegmentTable(int Seg) const
        {
            return CoreStorage_->SampleRows[Seg].data();
        }

        // FUN_06f22630 + FUN_06f22030 + FUN_06f222d0 + FUN_06f22600.
        bool RebuildSampleTables()
        {
            // Per-row owners retain capacity across rebuilds. Each segment
            // below constructs a distinct temporary table, then assigns it.
            SampleTableRows_ = LastLive_;
            TotalLength_ = 0.0f;

            unsigned int RowIndex = 1u;
            for (unsigned int Segment = static_cast<unsigned int>(FirstLive_);
                 Segment < static_cast<unsigned int>(LastLive_); ++Segment, ++RowIndex)
            {
                FVec3 P0;
                FVec3 P1;
                FVec3 P2;
                FVec3 P3;
                NeighborPoints(static_cast<int>(Segment), P0, P1, P2, P3);

                float SegmentLength = 0.0f;
                float IncomingTotal;
                std::memcpy(&IncomingTotal, &TotalLength_, sizeof(float));
                {
                    std::vector<float> SegmentSamples(G_SamplesPerSegment, 0.0f);
                    float* Table = SegmentSamples.data();
                    FVec3 Prev;
                    CatmullRomVec3(P0, P1, P2, P3, 0.0f, Prev);
                    Table[0] = IncomingTotal;
                    for (int Sample = 1; Sample <= G_CrSamples; ++Sample)
                    {
                        FVec3 Cur;
                        CatmullRomVec3(P0, P1, P2, P3, static_cast<float>(Sample) * G_CrStep, Cur);
                        AccumulateSegmentSampleRetail(Prev, Cur, IncomingTotal, SegmentLength, Table[Sample]);
                    }
                    CoreStorage_->SampleRows[RowIndex] = SegmentSamples;
                }
                // FUN_06f22630 publishes Total before the waiting Time store.
                TotalLength_ += SegmentLength;
                Times_[Segment + 1u] = ReadPublishedTotal(TotalLength_);
            }
            for (unsigned int Index = 1u; Index < RowIndex; ++Index)
            {
                float* Table = SegmentTable(static_cast<int>(Index));
                NormalizeSegmentSamplesRetail(Table, TotalLength_);
            }
            // 22726/2272c/22735 run after every row normalizer.
            Times_[1] = 0.0f;
            Times_[0] = 0.0f;
            Times_[static_cast<unsigned int>(LastLive_) + 1u] = 0.0f;
            // 22600 is the caller's separate time normalization stage.
            for (unsigned int Index = 0u; Index < static_cast<unsigned int>(PointCount_); ++Index)
                Times_[Index] /= TotalLength_;
            return true;
        }

        void NeighborPoints(int Seg, FVec3& P0, FVec3& P1, FVec3& P2, FVec3& P3) const
        {
            P0 = Points_[Seg - 1];
            P1 = Points_[Seg];
            P2 = Points_[Seg + 1];
            P3 = Points_[Seg + 2];
        }

        void NeighborQuats(int Seg, FQuat& Q0, FQuat& Q1, FQuat& Q2, FQuat& Q3) const
        {
            CopyQuaternionWords(&Q0, &Quats_[Seg - 1].W);
            CopyQuaternionWords(&Q1, &Quats_[Seg].W);
            CopyQuaternionWords(&Q2, &Quats_[Seg + 1].W);
            CopyQuaternionWords(&Q3, &Quats_[Seg + 2].W);
        }

        bool FindPathSegment(const float* T, int& Seg) const;
        bool FindSample(int Seg, const float* T, int& SampleIndex, float& Fraction) const;

        bool EvalTwoSample(const float* T, float* Out, bool bTangent) const
        {
            if (LiveCount_ <= 0)
                return false;
            if (!bTangent)
            {
                // 22dc0 selects endpoints only by T, regardless of point count.
                // OPEN-one interior reaches the named missing-row residual below.
                if (*T <= 0.0f)
                {
                    std::memcpy(Out, &Points_[FirstLive_].X, sizeof(float));
                    std::memcpy(Out + 1, &Points_[FirstLive_].Y, sizeof(float));
                    std::memcpy(Out + 2, &Points_[FirstLive_].Z, sizeof(float));
                    return true;
                }
                if (*T >= 1.0f)
                {
                    std::memcpy(Out, &Points_[LastLive_].X, sizeof(float));
                    std::memcpy(Out + 1, &Points_[LastLive_].Y, sizeof(float));
                    std::memcpy(Out + 2, &Points_[LastLive_].Z, sizeof(float));
                    return true;
                }
            }
            else if (LastLive_ <= FirstLive_)
            {
                // An OPEN one-point core has no segment. CLOSED one-point
                // persistence owns [1,2): four initialized duplicate points
                // and a real 17-sample row (zero length normalizes to NaNs).
                return false;
            }

            int Seg = 0;
            if (!FindPathSegment(T, Seg))
                return false;
            int SampleIndex = 0;
            float Fraction = 0.0f;
            if (!FindSample(Seg, T, SampleIndex, Fraction))
                return false;

            FVec3 P0;
            FVec3 P1;
            FVec3 P2;
            FVec3 P3;
            NeighborPoints(Seg, P0, P1, P2, P3);

            const float U0 = ThornSplineMath::SampleParameter(SampleIndex, G_CrStep);
            const float U1 = ThornSplineMath::SampleParameter(SampleIndex + 1, G_CrStep);
            FVec3 A;
            FVec3 B;
            if (bTangent)
            {
                CatmullRomTangent(P0, P1, P2, P3, U0, A);
                CatmullRomTangent(P0, P1, P2, P3, U1, B);
            }
            else
            {
                CatmullRomVec3(P0, P1, P2, P3, U0, A);
                CatmullRomVec3(P0, P1, P2, P3, U1, B);
            }
            ThornSplineMath::Lerp3D(&A.X, &B.X, Fraction, Out);
            return true;
        }

        bool EvalPosition(const float* T, float* Out) const { return EvalTwoSample(T, Out, false); }

        bool EvalTangent(const float* T, float* Out) const { return EvalTwoSample(T, Out, true); }

        bool EvalOrientation(const float* T, float* Out) const
        {
            // 22ec0 and 233b6 test only the actual inner ready byte.
            // FreePath/failed allocation clear it; Count0 identity setup leaves
            // it clear, so no invented count guard precedes the retail exit.
            if (!bOrientationReady_)
                return false;
            // 22ec0 selects endpoints only by T after the ready-byte gate.
            // OPEN-one interior reaches the named missing-row residual below.
            if (*T <= 0.0f)
            {
                std::memcpy(Out, &Quats_[FirstLive_].W, sizeof(float));
                std::memcpy(Out + 1, &Quats_[FirstLive_].X, sizeof(float));
                std::memcpy(Out + 2, &Quats_[FirstLive_].Y, sizeof(float));
                std::memcpy(Out + 3, &Quats_[FirstLive_].Z, sizeof(float));
                return true;
            }
            if (*T >= 1.0f)
            {
                std::memcpy(Out, &Quats_[LastLive_].W, sizeof(float));
                std::memcpy(Out + 1, &Quats_[LastLive_].X, sizeof(float));
                std::memcpy(Out + 2, &Quats_[LastLive_].Y, sizeof(float));
                std::memcpy(Out + 3, &Quats_[LastLive_].Z, sizeof(float));
                return true;
            }
            int Seg = 0;
            if (!FindPathSegment(T, Seg))
                return false;
            int SampleIndex = 0;
            float Fraction = 0.0f;
            if (!FindSample(Seg, T, SampleIndex, Fraction))
                return false;

            FQuat Q0;
            FQuat Q1;
            FQuat Q2;
            FQuat Q3;
            NeighborQuats(Seg, Q0, Q1, Q2, Q3);

            const float U0 = ThornSplineMath::SampleParameter(SampleIndex, G_CrStep);
            const float U1 = ThornSplineMath::SampleParameter(SampleIndex + 1, G_CrStep);
            FQuat SampleA;
            FQuat SampleB;
            // Retail (thorn.dll.txt:7594-7752) never tests either FUN_06f21770
            // call's result nor the final vtbl+0x58 slerp's GENRESULT -- all
            // three are consumed unconditionally before the four output
            // dwords are stored.
            EvalCrOrientation(Q0, Q1, Q2, Q3, U0, SampleA);
            EvalCrOrientation(Q0, Q1, Q2, Q3, U1, SampleB);
            FQuat Result;
            CoreQuatNlerp(Result, SampleA, SampleB, &Fraction);
            std::memcpy(Out, &Result.W, sizeof(float));
            std::memcpy(Out + 1, &Result.X, sizeof(float));
            std::memcpy(Out + 2, &Result.Y, sizeof(float));
            std::memcpy(Out + 3, &Result.Z, sizeof(float));
            return true;
        }

        // Retail +0x70 controlling unknown and +0x74 embedded inner unknown
        // {vtbl PTR_LAB_06f5abc8, RefCount, Owner}; the object's reference
        // count lives in Inner_ exactly as in retail.
        IDAComponent* Outer_;
        FThornInnerUnknown Inner_;
        FVec3* Points_;
        float* Times_;
        std::unique_ptr<FSplineCoreStorage> CoreStorage_;
        FQuat* Quats_;
        int LiveCount_;
        int PointCount_;
        int FirstLive_;
        int LastLive_;
        int SampleTableRows_;
        float TotalLength_;      // inner core +0x4
        bool bOrientation_;
        bool bOrientationReady_; // Retail inner +5d, distinct from requested class.
        unsigned char bClosed_;
        bool bInnerAllocated_;   // outer +0xc != NULL
    };

#pragma float_control(except, on, push)
    // 23071 rounds Ratio; 23076 discards the consumed boundary before Index.
    __declspec(noinline) float CalculateSampleRatio(const float* T,
        const float* Interval, float& Ratio)
    {
        Ratio = (*T - Interval[0]) / (Interval[1] - Interval[0]);
        return Interval[0];
    }

    __declspec(noinline) float ReadPublishedRatio(const float& Ratio)
    {
        return Ratio;
    }

    // LAB_06f22dc0 / LAB_06f22ec0 walk of +0x38; EBX is inner FUN_06f230b0 param_1.
    inline bool FSplinePath::FindPathSegment(const float* T, int& Seg) const
    {
        unsigned int TableIndex = static_cast<unsigned int>(FirstLive_);
        unsigned int Segment = 0;
        const unsigned int Last = static_cast<unsigned int>(LastLive_);
        while (TableIndex < Last)
        {
            // TEST AH,41 / JZ stops only on ordered Times[Index] > T.
            // Equality, less and unordered all increment both counters.
            if (Times_[TableIndex] > *T)
                break;
            ++Segment;
            ++TableIndex;
        }
        Seg = static_cast<int>(Segment);
        // Retail dispatches even for absent row/point -1. Keep failure for
        // that undefined storage, never clamp to another row.
        return Segment >= static_cast<unsigned int>(FirstLive_) && Segment < Last;
    }

    // FUN_06f22f80: locate sample i and lerp fraction inside the 17-entry table.
    inline bool FSplinePath::FindSample(int Seg, const float* T,
        int& SampleIndex, float& Fraction) const
    {
        if (CoreStorage_ == nullptr || Seg < 0 || Seg >= SampleTableRows_)
            return false;
        std::vector<float> SampleCopy(CoreStorage_->SampleRows[Seg]);
        const float* Table = SampleCopy.data();
        // An exact table hit selects the preceding interval.
        int Index = static_cast<int>(SampleCopy.size()) / 2;
        int Step = Index;
        do
        {
            const int PreviousIndex = Index;
            Step /= 2;
            if (*T < Table[PreviousIndex])
                Step = -Step;
            Index = PreviousIndex + Step;
            const int Difference = PreviousIndex - Index;
            Step = (Difference < 0) ? -Difference : Difference;
            if (Index == PreviousIndex)
                break;
        } while (true);

        // FCOM Table[Index],T; TEST AH,05; JP selects the preceding
        // interval for equal, greater, and unordered comparisons.
        if (!(*T > Table[Index]))
            --Index;
        // Retail divides unconditionally and stores float before Index.
        const float* Interval = Table + Index;
        float Ratio;
        (void)CalculateSampleRatio(T, Interval, Ratio);
        SampleIndex = Index;
        // Retail frees the copied row before returning the rounded ratio.
        std::vector<float>().swap(SampleCopy);
        Fraction = ReadPublishedRatio(Ratio);
        return true;
    }
#pragma float_control(pop)

    IDAComponent* CreateSplineObject(bool bOrientation, IDAComponent* Outer,
                                     IDAComponent** OutInnerUnknown)
    {
        if (OutInnerUnknown != nullptr)
            *OutInnerUnknown = nullptr;

        // LAB_06f37400 constructs both variants without acquiring math.
        FSplinePath* Object = new (std::nothrow) FSplinePath(bOrientation);
        if (Object == nullptr)
            return nullptr;
        IDAComponent* Inner = Object->Aggregate(Outer);
        if (OutInnerUnknown != nullptr)
            *OutInnerUnknown = Inner;
        return static_cast<ICRSpline*>(Object);
    }
}

IDAComponent* ThornCreateSplinePath(const char* FactoryName, bool bOrientation, IDAComponent* Outer,
                                    IDAComponent** OutInnerUnknown)
{
    (void)FactoryName;
    return CreateSplineObject(bOrientation, Outer, OutInnerUnknown);
}

GENRESULT ThornQuatMul(I3DMathEngine* Math, FQuat& Dst, const FQuat& A, const FQuat& B)
{
    if (Math == nullptr)
        return GR_GENERIC;
    return Math->MulQuat(reinterpret_cast<FQuaternion&>(Dst), // +0x18
                         reinterpret_cast<const FQuaternion&>(A),
                         reinterpret_cast<const FQuaternion&>(B));
}

GENRESULT ThornQuatNlerp(I3DMathEngine* Math, FQuat& Dst, const FQuat& A, const FQuat& B, float T)
{
    if (Math == nullptr)
        return GR_GENERIC;
    return Math->QuatNlerp(reinterpret_cast<FQuaternion&>(Dst), // +0x58
                           reinterpret_cast<const FQuaternion&>(A),
                           reinterpret_cast<const FQuaternion&>(B), T);
}

GENRESULT ThornQuatToMatrix(I3DMathEngine* Math, float* Dst9, const FQuat& Quat)
{
    if (Math == nullptr || Dst9 == nullptr)
        return GR_GENERIC;
    return Math->QuaternionToMatrix(*reinterpret_cast<FMatrix*>(Dst9), // +0x54
                                    reinterpret_cast<const FQuaternion&>(Quat));
}
