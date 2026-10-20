// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "alchemy_appearances.h"

#include "alchemy_common.h"
#include "alchemy_math.h"
#include "alchemy_ramp.h"
#include "common_misc.h"
#include "render/shading.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

// alchemy.dll — DACOM particle FX scaffolding.
// The eight appearance leaves: their property sets (each class's set creator and
// builder, and the set slots' tables), plus the per-leaf update, spawn, bounds and draw
// slots.
namespace AlchemyImpl
{
    namespace
    {
        I3DMathEngine* G_Perp3DMath = nullptr; // DAT_06257b38, shared by Perp's draw/fade calls.
        I3DMathEngine* G_BasicGeometry3DMath = nullptr; // DAT_06257a28, Basic quad/triangle.
        I3DMathEngine* G_Rect3DMath = nullptr; // DAT_06257b8c, Rect bounds and fill.

        // Retail FCOMP raises INVALID for quiet NaNs and still treats unordered as unequal.
        // Plain != lowers to quiet FUCOMPP, so preserve ordered relational comparisons.
        // Release /O2 reverses constant/field operands and x87 condition flags without this scope.
#pragma optimize("", off)
#pragma float_control(except, on, push)
        __declspec(noinline) bool IsNonzeroParticleField(const float& Value)
        {
            return Value <= 0.0f ? Value < 0.0f : true;
        }

        __declspec(noinline) bool IsNonunitParticleField(const float& Value)
        {
            return Value <= 1.0f ? Value < 1.0f : true;
        }

        // Basic's unrounded scale uses signalling FCOMP before the zero-only return.
        __declspec(noinline) bool IsZeroBasicScale(const double& Value)
        {
            return Value <= 0.0 ? Value >= 0.0 : false;
        }

        // Perp/Rect frame flips convert only one side through x87; UV words may be sNaN/subnormal.
        __declspec(noinline) double LoadAppearanceScalar(const float& Value)
        {
            return Value;
        }

        // Preserve the cleared DeathEffect at the reciprocal's FP exception point.
        __declspec(noinline) void InitializeParticleReciprocal(FParticleAppRecord& Record)
        {
            Record.DeathEffect = nullptr;
            Record.Base.InvDuration = 1.0f / Record.Base.Duration;
        }

        __declspec(noinline) bool IsMeshParticleExpired(const FPooledParticle& Particle)
        {
            return Particle.Age > Particle.Duration;
        }

        // FUN_06206c40 rounds all three velocity products before position additions.
        __declspec(noinline) void MeshParticleStep(const FPooledParticle& Particle, float Dt, float* Out)
        {
            const double Z = static_cast<double>(Dt) * Particle.VelZ;
            const double Y = static_cast<double>(Dt) * Particle.VelY;
            const double X = static_cast<double>(Dt) * Particle.VelX;
            Out[0] = static_cast<float>(X);
            Out[1] = static_cast<float>(Y);
            Out[2] = static_cast<float>(Z);
        }

        // FUN_06225000 merges zero-radius mesh points; retain each x87 float store.
        __declspec(noinline) void MergeMeshBoundsPoint(float* Sphere, const float* Center)
        {
            const double DifferenceZ = static_cast<double>(Center[2]) - Sphere[2];
            const double DifferenceY = static_cast<double>(Center[1]) - Sphere[1];
            const double DifferenceX = static_cast<double>(Center[0]) - Sphere[0];
            float Difference[3];
            Difference[0] = static_cast<float>(DifferenceX);
            Difference[1] = static_cast<float>(DifferenceY);
            Difference[2] = static_cast<float>(DifferenceZ);
            const double SquareY = static_cast<double>(Difference[1]) * Difference[1];
            const double SquareZ = static_cast<double>(Difference[2]) * Difference[2];
            const double SumYz = SquareY + SquareZ;
            const double SquareX = static_cast<double>(Difference[0]) * Difference[0];
            const float LengthSquared = static_cast<float>(SumYz + SquareX);
            std::int32_t LengthBits = 0;
            std::memcpy(&LengthBits, &LengthSquared, sizeof(LengthBits));
            const std::uint32_t GuessBits = 0x5f3759dfu -
                static_cast<std::uint32_t>(LengthBits >> 1);
            float Guess;
            std::memcpy(&Guess, &GuessBits, sizeof(Guess));
            const double GuessSquared = static_cast<double>(Guess) * Guess;
            const double InverseLength = (1.5 - ((GuessSquared * LengthSquared) * 0.5)) * Guess;
            const double Distance = InverseLength * LengthSquared;
            const float RoundedDistance = static_cast<float>(Distance);
            // TEST AH,5 + JP branches away unless the first sum is ordered less.
            if (Distance + Sphere[3] < 0.0)
            {
                std::memcpy(Sphere, Center, sizeof(float) * 3);
                Sphere[3] = 0.0f;
                return;
            }
            // TEST AH,5 + JNP skips the merge only for ordered less.
            if (static_cast<double>(RoundedDistance) + 0.0 < Sphere[3])
                return;
            const float NewRadius = static_cast<float>(
                ((0.0 + static_cast<double>(Sphere[3])) + RoundedDistance) * 0.5);
            // TEST AH,0x41 + JNP skips normalization on ordered less or equal.
            if (!(static_cast<double>(RoundedDistance) <= 1e-6))
            {
                const double InverseDistance = 1.0 / RoundedDistance;
                Difference[0] = static_cast<float>(InverseDistance * Difference[0]);
                Difference[1] = static_cast<float>(InverseDistance * Difference[1]);
                Difference[2] = static_cast<float>(InverseDistance * Difference[2]);
                const float Step = static_cast<float>(static_cast<double>(NewRadius) - Sphere[3]);
                const double StepZ = static_cast<double>(Step) * Difference[2];
                const double StepY = static_cast<double>(Step) * Difference[1];
                const double StepX = static_cast<double>(Step) * Difference[0];
                float Offset[3];
                Offset[0] = static_cast<float>(StepX);
                Offset[1] = static_cast<float>(StepY);
                Offset[2] = static_cast<float>(StepZ);
                Sphere[0] = static_cast<float>(static_cast<double>(Offset[0]) + Sphere[0]);
                Sphere[1] = static_cast<float>(static_cast<double>(Offset[1]) + Sphere[1]);
                Sphere[2] = static_cast<float>(static_cast<double>(Offset[2]) + Sphere[2]);
            }
            Sphere[3] = NewRadius;
        }
        enum class EPerpRectBoundsDistance
        {
            PerpUnroundedXyz,
            RectRoundedYzx
        };

        // The two callers have independently traced distance terms: Perp's 06206ad0
        // retains the XYZ sum on ST, while Rect stores its inlined YZX sum as float.
        __declspec(noinline) void MergePerpRectBoundsSphere(float* Sphere, const float* Center,
            const float& Radius, EPerpRectBoundsDistance DistanceKind)
        {
            const double DifferenceZ = static_cast<double>(Center[2]) - Sphere[2];
            const double DifferenceY = static_cast<double>(Center[1]) - Sphere[1];
            const double DifferenceX = static_cast<double>(Center[0]) - Sphere[0];
            float Difference[3];
            Difference[0] = static_cast<float>(DifferenceX);
            Difference[1] = static_cast<float>(DifferenceY);
            Difference[2] = static_cast<float>(DifferenceZ);
            double LengthSquared;
            float RoundedLengthSquared;
            if (DistanceKind == EPerpRectBoundsDistance::PerpUnroundedXyz)
            {
                const double SquareX = static_cast<double>(Difference[0]) * Difference[0];
                const double SquareY = static_cast<double>(Difference[1]) * Difference[1];
                const double SumXy = SquareX + SquareY;
                const double SquareZ = static_cast<double>(Difference[2]) * Difference[2];
                LengthSquared = SumXy + SquareZ;
                RoundedLengthSquared = static_cast<float>(LengthSquared);
            }
            else
            {
                const double SquareY = static_cast<double>(Difference[1]) * Difference[1];
                const double SquareZ = static_cast<double>(Difference[2]) * Difference[2];
                const double SumYz = SquareY + SquareZ;
                const double SquareX = static_cast<double>(Difference[0]) * Difference[0];
                RoundedLengthSquared = static_cast<float>(SumYz + SquareX);
                LengthSquared = RoundedLengthSquared;
            }
            std::int32_t LengthBits;
            std::memcpy(&LengthBits, &RoundedLengthSquared, sizeof(LengthBits));
            const std::uint32_t GuessBits = 0x5f3759dfu -
                static_cast<std::uint32_t>(LengthBits >> 1);
            float Guess;
            std::memcpy(&Guess, &GuessBits, sizeof(Guess));
            const double GuessSquared = static_cast<double>(Guess) * Guess;
            const double InverseLength = (1.5 - ((GuessSquared * LengthSquared) * 0.5)) * Guess;
            const double Distance = InverseLength * LengthSquared;
            const float RoundedDistance = static_cast<float>(Distance);
            // TEST AH,5 + JP branches away unless the first sum is ordered less.
            if (Distance + Sphere[3] < Radius)
            {
                std::memcpy(Sphere, Center, sizeof(float) * 3);
                std::memcpy(Sphere + 3, &Radius, sizeof(Radius));
                return;
            }
            // TEST AH,5 + JNP skips the merge only for ordered less.
            if (static_cast<double>(Radius) + RoundedDistance < Sphere[3])
                return;
            const float NewRadius = static_cast<float>(
                ((static_cast<double>(Radius) + Sphere[3]) + RoundedDistance) * 0.5);
            // TEST AH,0x41 + JNP skips normalization for ordered less or equal.
            if (!(static_cast<double>(RoundedDistance) <= 1e-6))
            {
                const double InverseDistance = 1.0 / RoundedDistance;
                Difference[0] = static_cast<float>(InverseDistance * Difference[0]);
                Difference[1] = static_cast<float>(InverseDistance * Difference[1]);
                Difference[2] = static_cast<float>(InverseDistance * Difference[2]);
                const float Step = static_cast<float>(static_cast<double>(NewRadius) - Sphere[3]);
                const double StepZ = static_cast<double>(Step) * Difference[2];
                const double StepY = static_cast<double>(Step) * Difference[1];
                const double StepX = static_cast<double>(Step) * Difference[0];
                float Offset[3];
                Offset[0] = static_cast<float>(StepX);
                Offset[1] = static_cast<float>(StepY);
                Offset[2] = static_cast<float>(StepZ);
                Sphere[0] = static_cast<float>(static_cast<double>(Offset[0]) + Sphere[0]);
                Sphere[1] = static_cast<float>(static_cast<double>(Offset[1]) + Sphere[1]);
                Sphere[2] = static_cast<float>(static_cast<double>(Offset[2]) + Sphere[2]);
            }
            std::memcpy(Sphere + 3, &NewRadius, sizeof(NewRadius));
        }
#pragma float_control(pop)
#pragma optimize("", on)

        // Perp's spawn tail (FUN_06235db0, 0x06235e15..0x06235e9b): the squared
        // distance of the spawn velocity from +X, and its Y and Z squares, are each
        // compared against DAT_0624eb68, whose bytes BE 37 86 35 are 0x358637BE,
        // about 1.0e-6. Written from the bits; the previous copy of this test used
        // 1e-8.
        float PerpAxisParallelEpsilon()
        {
            const unsigned long Bits = 0x358637BEu;
            float Value = 0.0f;
            std::memcpy(&Value, &Bits, sizeof(Value));
            return Value;
        }

        // wave68 (sonnet:alchemy-wave68-lane1, 2026-09-11): two small raw-
        // vtable helpers for FxPerpAppearance::DrawOrEnqueue below, local to
        // this file rather than new shared entry points in
        // alchemy_common.h/.cpp (out of this lane's file ownership this
        // wave) — same "Vtable[N]" raw-pointer-cast idiom already
        // established throughout this module (e.g. alchemy_common.cpp's
        // CallSetRenderState/CallTextureLibraryFrameLookup). Both call
        // shapes are byte-confirmed against FUN_06233170's own raw listing
        // (alchemy.dll.txt 0x062332cc-0x062332ea, decomp-tracker item
        // 134306): RenderPipeline::SetTexture(stage, handle) at vtable+0x108
        // (the SAME slot GetTextureHandle's own doc comment, alchemy_
        // genericfx.h, already cites for this exact call) and
        // RenderPipeline::SetTransform(const float*) at vtable+0x7c.
        void CallRenderPipelineSetTexture(IRenderPipeline* Pipeline, int Stage, int Handle)
        {
            if (Pipeline == nullptr)
                return;
            Pipeline->SetTextureStageTexture(
                Stage, reinterpret_cast<void*>(static_cast<ptrdiff_t>(Handle)));
        }

        void CallRenderPipelineSetTransform(IRenderPipeline* Pipeline, const float* Xform12)
        {
            if (Pipeline == nullptr || Xform12 == nullptr)
                return;
            Pipeline->SetModelview(Xform12);
        }

        // {Pipeline, State, Saved} render-state saver of FUN_06206850 (stack +0x8/+0xc/+0x10):
        // the constructor is inlined there (GetRenderState, vt+0xf4); the destructor is
        // FUN_06208a20 (`SetRenderState(State, Saved)`, vt+0xf0), reached both from the
        // normal exit and from the EH unwind Unwind@06247460.
        struct FScopedRenderState
        {
            IRenderPipeline* Pipeline;
            int State;
            int Saved;

            FScopedRenderState(IRenderPipeline* InPipeline, int InState)
                : Pipeline(InPipeline), State(InState), Saved(0)
            {
                CallGetRenderState(Pipeline, State, &Saved);
            }

            ~FScopedRenderState()
            {
                CallSetRenderState(Pipeline, State, Saved);
            }

            FScopedRenderState(const FScopedRenderState&) = delete;
            FScopedRenderState& operator=(const FScopedRenderState&) = delete;
        };

        // (MergeBoundingSphere, the wave70 sqrt/float lookalike of the bounds merge, was
        // replaced 2026-09-26 by MergeListBoundsSphere below.)

        // ---- FxBasicAppearance fill helpers (FUN_06216550 / FUN_06216f20) ----

        float FloatFromBits(unsigned long Bits)
        {
            float Value = 0.0f;
            std::memcpy(&Value, &Bits, sizeof(Value));
            return Value;
        }

        // 0x3d088880 (DAT_0624cdd8, ~1/30): the streak's backward shift per unit of
        // camera-space speed along the screen axis.
        float StreakShiftScale()
        {
            return FloatFromBits(0x3d088880u);
        }

        // 0x3f5db3d7 (DAT_0624cddc) = sqrt(3)/2: the triangle's lower corners.
        float TriangleCornerScale()
        {
            return FloatFromBits(0x3f5db3d7u);
        }

        // The streak's inline reciprocal square root (0x06216931..0x06216955): SAR the bits,
        // subtract from 0x5f3759df, then y * (1.5 - y*y*x*0.5) in that operand order.
        float RetailInverseSqrt(float Value)
        {
            long Bits = 0;
            std::memcpy(&Bits, &Value, sizeof(Bits));
            const long GuessBits = 0x5f3759dfL - (Bits >> 1);
            float Guess = 0.0f;
            std::memcpy(&Guess, &GuessBits, sizeof(Guess));
            return (1.5f - ((Guess * Guess) * Value) * 0.5f) * Guess;
        }

        // Math engine vt+0x28 TransformXform / vt+0x2c TransformMatrix, the calls both fills
        // make through the lazily cached engine DAT_06257a28.
        void CameraSpacePoint(const float* ViewWorld12, const float* Point, float* Out)
        {
            Acquire3DMath()->TransformXform(*reinterpret_cast<FVector*>(Out),
                                            *reinterpret_cast<const FTransform*>(ViewWorld12),
                                            *reinterpret_cast<const FVector*>(Point));
        }

        // ---- List bounds: basic FUN_06216090, perp FUN_06232cc0, rect FUN_06238280,
        // oriented FUN_0622bed0, beam FUN_062050c0 -- one body in five copies that differ
        // only where FListBoundsShape says ----

        // How a copy computes the per-particle radius.
        //   Primary (basic, perp; 0x0621610f / 0x06232d7a): P+0x78 @ (node+0x8c, t) * Scale.
        //   Side lengths (rect 0x06238316..0x0623838c, oriented 0x0622bf55..0x0622bfd1):
        //     A = P+0x78 @ (node+0x8c, t) * Scale, B = P+0x7c @ (node+0x8c, t), C = P+0x80 @
        //     (node+0x8c, t), in that order; radius = max * A, the max picked differently
        //     (the two differ only for NaN):
        //       rect 0x0623836c: FLD B; FCOMP (B against C in ST1); AND 0x4100 / JNZ keeps
        //         C unless B > C -> B > C ? B : C.
        //       oriented 0x0622bfb1: FCOM [B] with C in ST0; AND 0x4100 / JZ keeps C only
        //         when C > B, otherwise B is reloaded -> C > B ? C : B.
        //   Beam (0x0620512f..0x06205183): no scale ramp at all; A = P+0x78 @ (node+0x8c,
        //     t), B = P+0x7c @ (node+0x8c, t); radius = (B * A) * 0.5f (DAT_0624bab8).
        const unsigned long kBoundsRadiusPrimary = 0;
        const unsigned long kBoundsRadiusRectSides = 1;
        const unsigned long kBoundsRadiusOrientedSides = 2;
        const unsigned long kBoundsRadiusBeam = 3;

        struct FListBoundsShape
        {
            unsigned long RadiusKind;
            // The peeled tail pass reads the tail's own t (basic) instead of the first's.
            bool bTailOwnAge;
            // Merge length through FUN_06206ad0, (x*x + y*y) + z*z, instead of the inline
            // (y*y + z*z) + x*x -- separately for the peeled pass and for the loop.
            bool bPeelLengthXyz;
            bool bLoopLengthXyz;
        };

        float ListBoundsRadius(const FAppearanceRampDescriptor* Descriptor, float OuterTime,
                               float InnerTime, float Scale, unsigned long RadiusKind)
        {
            if (RadiusKind == kBoundsRadiusBeam)
            {
                const float A =
                    Descriptor->PrimarySizeRamp->Eval(OuterTime, InnerTime);
                const float B =
                    Descriptor->TexFrameRamp->Eval(OuterTime, InnerTime);
                return (B * A) * 0.5f;
            }
            const float Primary =
                Descriptor->PrimarySizeRamp->Eval(OuterTime, InnerTime) *
                Scale;
            if (RadiusKind == kBoundsRadiusPrimary)
                return Primary;
            const float SideB =
                Descriptor->TexFrameRamp->Eval(OuterTime, InnerTime);
            const float SideC =
                Descriptor->AxisOverrideRamp->Eval(OuterTime, InnerTime);
            float Side = 0.0f;
            if (RadiusKind == kBoundsRadiusRectSides)
                Side = SideB > SideC ? SideB : SideC;
            else
                Side = SideC > SideB ? SideC : SideB;
            return Side * Primary;
        }

        // The merge each copy inlines (oriented 0x0622c07b..0x0622c183, rect
        // 0x06238432..0x06238537, basic 0x0621619b..0x0621627e, perp 0x06232dce..0x06232ead,
        // beam 0x062051ed..0x062052d5):
        //   D = C - S (FUN_06206c10).
        //   Dist: inline LenSq = (D.y*D.y + D.z*D.z) + D.x*D.x and FUN_06206b30(LenSq) *
        //     LenSq, or FUN_06206ad0, the same thing with LenSq = (D.x*D.x + D.y*D.y) +
        //     D.z*D.z (bLengthXyz): basic and perp use FUN_06206ad0 in both places, rect and
        //     oriented inline in both, beam FUN_06206ad0 in the peeled pass (0x06205229) and
        //     inline in the loop (0x06205358).
        //   Dist + S.r < C.r (TEST AH,5 / JP: only "less" falls through) -> S = C, done.
        //   C.r + Dist < S.r (TEST AH,5 / JNP skips on "less") -> done.
        //   NewR = ((C.r + S.r) + Dist) * 0.5 (double DAT_0624bab0).
        //   Dist > 1e-06 (double DAT_0624baa8; TEST AH,0x41 / JNP skips on less or equal,
        //     so greater and unordered shift): D *= 1 / Dist (FUN_06206bb0),
        //     S.center += D * (NewR - S.r) (FUN_06206c40, FUN_06206b80).
        //   S.r = NewR.
        void MergeListBoundsSphere(float* Sphere, const float* Center, float Radius,
                                   bool bLengthXyz)
        {
            float D[3];
            D[0] = Center[0] - Sphere[0];
            D[1] = Center[1] - Sphere[1];
            D[2] = Center[2] - Sphere[2];
            float LenSq = 0.0f;
            if (bLengthXyz)
                LenSq = (D[0] * D[0] + D[1] * D[1]) + D[2] * D[2];
            else
                LenSq = (D[1] * D[1] + D[2] * D[2]) + D[0] * D[0];
            const float Dist = RetailInverseSqrt(LenSq) * LenSq;
            if (Dist + Sphere[3] < Radius)
            {
                Sphere[0] = Center[0];
                Sphere[1] = Center[1];
                Sphere[2] = Center[2];
                Sphere[3] = Radius;
                return;
            }
            if (Radius + Dist < Sphere[3])
                return;
            const float NewRadius = static_cast<float>(
                ((static_cast<double>(Radius) + Sphere[3]) + Dist) * 0.5);
            if (!(static_cast<double>(Dist) <= 1e-06))
            {
                const float Inv = 1.0f / Dist;
                D[0] = D[0] * Inv;
                D[1] = D[1] * Inv;
                D[2] = D[2] * Inv;
                const float Step = NewRadius - Sphere[3];
                Sphere[0] = D[0] * Step + Sphere[0];
                Sphere[1] = D[1] * Step + Sphere[1];
                Sphere[2] = D[2] * Step + Sphere[2];
            }
            Sphere[3] = NewRadius;
        }

        // The shared body. Retail's list walk, read off all five listings:
        //   begin == end (null head) -> return -1, Out untouched.
        //   Scale = P+0x5c @ (node+0x8c, node+0x90 = LodInput) -- not evaluated by beam.
        //   t0 = first->InvLife * first->Age; Out = {first.pos, Radius(t0)}.
        //   Tail = [list+4] (FUN_06217be0 / FUN_06234040 / FUN_06239860 / FUN_0622d2a0 /
        //   FUN_062085f0). first == Tail -> done.
        //   One peeled pass merges the TAIL's centre. Its radius: basic reads the tail's
        //     own t (0x06216176 FLD [EAX+0x2c] with EAX = tail, bTailOwnAge); perp, rect,
        //     oriented and beam read the iterator, which still points at the first
        //     particle, so the tail is merged with t0 (0x06232da9 / 0x062383bb /
        //     0x0622c006 / 0x062051a9).
        //   Then cur = first->next while cur != Tail: Radius(cur's t) at cur's centre.
        //   So the tail is merged second and every particle in between follows in list
        //   order.
        //   Out.center = node+0x4c transform of Out.center (math vt+0x28), radius
        //   copied through (FUN_06208a70); return 0.
        GENRESULT ListBoundsSphere(const FPooledParticle* Head, const FPooledParticle* Tail,
                                   const FAppearanceRampDescriptor* Descriptor, float OuterTime,
                                   float LodInput, const float* NodeXform12,
                                   const FListBoundsShape& Shape, float* OutSphere)
        {
            if (Head == nullptr)
                return GR_GENERIC;
            float Scale = 1.0f;
            if (Shape.RadiusKind != kBoundsRadiusBeam)
                Scale = Descriptor->EffectScaleRamp->Eval(OuterTime, LodInput);
            const float FirstInnerTime = Head->InvDuration * Head->Age;
            OutSphere[0] = Head->PosX;
            OutSphere[1] = Head->PosY;
            OutSphere[2] = Head->PosZ;
            OutSphere[3] =
                ListBoundsRadius(Descriptor, OuterTime, FirstInnerTime, Scale, Shape.RadiusKind);
            if (Head != Tail)
            {
                const float TailInnerTime =
                    Shape.bTailOwnAge ? Tail->InvDuration * Tail->Age : FirstInnerTime;
                const float TailCenter[3] = { Tail->PosX, Tail->PosY, Tail->PosZ };
                MergeListBoundsSphere(
                    OutSphere, TailCenter,
                    ListBoundsRadius(Descriptor, OuterTime, TailInnerTime, Scale, Shape.RadiusKind),
                    Shape.bPeelLengthXyz);
                const FPooledParticle* Particle = Head->Next;
                for (; Particle != Tail; Particle = Particle->Next)
                {
                    const float InnerTime = Particle->InvDuration * Particle->Age;
                    const float Center[3] = { Particle->PosX, Particle->PosY, Particle->PosZ };
                    MergeListBoundsSphere(
                        OutSphere, Center,
                        ListBoundsRadius(Descriptor, OuterTime, InnerTime, Scale, Shape.RadiusKind),
                        Shape.bLoopLengthXyz);
                }
            }
            float WorldCenter[3];
            TransformPoint(NodeXform12, OutSphere[0], OutSphere[1], OutSphere[2], WorldCenter);
            OutSphere[0] = WorldCenter[0];
            OutSphere[1] = WorldCenter[1];
            OutSphere[2] = WorldCenter[2];
            return GR_OK;
        }

        void CameraSpaceVector(const float* ViewWorld12, const float* Vector, float* Out)
        {
            Acquire3DMath()->TransformMatrix(*reinterpret_cast<FVector*>(Out),
                                             *reinterpret_cast<const FMatrix*>(ViewWorld12),
                                             *reinterpret_cast<const FVector*>(Vector));
        }

        // Frame block {texture, U0, V0, U1, V1}: descriptor flag 0x1000 swaps U0/U1, 0x2000
        // swaps V0/V1 (0x06216645 / 0x06216825 in the quad, 0x06217003 / 0x06217319 in the
        // triangle).
        void FlipBasicAppearanceFrame(unsigned long Flags, float* FrameBlock)
        {
            if ((Flags & 0x1000u) != 0)
            {
                std::uint32_t Previous;
                std::memcpy(&Previous, &FrameBlock[3], sizeof(Previous));
                FrameBlock[3] = static_cast<float>(LoadAppearanceScalar(FrameBlock[1]));
                std::memcpy(&FrameBlock[1], &Previous, sizeof(Previous));
            }
            if ((Flags & 0x2000u) != 0)
            {
                std::uint32_t Previous;
                std::memcpy(&Previous, &FrameBlock[4], sizeof(Previous));
                FrameBlock[4] = static_cast<float>(LoadAppearanceScalar(FrameBlock[2]));
                std::memcpy(&FrameBlock[2], &Previous, sizeof(Previous));
            }
        }

        void ApplyFrameFlips(unsigned long Flags, float* FrameBlock)
        {
            if ((Flags & 0x1000u) != 0)
            {
                const float Swap = FrameBlock[1];
                FrameBlock[1] = FrameBlock[3];
                FrameBlock[3] = Swap;
            }
            if ((Flags & 0x2000u) != 0)
            {
                const float Swap = FrameBlock[2];
                FrameBlock[2] = FrameBlock[4];
                FrameBlock[4] = Swap;
            }
        }

        // FUN_06216f20's per-corner UV table (0x06217042 / 0x06217371): flag 1 selects
        // DAT_06255cc4.. (U 0,0,1  V 1,0,1), otherwise DAT_06255cdc.. (U 0.5,0,1  V 0,1,1).
        void LoadTriangleUvTable(unsigned long Flags, float* TriU, float* TriV)
        {
            if ((Flags & 1u) != 0)
            {
                TriU[0] = 0.0f;
                TriU[1] = 0.0f;
                TriU[2] = 1.0f;
                TriV[0] = 1.0f;
                TriV[1] = 0.0f;
                TriV[2] = 1.0f;
            }
            else
            {
                TriU[0] = 0.5f;
                TriU[1] = 0.0f;
                TriU[2] = 1.0f;
                TriV[0] = 0.0f;
                TriV[1] = 1.0f;
                TriV[2] = 1.0f;
            }
        }

        // Table into the frame rect: U*(U1-U0)+U0, V*(V1-V0)+V0 (0x062170d7..0x06217153).
        void MapTriangleUvTable(const float* FrameBlock, float* TriU, float* TriV,
                                bool bInitial, bool bMap = true)
        {
            const volatile double SpanU = static_cast<double>(FrameBlock[3]) - FrameBlock[1];
            const double SpanVUnrounded = static_cast<double>(FrameBlock[4]) - FrameBlock[2];
            // 062170f5 stores the initial V span; 0621736d retains the new frame's span.
            const volatile float InitialSpanV = bInitial ? static_cast<float>(SpanVUnrounded) : 0.0f;
            const double SpanV = bInitial ?
                static_cast<double>(InitialSpanV) : SpanVUnrounded;
            if (!bMap)
                return;
            for (int Corner = 0; Corner < 3; ++Corner)
                TriU[Corner] = static_cast<float>(TriU[Corner] * SpanU + FrameBlock[1]);
            for (int Corner = 0; Corner < 3; ++Corner)
            {
                TriV[Corner] = static_cast<float>(
                    static_cast<double>(TriV[Corner]) * SpanV + FrameBlock[2]);
            }
        }

        // Every fill writes into its OWN class's draw context (basic DAT_062579dc, rect
        // DAT_06257b40, perp DAT_06257aec, oriented DAT_06257a74, dust DAT_0625797c, beam
        // DAT_0625792c). The beam calls FUN_06208aa0 / FUN_06208b00 / FUN_06208b50 with ECX =
        // its context; the other classes inline the same stores against their context's
        // globals. The vertex store is written only while the counter is not past the
        // requested count (`CMP count,[+0x18]; JG skip`), and the index the caller keeps is
        // the counter before the increment -- or the requested count when the store was
        // skipped (AlSubmitVertex = FUN_06208aa0).
        unsigned short StoreDrawVertex(FAlDrawContext& Context, float X, float Y, float Z,
                                       unsigned long Diffuse, float U, float V)
        {
            return AlSubmitVertex(Context, X, Y, Z, Diffuse, U, V);
        }

        // Quad indices as the list classes inline them (basic 0x06216e7c / 0x06216bfb):
        // (I3,I2,I1) (I1,I2,I0), skipped when IndexCount + 5 > requested; the context's
        // +0xc primitive counter += 2 (basic 0x06216edf, dust 0x06210578, oriented
        // 0x0622cfd2, perp 0x06233d8a, rect 0x062395ab) -- FUN_06208b50 with the order
        // reversed.
        void StoreQuadIndices(FAlDrawContext& Context, unsigned short I0, unsigned short I1,
                              unsigned short I2, unsigned short I3)
        {
            AlSubmitQuadIndices(Context, I3, I2, I1, I0);
        }

        // Triangle indices (basic 0x062175be): (I2,I1,I0), skipped when IndexCount + 2 >
        // requested; +0xc += 1 (0x06217601) -- FUN_06208b00 with the order reversed.
        void StoreTriangleIndices(FAlDrawContext& Context, unsigned short I0, unsigned short I1,
                                  unsigned short I2)
        {
            AlSubmitTriangleIndices(Context, I2, I1, I0);
        }

        // FUN_06208b00 / the beam's inline triangle stores (INC [0x06257938] at 0x06206586
        // ...): indices (A, B, C) in that order.
        void StoreBeamTriangle(unsigned short A, unsigned short B, unsigned short C)
        {
            AlSubmitTriangleIndices(G_BeamDrawContext, A, B, C);
        }

        // FUN_06208b50 on the beam context: indices (A, B, C, C, B, D).
        void StoreBeamQuad(unsigned short A, unsigned short B, unsigned short C, unsigned short D)
        {
            AlSubmitQuadIndices(G_BeamDrawContext, A, B, C, D);
        }
    } // namespace

    // ---- The property sets' tables (ALC-6) ----
    namespace
    {
        typedef FAppearanceRampDescriptor FSet;

        // Bool tables (+0x04 / +0x08, the getter and the setter test the same hashes in the
        // same order): basic FUN_06215930 / FUN_06215a00, rect FUN_06237c00 / FUN_06237cd0,
        // perp FUN_06235320 / FUN_06232730, oriented FUN_0622e680 / FUN_0622b8a0, dust
        // FUN_06211e10 / FUN_06211e70, beam FUN_06204af0 / FUN_06204ba0.
        const FDescriptorFlagBit kBasicFlags[] = {
            {0xF863872Eu, 0x1},    {0xF87B5FD5u, 0x2},    {0xE91467F1u, 0x4},
            {0xE2F60EEBu, 0x8},    {0xF3D52EE4u, 0x1000}, {0xE8DC7F5Eu, 0x2000},
        };
        const FDescriptorFlagBit kRectFlags[] = {
            {0xFB7958C1u, 0x1},    {0xE91467F1u, 0x4},    {0xF9FEBF0Du, 0x10},
            {0xE2F60EEBu, 0x8},    {0xF3D52EE4u, 0x1000}, {0xE8DC7F5Eu, 0x2000},
        };
        const FDescriptorFlagBit kPerpFlags[] = {
            {0xF9FEBF0Du, 0x10},
            {0xE2F60EEBu, 0x8},
            {0xF3D52EE4u, 0x1000},
            {0xE8DC7F5Eu, 0x2000},
        };
        const FDescriptorFlagBit kOrientedFlags[] = {
            {0xF9FEBF0Du, 0x40},
            {0xE2F60EEBu, 0x8},
            {0xF3D52EE4u, 0x1000},
            {0xE8DC7F5Eu, 0x2000},
        };
        const FDescriptorFlagBit kDustFlags[] = {
            {0xE2F60EEBu, 0x8},
            {0xF3D52EE4u, 0x1000},
            {0xE8DC7F5Eu, 0x2000},
        };
        // 0x03503B61 / 0xE2F60EEB / 0x0ABE0402 share the tail that stores 0 (getter
        // 0x06204b8c) or leaves Flags alone (setter 0x06204bc0) and returns 0.
        const FDescriptorFlagBit kBeamFlags[] = {
            {0x08508C35u, 0x200}, {0xE8B8CDE7u, 0x100}, {0x1C65B7B9u, 0x400},
            {0xF87B5FD5u, 0x2},   {0x03503B61u, 0x0},   {0xE2F60EEBu, 0x0},
            {0x0ABE0402u, 0x0},
        };

        // Kind 0x200 getters (+0x3c; outer offsets of the fields in the comments): basic
        // FUN_06215b50, rect FUN_06237e20, perp FUN_062354d0, oriented FUN_0622b990, dust
        // FUN_06212010, beam FUN_06204cb0.
        const FBillboardRampSlot kBasicSingleRamps[] = {
            {0x04FC9016u, &FSet::SizeRamp},           // +0x70
            {0xF7C2EBA9u, &FSet::PrimarySizeRamp},    // +0x78
            {0x05DB630Eu, &FSet::TexFrameRamp},       // +0x7c
            {0x015A6F47u, &FSet::AxisOverrideRamp},   // +0x80
            {0xE0E97650u, &FSet::CommonTexFrameRamp}, // +0x88
        };
        const FBillboardRampSlot kRectSingleRamps[] = {
            {0x04FC9016u, &FSet::SizeRamp},         // +0x70
            {0xFBA203B8u, &FSet::PrimarySizeRamp},  // +0x78
            {0xE48DDAE2u, &FSet::TexFrameRamp},     // +0x7c
            {0x1266735Fu, &FSet::AxisOverrideRamp}, // +0x80
            {0x015A6F47u, &FSet::FrameRamp},        // +0x84
        };
        const FBillboardRampSlot kPerpSingleRamps[] = {
            {0x04FC9016u, &FSet::SizeRamp},        // +0x70
            {0xF7C2EBA9u, &FSet::PrimarySizeRamp}, // +0x78
            {0x015A6F47u, &FSet::TexFrameRamp},    // +0x7c
        };
        const FBillboardRampSlot kOrientedSingleRamps[] = {
            {0x04FC9016u, &FSet::SizeRamp},         // +0x70
            {0xFBA203B8u, &FSet::PrimarySizeRamp},  // +0x78
            {0xF267B8E1u, &FSet::TexFrameRamp},     // +0x7c
            {0xFA03992Fu, &FSet::AxisOverrideRamp}, // +0x80
            {0x015A6F47u, &FSet::FrameRamp},        // +0x84
        };
        const FBillboardRampSlot kBeamSingleRamps[] = {
            {0x04FC9016u, &FSet::SizeRamp},           // +0x70
            {0xFBA203B8u, &FSet::PrimarySizeRamp},    // +0x78
            {0xE48DDAE2u, &FSet::CommonTexFrameRamp}, // +0x88
            {0x1266735Fu, &FSet::TexFrameRamp},       // +0x7c
            {0x015A6F47u, &FSet::FrameRamp},          // +0x84
        };

        // Kind 0x202 getters (+0x40), BasicApp_CommonTexFrame 0xF52B8DD5 in all six: basic
        // FUN_06215900 (+0x84), rect / oriented (+0x88), perp / dust / beam (+0x80).
        const FBillboardRampSlot kBasicAnimatedRamps[] = {{0xF52B8DD5u, &FSet::FrameRamp}};
        const FBillboardRampSlot kCommonTexFrameAnimatedRamps[] = {
            {0xF52B8DD5u, &FSet::CommonTexFrameRamp}};
        const FBillboardRampSlot kAxisOverrideAnimatedRamps[] = {
            {0xF52B8DD5u, &FSet::AxisOverrideRamp}};

        // The class tables (+0x54, after the base table): basic 0x06255b70, rect
        // 0x06256aa0, perp 0x06256890, oriented 0x062565b8, dust 0x06255918, beam
        // 0x062555c0, particle 0x06256780, mesh 0x06256298.
        const FNodePropertyRow kBasicPropertyRows[] = {
            {"Appearance_LODCurve", 0x0C53B008U, 0x200, 0.0f, 100.0f},
            {"BasicApp_TriTexture", 0xF863872EU, 0x1, 0.0f, 0.0f},
            {"BasicApp_QuadTexture", 0xF87B5FD5U, 0x1, 0.0f, 0.0f},
            {"BasicApp_MotionBlur", 0xE91467F1U, 0x1, 0.0f, 0.0f},
            {"BasicApp_Color", 0x048767E8U, 0x201, 0.0f, 1.0f},
            {"BasicApp_Alpha", 0x04FC9016U, 0x200, 0.0f, 1.0f},
            {"BasicApp_Size", 0xF7C2EBA9U, 0x200, 0.0f, kNodePropertyRowMax},
            {"BasicApp_HToVAspect", 0xE0E97650U, 0x200, 0.0f, kNodePropertyRowMax},
            {"BasicApp_Rotate", 0x05DB630EU, 0x200, -kNodePropertyRowMax, kNodePropertyRowMax},
            {"BasicApp_TexName", 0x1BA23359U, 0x103, 0.0f, 0.0f},
            {"BasicApp_BlendInfo", 0x1506EB6CU, 0x104, 0.0f, 0.0f},
            {"BasicApp_UseCommonTexFrame", 0xE2F60EEBU, 0x1, 0.0f, 0.0f},
            {"BasicApp_TexFrame", 0x015A6F47U, 0x200, 0.0f, 1.0f},
            {"BasicApp_CommonTexFrame", 0xF52B8DD5U, 0x202, 0.0f, 1.0f},
            {"BasicApp_FlipTexU", 0xF3D52EE4U, 0x1, 0.0f, 0.0f},
            {"BasicApp_FlipTexV", 0xE8DC7F5EU, 0x1, 0.0f, 0.0f},
            {nullptr, 0, 0, 0.0f, 0.0f},
        };
        const FNodePropertyRow kRectPropertyRows[] = {
            {"Appearance_LODCurve", 0x0C53B008U, 0x200, 0.0f, 100.0f},
            {"BasicApp_MotionBlur", 0xE91467F1U, 0x1, 0.0f, 0.0f},
            {"RectApp_CenterOnPos", 0xFB7958C1U, 0x1, 0.0f, 0.0f},
            {"RectApp_ViewingAngleFade", 0xF9FEBF0DU, 0x1, 0.0f, 0.0f},
            {"BasicApp_Color", 0x048767E8U, 0x201, 0.0f, 1.0f},
            {"BasicApp_Alpha", 0x04FC9016U, 0x200, 0.0f, 1.0f},
            {"RectApp_Scale", 0xFBA203B8U, 0x200, 0.0f, kNodePropertyRowMax},
            {"RectApp_Length", 0xE48DDAE2U, 0x200, 0.0f, kNodePropertyRowMax},
            {"RectApp_Width", 0x1266735FU, 0x200, 0.0f, kNodePropertyRowMax},
            {"BasicApp_TexName", 0x1BA23359U, 0x103, 0.0f, 0.0f},
            {"BasicApp_BlendInfo", 0x1506EB6CU, 0x104, 0.0f, 0.0f},
            {"BasicApp_UseCommonTexFrame", 0xE2F60EEBU, 0x1, 0.0f, 0.0f},
            {"BasicApp_TexFrame", 0x015A6F47U, 0x200, 0.0f, 1.0f},
            {"BasicApp_CommonTexFrame", 0xF52B8DD5U, 0x202, 0.0f, 1.0f},
            {"BasicApp_FlipTexU", 0xF3D52EE4U, 0x1, 0.0f, 0.0f},
            {"BasicApp_FlipTexV", 0xE8DC7F5EU, 0x1, 0.0f, 0.0f},
            {nullptr, 0, 0, 0.0f, 0.0f},
        };
        const FNodePropertyRow kPerpPropertyRows[] = {
            {"Appearance_LODCurve", 0x0C53B008U, 0x200, 0.0f, 100.0f},
            {"RectApp_ViewingAngleFade", 0xF9FEBF0DU, 0x1, 0.0f, 0.0f},
            {"BasicApp_Color", 0x048767E8U, 0x201, 0.0f, 1.0f},
            {"BasicApp_Alpha", 0x04FC9016U, 0x200, 0.0f, 1.0f},
            {"BasicApp_Size", 0xF7C2EBA9U, 0x200, 0.0f, kNodePropertyRowMax},
            {"BasicApp_TexName", 0x1BA23359U, 0x103, 0.0f, 0.0f},
            {"BasicApp_BlendInfo", 0x1506EB6CU, 0x104, 0.0f, 0.0f},
            {"BasicApp_UseCommonTexFrame", 0xE2F60EEBU, 0x1, 0.0f, 0.0f},
            {"BasicApp_TexFrame", 0x015A6F47U, 0x200, 0.0f, 1.0f},
            {"BasicApp_CommonTexFrame", 0xF52B8DD5U, 0x202, 0.0f, 1.0f},
            {"BasicApp_FlipTexU", 0xF3D52EE4U, 0x1, 0.0f, 0.0f},
            {"BasicApp_FlipTexV", 0xE8DC7F5EU, 0x1, 0.0f, 0.0f},
            {nullptr, 0, 0, 0.0f, 0.0f},
        };
        const FNodePropertyRow kOrientedPropertyRows[] = {
            {"Appearance_LODCurve", 0x0C53B008U, 0x200, 0.0f, 100.0f},
            {"RectApp_ViewingAngleFade", 0xF9FEBF0DU, 0x1, 0.0f, 0.0f},
            {"MeshApp_ParticleTransform", 0xF0004D58U, 0x105, 0.0f, 0.0f},
            {"BasicApp_Color", 0x048767E8U, 0x201, 0.0f, 1.0f},
            {"BasicApp_Alpha", 0x04FC9016U, 0x200, 0.0f, 1.0f},
            {"RectApp_Scale", 0xFBA203B8U, 0x200, 0.0f, kNodePropertyRowMax},
            {"OrientedApp_Height", 0xF267B8E1U, 0x200, 0.0f, kNodePropertyRowMax},
            {"OrientedApp_Width", 0xFA03992FU, 0x200, 0.0f, kNodePropertyRowMax},
            {"BasicApp_TexName", 0x1BA23359U, 0x103, 0.0f, 0.0f},
            {"BasicApp_BlendInfo", 0x1506EB6CU, 0x104, 0.0f, 0.0f},
            {"BasicApp_UseCommonTexFrame", 0xE2F60EEBU, 0x1, 0.0f, 0.0f},
            {"BasicApp_TexFrame", 0x015A6F47U, 0x200, 0.0f, 1.0f},
            {"BasicApp_CommonTexFrame", 0xF52B8DD5U, 0x202, 0.0f, 1.0f},
            {"BasicApp_FlipTexU", 0xF3D52EE4U, 0x1, 0.0f, 0.0f},
            {"BasicApp_FlipTexV", 0xE8DC7F5EU, 0x1, 0.0f, 0.0f},
            {nullptr, 0, 0, 0.0f, 0.0f},
        };
        const FNodePropertyRow kDustPropertyRows[] = {
            {"BasicApp_Color", 0x048767E8U, 0x201, 0.0f, 1.0f},
            {"BasicApp_Alpha", 0x04FC9016U, 0x200, 0.0f, 1.0f},
            {"BasicApp_Size", 0xF7C2EBA9U, 0x200, 0.0f, kNodePropertyRowMax},
            {"BasicApp_TexName", 0x1BA23359U, 0x103, 0.0f, 0.0f},
            {"BasicApp_BlendInfo", 0x1506EB6CU, 0x104, 0.0f, 0.0f},
            {"BasicApp_UseCommonTexFrame", 0xE2F60EEBU, 0x1, 0.0f, 0.0f},
            {"BasicApp_TexFrame", 0x015A6F47U, 0x200, 0.0f, 1.0f},
            {"BasicApp_CommonTexFrame", 0xF52B8DD5U, 0x202, 0.0f, 1.0f},
            {"BasicApp_FlipTexU", 0xF3D52EE4U, 0x1, 0.0f, 0.0f},
            {"BasicApp_FlipTexV", 0xE8DC7F5EU, 0x1, 0.0f, 0.0f},
            {nullptr, 0, 0, 0.0f, 0.0f},
        };
        const FNodePropertyRow kBeamPropertyRows[] = {
            {"Appearance_LODCurve", 0x0C53B008U, 0x200, 0.0f, 100.0f},
            {"BasicApp_Color", 0x048767E8U, 0x201, 0.0f, 1.0f},
            {"BasicApp_Alpha", 0x04FC9016U, 0x200, 0.0f, 1.0f},
            {"RectApp_Scale", 0xFBA203B8U, 0x200, 0.0f, 1000.0f},
            {"RectApp_Width", 0x1266735FU, 0x200, 0.0f, 1001.0f},
            {"BasicApp_TexName", 0x1BA23359U, 0x103, 0.0f, 0.0f},
            {"BasicApp_BlendInfo", 0x1506EB6CU, 0x104, 0.0f, 0.0f},
            {"BasicApp_TexFrame", 0x015A6F47U, 0x200, 0.0f, 1.0f},
            {"BasicApp_QuadTexture", 0xF87B5FD5U, 0x1, 0.0f, 0.0f},
            {"BeamApp_DisablePlaceHolder", 0x08508C35U, 0x1, 0.0f, 0.0f},
            {"BeamApp_DupeFirstParticle", 0xE8B8CDE7U, 0x1, 0.0f, 0.0f},
            {"BeamApp_LineAppearance", 0x1C65B7B9U, 0x1, 0.0f, 0.0f},
            {nullptr, 0, 0, 0.0f, 0.0f},
        };
        const FNodePropertyRow kParticlePropertyRows[] = {
            {"ParticleApp_LifeName", 0x0EC7A290U, 0x103, 0.0f, 0.0f},
            {"ParticleApp_DeathName", 0x03B76E6CU, 0x103, 0.0f, 0.0f},
            {"ParticleApp_UseDynamicRotation", 0xE34C3C55U, 0x1, 0.0f, 0.0f},
            {"ParticleApp_SmoothRotation", 0x049A6DBEU, 0x1, 0.0f, 0.0f},
            {nullptr, 0, 0, 0.0f, 1.0f},
        };
        const FNodePropertyRow kMeshPropertyRows[] = {
            {"MeshApp_MeshId", 0x0B7EE41CU, 0x2, 0.0f, 0.0f},
            {"MeshApp_MeshName", 0x18DADCA8U, 0x103, 0.0f, 0.0f},
            {"MeshApp_UseParticleTransform", 0x001FC112U, 0x1, 0.0f, 0.0f},
            {"MeshApp_ParticleTransform", 0xF0004D58U, 0x105, 0.0f, 0.0f},
            {nullptr, 0, 0, 0.0f, 0.0f},
        };

        template <typename T, unsigned long N>
        constexpr unsigned long CountOf(const T (&)[N])
        {
            return N;
        }

        // +0x50: basic / rect / perp / dust are FUN_0623fb50 itself; oriented FUN_0622e640
        // (0xF0004D58) and beam FUN_0620db40 (0x0BA0B3BB) hand out LocalTransform first.
        const FBillboardPropertyClass kBasicClass = {
            kBasicFlags, CountOf(kBasicFlags), kBasicSingleRamps, CountOf(kBasicSingleRamps),
            kBasicAnimatedRamps, CountOf(kBasicAnimatedRamps), 0, kBasicPropertyRows};
        const FBillboardPropertyClass kRectClass = {
            kRectFlags, CountOf(kRectFlags), kRectSingleRamps, CountOf(kRectSingleRamps),
            kCommonTexFrameAnimatedRamps, CountOf(kCommonTexFrameAnimatedRamps), 0,
            kRectPropertyRows};
        const FBillboardPropertyClass kPerpClass = {
            kPerpFlags, CountOf(kPerpFlags), kPerpSingleRamps, CountOf(kPerpSingleRamps),
            kAxisOverrideAnimatedRamps, CountOf(kAxisOverrideAnimatedRamps), 0,
            kPerpPropertyRows};
        const FBillboardPropertyClass kOrientedClass = {
            kOrientedFlags, CountOf(kOrientedFlags), kOrientedSingleRamps,
            CountOf(kOrientedSingleRamps), kCommonTexFrameAnimatedRamps,
            CountOf(kCommonTexFrameAnimatedRamps), 0xF0004D58u, kOrientedPropertyRows};
        const FBillboardPropertyClass kDustClass = {
            kDustFlags, CountOf(kDustFlags), kPerpSingleRamps, CountOf(kPerpSingleRamps),
            kAxisOverrideAnimatedRamps, CountOf(kAxisOverrideAnimatedRamps), 0,
            kDustPropertyRows};
        const FBillboardPropertyClass kBeamClass = {
            kBeamFlags, CountOf(kBeamFlags), kBeamSingleRamps, CountOf(kBeamSingleRamps),
            kAxisOverrideAnimatedRamps, CountOf(kAxisOverrideAnimatedRamps), 0x0BA0B3BBu,
            kBeamPropertyRows};

        // Particle FUN_06231930 / FUN_06231980 and the names FUN_06231ad0 / FUN_06231b20.
        const unsigned long kParticleUseDynamicRotationHash = 0xE34C3C55u;
        const unsigned long kParticleSmoothRotationHash = 0x049A6DBEu;
        const unsigned long kParticleLifeNameHash = 0x0EC7A290u;
        const unsigned long kParticleDeathNameHash = 0x03B76E6Cu;
        // The billboard sets' shared hashes.
        const unsigned long kBillboardTexNameHash = 0x1BA23359u;
        const unsigned long kBillboardBlendInfoHash = 0x1506EB6Cu;
        const unsigned long kBillboardColorHash = 0x048767E8u;
        // DAT_06255804: the name every texture key and the TexName getter hand out.
        const char kTexNameUnknown[] = "???";
        // Mesh FUN_06226b80.. (set 0x0624dff0).
        const unsigned long kMeshUseParticleTransformHash = 0x001FC112u;
        const unsigned long kMeshIdHash = 0x0B7EE41Cu;
        const unsigned long kMeshNameHash = 0x18DADCA8u;
        const unsigned long kMeshParticleTransformHash = 0xF0004D58u;
    } // namespace

    // ---- FFxBillboardPropertySet ----

    FFxBillboardPropertySet::FFxBillboardPropertySet(const FBillboardPropertyClass* PropertyClass)
        : PropertyClass_(PropertyClass)
    {
    }

    // Bool get / set (basic FUN_06215930 / FUN_06215a00, beam FUN_06204af0 / FUN_06204ba0,
    // dust 06211e10 / 06211e70, oriented 0622e680 / 0622b8a0, perp 06235320 / 06232730,
    // rect 06237c00 / 06237cd0): the first row with the hash reads its Flags (+0x60) bit
    // into the byte (`SHR; AND CL,1`) or sets / clears it on the value's low byte (`TEST
    // AL,AL`), returning 0; no row -8. A row with mask 0 (the beam's 0x03503B61 /
    // 0xE2F60EEB / 0x0ABE0402) reads 0 and ignores a set.
    GENRESULT DACOM_API FFxBillboardPropertySet::GetPropBool(unsigned long Hash, unsigned char* Out)
    {
        for (unsigned long Index = 0; Index < PropertyClass_->FlagCount; ++Index)
        {
            const FDescriptorFlagBit& Row = PropertyClass_->Flags[Index];
            if (Row.Hash != Hash)
                continue;
            *Out = (Flags & Row.Mask) != 0 ? 1 : 0;
            return GR_OK;
        }
        return GR_ALE_CREATE_FAIL;
    }

    GENRESULT DACOM_API FFxBillboardPropertySet::SetPropBool(unsigned long Hash, unsigned char Value)
    {
        for (unsigned long Index = 0; Index < PropertyClass_->FlagCount; ++Index)
        {
            const FDescriptorFlagBit& Row = PropertyClass_->Flags[Index];
            if (Row.Hash != Hash)
                continue;
            if (Value != 0)
                Flags |= Row.Mask;
            else
                Flags &= ~Row.Mask;
            return GR_OK;
        }
        return GR_ALE_CREATE_FAIL;
    }

    // Basic FUN_06218b60 / FUN_06218b90 (beam 0620dc00 / 0620dc30, dust 06211f50 /
    // 06211f80, oriented 0622e770 / 0622e7a0, perp 06235410 / 06235440, rect 0623a9d0 /
    // 0623aa00): BlendInfo -> copy the two dwords, 0; else -8.
    GENRESULT DACOM_API FFxBillboardPropertySet::GetPropPair(unsigned long Hash, unsigned long* Out2)
    {
        if (Hash != kBillboardBlendInfoHash)
            return GR_ALE_CREATE_FAIL;
        Out2[0] = BlendInfo[0];
        Out2[1] = BlendInfo[1];
        return GR_OK;
    }

    GENRESULT DACOM_API FFxBillboardPropertySet::SetPropPair(unsigned long Hash,
                                                             const unsigned long* In2)
    {
        if (Hash != kBillboardBlendInfoHash)
            return GR_ALE_CREATE_FAIL;
        BlendInfo[0] = In2[0];
        BlendInfo[1] = In2[1];
        return GR_OK;
    }

    // Basic FUN_06218c60 (0x06218c60..0x06218c89; beam 0620dd00, dust 062120c0, oriented
    // 0622e870, perp 06235580, rect 0623aad0): TexName -> *Out = "???", 0; else
    // FUN_0623f9a0.
    GENRESULT DACOM_API FFxBillboardPropertySet::GetPropName(unsigned long Hash, const char** Out)
    {
        if (Hash == kBillboardTexNameHash)
        {
            *Out = kTexNameUnknown;
            return GR_OK;
        }
        return FFxNodePropertySet::GetPropName(Hash, Out);
    }

    // Basic FUN_06218c90 (and 0x06204da4, 0x0620f454, 0x0622ba84, 0x062328c4, 0x06237f14):
    // TexName -> [+0x64] = DACOM_CRC::GetCRC32(Name), 0; else FUN_0623f9e0.
    GENRESULT DACOM_API FFxBillboardPropertySet::SetPropName(unsigned long Hash, const char* Text)
    {
        if (Hash == kBillboardTexNameHash)
        {
            TextureCrc = DACOM_CRC::GetCRC32(Text);
            return GR_OK;
        }
        return FFxNodePropertySet::SetPropName(Hash, Text);
    }

    // Basic FUN_06218bf0 (0x06218bf0..0x06218c17; beam 0620dc90, dust 06211fe0, oriented
    // 0622e800, perp 062354a0, rect 0623aa60): Color -> *Out = [+0x74], AddRef, 0; else -8.
    GENRESULT DACOM_API FFxBillboardPropertySet::GetPropColorRampRamp(unsigned long Hash,
                                                                      IRampColorRamp** Out)
    {
        if (Hash != kBillboardColorHash)
            return GR_ALE_CREATE_FAIL;
        *Out = ColorRamp;
        (*Out)->AddRef();
        return GR_OK;
    }

    // The class's 0x200 getter: `CMP EAX,hash; JNZ next` per case, the hit copies the
    // field to *Out and AddRefs it (+4), returns 0; past the last case it jumps to
    // FUN_06244330.
    GENRESULT DACOM_API FFxBillboardPropertySet::GetPropSingleRampRamp(unsigned long Hash,
                                                                       INestedSingleRamp** Out)
    {
        for (unsigned long Index = 0; Index < PropertyClass_->SingleRampCount; ++Index)
        {
            const FBillboardRampSlot& Slot = PropertyClass_->SingleRamps[Index];
            if (Slot.Hash != Hash)
                continue;
            *Out = this->*Slot.Field;
            (*Out)->AddRef();
            return GR_OK;
        }
        return FFxAppearancePropertySet::GetPropSingleRampRamp(Hash, Out);
    }

    // The class's 0x202 getter (basic FUN_06215900): the one hash -> field AddRef'd, 0;
    // else -8.
    GENRESULT DACOM_API FFxBillboardPropertySet::GetPropAnimatedSingleRamp(unsigned long Hash,
                                                                           INestedSingleRamp** Out)
    {
        for (unsigned long Index = 0; Index < PropertyClass_->AnimatedRampCount; ++Index)
        {
            const FBillboardRampSlot& Slot = PropertyClass_->AnimatedRamps[Index];
            if (Slot.Hash != Hash)
                continue;
            *Out = this->*Slot.Field;
            (*Out)->AddRef();
            return GR_OK;
        }
        return GR_ALE_CREATE_FAIL;
    }

    // Beam FUN_0620db40 (0x0620db40..0x0620db72; oriented FUN_0622e640): the class hash ->
    // [inner+0x88] (LocalTransform) AddRef'd, 0; any other hash JMPs FUN_0623fb50.
    GENRESULT DACOM_API FFxBillboardPropertySet::GetPropTransform(unsigned long Hash,
                                                                  IAnimatedTransform** Out)
    {
        if (PropertyClass_->LocalTransformHash != 0 && Hash == PropertyClass_->LocalTransformHash)
        {
            *Out = LocalTransform;
            (*Out)->AddRef();
            return GR_OK;
        }
        return FFxNodePropertySet::GetPropTransform(Hash, Out);
    }

    // Basic FUN_062158d0 / FUN_06218cd0 (beam FUN_0620dd70, dust FUN_06212130, oriented
    // FUN_0622e8e0, perp FUN_062355f0, rect FUN_0623ab40): FUN_06244320 (the base table);
    // a non-zero result is returned, else the class table (FUN_0623f920).
    int DACOM_API FFxBillboardPropertySet::EnumerateProps(FNodePropertyRowCallback Callback,
                                                          void* Context)
    {
        const int Result = FFxNodePropertySet::EnumerateProps(Callback, Context);
        if (Result != 0)
            return Result;
        return WalkPropertyRows(PropertyClass_->Rows, Callback, Context);
    }

    // ---- FFxBillboardAppearance ----

    FFxBillboardAppearance::FFxBillboardAppearance(const char* PrimaryIid, const char* FactoryName,
                                                   const FBillboardPropertyClass* PropertyClass)
        : FGenericFxObjectBase(PrimaryIid, FactoryName), PropertyClass_(PropertyClass)
    {
    }

    // Retail's draws read the set through node+0xf0 (dust +0xe8), the creators' cache of
    // the set pointer.
    void FFxBillboardAppearance::OnPropertySetChanged()
    {
        Descriptor_ = static_cast<FAppearanceRampDescriptor*>(PropertySet_);
    }

    // Defined with the other descriptor builders further down.
    static void BuildBasicDescriptorRamps(FAppearanceRampDescriptor& Descriptor);
    static void BuildRectDescriptorRamps(FAppearanceRampDescriptor& Descriptor);
    static void BuildPerpDescriptorRamps(FAppearanceRampDescriptor& Descriptor);
    static void BuildOrientedDescriptorRamps(FAppearanceRampDescriptor& Descriptor);
    static void BuildBeamDescriptorRamps(FAppearanceRampDescriptor& Descriptor);

    // The six billboard creators (basic FUN_06215e80, rect FUN_06238150, perp FUN_06232b00,
    // oriented FUN_0622bcc0, dust FUN_0620f6b0, beam FUN_06205000): operator_new, the
    // class's builder, the outer vtable and count 1, then the shared adoption tail (Release
    // the old +0x7c, store and AddRef, Release the creation reference; basic 0x06215f1f..
    // 0x06215f2b caches the set at +0xf0 and its inner at +0xa8 and +0x80).
    void FFxBillboardAppearance::AdoptBillboardSet(void (*Build)(FAppearanceRampDescriptor&))
    {
        FFxBillboardPropertySet* const Set = new FFxBillboardPropertySet(PropertyClass_);
        Build(*Set);
        AdoptPropertySet(Set);
    }

    FxBasicAppearance::FxBasicAppearance(const char* PrimaryIid, const char* FactoryName)
        : FFxBillboardAppearance(PrimaryIid, FactoryName, &kBasicClass)
    {
    }

    void FxBasicAppearance::CreatePropertySet()
    {
        AdoptBillboardSet(&BuildBasicDescriptorRamps);
    }

    // FUN_06215d80 (RET 8; this = the prototype's node): operator_new and the basic
    // constructor inline (the stores of its factory 0x06218246..), FUN_06244120
    // (0x06215e46), the +0xf0 set cache (0x06215e4b..0x06215e55, Descriptor_ here); *Out =
    // the new outer, 0.
    GENRESULT DACOM_API FxBasicAppearance::CloneNode(IDAComponent** Out)
    {
        FxBasicAppearance* const Node = new (std::nothrow) FxBasicAppearance(PrimaryIid_, FactoryName_);
        Node->InitAppearanceFromArchetype(this);
        // 06215e4b..5e55 reloads the prototype cache after initialization callbacks.
        Node->Descriptor_ = Descriptor_;
        *Out = Node;
        return GR_OK;
    }

    FxBasicAppearance::~FxBasicAppearance()
    {
    }

    // Outer +0x14 -> FUN_06216420. See FGenericFxObjectBase::UpdateListAppearance.
    GENRESULT DACOM_API FxBasicAppearance::Pad5(float Dt)
    {
        G_BasicDrawContext.Field_0xA = 0;
        G_BasicDrawContext.PrimCount = 0;
        Age_ = static_cast<float>(static_cast<double>(Dt) + Age_);
        if (EmitterRefCount_ == 0 && InstanceListHead_ == nullptr)
            NodeFlags_ |= 1;
        const FFxTextureKey Key = {"???", Descriptor_->TextureCrc};
        Texture_.SetTexture(&Key);
        if (Texture_.Handle == -1)
            Texture_.Resolve();
        if ((Descriptor_->Flags & 8u) != 0)
        {
            const float Position = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->FrameRamp, AppearanceOuterTime_, Age_));
            if (Texture_.FrameCount != 1 && Texture_.Handle != -1)
            {
                const std::uint32_t Span = Texture_.FrameCount - 1u;
                const std::int64_t Frame = static_cast<std::int64_t>(static_cast<double>(Span) * Position);
                static_cast<ITextureLibrary2*>(G_TextureLibrary)->GetFrame(Texture_.Handle,
                    static_cast<int>(Frame), reinterpret_cast<unsigned long*>(Texture_.Frame));
            }
        }
        else if (Texture_.FrameCount != 1)
            Texture_.Advance(Dt);
        ApplyFieldsToPool(this, false);
        IntegratePool(this, &G_PoolBasic, Dt);
        return GR_OK;
    }

    // FUN_06216090, the basic appearance's outer +0x8C (FUN_06219320: `ADD this,0xc;
    // JMP`), RET 8 -- GetBoundingSphere(Out). CORRECTED 2026-09-26; the history
    // below this paragraph is superseded where it disagrees.
    //   0x062160bb: empty list (begin == end) -> return -1, Out untouched.
    //   0x062160e9: Scale = P+0x5c (EffectScaleRamp) @ (node+0x8c, node+0x90), kept
    //     in [ESP+0x14] -- NOT discarded. node+0x90 is LodInput_, not a constant.
    //   0x0621610f: first particle: radius = P+0x78 (PrimarySizeRamp) @ (node+0x8c,
    //     Age * InvLife) * Scale; Out = {pos, radius}.
    //   0x06216158..: every further particle, same radius, merged into Out.
    //   0x062163b8: Out.center = node+0x4c transform of Out.center (FUN_06208690);
    //     radius copied through (FUN_06208a70); return 0.
    // It used to run as UpdateCachedBoundsSphere after every integrate, over the
    // private list nothing drew, into a cache nothing read, with Scale replaced by a
    // constant 1.0 (EffectOuterScale_) -- wrong on each count.
    //
    // The list TAIL ([list+4], FUN_06217be0) is merged
    // right after the first particle (0x06216164..0x0621627e, with the tail's own t), and
    // only then first->next .. tail-1 in order; and the merge is the listing's (FUN_06206ad0
    // rsqrt length, strict compares, double 0.5 / 1e-06), not MergeBoundingSphere.
    GENRESULT DACOM_API FxBasicAppearance::GetBoundingSphere(float* OutSphere)
    {
        const FPooledParticle* const Head = InstanceListHead_;
        if (Head == nullptr)
            return GR_GENERIC;
        const float Scale = static_cast<float>(ForwardAppearanceSingleEval(
            Descriptor_->EffectScaleRamp, AppearanceOuterTime_, LodInput_));
        const float FirstInnerTime = static_cast<float>(static_cast<double>(Head->InvDuration) * Head->Age);
        const double FirstRadius = ForwardAppearanceSingleEval(
            Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, FirstInnerTime) * Scale;
        std::memcpy(OutSphere, &Head->PosX, sizeof(float) * 2);
        OutSphere[3] = static_cast<float>(FirstRadius);
        std::memcpy(OutSphere + 2, &Head->PosZ, sizeof(float));
        const FPooledParticle* const Tail = InstanceListTail_;
        if (Head != Tail)
        {
            const float TailInnerTime = static_cast<float>(static_cast<double>(Tail->InvDuration) * Tail->Age);
            const double TailRadius = ForwardAppearanceSingleEval(
                Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, TailInnerTime) * Scale;
            float TailSphere[4];
            std::memcpy(TailSphere, &Tail->PosX, sizeof(float) * 2);
            TailSphere[3] = static_cast<float>(TailRadius);
            std::memcpy(TailSphere + 2, &Tail->PosZ, sizeof(float));
            MergePerpRectBoundsSphere(OutSphere, TailSphere, TailSphere[3],
                EPerpRectBoundsDistance::PerpUnroundedXyz);
            const FPooledParticle* Particle = Head->Next;
            for (; Particle != Tail; Particle = Particle->Next)
            {
                const float InnerTime = static_cast<float>(static_cast<double>(Particle->InvDuration) * Particle->Age);
                const double Radius = ForwardAppearanceSingleEval(
                    Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, InnerTime) * Scale;
                float Sphere[4];
                std::memcpy(Sphere, &Particle->PosX, sizeof(float) * 2);
                Sphere[3] = static_cast<float>(Radius);
                std::memcpy(Sphere + 2, &Particle->PosZ, sizeof(float));
                MergePerpRectBoundsSphere(OutSphere, Sphere, Sphere[3],
                    EPerpRectBoundsDistance::PerpUnroundedXyz);
            }
        }
        FVector WorldCenter;
        TransformPerpBoundsCenter(*reinterpret_cast<const FTransform*>(NodeWorldXform_),
            WorldCenter, *reinterpret_cast<const FVector*>(OutSphere));
        float WorldSphere[4];
        std::memcpy(WorldSphere, &WorldCenter, sizeof(WorldCenter));
        std::memcpy(WorldSphere + 3, OutSphere + 3, sizeof(float));
        std::memcpy(OutSphere, WorldSphere, sizeof(WorldSphere));
        return GR_OK;
    }

    // Outer +0xBC -> FUN_06215f40 (LAB_06219640 is the `ADD this,0xc; JMP`). The
    // body is FGenericFxObjectBase::SpawnListParticle; basic adds nothing to it.
    GENRESULT DACOM_API FxBasicAppearance::Pad47(const float* Six, float Age, float Duration)
    {
        SpawnListParticle(&G_PoolBasic, Six, Age, Duration);
        return GR_OK;
    }

    // FUN_06217640 (node vtable 0x0624cb30), RET 4, always returns 0. Rewritten 2026-09-26
    // from the listing; the previous version filled through FillPoolParticles (a world-space
    // lookalike walking the whole pool), hardcoded the blend pair and bound TextureHandle_
    // (+0xb8) instead of the dword at +0xbc.
    //   0x0621766b: begin/end of the list at node+0xd8; equal -> return.
    //   0x06217699: DAT_06257919 (G_Defaults.bUseMaterialBatcher) set ->
    //     FUN_06201c70(0, [node+0xac]) = EnqueueAppearanceToList(0, this), result ignored.
    //   0x062176d0: counts from [node+0xac] vt+0xd0/+0xd4, then FUN_06240f50(DAT_062579dc,
    //     4, verts, indices); false -> return.
    //   0x06217708: ++DAT_062578f0 (G_DrawCounters[3]).
    //   0x0621771d: descriptor flag 2 -> FUN_06216550, else FUN_06216f20.
    //   0x06217738: FUN_06241070 (end) == 0 -> return; else ++DAT_062578e8
    //     (G_DrawCounters[1]).
    //   0x06217750: render states 0x13/0x14 from descriptor +0x68/+0x6c (the set's
    //     BlendInfo), SetTexture(0, [node+0xbc]), SetModelview with an identity
    //     12-float transform (1.0 at [ESP+0x14]/+0x24/+0x34 = dwords 0/4/8: the fills already
    //     produced camera-space vertices), FUN_06241180 (draw).
    GENRESULT DACOM_API FxBasicAppearance::DrawOrEnqueue()
    {
        if (InstanceListHead_ == nullptr)
            return GR_OK;

        if (G_Defaults.bUseMaterialBatcher != 0)
        {
            EnqueueAppearanceToList(0, this);
            return GR_OK;
        }

        unsigned long VertCount = 0;
        unsigned long IndexCount = 0;
        GetQueuedVertCount(&VertCount);
        GetQueuedIndexCount(&IndexCount);
        if (!AlDrawContextBegin(G_BasicDrawContext, 4, VertCount, IndexCount))
            return GR_OK;

        ++G_DrawCounters[3];
        if ((Descriptor_->Flags & 2u) != 0)
            FillQuadGeometry();
        else
            FillTriangleGeometry();

        // 0x06217738 onward is the same sequence as the outer +0xA0 body FUN_062193b0.
        EndBatchedDraw();
        return GR_OK;
    }

    // LAB_06217940 (inner) / FUN_06219390 (outer +0x9C), RET 0xc:
    // FUN_06240f50(DAT_062579dc, 4, Verts, Indices),
    // true -> 0, false -> -1 (`NEG AL; SBB EAX,EAX; NEG EAX; DEC EAX`).
    GENRESULT DACOM_API FxBasicAppearance::BeginBatchedDraw(unsigned long Verts,
                                                           unsigned long Indices)
    {
        return AlDrawContextBegin(G_BasicDrawContext, 4, Verts, Indices) ? GR_OK : GR_GENERIC;
    }

    // FUN_062193b0 (outer +0xA0, RET 4), traced 2026-09-26 against 0x062193b0-0x062194a0.
    // ESI = outer, so [ESI+0xfc] = node+0xf0 (descriptor) and [ESI+0xc8] = node+0xbc:
    //   FUN_06241070 (end) == 0 -> return 0;
    //   ++DAT_062578e8 (G_DrawCounters[1]);
    //   SetRenderState(0x13, [desc+0x68]), SetRenderState(0x14, [desc+0x6c]) (vt+0xf0);
    //   SetTexture(0, [node+0xbc]) (vt+0x108);
    //   SetModelview (vt+0x7c) with the 12-float identity built at [ESP+4] (1.0 at dwords
    //     0/4/8, everything else 0);
    //   FUN_06241180 (draw); return 0.
    GENRESULT DACOM_API FxBasicAppearance::EndBatchedDraw()
    {
        if (AlDrawContextEnd(G_BasicDrawContext) == 0)
            return GR_OK;
        ++G_DrawCounters[1];

        G_RenderPipeline->SetRenderState(D3DRS_SRCBLEND, static_cast<int>(Descriptor_->BlendInfo[0]));
        G_RenderPipeline->SetRenderState(D3DRS_DESTBLEND, static_cast<int>(Descriptor_->BlendInfo[1]));

        // node+0xbc: the first dword of the frame block FUN_06244f30 fills (Texture_.Frame[0]).
        void* FrameTexture = nullptr;
        std::memcpy(&FrameTexture, &Texture_.Frame[0], sizeof(FrameTexture));
        G_RenderPipeline->SetTextureStageTexture(0, FrameTexture);

        const float IdentityXform12[12] = {
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 0.0f,
        };
        G_RenderPipeline->SetModelview(IdentityXform12);

        AlDrawContextDraw(G_BasicDrawContext);
        return GR_OK;
    }

    // Outer +0xA4 = LAB_062194b0 (`LEA ECX,[this+0xc]`), node body LAB_06217a60
    // (0x0624cb74): ++DAT_062578f0, then the same flag-2 dispatch as the immediate draw.
    // The batch flush FUN_06201cc0 calls it between its +0x9C begin and +0xA0 draw.
    GENRESULT DACOM_API FxBasicAppearance::FillBatchedGeometry()
    {
        ++G_DrawCounters[3];
        if ((Descriptor_->Flags & 2u) != 0)
            FillQuadGeometry();
        else
            FillTriangleGeometry();
        return GR_OK;
    }

    // FUN_06216550, __thiscall on the node, no stack arguments. Traced 2026-09-26 against
    // alchemy.dll.txt 0x06216550-0x06216f1b. Offsets below are ESP-relative after the
    // prologue.
    //   Scale = descriptor+0x5c ramp (OuterTime node+0x8c, LodInput node+0x90); exactly 0
    //     -> return (0x062165d5: TEST AH,0x44 / JNP taken only for C3 alone).
    //   bFrameLookup [0x17] = node+0xd0 != 1 && (flags & 8) == 0.
    //   bStreak [0x3f] = flags & 4.
    //   ViewWorld [0xc0] = FUN_06208640(camera vt+0x40, node+0x4c).
    //   Frame block [0x44..0x57] = the 5 dwords at node+0xbc {texture, U0, V0, U1, V1};
    //     flag 0x1000 swaps U0/U1, flag 0x2000 swaps V0/V1. LastFrame [0x74] = -1.
    //   Per particle (list order):
    //     C = TransformXform(ViewWorld, pos) (math vt+0x28); C.z > 0 -> next (0x062166dc:
    //       AND 0x4100 / JZ = strictly greater).
    //     t = InvLife * Age. Alpha = +0x70 ramp, Size = +0x78 ramp * Scale; Alpha < 0.01
    //       -> next (TEST AH,5 / JNP).
    //     Diffuse = +0x74 ramp vt+0x20 | ftol(Alpha * 255) << 24.
    //     Angle = +0x7c ramp; Aspect = +0x88 ramp; (cos, sin) = FUN_0623f6b0(Angle).
    //     bFrameLookup: frame = ftol(+0x80 ramp * (node+0xd0 - 1)); when it differs from
    //       LastFrame, remember it, FUN_06244f30(node+0xb4, frame, frame block) and redo
    //       the two flips.
    //     Streak (0x06216876): V = TransformMatrix(ViewWorld, vel) (vt+0x2c); N = C x V;
    //       U = N x C normalized with one Newton step (0x5f3759df); D = (V . U) *
    //       0x3d088880; B = C - D*U (x, y); Ux/Uy = U*Size; P = (-Uy, Ux)*Aspect.
    //       Corners (B-U-P, C+U-P, B-U+P, C+U+P) with UVs (U1,V0) (U1,V1) (U0,V0) (U0,V1).
    //     Plain (0x06216c4e): W = Size*(sin, cos), H = Size*Aspect*(cos, -sin); corners
    //       C+W+H, C+W-H, C-W+H, C-W-H with UVs (U1,V0) (U0,V0) (U1,V1) (U0,V1). z = C.z.
    //     Each vertex store is skipped while the vertex counter is past the requested
    //     count (JG), the skipped index being that count; the six indices (I3,I2,I1)
    //     (I1,I2,I0) are skipped when IndexCount + 5 > requested indices.
    // Every ramp is called through its +0x1C Eval(Outer, Inner) and the colour ramp through
    // +0x20 EvalPacked (0x0621675a), unchecked as in retail: FUN_06215570 builds them all.
    void FxBasicAppearance::FillQuadGeometry()
    {
        const FPooledParticle* Particle = InstanceListHead_;
        const double ScaleValue = ForwardAppearanceSingleEval(
            Descriptor_->EffectScaleRamp, AppearanceOuterTime_, LodInput_);
        const float Scale = static_cast<float>(ScaleValue);
        if (IsZeroBasicScale(ScaleValue))
            return;
        const bool bFrameLookup = Texture_.FrameCount != 1 && (Descriptor_->Flags & 8u) == 0;
        const bool bStreak = (Descriptor_->Flags & 4u) != 0;
        ICamera* const Camera = reinterpret_cast<ICamera*>(static_cast<std::uintptr_t>(G_DrawCookie));
        float ViewWorld[12];
        ComposeTransform(ViewWorld, Camera->GetViewTransform(), NodeWorldXform_);
        float FrameBlock[5];
        std::memcpy(FrameBlock, Texture_.Frame, sizeof(FrameBlock));
        FlipBasicAppearanceFrame(Descriptor_->Flags, FrameBlock);
        std::int32_t LastFrame = -1;
        for (; Particle != nullptr; Particle = Particle->Next)
        {
            if (G_BasicGeometry3DMath == nullptr)
                G_BasicGeometry3DMath = Create3DMath();
            float C[3];
            G_BasicGeometry3DMath->TransformXform(*reinterpret_cast<FVector*>(C),
                *reinterpret_cast<const FTransform*>(ViewWorld),
                *reinterpret_cast<const FVector*>(&Particle->PosX));
            if (C[2] > 0.0f)
                continue;
            float OuterTime;
            std::memcpy(&OuterTime, &AppearanceOuterTime_, sizeof(float));
            const float InnerTime = static_cast<float>(static_cast<double>(Particle->InvDuration) * Particle->Age);
            const float Alpha = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->SizeRamp, OuterTime, InnerTime));
            const float Size = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, InnerTime) * Scale);
            if (Alpha < FloatFromBits(0x3c23d70au))
                continue;
            const unsigned long Color = ForwardAppearanceColorEval(
                Descriptor_->ColorRamp, AppearanceOuterTime_, InnerTime);
            const std::int64_t AlphaInteger = static_cast<std::int64_t>(static_cast<double>(Alpha) * 255.0);
            const unsigned long Diffuse = Color | (static_cast<std::uint32_t>(AlphaInteger) << 24);
            const float Angle = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->TexFrameRamp, AppearanceOuterTime_, InnerTime));
            const float Aspect = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->CommonTexFrameRamp, AppearanceOuterTime_, InnerTime));
            float Trig[2];
            ForwardAppearanceSinCos(Angle, Trig);
            if (bFrameLookup)
            {
                const double FramePosition = ForwardAppearanceSingleEval(
                    Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, InnerTime);
                const std::uint32_t CountBits = Texture_.FrameCount - 1u;
                std::int32_t SignedCount;
                std::memcpy(&SignedCount, &CountBits, sizeof(SignedCount));
                const std::int64_t FrameInteger = static_cast<std::int64_t>(FramePosition * SignedCount);
                const std::int32_t Frame = static_cast<std::int32_t>(FrameInteger);
                if (Frame != LastFrame)
                {
                    LastFrame = Frame;
                    Texture_.LookupFrame(Frame, FrameBlock);
                    FlipBasicAppearanceFrame(Descriptor_->Flags, FrameBlock);
                }
            }
            float Bx = 0.0f;
            float By = 0.0f;
            float Ux;
            float Uy;
            float Px;
            float Py;
            if (bStreak)
            {
                if (G_BasicGeometry3DMath == nullptr)
                    G_BasicGeometry3DMath = Create3DMath();
                float V[3];
                G_BasicGeometry3DMath->TransformMatrix(*reinterpret_cast<FVector*>(V),
                    *reinterpret_cast<const FMatrix*>(ViewWorld),
                    *reinterpret_cast<const FVector*>(&Particle->VelX));
                const double N0 = static_cast<double>(C[1]) * V[2] - static_cast<double>(V[1]) * C[2];
                const double N1 = static_cast<double>(V[0]) * C[2] - static_cast<double>(C[0]) * V[2];
                const double N2 = static_cast<double>(C[0]) * V[1] - static_cast<double>(V[0]) * C[1];
                const float Ua = static_cast<float>(static_cast<double>(C[2]) * N1 - static_cast<double>(C[1]) * N2);
                const float Ub = static_cast<float>(static_cast<double>(C[0]) * N2 - static_cast<double>(C[2]) * N0);
                const double Uc = N0 * C[1] - static_cast<double>(C[0]) * N1;
                const double SquareB = static_cast<double>(Ub) * Ub;
                const double SquareA = static_cast<double>(Ua) * Ua;
                const double SumBa = SquareB + SquareA;
                const float LengthSquared = static_cast<float>(SumBa + Uc * Uc);
                std::int32_t LengthBits;
                std::memcpy(&LengthBits, &LengthSquared, sizeof(LengthBits));
                const std::uint32_t GuessBits = 0x5f3759dfu - static_cast<std::uint32_t>(LengthBits >> 1);
                float Guess;
                std::memcpy(&Guess, &GuessBits, sizeof(Guess));
                const double InverseLength = (1.5 - (((static_cast<double>(Guess) * Guess) * LengthSquared) * 0.5)) * Guess;
                const float NormalX = static_cast<float>(static_cast<double>(Ua) * InverseLength);
                const float NormalY = static_cast<float>(static_cast<double>(Ub) * InverseLength);
                const double NormalZ = Uc * InverseLength;
                const double DotY = static_cast<double>(V[1]) * NormalY;
                const double DotX = static_cast<double>(V[0]) * NormalX;
                const double SumYx = DotY + DotX;
                const double Shift = (SumYx + static_cast<double>(V[2]) * NormalZ) * StreakShiftScale();
                const double ShiftX = static_cast<double>(NormalX) * Shift;
                const float ShiftY = static_cast<float>(Shift * NormalY);
                Bx = static_cast<float>(static_cast<double>(C[0]) - ShiftX);
                By = static_cast<float>(static_cast<double>(C[1]) - ShiftY);
                Ux = static_cast<float>(static_cast<double>(NormalX) * Size);
                const double UnroundedUy = static_cast<double>(NormalY) * Size;
                Uy = static_cast<float>(UnroundedUy);
                Px = static_cast<float>(-UnroundedUy * Aspect);
                Py = static_cast<float>(static_cast<double>(Ux) * Aspect);
            }
            else
            {
                const double AspectSize = static_cast<double>(Aspect) * Size;
                Px = static_cast<float>(static_cast<double>(Trig[0]) * AspectSize);
                Py = static_cast<float>(static_cast<double>(Trig[1]) * AspectSize);
                Ux = static_cast<float>(static_cast<double>(Trig[0]) * Size);
                Uy = static_cast<float>(static_cast<double>(Trig[1]) * Size);
            }
            unsigned short Indices[4];
            for (unsigned long Corner = 0; Corner < 4; ++Corner)
            {
                const unsigned long UIndex = bStreak ? (Corner < 2 ? 3 : 1) :
                    ((Corner == 0 || Corner == 2) ? 3 : 1);
                const unsigned long VIndex = bStreak ? ((Corner == 0 || Corner == 2) ? 2 : 4) :
                    (Corner < 2 ? 2 : 4);
                double V = 0.0;
                if (Corner == 0)
                    V = LoadAppearanceScalar(FrameBlock[VIndex]);
                const double U = LoadAppearanceScalar(FrameBlock[UIndex]);
                const long Count = G_BasicDrawContext.VertCount;
                Indices[Corner] = static_cast<unsigned short>(G_BasicDrawContext.RequestedVerts);
                if (Count > static_cast<long>(G_BasicDrawContext.RequestedVerts))
                    continue;
                FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(G_BasicDrawContext.LockedVerts);
                double X;
                if (bStreak)
                    X = Corner == 0 ? (static_cast<double>(Bx) - Ux) - Px :
                        Corner == 1 ? (static_cast<double>(C[0]) + Ux) - Px :
                        Corner == 2 ? (static_cast<double>(Bx) - Ux) + Px :
                                      (static_cast<double>(C[0]) + Px) + Ux;
                else
                    X = Corner == 0 ? (static_cast<double>(C[0]) + Uy) + Px :
                        Corner == 1 ? (static_cast<double>(C[0]) - Px) + Uy :
                        Corner == 2 ? (static_cast<double>(C[0]) + Px) - Uy :
                                      (static_cast<double>(C[0]) - Px) - Uy;
                Vertex->Diffuse = Diffuse;
                if (Corner != 0)
                    std::memcpy(&Vertex->V, &FrameBlock[VIndex], sizeof(float));
                Vertex->X = static_cast<float>(X);
                double Y;
                if (bStreak)
                    Y = Corner == 0 ? (static_cast<double>(By) - Uy) - Py :
                        Corner == 1 ? (static_cast<double>(C[1]) + Uy) - Py :
                        Corner == 2 ? (static_cast<double>(By) - Uy) + Py :
                                      (static_cast<double>(C[1]) + Py) + Uy;
                else
                    Y = Corner == 0 ? (static_cast<double>(C[1]) - Py) + Ux :
                        Corner == 1 ? (static_cast<double>(C[1]) + Ux) + Py :
                        Corner == 2 ? (static_cast<double>(C[1]) - Py) - Ux :
                                      (static_cast<double>(C[1]) + Py) - Ux;
                Vertex->Y = static_cast<float>(Y);
                Vertex->Z = static_cast<float>(LoadAppearanceScalar(C[2]));
                Vertex->U = static_cast<float>(U);
                if (Corner == 0)
                    Vertex->V = static_cast<float>(V);
                Indices[Corner] = static_cast<unsigned short>(Count);
                G_BasicDrawContext.LockedVerts = Vertex + 1;
                ++G_BasicDrawContext.VertCount;
            }
            StoreQuadIndices(G_BasicDrawContext, Indices[0], Indices[1], Indices[2], Indices[3]);
        }
    }
    // FUN_06216f20, __thiscall on the node. Traced 2026-09-26 against alchemy.dll.txt
    // 0x06216f20-0x0621763c.
    //   bFrameLookup [0x13] as in the quad. ViewWorld = MulTransform(camera vt+0x40,
    //     node+0x4c) (math vt+0x1c into [0x88], copied to [0xb8] by FUN_06217ab0).
    //   Frame block [0x38..0x4b] from node+0xbc with the 0x1000/0x2000 flips.
    //   UV table by flag 1 (0x06217042): set -> U {0,0,1}, V {1,0,1} (DAT_06255cc4..);
    //     clear -> U {0.5,0,1}, V {0,1,1} (DAT_06255cdc..). A fourth U/V dword is loaded
    //     too and never read -- for flag 1 clear it is the first four bytes of the string
    //     "BasicApp_Rotate" at 0x06255cf4.
    //   Without bFrameLookup the table is mapped into the frame rect once:
    //     U = U*(U1-U0) + U0, V = V*(V1-V0) + V0.
    //   Scale = +0x5c ramp (no zero test here). LastFrame = -1.
    //   Per particle: t, Alpha (+0x70), Size (+0x78 * Scale); Alpha < 0.01 -> next;
    //     C = TransformXform(ViewWorld, pos) (no depth test here); Diffuse as in the quad;
    //     Size2 = Size + Size; the stored Size2*0x3f5db3d7 is overwritten by trig output.
    //     Angle (+0x7c), (cos, sin); Xa = cos*Size2, Ya = sin*Size2,
    //     Xb = cos*Size, Yb = sin*Size (actual stack reuse at 0x06217280..0x062172b5).
    //     bFrameLookup and a new frame: lookup, flips, reload the table by flag 1 and map it.
    //     Corners (C.x+Xa-Yb, C.y-Ya-Xb) UV[2], (C.x-Xa-Yb, C.y+Ya-Xb) UV[1],
    //     (C.x+sin*Alpha, C.y+cos*Alpha) UV[0]; indices (I2,I1,I0) unless
    //     IndexCount + 2 > requested.
    void FxBasicAppearance::FillTriangleGeometry()
    {
        const FPooledParticle* Particle = InstanceListHead_;
        const bool bFrameLookup = Texture_.FrameCount != 1 && (Descriptor_->Flags & 8u) == 0;
        ICamera* const Camera = reinterpret_cast<ICamera*>(static_cast<std::uintptr_t>(G_DrawCookie));
        const float* const View = Camera->GetViewTransform();
        if (G_BasicGeometry3DMath == nullptr)
            G_BasicGeometry3DMath = Create3DMath();
        FTransform ViewTemporary;
        G_BasicGeometry3DMath->MulTransform(ViewTemporary,
            *reinterpret_cast<const FTransform*>(View),
            *reinterpret_cast<const FTransform*>(NodeWorldXform_));
        float ViewWorld[12];
        std::memcpy(ViewWorld, &ViewTemporary, sizeof(ViewTemporary));
        float FrameBlock[5];
        std::memcpy(FrameBlock, Texture_.Frame, sizeof(FrameBlock));
        const FAppearanceRampDescriptor* const InitialDescriptor = Descriptor_;
        FlipBasicAppearanceFrame(InitialDescriptor->Flags, FrameBlock);
        float TriU[3];
        float TriV[3];
        LoadTriangleUvTable(InitialDescriptor->Flags, TriU, TriV);
        MapTriangleUvTable(FrameBlock, TriU, TriV, true, !bFrameLookup);
        const float Scale = static_cast<float>(ForwardAppearanceSingleEval(
            InitialDescriptor->EffectScaleRamp, AppearanceOuterTime_, LodInput_));
        std::int32_t LastFrame = -1;
        for (; Particle != nullptr; Particle = Particle->Next)
        {
            const float InnerTime = static_cast<float>(static_cast<double>(Particle->InvDuration) * Particle->Age);
            const float Alpha = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->SizeRamp, AppearanceOuterTime_, InnerTime));
            const float Size = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, InnerTime) * Scale);
            if (Alpha < FloatFromBits(0x3c23d70au))
                continue;
            if (G_BasicGeometry3DMath == nullptr)
                G_BasicGeometry3DMath = Create3DMath();
            float C[3];
            G_BasicGeometry3DMath->TransformXform(*reinterpret_cast<FVector*>(C),
                *reinterpret_cast<const FTransform*>(ViewWorld),
                *reinterpret_cast<const FVector*>(&Particle->PosX));
            const unsigned long Color = ForwardAppearanceColorEval(
                Descriptor_->ColorRamp, AppearanceOuterTime_, InnerTime);
            const std::int64_t AlphaInteger = static_cast<std::int64_t>(static_cast<double>(Alpha) * 255.0);
            const unsigned long Diffuse = Color | (static_cast<std::uint32_t>(AlphaInteger) << 24);
            const double DoubledSize = static_cast<double>(Size) + Size;
            const volatile float Size2 = static_cast<float>(DoubledSize);
            // The next trig output overwrites this stored product in retail.
            const volatile float RoundedCornerProduct = static_cast<float>(DoubledSize * TriangleCornerScale());
            const float Angle = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->TexFrameRamp, AppearanceOuterTime_, InnerTime));
            float Trig[2];
            ForwardAppearanceSinCos(Angle, Trig);
            const float Xa = static_cast<float>(static_cast<double>(Trig[0]) * Size2);
            const float Ya = static_cast<float>(static_cast<double>(Trig[1]) * Size2);
            const float Xb = static_cast<float>(static_cast<double>(Trig[0]) * Size);
            const float Yb = static_cast<float>(static_cast<double>(Trig[1]) * Size);
            if (bFrameLookup)
            {
                const double FramePosition = ForwardAppearanceSingleEval(
                    Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, InnerTime);
                const std::uint32_t CountBits = Texture_.FrameCount - 1u;
                std::int32_t SignedCount;
                std::memcpy(&SignedCount, &CountBits, sizeof(SignedCount));
                const std::int64_t FrameInteger = static_cast<std::int64_t>(FramePosition * SignedCount);
                const std::int32_t Frame = static_cast<std::int32_t>(FrameInteger);
                if (Frame != LastFrame)
                {
                    LastFrame = Frame;
                    Texture_.LookupFrame(Frame, FrameBlock);
                    const FAppearanceRampDescriptor* const FrameDescriptor = Descriptor_;
                    FlipBasicAppearanceFrame(FrameDescriptor->Flags, FrameBlock);
                    LoadTriangleUvTable(FrameDescriptor->Flags, TriU, TriV);
                    MapTriangleUvTable(FrameBlock, TriU, TriV, false);
                }
            }
            unsigned short Indices[3];
            for (unsigned long Corner = 0; Corner < 3; ++Corner)
            {
                double ThirdY = 0.0;
                if (Corner == 2)
                    ThirdY = static_cast<double>(Trig[0]) * Alpha + C[1];
                const long Count = G_BasicDrawContext.VertCount;
                Indices[Corner] = static_cast<unsigned short>(G_BasicDrawContext.RequestedVerts);
                if (Count > static_cast<long>(G_BasicDrawContext.RequestedVerts))
                    continue;
                FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(G_BasicDrawContext.LockedVerts);
                const double X = Corner == 0 ? (static_cast<double>(C[0]) + Xa) - Yb :
                    Corner == 1 ? (static_cast<double>(C[0]) - Xa) - Yb :
                                  static_cast<double>(Trig[1]) * Alpha + C[0];
                const unsigned long UvIndex = 2 - Corner;
                Vertex->Diffuse = Diffuse;
                std::memcpy(&Vertex->U, &TriU[UvIndex], sizeof(float));
                std::memcpy(&Vertex->V, &TriV[UvIndex], sizeof(float));
                Vertex->X = static_cast<float>(X);
                if (Corner == 2)
                    Vertex->Y = static_cast<float>(ThirdY);
                else
                    Vertex->Y = static_cast<float>(Corner == 0 ?
                        (static_cast<double>(C[1]) - Ya) - Xb :
                        (static_cast<double>(C[1]) + Ya) - Xb);
                Vertex->Z = static_cast<float>(LoadAppearanceScalar(C[2]));
                Indices[Corner] = static_cast<unsigned short>(Count);
                G_BasicDrawContext.LockedVerts = Vertex + 1;
                ++G_BasicDrawContext.VertCount;
            }
            StoreTriangleIndices(G_BasicDrawContext, Indices[0], Indices[1], Indices[2]);
        }
    }
    // ---- The six billboard set builders (ALC-6, re-traced 2026-09-30) ----
    // Each runs FUN_06243ff0 first (FFxAppearancePropertySet's constructor: the
    // Appearance_LODCurve ramp at +0x58), zeroes Flags, TextureCrc and its ramp slots,
    // seeds BlendInfo {5, 2} (FAppearanceRampDescriptor's constructor), then builds the
    // ColorRamp (FxRampColorRamp{FUN_06209240({0.8, 0.8, 0.8})}) and its own list; each
    // step first releases what its slot holds (the set is new: nothing). Nested scalar
    // ramps are FxRampSingleRamp{FUN_06208bc0(value)} unless noted. Inner offsets (+4 for
    // the draw fills):
    //   basic    FUN_06215570: +0x6c 1, +0x74 1, +0x78 0, +0x7c 0, +0x80
    //            FxAnimatedSingleRamp{FUN_06209ad0(0)} (0x0621584e), +0x84 FUN_06217c20(1)
    //            (the same one-key FxRampSingleRamp inline), Flags 0 (0x062158b5).
    //   rect     FUN_06237840: +0x6c 1, +0x74 1, +0x78 1, +0x7c 1, +0x80 0, +0x84
    //            FUN_06210900(0), Flags 0 (0x06237b89).
    //   perp / dust FUN_062323a0 / FUN_0620ef80: +0x6c 1, +0x74 1, +0x78 0, +0x7c
    //            FUN_06210900(0), Flags 0 (0x06232622 / 0x0620f202).
    //   oriented FUN_0622b3e0: the rect list, then +0x88 a fresh FxAnimatedTransform
    //            (0x0622b721) stored through FUN_062084f0, Flags 0 (0x0622b768).
    //   beam     FUN_06204670: +0x6c 1, +0x74 1, +0x84 1, +0x78 1, +0x80 0, +0x7c
    //            FxAnimatedSingleRamp{FUN_06209ad0(0)}, +0x88 FUN_06208620 (a fresh
    //            FxAnimatedTransform) stored through FUN_062084f0 and then released
    //            (0x06204a3b), Flags 2 (0x06204a47).
    // The colour ramp comes first in all six.
    static void BuildDescriptorHead(FAppearanceRampDescriptor& Descriptor)
    {
        const float Grey[3] = {0.8f, 0.8f, 0.8f};
        MakeRampColorRamp(Grey, &Descriptor.ColorRamp);
    }

    // FUN_06215570.
    static void BuildBasicDescriptorRamps(FAppearanceRampDescriptor& Descriptor)
    {
        BuildDescriptorHead(Descriptor);
        MakeRampSingleRamp(1.0f, &Descriptor.SizeRamp);
        MakeRampSingleRamp(1.0f, &Descriptor.PrimarySizeRamp);
        MakeRampSingleRamp(0.0f, &Descriptor.TexFrameRamp);
        MakeRampSingleRamp(0.0f, &Descriptor.AxisOverrideRamp);
        MakeAnimatedSingleRamp(0.0f, &Descriptor.FrameRamp);
        MakeRampSingleRamp(1.0f, &Descriptor.CommonTexFrameRamp);
        Descriptor.Flags = 0;
    }

    // FUN_06237840, and the ramp part of FUN_0622b3e0.
    static void BuildRectDescriptorRamps(FAppearanceRampDescriptor& Descriptor)
    {
        BuildDescriptorHead(Descriptor);
        MakeRampSingleRamp(1.0f, &Descriptor.SizeRamp);
        MakeRampSingleRamp(1.0f, &Descriptor.PrimarySizeRamp);
        MakeRampSingleRamp(1.0f, &Descriptor.TexFrameRamp);
        MakeRampSingleRamp(1.0f, &Descriptor.AxisOverrideRamp);
        MakeRampSingleRamp(0.0f, &Descriptor.FrameRamp);
        MakeAnimatedSingleRamp(0.0f, &Descriptor.CommonTexFrameRamp);
        Descriptor.Flags = 0;
    }

    // FUN_062323a0 (perp) and FUN_0620ef80 (dust), which build the same set.
    static void BuildPerpDescriptorRamps(FAppearanceRampDescriptor& Descriptor)
    {
        BuildDescriptorHead(Descriptor);
        MakeRampSingleRamp(1.0f, &Descriptor.SizeRamp);
        MakeRampSingleRamp(1.0f, &Descriptor.PrimarySizeRamp);
        MakeRampSingleRamp(0.0f, &Descriptor.TexFrameRamp);
        MakeAnimatedSingleRamp(0.0f, &Descriptor.AxisOverrideRamp);
        Descriptor.Flags = 0;
    }

    // FUN_0622b3e0: the rect list, then at 0x0622b721 operator_new(0x34) + FUN_062434c0 +
    // vtable 0x0624bbb8 + RefCount 1, stored through FUN_062084f0 (Release old, store,
    // AddRef). The builder keeps its construction reference, so the transform's count
    // ends at 2 in retail as well; one of the two is never released.
    static void BuildOrientedDescriptorRamps(FAppearanceRampDescriptor& Descriptor)
    {
        BuildRectDescriptorRamps(Descriptor);
        FFxAnimatedTransform* Transform = new FFxAnimatedTransform();
        ReleaseDescriptorSlot(Descriptor.LocalTransform);
        Descriptor.LocalTransform = Transform;
        Transform->AddRef();
    }

    // FUN_06204670. The transform (FUN_06208620: FUN_062434c0 + vtable 0x0624bbb8 +
    // RefCount 1) goes through FUN_062084f0 and is then released once (0x06204a3b), so
    // the descriptor holds the only reference. Flags = 2 (0x06204a47).
    static void BuildBeamDescriptorRamps(FAppearanceRampDescriptor& Descriptor)
    {
        BuildDescriptorHead(Descriptor);
        MakeRampSingleRamp(1.0f, &Descriptor.SizeRamp);
        MakeRampSingleRamp(1.0f, &Descriptor.PrimarySizeRamp);
        MakeRampSingleRamp(1.0f, &Descriptor.CommonTexFrameRamp);
        MakeRampSingleRamp(1.0f, &Descriptor.TexFrameRamp);
        MakeRampSingleRamp(0.0f, &Descriptor.FrameRamp);
        MakeAnimatedSingleRamp(0.0f, &Descriptor.AxisOverrideRamp);
        FFxAnimatedTransform* Transform = new FFxAnimatedTransform();
        ReleaseDescriptorSlot(Descriptor.LocalTransform);
        Descriptor.LocalTransform = Transform;
        Transform->AddRef();
        Transform->Release();
        Descriptor.Flags = 2;
    }

    FxRectAppearance::FxRectAppearance(const char* PrimaryIid, const char* FactoryName)
        : FFxBillboardAppearance(PrimaryIid, FactoryName, &kRectClass)
    {
    }

    void FxRectAppearance::CreatePropertySet()
    {
        AdoptBillboardSet(&BuildRectDescriptorRamps);
    }

    // FUN_06238050 (RET 8): the rect constructor inline (as its factory 0x06239c76..),
    // FUN_06244120 (0x06238116), the +0xf0 set cache (0x0623811b..0x06238125); *Out, 0.
    GENRESULT DACOM_API FxRectAppearance::CloneNode(IDAComponent** Out)
    {
        FxRectAppearance* const Node = new (std::nothrow) FxRectAppearance(PrimaryIid_, FactoryName_);
        Node->InitAppearanceFromArchetype(this);
        Node->Descriptor_ = Descriptor_;
        *Out = Node;
        return GR_OK;
    }

    FxRectAppearance::~FxRectAppearance() noexcept(false)
    {
    }

    // Outer +0x14 -> FUN_06238780. See FGenericFxObjectBase::UpdateListAppearance.
    GENRESULT DACOM_API FxRectAppearance::Pad5(float Dt)
    {
        G_RectDrawContext.Field_0xA = 0;
        G_RectDrawContext.PrimCount = 0;
        Age_ = static_cast<float>(static_cast<double>(Dt) + Age_);
        if (EmitterRefCount_ == 0 && InstanceListHead_ == nullptr)
            NodeFlags_ |= 1;
        const FFxTextureKey Key = {"???", Descriptor_->TextureCrc};
        Texture_.SetTexture(&Key);
        if (Texture_.Handle == -1)
            Texture_.Resolve();
        if ((Descriptor_->Flags & 8u) != 0)
        {
            const float Position = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->CommonTexFrameRamp, AppearanceOuterTime_, Age_));
            if (Texture_.FrameCount != 1 && Texture_.Handle != -1)
            {
                const int Handle = Texture_.Handle;
                const std::uint32_t Span = Texture_.FrameCount - 1u;
                ITextureLibrary2* const Library = static_cast<ITextureLibrary2*>(G_TextureLibrary);
                const std::int64_t Frame = static_cast<std::int64_t>(static_cast<double>(Span) * Position);
                Library->GetFrame(Handle, static_cast<int>(Frame),
                    reinterpret_cast<unsigned long*>(Texture_.Frame));
            }
        }
        else if (Texture_.FrameCount != 1)
            Texture_.Advance(Dt);
        ApplyFieldsToPool(this, false);
        IntegratePool(this, &G_PoolRect, Dt);
        return GR_OK;
    }

    // FUN_06238280: round Primary and B before the next callbacks, but retain C
    // as double for the ordered max and radius. Distance uses a float-stored YZX sum.
    GENRESULT DACOM_API FxRectAppearance::GetBoundingSphere(float* OutSphere)
    {
        const FPooledParticle* Head = InstanceListHead_;
        if (Head == nullptr)
            return GR_GENERIC;
        const float Scale = static_cast<float>(ForwardAppearanceSingleEval(
            Descriptor_->EffectScaleRamp, AppearanceOuterTime_, LodInput_));
        const float FirstInnerTime = static_cast<float>(static_cast<double>(Head->InvDuration) * Head->Age);
        const float FirstPrimary = static_cast<float>(ForwardAppearanceSingleEval(
            Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, FirstInnerTime) * Scale);
        const float FirstB = static_cast<float>(ForwardAppearanceSingleEval(
            Descriptor_->TexFrameRamp, AppearanceOuterTime_, FirstInnerTime));
        const double FirstC = ForwardAppearanceSingleEval(
            Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, FirstInnerTime);
        const double FirstSide = static_cast<double>(FirstB) > FirstC ? FirstB : FirstC;
        const double FirstRadius = FirstSide * FirstPrimary;
        std::memcpy(OutSphere, &Head->PosX, sizeof(float) * 2);
        OutSphere[3] = static_cast<float>(FirstRadius);
        std::memcpy(OutSphere + 2, &Head->PosZ, sizeof(float));
        const FPooledParticle* Tail = InstanceListTail_;
        if (Head != Tail)
        {
            const float TailInnerTime = static_cast<float>(static_cast<double>(Head->InvDuration) * Head->Age);
            const float TailPrimary = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, TailInnerTime) * Scale);
            const float TailB = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->TexFrameRamp, AppearanceOuterTime_, TailInnerTime));
            const double TailC = ForwardAppearanceSingleEval(
                Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, TailInnerTime);
            const double TailSide = static_cast<double>(TailB) > TailC ? TailB : TailC;
            const double TailRadius = TailSide * TailPrimary;
            float TailSphere[4];
            std::memcpy(TailSphere, &Tail->PosX, sizeof(float) * 2);
            TailSphere[3] = static_cast<float>(TailRadius);
            std::memcpy(TailSphere + 2, &Tail->PosZ, sizeof(float));
            MergePerpRectBoundsSphere(OutSphere, TailSphere, TailSphere[3],
                EPerpRectBoundsDistance::RectRoundedYzx);
            const FPooledParticle* Particle = Head->Next;
            for (; Particle != Tail; Particle = Particle->Next)
            {
                const float InnerTime = static_cast<float>(static_cast<double>(Particle->InvDuration) * Particle->Age);
                const float Primary = static_cast<float>(ForwardAppearanceSingleEval(
                    Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, InnerTime) * Scale);
                const float B = static_cast<float>(ForwardAppearanceSingleEval(
                    Descriptor_->TexFrameRamp, AppearanceOuterTime_, InnerTime));
                const double C = ForwardAppearanceSingleEval(
                    Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, InnerTime);
                const double Side = static_cast<double>(B) > C ? B : C;
                const double Radius = Side * Primary;
                float Sphere[4];
                std::memcpy(Sphere, &Particle->PosX, sizeof(float) * 2);
                Sphere[3] = static_cast<float>(Radius);
                std::memcpy(Sphere + 2, &Particle->PosZ, sizeof(float));
                MergePerpRectBoundsSphere(OutSphere, Sphere, Sphere[3],
                    EPerpRectBoundsDistance::RectRoundedYzx);
            }
        }
        if (G_Rect3DMath == nullptr)
            G_Rect3DMath = Create3DMath();
        FVector WorldCenter;
        G_Rect3DMath->TransformXform(WorldCenter,
            *reinterpret_cast<const FTransform*>(NodeWorldXform_),
            *reinterpret_cast<const FVector*>(OutSphere));
        float WorldSphere[4];
        std::memcpy(WorldSphere, &WorldCenter, sizeof(WorldCenter));
        std::memcpy(WorldSphere + 3, OutSphere + 3, sizeof(float));
        std::memcpy(OutSphere, WorldSphere, sizeof(WorldSphere));
        return GR_OK;
    }

    // Outer +0xBC -> FUN_0623b410 -> FUN_0623a2e0. See
    // FGenericFxObjectBase::SpawnListParticle.
    GENRESULT DACOM_API FxRectAppearance::Pad47(const float* Six, float Age, float Duration)
    {
        SpawnListParticle(&G_PoolRect, Six, Age, Duration);
        return GR_OK;
    }

    // FUN_062388b0 (node vtable 0x0624eee8 +0x10; outer +0x18 thunk LAB_0623ac50), traced
    // 2026-09-26 against 0x062388b0-0x06238a68: the same shape as the perp draw --
    //   node+0xd8 list empty -> return 0;
    //   DAT_06257919 -> FUN_06201c70(1, [node+0xac]), return 0;
    //   counts from [node+0xac] vt+0xd0 / +0xd4; FUN_06240f50(DAT_06257b40, 4, v, i) false ->
    //     return 0;
    //   FUN_06238a70 (fill); then from 0x06238990 the end-and-draw sequence of the outer
    //     +0xA0 body FUN_0623b1a0 (EndBatchedDraw). Return 0 on every path.
    GENRESULT DACOM_API FxRectAppearance::DrawOrEnqueue()
    {
        if (InstanceListHead_ == nullptr)
            return GR_OK;

        if (G_Defaults.bUseMaterialBatcher != 0)
        {
            EnqueueAppearanceToList(1, this);
            return GR_OK;
        }

        unsigned long VertCount = 0;
        unsigned long IndexCount = 0;
        GetQueuedVertCount(&VertCount);
        GetQueuedIndexCount(&IndexCount);
        if (!AlDrawContextBegin(G_RectDrawContext, 4, VertCount, IndexCount))
            return GR_OK;

        FillGeometry();
        EndBatchedDraw();
        return GR_OK;
    }

    // LAB_0623b140 (outer +0x9C, RET 0xc): FUN_06240f50(DAT_06257b40, 4, Verts, Indices)
    // folded to 0 / -1 (see FxPerpAppearance::BeginBatchedDraw on the single context).
    GENRESULT DACOM_API FxRectAppearance::BeginBatchedDraw(unsigned long Verts,
                                                          unsigned long Indices)
    {
        return AlDrawContextBegin(G_RectDrawContext, 4, Verts, Indices) ? GR_OK : GR_GENERIC;
    }

    // FUN_0623b1a0 (outer +0xA0, RET 4), traced 2026-09-26 against 0x0623b1a0-0x0623b271;
    // byte-for-byte the perp body FUN_06235a60 on context DAT_06257b40 (math engine cache
    // DAT_06257b8c): end == 0 -> return 0; ++DAT_062578e8; blend 0x13/0x14 from
    // [desc+0x68]/[desc+0x6c]; SetTexture(0, [node+0xbc]); SetModelview(MulTransform(camera
    // vt+0x40, node+0x4c)); draw; return 0.
    GENRESULT DACOM_API FxRectAppearance::EndBatchedDraw()
    {
        if (AlDrawContextEnd(G_RectDrawContext) == 0)
            return GR_OK;
        ++G_DrawCounters[1];

        const unsigned long* const Blend = Descriptor_->BlendInfo;
        CallSetRenderState(G_RenderPipeline, 0x13, static_cast<int>(Blend[0]));
        CallSetRenderState(G_RenderPipeline, 0x14, static_cast<int>(Blend[1]));

        unsigned long FrameTexture = 0;
        std::memcpy(&FrameTexture, &Texture_.Frame[0], sizeof(FrameTexture));
        CallRenderPipelineSetTexture(G_RenderPipeline, 0, static_cast<int>(FrameTexture));

        float ViewWorld[12];
        ComposeTransform(ViewWorld, GetDrawCameraTransform(), NodeWorldXform_);
        CallRenderPipelineSetTransform(G_RenderPipeline, ViewWorld);

        AlDrawContextDraw(G_RectDrawContext);
        return GR_OK;
    }

    // FUN_0623b2a0 (outer +0xA4): `LEA ECX,[this+0xc]; CALL FUN_06238a70; XOR EAX,EAX`.
    GENRESULT DACOM_API FxRectAppearance::FillBatchedGeometry()
    {
        FillGeometry();
        return GR_OK;
    }

    // FUN_06238a70: Rect's independent camera transform and full list/quad walk.
#pragma float_control(except, on, push)
    void FxRectAppearance::FillGeometry()
    {
        const float TinyLength = FloatFromBits(0x358637bdu);
        const float TinyAlpha = FloatFromBits(0x38d1b717u);
        const float SpeedStretch = FloatFromBits(0x3d08850au);
        const float FacingCull = FloatFromBits(0x3f7c1c5cu);
        const float FacingFade = FloatFromBits(0x3f5db3d7u);
        const float FacingFadeScale = FloatFromBits(0x4106b33du);
        ++G_DrawCounters[4];
        const FPooledParticle* Particle = InstanceListHead_;
        const bool bFrameLookup = Texture_.FrameCount != 1 && (Descriptor_->Flags & 8u) == 0;
        const bool bStretch = (Descriptor_->Flags & 4u) != 0;
        const bool bCentered = (Descriptor_->Flags & 1u) != 0;
        const bool bViewFade = (Descriptor_->Flags & 0x10u) != 0;
        FTransform InvNode;
        float InverseW;
        ComputeGeneralInverseTransform(*reinterpret_cast<const FTransform*>(NodeWorldXform_),
            InvNode, InverseW);
        float FrameBlock[5];
        std::memcpy(FrameBlock, Texture_.Frame, sizeof(FrameBlock));
        if ((Descriptor_->Flags & 0x1000u) != 0)
        {
            std::uint32_t Previous;
            std::memcpy(&Previous, &FrameBlock[3], sizeof(Previous));
            FrameBlock[3] = static_cast<float>(LoadAppearanceScalar(FrameBlock[1]));
            std::memcpy(&FrameBlock[1], &Previous, sizeof(Previous));
        }
        if ((Descriptor_->Flags & 0x2000u) != 0)
        {
            std::uint32_t Previous;
            std::memcpy(&Previous, &FrameBlock[4], sizeof(Previous));
            FrameBlock[4] = static_cast<float>(LoadAppearanceScalar(FrameBlock[2]));
            std::memcpy(&FrameBlock[2], &Previous, sizeof(Previous));
        }
        std::int32_t LastFrame = -1;
        ICamera* const Camera = reinterpret_cast<ICamera*>(static_cast<std::uintptr_t>(G_DrawCookie));
        const float* const CameraPosition = Camera->GetPosition();
        if (G_Rect3DMath == nullptr)
            G_Rect3DMath = Create3DMath();
        FVector LocalCamera;
        G_Rect3DMath->TransformXform(LocalCamera, InvNode,
            *reinterpret_cast<const FVector*>(CameraPosition));
        const float Scale = static_cast<float>(ForwardAppearanceSingleEval(
            Descriptor_->EffectScaleRamp, AppearanceOuterTime_, LodInput_));
        for (; Particle != nullptr; Particle = Particle->Next)
        {
            const float InnerTime = static_cast<float>(static_cast<double>(Particle->InvDuration) * Particle->Age);
            float Alpha = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->SizeRamp, AppearanceOuterTime_, InnerTime));
            const float SizeScale = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, InnerTime) * Scale);
            const float Length = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->TexFrameRamp, AppearanceOuterTime_, InnerTime) * SizeScale);
            const float HalfWidth = static_cast<float>((ForwardAppearanceSingleEval(
                Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, InnerTime) * SizeScale) * 0.5);
            // TEST AH,0x41 + JNP skips only ordered less/equal; unordered continues.
            if (Length <= TinyLength || HalfWidth <= TinyLength || Alpha <= TinyAlpha)
                continue;
            FVector Position;
            FVector Velocity;
            std::memcpy(&Position, &Particle->PosX, sizeof(Position));
            std::memcpy(&Velocity, &Particle->VelX, sizeof(Velocity));
            const double VelocitySquared = ((static_cast<double>(Velocity.Z) * Velocity.Z) +
                (static_cast<double>(Velocity.Y) * Velocity.Y)) + (static_cast<double>(Velocity.X) * Velocity.X);
            const float VelocitySeed = static_cast<float>(VelocitySquared);
            std::int32_t VelocityBits;
            std::memcpy(&VelocityBits, &VelocitySeed, sizeof(VelocityBits));
            const std::uint32_t VelocityGuessBits = 0x5f3759dfu - static_cast<std::uint32_t>(VelocityBits >> 1);
            float VelocityGuess;
            std::memcpy(&VelocityGuess, &VelocityGuessBits, sizeof(VelocityGuess));
            const double VelocityInverse = (1.5 - (((static_cast<double>(VelocityGuess) * VelocityGuess) *
                VelocitySquared) * 0.5)) * VelocityGuess;
            const float Speed = static_cast<float>(VelocityInverse * VelocitySquared);
            // TEST AH,0x41 + JP takes greater/unordered normalization.
            if (!(Speed <= TinyLength))
            {
                const double InverseSpeed = 1.0 / Speed;
                Velocity.X = static_cast<float>(static_cast<double>(Velocity.X) * InverseSpeed);
                Velocity.Y = static_cast<float>(static_cast<double>(Velocity.Y) * InverseSpeed);
                Velocity.Z = static_cast<float>(InverseSpeed * Velocity.Z);
            }
            else
            {
                Velocity.X = 0.0f;
                Velocity.Y = 1.0f;
                Velocity.Z = 0.0f;
            }
            FVector Direction;
            Direction.Z = static_cast<float>(static_cast<double>(Position.Z) - LocalCamera.Z);
            Direction.Y = static_cast<float>(static_cast<double>(Position.Y) - LocalCamera.Y);
            Direction.X = static_cast<float>(static_cast<double>(Position.X) - LocalCamera.X);
            if (bViewFade)
            {
                const double DirectionSquared = ((static_cast<double>(Direction.Z) * Direction.Z) +
                    (static_cast<double>(Direction.X) * Direction.X)) + (static_cast<double>(Direction.Y) * Direction.Y);
                const float DirectionSeed = static_cast<float>(DirectionSquared);
                std::int32_t DirectionBits;
                std::memcpy(&DirectionBits, &DirectionSeed, sizeof(DirectionBits));
                const std::uint32_t DirectionGuessBits = 0x5f3759dfu - static_cast<std::uint32_t>(DirectionBits >> 1);
                float DirectionGuess;
                std::memcpy(&DirectionGuess, &DirectionGuessBits, sizeof(DirectionGuess));
                const double DirectionInverse = (1.5 - (((static_cast<double>(DirectionGuess) * DirectionGuess) *
                    DirectionSquared) * 0.5)) * DirectionGuess;
                const double DirectionLength = DirectionInverse * DirectionSquared;
                if (!(DirectionLength <= TinyLength))
                {
                    const double InverseDirection = 1.0 / DirectionLength;
                    Direction.X = static_cast<float>(static_cast<double>(Direction.X) * InverseDirection);
                    Direction.Y = static_cast<float>(static_cast<double>(Direction.Y) * InverseDirection);
                    Direction.Z = static_cast<float>(InverseDirection * Direction.Z);
                }
                else
                {
                    Direction.X = 0.0f;
                    Direction.Y = 1.0f;
                    Direction.Z = 0.0f;
                }
                const double UnroundedFacing = std::fabs(((static_cast<double>(Direction.Z) * Velocity.Z) +
                    (static_cast<double>(Velocity.Y) * Direction.Y)) + (static_cast<double>(Direction.X) * Velocity.X));
                const float Facing = static_cast<float>(UnroundedFacing);
                // AND EAX,4100 + JZ skips ordered greater using the still-unrounded ST value.
                if (UnroundedFacing > FacingCull)
                    continue;
                if (Facing > FacingFade)
                {
                    const double Fade = (static_cast<double>(FacingCull) - Facing) * FacingFadeScale;
                    Alpha = static_cast<float>((static_cast<double>(Alpha) * Fade) * Fade);
                }
                if (Alpha <= TinyAlpha)
                    continue;
            }
            const unsigned long Color = ForwardAppearanceColorEval(
                Descriptor_->ColorRamp, AppearanceOuterTime_, InnerTime);
            const std::int64_t AlphaInteger = static_cast<std::int64_t>(static_cast<double>(Alpha) * 255.0f);
            const unsigned long Diffuse = Color | (static_cast<std::uint32_t>(AlphaInteger) << 24);
            if (bFrameLookup)
            {
                const double FramePosition = ForwardAppearanceSingleEval(
                    Descriptor_->FrameRamp, AppearanceOuterTime_, InnerTime);
                const std::int32_t Span = static_cast<std::int32_t>(Texture_.FrameCount - 1u);
                const std::int64_t FrameInteger = static_cast<std::int64_t>(FramePosition * Span);
                const std::int32_t Frame = static_cast<std::int32_t>(FrameInteger);
                if (Frame != LastFrame)
                {
                    LastFrame = Frame;
                    Texture_.LookupFrame(Frame, FrameBlock);
                    if ((Descriptor_->Flags & 0x1000u) != 0)
                    {
                        std::uint32_t Previous;
                        std::memcpy(&Previous, &FrameBlock[3], sizeof(Previous));
                        FrameBlock[3] = static_cast<float>(LoadAppearanceScalar(FrameBlock[1]));
                        std::memcpy(&FrameBlock[1], &Previous, sizeof(Previous));
                    }
                    if ((Descriptor_->Flags & 0x2000u) != 0)
                    {
                        std::uint32_t Previous;
                        std::memcpy(&Previous, &FrameBlock[4], sizeof(Previous));
                        FrameBlock[4] = static_cast<float>(LoadAppearanceScalar(FrameBlock[2]));
                        std::memcpy(&FrameBlock[2], &Previous, sizeof(Previous));
                    }
                }
            }
            FVector P0;
            FVector P1;
            if (bCentered)
            {
                const double Half = bStretch ? ((static_cast<double>(Speed) * SpeedStretch) + Length) * 0.5 :
                    static_cast<double>(Length) * 0.5;
                const float ExtentX = static_cast<float>(static_cast<double>(Velocity.X) * Half);
                const float ExtentY = static_cast<float>(static_cast<double>(Velocity.Y) * Half);
                const float ExtentZ = static_cast<float>(Half * Velocity.Z);
                P0.Z = static_cast<float>(static_cast<double>(Position.Z) - ExtentZ);
                P0.Y = static_cast<float>(static_cast<double>(Position.Y) - ExtentY);
                P0.X = static_cast<float>(static_cast<double>(Position.X) - ExtentX);
                P1.X = static_cast<float>(static_cast<double>(ExtentX) + Position.X);
                P1.Y = static_cast<float>(static_cast<double>(ExtentY) + Position.Y);
                P1.Z = static_cast<float>(static_cast<double>(ExtentZ) + Position.Z);
            }
            else
            {
                std::memcpy(&P0, &Position, sizeof(P0));
                if (bStretch)
                {
                    const double Extent = (static_cast<double>(Speed) * SpeedStretch) + Length;
                    const double ExtentX = static_cast<double>(Velocity.X) * Extent;
                    const float ExtentY = static_cast<float>(Extent * Velocity.Y);
                    const float ExtentZ = static_cast<float>(Extent * Velocity.Z);
                    P1.X = static_cast<float>(ExtentX + Position.X);
                    P1.Y = static_cast<float>(static_cast<double>(ExtentY) + Position.Y);
                    P1.Z = static_cast<float>(static_cast<double>(ExtentZ) + Position.Z);
                }
                else
                {
                    const double ExtentX = static_cast<double>(Velocity.X) * Length;
                    const double ExtentY = static_cast<double>(Velocity.Y) * Length;
                    const float ExtentZ = static_cast<float>(static_cast<double>(Velocity.Z) * Length);
                    P1.X = static_cast<float>(ExtentX + Position.X);
                    P1.Y = static_cast<float>(ExtentY + Position.Y);
                    P1.Z = static_cast<float>(static_cast<double>(ExtentZ) + Position.Z);
                }
            }
            const double NormalX = (static_cast<double>(Direction.Z) * Velocity.Y) -
                (static_cast<double>(Velocity.Z) * Direction.Y);
            const float NormalY = static_cast<float>((static_cast<double>(Velocity.Z) * Direction.X) -
                (static_cast<double>(Direction.Z) * Velocity.X));
            const double NormalZ = (static_cast<double>(Direction.Y) * Velocity.X) -
                (static_cast<double>(Direction.X) * Velocity.Y);
            const double NormalSquared = ((NormalX * NormalX) + (static_cast<double>(NormalY) * NormalY)) +
                (NormalZ * NormalZ);
            const float NormalSeed = static_cast<float>(NormalSquared);
            std::int32_t NormalBits;
            std::memcpy(&NormalBits, &NormalSeed, sizeof(NormalBits));
            const std::uint32_t NormalGuessBits = 0x5f3759dfu - static_cast<std::uint32_t>(NormalBits >> 1);
            float NormalGuess;
            std::memcpy(&NormalGuess, &NormalGuessBits, sizeof(NormalGuess));
            const double NormalInverse = (1.5 - (((static_cast<double>(NormalGuess) * NormalGuess) *
                NormalSquared) * 0.5)) * NormalGuess;
            const double NormalLength = NormalInverse * NormalSquared;
            if (NormalLength <= TinyLength)
                continue;
            const double WidthScale = static_cast<double>(HalfWidth) / NormalLength;
            const float Qx = static_cast<float>(WidthScale * NormalX);
            const float Qy = static_cast<float>(static_cast<double>(NormalY) * WidthScale);
            const float Qz = static_cast<float>(NormalZ * WidthScale);
            FAlDrawContext& Context = G_RectDrawContext;
            // Vertex stores are inlined below: skipped capacity paths do no coordinate arithmetic.
            unsigned short IndexA;
            const double VA = FrameBlock[2];
            const double UA = FrameBlock[3];
            if (static_cast<long>(Context.VertCount) > static_cast<long>(Context.RequestedVerts))
                IndexA = static_cast<unsigned short>(Context.RequestedVerts);
            else
            {
                FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(Context.LockedVerts);
                Vertex->X = static_cast<float>(static_cast<double>(P1.X) - Qx);
                Vertex->Diffuse = Diffuse;
                Vertex->Y = static_cast<float>(static_cast<double>(P1.Y) - Qy);
                Vertex->Z = static_cast<float>(static_cast<double>(P1.Z) - Qz);
                Vertex->U = static_cast<float>(UA);
                Vertex->V = static_cast<float>(VA);
                Context.LockedVerts = Vertex + 1;
                IndexA = Context.VertCount;
                Context.VertCount = static_cast<unsigned short>(Context.VertCount + 1);
            }
            unsigned short IndexB;
            const double VB = FrameBlock[2];
            const double UB = FrameBlock[1];
            if (static_cast<long>(Context.VertCount) > static_cast<long>(Context.RequestedVerts))
                IndexB = static_cast<unsigned short>(Context.RequestedVerts);
            else
            {
                FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(Context.LockedVerts);
                Vertex->X = static_cast<float>(static_cast<double>(Qx) + P1.X);
                Vertex->Diffuse = Diffuse;
                Vertex->Y = static_cast<float>(static_cast<double>(Qy) + P1.Y);
                Vertex->Z = static_cast<float>(static_cast<double>(Qz) + P1.Z);
                Vertex->U = static_cast<float>(UB);
                Vertex->V = static_cast<float>(VB);
                Context.LockedVerts = Vertex + 1;
                IndexB = Context.VertCount;
                Context.VertCount = static_cast<unsigned short>(Context.VertCount + 1);
            }
            unsigned short IndexC;
            const double VC = FrameBlock[4];
            const double UC = FrameBlock[3];
            if (static_cast<long>(Context.VertCount) > static_cast<long>(Context.RequestedVerts))
                IndexC = static_cast<unsigned short>(Context.RequestedVerts);
            else
            {
                FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(Context.LockedVerts);
                Vertex->X = static_cast<float>(static_cast<double>(P0.X) - Qx);
                Vertex->Diffuse = Diffuse;
                Vertex->Y = static_cast<float>(static_cast<double>(P0.Y) - Qy);
                Vertex->Z = static_cast<float>(static_cast<double>(P0.Z) - Qz);
                Vertex->U = static_cast<float>(UC);
                Vertex->V = static_cast<float>(VC);
                Context.LockedVerts = Vertex + 1;
                IndexC = Context.VertCount;
                Context.VertCount = static_cast<unsigned short>(Context.VertCount + 1);
            }
            unsigned short IndexD;
            const double VD = FrameBlock[4];
            const double UD = FrameBlock[1];
            if (static_cast<long>(Context.VertCount) > static_cast<long>(Context.RequestedVerts))
                IndexD = static_cast<unsigned short>(Context.RequestedVerts);
            else
            {
                FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(Context.LockedVerts);
                Vertex->X = static_cast<float>(static_cast<double>(P0.X) + Qx);
                Vertex->Diffuse = Diffuse;
                Vertex->Y = static_cast<float>(static_cast<double>(P0.Y) + Qy);
                Vertex->Z = static_cast<float>(static_cast<double>(P0.Z) + Qz);
                Vertex->U = static_cast<float>(UD);
                Vertex->V = static_cast<float>(VD);
                Context.LockedVerts = Vertex + 1;
                IndexD = Context.VertCount;
                Context.VertCount = static_cast<unsigned short>(Context.VertCount + 1);
            }
            StoreQuadIndices(Context, IndexA, IndexB, IndexC, IndexD);
        }
    }


#pragma float_control(pop)

    FxPerpAppearance::FxPerpAppearance(const char* PrimaryIid, const char* FactoryName)
        : FFxBillboardAppearance(PrimaryIid, FactoryName, &kPerpClass)
    {
    }

    void FxPerpAppearance::CreatePropertySet()
    {
        AdoptBillboardSet(&BuildPerpDescriptorRamps);
    }

    // FUN_06232a00 (RET 8): the perp constructor inline (as its factory 0x06234446..),
    // FUN_06244120 (0x06232ac6), the +0xf0 set cache (0x06232acb..0x06232ad5); *Out, 0.
    GENRESULT DACOM_API FxPerpAppearance::CloneNode(IDAComponent** Out)
    {
        FxPerpAppearance* const Node = new (std::nothrow) FxPerpAppearance(PrimaryIid_, FactoryName_);
        Node->InitAppearanceFromArchetype(this);
        Node->Descriptor_ = Descriptor_;
        *Out = Node;
        return GR_OK;
    }

    FxPerpAppearance::~FxPerpAppearance()
    {
    }

    // Outer +0x14 -> FUN_06233040. See FGenericFxObjectBase::UpdateListAppearance.
    GENRESULT DACOM_API FxPerpAppearance::Pad5(float Dt)
    {
        G_PerpDrawContext.Field_0xA = 0;
        G_PerpDrawContext.PrimCount = 0;
        Age_ = static_cast<float>(static_cast<double>(Dt) + Age_);
        if (EmitterRefCount_ == 0 && InstanceListHead_ == nullptr)
            NodeFlags_ |= 1;
        const FFxTextureKey Key = {"???", Descriptor_->TextureCrc};
        Texture_.SetTexture(&Key);
        if (Texture_.Handle == -1)
            Texture_.Resolve();
        if ((Descriptor_->Flags & 8u) != 0)
        {
            const float Position = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, Age_));
            if (Texture_.FrameCount != 1 && Texture_.Handle != -1)
            {
                const int Handle = Texture_.Handle;
                const std::uint32_t Span = Texture_.FrameCount - 1u;
                ITextureLibrary2* const Library = static_cast<ITextureLibrary2*>(G_TextureLibrary);
                const std::int64_t Frame = static_cast<std::int64_t>(static_cast<double>(Span) * Position);
                Library->GetFrame(Handle, static_cast<int>(Frame),
                    reinterpret_cast<unsigned long*>(Texture_.Frame));
            }
        }
        else if (Texture_.FrameCount != 1)
            Texture_.Advance(Dt);
        ApplyFieldsToPool(this, false);
        IntegratePool(this, &G_PoolPerp, Dt);
        return GR_OK;
    }

    // FUN_06232cc0: XYZ distance retains its unrounded squared length; the peeled
    // tail merge re-evaluates the head's age. Tail is captured after the first callback.
    GENRESULT DACOM_API FxPerpAppearance::GetBoundingSphere(float* OutSphere)
    {
        const FPooledParticle* Head = InstanceListHead_;
        if (Head == nullptr)
            return GR_GENERIC;
        const float Scale = static_cast<float>(ForwardAppearanceSingleEval(
            Descriptor_->EffectScaleRamp, AppearanceOuterTime_, LodInput_));
        const float FirstInnerTime = static_cast<float>(static_cast<double>(Head->InvDuration) * Head->Age);
        const double FirstRadius = ForwardAppearanceSingleEval(
            Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, FirstInnerTime) * Scale;
        std::memcpy(OutSphere, &Head->PosX, sizeof(float) * 2);
        OutSphere[3] = static_cast<float>(FirstRadius);
        std::memcpy(OutSphere + 2, &Head->PosZ, sizeof(float));
        const FPooledParticle* Tail = InstanceListTail_;
        if (Head != Tail)
        {
            const float TailInnerTime = static_cast<float>(static_cast<double>(Head->InvDuration) * Head->Age);
            const double TailRadius = ForwardAppearanceSingleEval(
                Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, TailInnerTime) * Scale;
            float TailSphere[4];
            std::memcpy(TailSphere, &Tail->PosX, sizeof(float) * 2);
            TailSphere[3] = static_cast<float>(TailRadius);
            std::memcpy(TailSphere + 2, &Tail->PosZ, sizeof(float));
            MergePerpRectBoundsSphere(OutSphere, TailSphere, TailSphere[3],
                EPerpRectBoundsDistance::PerpUnroundedXyz);
            const FPooledParticle* Particle = Head->Next;
            for (; Particle != Tail; Particle = Particle->Next)
            {
                const float InnerTime = static_cast<float>(static_cast<double>(Particle->InvDuration) * Particle->Age);
                const double Radius = ForwardAppearanceSingleEval(
                    Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, InnerTime) * Scale;
                float Sphere[4];
                std::memcpy(Sphere, &Particle->PosX, sizeof(float) * 2);
                Sphere[3] = static_cast<float>(Radius);
                std::memcpy(Sphere + 2, &Particle->PosZ, sizeof(float));
                MergePerpRectBoundsSphere(OutSphere, Sphere, Sphere[3],
                    EPerpRectBoundsDistance::PerpUnroundedXyz);
            }
        }
        FVector WorldCenter;
        TransformPerpBoundsCenter(*reinterpret_cast<const FTransform*>(NodeWorldXform_),
            WorldCenter, *reinterpret_cast<const FVector*>(OutSphere));
        float WorldSphere[4];
        std::memcpy(WorldSphere, &WorldCenter, sizeof(WorldCenter));
        std::memcpy(WorldSphere + 3, OutSphere + 3, sizeof(float));
        std::memcpy(OutSphere, WorldSphere, sizeof(WorldSphere));
        return GR_OK;
    }

    // Outer +0xBC -> FUN_06235db0: FUN_06234ab0 (FGenericFxObjectBase::
    // SpawnListParticle), then seed the new tail's axis at +0x30.
    //   0x06235e15: (Six[3] - 1)^2, Six[4]^2 and Six[5]^2 are each compared against
    //     DAT_0624eb68 (~1.0e-6): FCOMP; TEST AH,0x41; JP -> LAB_06235e84. Parity is
    //     even when neither C0 nor C3 is set, i.e. the square is greater than the
    //     epsilon, and that jump is taken.
    //   All three within the epsilon (velocity ~ +X): axis = (0, 0, 1) (0x06235e5d).
    //   Any one outside it: axis = (1, 0, 0) (LAB_06235e84).
    // Returns 0.
    GENRESULT DACOM_API FxPerpAppearance::Pad47(const float* Six, float Age, float Duration)
    {
        SpawnListParticle(&G_PoolPerp, Six, Age, Duration);
        const float Epsilon = PerpAxisParallelEpsilon();
        const double DeltaX = static_cast<double>(Six[3]) - 1.0f;
        const bool bParallel = DeltaX * DeltaX <= Epsilon &&
            static_cast<double>(Six[4]) * Six[4] <= Epsilon &&
            static_cast<double>(Six[5]) * Six[5] <= Epsilon;
        FVector* const Axis = reinterpret_cast<FVector*>(
            reinterpret_cast<unsigned char*>(InstanceListTail_) + 0x30);
        if (bParallel)
        {
            Axis->X = 0.0f;
            Axis->Y = 0.0f;
            Axis->Z = 1.0f;
        }
        else
        {
            Axis->X = 1.0f;
            Axis->Y = 0.0f;
            Axis->Z = 0.0f;
        }
        return GR_OK;
    }

    // FUN_06233170 (node vtable 0x0624e8b0 +0x10; outer +0x18 thunk FUN_06235700 / 0x06235010),
    // traced 2026-09-26 against 0x06233170-0x06233328:
    //   node+0xd8 list empty (begin == end) -> return 0;
    //   DAT_06257919 (batcher) -> FUN_06201c70(2, [node+0xac]) and return 0;
    //   counts from [node+0xac] vt+0xd0 / +0xd4 (locals zeroed first); FUN_06240f50(
    //     DAT_06257aec, 4, verts, indices) false -> return 0;
    //   FUN_06233330 (fill); then the end-and-draw tail FUN_06235a60 also runs (see
    //   EndBatchedDraw). Return 0 on every path.
    GENRESULT DACOM_API FxPerpAppearance::DrawOrEnqueue()
    {
        if (InstanceListHead_ == nullptr)
            return GR_OK;

        if (G_Defaults.bUseMaterialBatcher != 0)
        {
            EnqueueAppearanceToList(2, this);
            return GR_OK;
        }

        unsigned long VertCount = 0;
        unsigned long IndexCount = 0;
        GetQueuedVertCount(&VertCount);
        GetQueuedIndexCount(&IndexCount);
        if (!AlDrawContextBegin(G_PerpDrawContext, 4, VertCount, IndexCount))
            return GR_OK;

        FillGeometry();

        // 0x06233250 onward is the same sequence as the outer +0xA0 body FUN_06235a60.
        EndBatchedDraw();
        return GR_OK;
    }

    // FUN_06235a40 (outer +0x9C, RET 0xc): FUN_06240f50(DAT_06257aec, 4, Verts, Indices)
    // folded to 0 / -1. The perp appearance has its own draw context DAT_06257aec; the
    // port keeps one context, which is equivalent because no two draw contexts are ever
    // open at once (each Begin is closed by its End before the next list or appearance).
    GENRESULT DACOM_API FxPerpAppearance::BeginBatchedDraw(unsigned long Verts,
                                                          unsigned long Indices)
    {
        return AlDrawContextBegin(G_PerpDrawContext, 4, Verts, Indices) ? GR_OK : GR_GENERIC;
    }

    // FUN_06235a60 (outer +0xA0, RET 4), traced 2026-09-26 against 0x06235a60-0x06235b31.
    // ESI = outer: [ESI+0xfc] = node+0xf0 (descriptor), [ESI+0xc8] = node+0xbc,
    // ESI+0x58 = node+0x4c.
    //   FUN_06241070 (end) == 0 -> return 0;
    //   ++DAT_062578e8 (G_DrawCounters[1]);
    //   SetRenderState(0x13, [desc+0x68]), SetRenderState(0x14, [desc+0x6c]);
    //   SetTexture(0, [node+0xbc]);
    //   math vt+0x1c MulTransform(local, camera vt+0x40, node+0x4c), copied (REP MOVSD 0xc)
    //     and handed to SetModelview (vt+0x7c) -- the fill wrote node-local vertices;
    //   FUN_06241180 (draw); return 0.
    // The immediate draw FUN_06233170 runs the same sequence inline from 0x06233250.
    GENRESULT DACOM_API FxPerpAppearance::EndBatchedDraw()
    {
        if (AlDrawContextEnd(G_PerpDrawContext) == 0)
            return GR_OK;
        ++G_DrawCounters[1];

        G_RenderPipeline->SetRenderState(D3DRS_SRCBLEND, static_cast<int>(Descriptor_->BlendInfo[0]));
        G_RenderPipeline->SetRenderState(D3DRS_DESTBLEND, static_cast<int>(Descriptor_->BlendInfo[1]));

        void* FrameTexture = nullptr;
        std::memcpy(&FrameTexture, &Texture_.Frame[0], sizeof(FrameTexture));
        G_RenderPipeline->SetTextureStageTexture(0, FrameTexture);

        ICamera* const Camera = reinterpret_cast<ICamera*>(static_cast<std::uintptr_t>(G_DrawCookie));
        const float* const View = Camera->GetViewTransform();
        if (G_Perp3DMath == nullptr)
            G_Perp3DMath = Create3DMath();
        FTransform ViewWorld;
        G_Perp3DMath->MulTransform(ViewWorld, *reinterpret_cast<const FTransform*>(View),
                                   *reinterpret_cast<const FTransform*>(NodeWorldXform_));
        FTransform Modelview;
        std::memcpy(&Modelview, &ViewWorld, sizeof(Modelview));
        G_RenderPipeline->SetModelview(reinterpret_cast<const float*>(&Modelview));

        AlDrawContextDraw(G_PerpDrawContext);
        return GR_OK;
    }

    // FUN_06235b40 (outer +0xA4): `LEA ECX,[this+0xc]; CALL FUN_06233330; XOR EAX,EAX`.
    GENRESULT DACOM_API FxPerpAppearance::FillBatchedGeometry()
    {
        FillGeometry();
        return GR_OK;
    }

    // FUN_06233330: Perp basis, camera fade, atlas and four-vertex list walk.
#pragma float_control(except, on, push)
    void FxPerpAppearance::FillGeometry()
    {
        const float TinyLength = FloatFromBits(0x358637bdu);
        const float TinyAxisSquared = FloatFromBits(0x322bcc76u);
        const float FacingCull = FloatFromBits(0x3f7c1c5cu);
        const float FacingFade = FloatFromBits(0x3f5db3d7u);
        const float FacingFadeScale = FloatFromBits(0x4106b33du);

        ++G_DrawCounters[5];
        FPooledParticle* Particle = InstanceListHead_;
        const bool bFrameLookup = Texture_.FrameCount != 1 && (Descriptor_->Flags & 8u) == 0;
        const bool bCameraFade = (Descriptor_->Flags & 0x10u) != 0;
        FTransform InvNode;
        float InverseW;
        ComputeGeneralInverseTransform(*reinterpret_cast<const FTransform*>(NodeWorldXform_),
                                       InvNode, InverseW);
        float FrameBlock[5];
        std::memcpy(FrameBlock, Texture_.Frame, sizeof(FrameBlock));
        if ((Descriptor_->Flags & 0x1000u) != 0)
        {
            std::uint32_t Previous;
            std::memcpy(&Previous, &FrameBlock[3], sizeof(Previous));
            FrameBlock[3] = static_cast<float>(LoadAppearanceScalar(FrameBlock[1]));
            std::memcpy(&FrameBlock[1], &Previous, sizeof(Previous));
        }
        if ((Descriptor_->Flags & 0x2000u) != 0)
        {
            std::uint32_t Previous;
            std::memcpy(&Previous, &FrameBlock[4], sizeof(Previous));
            FrameBlock[4] = static_cast<float>(LoadAppearanceScalar(FrameBlock[2]));
            std::memcpy(&FrameBlock[2], &Previous, sizeof(Previous));
        }
        std::int32_t LastFrame = -1;
        ICamera* const Camera = reinterpret_cast<ICamera*>(static_cast<std::uintptr_t>(G_DrawCookie));
        FVector CameraPosition;
        std::memcpy(&CameraPosition, Camera->GetPosition(), sizeof(CameraPosition));
        const float Scale = static_cast<float>(ForwardAppearanceSingleEval(
            Descriptor_->EffectScaleRamp, AppearanceOuterTime_, LodInput_));

        for (; Particle != nullptr; Particle = Particle->Next)
        {
            const float InnerTime = static_cast<float>(static_cast<double>(Particle->Age) * Particle->InvDuration);
            float Alpha = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->SizeRamp, AppearanceOuterTime_, InnerTime));
            const double UnroundedHalfSize = (ForwardAppearanceSingleEval(
                Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, InnerTime) * Scale) * 0.5;
            const float HalfSize = static_cast<float>(UnroundedHalfSize);
            // TEST AH,41 / JNP skips ordered less/equal, but permits unordered.
            if (UnroundedHalfSize <= TinyLength || Alpha <= TinyLength)
                continue;

            float* const Axis = reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(Particle) + 0x30);
            FVector Velocity;
            std::memcpy(&Velocity, &Particle->VelX, sizeof(Velocity));
            FVector PreviousAxis;
            std::memcpy(&PreviousAxis, Axis, sizeof(PreviousAxis));
            const double VelocitySquared = ((static_cast<double>(Velocity.X) * Velocity.X) +
                (static_cast<double>(Velocity.Z) * Velocity.Z)) + (static_cast<double>(Velocity.Y) * Velocity.Y);
            const float RoundedVelocitySquared = static_cast<float>(VelocitySquared);
            std::int32_t VelocityBits;
            std::memcpy(&VelocityBits, &RoundedVelocitySquared, sizeof(VelocityBits));
            const std::uint32_t VelocityGuessBits = 0x5f3759dfu - static_cast<std::uint32_t>(VelocityBits >> 1);
            float VelocityGuess;
            std::memcpy(&VelocityGuess, &VelocityGuessBits, sizeof(VelocityGuess));
            const double VelocityInverse = (1.5 - (((static_cast<double>(VelocityGuess) * VelocityGuess) *
                VelocitySquared) * 0.5)) * VelocityGuess;
            const double VelocityLength = VelocitySquared * VelocityInverse;
            if (!(VelocityLength <= TinyLength))
            {
                const double InverseLength = 1.0 / VelocityLength;
                Velocity.X = static_cast<float>(static_cast<double>(Velocity.X) * InverseLength);
                Velocity.Y = static_cast<float>(static_cast<double>(Velocity.Y) * InverseLength);
                Velocity.Z = static_cast<float>(static_cast<double>(Velocity.Z) * InverseLength);
            }
            else
            {
                Velocity.X = 0.0f;
                Velocity.Y = 1.0f;
                Velocity.Z = 0.0f;
                const double DifferenceX = -static_cast<double>(PreviousAxis.X);
                const double DifferenceY = 1.0 - PreviousAxis.Y;
                const double DifferenceZ = -static_cast<double>(PreviousAxis.Z);
                if (DifferenceX * DifferenceX <= TinyAxisSquared &&
                    DifferenceY * DifferenceY <= TinyAxisSquared &&
                    DifferenceZ * DifferenceZ <= TinyAxisSquared)
                {
                    Velocity.X = 1.0f;
                    Velocity.Y = 0.0f;
                }
            }
            FVector Position;
            std::memcpy(&Position, &Particle->PosX, sizeof(Position));
            if (bCameraFade)
            {
                if (G_Perp3DMath == nullptr)
                    G_Perp3DMath = Create3DMath();
                FVector LocalCamera;
                G_Perp3DMath->TransformXform(LocalCamera, InvNode, CameraPosition);
                FVector ToCamera;
                ToCamera.X = static_cast<float>(static_cast<double>(LocalCamera.X) - Position.X);
                ToCamera.Y = static_cast<float>(static_cast<double>(LocalCamera.Y) - Position.Y);
                ToCamera.Z = static_cast<float>(static_cast<double>(LocalCamera.Z) - Position.Z);
                const double CameraSquared = ((static_cast<double>(ToCamera.X) * ToCamera.X) +
                    (static_cast<double>(ToCamera.Z) * ToCamera.Z)) + (static_cast<double>(ToCamera.Y) * ToCamera.Y);
                const float RoundedCameraSquared = static_cast<float>(CameraSquared);
                std::int32_t CameraBits;
                std::memcpy(&CameraBits, &RoundedCameraSquared, sizeof(CameraBits));
                const std::uint32_t CameraGuessBits = 0x5f3759dfu - static_cast<std::uint32_t>(CameraBits >> 1);
                float CameraGuess;
                std::memcpy(&CameraGuess, &CameraGuessBits, sizeof(CameraGuess));
                const double CameraInverse = (1.5 - (((static_cast<double>(CameraGuess) * CameraGuess) *
                    CameraSquared) * 0.5)) * CameraGuess;
                const double CameraLength = CameraSquared * CameraInverse;
                if (!(CameraLength <= TinyLength))
                {
                    const double InverseLength = 1.0 / CameraLength;
                    ToCamera.X = static_cast<float>(static_cast<double>(ToCamera.X) * InverseLength);
                    ToCamera.Y = static_cast<float>(static_cast<double>(ToCamera.Y) * InverseLength);
                    ToCamera.Z = static_cast<float>(static_cast<double>(ToCamera.Z) * InverseLength);
                }
                else
                {
                    ToCamera.X = 0.0f;
                    ToCamera.Y = 1.0f;
                    ToCamera.Z = 0.0f;
                }
                const double Dot = ((static_cast<double>(ToCamera.X) * Velocity.X) +
                    (static_cast<double>(ToCamera.Z) * Velocity.Z)) + (static_cast<double>(ToCamera.Y) * Velocity.Y);
                const double UnroundedFacing = 1.0 - std::fabs(Dot);
                const float Facing = static_cast<float>(UnroundedFacing);
                // TEST AX,4100 / JZ skips only ordered greater, using the unrounded value.
                if (UnroundedFacing > FacingCull)
                    continue;
                if (Facing > FacingFade)
                {
                    const double Fade = (static_cast<double>(FacingCull) - Facing) * FacingFadeScale;
                    Alpha = static_cast<float>((static_cast<double>(Alpha) * Fade) * Fade);
                }
                if (Alpha <= TinyLength)
                    continue;
            }
            const unsigned long PackedColor = ForwardAppearanceColorEval(
                Descriptor_->ColorRamp, AppearanceOuterTime_, InnerTime);
            const std::int64_t AlphaInteger = static_cast<std::int64_t>(static_cast<double>(Alpha) * 255.0f);
            const unsigned long Diffuse = PackedColor | (static_cast<unsigned long>(AlphaInteger) << 24);
            if (bFrameLookup)
            {
                const double FramePosition = ForwardAppearanceSingleEval(
                    Descriptor_->TexFrameRamp, AppearanceOuterTime_, InnerTime);
                const std::int32_t FrameCountMinusOne = static_cast<std::int32_t>(Texture_.FrameCount - 1u);
                const std::int64_t FrameInteger = static_cast<std::int64_t>(FramePosition * FrameCountMinusOne);
                const std::int32_t Frame = static_cast<std::int32_t>(FrameInteger);
                if (Frame != LastFrame)
                {
                    LastFrame = Frame;
                    Texture_.LookupFrame(Frame, FrameBlock);
                    if ((Descriptor_->Flags & 0x1000u) != 0)
                    {
                        std::uint32_t Previous;
                        std::memcpy(&Previous, &FrameBlock[1], sizeof(Previous));
                        FrameBlock[1] = static_cast<float>(LoadAppearanceScalar(FrameBlock[3]));
                        std::memcpy(&FrameBlock[3], &Previous, sizeof(Previous));
                    }
                    if ((Descriptor_->Flags & 0x2000u) != 0)
                    {
                        std::uint32_t Previous;
                        std::memcpy(&Previous, &FrameBlock[2], sizeof(Previous));
                        FrameBlock[2] = static_cast<float>(LoadAppearanceScalar(FrameBlock[4]));
                        std::memcpy(&FrameBlock[4], &Previous, sizeof(Previous));
                    }
                }
            }
            const float NormalX = static_cast<float>((static_cast<double>(Velocity.Z) * PreviousAxis.Y) -
                (static_cast<double>(Velocity.Y) * PreviousAxis.Z));
            const float NormalY = static_cast<float>((static_cast<double>(PreviousAxis.Z) * Velocity.X) -
                (static_cast<double>(Velocity.Z) * PreviousAxis.X));
            const float NormalZ = static_cast<float>((static_cast<double>(Velocity.Y) * PreviousAxis.X) -
                (static_cast<double>(PreviousAxis.Y) * Velocity.X));
            const float NormalSquared = static_cast<float>(((static_cast<double>(NormalX) * NormalX) +
                (static_cast<double>(NormalZ) * NormalZ)) + (static_cast<double>(NormalY) * NormalY));
            std::int32_t NormalBits;
            std::memcpy(&NormalBits, &NormalSquared, sizeof(NormalBits));
            const std::uint32_t NormalGuessBits = 0x5f3759dfu - static_cast<std::uint32_t>(NormalBits >> 1);
            float NormalGuess;
            std::memcpy(&NormalGuess, &NormalGuessBits, sizeof(NormalGuess));
            const double NormalInverse = (1.5 - (((static_cast<double>(NormalGuess) * NormalGuess) *
                NormalSquared) * 0.5)) * NormalGuess;
            const double NormalizedX = static_cast<double>(NormalX) * NormalInverse;
            const float NormalizedY = static_cast<float>(static_cast<double>(NormalY) * NormalInverse);
            const double UnroundedNormalizedZ = static_cast<double>(NormalZ) * NormalInverse;
            const float NormalizedZ = static_cast<float>(UnroundedNormalizedZ);
            FVector NewAxis;
            NewAxis.X = static_cast<float>((UnroundedNormalizedZ * Velocity.Y) -
                (static_cast<double>(NormalizedY) * Velocity.Z));
            NewAxis.Y = static_cast<float>((static_cast<double>(Velocity.Z) * NormalizedX) -
                (static_cast<double>(NormalizedZ) * Velocity.X));
            NewAxis.Z = static_cast<float>((static_cast<double>(NormalizedY) * Velocity.X) -
                (static_cast<double>(Velocity.Y) * NormalizedX));
            std::memcpy(Axis, &NewAxis, sizeof(NewAxis));
            const float ScaledNormalX = static_cast<float>(NormalizedX * HalfSize);
            const float ScaledNormalY = static_cast<float>(static_cast<double>(NormalizedY) * HalfSize);
            const float ScaledNormalZ = static_cast<float>(static_cast<double>(NormalizedZ) * HalfSize);
            const float ScaledAxisX = static_cast<float>(static_cast<double>(NewAxis.X) * HalfSize);
            const float ScaledAxisY = static_cast<float>(static_cast<double>(NewAxis.Y) * HalfSize);
            const double UnroundedScaledAxisZ = static_cast<double>(NewAxis.Z) * HalfSize;
            const float ScaledAxisZ = static_cast<float>(UnroundedScaledAxisZ);
            const float SumX = static_cast<float>(static_cast<double>(ScaledAxisX) + ScaledNormalX);
            const float SumY = static_cast<float>(static_cast<double>(ScaledAxisY) + ScaledNormalY);
            const float SumZ = static_cast<float>(UnroundedScaledAxisZ + ScaledNormalZ);
            const double DifferenceX = static_cast<double>(ScaledAxisX) - ScaledNormalX;
            const double DifferenceY = static_cast<double>(ScaledAxisY) - ScaledNormalY;
            const double DifferenceZ = static_cast<double>(ScaledAxisZ) - ScaledNormalZ;
            FAlDrawContext& Context = G_PerpDrawContext;
            const double V0 = FrameBlock[2];
            const double U1 = FrameBlock[3];
            unsigned short IndexA;
            if (static_cast<long>(Context.VertCount) > static_cast<long>(Context.RequestedVerts))
                IndexA = static_cast<unsigned short>(Context.RequestedVerts);
            else
            {
                FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(Context.LockedVerts);
                Vertex->Diffuse = Diffuse;
                Vertex->X = static_cast<float>(DifferenceX + Position.X);
                Vertex->Y = static_cast<float>(DifferenceY + Position.Y);
                Vertex->Z = static_cast<float>(DifferenceZ + Position.Z);
                Vertex->U = static_cast<float>(U1);
                Vertex->V = static_cast<float>(V0);
                Context.LockedVerts = Vertex + 1;
                IndexA = Context.VertCount;
                Context.VertCount = static_cast<unsigned short>(Context.VertCount + 1);
            }
            const double VertexBY = static_cast<double>(Position.Y) - SumY;
            const float VertexBZ = static_cast<float>(static_cast<double>(Position.Z) - SumZ);
            unsigned short IndexB;
            if (static_cast<long>(Context.VertCount) > static_cast<long>(Context.RequestedVerts))
                IndexB = static_cast<unsigned short>(Context.RequestedVerts);
            else
            {
                FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(Context.LockedVerts);
                std::memcpy(&Vertex->Z, &VertexBZ, sizeof(VertexBZ));
                std::memcpy(&Vertex->U, &FrameBlock[1], sizeof(Vertex->U));
                Vertex->X = static_cast<float>(static_cast<double>(Position.X) - SumX);
                Vertex->Diffuse = Diffuse;
                std::memcpy(&Vertex->V, &FrameBlock[2], sizeof(Vertex->V));
                Vertex->Y = static_cast<float>(VertexBY);
                Context.LockedVerts = Vertex + 1;
                IndexB = Context.VertCount;
                Context.VertCount = static_cast<unsigned short>(Context.VertCount + 1);
            }
            const double VertexCY = static_cast<double>(SumY) + Position.Y;
            const float VertexCZ = static_cast<float>(static_cast<double>(SumZ) + Position.Z);
            unsigned short IndexC;
            if (static_cast<long>(Context.VertCount) > static_cast<long>(Context.RequestedVerts))
                IndexC = static_cast<unsigned short>(Context.RequestedVerts);
            else
            {
                FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(Context.LockedVerts);
                std::memcpy(&Vertex->Z, &VertexCZ, sizeof(VertexCZ));
                std::memcpy(&Vertex->U, &FrameBlock[3], sizeof(Vertex->U));
                Vertex->X = static_cast<float>(static_cast<double>(SumX) + Position.X);
                Vertex->Diffuse = Diffuse;
                std::memcpy(&Vertex->V, &FrameBlock[4], sizeof(Vertex->V));
                Vertex->Y = static_cast<float>(VertexCY);
                Context.LockedVerts = Vertex + 1;
                IndexC = Context.VertCount;
                Context.VertCount = static_cast<unsigned short>(Context.VertCount + 1);
            }
            unsigned short IndexD;
            if (static_cast<long>(Context.VertCount) > static_cast<long>(Context.RequestedVerts))
                IndexD = static_cast<unsigned short>(Context.RequestedVerts);
            else
            {
                FAlchemyVertex Vertex;
                Vertex.V = FrameBlock[4];
                Vertex.U = FrameBlock[1];
                Vertex.Diffuse = Diffuse;
                Vertex.Z = static_cast<float>(static_cast<double>(Position.Z) - DifferenceZ);
                Vertex.Y = static_cast<float>(static_cast<double>(Position.Y) - DifferenceY);
                Vertex.X = static_cast<float>(static_cast<double>(Position.X) - DifferenceX);
                FAlchemyVertex* const Out = static_cast<FAlchemyVertex*>(Context.LockedVerts);
                std::memcpy(Out, &Vertex, sizeof(Vertex));
                Context.LockedVerts = Out + 1;
                IndexD = Context.VertCount;
                Context.VertCount = static_cast<unsigned short>(Context.VertCount + 1);
            }
            StoreQuadIndices(Context, IndexA, IndexB, IndexC, IndexD);

        }
    }
#pragma float_control(pop)

    FxOrientedAppearance::FxOrientedAppearance(const char* PrimaryIid, const char* FactoryName)
        : FFxBillboardAppearance(PrimaryIid, FactoryName, &kOrientedClass)
    {
    }

    void FxOrientedAppearance::CreatePropertySet()
    {
        AdoptBillboardSet(&BuildOrientedDescriptorRamps);
    }

    // LAB_0622e950: four vertices per node-local particle.
    GENRESULT DACOM_API FxOrientedAppearance::GetQueuedVertCount(unsigned long* Out)
    {
        *Out = LiveInstanceCount_ << 2;
        return GR_OK;
    }

    // LAB_0622e970: six indices per node-local particle.
    GENRESULT DACOM_API FxOrientedAppearance::GetQueuedIndexCount(unsigned long* Out)
    {
        *Out = LiveInstanceCount_ * 6U;
        return GR_OK;
    }

    // FUN_0622bbc0 (RET 8): the oriented constructor inline (as its factory 0x0622d7c6..),
    // FUN_06244120 (0x0622bc86), the +0xf0 set cache (0x0622bc8b..0x0622bc95); *Out, 0.
    GENRESULT DACOM_API FxOrientedAppearance::CloneNode(IDAComponent** Out)
    {
        FxOrientedAppearance* const Node = new (std::nothrow) FxOrientedAppearance(PrimaryIid_, FactoryName_);
        Node->InitAppearanceFromArchetype(this);
        // 0622bc8b..bc95 reloads the prototype cache after initialization callbacks.
        Node->Descriptor_ = Descriptor_;
        *Out = Node;
        return GR_OK;
    }

    FxOrientedAppearance::~FxOrientedAppearance()
    {
    }

    // Outer +0x14 -> FUN_0622c3d0. See FGenericFxObjectBase::UpdateListAppearance.
    GENRESULT DACOM_API FxOrientedAppearance::Pad5(float Dt)
    {
        G_OrientedDrawContext.Field_0xA = 0;
        G_OrientedDrawContext.PrimCount = 0;
        Age_ = static_cast<float>(static_cast<double>(Dt) + Age_);
        if (EmitterRefCount_ == 0 && InstanceListHead_ == nullptr)
            NodeFlags_ |= 1;
        const FFxTextureKey Key = {"???", Descriptor_->TextureCrc};
        Texture_.SetTexture(&Key);
        if (Texture_.Handle == -1)
            Texture_.Resolve();
        if ((Descriptor_->Flags & 8u) != 0)
        {
            const float Position = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->CommonTexFrameRamp, AppearanceOuterTime_, Age_));
            if (Texture_.FrameCount != 1 && Texture_.Handle != -1)
            {
                const std::uint32_t Span = Texture_.FrameCount - 1u;
                const std::int64_t Frame = static_cast<std::int64_t>(static_cast<double>(Span) * Position);
                static_cast<ITextureLibrary2*>(G_TextureLibrary)->GetFrame(Texture_.Handle,
                    static_cast<int>(Frame), reinterpret_cast<unsigned long*>(Texture_.Frame));
            }
        }
        else if (Texture_.FrameCount != 1)
            Texture_.Advance(Dt);
        ApplyFieldsToPool(this, false);
        IntegratePool(this, &G_PoolOriented, Dt);
        return GR_OK;
    }

    // Outer +0x8C -> FUN_0622eca0 (`ADD [ESP+4],0xc; JMP`) -> FUN_0622bed0, RET 8 --
    // GetBoundingSphere(Out), listing 90288-90700. Same body as rect's FUN_06238280:
    // list at node+0xd8 (FUN_0622d220 begin = head, FUN_0622d240 end = null,
    // FUN_0622d2a0 = [list+4] tail), Scale P+0x5c @ (node+0x8c, node+0x90), radius per
    // ListBoundsRadius (oriented max), the peeled tail pass at t0, the inlined merge,
    // and the node+0x4c transform through the cached math engine DAT_06257ac0 vt+0x28 +
    // FUN_06208a70. See ListBoundsSphere for the term-by-term reading.
    GENRESULT DACOM_API FxOrientedAppearance::GetBoundingSphere(float* OutSphere)
    {
        const FPooledParticle* const Head = InstanceListHead_;
        if (Head == nullptr)
            return GR_GENERIC;
        const float Scale = static_cast<float>(ForwardAppearanceSingleEval(
            Descriptor_->EffectScaleRamp, AppearanceOuterTime_, LodInput_));
        float FirstOuterTime;
        std::memcpy(&FirstOuterTime, &AppearanceOuterTime_, sizeof(float));
        const float FirstInnerTime = static_cast<float>(static_cast<double>(Head->InvDuration) * Head->Age);
        const float FirstPrimary = static_cast<float>(ForwardAppearanceSingleEval(
            Descriptor_->PrimarySizeRamp, FirstOuterTime, FirstInnerTime) * Scale);
        const float FirstB = static_cast<float>(ForwardAppearanceSingleEval(
            Descriptor_->TexFrameRamp, AppearanceOuterTime_, FirstInnerTime));
        const double FirstC = ForwardAppearanceSingleEval(
            Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, FirstInnerTime);
        const double FirstSide = FirstC > static_cast<double>(FirstB) ? FirstC : FirstB;
        const double FirstRadius = FirstSide * FirstPrimary;
        std::memcpy(OutSphere, &Head->PosX, sizeof(float) * 2);
        OutSphere[3] = static_cast<float>(FirstRadius);
        std::memcpy(OutSphere + 2, &Head->PosZ, sizeof(float));
        const FPooledParticle* const Tail = InstanceListTail_;
        if (Head != Tail)
        {
            const float TailInnerTime = static_cast<float>(static_cast<double>(Head->InvDuration) * Head->Age);
            const float TailPrimary = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, TailInnerTime) * Scale);
            const float TailB = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->TexFrameRamp, AppearanceOuterTime_, TailInnerTime));
            const double TailC = ForwardAppearanceSingleEval(
                Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, TailInnerTime);
            const double TailSide = TailC > static_cast<double>(TailB) ? TailC : TailB;
            const double TailRadius = TailSide * TailPrimary;
            float TailSphere[4];
            std::memcpy(TailSphere, &Tail->PosX, sizeof(float) * 2);
            TailSphere[3] = static_cast<float>(TailRadius);
            std::memcpy(TailSphere + 2, &Tail->PosZ, sizeof(float));
            MergePerpRectBoundsSphere(OutSphere, TailSphere, TailSphere[3],
                EPerpRectBoundsDistance::RectRoundedYzx);
            const FPooledParticle* Particle = Head->Next;
            for (; Particle != Tail; Particle = Particle->Next)
            {
                const float InnerTime = static_cast<float>(static_cast<double>(Particle->InvDuration) * Particle->Age);
                const float Primary = static_cast<float>(ForwardAppearanceSingleEval(
                    Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, InnerTime) * Scale);
                const float B = static_cast<float>(ForwardAppearanceSingleEval(
                    Descriptor_->TexFrameRamp, AppearanceOuterTime_, InnerTime));
                const double C = ForwardAppearanceSingleEval(
                    Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, InnerTime);
                const double Side = C > static_cast<double>(B) ? C : B;
                const double Radius = Side * Primary;
                float Sphere[4];
                std::memcpy(Sphere, &Particle->PosX, sizeof(float) * 2);
                Sphere[3] = static_cast<float>(Radius);
                std::memcpy(Sphere + 2, &Particle->PosZ, sizeof(float));
                MergePerpRectBoundsSphere(OutSphere, Sphere, Sphere[3],
                    EPerpRectBoundsDistance::RectRoundedYzx);
            }
        }
        FVector WorldCenter;
        TransformOrientedBoundsCenter(*reinterpret_cast<const FTransform*>(NodeWorldXform_),
            WorldCenter, *reinterpret_cast<const FVector*>(OutSphere));
        float WorldSphere[4];
        std::memcpy(WorldSphere, &WorldCenter, sizeof(WorldCenter));
        std::memcpy(WorldSphere + 3, OutSphere + 3, sizeof(float));
        std::memcpy(OutSphere, WorldSphere, sizeof(WorldSphere));
        return GR_OK;
    }

    // Outer +0xBC -> FUN_0622ee90 -> FUN_0622bd80. See
    // FGenericFxObjectBase::SpawnListParticle.
    GENRESULT DACOM_API FxOrientedAppearance::Pad47(const float* Six, float Age, float Duration)
    {
        SpawnListParticle(&G_PoolOriented, Six, Age, Duration);
        return GR_OK;
    }

    // FUN_0622c500, __stdcall on the node. Traced 2026-09-26 against alchemy.dll.txt
    // 0x0622c500-0x0622d0a9; offsets are ESP-relative after the four register pushes, with
    // every pending PUSH / SUB ESP taken off.
    //   ++DAT_062578fc (G_DrawCounters[6]) first, then node+0xd8 empty -> return 0.
    //   bFrameLookup [0x17] = node+0xd0 != 1 && (flags & 8) == 0.
    //   [node+0xac] vt+0xd0 / +0xd4 counts (4 and 6 per particle, LAB_0622e950 / e970);
    //   FUN_06240f50(&DAT_06257a74, 4, V, I) false -> return 0. No batcher branch.
    //   bCameraFade [0x1f] = flags & 0x40: InvNode [0x164] = FUN_0622d2e0(node+0x4c)
    //     (GeneralInverse), CameraPos [0x134] = camera vt+0xc.
    //   Frame block [0x40] = node+0xbc (5 dwords) with the 0x1000 / 0x2000 flips;
    //   LastFrame [0x80] = -1; Scale [0xa4] = +0x5c ramp (node+0x8c, node+0x90).
    //   Per particle (node+0xd8 order), t = [+0x2c] * [+0x24] (kept in ESI):
    //     Alpha [0x3c] = +0x70; SS [0x10] = +0x78 * Scale; H [0x18] = +0x7c * SS (it
    //     overwrites t's spill); W [0x70] = +0x80 * SS.
    //     Alpha == 0 -> next (TEST AH,0x44 / JNP); SS < 1e-11 (0x2d2febff) -> next
    //     (TEST AH,0x05 / JNP).
    //     P [0x20] = [+0xc]. +0x8c IAnimatedTransform: vt+0x20 IsIdentity false ->
    //     vt+0x2c Evaluate(node+0x8c, t, &M [0xb4]) and P += M[9..11]; true -> M =
    //     FUN_06206c70 identity with M[9..11] = 0.
    //     bCameraFade: N [0xa8] = col0(M) x col1(M) = (M3*M7 - M4*M6, M6*M1 - M7*M0,
    //       M0*M4 - M3*M1); D [0x2c] = TransformXform(InvNode, CameraPos) - P, length over
    //       (x*x + z*z) + y*y with the 0x5f3759df step; |D| > 1e-6 or unordered -> D /= |D|
    //       (FDIVR 1.0), else (0,1,0). F [0x10] = 1.0 (double) - |(Nx*Dx + Dz*Nz) + Dy*Ny|;
    //       F > 0.98481 -> next; F > 0.866025 -> Alpha = (Alpha * g) * g with
    //       g = (0.98481 - F) * 8.41883; Alpha == 0 -> next.
    //     Diffuse [EDI] = +0x74 vt+0x20 | ftol(Alpha * 255) << 24.
    //     bFrameLookup: frame = ftol(+0x84 ramp * (node+0xd0 - 1)); a new frame ->
    //       FUN_06244f30(node+0xb4, frame, block) and the flips again.
    //     A [0x60] = col0(M) * W; B [0x54] = col1(M) * H.
    //     Vertices (A + P) + B (U1,V0), (P - A) + B (U0,V0), (A + P) - B (U1,V1),
    //       (P - A) - B (U0,V1) -- the last through FUN_06208a40 -- each skipped past the
    //       requested count (JG); indices (D, C, B, B, C, A) unless IndexCount + 5 >
    //       requested; the context's +0xc primitive counter += 2 (never read).
    //   End == 0 -> return 0; ++DAT_062578e8; blend 0x13 / 0x14 from [desc+0x68] /
    //   [desc+0x6c]; SetTexture(0, [node+0xbc]); SetModelview(FUN_06208640(camera vt+0x40,
    //   node+0x4c)); Draw; return 0.
    // The +0x8c transform is called unchecked, as in retail (the constructor always builds
    // it). Ramps and colour go through Eval / EvalPacked like the other fills.
    #pragma float_control(except, on, push)
    GENRESULT DACOM_API FxOrientedAppearance::DrawOrEnqueue()
    {
        const float TinyScale = FloatFromBits(0x2d2febffu);
        const float TinyLength = FloatFromBits(0x358637bdu);
        const float FacingCull = FloatFromBits(0x3f7c1c5cu);
        const float FacingFade = FloatFromBits(0x3f5db3d7u);
        const float FacingFadeScale = FloatFromBits(0x4106b33du);
        ++G_DrawCounters[6];
        const FPooledParticle* Particle = InstanceListHead_;
        if (Particle == nullptr)
            return GR_OK;
        const bool bFrameLookup = Texture_.FrameCount != 1 && (Descriptor_->Flags & 8u) == 0;
        unsigned long VertCount = 0;
        unsigned long IndexCount = 0;
        GetQueuedVertCount(&VertCount);
        GetQueuedIndexCount(&IndexCount);
        if (!AlDrawContextBegin(G_OrientedDrawContext, 4, VertCount, IndexCount))
            return GR_OK;
        const bool bCameraFade = (Descriptor_->Flags & 0x40u) != 0;
        FTransform InverseNode;
        FVector CameraPosition;
        if (bCameraFade)
        {
            FTransform InverseTemporary;
            float InverseW;
            ComputeGeneralInverseTransform(*reinterpret_cast<const FTransform*>(NodeWorldXform_),
                InverseTemporary, InverseW);
            std::memcpy(&InverseNode, &InverseTemporary, sizeof(InverseNode));
            ICamera* const Camera = reinterpret_cast<ICamera*>(static_cast<std::uintptr_t>(G_DrawCookie));
            std::memcpy(&CameraPosition, Camera->GetPosition(), sizeof(CameraPosition));
        }
        float FrameBlock[5];
        std::memcpy(FrameBlock, Texture_.Frame, sizeof(FrameBlock));
        const FAppearanceRampDescriptor* const InitialDescriptor = Descriptor_;
        FlipBasicAppearanceFrame(InitialDescriptor->Flags, FrameBlock);
        const float Scale = static_cast<float>(ForwardAppearanceSingleEval(
            InitialDescriptor->EffectScaleRamp, AppearanceOuterTime_, LodInput_));
        std::int32_t LastFrame = -1;
        for (; Particle != nullptr; Particle = Particle->Next)
        {
            const float InnerTime = static_cast<float>(static_cast<double>(Particle->InvDuration) * Particle->Age);
            float Alpha = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->SizeRamp, AppearanceOuterTime_, InnerTime));
            const float SizeScale = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, InnerTime) * Scale);
            const float Height = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->TexFrameRamp, AppearanceOuterTime_, InnerTime) * SizeScale);
            const float Width = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, InnerTime) * SizeScale);
            if (!IsNonzeroParticleField(Alpha))
                continue;
            if (SizeScale < TinyScale)
                continue;
            float Position[3];
            std::memcpy(Position, &Particle->PosX, sizeof(Position));
            float M[12];
            if (static_cast<unsigned char>(Descriptor_->LocalTransform->IsIdentity()) == 0)
            {
                unsigned long OuterBits;
                unsigned long InnerBits;
                std::memcpy(&OuterBits, &AppearanceOuterTime_, sizeof(OuterBits));
                std::memcpy(&InnerBits, &InnerTime, sizeof(InnerBits));
                Descriptor_->LocalTransform->Evaluate(OuterBits, InnerBits, M);
                Position[0] = static_cast<float>(static_cast<double>(M[9]) + Position[0]);
                Position[1] = static_cast<float>(static_cast<double>(Position[1]) + M[10]);
                Position[2] = static_cast<float>(static_cast<double>(Position[2]) + M[11]);
            }
            else
            {
                FillIdentity3x3(M);
                M[11] = 0.0f;
                M[10] = 0.0f;
                M[9] = 0.0f;
            }
            if (bCameraFade)
            {
                const double B1 = LoadAppearanceScalar(M[1]);
                const double B4 = LoadAppearanceScalar(M[4]);
                const double B7 = LoadAppearanceScalar(M[7]);
                const float A0 = static_cast<float>(LoadAppearanceScalar(M[0]));
                const float A3 = static_cast<float>(LoadAppearanceScalar(M[3]));
                const double A6 = LoadAppearanceScalar(M[6]);
                const float NormalX = static_cast<float>(static_cast<double>(A3) * B7 - A6 * B4);
                const float NormalY = static_cast<float>(A6 * B1 - B7 * A0);
                const float NormalZ = static_cast<float>(B4 * A0 - static_cast<double>(A3) * B1);
                FVector LocalCamera;
                TransformOrientedBoundsCenter(InverseNode, LocalCamera, CameraPosition);
                float Direction[3];
                Direction[2] = static_cast<float>(static_cast<double>(LocalCamera.Z) - Position[2]);
                Direction[1] = static_cast<float>(static_cast<double>(LocalCamera.Y) - Position[1]);
                Direction[0] = static_cast<float>(static_cast<double>(LocalCamera.X) - Position[0]);
                const double SquareX = static_cast<double>(Direction[0]) * Direction[0];
                const double SquareZ = static_cast<double>(Direction[2]) * Direction[2];
                const double SumXz = SquareX + SquareZ;
                const double SquareY = static_cast<double>(Direction[1]) * Direction[1];
                const double LengthSquared = SumXz + SquareY;
                const float SeedInput = static_cast<float>(LengthSquared);
                std::int32_t LengthBits;
                std::memcpy(&LengthBits, &SeedInput, sizeof(LengthBits));
                const std::uint32_t GuessBits = 0x5f3759dfu - static_cast<std::uint32_t>(LengthBits >> 1);
                float Guess;
                std::memcpy(&Guess, &GuessBits, sizeof(Guess));
                const double InverseLength = (1.5 - (((static_cast<double>(Guess) * Guess) * LengthSquared) * 0.5)) * Guess;
                const double Length = InverseLength * LengthSquared;
                if (!(Length <= TinyLength))
                {
                    const double InverseDistance = 1.0 / Length;
                    Direction[0] = static_cast<float>(static_cast<double>(Direction[0]) * InverseDistance);
                    Direction[1] = static_cast<float>(static_cast<double>(Direction[1]) * InverseDistance);
                    Direction[2] = static_cast<float>(static_cast<double>(Direction[2]) * InverseDistance);
                }
                else
                {
                    Direction[0] = 0.0f;
                    Direction[1] = 1.0f;
                    Direction[2] = 0.0f;
                }
                const double DotX = static_cast<double>(NormalX) * Direction[0];
                const double DotZ = static_cast<double>(Direction[2]) * NormalZ;
                const double SumXzDot = DotX + DotZ;
                const double DotY = static_cast<double>(Direction[1]) * NormalY;
                const double FacingValue = 1.0 - std::fabs(SumXzDot + DotY);
                const float Facing = static_cast<float>(FacingValue);
                if (FacingValue > FacingCull)
                    continue;
                if (Facing > FacingFade)
                {
                    const double Fade = (static_cast<double>(FacingCull) - Facing) * FacingFadeScale;
                    Alpha = static_cast<float>((static_cast<double>(Alpha) * Fade) * Fade);
                }
                if (!IsNonzeroParticleField(Alpha))
                    continue;
            }
            const unsigned long Color = ForwardAppearanceColorEval(
                Descriptor_->ColorRamp, AppearanceOuterTime_, InnerTime);
            const std::int64_t AlphaInteger = static_cast<std::int64_t>(static_cast<double>(Alpha) * 255.0);
            const unsigned long Diffuse = Color | (static_cast<std::uint32_t>(AlphaInteger) << 24);
            if (bFrameLookup)
            {
                const double FramePosition = ForwardAppearanceSingleEval(
                    Descriptor_->FrameRamp, AppearanceOuterTime_, InnerTime);
                const std::uint32_t CountBits = Texture_.FrameCount - 1u;
                std::int32_t SignedCount;
                std::memcpy(&SignedCount, &CountBits, sizeof(SignedCount));
                const std::int64_t FrameInteger = static_cast<std::int64_t>(FramePosition * SignedCount);
                const std::int32_t Frame = static_cast<std::int32_t>(FrameInteger);
                if (Frame != LastFrame)
                {
                    LastFrame = Frame;
                    Texture_.LookupFrame(Frame, FrameBlock);
                    const FAppearanceRampDescriptor* const FrameDescriptor = Descriptor_;
                    if ((FrameDescriptor->Flags & 0x1000u) != 0)
                    {
                        std::uint32_t Previous;
                        std::memcpy(&Previous, &FrameBlock[1], sizeof(Previous));
                        FrameBlock[1] = static_cast<float>(LoadAppearanceScalar(FrameBlock[3]));
                        std::memcpy(&FrameBlock[3], &Previous, sizeof(Previous));
                    }
                    if ((FrameDescriptor->Flags & 0x2000u) != 0)
                    {
                        std::uint32_t Previous;
                        std::memcpy(&Previous, &FrameBlock[2], sizeof(Previous));
                        FrameBlock[2] = static_cast<float>(LoadAppearanceScalar(FrameBlock[4]));
                        std::memcpy(&FrameBlock[4], &Previous, sizeof(Previous));
                    }
                }
            }
            float A[3];
            float B[3];
            A[2] = static_cast<float>(static_cast<double>(M[6]) * Width);
            A[1] = static_cast<float>(static_cast<double>(M[3]) * Width);
            A[0] = static_cast<float>(static_cast<double>(M[0]) * Width);
            B[2] = static_cast<float>(static_cast<double>(M[7]) * Height);
            B[1] = static_cast<float>(static_cast<double>(M[4]) * Height);
            B[0] = static_cast<float>(static_cast<double>(M[1]) * Height);
            float MinusA[3];
            float PlusA[3];
            float Corners[4][3];
            for (int Axis = 2; Axis >= 0; --Axis)
                MinusA[Axis] = static_cast<float>(static_cast<double>(Position[Axis]) - A[Axis]);
            for (int Axis = 2; Axis >= 0; --Axis)
                Corners[3][Axis] = static_cast<float>(static_cast<double>(MinusA[Axis]) - B[Axis]);
            for (int Axis = 2; Axis >= 0; --Axis)
                PlusA[Axis] = static_cast<float>(static_cast<double>(A[Axis]) + Position[Axis]);
            for (int Axis = 2; Axis >= 0; --Axis)
                Corners[2][Axis] = static_cast<float>(static_cast<double>(PlusA[Axis]) - B[Axis]);
            for (int Axis = 2; Axis >= 0; --Axis)
                MinusA[Axis] = static_cast<float>(static_cast<double>(Position[Axis]) - A[Axis]);
            for (int Axis = 2; Axis >= 0; --Axis)
                Corners[1][Axis] = static_cast<float>(static_cast<double>(MinusA[Axis]) + B[Axis]);
            for (int Axis = 2; Axis >= 0; --Axis)
                PlusA[Axis] = static_cast<float>(static_cast<double>(A[Axis]) + Position[Axis]);
            for (int Axis = 2; Axis >= 0; --Axis)
                Corners[0][Axis] = static_cast<float>(static_cast<double>(PlusA[Axis]) + B[Axis]);
            unsigned short Indices[4];
            for (unsigned long Corner = 0; Corner < 4; ++Corner)
            {
                double Y = 0.0;
                double Z = 0.0;
                double U = 0.0;
                double V = 0.0;
                if (Corner == 0)
                {
                    V = LoadAppearanceScalar(FrameBlock[2]);
                    U = LoadAppearanceScalar(FrameBlock[3]);
                    Z = LoadAppearanceScalar(Corners[0][2]);
                    Y = LoadAppearanceScalar(Corners[0][1]);
                }
                else if (Corner != 3)
                    Y = LoadAppearanceScalar(Corners[Corner][1]);
                const long Count = G_OrientedDrawContext.VertCount;
                Indices[Corner] = static_cast<unsigned short>(G_OrientedDrawContext.RequestedVerts);
                if (Count > static_cast<long>(G_OrientedDrawContext.RequestedVerts))
                    continue;
                FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(G_OrientedDrawContext.LockedVerts);
                if (Corner == 0)
                {
                    Vertex->X = static_cast<float>(LoadAppearanceScalar(Corners[0][0]));
                    Vertex->Diffuse = Diffuse;
                    Vertex->Y = static_cast<float>(Y);
                    Vertex->Z = static_cast<float>(Z);
                    Vertex->U = static_cast<float>(U);
                    Vertex->V = static_cast<float>(V);
                }
                else if (Corner != 3)
                {
                    std::memcpy(&Vertex->Z, &Corners[Corner][2], sizeof(float));
                    Vertex->X = static_cast<float>(LoadAppearanceScalar(Corners[Corner][0]));
                    std::memcpy(&Vertex->U, &FrameBlock[Corner == 1 ? 1 : 3], sizeof(float));
                    Vertex->Y = static_cast<float>(Y);
                    Vertex->Diffuse = Diffuse;
                    std::memcpy(&Vertex->V, &FrameBlock[Corner == 1 ? 2 : 4], sizeof(float));
                }
                else
                {
                    float Values[5];
                    Values[4] = static_cast<float>(LoadAppearanceScalar(FrameBlock[4]));
                    Values[3] = static_cast<float>(LoadAppearanceScalar(FrameBlock[1]));
                    Values[2] = static_cast<float>(LoadAppearanceScalar(Corners[3][2]));
                    Values[1] = static_cast<float>(LoadAppearanceScalar(Corners[3][1]));
                    Values[0] = static_cast<float>(LoadAppearanceScalar(Corners[3][0]));
                    std::memcpy(&Vertex->X, &Values[0], sizeof(float));
                    std::memcpy(&Vertex->Y, &Values[1], sizeof(float));
                    std::memcpy(&Vertex->Z, &Values[2], sizeof(float));
                    Vertex->Diffuse = Diffuse;
                    std::memcpy(&Vertex->U, &Values[3], sizeof(float));
                    std::memcpy(&Vertex->V, &Values[4], sizeof(float));
                }
                Indices[Corner] = static_cast<unsigned short>(Count);
                G_OrientedDrawContext.LockedVerts = Vertex + 1;
                ++G_OrientedDrawContext.VertCount;
            }
            StoreQuadIndices(G_OrientedDrawContext, Indices[0], Indices[1], Indices[2], Indices[3]);
        }
        if (AlDrawContextEnd(G_OrientedDrawContext) == 0)
            return GR_OK;
        ++G_DrawCounters[1];
        G_RenderPipeline->SetRenderState(D3DRS_SRCBLEND, static_cast<int>(Descriptor_->BlendInfo[0]));
        G_RenderPipeline->SetRenderState(D3DRS_DESTBLEND, static_cast<int>(Descriptor_->BlendInfo[1]));
        void* FrameTexture;
        std::memcpy(&FrameTexture, Texture_.Frame, sizeof(FrameTexture));
        G_RenderPipeline->SetTextureStageTexture(0, FrameTexture);
        ICamera* const Camera = reinterpret_cast<ICamera*>(static_cast<std::uintptr_t>(G_DrawCookie));
        float ViewWorld[12];
        ComposeTransform(ViewWorld, Camera->GetViewTransform(), NodeWorldXform_);
        G_RenderPipeline->SetModelview(ViewWorld);
        AlDrawContextDraw(G_OrientedDrawContext);
        return GR_OK;
    }
    #pragma float_control(pop)
    // Node ctor FUN_0620f4e0: +0xec identity (1.0 at +0xec / +0xfc / +0x10c, the rest 0),
    // +0x11c = 0.05 (0x3d4ccccd), +0x120..+0x12c = 0.
    FLDustAppearance::FLDustAppearance(const char* PrimaryIid, const char* FactoryName)
        : FFxBillboardAppearance(PrimaryIid, FactoryName, &kDustClass),
          LastDt_(FloatFromBits(0x3d4ccccdu)), LastCameraStep_(0.0f)
    {
        FillIdentity3x3(ViewXform_);
        ViewXform_[9] = 0.0f;
        ViewXform_[10] = 0.0f;
        ViewXform_[11] = 0.0f;
        LastCameraPos_[0] = 0.0f;
        LastCameraPos_[1] = 0.0f;
        LastCameraPos_[2] = 0.0f;
    }

    // FUN_0620f6b0: the set FUN_0620ef80 (perp's builder, cached at node+0xe8).
    void FLDustAppearance::CreatePropertySet()
    {
        AdoptBillboardSet(&BuildPerpDescriptorRamps);
    }

    // FUN_062125e0: outer +0xf0 is the node's own live-instance count.
    GENRESULT DACOM_API FLDustAppearance::GetQueuedVertCount(unsigned long* Out)
    {
        *Out = LiveInstanceCount_ << 2;
        return GR_OK;
    }

    // FUN_06212600: the same count contributes six indices per dust particle.
    GENRESULT DACOM_API FLDustAppearance::GetQueuedIndexCount(unsigned long* Out)
    {
        *Out = LiveInstanceCount_ * 6U;
        return GR_OK;
    }

    // FUN_0620f600 (RET 8): the dust constructor inline (as its factory 0x06210e26..),
    // FUN_06244120 (0x0620f67d), the +0xe8 set cache (0x0620f682..0x0620f68c); *Out, 0.
    GENRESULT DACOM_API FLDustAppearance::CloneNode(IDAComponent** Out)
    {
        FLDustAppearance* const Node = new (std::nothrow) FLDustAppearance(PrimaryIid_, FactoryName_);
        Node->InitAppearanceFromArchetype(this);
        Node->Descriptor_ = Descriptor_;
        *Out = Node;
        return GR_OK;
    }

    // FUN_0620fb70 (node, RET 8), reached through outer +0x14 FUN_06212220 (`ADD this,0xc`):
    //   zero the dust context's two u16 counters DAT_06257986 / DAT_06257988;
    //   [node+0x11c] = Dt; Age += Dt;
    //   node+0xb0 == 0 && node+0xd8 == 0 -> flags |= 1; texture frame via the wrapper at
    //   node+0xb4 with the descriptor at node+0xe8 (frame ramp +0x80); FUN_062114c0([node+0x18],
    //   &node+0xd8, &node+0x98) -- the field walk; FUN_06211600(&node+0xd8, Dt) -- Age += Dt,
    //   erase past Duration (AND 0x4100 / JNZ keeps Age <= Duration), else pos += vel * Dt.
#pragma float_control(except, on, push)
    GENRESULT DACOM_API FLDustAppearance::Pad5(float Dt)
    {
        // The initial retail FLD precedes both counter stores. A float is exact
        // in double; this native spill keeps the load and later float store apart.
        volatile double PendingDt = static_cast<double>(Dt);
        volatile unsigned short& VertCounter = G_DustDrawContext.Field_0xA;
        volatile unsigned short& IndexCounter = G_DustDrawContext.PrimCount;
        VertCounter = 0;
        IndexCounter = 0;
        const double StoredDt = PendingDt;
        StoreAlchemyFpuFloat(LastDt_, StoredDt);
        const double NextAge = static_cast<double>(Dt) + Age_;
        StoreAlchemyFpuFloat(Age_, NextAge);
        if (EmitterRefCount_ == 0 && InstanceListHead_ == nullptr)
            NodeFlags_ |= 1;

        FFxTextureKey Key;
        Key.Name = kTexNameUnknown;
        Key.Crc = Descriptor_->TextureCrc;
        Texture_.SetTexture(&Key);
        if (Texture_.Handle == -1)
            Texture_.Resolve();
        if ((Descriptor_->Flags & 8u) == 8u)
        {
            // Texture callbacks can publish a different live descriptor.
            const double PositionValue = ForwardAppearanceSingleEval(
                Descriptor_->AxisOverrideRamp, AppearanceOuterTime_, Age_);
            float Position;
            StoreAlchemyFpuFloat(Position, PositionValue);
            const int FrameCount = Texture_.FrameCount;
            if (FrameCount != 1)
            {
                const int Handle = Texture_.Handle;
                if (Handle != -1)
                {
                    const unsigned long Span = static_cast<unsigned long>(FrameCount) - 1U;
                    const std::int64_t SpanWide = static_cast<std::int64_t>(Span);
                    const double WideSpan = ComputeAlchemyFpuWideInteger(SpanWide);
                    IDAComponent* const volatile& LibrarySlot = G_TextureLibrary;
                    ITextureLibrary2* const Library = static_cast<ITextureLibrary2*>(LibrarySlot);
                    const std::int64_t Frame = ConvertAlchemyFpuInteger(WideSpan * Position);
                    Library->GetFrame(Handle, static_cast<int>(Frame),
                                      reinterpret_cast<unsigned long*>(Texture_.Frame));
                }
            }
        }
        else if (Texture_.FrameCount != 1)
        {
            Texture_.Advance(Dt);
        }
        ApplyFieldsToPool(this, false);
        IntegratePool(this, &G_PoolDust, Dt);
        return GR_OK;
    }
#pragma float_control(pop)

    // FUN_06212700 (outer, RET 0x10):
    //   DAT_062579b4 == 0 -> FUN_062121d0 evicts the oldest used particle through its owner;
    //   FUN_06211440(&node+0xd8, node, Six[6], Age, Duration): item from the pool [list+8]
    //     (FUN_0623ec90, no eviction), +0xc Six, +8 node, +0x24 Age, +0x28 Duration,
    //     +0x2c 1.0 / Duration, ++[node+0xe4], appended at the tail;
    //   math engine vt+0x28 TransformXform(&P, node+0xec, Six) and the tail's +0x30 = P.
    // A failed allocation stores through null in retail (EAX = 0 at 0x0621148f) and does
    // here too.
    GENRESULT DACOM_API FLDustAppearance::Pad47(const float* Six, float Age, float Duration)
    {
        if (G_PoolDust.FreeCount == 0)
            EvictOldestListParticle(&G_PoolDust);

        FPooledParticle* Particle = static_cast<FPooledParticle*>(AllocPoolItem(&G_PoolDust));
        if (Particle != nullptr)
        {
            Particle->PosX = Six[0];
            Particle->PosY = Six[1];
            Particle->PosZ = Six[2];
            Particle->VelX = Six[3];
            Particle->VelY = Six[4];
            Particle->VelZ = Six[5];
            Particle->Appearance = this;
            Particle->Age = Age;
            Particle->Duration = Duration;
            Particle->InvDuration = 1.0f / Duration;
            ++LiveInstanceCount_;
        }
        AppendParticleNode(&InstanceListHead_, &InstanceListTail_, Particle);

        float* LastViewPos =
            reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(InstanceListTail_) + 0x30);
        CameraSpacePoint(ViewXform_, Six, LastViewPos);
        return GR_OK;
    }

    // FUN_0620fcb0 (node, RET 4), reached through outer +0x18 FUN_06212240. Offsets are
    // ESP-relative after the four register pushes.
    //   ++DAT_06257904 (G_DrawCounters[8]) first; empty node+0xd8 -> return 0.
    //   ViewWorld = FUN_06208640(camera vt+0x40, node+0x4c), copied to node+0xec.
    //   [node+0xac] vt+0xd0 / +0xd4 counts (count * 4 / count * 6).
    //   CamPos [0x70] = camera vt+0xc; Step [0x10] = TransformMatrix(camera vt+0x40,
    //     CamPos - node+0x120) (vt+0x2c); Len [0x44] = sqrt((x*x + y*y) + z*z);
    //     v = Len / [node+0x11c]:
    //     v < 100 (TEST AH,5 / JP not taken)        Fade = 1 - (1 - v / 100)^2   (double 1.0)
    //     100 <= v < 1200                           Fade = 1 - ((v - 100) / 1100)^2
    //     v >= 1200 or unordered                    Fade = 0.
    //     D = |[node+0x12c] - Len|: D > 20 (AND 0x4100 / JNZ skips otherwise): D >= 35 ->
    //     Fade = 0, else Fade *= 1 - (D - 20) / 15 (double 1.0). The constants are the
    //     .data floats DAT_062559fc / 06255a00 / 06255a04 / 06255a08 (100, 1200, 20, 35),
    //     read-only.
    //   FUN_06240f50(&DAT_0625797c, 4, V, I) false -> skip to the tail stores.
    //   Frame block [0x50] = node+0xbc with the 0x1000 / 0x2000 flips.
    //   Per particle P (node+0xd8 order):
    //     C [0x1c] = TransformXform(node+0xec, P.pos) (vt+0x28).
    //     C.z > -1.0 or P+0x38 > -1.0 (AND 0x4100 / JZ) -> no quad.
    //     t = [P+0x2c] * [P+0x24]; Alpha [0x30] = desc+0x70 (node+0x8c, t) * Fade;
    //     H [0x48] = desc+0x78 (node+0x8c, t) * 0.5 (double).
    //     M [0x38] = C - P+0x30 (DAT_062579cc, the switch to the camera step instead, is a
    //       .data byte 0 with one reader).
    //     N = C x M; U = N x C (the 0x062100b7..0x062100eb stack order: Ux = Cz*Ny - Cy*Nz,
    //       Uy = Cx*Nz - Cz*Nx, Uz = Nx*Cy - Cx*Ny); L2 = (Uy*Uy + Ux*Ux) + Uz*Uz; U *= the
    //       0x5f3759df step on L2 (one Newton pass).
    //     W = (M * 0.035) * (1 / [node+0x11c]) (DAT_062559f4); Proj = ((Wy*Uy + Wx*Ux) +
    //       Wz*Uz) * 0.5 (double); Proj < 0.025 (double) -> U = (0, 1, 0); Proj > 100
    //       (DAT_062559f8) -> Proj = 100; A = U * (Proj + H); B = A * 0.65; D = A * 1.53846;
    //       Side = (-Uy * H, Ux * H).
    //     Alpha <= 0.01 -> no quad. Colour = desc+0x74 vt+0x20 (node+0x8c, t) |
    //       ftol(Alpha * 255) << 24.
    //     Vertices C - D (U1, V0); (C.x + Side.x, C.y + Side.y, C.z) (U1, V1);
    //       (C.x - Side.x, C.y - Side.y, C.z) (U0, V0); C + B (U0, V1); each skipped past
    //       the requested count; indices (I3, I2, I1, I1, I2, I0) unless IndexCount + 5 >
    //       requested.
    //     P+0x30 = C for every particle, drawn or not.
    //   End == 0 -> tail stores; else ++DAT_062578e8, blend 0x13 / 0x14 from desc+0x68 /
    //   +0x6c, SetTexture(0, [node+0xbc]), identity (FUN_06206c70, zero translation) as the
    //   transform, Draw.
    //   Tail: node+0x120 = CamPos, node+0x12c = Len.
    // The ramps go through Eval / EvalPacked like the other fills.
    namespace
    {
        I3DMathEngine* G_Dust3DMath = nullptr; // DAT_062579c8
    }

#pragma optimize("", off)
#pragma float_control(except, on, push)
#pragma intrinsic(sqrt)
    GENRESULT DACOM_API FLDustAppearance::DrawOrEnqueue()
    {
        const float kFadeNear = FloatFromBits(0x42c80000u);    // 100
        const float kFadeFar = FloatFromBits(0x44960000u);     // 1200
        const float kJerkLow = FloatFromBits(0x41a00000u);     // 20
        const float kJerkHigh = FloatFromBits(0x420c0000u);    // 35
        const float kStreakScale = FloatFromBits(0x3d0f5c29u); // 0.035
        const float kStreakMax = FloatFromBits(0x42c80000u);   // 100
        const float kTailScale = FloatFromBits(0x3f266666u);   // 0.65
        const float kHeadScale = FloatFromBits(0x3fc4ec4fu);   // 1.53846
        const float kMinAlpha = FloatFromBits(0x3c23d70au);    // 0.01

        ++G_DrawCounters[8];
        FPooledParticle* Particle = InstanceListHead_;
        if (Particle == nullptr)
            return GR_OK;

        ICamera* const Camera = reinterpret_cast<ICamera*>(static_cast<std::uintptr_t>(G_DrawCookie));
        const float* CameraView = Camera->GetViewTransform();
        ComposeTransform(ViewXform_, CameraView, NodeWorldXform_);

        unsigned long VertCount = 0;
        unsigned long IndexCount = 0;
        GetQueuedVertCount(&VertCount);
        GetQueuedIndexCount(&IndexCount);

        float CameraPos[3];
        ICamera* const PositionCamera = reinterpret_cast<ICamera*>(static_cast<std::uintptr_t>(G_DrawCookie));
        std::memcpy(CameraPos, PositionCamera->GetPosition(), sizeof(CameraPos));
        float CameraDelta[3];
        StoreAlchemyFpuFloat(CameraDelta[0], static_cast<double>(CameraPos[0]) - LastCameraPos_[0]);
        StoreAlchemyFpuFloat(CameraDelta[1], static_cast<double>(CameraPos[1]) - LastCameraPos_[1]);
        StoreAlchemyFpuFloat(CameraDelta[2], static_cast<double>(CameraPos[2]) - LastCameraPos_[2]);
        float Step[3];
        if (G_Dust3DMath == nullptr)
            G_Dust3DMath = Create3DMath();
        G_Dust3DMath->TransformMatrix(*reinterpret_cast<FVector*>(Step),
            *reinterpret_cast<const FMatrix*>(CameraView),
            *reinterpret_cast<const FVector*>(CameraDelta));
        const double SquareX = static_cast<double>(Step[0]) * Step[0];
        const double SquareY = static_cast<double>(Step[1]) * Step[1];
        const double SumXy = SquareX + SquareY;
        const double SquareZ = static_cast<double>(Step[2]) * Step[2];
        const double LengthSquared = SumXy + SquareZ;
        const double Length = ComputeAlchemyFpuSquareRoot(LengthSquared);
        float StepLen;
        StoreAlchemyFpuFloat(StepLen, (Length));
        // Kept on the FPU stack in retail (FST [0x44] stores the length, the quotient is
        // never spilled), hence double here.
        const double Speed = Length / LastDt_;
        float Fade = 0.0f;
        if (Speed < kFadeNear)
        {
            const double Rise = 1.0 - Speed / kFadeNear;
            StoreAlchemyFpuFloat(Fade, (1.0 - Rise * Rise));
        }
        else if (Speed < kFadeFar)
        {
            const double Fall = (Speed - kFadeNear) / (kFadeFar - kFadeNear);
            StoreAlchemyFpuFloat(Fade, (1.0f - Fall * Fall));
        }
        else
        {
            Fade = 0.0f;
        }
        const double JerkValue = std::fabs(static_cast<double>(LastCameraStep_) - StepLen);
        float Jerk;
        StoreAlchemyFpuFloat(Jerk, (JerkValue));
        if (JerkValue > kJerkLow)
        {
            if (!(Jerk < kJerkHigh))
                Fade = 0.0f;
            else
                StoreAlchemyFpuFloat(Fade, ((1.0 - (static_cast<double>(Jerk) - kJerkLow) /
                    (static_cast<double>(kJerkHigh) - kJerkLow)) * Fade));
        }

        if (AlDrawContextBegin(G_DustDrawContext, 4, VertCount, IndexCount))
        {
            float FrameBlock[5];
            std::memcpy(FrameBlock, Texture_.Frame, sizeof(FrameBlock));
            const unsigned long Flags = Descriptor_->Flags;
            if ((Flags & 0x1000u) != 0)
            {
                const double Swap = LoadAppearanceScalar(FrameBlock[1]);
                std::memcpy(&FrameBlock[1], &FrameBlock[3], sizeof(float));
                StoreAlchemyFpuFloat(FrameBlock[3], (Swap));
            }
            if ((Flags & 0x2000u) != 0)
            {
                const double Swap = LoadAppearanceScalar(FrameBlock[2]);
                std::memcpy(&FrameBlock[2], &FrameBlock[4], sizeof(float));
                StoreAlchemyFpuFloat(FrameBlock[4], (Swap));
            }
            const float& U0 = FrameBlock[1];
            const float& V0 = FrameBlock[2];
            const float& U1 = FrameBlock[3];
            const float& V1 = FrameBlock[4];
            for (; Particle != nullptr; Particle = Particle->Next)
            {
                float* LastView =
                    reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(Particle) + 0x30);
                float C[3];
                if (G_Dust3DMath == nullptr)
                    G_Dust3DMath = Create3DMath();
                G_Dust3DMath->TransformXform(*reinterpret_cast<FVector*>(C),
                    *reinterpret_cast<const FTransform*>(ViewXform_),
                    *reinterpret_cast<const FVector*>(&Particle->PosX));

                if (!(C[2] > -1.0f) && !(LastView[2] > -1.0f))
                {
                    float InnerTime;
                    StoreAlchemyFpuFloat(InnerTime, (
                        static_cast<double>(Particle->InvDuration) * Particle->Age));
                    float Alpha;
                    StoreAlchemyFpuFloat(Alpha, (ForwardAppearanceSingleEval(
                            Descriptor_->SizeRamp, AppearanceOuterTime_, InnerTime) * Fade));
                    float HalfSize;
                    StoreAlchemyFpuFloat(HalfSize, (
                        ForwardAppearanceSingleEval(
                            Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, InnerTime) * 0.5));

                    float Mz;
                    StoreAlchemyFpuFloat(Mz, (static_cast<double>(C[2]) - LastView[2]));
                    float My;
                    StoreAlchemyFpuFloat(My, (static_cast<double>(C[1]) - LastView[1]));
                    float Mx;
                    StoreAlchemyFpuFloat(Mx, (static_cast<double>(C[0]) - LastView[0]));
                    const double Nx = static_cast<double>(C[1]) * Mz - static_cast<double>(C[2]) * My;
                    const double Ny = static_cast<double>(C[2]) * Mx - static_cast<double>(C[0]) * Mz;
                    const double Nz = static_cast<double>(C[0]) * My - static_cast<double>(C[1]) * Mx;
                    float Ux;
                    StoreAlchemyFpuFloat(Ux, (static_cast<double>(C[2]) * Ny - static_cast<double>(C[1]) * Nz));
                    float Uy;
                    StoreAlchemyFpuFloat(Uy, (static_cast<double>(C[0]) * Nz - static_cast<double>(C[2]) * Nx));
                    const double UnroundedUz = Nx * C[1] - static_cast<double>(C[0]) * Ny;
                    float LenSq;
                    StoreAlchemyFpuFloat(LenSq, ((static_cast<double>(Uy) * Uy +
                        static_cast<double>(Ux) * Ux) + UnroundedUz * UnroundedUz));
                    std::int32_t LengthBits;
                    std::memcpy(&LengthBits, &LenSq, sizeof(LengthBits));
                    const std::uint32_t GuessBits = 0x5f3759dfu - static_cast<std::uint32_t>(LengthBits >> 1);
                    float Guess;
                    std::memcpy(&Guess, &GuessBits, sizeof(Guess));
                    const double GuessSquared = static_cast<double>(Guess) * Guess;
                    const double InvLen = (1.5 - ((GuessSquared * LenSq) * 0.5)) * Guess;
                    StoreAlchemyFpuFloat(Ux, (static_cast<double>(Ux) * InvLen));
                    StoreAlchemyFpuFloat(Uy, (static_cast<double>(Uy) * InvLen));
                    float Uz;
                    StoreAlchemyFpuFloat(Uz, (UnroundedUz * InvLen));

                    float ScaledMz;
                    StoreAlchemyFpuFloat(ScaledMz, (static_cast<double>(Mz) * kStreakScale));
                    float ScaledMy;
                    StoreAlchemyFpuFloat(ScaledMy, (static_cast<double>(My) * kStreakScale));
                    float ScaledMx;
                    StoreAlchemyFpuFloat(ScaledMx, (static_cast<double>(Mx) * kStreakScale));
                    const double InvDt = 1.0 / LastDt_;
                    float Wz;
                    StoreAlchemyFpuFloat(Wz, (ScaledMz * InvDt));
                    float Wy;
                    StoreAlchemyFpuFloat(Wy, (ScaledMy * InvDt));
                    float Wx;
                    StoreAlchemyFpuFloat(Wx, (ScaledMx * InvDt));
                    double Proj = ((static_cast<double>(Wy) * Uy + static_cast<double>(Wx) * Ux) +
                        static_cast<double>(Wz) * Uz) * 0.5;
                    if (Proj < 0.025)
                    {
                        Ux = 0.0f;
                        Uy = 1.0f;
                        Uz = 0.0f;
                    }
                    if (Proj > kStreakMax)
                        Proj = kStreakMax;
                    const double Reach = Proj + HalfSize;
                    float Az;
                    StoreAlchemyFpuFloat(Az, (Uz * Reach));
                    float Ay;
                    StoreAlchemyFpuFloat(Ay, (Uy * Reach));
                    float Ax;
                    StoreAlchemyFpuFloat(Ax, (Reach * Ux));
                    float Bz;
                    StoreAlchemyFpuFloat(Bz, (static_cast<double>(Az) * kTailScale));
                    float By;
                    StoreAlchemyFpuFloat(By, (static_cast<double>(Ay) * kTailScale));
                    float Bx;
                    StoreAlchemyFpuFloat(Bx, (static_cast<double>(Ax) * kTailScale));
                    float Dz;
                    StoreAlchemyFpuFloat(Dz, (static_cast<double>(Az) * kHeadScale));
                    float Dy;
                    StoreAlchemyFpuFloat(Dy, (static_cast<double>(Ay) * kHeadScale));
                    float Dx;
                    StoreAlchemyFpuFloat(Dx, (static_cast<double>(Ax) * kHeadScale));
                    float SideX;
                    StoreAlchemyFpuFloat(SideX, (-static_cast<double>(Uy) * HalfSize));
                    float SideY;
                    StoreAlchemyFpuFloat(SideY, (static_cast<double>(Ux) * HalfSize));

                    if (Alpha > kMinAlpha)
                    {
                        const unsigned long Color = ForwardAppearanceColorEval(
                            Descriptor_->ColorRamp, AppearanceOuterTime_, InnerTime);
                        const std::int64_t AlphaInteger = ConvertAlchemyFpuInteger(static_cast<double>(Alpha) * 255.0f);
                        const unsigned long Diffuse = Color | (static_cast<unsigned long>(AlphaInteger) << 24);
                        const double FirstZ = static_cast<double>(C[2]) - Dz;
                        const double FirstY = static_cast<double>(C[1]) - Dy;
                        unsigned short I0 = static_cast<unsigned short>(G_DustDrawContext.RequestedVerts);
                        if (static_cast<long>(G_DustDrawContext.VertCount) <= static_cast<long>(G_DustDrawContext.RequestedVerts))
                        {
                            FAlchemyVertex* const Vert = static_cast<FAlchemyVertex*>(G_DustDrawContext.LockedVerts);
                            StoreAlchemyFpuFloat(Vert->X, static_cast<double>(C[0]) - Dx);
                            Vert->Diffuse = Diffuse;
                            StoreAlchemyFpuFloat(Vert->Y, FirstY);
                            StoreAlchemyFpuFloat(Vert->Z, FirstZ);
                            StoreAlchemyFpuFloat(Vert->U, LoadAppearanceScalar(U1));
                            StoreAlchemyFpuFloat(Vert->V, LoadAppearanceScalar(V0));
                            I0 = G_DustDrawContext.VertCount;
                            ++G_DustDrawContext.VertCount;
                            G_DustDrawContext.LockedVerts = Vert + 1;
                        }
                        unsigned short I1 = static_cast<unsigned short>(G_DustDrawContext.RequestedVerts);
                        if (static_cast<long>(G_DustDrawContext.VertCount) <= static_cast<long>(G_DustDrawContext.RequestedVerts))
                        {
                            FAlchemyVertex* const Vert = static_cast<FAlchemyVertex*>(G_DustDrawContext.LockedVerts);
                            StoreAlchemyFpuFloat(Vert->X, static_cast<double>(C[0]) + SideX);
                            const double SecondY = static_cast<double>(C[1]) + SideY;
                            Vert->Diffuse = Diffuse;
                            StoreAlchemyFpuFloat(Vert->Y, SecondY);
                            StoreAlchemyFpuFloat(Vert->Z, LoadAppearanceScalar(C[2]));
                            StoreAlchemyFpuFloat(Vert->U, LoadAppearanceScalar(U1));
                            StoreAlchemyFpuFloat(Vert->V, LoadAppearanceScalar(V1));
                            I1 = G_DustDrawContext.VertCount;
                            ++G_DustDrawContext.VertCount;
                            G_DustDrawContext.LockedVerts = Vert + 1;
                        }
                        unsigned short I2 = static_cast<unsigned short>(G_DustDrawContext.RequestedVerts);
                        if (static_cast<long>(G_DustDrawContext.VertCount) <= static_cast<long>(G_DustDrawContext.RequestedVerts))
                        {
                            FAlchemyVertex* const Vert = static_cast<FAlchemyVertex*>(G_DustDrawContext.LockedVerts);
                            StoreAlchemyFpuFloat(Vert->X, static_cast<double>(C[0]) - SideX);
                            const double ThirdY = static_cast<double>(C[1]) - SideY;
                            Vert->Diffuse = Diffuse;
                            StoreAlchemyFpuFloat(Vert->Y, ThirdY);
                            StoreAlchemyFpuFloat(Vert->Z, LoadAppearanceScalar(C[2]));
                            StoreAlchemyFpuFloat(Vert->U, LoadAppearanceScalar(U0));
                            StoreAlchemyFpuFloat(Vert->V, LoadAppearanceScalar(V0));
                            I2 = G_DustDrawContext.VertCount;
                            G_DustDrawContext.LockedVerts = Vert + 1;
                            ++G_DustDrawContext.VertCount;
                        }
                        const double LastY = static_cast<double>(C[1]) + By;
                        float LastZ;
                        StoreAlchemyFpuFloat(LastZ, static_cast<double>(C[2]) + Bz);
                        unsigned short I3 = static_cast<unsigned short>(G_DustDrawContext.RequestedVerts);
                        if (static_cast<long>(G_DustDrawContext.VertCount) <= static_cast<long>(G_DustDrawContext.RequestedVerts))
                        {
                            FAlchemyVertex* const Vert = static_cast<FAlchemyVertex*>(G_DustDrawContext.LockedVerts);
                            StoreAlchemyFpuFloat(Vert->X, static_cast<double>(C[0]) + Bx);
                            std::memcpy(&Vert->Z, &LastZ, sizeof(float));
                            Vert->Diffuse = Diffuse;
                            StoreAlchemyFpuFloat(Vert->Y, LastY);
                            StoreAlchemyFpuFloat(Vert->U, LoadAppearanceScalar(U0));
                            StoreAlchemyFpuFloat(Vert->V, LoadAppearanceScalar(V1));
                            I3 = G_DustDrawContext.VertCount;
                            ++G_DustDrawContext.VertCount;
                            G_DustDrawContext.LockedVerts = Vert + 1;
                        }
                        StoreQuadIndices(G_DustDrawContext, I0, I1, I2, I3);
                    }
                }

                std::memcpy(LastView, C, sizeof(C));
            }

            if (AlDrawContextEnd(G_DustDrawContext) != 0)
            {
                ++G_DrawCounters[1];
                CallSetRenderState(G_RenderPipeline, 0x13, static_cast<int>(Descriptor_->BlendInfo[0]));
                CallSetRenderState(G_RenderPipeline, 0x14, static_cast<int>(Descriptor_->BlendInfo[1]));
                unsigned long FrameTexture = 0;
                std::memcpy(&FrameTexture, &Texture_.Frame[0], sizeof(FrameTexture));
                CallRenderPipelineSetTexture(G_RenderPipeline, 0, static_cast<int>(FrameTexture));
                float Identity[12];
                FillIdentity3x3(Identity);
                Identity[9] = 0.0f;
                Identity[10] = 0.0f;
                Identity[11] = 0.0f;
                CallRenderPipelineSetTransform(G_RenderPipeline, Identity);
                AlDrawContextDraw(G_DustDrawContext);
            }
        }

        std::memcpy(LastCameraPos_, CameraPos, sizeof(CameraPos));
        std::memcpy(&LastCameraStep_, &StepLen, sizeof(StepLen));
        return GR_OK;
    }
#pragma function(sqrt)
#pragma float_control(pop)
#pragma optimize("", on)

    FLBeamAppearance::FLBeamAppearance(const char* PrimaryIid, const char* FactoryName)
        : FFxBillboardAppearance(PrimaryIid, FactoryName, &kBeamClass), bRibbonParity_(false)
    {
    }

    void FLBeamAppearance::CreatePropertySet()
    {
        AdoptBillboardSet(&BuildBeamDescriptorRamps);
    }

    // FUN_06204ef0 (RET 8): the beam constructor inline (as its factory 0x0620b486..),
    // FUN_06244120 (0x06204fc5), the +0xf0 set cache (0x06204fca..0x06204fd4); *Out, 0.
    GENRESULT DACOM_API FLBeamAppearance::CloneNode(IDAComponent** Out)
    {
        FLBeamAppearance* const Node = new (std::nothrow) FLBeamAppearance(PrimaryIid_, FactoryName_);
        Node->InitAppearanceFromArchetype(this);
        Node->Descriptor_ = Descriptor_;
        *Out = Node;
        return GR_OK;
    }

    // LAB_0620e080 (outer +0x64, FSTP [outer+0x90]) is the node Age setter:
    // the appearance node sits at outer+0xC, so outer+0x90 = node+0x84 = Age.
    // It is the shared inline FGenericFxObjectBase::SetNodeAge; the former
    // Beam-only Pad25 override (and its EffectOuterScale_ field) was that same
    // store under a guessed name, removed 2026-09-26.

    // FUN_0620e430 (outer +0xBC, RET 0x10; node-vtable twin LAB_062055a0 at 0x0624b718):
    //   DAT_06257964 == 0 -> FUN_0620dde0 erases the oldest used pool item from its owner;
    //   FUN_0620be90(&node+0xe4, node, Six[6], Age, Duration, [node+0xdc]):
    //     item from the pool [list+8] (FUN_0623ec90); item+0xc = Six, +8 = node,
    //     +0x24 = Age, +0x28 = Duration, +0x2c = 1.0 / Duration, +0x30 = [node+0xdc];
    //     ++[node+0xb4]; then PREPENDED (Prev = 0, Next = head, head = item, tail = item
    //     when empty);
    //   [node+0xe1] = ([node+0xe1] == 0); return 0.
    // The node ctors (0x06204e6e / 0x06204f56) seed node+0xdc with zero. Allocation
    // fills only a non-null item; the subsequent prepend is unconditional in retail.
    GENRESULT DACOM_API FLBeamAppearance::Pad47(const float* Six, float Age, float Duration)
    {
        if (G_PoolBeam.FreeCount == 0)
            EvictOldestListParticle(&G_PoolBeam);

        FPooledParticle* const Particle =
            static_cast<FPooledParticle*>(AllocPoolItem(&G_PoolBeam));
        if (Particle != nullptr)
        {
            Particle->PosX = Six[0];
            Particle->PosY = Six[1];
            Particle->PosZ = Six[2];
            Particle->VelX = Six[3];
            Particle->VelY = Six[4];
            Particle->VelZ = Six[5];
            Particle->Appearance = this;
            Particle->Age = Age;
            Particle->Duration = Duration;
            Particle->InvDuration = 1.0f / Duration;
            *reinterpret_cast<unsigned long*>(reinterpret_cast<unsigned char*>(Particle) + 0x30) = 0;
            ++LiveInstanceCount_;
        }
        PrependParticleNode(&InstanceListHead_, &InstanceListTail_, Particle);
        bRibbonParity_ = !bRibbonParity_;
        return GR_OK;
    }

    // wave124 (sonnet:wave124:beampool, 2026-09-15): real override of the
    // IGenericFx::Pad5 slot, reproducing retail FUN_0620de30 (decomp-tracker
    // item 132227, alchemy.dll.txt 29507-29563 / alchemy.dll.c 10418-10439).
    //
    // BACKGROUND -- why this is a real fix, not a cosmetic one: waves 86/99/
    // 108/110/111 (2026-09-15, all same day) traced this function extensively
    // and left it `simplified`, converging on the idea that it is Beam's own
    // "private pool" per-tick driver, filling the exact role the shared
    // Pad25-based ApplyFieldsToPool/IntegratePool/OnAfterIntegrate pipeline
    // (FGenericFxObjectBase::Pad25, alchemy_genericfx.cpp) fills for the
    // other 7 appearance leaves. This wave confirmed that role concretely:
    // FLBeamAppearance::Pad25 (above) is byte-confirmed (wave111) to be a
    // PLAIN FIELD SETTER, not the shared dispatcher -- so nothing anywhere in
    // this port currently ages, moves, expires, or applies fields to a Beam
    // appearance's own particles once EmitOne (Pad47) puts them into the
    // shared G_PoolBeam arena and this instance's own InstanceListHead_.
    // Without a Pad5 override, Beam particles would sit frozen forever. This
    // is exactly the gap FUN_0620de30 exists to close in retail.
    //
    // BYTE TRACE (alchemy.dll.txt 29515-29563), confirmed fresh from raw
    // disassembly this wave (not trusted from prior waves' paraphrase, per
    // this project's own "Ghidra .c decompile can garble/omit arguments"
    // rule):
    //   - `*(this+0x90) += Dt` -- accumulate onto EffectOuterScale_ (see this
    //     override's own header doc comment for the open naming/semantics
    //     doubt this raised, not resolved this wave).
    //   - `if (*(this+0xbc)==0 && *(this+0xf0)==0) *(this+0x10) |= 1` -- a
    //     completion/idle flag with NO confirmed port-side field or consumer
    //     (wave86/99's own conclusion, re-checked this wave, still holds: no
    //     FGenericFxObjectBase member corresponds to retail's this+0x10, and
    //     no reader of such a flag was found in alchemy_common.cpp/
    //     alchemy_effectobject.cpp). NOT reproduced -- fabricating a new,
    //     unconsumed member would add dead API surface, not close a gap.
    //   - `FUN_06244b80(this+0xc4, &stack)` / conditional `FUN_06244df0` --
    //     cached texture-frame-index refresh. Already classified `skipped`
    //     module-wide: this whole function family is superseded by the
    //     port's single G_TextureLibrary cache (original gap-fill wave's own
    //     finding, reconfirmed every wave since). NOT reproduced, per that
    //     established precedent.
    //   - `FUN_0620bc30(*(this+0x24), this+0xf0, this+0xa4)` then
    //     `FUN_0620bda0(this+0xf0, Dt)` -- confirmed (items 132109/132111,
    //     both `ready_unverified`) to be retail's own per-instance
    //     "apply associated fields, then integrate own particle list" pair --
    //     the EXACT retail operation the port's shared
    //     ApplyFieldsToPool(Pool, Owner, Dt) / IntegratePool(Owner, Pool, Dt)
    //     free functions already implement generically (see
    //     FGenericFxObjectBase's own Pad25 body, a few functions above in
    //     this same call shape, for the identical pairing). Reproduced here
    //     via those same two already-verified primitives rather than
    //     re-deriving FUN_0620bc30/FUN_0620bda0's raw pointer-walk a second
    //     time -- Owner=this and Pool=PoolForAppearanceFactory(FactoryName_)
    //     resolve to G_PoolBeam for a "FLBeamAppearance"-factory instance
    //     (alchemy_common.cpp), matching retail's own per-factory pool
    //     selection exactly.
    //
    // ALC-4 (2026-09-30): the "texture-frame refresh ... superseded" claim above is
    // wrong -- the descriptor's name property 0x1BA23359 writes the texture CRC at
    // descriptor +0x64 (LAB_06204da0 at 0x06204dbd on this class), so
    // 0x0620de6f..0x0620dea3 run for real: FUN_06244b80(outer+0xc4 = node+0xb8,
    // &{"???", [[outer+0xfc]+0x64]}), then FUN_06244df0 while the handle is -1. The beam
    // has no frame step. Ported below as Texture_.SetTexture / Resolve.
    // 0x0620de37..0x0620de3f (and the node twin LAB_062054d0 at 0x062054d9..0x062054df):
    // zero the beam draw context's +0xa / +0xc counters (DAT_06257936 / DAT_06257938).
    //
    // Dt is passed through UNCLAMPED, matching FUN_0620de30's own raw body
    // exactly -- the `Dt > 0.0f && Dt < 1.0f` clamp seen in
    // FGenericFxObjectBase::Pad25's shared body belongs to THAT function's
    // own caller (FFxEffectObject::WalkNodesCallOff), not to this slot; this
    // slot's own real external trigger is still unconfirmed (see this
    // override's own decomp-tracker comment on item 132227 for the full
    // "no XREF anywhere, direct or indirect, to this vtable slot" search
    // this wave re-ran and re-confirmed) -- same class of external-driver
    // gap already accepted elsewhere in this port (AppearanceOuterTime_,
    // the former ComposeAppearanceWorldTransform). Field walking ignores Dt;
    // integration receives the original Dt word, including zero and unordered values.
    //
    // CORRECTED 2026-09-26: FUN_0620de30 runs on the OUTER object (node at
    // outer+0xC), so `*(this+0x90) += Dt` is node+0x84 = Age_, and this+0x10 is
    // node+4 = NodeFlags_ (bit0 = done); this+0xbc = node+0xb0 = the emitter
    // reference count (EmitterRefCount_).
    // ALC-1 2026-09-26: 0x0620de51..0x0620de6c -- EmitterRefCount_ == 0 and the
    // particle list head [outer+0xf0] (node+0xe4, the list FUN_0620bc30/FUN_0620bda0
    // walk and the draw FUN_06205620 tests) null -> NodeFlags_ |= 1. That list is the
    // port's InstanceListHead_ (FLBeamAppearance::Pad47 links every beam particle into
    // it), so the store is ported below, after the Age_ update as in retail.
    #pragma float_control(except, on, push)
    GENRESULT DACOM_API FLBeamAppearance::Pad5(float Dt)
    {
        const double Delta = LoadAppearanceScalar(Dt);
        G_BeamDrawContext.Field_0xA = 0;
        G_BeamDrawContext.PrimCount = 0;
        Age_ = static_cast<float>(Delta + Age_);
        if (EmitterRefCount_ == 0 && InstanceListHead_ == nullptr)
            NodeFlags_ |= 1;
        FFxTextureKey Key;
        Key.Name = "???"; // DAT_06255804
        Key.Crc = Descriptor_->TextureCrc;
        Texture_.SetTexture(&Key);
        if (Texture_.Handle == -1)
            Texture_.Resolve();
        ApplyFieldsToPool(this, false);
        IntegratePool(this, &G_PoolBeam, Dt);
        return GR_OK;
    }
    #pragma float_control(pop)

    namespace
    {
        // The peeled Beam pass loads Z/Y/X before any 06206ad0 products. Its
        // loop instead rounds the inline YZ + X squared distance before 06206b30.
        #pragma optimize("", off)
        #pragma float_control(except, on, push)
        __declspec(noinline) void StoreBeamBoundsSphere(float* Sphere,
            const FPooledParticle* Particle, const double& Frame, const float& Primary)
        {
            const double Product = Frame * Primary;
            std::uint32_t Word;
            std::memcpy(&Word, &Particle->PosX, sizeof(Word));
            std::memcpy(Sphere, &Word, sizeof(Word));
            std::memcpy(&Word, &Particle->PosY, sizeof(Word));
            std::memcpy(Sphere + 1, &Word, sizeof(Word));
            const double Radius = Product * 0.5;
            std::memcpy(&Word, &Particle->PosZ, sizeof(Word));
            std::memcpy(Sphere + 2, &Word, sizeof(Word));
            StoreAlchemyFpuFloat(Sphere[3], Radius);
        }

        __declspec(noinline) void MergeBeamBoundsSphere(float* Sphere, const float* Center,
            const float& Radius, EPerpRectBoundsDistance DistanceKind)
        {
            const double DifferenceZ = static_cast<double>(Center[2]) - Sphere[2];
            const double DifferenceY = static_cast<double>(Center[1]) - Sphere[1];
            const double DifferenceX = static_cast<double>(Center[0]) - Sphere[0];
            float Difference[3];
            StoreAlchemyFpuFloat(Difference[0], DifferenceX);
            StoreAlchemyFpuFloat(Difference[1], DifferenceY);
            StoreAlchemyFpuFloat(Difference[2], DifferenceZ);
            double LengthSquared;
            float RoundedLengthSquared;
            if (DistanceKind == EPerpRectBoundsDistance::PerpUnroundedXyz)
            {
                const double Z = LoadAppearanceScalar(Difference[2]);
                const double Y = LoadAppearanceScalar(Difference[1]);
                const double X = LoadAppearanceScalar(Difference[0]);
                const double SquareX = X * X;
                const double SquareY = Y * Y;
                const double SumXy = SquareX + SquareY;
                const double SquareZ = Z * Z;
                LengthSquared = SumXy + SquareZ;
                StoreAlchemyFpuFloat(RoundedLengthSquared, LengthSquared);
            }
            else
            {
                const double SquareY = static_cast<double>(Difference[1]) * Difference[1];
                const double SquareZ = static_cast<double>(Difference[2]) * Difference[2];
                const double SumYz = SquareY + SquareZ;
                const double SquareX = static_cast<double>(Difference[0]) * Difference[0];
                StoreAlchemyFpuFloat(RoundedLengthSquared, SumYz + SquareX);
                LengthSquared = RoundedLengthSquared;
            }
            std::int32_t LengthBits;
            std::memcpy(&LengthBits, &RoundedLengthSquared, sizeof(LengthBits));
            const std::uint32_t GuessBits = 0x5f3759dfu -
                static_cast<std::uint32_t>(LengthBits >> 1);
            float Guess;
            std::memcpy(&Guess, &GuessBits, sizeof(Guess));
            const double GuessSquared = static_cast<double>(Guess) * Guess;
            const double InverseLength = (1.5 - ((GuessSquared * LengthSquared) * 0.5)) * Guess;
            const double Distance = InverseLength * LengthSquared;
            float RoundedDistance;
            StoreAlchemyFpuFloat(RoundedDistance, Distance);
            // TEST AH,5 + JP skips the copy unless the first sum is ordered less.
            if (Distance + Sphere[3] < Radius)
            {
                std::memcpy(Sphere, Center, sizeof(float) * 3);
                std::memcpy(Sphere + 3, &Radius, sizeof(Radius));
                return;
            }
            // TEST AH,5 + JNP skips the merge only for ordered less.
            if (static_cast<double>(Radius) + RoundedDistance < Sphere[3])
                return;
            const float NewRadius = static_cast<float>(
                ((static_cast<double>(Radius) + Sphere[3]) + RoundedDistance) * 0.5);
            // TEST AH,0x41 + JNP skips normalization for ordered less/equal.
            if (!(static_cast<double>(RoundedDistance) <= 1e-6))
            {
                const double InverseDistance = 1.0 / RoundedDistance;
                StoreAlchemyFpuFloat(Difference[0], InverseDistance * Difference[0]);
                StoreAlchemyFpuFloat(Difference[1], InverseDistance * Difference[1]);
                StoreAlchemyFpuFloat(Difference[2], InverseDistance * Difference[2]);
                const float Step = static_cast<float>(static_cast<double>(NewRadius) - Sphere[3]);
                const double StepZ = static_cast<double>(Step) * Difference[2];
                const double StepY = static_cast<double>(Step) * Difference[1];
                const double StepX = static_cast<double>(Step) * Difference[0];
                float Offset[3];
                StoreAlchemyFpuFloat(Offset[0], StepX);
                StoreAlchemyFpuFloat(Offset[1], StepY);
                StoreAlchemyFpuFloat(Offset[2], StepZ);
                StoreAlchemyFpuFloat(Sphere[0], static_cast<double>(Offset[0]) + Sphere[0]);
                StoreAlchemyFpuFloat(Sphere[1], static_cast<double>(Offset[1]) + Sphere[1]);
                StoreAlchemyFpuFloat(Sphere[2], static_cast<double>(Offset[2]) + Sphere[2]);
            }
            std::memcpy(Sphere + 3, &NewRadius, sizeof(NewRadius));
        }
        #pragma float_control(pop)
        #pragma optimize("", on)
    }

    // FUN_062050c0 (node vtable 0x0624b6e4; outer +0x8C through FUN_0620e1b0), RET 8,
    // listing 11951-12292: the beam's GetBoundingSphere over the node+0xe4 particle list
    // (FUN_06208570 head, FUN_06208590 null end, FUN_062085f0 = [list+4] tail). Same
    // walk as the billboards -- see ListBoundsSphere -- with the beam radius
    // (P+0x7c * P+0x78 * 0.5, no scale ramp), the peeled tail pass at the first
    // particle's t, FUN_06206ad0 in the peeled merge and the inline length in the loop;
    // the tail transform goes through the beam's cached math engine DAT_06257978.
    // Empty -> -1 (0x062054ae), else 0.
    // Replaces FGenericFxObjectBase::UpdateInstanceBoundsSphere (wave71), which had no
    // caller, discarded P+0x78 and multiplied by node+0x8c instead.
    #pragma float_control(except, on, push)
    GENRESULT DACOM_API FLBeamAppearance::GetBoundingSphere(float* OutSphere)
    {
        const FPooledParticle* const Head = InstanceListHead_;
        if (Head == nullptr)
            return GR_GENERIC;
        const double FirstAge = LoadAppearanceScalar(Head->Age);
        const float FirstTime = static_cast<float>(FirstAge * Head->InvDuration);
        const float Primary = static_cast<float>(ForwardAppearanceSingleEval(
            Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, FirstTime));
        const double Frame = ForwardAppearanceSingleEval(
            Descriptor_->TexFrameRamp, AppearanceOuterTime_, FirstTime);
        StoreBeamBoundsSphere(OutSphere, Head, Frame, Primary);
        const FPooledParticle* const Tail = InstanceListTail_;
        if (Head != Tail)
        {
            // The peeled tail deliberately uses the head's current time again.
            const double TailAge = LoadAppearanceScalar(Head->Age);
            const float TailTime = static_cast<float>(TailAge * Head->InvDuration);
            const float TailPrimary = static_cast<float>(ForwardAppearanceSingleEval(
                Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, TailTime));
            const double TailFrame = ForwardAppearanceSingleEval(
                Descriptor_->TexFrameRamp, AppearanceOuterTime_, TailTime);
            float TailSphere[4];
            StoreBeamBoundsSphere(TailSphere, Tail, TailFrame, TailPrimary);
            MergeBeamBoundsSphere(OutSphere, TailSphere, TailSphere[3],
                EPerpRectBoundsDistance::PerpUnroundedXyz);
            const FPooledParticle* Particle = Head->Next;
            for (; Particle != Tail; Particle = Particle->Next)
            {
                const float Time = static_cast<float>(static_cast<double>(Particle->InvDuration) * Particle->Age);
                const float ParticlePrimary = static_cast<float>(ForwardAppearanceSingleEval(
                    Descriptor_->PrimarySizeRamp, AppearanceOuterTime_, Time));
                const double ParticleFrame = ForwardAppearanceSingleEval(
                    Descriptor_->TexFrameRamp, AppearanceOuterTime_, Time);
                float Sphere[4];
                StoreBeamBoundsSphere(Sphere, Particle, ParticleFrame, ParticlePrimary);
                MergeBeamBoundsSphere(OutSphere, Sphere, Sphere[3],
                    EPerpRectBoundsDistance::RectRoundedYzx);
            }
        }
        FVector WorldCenter;
        TransformPerpBoundsCenter(*reinterpret_cast<const FTransform*>(NodeWorldXform_),
            WorldCenter, *reinterpret_cast<const FVector*>(OutSphere));
        float WorldSphere[4];
        std::memcpy(WorldSphere, &WorldCenter, sizeof(WorldCenter));
        std::memcpy(WorldSphere + 3, OutSphere + 3, sizeof(float));
        std::memcpy(OutSphere, WorldSphere, sizeof(WorldSphere));
        return GR_OK;
    }
    #pragma float_control(pop)

    // FUN_06205620, __stdcall on the node (listing 0x06205620-0x0620571b):
    //   node+0xe4 list empty (FUN_062085b0 head vs FUN_062085d0 null end) -> return 0;
    //   DAT_06257919 -> FUN_06201c70(3, [node+0xac]), return 0;
    //   [node+0xac] vt+0xd0/+0xd4 counts; FUN_06240f50(&DAT_0625792c, 4, V, I) false -> 0;
    //   FUN_06205720(node); FUN_06241070 == 0 -> 0; ++DAT_062578e8; FUN_06206850(node);
    //   FUN_06241180; return 0.
    // The tail from FUN_06241070 on is the same sequence as LAB_062069e0.
    GENRESULT DACOM_API FLBeamAppearance::DrawOrEnqueue()
    {
        if (GetFirstInstanceParticle() == nullptr)
            return GR_OK;

        if (G_Defaults.bUseMaterialBatcher != 0)
        {
            EnqueueAppearanceToList(3, this);
            return GR_OK;
        }

        unsigned long VertCount = 0;
        unsigned long IndexCount = 0;
        GetQueuedVertCount(&VertCount);
        GetQueuedIndexCount(&IndexCount);
        if (!AlDrawContextBegin(G_BeamDrawContext, 4, VertCount, IndexCount))
            return GR_OK;

        FillRibbon();

        if (AlDrawContextEnd(G_BeamDrawContext) == 0)
            return GR_OK;
        ++G_DrawCounters[1];
        ApplyDrawState();
        AlDrawContextDraw(G_BeamDrawContext);
        return GR_OK;
    }

    // LAB_062069c0 (node vtable +0x4c, outer +0x9C): FUN_06240f50(&DAT_0625792c, 4, V, I),
    // folded to 0 / -1 by NEG/SBB/NEG/DEC.
    GENRESULT DACOM_API FLBeamAppearance::BeginBatchedDraw(unsigned long Verts,
                                                           unsigned long Indices)
    {
        return AlDrawContextBegin(G_BeamDrawContext, 4, Verts, Indices) ? GR_OK : GR_GENERIC;
    }

    // LAB_062069e0 (node vtable +0x50, outer +0xA0): End == 0 -> return 0; ++DAT_062578e8;
    // FUN_06206850(node); FUN_06241180; return 0.
    GENRESULT DACOM_API FLBeamAppearance::EndBatchedDraw()
    {
        if (AlDrawContextEnd(G_BeamDrawContext) == 0)
            return GR_OK;
        ++G_DrawCounters[1];
        ApplyDrawState();
        AlDrawContextDraw(G_BeamDrawContext);
        return GR_OK;
    }

    // LAB_06206a20 (node vtable +0x54, outer +0xA4): `CALL FUN_06205720; XOR EAX,EAX`.
    GENRESULT DACOM_API FLBeamAppearance::FillBatchedGeometry()
    {
        FillRibbon();
        return GR_OK;
    }

    // LAB_06206a30 (node vtable +0x7c, outer +0xD0): *Out = [node+0xb4] * 5, then
    // *Out += 5 when (desc+0x60 & 0x100) == 0x100. No null check on Out.
    GENRESULT DACOM_API FLBeamAppearance::GetQueuedVertCount(unsigned long* Out)
    {
        unsigned long Count = ResolveLiveInstanceCount() * 5;
        *Out = Count;
        if ((GetAppearanceRampDescriptor()->Flags & 0x100u) == 0x100u)
            *Out = Count + 5;
        return GR_OK;
    }

    // LAB_06206a70 (node vtable +0x80, outer +0xD4): *Out = [node+0xb4] * 0x18, then
    // *Out += 0x18 when (desc+0x60 & 0x100) == 0x100.
    GENRESULT DACOM_API FLBeamAppearance::GetQueuedIndexCount(unsigned long* Out)
    {
        unsigned long Count = ResolveLiveInstanceCount() * 0x18;
        *Out = Count;
        if ((GetAppearanceRampDescriptor()->Flags & 0x100u) == 0x100u)
            *Out = Count + 0x18;
        return GR_OK;
    }

    // FUN_06206850, __thiscall on the node (listing 0x06206850-0x062069a0):
    //   SetRenderState(0x13, [desc+0x68]); SetRenderState(0x14, [desc+0x6c]);
    //   (desc+0x60 & 0x400) == 0x400 ? (Texture = 0, FillMode = 2) :
    //                                  (Texture = [node+0xc0], FillMode = 3);
    //   SetTexture(0, Texture)                                   (vt+0x108)
    //   saver {Pipeline, 8, GetRenderState(8)}                   (vt+0xf4)
    //   SetRenderState(8, FillMode)                              (vt+0xf0)
    //   SetModelview(MulTransform(camera vt+0x40, node+0x4c))    (vt+0x7c)
    //   ~saver: SetRenderState(8, saved)                         (FUN_06208a20, also the
    //                                                             EH unwind Unwind@06247460)
    // The fill mode is therefore restored before the caller's Draw, exactly as in retail.
    // [node+0xc0] is the beam's frame-block texture (the block starts 4 bytes later than
    // on the other kinds: node+0xb4 is the beam's segment count), Texture_.Frame[0] here as
    // on the siblings.
    void FLBeamAppearance::ApplyDrawState()
    {
        G_RenderPipeline->SetRenderState(D3DRS_SRCBLEND, static_cast<int>(Descriptor_->BlendInfo[0]));
        G_RenderPipeline->SetRenderState(D3DRS_DESTBLEND, static_cast<int>(Descriptor_->BlendInfo[1]));

        int FillMode = 0;
        unsigned long Texture = 0;
        if ((GetAppearanceRampDescriptor()->Flags & 0x400u) == 0x400u)
        {
            FillMode = 2;
            Texture = 0;
        }
        else
        {
            std::memcpy(&Texture, &Texture_.Frame[0], sizeof(Texture));
            FillMode = 3;
        }
        G_RenderPipeline->SetTextureStageTexture(0, reinterpret_cast<void*>(Texture));

        // Retail arms the unwind only after both GetRenderState and SetRenderState.
        struct FBeamFillModeState
        {
            IRenderPipeline* Pipeline;
            int Saved;
            FBeamFillModeState(IRenderPipeline* InPipeline, int InFillMode)
                : Pipeline(InPipeline)
            {
                Pipeline->GetRenderState(D3DRS_FILLMODE, &Saved);
                Pipeline->SetRenderState(D3DRS_FILLMODE, InFillMode);
            }
            ~FBeamFillModeState() noexcept(false)
            {
                Pipeline->SetRenderState(D3DRS_FILLMODE, Saved);
            }
        } SavedFillMode(G_RenderPipeline, FillMode);

        float ViewWorld[12];
        ICamera* const Camera = reinterpret_cast<ICamera*>(static_cast<std::uintptr_t>(G_DrawCookie));
        ComposeTransform(ViewWorld, Camera->GetViewTransform(), NodeWorldXform_);
        G_RenderPipeline->SetModelview(ViewWorld);
    }

    // FUN_06205720, __thiscall on the node (listing 0x06205720-0x06205eef). Offsets are
    // ESP-relative after the four register pushes.
    //   ++DAT_06257900 (G_DrawCounters[7]); Head [0x20] = begin of node+0xe4 (FUN_062085b0).
    //   Field walk over node+0x9c..+0xa0, the count taken once: field +0x70 (factory
    //     name) REPE CMPSB against "FLBeamField", 12 bytes. The first match: field +0x40
    //     GetNodeTransform(0, &T); Tip [0x40] = FUN_06208690(T, FUN_06206b60(node+0x70)),
    //     i.e. T applied to minus the translation of node+0x4c; D = Head.pos - Tip;
    //     (x*x + y*y) + z*z > 0.01 (AND 0x4100 / JNZ skips on not-greater) -> bHaveTip
    //     and straight to the state setup (LAB_062058d0); otherwise out of the walk to the
    //     count check.
    //   [node+0xb4] < 2 -> return. Flag 0x200 clear: D = Next.pos - Head.pos, the same
    //     length test -> k = Head.Age / (Next.Age - Head.Age) (FSTP float), Tip =
    //     Head.pos - D * k, bHaveTip.
    //   State [0x6c]: Counter 0, Node, Xform = FUN_06206c70 identity with a zero
    //     translation, then overwritten with FUN_06208640(camera vt+0x10, node+0x4c);
    //     Idx[0] = 0xffff (the other three and PrevPos stay uninitialised; zeroed here).
    //     LeadPos [0x30] = Head.pos, bTipNoCap [0x2b] = 0.
    //   bHaveTip: flag 0x100 -> the cap ring at Tip; else bTipNoCap = 1, LeadPos = Tip.
    //   Cap: FUN_06205f00(state, Head, &HW, &RGBA, &V); colour = ftol(A * 255) << 24 over
    //     the three masked RGB bytes; rail A = FUN_06206ca0 (M0, M3, M6) * HW: Tip - A
    //     (U0, V), Tip + A (U1, V); rail B the same with FUN_06206cc0 (M1, M4, M7); all
    //     four through FUN_06208aa0. Idx[0] != 0xffff (impossible here, the state was just
    //     set up) joins exactly as FUN_06206020 does. PrevPos = Tip, Idx = {A0, A1, B0,
    //     B1}, ++Counter.
    //   FUN_06206020(state, Head, LeadPos).
    //   Lead [0x20] = Head->Next; Lag [0x24] = bTipNoCap ? Head : Head->Next; Last (EBX) =
    //     LeadPos. While Lead != end (FUN_062085d0, null): D = Lag.pos - Last;
    //     (x*x + z*z) + y*y > 0.01 -> FUN_06206020(state, Lead, Lag.pos), Last = Lag.pos;
    //     Lead = Lead->Next, Lag = Lag->Next. Lead supplies the ramp time, Lag the
    //     position -- one particle apart after an uncapped tip, as in retail.
    // DAT_0625580c / DAT_06255808 (rail A / rail B switches) are .data bytes = 1 with six
    // readers each and no writer, so both rails are always built. U0 / V0 / U1 / V1 are
    // node+0xc4 / +0xc8 / +0xcc / +0xd0 = Texture_.Frame[1..4], read without the 0x1000 /
    // 0x2000 flips. The colour is the ramp's +0x1C EvalRgb, packed per channel here
    // (PackRingColor).
    namespace
    {
        // Beam's vector helpers keep Z/Y/X calculations before X/Y/Z float stores.
        #pragma optimize("", off)
        #pragma float_control(except, on, push)
        __declspec(noinline) void BeamSubtract(float* Out, const float* A, const float* B)
        {
            const double Z = static_cast<double>(A[2]) - B[2];
            const double Y = static_cast<double>(A[1]) - B[1];
            const double X = static_cast<double>(A[0]) - B[0];
            StoreAlchemyFpuFloat(Out[0], X);
            StoreAlchemyFpuFloat(Out[1], Y);
            StoreAlchemyFpuFloat(Out[2], Z);
        }
        __declspec(noinline) void BeamAdd(float* Out, const float* A, const float* B)
        {
            const double Z = static_cast<double>(A[2]) + B[2];
            const double Y = static_cast<double>(A[1]) + B[1];
            const double X = static_cast<double>(A[0]) + B[0];
            StoreAlchemyFpuFloat(Out[0], X);
            StoreAlchemyFpuFloat(Out[1], Y);
            StoreAlchemyFpuFloat(Out[2], Z);
        }
        __declspec(noinline) void BeamScale(float* Out, const float* Value, const float& Scale)
        {
            const double Z = static_cast<double>(Scale) * Value[2];
            const double Y = static_cast<double>(Scale) * Value[1];
            const double X = static_cast<double>(Scale) * Value[0];
            StoreAlchemyFpuFloat(Out[0], X);
            StoreAlchemyFpuFloat(Out[1], Y);
            StoreAlchemyFpuFloat(Out[2], Z);
        }
        __declspec(noinline) void BeamBasis(float* Out, const float* Matrix, int Column)
        {
            const double Z = LoadAppearanceScalar(Matrix[Column + 6]);
            const double Y = LoadAppearanceScalar(Matrix[Column + 3]);
            const double X = LoadAppearanceScalar(Matrix[Column]);
            StoreAlchemyFpuFloat(Out[0], X);
            StoreAlchemyFpuFloat(Out[1], Y);
            StoreAlchemyFpuFloat(Out[2], Z);
        }
        __declspec(noinline) bool BeamLongSegmentXyz(const float* Value)
        {
            const double X = static_cast<double>(Value[0]) * Value[0];
            const double Y = static_cast<double>(Value[1]) * Value[1];
            const double Xy = X + Y;
            const double Z = static_cast<double>(Value[2]) * Value[2];
            return Xy + Z > 0.01f;
        }
        __declspec(noinline) void BeamNegate(float* Out, const float* Value)
        {
            const double Z = -static_cast<double>(Value[2]);
            const double Y = -static_cast<double>(Value[1]);
            const double X = -static_cast<double>(Value[0]);
            StoreAlchemyFpuFloat(Out[0], X);
            StoreAlchemyFpuFloat(Out[1], Y);
            StoreAlchemyFpuFloat(Out[2], Z);
        }
        __declspec(noinline) double BeamExtrapolation(const float& NextAge, const float& HeadAge)
        {
            const double Denominator = static_cast<double>(NextAge) - HeadAge;
            return static_cast<double>(HeadAge) / Denominator;
        }
        __declspec(noinline) bool BeamLongSegmentXzy(const float* Value)
        {
            const double X = static_cast<double>(Value[0]) * Value[0];
            const double Z = static_cast<double>(Value[2]) * Value[2];
            const double Xz = X + Z;
            const double Y = static_cast<double>(Value[1]) * Value[1];
            return Xz + Y > 0.01f;
        }
        // 08aa0's cap arguments are raw words. 06020's inline writer converts them
        // through x87 only after its capacity gate (V, U, Z, Y, X, in that order).
        __declspec(noinline) unsigned short StoreBeamRawVertex(const float* Pos,
            unsigned long Diffuse, const float& U, const float& V)
        {
            const long Count = G_BeamDrawContext.VertCount;
            if (Count > static_cast<long>(G_BeamDrawContext.RequestedVerts))
                return static_cast<unsigned short>(G_BeamDrawContext.RequestedVerts);
            FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(G_BeamDrawContext.LockedVerts);
            std::memcpy(&Vertex->X, Pos, 3 * sizeof(float));
            Vertex->Diffuse = Diffuse;
            std::memcpy(&Vertex->U, &U, sizeof(float));
            std::memcpy(&Vertex->V, &V, sizeof(float));
            G_BeamDrawContext.LockedVerts = Vertex + 1;
            G_BeamDrawContext.VertCount = static_cast<unsigned short>(Count + 1);
            return static_cast<unsigned short>(Count);
        }
        __declspec(noinline) unsigned short StoreBeamConvertedVertex(const float* Pos,
            unsigned long Diffuse, const float& U, const float& V)
        {
            const long Count = G_BeamDrawContext.VertCount;
            if (Count > static_cast<long>(G_BeamDrawContext.RequestedVerts))
                return static_cast<unsigned short>(G_BeamDrawContext.RequestedVerts);
            FAlchemyVertex Words;
            Words.V = static_cast<float>(LoadAppearanceScalar(V));
            Words.U = static_cast<float>(LoadAppearanceScalar(U));
            const double Z = LoadAppearanceScalar(Pos[2]);
            FAlchemyVertex* const Vertex = static_cast<FAlchemyVertex*>(G_BeamDrawContext.LockedVerts);
            Words.Z = static_cast<float>(Z);
            Words.Y = static_cast<float>(LoadAppearanceScalar(Pos[1]));
            Words.X = static_cast<float>(LoadAppearanceScalar(Pos[0]));
            Words.Diffuse = Diffuse;
            std::memcpy(Vertex, &Words, sizeof(Words));
            G_BeamDrawContext.LockedVerts = Vertex + 1;
            G_BeamDrawContext.VertCount = static_cast<unsigned short>(Count + 1);
            return static_cast<unsigned short>(Count);
        }
        #pragma float_control(pop)
        #pragma optimize("", on)
    }

    #pragma float_control(except, on, push)
    void FLBeamAppearance::FillRibbon()
    {
        static const char kBeamFieldName[12] = "FLBeamField";
        FPooledParticle* Head = InstanceListHead_;
        ++G_DrawCounters[7];

        float Tip[3] = {0.0f, 0.0f, 0.0f};
        bool bHaveTip = false;
        bool bFieldTip = false;
        const long FieldTotal = static_cast<long>(FieldCount_);
        for (long FieldIndex = 0; FieldIndex < FieldTotal; ++FieldIndex)
        {
            IGenericFx* Field = AsFxNode(AssociatedFields_[FieldIndex]);
            const char* Name = Field->GetFxFactoryName();
            bool bMatch = true;
            for (int Byte = 0; Byte < 12; ++Byte)
            {
                if (Name[Byte] != kBeamFieldName[Byte])
                {
                    bMatch = false;
                    break;
                }
            }
            if (!bMatch)
            {
                continue;
            }

            float* FieldWorld = nullptr;
            Field->GetNodeTransform(0, &FieldWorld);
            float NegNodePos[3];
            BeamNegate(NegNodePos, NodeWorldXform_ + 9);
            TransformPerpBoundsCenter(*reinterpret_cast<const FTransform*>(FieldWorld),
                *reinterpret_cast<FVector*>(Tip),
                *reinterpret_cast<const FVector*>(NegNodePos));
            float Difference[3];
            BeamSubtract(Difference, &Head->PosX, Tip);
            if (BeamLongSegmentXyz(Difference))
            {
                bHaveTip = true;
                bFieldTip = true;
            }
            break;
        }

        if (!bFieldTip)
        {
            if (static_cast<long>(LiveInstanceCount_) < 2)
                return;
            const FPooledParticle* Next = Head->Next;
            if ((Descriptor_->Flags & 0x200u) != 0x200u)
            {
                float Difference[3];
                BeamSubtract(Difference, &Next->PosX, &Head->PosX);
                if (BeamLongSegmentXyz(Difference))
                {
                    const float K = static_cast<float>(BeamExtrapolation(Next->Age, Head->Age));
                    BeamScale(Difference, Difference, K);
                    BeamSubtract(Tip, &Head->PosX, Difference);
                    bHaveTip = true;
                }
            }
        }

        FBeamRingState State;
        State.Idx[0] = 0xffff;
        State.Idx[1] = 0;
        State.Idx[2] = 0;
        State.Idx[3] = 0;
        State.Counter = 0;
        State.Node = this;
        FillIdentity3x3(State.Xform);
        State.Xform[9] = 0.0f;
        State.Xform[10] = 0.0f;
        State.Xform[11] = 0.0f;
        State.PrevPos[0] = 0.0f;
        State.PrevPos[1] = 0.0f;
        State.PrevPos[2] = 0.0f;
        float Basis[12];
        ICamera* const Camera = reinterpret_cast<ICamera*>(static_cast<std::uintptr_t>(G_DrawCookie));
        ComposeTransform(Basis, Camera->GetOrientation(), NodeWorldXform_);
        std::memcpy(State.Xform, Basis, sizeof(Basis));

        const float* LeadPos = &Head->PosX;
        bool bTipNoCap = false;
        if (bHaveTip)
        {
            if ((Descriptor_->Flags & 0x100u) == 0x100u)
            {
                float HalfWidth = 0.0f;
                float Rgba[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                float V = 0.0f;
                EvalRingParams(State, Head, &HalfWidth, Rgba, &V);
                const unsigned long Diffuse = PackRingColor(Rgba);
                unsigned short NewIdx[4];
                float BasisColumn[3];
                float Side[3];
                float VertexPos[3];
                BeamBasis(BasisColumn, State.Xform, 0);
                const float WidthA = static_cast<float>(LoadAppearanceScalar(HalfWidth));
                BeamScale(Side, BasisColumn, WidthA);
                BeamSubtract(VertexPos, Tip, Side);
                NewIdx[0] = StoreBeamRawVertex(VertexPos, Diffuse, State.Node->Texture_.Frame[1], V);
                BeamAdd(VertexPos, Tip, Side);
                NewIdx[1] = StoreBeamRawVertex(VertexPos, Diffuse, State.Node->Texture_.Frame[3], V);
                BeamBasis(BasisColumn, State.Xform, 1);
                const float WidthB = static_cast<float>(LoadAppearanceScalar(HalfWidth));
                BeamScale(Side, BasisColumn, WidthB);
                BeamSubtract(VertexPos, Tip, Side);
                NewIdx[2] = StoreBeamRawVertex(VertexPos, Diffuse, State.Node->Texture_.Frame[1], V);
                BeamAdd(VertexPos, Tip, Side);
                NewIdx[3] = StoreBeamRawVertex(VertexPos, Diffuse, State.Node->Texture_.Frame[3], V);

                if (State.Idx[0] != 0xffff)
                    JoinRing(State, Tip, Diffuse, NewIdx);

                std::memcpy(State.PrevPos, Tip, 3 * sizeof(float));
                State.Idx[0] = NewIdx[0];
                State.Idx[1] = NewIdx[1];
                State.Idx[2] = NewIdx[2];
                State.Idx[3] = NewIdx[3];
                ++State.Counter;
            }
            else
            {
                bTipNoCap = true;
                LeadPos = Tip;
            }
        }

        EmitRing(State, Head, LeadPos);

        const FPooledParticle* Lead = Head->Next;
        const FPooledParticle* Lag = bTipNoCap ? InstanceListHead_ : Lead;
        const float* Last = LeadPos;
        while (Lead != nullptr)
        {
            float Difference[3];
            BeamSubtract(Difference, &Lag->PosX, Last);
            if (BeamLongSegmentXzy(Difference))
            {
                EmitRing(State, Lead, &Lag->PosX);
                Last = &Lag->PosX;
            }
            Lead = Lead->Next;
            Lag = Lag->Next;
        }
    }
    #pragma float_control(pop)

    // FUN_06205f00, __thiscall on the ring state, RET 0x10 (P, OutHW, OutRGBA, OutV):
    //   t = [P+0x2c] * [P+0x24] (InvDuration * Age);
    //   Scale = +0x5c ramp (node+0x8c, node+0x90); SS = +0x78 ramp (node+0x8c, t) * Scale;
    //   *OutHW = (+0x7c ramp (node+0x8c, t) * SS) * 0.5; Alpha = +0x70 ramp (node+0x8c, t);
    //   +0x74 vt+0x1c(&RGB, node+0x8c, t); OutRGBA = {R, G, B, Alpha};
    //   *OutV = ((Counter & 1) == [node+0xe1]) ? [node+0xc8] : [node+0xd0].
    // FUN_06206020 opens with the same sequence inline.
    #pragma float_control(except, on, push)
    void FLBeamAppearance::EvalRingParams(const FBeamRingState& State, const FPooledParticle* P,
                                          float* OutHalfWidth, float* OutRgba, float* OutV)
    {
        const float T = P->InvDuration * P->Age;
        const float Scale = static_cast<float>(ForwardAppearanceSingleEval(
            State.Node->Descriptor_->EffectScaleRamp, State.Node->AppearanceOuterTime_,
            State.Node->LodInput_));
        const float SizeScale = static_cast<float>(ForwardAppearanceSingleEval(
            State.Node->Descriptor_->PrimarySizeRamp, State.Node->AppearanceOuterTime_, T) * Scale);
        *OutHalfWidth = static_cast<float>((ForwardAppearanceSingleEval(
            State.Node->Descriptor_->TexFrameRamp, State.Node->AppearanceOuterTime_, T) * SizeScale) * 0.5);
        const float Alpha = static_cast<float>(ForwardAppearanceSingleEval(
            State.Node->Descriptor_->SizeRamp, State.Node->AppearanceOuterTime_, T));
        float Rgb[3];
        ForwardAppearanceRgbEval(State.Node->Descriptor_->ColorRamp, Rgb,
            State.Node->AppearanceOuterTime_, T);
        // 06205fce loads only red through x87; green, blue and alpha are raw words.
        const double Red = LoadAppearanceScalar(Rgb[0]);
        std::memcpy(&OutRgba[1], &Rgb[1], sizeof(float));
        std::memcpy(&OutRgba[2], &Rgb[2], sizeof(float));
        OutRgba[0] = static_cast<float>(Red);
        std::memcpy(&OutRgba[3], &Alpha, sizeof(float));
        const FLBeamAppearance* const Node = State.Node;
        const long Parity = Node->bRibbonParity_ ? 1 : 0;
        const float* const V = ((State.Counter & 1) == Parity)
            ? &Node->Texture_.Frame[2] : &Node->Texture_.Frame[4];
        std::memcpy(OutV, V, sizeof(float));
    }
    #pragma float_control(pop)

    // The callers' packing of FUN_06205f00's {R, G, B, A} (0x06205a62..0x06205ac8, and
    // inline in FUN_06206020): ftol(A * 255), then for R, G, B in turn `<< 8 | ftol(x *
    // 255) & 0xff`. Alpha is not masked; its high bits shift out.
    #pragma optimize("", off)
    #pragma float_control(except, on, push)
    __declspec(noinline) unsigned long FLBeamAppearance::PackRingColor(const float* Rgba)
    {
        unsigned long Packed = static_cast<unsigned long>(ConvertAlchemyFpuInteger(static_cast<double>(Rgba[3]) * 255.0f));
        Packed = (Packed << 8) |
                 (static_cast<unsigned long>(ConvertAlchemyFpuInteger(static_cast<double>(Rgba[0]) * 255.0f)) & 0xffu);
        Packed = (Packed << 8) |
                 (static_cast<unsigned long>(ConvertAlchemyFpuInteger(static_cast<double>(Rgba[1]) * 255.0f)) & 0xffu);
        Packed = (Packed << 8) |
                 (static_cast<unsigned long>(ConvertAlchemyFpuInteger(static_cast<double>(Rgba[2]) * 255.0f)) & 0xffu);
        return Packed;
    }
    #pragma float_control(pop)
    #pragma optimize("", on)

    // FUN_06206020, __thiscall on the ring state, RET 8 (P, Pos) (listing
    // 0x06206020-0x0620683e):
    //   HW / Alpha / RGB / V exactly as FUN_06205f00; colour EDI = ftol(A * 255) << 24
    //   over the three masked RGB bytes.
    //   Rail A: A = (Xform[0], Xform[3], Xform[6]) * HW; Pos - A (U0 = node+0xc4, V) ->
    //     [0x18], Pos + A (U1 = node+0xcc, V) -> BX / [0x1a]. Rail B: B = (Xform[1],
    //     Xform[4], Xform[7]) * HW; Pos - B (U0) -> [0x1c], Pos + B (U1) -> BP / [0x1e].
    //     Every store inline: past the requested count (JG) the index is the requested
    //     count and nothing is written.
    //   Idx[0] != 0xffff: flag 2 -> centre vertex ((Pos + PrevPos) * 0.5, U = V = 0.5) and
    //     the triangles (Idx0, Idx1, M) (Idx1, A1, M) (A1, A0, M) (A0, Idx0, M) (Idx2,
    //     Idx3, M) (Idx3, B1, M) (B1, B0, M) (B0, Idx2, M), each skipped when IndexCount + 2
    //     > requested; otherwise FUN_06208b50(Idx0, Idx1, A0, A1) and (Idx2, Idx3, B0, B1).
    //   Idx = {A0, A1, B0, B1}; PrevPos = *Pos; ++Counter.
    #pragma float_control(except, on, push)
    void FLBeamAppearance::EmitRing(FBeamRingState& State, const FPooledParticle* P,
                                    const float* Pos)
    {
        const float T = P->InvDuration * P->Age;
        const float Scale = static_cast<float>(ForwardAppearanceSingleEval(
            State.Node->Descriptor_->EffectScaleRamp, State.Node->AppearanceOuterTime_,
            State.Node->LodInput_));
        const float SizeScale = static_cast<float>(ForwardAppearanceSingleEval(
            State.Node->Descriptor_->PrimarySizeRamp, State.Node->AppearanceOuterTime_, T) * Scale);
        const float HalfWidth = static_cast<float>((ForwardAppearanceSingleEval(
            State.Node->Descriptor_->TexFrameRamp, State.Node->AppearanceOuterTime_, T) * SizeScale) * 0.5f);
        const float Alpha = static_cast<float>(ForwardAppearanceSingleEval(
            State.Node->Descriptor_->SizeRamp, State.Node->AppearanceOuterTime_, T));
        float Rgba[4];
        ForwardAppearanceRgbEval(State.Node->Descriptor_->ColorRamp, Rgba,
            State.Node->AppearanceOuterTime_, T);
        std::memcpy(&Rgba[3], &Alpha, sizeof(float));
        float V;
        const FLBeamAppearance* const Node = State.Node;
        const float* const VWord = ((State.Counter & 1) == (Node->bRibbonParity_ ? 1 : 0))
            ? &Node->Texture_.Frame[2] : &Node->Texture_.Frame[4];
        std::memcpy(&V, VWord, sizeof(float));
        const unsigned long Diffuse = PackRingColor(Rgba);

        unsigned short NewIdx[4];
        float BasisColumn[3];
        float Side[3];
        float VertexPos[3];
        // The inline rail-A extraction loads/stores X, Y, Z successively.
        BasisColumn[0] = static_cast<float>(LoadAppearanceScalar(State.Xform[0]));
        BasisColumn[1] = static_cast<float>(LoadAppearanceScalar(State.Xform[3]));
        BasisColumn[2] = static_cast<float>(LoadAppearanceScalar(State.Xform[6]));
        const float WidthA = static_cast<float>(LoadAppearanceScalar(HalfWidth));
        BeamScale(Side, BasisColumn, WidthA);
        BeamSubtract(VertexPos, Pos, Side);
        NewIdx[0] = StoreBeamConvertedVertex(VertexPos, Diffuse, State.Node->Texture_.Frame[1], V);
        BeamAdd(VertexPos, Pos, Side);
        NewIdx[1] = StoreBeamConvertedVertex(VertexPos, Diffuse, State.Node->Texture_.Frame[3], V);
        // Unlike rail A, rail B multiplies the loaded X/Y/Z values before storing.
        const double Bx = LoadAppearanceScalar(State.Xform[1]);
        const double By = LoadAppearanceScalar(State.Xform[4]);
        const double Bz = LoadAppearanceScalar(State.Xform[7]);
        StoreAlchemyFpuFloat(Side[2], Bz * HalfWidth);
        StoreAlchemyFpuFloat(Side[1], By * HalfWidth);
        StoreAlchemyFpuFloat(Side[0], Bx * HalfWidth);
        BeamSubtract(VertexPos, Pos, Side);
        NewIdx[2] = StoreBeamConvertedVertex(VertexPos, Diffuse, State.Node->Texture_.Frame[1], V);
        BeamAdd(VertexPos, Pos, Side);
        NewIdx[3] = StoreBeamConvertedVertex(VertexPos, Diffuse, State.Node->Texture_.Frame[3], V);

        if (State.Idx[0] != 0xffff)
            JoinRing(State, Pos, Diffuse, NewIdx);

        State.Idx[0] = NewIdx[0];
        State.Idx[1] = NewIdx[1];
        State.Idx[2] = NewIdx[2];
        State.Idx[3] = NewIdx[3];
        std::memcpy(State.PrevPos, Pos, 3 * sizeof(float));
        ++State.Counter;
    }
    #pragma float_control(pop)

    // Inline in FUN_06205720 (0x06205c4a-0x06205db7) and FUN_06206020 (0x0620644b-
    // 0x06206807), identical in both: flag 2 (desc+0x60 & 2) -> centre vertex
    // ((Pos + PrevPos) * 0.5, colour, U = V = 0.5) and eight triangles; else two quads.
    #pragma float_control(except, on, push)
    void FLBeamAppearance::JoinRing(const FBeamRingState& State, const float* Pos,
                                    unsigned long Diffuse, const unsigned short* NewIdx)
    {
        if ((State.Node->Descriptor_->Flags & 2u) == 2u)
        {
            float Middle[3];
            BeamAdd(Middle, Pos, State.PrevPos);
            const float Half = 0.5f;
            const double Z = static_cast<double>(Middle[2]) * Half;
            StoreAlchemyFpuFloat(Middle[2], Z);
            const double Y = static_cast<double>(Middle[1]) * Half;
            StoreAlchemyFpuFloat(Middle[1], Y);
            const double X = static_cast<double>(Middle[0]) * Half;
            StoreAlchemyFpuFloat(Middle[0], X);
            const unsigned short Mid = StoreBeamConvertedVertex(Middle, Diffuse, Half, Half);
            StoreBeamTriangle(State.Idx[0], State.Idx[1], Mid);
            StoreBeamTriangle(State.Idx[1], NewIdx[1], Mid);
            StoreBeamTriangle(NewIdx[1], NewIdx[0], Mid);
            StoreBeamTriangle(NewIdx[0], State.Idx[0], Mid);
            StoreBeamTriangle(State.Idx[2], State.Idx[3], Mid);
            StoreBeamTriangle(State.Idx[3], NewIdx[3], Mid);
            StoreBeamTriangle(NewIdx[3], NewIdx[2], Mid);
            StoreBeamTriangle(NewIdx[2], State.Idx[2], Mid);
        }
        else
        {
            StoreBeamQuad(State.Idx[0], State.Idx[1], NewIdx[0], NewIdx[1]);
            StoreBeamQuad(State.Idx[2], State.Idx[3], NewIdx[2], NewIdx[3]);
        }
    }
    #pragma float_control(pop)

    FxMeshAppearance::FxMeshAppearance(const char* PrimaryIid, const char* FactoryName)
        : FGenericFxObjectBase(PrimaryIid, FactoryName), MeshPropertySet_(nullptr)
    {
    }

    FxMeshAppearance::~FxMeshAppearance()
    {
    }

    // FUN_06224b60: operator_new(0x90), FUN_06224710 (FUN_06243ff0, then MeshId 0, MeshName
    // "", the byte 0, a fresh FxAnimatedTransform), outer vtable 0x0624df90, count 1, then
    // the adoption tail (the set is cached at node+0xc0 in retail; the port reads it
    // through MeshPropertySet_).
    void FxMeshAppearance::CreatePropertySet()
    {
        FFxMeshAppearancePropertySet* const Set = new FFxMeshAppearancePropertySet();
        AdoptPropertySet(Set);
        MeshPropertySet_ = Set;
    }

    // FUN_06224a90 (RET 8): the mesh constructor inline (as its factory 0x06225ed6..),
    // FUN_06244120 (0x06224b2d), the +0xc0 set cache (0x06224b32..0x06224b3c); *Out, 0.
    GENRESULT DACOM_API FxMeshAppearance::CloneNode(IDAComponent** Out)
    {
        FxMeshAppearance* const Node = new FxMeshAppearance(PrimaryIid_, FactoryName_);
        Node->InitAppearanceFromArchetype(this);
        Node->MeshPropertySet_ = MeshPropertySet_;
        *Out = Node;
        return GR_OK;
    }

    // FUN_06225000: list order, point spheres, then the node transform even if empty.
    GENRESULT DACOM_API FxMeshAppearance::GetBoundingSphere(float* OutSphere)
    {
        unsigned long Count = 0;
        FPooledParticle* Particle = InstanceListHead_;
        while (Particle != nullptr)
        {
            if (Count == 0)
            {
                // 06225221..06225233: radius is written before the Z word is read.
                std::memcpy(&OutSphere[0], &Particle->PosX, sizeof(float));
                std::memcpy(&OutSphere[1], &Particle->PosY, sizeof(float));
                OutSphere[3] = 0.0f;
                std::memcpy(&OutSphere[2], &Particle->PosZ, sizeof(float));
            }
            else
            {
                float Center[3];
                std::memcpy(Center, &Particle->PosX, sizeof(Center));
                MergeMeshBoundsPoint(OutSphere, Center);
            }
            Particle = Particle->Next;
            ++Count;
        }
        if (G_3DMath == nullptr)
            G_3DMath = Create3DMath();
        FVector WorldCenter;
        G_3DMath->TransformXform(WorldCenter, *reinterpret_cast<const FTransform*>(NodeWorldXform_),
                                *reinterpret_cast<const FVector*>(OutSphere));
        std::memcpy(OutSphere, &WorldCenter, sizeof(WorldCenter));
        return Count == 0 ? GR_GENERIC : GR_OK;
    }

    // ---- FFxMeshAppearancePropertySet's slots (inner 0x0624dff0; +0x3c is FUN_06244330
    // itself, inherited from FFxAppearancePropertySet) ----

    // FUN_06226b80: *Out = the byte +0x80 as stored, 0; else -8.
    GENRESULT DACOM_API FFxMeshAppearancePropertySet::GetPropBool(unsigned long Hash,
                                                                  unsigned char* Out)
    {
        if (Hash != kMeshUseParticleTransformHash)
            return GR_ALE_CREATE_FAIL;
        *Out = bUseParticleTransform;
        return GR_OK;
    }

    // FUN_06226bb0: the byte stored as passed, 0; else -8.
    GENRESULT DACOM_API FFxMeshAppearancePropertySet::SetPropBool(unsigned long Hash,
                                                                  unsigned char Value)
    {
        if (Hash != kMeshUseParticleTransformHash)
            return GR_ALE_CREATE_FAIL;
        bUseParticleTransform = Value;
        return GR_OK;
    }

    // FUN_06226be0 / FUN_06226c10: MeshId.
    GENRESULT DACOM_API FFxMeshAppearancePropertySet::GetPropInt(unsigned long Hash,
                                                                 unsigned long* Out)
    {
        if (Hash != kMeshIdHash)
            return GR_ALE_CREATE_FAIL;
        *Out = MeshId;
        return GR_OK;
    }

    GENRESULT DACOM_API FFxMeshAppearancePropertySet::SetPropInt(unsigned long Hash,
                                                                 unsigned long Value)
    {
        if (Hash != kMeshIdHash)
            return GR_ALE_CREATE_FAIL;
        MeshId = Value;
        return GR_OK;
    }

    // FUN_06226cf0: MeshName -> its buffer, 0; else FUN_0623f9a0.
    GENRESULT DACOM_API FFxMeshAppearancePropertySet::GetPropName(unsigned long Hash,
                                                                  const char** Out)
    {
        if (Hash != kMeshNameHash)
            return FFxNodePropertySet::GetPropName(Hash, Out);
        *Out = MeshName.Buffer;
        return GR_OK;
    }

    // FUN_06226d20 (0x06226d20..0x06226e00): MeshName -> the hashed-string assignment
    // (FFxHashedName::Assign), 0; else FUN_0623f9e0.
    GENRESULT DACOM_API FFxMeshAppearancePropertySet::SetPropName(unsigned long Hash,
                                                                  const char* Text)
    {
        if (Hash != kMeshNameHash)
            return FFxNodePropertySet::SetPropName(Hash, Text);
        MeshName.Assign(Text);
        return GR_OK;
    }

    // FUN_06226b40: MeshApp_ParticleTransform -> [+0x84] AddRef'd, 0; else FUN_0623fb50.
    GENRESULT DACOM_API FFxMeshAppearancePropertySet::GetPropTransform(unsigned long Hash,
                                                                       IAnimatedTransform** Out)
    {
        if (Hash != kMeshParticleTransformHash)
            return FFxNodePropertySet::GetPropTransform(Hash, Out);
        *Out = ParticleTransform;
        (*Out)->AddRef();
        return GR_OK;
    }

    // FUN_06226e10: FUN_06244320, then the table 0x06256298 unless it returned non-zero.
    int DACOM_API FFxMeshAppearancePropertySet::EnumerateProps(FNodePropertyRowCallback Callback,
                                                               void* Context)
    {
        const int Result = FFxNodePropertySet::EnumerateProps(Callback, Context);
        if (Result != 0)
            return Result;
        return WalkPropertyRows(kMeshPropertyRows, Callback, Context);
    }

    // FUN_06224c20 (alchemy.dll.txt 75997-76268), RET 0x10: (this, Six, Age, Duration),
    // always returns 0. Reached through vtable 0x0624dd90 +0x68 and the this+0xc adjustor
    // thunk FUN_06227330.
    GENRESULT DACOM_API FxMeshAppearance::EmitOne(const float* Six, unsigned long Extra,
                                                  float Duration)
    {
        float Age = 0.0f;
        std::memcpy(&Age, &Extra, 4);

        // 06224c2e..06224c6e: a full mesh pool (FreeCount, DAT_06257a58 == 0) first gives up
        // its oldest particle. The UsedHead item's owner (+0x08; only mesh appearances
        // allocate from this pool) erases it through FUN_06224fd0 -> FUN_062257f0: unlink
        // from the owner's +0xb4 list, IFxMesh slot 7 + Release on +0x30, FUN_0623ecf0 back
        // to the pool (the last two are FreePoolItem's mesh branch).
        if (G_PoolMesh.FreeCount == 0)
        {
            FPooledParticle* const Oldest = ParticleFromUsedLink(&G_PoolMesh, G_PoolMesh.UsedHead);
            FxMeshAppearance* const Owner = static_cast<FxMeshAppearance*>(
                static_cast<FGenericFxObjectBase*>(Oldest->Appearance));
            UnlinkParticleNode(&Owner->InstanceListHead_, &Owner->InstanceListTail_, Oldest);
            if (Owner->LiveInstanceCount_ != 0)
                --Owner->LiveInstanceCount_;
            ReleaseAppearancePoolItem(&G_PoolMesh, Oldest);
        }

        // [ESP+0x10]: the factory's out-param, released on every exit at 06224f90.
        IFxMesh* Mesh = nullptr;

        // 06224c79..06224c88: no bound factory (DAT_06257b9c) -> return 0, nothing emitted.
        if (G_FxMeshFactory == nullptr)
            return GR_OK;

        // 06224c95..06224c9e: this->vt+0x14 (FUN_062400b0, index 0) hands out the
        // appearance's composed world transform, node+0x4c.
        float* World12 = nullptr;
        GetNodeTransform(0, &World12);

        // 06224ca1..06224d25: both halves of Six go through I3DMathEngine +0x28 (DAT_06257a6c,
        // FUN_0623e830) as points -- the velocity picks up the translation too, as in retail.
        I3DMathEngine* const Math = Acquire3DMath();
        float WorldSix[6];
        Math->TransformXform(*reinterpret_cast<FVector*>(&WorldSix[0]),
                             *reinterpret_cast<const FTransform*>(World12),
                             *reinterpret_cast<const FVector*>(&Six[0])); // +0x28
        Math->TransformXform(*reinterpret_cast<FVector*>(&WorldSix[3]),
                             *reinterpret_cast<const FTransform*>(World12),
                             *reinterpret_cast<const FVector*>(&Six[3])); // +0x28

        // 06224d35..06224d5b: IFxMeshFactory slot 3 with the property set's +0x60 (MeshId)
        // and +0x74 (MeshName) dwords, the transformed pos+vel, &Mesh. Nonzero -> 06224f90.
        // The property set is the 0x90 object FUN_06224b60 builds (vtable 0x0624df90, the
        // FUN_06224710 body at +4): +0x60 the MeshId dword (body[0x17], 0 by default); +0x6c
        // the MeshName string (body[0x1a], vtable 0x0624bd80) whose buffer pointer is +0x74
        // (body[0x1c]) -- FUN_06225a50 assigns it "" (DAT_06257a70) at construction, which
        // allocates a one-byte buffer, so the pointer is never null. The only callee,
        // freelancer.exe FUN_004f6810, reads neither argument.
        FFxMeshAppearancePropertySet* const Set = MeshProps();
        void* const MeshIdArg = reinterpret_cast<void*>(static_cast<std::uintptr_t>(Set->MeshId));
        void* const MeshNameArg = Set->MeshName.Buffer;
        IFxMeshFactory* const Factory = static_cast<IFxMeshFactory*>(G_FxMeshFactory);
        if (Factory->Slot3(MeshIdArg, MeshNameArg, WorldSix, &Mesh) != 0)
        {
            if (Mesh != nullptr)
                Mesh->Release();
            return GR_OK;
        }

        // 06224d61..06224ed9: MeshApp_UseParticleTransform ([this+0xc0]+0x84) set ->
        // Anim = {FUN_06206c70 identity rotation, zero translation (0x94..0x9c)}, then
        // [[this+0xc0]+0x88]->Evaluate(this+0x8c, Age / Duration, &Anim) (IAnimatedTransform
        // +0x2c); T = {identity, the caller's LOCAL Six[0..2]} (ESP+0x20..0x4c);
        // A = MulTransform(World, T) (0x06224e63), B = MulTransform(A, Anim) (0x06224eb3),
        // both I3DMathEngine +0x1c with FUN_06217ab0 copies; IFxMesh slot 3 (&B). The
        // property set's +0x88 is always a FxAnimatedTransform (FUN_06224710 builds a
        // default one; the .ale reader fills it in place, FUN_062280a0), so the call is
        // unconditional (0x06224d61..0x06224d6f test only the byte).
        if (Set->bUseParticleTransform != 0)
        {
            float Anim[12] = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                               0.0f, 0.0f, 0.0f };
            {
                const float Phase = Age / Duration;
                unsigned long TimeA = 0;
                unsigned long TimeB = 0;
                std::memcpy(&TimeA, &AppearanceOuterTime_, sizeof(TimeA));
                std::memcpy(&TimeB, &Phase, sizeof(TimeB));
                Set->ParticleTransform->Evaluate(TimeA, TimeB, Anim);
            }
            const float T[12] = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                  Six[0], Six[1], Six[2] };
            float A[12];
            Math->MulTransform(*reinterpret_cast<FTransform*>(A),
                               *reinterpret_cast<const FTransform*>(World12),
                               *reinterpret_cast<const FTransform*>(T)); // +0x1C
            float B[12];
            Math->MulTransform(*reinterpret_cast<FTransform*>(B),
                               *reinterpret_cast<const FTransform*>(A),
                               *reinterpret_cast<const FTransform*>(Anim)); // +0x1C
            Mesh->Slot3(B);
        }

        // 06224edc..06224eeb: a counted local copy ([ESP+0x18]), released by FUN_06225940 at
        // 06224f8b.
        IFxMesh* const LocalMesh = Mesh;
        if (LocalMesh != nullptr)
            LocalMesh->AddRef();

        // 06224eee..06224f27: FUN_0623ec90 on this+0xbc (the mesh pool).
        FPooledParticle* const Particle = static_cast<FPooledParticle*>(AllocPoolItem(&G_PoolMesh));
        if (Particle != nullptr)
        {
            // 06224f29..06224f68: the caller's Six (not the transformed copy), owner, Age,
            // Duration, the mesh through FUN_062257b0 (AddRef, no Release of the old), then
            // 1.0f (DAT_0624bac4) / Duration.
            Particle->PosX = Six[0];
            Particle->PosY = Six[1];
            Particle->PosZ = Six[2];
            Particle->VelX = Six[3];
            Particle->VelY = Six[4];
            Particle->VelZ = Six[5];
            Particle->Appearance = static_cast<FGenericFxObjectBase*>(this);
            Particle->Duration = Duration;
            Particle->Age = Age;
            AssignComPtr(reinterpret_cast<IDAComponent**>(
                             reinterpret_cast<unsigned char*>(Particle) + 0x30),
                         LocalMesh);
            Particle->InvDuration = 1.0f / Particle->Duration;
        }

        // 06224f6f..06224f7a: FUN_06226690 appends the item to this+0xb4 -- a null item
        // included, which faults on its first store exactly as retail does.
        // LiveInstanceCount_ is the port's count of that list.
        AppendParticleNode(&InstanceListHead_, &InstanceListTail_, Particle);
        ++LiveInstanceCount_;

        // 06224f7f..06224f8b: FUN_06225940 on the local copy.
        if (LocalMesh != nullptr)
            LocalMesh->Release();

        // 06224f90..06224fc3: the out-param reference, then return 0.
        if (Mesh != nullptr)
            Mesh->Release();
        return GR_OK;
    }

    // FUN_06225470 (mesh node vtable 0x0624dd90 +0xC; outer +0x14), RET 8.
    //   0x06225488: Age (node+0x84) += Dt; node+0xb0 (emitter refs) == 0 and the list
    //     head node+0xb4 null -> node flags |= 1.
    //   0x062254bc..0x0622559d: the field walk over node+0x9c..+0xa0 -- a dead field
    //     (+0x1c) is erased from the array (FUN_06226600), a live one gets +0x9c
    //     ([node+0x18]), +0xa4(item+0xc, item+0x24) per particle and +0xa0.
    //   0x062255e2: per particle Age += Dt; FCOMP Age vs Life, AND 0x4100, JNZ ->
    //     alive (Age <= Life or unordered): pos += FUN_06206c40(vel, Dt) = vel * Dt, then the
    //     item's IFxMesh (+0x30) slot 5 with Dt (0x0622563c pushes it, 0x0622565d
    //     calls +0x14). Otherwise FUN_062257f0: unlink, IFxMesh slot 7, Release,
    //     FUN_0623ecf0 -- unlink, mesh cleanup, then return the pool item. Retail
    //     keeps no count on this list; LiveInstanceCount_ is the port's and is
    //     decremented to stay equal to the list length.
    //   0x06225676: node vt+0x34 (FUN_06225000, the bounds) into an unused stack local.
    //     The call still has observable math factory/interface side effects. Mesh's
    //     bounds implementation preserves those calls even for an empty list.
    //   Returns 0.
    GENRESULT DACOM_API FxMeshAppearance::Pad5(float Dt)
    {
        Age_ += Dt;
        if (EmitterRefCount_ == 0 && InstanceListHead_ == nullptr)
            NodeFlags_ |= 1;
        IDAComponent** Field = AssociatedFields_;
        IDAComponent** FieldEnd = AssociatedFields_ + FieldCount_;
        while (Field != FieldEnd)
        {
            if (static_cast<unsigned char>(AsFxNode(*Field)->IsNodeDead()) != 0)
            {
                Field = EraseAssociatedField(Field);
                FieldEnd = AssociatedFields_ + FieldCount_;
                continue;
            }
            AsFxNode(*Field)->BeginField(static_cast<IGenericFx*>(this), 0.0f);
            FPooledParticle* FieldParticle = InstanceListHead_;
            while (FieldParticle != nullptr)
            {
                AsFxNode(*Field)->ApplyFieldToVel(&FieldParticle->PosX, &FieldParticle->Age, 0.0f);
                FieldParticle = FieldParticle->Next;
            }
            AsFxNode(*Field)->Pad40();
            ++Field;
        }

        unsigned long DtBits = 0;
        std::memcpy(&DtBits, &Dt, sizeof(DtBits));
        FPooledParticle* Particle = InstanceListHead_;
        while (Particle != nullptr)
        {
            Particle->Age += Dt;
            if (!IsMeshParticleExpired(*Particle))
            {
                float Step[3];
                MeshParticleStep(*Particle, Dt, Step);
                Particle->PosX += Step[0];
                Particle->PosY += Step[1];
                Particle->PosZ += Step[2];
                IFxMesh* const ParticleMesh = *reinterpret_cast<IFxMesh**>(
                    reinterpret_cast<unsigned char*>(Particle) + 0x30);
                ParticleMesh->Slot5(reinterpret_cast<void*>(static_cast<std::uintptr_t>(DtBits)));
                Particle = Particle->Next;
            }
            else
            {
                FPooledParticle* const Next = Particle->Next;
                UnlinkParticleNode(&InstanceListHead_, &InstanceListTail_, Particle);
                --LiveInstanceCount_;
                ReleaseAppearancePoolItem(&G_PoolMesh, Particle);
                Particle = Next;
            }
        }
        float Bounds[4];
        GetBoundingSphere(Bounds);
        return GR_OK;
    }

    // FUN_06225240 (alchemy.dll.txt 76537-76691), RET 0xC. Reached from the outer
    // +0x10 thunk FUN_06226e60 (this+0xc, both stack arguments passed through).
    //   0x06225276: r = FUN_062401a0(this, Unused, Parent12); nonzero -> return r.
    //   0x06225292: this->vt+0x14 (FUN_062400b0, index 0) -> World = &node+0x4c.
    //   0x062252a2..0x06225449: walk the this+0xb4 instance list (FUN_06225890 begin,
    //   FUN_062258b0 end, next = [item]); an item whose +0x30 IFxMesh is null is skipped.
    //     [[this+0xc0]+0x84] (MeshApp_UseParticleTransform) set, 0x062252ff..0x062253c3:
    //       Anim = {FUN_06206c70 identity, zero translation};
    //       [[this+0xc0]+0x88]->Evaluate(this+0x8c, item.Age / item.Duration, &Anim);
    //       T = {FUN_06206c70 identity, item+0xc..+0x14};
    //       X = FUN_06208640(World, T) = MulTransform(World, T);
    //       Y = FUN_06208640(X, Anim); mesh slot 3 (&Y).
    //     clear, LAB_062253c5..0x06225436: M = copy of the 12 floats mesh slot 4 returns;
    //       M translation = I3DMathEngine +0x28 TransformXform(World, item+0xc); mesh
    //       slot 3 (&M).
    //   0x0622544f: return r (always 0).
    // Until 2026-09-26 the port posed these meshes from the per-tick Pad25
    // (OnAfterIntegrate) and again from FlushAppearanceBatches (PoseAllMeshExtra2), with
    // an identity node transform and without the particle-transform branch.
    GENRESULT DACOM_API FxMeshAppearance::ComposeNodeTransform(void* Unused, const float* Parent12)
    {
        const GENRESULT Result = FGenericFxObjectBase::ComposeNodeTransform(Unused, Parent12);
        if (Result != GR_OK)
            return Result;

        float* World = nullptr;
        GetNodeTransform(0, &World);

        FPooledParticle* Item = InstanceListHead_;
        while (Item != nullptr)
        {
            IFxMesh* const Mesh = *reinterpret_cast<IFxMesh**>(
                reinterpret_cast<unsigned char*>(Item) + 0x30);
            if (Mesh != nullptr)
            {
                I3DMathEngine* const Math = Acquire3DMath();
                FFxMeshAppearancePropertySet* const Set = MeshProps();
                if (Set->bUseParticleTransform != 0)
                {
                    float Anim[12] = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                       0.0f, 0.0f, 0.0f };
                    {
                        const float Phase = Item->Age / Item->Duration;
                        unsigned long TimeA = 0;
                        unsigned long TimeB = 0;
                        std::memcpy(&TimeA, &AppearanceOuterTime_, sizeof(TimeA));
                        std::memcpy(&TimeB, &Phase, sizeof(TimeB));
                        Set->ParticleTransform->Evaluate(TimeA, TimeB, Anim);
                    }
                    const float T[12] = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                          Item->PosX, Item->PosY, Item->PosZ };
                    float X[12];
                    Math->MulTransform(*reinterpret_cast<FTransform*>(X),
                                       *reinterpret_cast<const FTransform*>(World),
                                       *reinterpret_cast<const FTransform*>(T)); // +0x1C
                    float Y[12];
                    Math->MulTransform(*reinterpret_cast<FTransform*>(Y),
                                       *reinterpret_cast<const FTransform*>(X),
                                       *reinterpret_cast<const FTransform*>(Anim)); // +0x1C
                    Mesh->Slot3(Y);
                }
                else
                {
                    float M[12];
                    std::memcpy(M, Mesh->Slot4(), sizeof(M));
                    float WorldPos[3];
                    Math->TransformXform(*reinterpret_cast<FVector*>(WorldPos),
                                         *reinterpret_cast<const FTransform*>(World),
                                         *reinterpret_cast<const FVector*>(&Item->PosX)); // +0x28
                    M[9] = WorldPos[0];
                    M[10] = WorldPos[1];
                    M[11] = WorldPos[2];
                    Mesh->Slot3(M);
                }
            }
            Item = Item->Next;
        }
        return Result;
    }

    // FUN_062256a0 (alchemy.dll.txt 76899-76949), RET 4, reached through the outer +0x18
    // thunk FUN_06226ea0 (this+0xc). Walks the +0xb4 instance list (FUN_062258f0 begin,
    // FUN_062258d0 null end, next = [item]) and calls [item+0x30] IFxMesh slot 6 with
    // DAT_06257908, the draw walk's camera cookie; no null check on the mesh (EmitOne links
    // an item only after the factory handed one out). Returns 0.
    GENRESULT DACOM_API FxMeshAppearance::DrawOrEnqueue()
    {
        FPooledParticle* Item = InstanceListHead_;
        while (Item != nullptr)
        {
            IFxMesh* const Mesh = *reinterpret_cast<IFxMesh**>(
                reinterpret_cast<unsigned char*>(Item) + 0x30);
            Mesh->Slot6(reinterpret_cast<void*>(static_cast<std::uintptr_t>(G_DrawCookie)));
            Item = Item->Next;
        }
        return GR_OK;
    }

    FxParticleAppearance::FxParticleAppearance(const char* PrimaryIid, const char* FactoryName)
        : FGenericFxObjectBase(PrimaryIid, FactoryName), ParticlePropertySet_(nullptr)
    {
    }

    FxParticleAppearance::~FxParticleAppearance()
    {
    }

    // FUN_0622f550: operator_new(0xa8), FUN_06243ff0, both names "" and hashed, Flags 0,
    // outer vtable and count 1, then the adoption tail (cached at node+0xc0).
    void FxParticleAppearance::CreatePropertySet()
    {
        AdoptPropertySet(new FFxParticleAppearancePropertySet());
    }

    void FxParticleAppearance::OnPropertySetChanged()
    {
        ParticlePropertySet_ = static_cast<FFxParticleAppearancePropertySet*>(PropertySet_);
    }

    // FUN_0622f480 (RET 8): the particle constructor inline (as its factory 0x06230ce6..),
    // FUN_06244120 (0x0622f51d), the +0xc0 set cache (0x0622f522..0x0622f52c); *Out, 0.
    GENRESULT DACOM_API FxParticleAppearance::CloneNode(IDAComponent** Out)
    {
        FxParticleAppearance* const Node = new (std::nothrow) FxParticleAppearance(PrimaryIid_, FactoryName_);
        Node->InitAppearanceFromArchetype(this);
        Node->ParticlePropertySet_ = ParticlePropertySet_;
        *Out = Node;
        return GR_OK;
    }

    // FUN_06231930: UseDynamicRotation -> the low bit of the byte +0xa0 (`AND AL,1`),
    // SmoothRotation -> bit 1 (`SHR EDX,1; AND DL,1`), 0; else -8.
    GENRESULT DACOM_API FFxParticleAppearancePropertySet::GetPropBool(unsigned long Hash,
                                                                      unsigned char* Out)
    {
        if (Hash == kParticleUseDynamicRotationHash)
        {
            *Out = static_cast<unsigned char>(Flags & 1u);
            return GR_OK;
        }
        if (Hash == kParticleSmoothRotationHash)
        {
            *Out = static_cast<unsigned char>((Flags >> 1) & 1u);
            return GR_OK;
        }
        return GR_ALE_CREATE_FAIL;
    }

    // FUN_06231980: the bit set (`OR 1` / `OR 2`) when the byte is non-zero (`TEST CL,CL`),
    // cleared otherwise, 0; else -8.
    GENRESULT DACOM_API FFxParticleAppearancePropertySet::SetPropBool(unsigned long Hash,
                                                                      unsigned char Value)
    {
        unsigned long Mask = 0;
        if (Hash == kParticleUseDynamicRotationHash)
            Mask = 1u;
        else if (Hash == kParticleSmoothRotationHash)
            Mask = 2u;
        else
            return GR_ALE_CREATE_FAIL;
        if (Value != 0)
            Flags |= Mask;
        else
            Flags &= ~Mask;
        return GR_OK;
    }

    // FUN_06231ad0: LifeName / DeathName -> the buffer, 0; else FUN_0623f9a0.
    GENRESULT DACOM_API FFxParticleAppearancePropertySet::GetPropName(unsigned long Hash,
                                                                      const char** Out)
    {
        if (Hash == kParticleLifeNameHash)
        {
            *Out = LifeName.Buffer;
            return GR_OK;
        }
        if (Hash == kParticleDeathNameHash)
        {
            *Out = DeathName.Buffer;
            return GR_OK;
        }
        return FFxNodePropertySet::GetPropName(Hash, Out);
    }

    // FUN_06231b20 (0x06231b20..0x06231cdf): the hashed-string assignment on either name,
    // 0; else FUN_0623f9e0.
    GENRESULT DACOM_API FFxParticleAppearancePropertySet::SetPropName(unsigned long Hash,
                                                                      const char* Text)
    {
        if (Hash == kParticleLifeNameHash)
        {
            LifeName.Assign(Text);
            return GR_OK;
        }
        if (Hash == kParticleDeathNameHash)
        {
            DeathName.Assign(Text);
            return GR_OK;
        }
        return FFxNodePropertySet::SetPropName(Hash, Text);
    }

    // +0x3c is FUN_06244330 itself (FFxAppearancePropertySet's).

    // FUN_06231cf0: FUN_06244320, then the table 0x06256780 unless it returned non-zero.
    int DACOM_API FFxParticleAppearancePropertySet::EnumerateProps(
        FNodePropertyRowCallback Callback, void* Context)
    {
        const int Result = FFxNodePropertySet::EnumerateProps(Callback, Context);
        if (Result != 0)
            return Result;
        return WalkPropertyRows(kParticlePropertyRows, Callback, Context);
    }

    // ---- FxParticleAppearance (ALC-1, 2026-09-26) ----
    //
    // The two open questions wave16 left here are answered by the listing:
    //   - [node+0xc0] is the class's own property set (the 0xa8 object FUN_0622f550
    //     builds and FUN_0622f480 copies into every clone). Its +0x70/+0x74 are the
    //     ParticleApp_LifeName buffer/length (hash 0x0ec7a290, setter LAB_0622f270 /
    //     FUN_06231b20), +0x90/+0x94 ParticleApp_DeathName (0x03b76e6c), +0xa0 the
    //     UseDynamicRotation / SmoothRotation bits: FFxParticleAppearancePropertySet.
    //   - the "arity mismatch" is two interleaved calls: 0x0622f725 pushes &child first,
    //     then IAlchemy::HashNodeName(name) (RET 8) leaves it on the stack for
    //     IFxEffectLibrary::CreateEffect([node+0x18], hash, &child) (RET 0x10).
    namespace
    {
        FParticleAppRecord* AsParticleRecord(FPooledParticle* Record)
        {
            return reinterpret_cast<FParticleAppRecord*>(Record);
        }

        // FUN_062306c0, used by Pad47's Child and Held unwind scopes.
        struct FParticleEffectReference
        {
            FParticleEffectReference() : Value(nullptr), bReleaseOnDestruction(true) {}
            explicit FParticleEffectReference(IFxEffect* Effect)
                : Value(Effect), bReleaseOnDestruction(true)
            {
                if (Value != nullptr)
                    Value->AddRef();
            }
            ~FParticleEffectReference() noexcept(false)
            {
                if (bReleaseOnDestruction)
                    Reset();
            }
            FParticleEffectReference(const FParticleEffectReference&) = delete;
            FParticleEffectReference& operator=(const FParticleEffectReference&) = delete;

            void Reset()
            {
                // Pad47 disarms its unwind state before the normal Release call.
                bReleaseOnDestruction = false;
                if (Value != nullptr)
                {
                    Value->Release();
                    Value = nullptr;
                }
            }

            IFxEffect* Value;
            bool bReleaseOnDestruction;
        };

        // DAT_06257ae8: all particle quaternion and matrix kernels share this cache.
        I3DMathEngine* G_Particle3DMath = nullptr;

        I3DMathEngine* ParticleMath()
        {
            I3DMathEngine* Math = G_Particle3DMath;
            if (Math == nullptr)
            {
                Math = Create3DMath();
                G_Particle3DMath = Math;
            }
            return Math;
        }

        // FUN_062307f0: preserve aliased inputs until the service returns.
        void MultiplyParticleQuaternions(FQuaternion& Out, const FQuaternion& A,
            const FQuaternion& B)
        {
            FQuaternion Product;
            ParticleMath()->MulQuat(Product, A, B);
            std::memcpy(&Out, &Product, sizeof(Product));
        }

#pragma optimize("", off)
#pragma float_control(except, on, push)
        // The Newton terms retain LengthSquared; only the integer seed sees its float store.
        __declspec(noinline) double ParticleInverseSqrt(const double& LengthSquared)
        {
            const float RoundedLengthSquared = static_cast<float>(LengthSquared);
            std::int32_t LengthBits;
            std::memcpy(&LengthBits, &RoundedLengthSquared, sizeof(LengthBits));
            const std::uint32_t GuessBits = 0x5f3759dfu -
                static_cast<std::uint32_t>(LengthBits >> 1);
            float Guess;
            std::memcpy(&Guess, &GuessBits, sizeof(Guess));
            const double GuessSquared = static_cast<double>(Guess) * Guess;
            return (1.5 - ((GuessSquared * LengthSquared) * 0.5)) * Guess;
        }

        // FUN_06230690 retains the complete XYZ sum through its square root.
        __declspec(noinline) double ParticleAxisLength(const float* Axis)
        {
            const double Z = Axis[2];
            const double Y = Axis[1];
            const double X = Axis[0];
            const double SquareX = X * X;
            const double SquareY = Y * Y;
            const double SumXy = SquareX + SquareY;
            const double SquareZ = Z * Z;
            const double LengthSquared = SumXy + SquareZ;
            return ComputeAlchemyFpuSquareRoot(LengthSquared);
        }

        // FUN_06206c40 calculates all three Z/Y/X products before X/Y/Z stores.
        __declspec(noinline) void ScaleParticleQuaternionVector(FQuaternion& Out,
            const float* Axis, const float& Scale)
        {
            const double Z = static_cast<double>(Scale) * Axis[2];
            const double Y = static_cast<double>(Scale) * Axis[1];
            const double X = static_cast<double>(Scale) * Axis[0];
            StoreAlchemyFpuFloat(Out.X, X);
            StoreAlchemyFpuFloat(Out.Y, Y);
            StoreAlchemyFpuFloat(Out.Z, Z);
        }
#pragma float_control(pop)
#pragma optimize("", on)

        // About -pi/4, exactly as retail's double DAT_0624e7f0 holds it
        // (0xbfe921fb51000000 = -0.7853981573134661, read from the PE bytes).
        double ParticleTwistAngle()
        {
            const unsigned long long Bits = 0xbfe921fb51000000ull;
            double Value = 0.0;
            std::memcpy(&Value, &Bits, sizeof(Value));
            return Value;
        }

        // One sphere of FUN_0622fa10's merge, after the first: Out grows to hold Child.
        // bInlineLength selects the second child's shape (0x0622fbe0..0x0622fc0f: inline
        // (y*y + z*z) + x*x, FUN_06206b30 * LenSq, FUN_06206b80 add) over the first's
        // (FUN_06206ad0 length, component FADDs); the arithmetic is otherwise the same.
#pragma optimize("", off)
#pragma float_control(except, on, push)
        __declspec(noinline) void MergeParticleSphere(float* Out, const float* Child,
                                                       bool bInlineLength)
        {
            // FUN_06206c10 calculates Z/Y/X before the three X/Y/Z float stores.
            const double DifferenceZ = static_cast<double>(Child[2]) - Out[2];
            const double DifferenceY = static_cast<double>(Child[1]) - Out[1];
            const double DifferenceX = static_cast<double>(Child[0]) - Out[0];
            float D[3];
            StoreAlchemyFpuFloat(D[0], DifferenceX);
            StoreAlchemyFpuFloat(D[1], DifferenceY);
            StoreAlchemyFpuFloat(D[2], DifferenceZ);
            double LenSq;
            if (bInlineLength)
            {
                const double SquareY = static_cast<double>(D[1]) * D[1];
                const double SquareZ = static_cast<double>(D[2]) * D[2];
                const double SumYz = SquareY + SquareZ;
                const double SquareX = static_cast<double>(D[0]) * D[0];
                const float RoundedLength = static_cast<float>(SumYz + SquareX);
                LenSq = RoundedLength;
            }
            else
            {
                const double Z = D[2];
                const double Y = D[1];
                const double X = D[0];
                const double SquareX = X * X;
                const double SquareY = Y * Y;
                const double SumXy = SquareX + SquareY;
                const double SquareZ = Z * Z;
                LenSq = SumXy + SquareZ;
            }
            const double Dist = ParticleInverseSqrt(LenSq) * LenSq;
            const volatile float RoundedDist = static_cast<float>(Dist);
            // TEST AH,5 + JP skips containment unless the unrounded sum is ordered less.
            if (Dist + Out[3] < Child[3])
            {
                std::memcpy(Out, Child, 4 * sizeof(float));
                return;
            }
            // TEST AH,5 + JNP returns only for ordered less, using the stored distance.
            if (static_cast<double>(Child[3]) + RoundedDist < Out[3])
                return;
            const float NewRadius = static_cast<float>(
                ((static_cast<double>(Child[3]) + RoundedDist) + Out[3]) * 0.5);
            // TEST AH,0x41 + JNP skips the shift for ordered less/equal; unordered shifts.
            if (!(static_cast<double>(RoundedDist) <= 1e-06))
            {
                const double Inv = 1.0 / RoundedDist;
                StoreAlchemyFpuFloat(D[0], Inv * D[0]);
                StoreAlchemyFpuFloat(D[1], Inv * D[1]);
                StoreAlchemyFpuFloat(D[2], Inv * D[2]);
                const float Step = static_cast<float>(static_cast<double>(NewRadius) - Out[3]);
                const double ProductZ = static_cast<double>(Step) * D[2];
                const double ProductY = static_cast<double>(Step) * D[1];
                const double ProductX = static_cast<double>(Step) * D[0];
                float Offset[3];
                StoreAlchemyFpuFloat(Offset[0], ProductX);
                StoreAlchemyFpuFloat(Offset[1], ProductY);
                StoreAlchemyFpuFloat(Offset[2], ProductZ);
                StoreAlchemyFpuFloat(Out[0], static_cast<double>(Offset[0]) + Out[0]);
                StoreAlchemyFpuFloat(Out[1], static_cast<double>(Offset[1]) + Out[1]);
                StoreAlchemyFpuFloat(Out[2], static_cast<double>(Offset[2]) + Out[2]);
            }
            std::memcpy(Out + 3, &NewRadius, sizeof(NewRadius));
        }
#pragma float_control(pop)
#pragma optimize("", on)
    } // namespace

    // FUN_0622ff00 (node this, RET 8), alchemy.dll.txt 0x0622ff00-0x0623035f:
    //   0x0622ff06..0x0622ff6d: Pos += Dt * Vel.
    //   0x0622ff70..0x0622ff92: M = {FUN_06206c70 identity, Pos}.
    //   [propset+0xa0] & 1 (UseDynamicRotation), 0x0622ffab..0x06230236: N = Vel * rsqrt
    //     ((z*z + x*x) + y*y), Speed = rsqrt((x*x + y*y) + z*z) * that sum, both through the
    //     inline 0x5f3759df step. |N.y| < 0.99f (DAT_0624e7f8): axis (N.z, 0, -N.x), angle
    //     acos(N.y); otherwise axis (0, -N.z, N.y), angle acos(N.x), then MulQuat by the
    //     -pi/4 twist about (0, 0, 1) (FUN_062307f0 -> I3DMathEngine +0x18). Half-angle quat
    //     w = cos, xyz = axis * sin / FUN_06230690(axis) -> +0x58.
    //   0x06230239: +0x68 = 0. [propset+0xa0] & 2 (SmoothRotation): Speed < 0.5f -> +0x58 =
    //     identity and Blend = (Dt + [+0x68]) + 0.01f, else Blend = Speed * Dt; Blend > 1 ->
    //     1; +0x3c = QuatNlerp(+0x3c, +0x58, Blend) (+0x58) and the matrix from +0x3c,
    //     otherwise the matrix from +0x58 (QuaternionToMatrix +0x54) -> M's rotation.
    //   0x06230345: life effect (+0x6c) IFxEffect slot 3 (1, &M), no null test.
#pragma float_control(except, on, push)
    __declspec(noinline) void FxParticleAppearance::IntegrateRecord(FPooledParticle* Record,
                                                                 const float& Dt)
    {
        FParticleAppRecord* const R = AsParticleRecord(Record);
        const volatile float StepZ = static_cast<float>(static_cast<double>(Dt) * Record->VelZ);
        const volatile float StepY = static_cast<float>(static_cast<double>(Dt) * Record->VelY);
        const volatile float StepX = static_cast<float>(static_cast<double>(Dt) * Record->VelX);
        Record->PosX = static_cast<float>(static_cast<double>(StepX) + Record->PosX);
        Record->PosY = static_cast<float>(static_cast<double>(StepY) + Record->PosY);
        Record->PosZ = static_cast<float>(static_cast<double>(StepZ) + Record->PosZ);

        float M[12];
        FillIdentity3x3(M);
        std::memcpy(M + 9, &Record->PosX, 3 * sizeof(float));

        const unsigned long Flags = ParticleProps()->Flags; // [[node+0xc0]+0xa0]
        if ((Flags & 1) != 0)
        {
            float N[3];
            std::memcpy(N, &Record->VelX, sizeof(N));
            const float LenSqA = static_cast<float>(
                (static_cast<double>(N[2]) * N[2] + static_cast<double>(N[0]) * N[0]) +
                static_cast<double>(N[1]) * N[1]);
            const double RoundedNormalLength = LenSqA;
            const double InvLen = ParticleInverseSqrt(RoundedNormalLength);
            StoreAlchemyFpuFloat(N[0], static_cast<double>(N[0]) * InvLen);
            StoreAlchemyFpuFloat(N[1], static_cast<double>(N[1]) * InvLen);
            const double Nz = InvLen * N[2];
            const volatile double SpeedZ = Record->VelZ;
            const volatile double SpeedY = Record->VelY;
            const volatile double SpeedX = Record->VelX;
            const double SquareX = SpeedX * SpeedX;
            const double SquareY = SpeedY * SpeedY;
            const double SumXy = SquareX + SquareY;
            const double SquareZ = SpeedZ * SpeedZ;
            const double LenSqB = SumXy + SquareZ;
            const float Speed = static_cast<float>(ParticleInverseSqrt(LenSqB) * LenSqB);

            FQuaternion Q;
            // TEST AH,5 + JP takes the other axis for >=0.99 or unordered.
            if (std::fabs(static_cast<double>(N[1])) < 0.9900000095367432)
            {
                const float Axis[3] = {static_cast<float>(Nz), 0.0f, -N[0]};
                const double Half = std::acos(static_cast<double>(N[1])) * 0.5;
                const volatile float StoredHalf = static_cast<float>(Half);
                const float Sine = static_cast<float>(ComputeAlchemyFpuSine(Half));
                const float Scale = static_cast<float>(static_cast<double>(Sine) / ParticleAxisLength(Axis));
                const double RoundedHalf = StoredHalf;
                const float Cosine = static_cast<float>(ComputeAlchemyFpuCosine(RoundedHalf));
                std::memcpy(&Q.W, &Cosine, sizeof(Cosine));
                ScaleParticleQuaternionVector(Q, Axis, Scale);
            }
            else
            {
                const float Axis[3] = {0.0f, static_cast<float>(-Nz), N[1]};
                const double Half = std::acos(static_cast<double>(N[0])) * 0.5;
                const volatile float StoredHalf = static_cast<float>(Half);
                const float Sine = static_cast<float>(ComputeAlchemyFpuSine(Half));
                const float Scale = static_cast<float>(static_cast<double>(Sine) / ParticleAxisLength(Axis));
                FQuaternion Q1;
                const double RoundedHalf = StoredHalf;
                const float Cosine = static_cast<float>(ComputeAlchemyFpuCosine(RoundedHalf));
                std::memcpy(&Q1.W, &Cosine, sizeof(Cosine));
                ScaleParticleQuaternionVector(Q1, Axis, Scale);
                const float TwistAxis[3] = {0.0f, 0.0f, 1.0f};
                const double Twist = ParticleTwistAngle();
                const float TwistSine = static_cast<float>(ComputeAlchemyFpuSine(Twist));
                const float TwistScale = static_cast<float>(
                    static_cast<double>(TwistSine) / ParticleAxisLength(TwistAxis));
                FQuaternion Q2;
                const float TwistCosine = static_cast<float>(ComputeAlchemyFpuCosine(Twist));
                std::memcpy(&Q2.W, &TwistCosine, sizeof(TwistCosine));
                ScaleParticleQuaternionVector(Q2, TwistAxis, TwistScale);
                MultiplyParticleQuaternions(Q, Q1, Q2);
            }
            std::memcpy(R->VelQuat, &Q, sizeof(Q));
            R->Blend = 0.0f;

            FMatrix Rotation;
            // 0623023e reloads the class cache after the MulQuat callback.
            if ((ParticleProps()->Flags & 2) != 0)
            {
                // TEST AH,5 / JP: Speed >= 0.5 (or unordered) takes Speed * Dt.
                if (Speed < 0.5f)
                {
                    R->VelQuat[0] = 1.0f;
                    R->VelQuat[1] = 0.0f;
                    R->VelQuat[2] = 0.0f;
                    R->VelQuat[3] = 0.0f;
                    R->Blend = (static_cast<double>(Dt) + R->Blend) +
                        static_cast<double>(FloatFromBits(0x3c23d70au)); // DAT_0624c350
                }
                else
                {
                    R->Blend = static_cast<double>(Speed) * Dt;
                }
                if (R->Blend > 1.0f)
                    R->Blend = 1.0f;
                // 062302af: capture the raw interpolation word before lazy creation.
                float SlerpWeight;
                std::memcpy(&SlerpWeight, &R->Blend, sizeof(SlerpWeight));
                FQuaternion Nlerped;
                ParticleMath()->QuatNlerpValue(Nlerped,
                    *reinterpret_cast<const FQuaternion*>(R->BlendQuat),
                    *reinterpret_cast<const FQuaternion*>(R->VelQuat), &SlerpWeight);
                std::memcpy(R->BlendQuat, &Nlerped, sizeof(Nlerped));
                ParticleMath()->QuaternionToMatrix(Rotation,
                    *reinterpret_cast<const FQuaternion*>(R->BlendQuat)); // +0x54
            }
            else
            {
                ParticleMath()->QuaternionToMatrix(Rotation,
                    *reinterpret_cast<const FQuaternion*>(R->VelQuat)); // +0x54
            }
            std::memcpy(M, &Rotation, sizeof(float) * 9);
        }

        R->LifeEffect->Slot3(reinterpret_cast<void*>(1), M);
    }
#pragma float_control(pop)

    // FUN_062306e0 (the list, &next, record): FUN_062308c0 unlinks and yields the next
    // record, FUN_06230600's teardown runs on +0x6c/+0x70 (FreePoolItem's particle branch)
    // and FUN_0623ecf0 returns the record to [list+8] = G_PoolParticle.
    FPooledParticle* FxParticleAppearance::EraseRecord(FPooledParticle* Record)
    {
        FPooledParticle* const Next = Record->Next;
        UnlinkParticleNode(&InstanceListHead_, &InstanceListTail_, Record);
        ReleaseAppearancePoolItem(&G_PoolParticle, Record);
        return Next;
    }

    // FUN_0622f6a0 (node this, Six, Age, Duration; RET 0x10), always returns 0.
    //   0x0622f6ae..0x0622f6ea: pool full (DAT_06257ad4 FreeCount == 0) -> the oldest used
    //     record's owner ([item+8]) erases it (FUN_0622f880 -> FUN_062306e0).
    //   0x0622f6ef..0x0622f6fc: LifeName length ([propset+0x74]) == 0 -> return.
    //   0x0622f709..0x0622f714: DAT_06257b98 (IAlchemy) null -> return. DAT_06257b90 is
    //     not tested.
    //   0x0622f71a..0x0622f742: CreateEffect([node+0x18] = this, HashNodeName(LifeName),
    //     &Child); non-zero -> release Child, return.
    //   0x0622f748..0x0622f7c5: AddRef a local copy; AllocPoolItem([node+0xbc]); non-null ->
    //     FUN_06231d30(item, node, Six, Age, Duration, Child); FUN_06231470 appends the
    //     result (null included); release the local copy.
    //   0x0622f7c8..0x0622f801: the tail record's +0x30..+0x68 seeds; FUN_0622ff00(tail, 0).
    //   0x0622f809..0x0622f84e: [node+0x8c] != 0.0 -> Child slot 6; [node+0x90] != 1.0 ->
    //     Child slot 14. 0x0622f851: release Child.
    GENRESULT DACOM_API FxParticleAppearance::Pad47(const float* Six, float Age, float Duration)
    {
        if (G_PoolParticle.FreeCount == 0)
        {
            FPooledParticle* const Oldest =
                ParticleFromUsedLink(&G_PoolParticle, G_PoolParticle.UsedHead);
            FxParticleAppearance* const Owner = static_cast<FxParticleAppearance*>(
                static_cast<FGenericFxObjectBase*>(Oldest->Appearance));
            Owner->EraseRecord(Oldest);
        }

        const FFxHashedName& LifeNameString = ParticleProps()->LifeName;
        if (LifeNameString.Length == 0)
            return GR_OK;
        const char* const LifeName = LifeNameString.Buffer;

        FParticleEffectReference Child;
        if (G_RuntimeAlchemy != nullptr)
        {
            IAlchemy* const Alchemy = static_cast<IAlchemy*>(G_RuntimeAlchemy);
            IFxEffectLibrary* const Library = static_cast<IFxEffectLibrary*>(G_RuntimeEffectLibrary);
            const unsigned long Key = Alchemy->HashNodeName(LifeName);
            if (Library->CreateEffect(static_cast<IGenericFx*>(this),
                                      reinterpret_cast<void*>(static_cast<std::uintptr_t>(Key)),
                                      &Child.Value) == GR_OK)
            {
                FParticleEffectReference Held(Child.Value);
                float CapturedSix[6];
                std::memcpy(CapturedSix, Six, sizeof(CapturedSix));

                FParticleAppRecord* const Record =
                    static_cast<FParticleAppRecord*>(AllocPoolItem(&G_PoolParticle));
                if (Record != nullptr)
                {
                    // FUN_06231d30.
                    Record->Base.Appearance = static_cast<FGenericFxObjectBase*>(this);
                    std::memcpy(&Record->Base.PosX, CapturedSix, sizeof(CapturedSix));
                    std::memcpy(&Record->Base.Duration, &Duration, sizeof(Duration));
                    std::memcpy(&Record->Base.Age, &Age, sizeof(Age));
                    Record->LifeEffect = Held.Value;
                    if (Held.Value != nullptr)
                        Held.Value->AddRef();
                    InitializeParticleReciprocal(*Record);
                }
                AppendParticleNode(&InstanceListHead_, &InstanceListTail_,
                                   Record != nullptr ? &Record->Base : nullptr);
                Held.Reset();

                FParticleAppRecord* const Tail = AsParticleRecord(InstanceListTail_);
                Tail->UnusedSpawnWords_0x30[0] = 0;
                Tail->UnusedSpawnWords_0x30[1] = 0x3f800000u;
                Tail->UnusedSpawnWords_0x30[2] = 0;
                Tail->UnusedSpawnWords_0x4c[0] = 0x3f800000u;
                Tail->UnusedSpawnWords_0x4c[1] = 0;
                Tail->UnusedSpawnWords_0x4c[2] = 0;
                Tail->BlendQuat[0] = 1.0f;
                Tail->BlendQuat[3] = 0.0f;
                Tail->BlendQuat[2] = 0.0f;
                Tail->BlendQuat[1] = 0.0f;
                Tail->VelQuat[0] = 1.0f;
                Tail->VelQuat[3] = 0.0f;
                Tail->VelQuat[2] = 0.0f;
                Tail->VelQuat[1] = 0.0f;
                Tail->Blend = 0.0f;
                IntegrateRecord(InstanceListTail_, 0.0f);

                if (IsNonzeroParticleField(AppearanceOuterTime_))
                    ForwardEffectFloat(Child.Value, &IFxEffect::Slot6, AppearanceOuterTime_);
                if (IsNonunitParticleField(LodInput_))
                    ForwardEffectFloat(Child.Value, &IFxEffect::Slot14, LodInput_);
            }
        }
        return GR_OK;
    }
    // FUN_06230370 (node this, Dt; RET 8), always returns 0.
    //   0x06230394..0x062303b9: Age += Dt; no emitter reference ([node+0xb0]) and no record
    //     ([node+0xb4]) -> node flags |= 1.
    //   0x062303bc..0x062304bb: the associated-field walk (erase dead fields; Begin with
    //     [node+0x18]; per record with Age < Duration field +0xa4; End) = ApplyFieldsToPool.
    //   0x062304c1..0x062305c7, per record:
    //     Age <= Duration (TEST AH,0x41 / JP): Age += Dt; still not past Duration (AND
    //       0x4100 / JNZ) -> FUN_0622ff00(record, Dt); past it -> life slot 11(7) false ->
    //       life slot 13(1). Next record.
    //     past Duration (or unordered): life slot 11(7) false -> next; a death effect whose
    //       slot 11(7) is false -> next; otherwise FUN_062306e0 erases the record.
    GENRESULT DACOM_API FxParticleAppearance::Pad5(float Dt)
    {
        Age_ = static_cast<float>(static_cast<double>(Dt) + Age_);
        if (EmitterRefCount_ == 0 && InstanceListHead_ == nullptr)
            NodeFlags_ |= 1;
        ApplyFieldsToPool(this, true);

        FPooledParticle* Record = InstanceListHead_;
        while (Record != nullptr)
        {
            FParticleAppRecord* const R = AsParticleRecord(Record);
            if (Record->Age <= Record->Duration)
            {
                Record->Age = Dt + Record->Age;
                if (!(Record->Age > Record->Duration))
                    IntegrateRecord(Record, Dt);
                else if (static_cast<unsigned char>(
                             R->LifeEffect->Slot11(reinterpret_cast<void*>(7))) == 0)
                    R->LifeEffect->Slot13(reinterpret_cast<void*>(1));
                Record = Record->Next;
                continue;
            }
            if (static_cast<unsigned char>(R->LifeEffect->Slot11(reinterpret_cast<void*>(7))) == 0)
            {
                Record = Record->Next;
                continue;
            }
            if (R->DeathEffect != nullptr &&
                static_cast<unsigned char>(R->DeathEffect->Slot11(reinterpret_cast<void*>(7))) == 0)
            {
                Record = Record->Next;
                continue;
            }
            Record = EraseRecord(Record);
        }
        return GR_OK;
    }

    // FUN_0622fcf0 (node this, Unused, Parent12; RET 0xc):
    //   DeathName length ([propset+0x94]) != 0 -> per record with no death effect, Age
    //   strictly past Duration (AND 0x4100 / JNZ skips <, = and unordered), DAT_06257b90
    //   and DAT_06257b98 both bound: (release the null +0x70) CreateEffect([node+0x18],
    //   HashNodeName(DeathName), &record+0x70); on success slot 3(1, {identity, Pos}),
    //   [node+0x8c] != 0.0 -> slot 6, [node+0x90] != 1.0 -> slot 14.
    //   Then FUN_062401a0, the shared node compose.
    GENRESULT DACOM_API FxParticleAppearance::ComposeNodeTransform(void* Unused,
                                                                   const float* Parent12)
    {
        if (ParticleProps()->DeathName.Length != 0)
        {
            FPooledParticle* Record = InstanceListHead_;
            while (Record != nullptr)
            {
                FParticleAppRecord* const R = AsParticleRecord(Record);
                if (R->DeathEffect == nullptr && Record->Age > Record->Duration &&
                    G_RuntimeEffectLibrary != nullptr && G_RuntimeAlchemy != nullptr)
                {
                    IAlchemy* const Alchemy = static_cast<IAlchemy*>(G_RuntimeAlchemy);
                    IFxEffectLibrary* const Library =
                        static_cast<IFxEffectLibrary*>(G_RuntimeEffectLibrary);
                    const unsigned long Key = Alchemy->HashNodeName(ParticleProps()->DeathName.Buffer);
                    if (Library->CreateEffect(
                            static_cast<IGenericFx*>(this),
                            reinterpret_cast<void*>(static_cast<std::uintptr_t>(Key)),
                            &R->DeathEffect) == GR_OK)
                    {
                        float M[12] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                       0.0f, 0.0f, 0.0f};
                        // 0622fe42..fe53 copies position DWORDs without conversion.
                        std::memcpy(M + 9, &Record->PosX, 3 * sizeof(float));
                        R->DeathEffect->Slot3(reinterpret_cast<void*>(1), M);
                        if (IsNonzeroParticleField(AppearanceOuterTime_))
                            ForwardEffectFloat(R->DeathEffect, &IFxEffect::Slot6,
                                               AppearanceOuterTime_);
                        if (IsNonunitParticleField(LodInput_))
                            ForwardEffectFloat(R->DeathEffect, &IFxEffect::Slot14, LodInput_);
                    }
                }
                Record = Record->Next;
            }
        }
        return FGenericFxObjectBase::ComposeNodeTransform(Unused, Parent12);
    }
    // FUN_0622f8b0 (node, float; RET 8): FLD/FSTP to node+0x8c, then forward the
    // original argument to each non-null life/death effect through IFxEffect slot 6.
    GENRESULT DACOM_API FxParticleAppearance::SetNodeOuterTime(float OuterTime)
    {
        AppearanceOuterTime_ = OuterTime;
        ForwardChildFloat(&IFxEffect::Slot6, OuterTime);
        return GR_OK;
    }

    // FUN_0622f960 (node, float; RET 8): FLD/FSTP to node+0x90, then slot 14 on
    // each non-null child with the original argument.
    GENRESULT DACOM_API FxParticleAppearance::SetNodeLodInput(float LodInput)
    {
        LodInput_ = LodInput;
        ForwardChildFloat(&IFxEffect::Slot14, LodInput);
        return GR_OK;
    }

    // FUN_0622fa10 (node, Out; RET 8): every child effect's IFxEffect slot 15 sphere that
    // answers 0 goes into Out -- the first copied, the rest merged (MergeParticleSphere).
    // No sphere at all -> -1, otherwise 0.
    GENRESULT DACOM_API FxParticleAppearance::GetBoundingSphere(float* OutSphere)
    {
        unsigned long Merged = 0;
        FPooledParticle* Record = InstanceListHead_;
        while (Record != nullptr)
        {
            FParticleAppRecord* const R = AsParticleRecord(Record);
            float Child[4];
            if (R->LifeEffect != nullptr && R->LifeEffect->Slot15(Child) == GR_OK)
            {
                if (Merged == 0)
                {
                    std::memcpy(OutSphere, Child, sizeof(Child));
                }
                else
                {
                    MergeParticleSphere(OutSphere, Child, false);
                }
                ++Merged;
            }
            if (R->DeathEffect != nullptr && R->DeathEffect->Slot15(Child) == GR_OK)
            {
                if (Merged == 0)
                {
                    std::memcpy(OutSphere, Child, sizeof(Child));
                }
                else
                {
                    MergeParticleSphere(OutSphere, Child, true);
                }
                ++Merged;
            }
            Record = Record->Next;
        }
        return Merged == 0 ? GR_GENERIC : GR_OK;
    }

} // namespace AlchemyImpl
