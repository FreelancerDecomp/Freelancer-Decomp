// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// The death camera: controller ctor step at 0x00549371..0x0054939e, slots LAB_00549470 (0x58),
// LAB_00549450 (0x60) and LAB_00549620 (0x6c).

#include "space_camera_death.h"

#include "common_iface.h"   // IObjInspect -- the watched object's thiscall getters

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    // The four literals the controller writes at 0x00549376 .. 0x00549394.
    const float kDeathDriftSpeed = 1.0f;        // 0x3f800000
    const float kDeathShakeDuration = 1.5f;     // 0x3fc00000
    const float kDeathDriftDamping = 0.85f;     // 0x3f59999a
    const float kDeathMaxDriftSpeed = 100.0f;   // 0x42c80000

    // The attach slot's override for a live ship (0x005494e5): half the speed cap, and the same
    // damping the controller already wrote -- retail rewrites it anyway.
    const float kDeathShipMaxDriftSpeed = 50.0f;   // 0x42480000

    // The no-object drift direction (0x00549602): straight along -X.
    const float kDeathFallbackDirectionX = -1.0f;  // 0xbf800000

    // 0x0054967b / 0x0054968c: the shake lives inside a two-second window (.rdata 0x005d55b4).
    const float kShakeWindowSeconds = 2.0f;

    // 0x00549698: .rdata 0x005c75e0.
    const float kShakeTimeScale = 0.5f;

    // 0x005496a2: .rdata 0x005d4c88, a DOUBLE. The amplitude is quadratic in the time left.
    const double kShakeAmplitudeScale = 0.03;

    // 0x005496d1: .rdata 0x005caeec = 0x38000000 = 1/32768, i.e. one over RAND_MAX+1. Retail
    // divides rand()'s result by this to get a 0..1 fraction, so the port must use the same
    // divisor rather than RAND_MAX -- on this CRT they differ by one and the angle would drift.
    const float kRandToUnit = 3.0517578125e-05f;

    // 0x005494cf: the CEqObj type mask the attach slot tests. Both bits must be set.
    const unsigned kEqObjShipTypeMask = 0x503;

    const int kWatchableOffset = 8;

    // 0x005496bc..0x005496df / 0x00549723..0x00549748: an angle uniformly in [-Amplitude,
    // +Amplitude], spelled `frac * (hi - lo) + lo` with lo = -hi (a float, FST [ESP+0x18] /
    // FSTP [ESP+0x10]). FILD rand, FMUL float 1/32768, FLD hi FSUB lo, FMULP, FADD lo: the angle
    // is never stored -- it stays on the x87 stack for FCOS / FSIN.
    double RandomShakeAngle(float Amplitude)
    {
        const float Low = -Amplitude;
        const double Fraction = static_cast<double>(rand()) * static_cast<double>(kRandToUnit);
        return Fraction * (static_cast<double>(Amplitude) - static_cast<double>(Low)) +
               static_cast<double>(Low);
    }

    // The two shake loops, which are the same three-iteration rotation over a different pair of
    // rows. Retail unrolls neither; both are `MOV ECX,3` loops walking one float at a time. The
    // cosine is FSTP'd to a float temporary ([ESP+0x10]); the sine stays on the x87 stack.
    //
    // 0x005496f0: rows 0 and 2 (a stride of six floats) -- a rotation about Y:
    // row0 = sin*B + A*cos, row2 = cos*B - A*sin.
    void RotateRowsAboutY(float* M, float Cosine, double Sine)
    {
        for (int I = 0; I < 3; ++I)
        {
            const float A = M[I];
            const float B = M[I + 6];
            M[I] = static_cast<float>(Sine * static_cast<double>(B) +
                                      static_cast<double>(A) * static_cast<double>(Cosine));
            M[I + 6] = static_cast<float>(static_cast<double>(Cosine) * static_cast<double>(B) -
                                          static_cast<double>(A) * Sine);
        }
    }

    // 0x00549756: rows 0 and 1 (a stride of three floats) -- a rotation about Z. Note the signs
    // are the mirror of the loop above: row0 = cos*A - sin*B, row1 = cos*B + A*sin.
    void RotateRowsAboutZ(float* M, float Cosine, double Sine)
    {
        for (int I = 0; I < 3; ++I)
        {
            const float A = M[I];
            const float B = M[I + 3];
            M[I] = static_cast<float>(static_cast<double>(Cosine) * static_cast<double>(A) -
                                      Sine * static_cast<double>(B));
            M[I + 3] = static_cast<float>(static_cast<double>(Cosine) * static_cast<double>(B) +
                                          static_cast<double>(A) * Sine);
        }
    }

    // PTR_LAB_005e061c: the GameCamera table with the three death slots swapped in (0x58
    // LAB_00549470, 0x60 LAB_00549450, 0x6c LAB_00549620). Slot 0x54 stays GameCamera's own
    // name (LAB_0051b3b0) -- this class has no name override. Leaves take `this` on the stack,
    // the port's slot convention.

}

void DeathCameraConstruct(FDeathCamera* Cam)
{
    // Inlined into the controller ctor on ESI + 0x60c (0x00549364): no null test in retail.
    // 0x00549371: the FGameCamera ctor, called directly by the controller.
    GameCameraConstruct((FGameCamera*)Cam);

    // 0x00549376 .. 0x00549394: four floats, and nothing else. See the header for what stays
    // uninitialised and why the port does not fill it in.
    Cam->DriftSpeed = kDeathDriftSpeed;
    Cam->ShakeDuration = kDeathShakeDuration;
    Cam->DriftDamping = kDeathDriftDamping;
    Cam->MaxDriftSpeed = kDeathMaxDriftSpeed;

    // 0x0054939e: and the vptr becomes PTR_LAB_005e061c.
}

void DeathCameraAttach(FDeathCamera* Cam, void* Object)
{
    // 0x00549470: `this` (ECX) is used without a null test.
    // 0x0054947b: the usual watcher swap with the usual short-circuit.
    Watchable const* const Target =
        (Object != nullptr)
            ? reinterpret_cast<Watchable const*>(static_cast<unsigned char*>(Object) +
                                                 kWatchableOffset)
            : nullptr;

    if (Target != Cam->Watched.target())
        Cam->Watched.set(Target);

    // 0x0054949c: with nothing watched, everything is zeroed and the drift points along -X.
    // Retail tests the stored pointer AND the pointer minus eight, the same inlined
    // Watchable::get() the other cameras use.
    unsigned char* const Watched =
        const_cast<unsigned char*>(
            reinterpret_cast<const unsigned char*>(Cam->Watched.target()));
    if (Watched == nullptr || (Watched - kWatchableOffset) == nullptr)
    {
        // 0x005495e3.
        Cam->BasePosition[0] = 0.0f;
        Cam->BasePosition[1] = 0.0f;
        Cam->BasePosition[2] = 0.0f;
        Cam->DriftDirection[0] = kDeathFallbackDirectionX;
        Cam->DriftDirection[1] = 0.0f;
        Cam->DriftDirection[2] = 0.0f;
        return;
    }

    void* const Watchee = Watched - kWatchableOffset;

    // 0x005494b1 .. 0x005494ef: a live SHIP gets a lower speed cap. The chain is the retail one
    // walked literally -- the IObjRW's CEqObj at +0x10, its type word at +0x4c masked with
    // 0x503, and a non-null field at +0x218. Any link missing leaves the controller's 100.0f.
    //
    // The +0x218 field is not identified; it is tested for non-null and nothing more, so the
    // port tests it the same way rather than guessing what it means.
    unsigned char* const EqObj = *reinterpret_cast<unsigned char**>(
        static_cast<unsigned char*>(Watchee) + 0x10);
    if (EqObj != nullptr)
    {
        const unsigned TypeWord = *reinterpret_cast<const unsigned*>(EqObj + 0x4c);
        const void* const Field218 = *reinterpret_cast<void* const*>(EqObj + 0x218);
        if ((TypeWord & kEqObjShipTypeMask) == kEqObjShipTypeMask && Field218 != nullptr)
        {
            Cam->MaxDriftSpeed = kDeathShipMaxDriftSpeed;
            Cam->DriftDamping = kDeathDriftDamping;
        }
    }

    // 0x00549506: `MOV ECX,obj / CALL [EDX]` -- IObjInspect::get_position (slot 0, __thiscall).
    IObjInspect* const Inspect = static_cast<IObjInspect*>(Watchee);
    const Vector& World = Inspect->get_position();
    Cam->BasePosition[0] = World.x;
    Cam->BasePosition[1] = World.y;
    Cam->BasePosition[2] = World.z;

    // 0x00549534: `PUSH &local / CALL [EDX+4]` -- IObjInspect::get_velocity (slot 1), a
    // __thiscall returning Vector by value through the hidden buffer.
    const Vector Velocity = Inspect->get_velocity();
    Cam->DriftDirection[0] = Velocity.x;
    Cam->DriftDirection[1] = Velocity.y;
    Cam->DriftDirection[2] = Velocity.z;

    // 0x0054954f: and the drift is CLAMPED, not normalised -- a slow wreck drifts slowly, a fast
    // one is capped. FLD z/y/x, (x*x + y*y) + z*z, FSQRT: the length stays on the x87 stack.
    // FCOM float +0x150, TEST AH,0x41 / JNZ: only a length strictly above the cap goes on (equal
    // and unordered are left alone).
    const double X = Cam->DriftDirection[0];
    const double Y = Cam->DriftDirection[1];
    const double Z = Cam->DriftDirection[2];
    const double Length = sqrt((X * X + Y * Y) + Z * Z);
    if (Length > static_cast<double>(Cam->MaxDriftSpeed))
    {
        // 0x00549580..0x005495da: FDIVR 1.0f keeps 1/Length on the stack. inv*y and inv*z go
        // through float temporaries (FSTP [ESP+0x10] / [ESP+0x14]); inv*x does not. Each is then
        // multiplied by the cap and stored as float, x first.
        const double Inv = 1.0 / Length;
        const double Cap = Cam->MaxDriftSpeed;
        const float InvY = static_cast<float>(Inv * Y);
        const float InvZ = static_cast<float>(Inv * Z);
        Cam->DriftDirection[0] = static_cast<float>(Inv * X * Cap);
        Cam->DriftDirection[1] = static_cast<float>(static_cast<double>(InvY) * Cap);
        Cam->DriftDirection[2] = static_cast<float>(static_cast<double>(InvZ) * Cap);
    }
}

bool DeathCameraIsFinished(const FDeathCamera* Cam)
{
    // LAB_00549450 opens with FLD [ECX + 0x144]: no null test on `this`.
    // LAB_00549450: FCOMP against 0.0 with TEST AH,0x44 / JP. The mask covers C3 ("equal") and
    // C2 ("unordered"); the jump is taken on an EVEN number of those bits, so exactly one of
    // them -- an equality -- falls through to `return 1`. Everything else, greater OR less,
    // returns 0.
    return Cam->ShakeTimer == 0.0f;
}

void DeathCameraUpdate(FDeathCamera* Cam, float DeltaSeconds)
{
    // 0x00549620..00549626: MOV EBX,ECX / MOV EAX,[EBX] -- no null test on `this`.
    // 0x00549628: the first thing it does is ask its own slot 0x60, and a finished camera does
    // nothing whatsoever -- it does not even copy its orientation out.
    if (DeathCameraIsFinished(Cam))
        return;

    // 0x00549633: the timer runs down and is clamped at zero, which is what keeps the equality
    // test above reachable. FCOMP 0 / TEST AH,0x41 / JNZ: only a timer above zero runs. The
    // difference is FST'd to the timer, then the UNROUNDED difference is compared (TEST AH,5 /
    // JP): below zero stores 0.
    if (Cam->ShakeTimer > 0.0f)
    {
        const double Left = static_cast<double>(Cam->ShakeTimer) - static_cast<double>(DeltaSeconds);
        Cam->ShakeTimer = static_cast<float>(Left);
        if (Left < 0.0)
            Cam->ShakeTimer = 0.0f;
    }

    // 0x0054966d: how long the death has been running, kept on the x87 stack. The shake only
    // happens in the first two seconds of it (FCOM 2.0f, TEST AH,5 / JP: strictly less).
    const double Elapsed =
        static_cast<double>(Cam->ShakeDuration) - static_cast<double>(Cam->ShakeTimer);
    if (Elapsed < static_cast<double>(kShakeWindowSeconds))
    {
        // 0x0054968c..0x005496a8: amplitude falls off with the SQUARE of the time left in the
        // window. FSUBR 2.0f, FMUL 0.5f, square, FMUL double 0.03, and only then FSTP float.
        const double Scaled = (static_cast<double>(kShakeWindowSeconds) - Elapsed) *
                              static_cast<double>(kShakeTimeScale);
        const float Amplitude = static_cast<float>(Scaled * Scaled * kShakeAmplitudeScale);

        // Two independent draws, in this order: retail calls rand() once per axis and the second
        // call happens after the first rotation has already been applied. FCOS of the unrounded
        // angle goes to a float temporary; FSIN of it stays on the stack.
        const double AngleY = RandomShakeAngle(Amplitude);
        RotateRowsAboutY(Cam->ShakeOrientation, static_cast<float>(cos(AngleY)), sin(AngleY));

        const double AngleZ = RandomShakeAngle(Amplitude);
        RotateRowsAboutZ(Cam->ShakeOrientation, static_cast<float>(cos(AngleZ)), sin(AngleZ));
    }

    FSpaceCamera* const Base = (FSpaceCamera*)Cam;

    // 0x00549799: nine floats out of the shake matrix into the world orientation.
    memcpy(Base->World, Cam->ShakeOrientation, 9 * sizeof(float));

    // 0x00549787..0x005497bb: the drift decays -- FLD dt, FMUL damping, FSUBR speed, FST speed --
    // and the UNROUNDED result is compared with zero (TEST AH,5 / JP): below zero stores 0.
    const double Speed = static_cast<double>(Cam->DriftSpeed) -
                         static_cast<double>(DeltaSeconds) * static_cast<double>(Cam->DriftDamping);
    Cam->DriftSpeed = static_cast<float>(Speed);
    Base->bViewDirty = 1;
    if (Speed < 0.0)
        Cam->DriftSpeed = 0.0f;

    // 0x005497c5..0x0054980d: and the accumulator takes one step along the drift direction, with
    // the speed AFTER the decay (reloaded from memory). The step stays on the stack; step*dir.y
    // and step*dir.z go through float temporaries, step*dir.x does not. The accumulator -- not
    // the base position -- is what becomes the world position.
    const double Step = static_cast<double>(DeltaSeconds) * static_cast<double>(Cam->DriftSpeed);
    const float StepY = static_cast<float>(Step * static_cast<double>(Cam->DriftDirection[1]));
    const float StepZ = static_cast<float>(Step * static_cast<double>(Cam->DriftDirection[2]));
    Cam->DriftPosition[0] = static_cast<float>(Step * static_cast<double>(Cam->DriftDirection[0]) +
                                               static_cast<double>(Cam->DriftPosition[0]));
    Cam->DriftPosition[1] = static_cast<float>(static_cast<double>(StepY) +
                                               static_cast<double>(Cam->DriftPosition[1]));
    Cam->DriftPosition[2] = static_cast<float>(static_cast<double>(StepZ) +
                                               static_cast<double>(Cam->DriftPosition[2]));

    Base->WorldPos[0] = Cam->DriftPosition[0];
    Base->WorldPos[1] = Cam->DriftPosition[1];
    Base->WorldPos[2] = Cam->DriftPosition[2];
    Base->bViewDirty = 1;
}

void FDeathCamera::Attach(void* Object)
{
    DeathCameraAttach(this, Object);
}

bool FDeathCamera::IsFinished()
{
    return DeathCameraIsFinished(this);
}

void FDeathCamera::Update(float DeltaSeconds)
{
    DeathCameraUpdate(this, DeltaSeconds);
}
