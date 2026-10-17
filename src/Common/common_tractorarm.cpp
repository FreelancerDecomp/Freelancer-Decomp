// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// TractorArm -- one grab arm of a CETractor (retail CETractor TU, 0x0629d870-0x0629df10).
// The class is declared in common_cequip.h, right after CETractor.
#include "common_cequip.h"
#include "common_cobject.h" // CLoot, CSimple::SelfWatchable
#include "common_archetype.h" // Archetype::Tractor (ReachSpeed)

#include <cmath>
#include <cstddef>

namespace
{
    // CLoot* -> its CSimple::SelfWatchable (+0x9c), null staying null (retail's ADD 0x9c with
    // the null test, e.g. 0629dedf-0629dee6).
    Watchable const* LootWatchable(CLoot* Target)
    {
        if (Target == nullptr)
            return nullptr;
        return (Watchable const*)((unsigned char const*)Target + offsetof(CSimple, SelfWatchable));
    }
}

TractorArm::TractorArm(CETractor* OwnerTractor)
    : Owner(OwnerTractor)
    , State(MODE_0)
    , PullOffset{0.0f, 0.0f, 0.0f}
    , Timer(0.0f)
    , ErrorCode(TractorOk)
{
    // Common.dll.c 0x3db80 (65311-65327). TargetWatch (BaseWatcher) default-constructs to a
    // null/unlinked watcher, matching retail's `this+8 = 0; this+0xc = 0`.
}

TractorArm::TractorArm(TractorArm const& Other)
    : Owner(Other.Owner)
    , State(Other.State)
    , PullOffset(Other.PullOffset)
    , Timer(Other.Timer)
    , ErrorCode(Other.ErrorCode)
{
    // Listing 0x0629d890-0x0629d91c. TargetWatch starts null; retail relinks through the CLoot
    // round trip (Watched -> Watched - 0x9c -> + 0x9c, each step tested, 0629d8c5-0629d8e3), so
    // a watched pointer equal to 0x9c (a CLoot at address 0, very strange) is not relinked.
    CLoot* Target = Other.GetTarget();
    if (Target != nullptr)
        TargetWatch.set(LootWatchable(Target));
}

TractorArm& TractorArm::operator=(TractorArm const& Other)
{
    // Listing 0x0629d920-0x0629d97e: the new pointer goes through the same CLoot round trip as
    // the copy ctor (0629d932-0629d94e); BaseWatcher::set's `!= Watched` test is retail's
    // CMP / JZ at 0629d94e.
    Owner = Other.Owner;
    State = Other.State;
    TargetWatch.set(LootWatchable(Other.GetTarget()));
    PullOffset = Other.PullOffset;
    Timer = Other.Timer;
    ErrorCode = Other.ErrorCode;
    return *this;
}

TractorArm::~TractorArm()
{
    // Common.dll.c 0x3dbd0 (65331-65363): retail's own body explicitly unwatches and resets
    // fields, then the compiler-emitted member destructor for the embedded BaseWatcher does
    // the same unwatch check again (harmless no-op the second time). TargetWatch's real
    // destructor runs automatically after this body, reproducing that redundant second check.
    TargetWatch.set(nullptr);
    State = MODE_0;
    PullOffset = Vector{0.0f, 0.0f, 0.0f};
    Timer = 0.0f;
    ErrorCode = TractorOk;
}

CLoot* TractorArm::GetTarget() const
{
    // Common.dll.c 0x3d870 (65123-65131).
    Watchable const* Watched = TargetWatch.target();
    if (Watched == nullptr)
        return nullptr;
    return reinterpret_cast<CLoot*>(
        const_cast<unsigned char*>(reinterpret_cast<unsigned char const*>(Watched)) -
        offsetof(CSimple, SelfWatchable));
}

void TractorArm::SetTarget(CLoot* Target)
{
    // Common.dll.c 0x3ded0 (65508-65532).
    TargetWatch.set(LootWatchable(Target));
    PullOffset = Vector{0.0f, 0.0f, 0.0f};
    Timer = 0.0f;
    ErrorCode = TractorOk;
    State = MODE_1;
}

void TractorArm::TurnOff()
{
    // Common.dll.c 0x3dea0 (65488-65504).
    TargetWatch.set(nullptr);
    State = MODE_0;
    PullOffset = Vector{0.0f, 0.0f, 0.0f};
    Timer = 0.0f;
    ErrorCode = TractorOk;
}

void TractorArm::Update(float Dt)
{
    // Common.dll.c 0x3dc40 (65369-65458). Cross-checked against the raw .txt listing to recover
    // FUN_0629de50's and the Vec3Assign call's real (Ghidra-dropped) arguments: FUN_0629de50 is
    // the same owner-Transform-composed-with-HpXform operation as CETractor::GetSourcePos (see
    // ResolveTractorSourceXform's comment in common_cetractor.cpp), reused here via that public
    // accessor. Mode machine: MODE_1 pulls the target toward the beam source at up to
    // Arch->ReachSpeed units/sec; once the remaining distance for this frame would reach or pass
    // the source, it snaps PullOffset exactly onto the target and becomes MODE_2, which just
    // counts a 1-second consume timer down to Event_0x18.
    if (State == MODE_0)
        return;

    CLoot* Target = GetTarget();
    TractorFailureCode const Code = Owner->VerifyTarget(Target);
    if (Code != TractorOk)
    {
        ErrorCode = Code;
        Owner->Notify(INotify::Event_0x15, this);
        TurnOff();
        return;
    }

    if (State == MODE_1)
    {
        Vector const SourcePos = Owner->GetSourcePos();
        Vector const Candidate{SourcePos.x + PullOffset.x, SourcePos.y + PullOffset.y,
                                SourcePos.z + PullOffset.z};
        Vector const Delta{Target->WorldPosition.x - Candidate.x, Target->WorldPosition.y - Candidate.y,
                            Target->WorldPosition.z - Candidate.z};

        // 0629dd18-0629dd93: dx and dz are rounded to float (FSTP), dy, MaxStep, the sum
        // (dz^2 + dy^2) + dx^2 and Frac stay on the FPU. FCOMPP DistSq vs MaxStep^2, TEST AH,0x41
        // / JNZ: only "greater" pulls; below, equal or unordered snap. dx * Frac is added
        // unrounded, dy * Frac and dz * Frac are rounded to float before their adds.
        double const Dx = Delta.x;
        double const Dy = (double)Target->WorldPosition.y - Candidate.y;
        double const Dz = Delta.z;
        double const MaxStep = (double)Dt * ((Archetype::Tractor const*)Owner->EquipArch())->ReachSpeed;
        double const DistSq = Dz * Dz + Dy * Dy + Dx * Dx;
        if (DistSq > MaxStep * MaxStep)
        {
            double const Frac = MaxStep / sqrt(DistSq);
            float const StepY = (float)(Dy * Frac);
            float const StepZ = (float)(Dz * Frac);
            PullOffset.x = (float)(Dx * Frac + PullOffset.x);
            PullOffset.y = StepY + PullOffset.y;
            PullOffset.z = StepZ + PullOffset.z;
            return;
        }

        PullOffset.x = Target->WorldPosition.x - SourcePos.x;
        PullOffset.y = Target->WorldPosition.y - SourcePos.y;
        PullOffset.z = Target->WorldPosition.z - SourcePos.z;
        Owner->Notify(INotify::Event_0x17, this);
        State = MODE_2;
        Timer = ACQUIRE_DELAY;   // 0629de02
        return;
    }

    if (State == MODE_2)
    {
        Timer -= Dt;
        if (Timer < 0.0f)
            Owner->Notify(INotify::Event_0x18, this);
    }
}

Vector TractorArm::GetTipPos() const
{
    // Common.dll.c 0x3df10 (65539-65589). Mode 1 (actively pulling): tip = beam source + the
    // in-flight PullOffset accumulated so far. Mode 2 (target locked at the tip, countdown
    // running): tip = the target's own current world position (it sits exactly at the tip by
    // construction once MODE_2 is entered) -- falls back to the beam source itself if the
    // watched target is gone (0629df5c-0629df6a). Retail's read of the literal 0x2c/0x30/0x34
    // (0629df89) sits behind a re-test of the pointer already tested non-null, so it is dead.
    // Mode 0 (idle) and any other mode: tip = the beam source itself.
    if (State == MODE_1)
    {
        Vector const Source = Owner->GetSourcePos();
        return Vector{Source.x + PullOffset.x, Source.y + PullOffset.y, Source.z + PullOffset.z};
    }

    if (State == MODE_2)
    {
        CLoot* Target = GetTarget();
        if (Target != nullptr)
            return Target->WorldPosition;
    }

    return Owner->GetSourcePos();
}

// TractorArm's two constants -- ordinals 1174 and 1481, .rdata 0x0639d80c / 0x0639d810, both 1.0f.
// Declared in common_cequip.h with the class; moved here from common_cequip.cpp (CMN-B2).
// ACQUIRE_DELAY is stored into TractorArm+0x1c at 0629de02 when the arm enters Mode 2 (the
// countdown before the towed CLoot is consumed). GRAB_TOLERANCE_SQUARED has no reader anywhere
// in Common.dll -- it is exported but dead in retail, ported for symbol parity only.
float const TractorArm::ACQUIRE_DELAY = 1.0f;
float const TractorArm::GRAB_TOLERANCE_SQUARED = 1.0f;
