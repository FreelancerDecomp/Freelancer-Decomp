// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll DodgeMissileIdea (ctor 0x062d94b0). See src/include/common_idea.h.
//
// CMN-SPLIT stage 1 (2026-10-14): split out of common_idea.cpp, code unchanged.

#include "common_idea.h"
#include "common_idea_internal.h"
#include "common_aisteer.h"   // BearingAngleToPoint, GetIfacePositionOrZero
#include "common_behavior.h"
#include "common_cequip.h"    // ThrustEquipType (IObjInspect::get_speed's 2nd argument)
#include "common_csys.h"      // Matrix / Transform
#include "common_iface.h"     // IObjInspect / IObjRW / IBehaviorManager / ObjRwFromWatchable
#include "common_pathfind.h"  // PathfindManager (IBehaviorManager::Pathfind)
#include "common_systemmanager.h" // SystemManager / ThrustManager (BaseIdea::Handle)
#include "x86math.h"          // I3DMathEngine::TransformMatrix

#include <cmath>
#include <cstring>

using namespace CmnIdea;

namespace
{
    // DodgeMissileIdea::Update's two inline cross products (062d975d-062d97aa and
    // 062d98cf-062d991c, the same schedule): Dir x Axis with every product and
    // difference on the x87 stack and one FSTP float per component.
    Vector DodgeCross(Vector const& Dir, Vector const& Axis)
    {
        Vector Result;
        Result.x = static_cast<float>(static_cast<double>(Dir.y) * Axis.z - static_cast<double>(Axis.y) * Dir.z);
        Result.y = static_cast<float>(static_cast<double>(Dir.z) * Axis.x - static_cast<double>(Axis.z) * Dir.x);
        Result.z = static_cast<float>(static_cast<double>(Axis.y) * Dir.x - static_cast<double>(Dir.y) * Axis.x);
        return Result;
    }
}

// ----------------------------------------------------------------------------
// DodgeMissileIdea -- see common_idea.h for the RTTI walk, the exact size proof
// and the verified address ranges this port covers.
// ----------------------------------------------------------------------------
// Two raw constants are decoded here rather than trusted from Ghidra's inline
// comments, because both would be wrong if taken at face value:
//
//   .rdata:DAT_0639f534 = db 0f 49 3e = 0x3E490FDB = 0.19634954f = pi/16
//     (Common.dll.txt:562530-562533; Ghidra prints it as "= DBh"). It is the
//     SAME value that is pushed as BuildRandomSpreadFrame's spread angle at
//     .text:062d9571, which is why it appears twice below as one named local.
//
//   .rdata:DAT_063a0540 = 00 00 00 60 66 66 e6 3f, i.e. the little-endian
//     qword 0x3FE6666660000000 (Common.dll.txt:563940-563947). That is NOT
//     double-precision 0.7 -- double 0.7 is 0x3FE6666666666666. It is the
//     FLOAT literal 0.7f WIDENED to double (float 0.7f == 0x3F333333, and
//     widening it gives 0x3FE6666660000000 exactly), which is what MSVC emits
//     for `fabs(<float expr>) > 0.7f`: the CRT `fabs` forces the comparison
//     into double and the float literal is promoted. Reproduced below as an
//     explicit double comparison against `static_cast<double>(0.7f)` so the
//     bit pattern is the same one retail compares against.
//
// The two `FCOMP double ptr [DAT_063a0540]` gates use DIFFERENT tests and they
// point OPPOSITE ways -- naming the predicate alone would have inverted one of
// them, so both are spelled out with their mask and their jump:
//   .text:062d9728 `TEST AH,0x41` (C0|C3) + `JNZ LAB_062d9a44`. The jump goes
//     to the bare `return 0` tail, and a `TEST/JNZ` on C0|C3 fires on
//     less-than OR equal, so execution CONTINUES only on strictly-greater:
//     `|dot| > 0.7f` keeps going, everything else returns 0.
//   .text:062d9881 `TEST AH,0x5` (C0|C2) + `JP LAB_062d98b4`. With no NaN, C2
//     is 0, so the tested byte is 0x01 exactly when C0 is set (strictly-less)
//     -- one set bit, odd parity, PF clear. PF is therefore SET on
//     greater-or-equal, so `JP` is the `|dot| >= 0.7f` arm (which picks the
//     ship's UP column) and the fall-through is the strictly-less arm (which
//     picks the ship's FORWARD column). Same constant, opposite sense.
// ----------------------------------------------------------------------------

// Ctor FUN_062d94b0 (item 1639), Common.dll.txt 222258-222279, 17 instructions.
// Writes exactly +0x04, +0x08, +0x10 (byte), +0x14 and +0x18, all zero, then
// installs .rdata:PTR_LAB_063a0524 at +0x00 (.text:062d94dc). Context (+0x0c)
// and Mode1c (+0x1c) are NOT touched -- both are zeroed here under this file's
// documented safe-over-init convention, see the header.
//
// The SEH frame retail opens at .text:062d94b0-062d94c2 (`PUSH -1; PUSH
// LAB_06395288`) guards no call at all in this body; it is MSVC's standard
// member-unwind funclet for the BaseWatcher sub-object at +0x14, which has a
// destructor. Not modelled -- the C++ compiler generates the equivalent.
DodgeMissileIdea::DodgeMissileIdea()
    : BaseIdea()          // 062d94c9-062d94d0: +0x04, +0x08 and the +0x10 byte
    , MissileWatcher()    // 062d94d2-062d94d7: the two dwords at +0x14/+0x18
    , Mode1c(0)           // NOT written by retail's ctor -- see the header
{
}

// Own vtable slot 1 -- LAB_062d94f0, item 1640, Common.dll.txt 222283-222728.
// ALL 428 instructions traced this pass (.text:062d94f0-062d9a4e inclusive).
int DodgeMissileIdea::Update(float /*DeltaTime*/)
{
    // 062d94f9-062d950c. DeltaTime is genuinely UNUSED: the argument exists
    // (every exit is `RET 0x4`) but no instruction in the body reads the
    // incoming stack slot. Retail really does ignore it here -- unlike
    // BreakAwayIdea::Update, which accumulates it into its reroll timer.
    if (Object == nullptr)
    {
        return 3;
    }

    // 062d950f-062d9529: the family's `Ptr ? Ptr - 8 : nullptr` Watchable ->
    // IObjRW downcast, written by retail as two tests whose second one
    // (`ADD EAX,-8; TEST EAX,EAX`) can only fire on the raw value 8.
    IObjRW* const Missile = WatchedObjectOf(MissileWatcher);
    if (Missile == nullptr)
    {
        return 1;
    }

    // Context is the IBehaviorManager; Context+0xc, Handle, Handle+0x14fc, its +0x84
    // owner and Handle+0x18 are all used by retail without a test.
    IBehaviorManager* const Manager = static_cast<IBehaviorManager*>(Context);
    FAiTarget* const Nav = &Handle->SelfTarget;
    FAiTarget* const NavTarget = Nav;
    IObjInspect const* const Owner = Nav->Iface;
    ThrustManager* const Thrust = &Handle->Thrust;

    // pi/16, the DAT_0639f534 / 0x3e490fdb constant decoded above.
    float const SpreadAngle = 0.19634954f;
    // The DAT_063a0540 qword, decoded above: float 0.7f widened to double.
    double const HeadOnCosine = static_cast<double>(0.7f);

    // 062d952c-062d953f: ask the AI volume probe whether anything has to be
    // avoided right now. A ZERO return means "yes, go this way instead" and
    // takes over the whole tick, exactly as in BreakAwayIdea::Update above.
    Vector ProbeDir;
    if (Manager->Pathfind->GetUserZoneApproachDirection(&ProbeDir) == 0)
    {
        // --------------------------------------------------------------------
        // 062d9545-062d9648. Structurally identical to BreakAwayIdea::Update's
        // own probe arm, down to the stack slots; the one textual difference is
        // that the null UpHint is a literal `PUSH 0x0` here (062d9555) where
        // BreakAway reuses a zeroed EDI.
        // --------------------------------------------------------------------
        // CORRECTION to item 1640's inherited note, which read the two pushes
        // at 062d9555/062d955b as arguments of the `CALL [EDX+0xc]` vcall: they
        // are not. Slot 0x0c is IObjInspect::get_transform(), which takes NO
        // arguments (`MOV ECX,EAX; CALL dword ptr [EDX + 0xc]` with nothing
        // pushed for it). MSVC simply pushed BuildOrientationBasis's own later
        // arguments first and evaluated the receiver call last. The same
        // misreading appears in that note for 062d95a5-062d95af; both are
        // corrected here and in the tracker.
        // 062d9545-062d956a: aim a basis down the probe's direction, with no up hint.
        Matrix Frame;
        BuildOrientationBasis(Frame, Owner->get_transform().Rotation, ProbeDir, nullptr);

        // 062d956f-062d958b: jitter it by pi/16, with a zero bank angle.
        Matrix Spread;
        BuildRandomSpreadFrame(Spread, Frame, SpreadAngle, 0.0f);

        // 062d958e-062d95bd. The fourth argument is `LEA ECX,[ESP+0x2c]` over a
        // dword retail zeroes at 062d9592 and reads back as a float at
        // 062d95bd -- the optional `float* OutAngle` out-param, not a flag and
        // not a zeroed vector.
        float TurnAngle = 0.0f;
        Vector Throttle = TurnHelper::get_angular_throttle(Owner->get_transform().Rotation,
                                                           Spread, &TurnAngle);   // 062d95b8

        // 062d95bd-062d9602 vs. LAB_062d9604. `TEST AH,0x41` + `JNZ` takes the
        // not-greater case away to the zero-vector arm, so the steering arm is
        // the strictly-greater one.
        Vector Command{ 0.0f, 0.0f, 0.0f };
        if (TurnAngle > SpreadAngle)
        {
            Vector const Up = Manager->get_ship_up_direction();         // 062d95e5
            ComputeBankedSteeringFrame(Nav, Up, Spread, Throttle, 1);   // 062d95f5
            Command = Throttle;   // 062d95fd
        }
        // else: LAB_062d9604 zero-fills a SEPARATE 12-byte slot and passes
        // that, so the small-turn arm commands a zero vector rather than the
        // unsteered throttle.

        // LAB_062d9621, 062d9621-062d9648. Note what is NOT here: the mode
        // ladder's shared tail additionally calls Behavior_ConditionalNotify,
        // and this arm does not.
        Thrust->SetAxisThrottle(Command);                             // 062d9627
        Thrust->SetThrottle(1.0f, 0);                                 // 062d9639
        return 0;
    }

    // ------------------------------------------------------------------------
    // LAB_062d964b: the mode ladder. Retail writes it as `MOV EAX,[ESI+0x1c];
    // SUB EAX,0x0; JZ mode0; DEC EAX; JZ mode1; DEC EAX; JNZ tail`, i.e. the
    // mode == 2 case is the fall-through and every other value returns 0.
    // ------------------------------------------------------------------------
    Vector Command{ 0.0f, 0.0f, 0.0f };

    if (Mode1c == 2)
    {
        // --------------------------------------------------------------------
        // 062d9665-062d97ee. "Nudge the predicted position sideways" -- this arm
        // never touches the throttle or the cruise gate and returns on its own.
        // --------------------------------------------------------------------

        // 062d9665-062d968c: the guard byte is Handle + 0x157a, and
        // 0x157a == 0x14fc + 0x7e == FAiTarget::bPosValid. The body is
        // FAiTarget::RefreshPos (FUN_062c47c0, item 1283) INLINED: guard raised
        // BEFORE the vcall (062d9685), radius out-param pushed LAST so it is
        // argument one (062d9682 pushes &Nav[0x18] after 062d9681 pushes
        // &Nav[0x0c]), slot 0x2c == IObjInspect::get_physical_radius. The
        // Mode1c == 1 arm below calls the very same function out of line.
        NavTarget->RefreshPos();

        // 062d968f-062d96c4: Delta = missile position - the cached centre.
        // Slot 0 of the watched object is IObjInspect::get_position.
        Vector const& MissilePos = ObjInspectOf(Missile)->get_position();
        Vector Delta{ MissilePos.x - NavTarget->CachedPos.x,     // 062d96bc-062d96c1
                      MissilePos.y - NavTarget->CachedPos.y,     // 062d96b2-062d96b8
                      MissilePos.z - NavTarget->CachedPos.z };   // 062d96a1-062d96ae

        // 062d96c9-062d96f2. Retail pushes (Src, Out) in that order, so the
        // LAST push is VecNormalize3's param_1/Out.
        Vector Dir{ 0.0f, 0.0f, 0.0f };
        VecNormalize3(&Delta, &Dir);

        // 062d96f7-062d9739. Fwd is column 2 of the owner's rotation (the
        // +0x08/+0x14/+0x20 stride), and retail accumulates the z-term, then
        // the y-term, then the x-term -- reproduced in that order because x87
        // addition is not associative.
        Matrix const& Rot = Owner->get_transform().Rotation;
        Vector const Fwd = MatrixColumn2(Rot);
        double const HeadOnDot = static_cast<double>(Dir.z) * Fwd.z
                               + static_cast<double>(Dir.y) * Fwd.y
                               + static_cast<double>(Dir.x) * Fwd.x;
        if (!(std::fabs(HeadOnDot) > HeadOnCosine))
        {
            // 062d9739 jumps to LAB_062d9a44, the bare `XOR EAX,EAX` tail.
            return 0;
        }

        // 062d973f-062d97b8. Up is column 1 (the +0x04/+0x10/+0x1c stride) of a
        // SECOND, freshly re-fetched get_transform -- retail does not reuse the
        // matrix it just read. Cross = Dir x Up, component for component, each
        // product and difference on the x87 stack and only the FSTP rounding:
        //   x  062d975d-062d976b   Dir.y*Up.z - Up.y*Dir.z
        //   y  062d976f-062d978a   Dir.z*Up.x - Up.z*Dir.x
        //   z  062d978e-062d97aa   Up.y*Dir.x - Dir.y*Up.x
        Matrix const& Rot2 = Owner->get_transform().Rotation;
        Vector const Up{ Rot2.E[0][1], Rot2.E[1][1], Rot2.E[2][1] };
        Vector const Cross = DodgeCross(Dir, Up);

        Vector Sideways;
        VecNormalize3(&Cross, &Sideways);   // 062d97b8

        // 062d97bd-062d97ee.
        Thrust->SetNudgeDirection(Sideways);
        return 0;
    }

    if (Mode1c == 1)
    {
        // --------------------------------------------------------------------
        // LAB_062d97f1, 062d97f1-062d99fb. The real dodge: build a frame that
        // points along the perpendicular escape direction and fly it.
        // --------------------------------------------------------------------
        NavTarget->RefreshPos();   // 062d97fc, FUN_062c47c0 called out of line

        // 062d9801-062d981d. FUN_06288800 is Vec3Sub(Out, A, B) -> Out = A - B
        // (its own body at .text:06288800-06288824 subtracts param_3 from
        // param_2 into param_1); the call site pushes (Nav+0x0c, MissilePos,
        // Out), so this is the identical Delta the Mode1c == 2 arm builds from
        // inline FSUBs.
        Vector const& MissilePos = ObjInspectOf(Missile)->get_position();
        Vector Delta{ MissilePos.x - NavTarget->CachedPos.x,
                      MissilePos.y - NavTarget->CachedPos.y,
                      MissilePos.z - NavTarget->CachedPos.z };

        Vector Dir{ 0.0f, 0.0f, 0.0f };
        VecNormalize3(&Delta, &Dir);   // 062d982f

        // 062d9848-062d9895: the same column-2 dot as above, but gated the
        // OTHER way (`TEST AH,0x5` + `JP`, see this section's header comment).
        Matrix const& Rot = Owner->get_transform().Rotation;
        Vector const Fwd = MatrixColumn2(Rot);
        double const HeadOnDot = static_cast<double>(Dir.z) * Fwd.z
                               + static_cast<double>(Dir.y) * Fwd.y
                               + static_cast<double>(Dir.x) * Fwd.x;

        // LAB_062d98b4 (taken by JP, i.e. |dot| >= 0.7f) reads the +0x04/+0x10/
        // +0x1c stride == column 1 == the ship's UP axis; the fall-through at
        // 062d9897 (|dot| < 0.7f) reads +0x08/+0x14/+0x20 == column 2 == the
        // ship's FORWARD axis. Both re-fetch get_transform first.
        // 062d9881 TEST AH,0x5 + JP: greater, equal OR unordered take the UP column.
        Matrix const& Rot2 = Owner->get_transform().Rotation;
        Vector Axis{ 0.0f, 0.0f, 0.0f };
        if (!(std::fabs(HeadOnDot) < HeadOnCosine))
        {
            Axis.x = Rot2.E[0][1];
            Axis.y = Rot2.E[1][1];
            Axis.z = Rot2.E[2][1];
        }
        else
        {
            Axis.x = Rot2.E[0][2];
            Axis.y = Rot2.E[1][2];
            Axis.z = Rot2.E[2][2];
        }

        // LAB_062d98cf, 062d98cf-062d992a. Byte-for-byte the same cross-product
        // schedule as the Mode1c == 2 arm's, with Axis in place of Up.
        Vector const Cross = DodgeCross(Dir, Axis);

        Vector Escape;
        VecNormalize3(&Cross, &Escape);   // 062d992a

        // 062d992f-062d9995: UpHint is column 1 of yet another fresh
        // get_transform (062d995b-062d997d writes E[0][1], E[1][1], E[2][1]
        // into three consecutive stack floats), and the Reference matrix is a
        // FOURTH fetch at 062d998c.
        Matrix const& Rot3 = Owner->get_transform().Rotation;
        Vector const UpHint{ Rot3.E[0][1], Rot3.E[1][1], Rot3.E[2][1] };
        Matrix Basis;
        BuildOrientationBasis(Basis, Owner->get_transform().Rotation, Escape, &UpHint);

        // 062d999a-062d99c4. The OutAngle argument here is a literal
        // `PUSH 0x0` (062d99ad), i.e. a null out-param -- and there is NO angle
        // gate on this arm at all, unlike the probe arm above. The banked
        // steering always runs.
        Vector Throttle = TurnHelper::get_angular_throttle(Owner->get_transform().Rotation,
                                                           Basis, nullptr);

        // 062d99c7-062d99f6.
        Vector const Up = Manager->get_ship_up_direction();         // 062d99de
        ComputeBankedSteeringFrame(Nav, Up, Basis, Throttle, 1);    // 062d99ee
        Command = Throttle;   // 062d99f6
    }
    else if (Mode1c != 0)
    {
        // 062d965f `JNZ LAB_062d9a44`: any mode other than 0/1/2 is a plain
        // `return 0` with nothing commanded.
        return 0;
    }
    // else Mode1c == 0: LAB_062d99fd zero-fills the command vector (Command is
    // already zero) and falls straight into the shared tail.

    // LAB_062d9a1a, 062d9a1a-062d9a4e. Reached by BOTH the Mode1c == 1 arm
    // (via the `JMP` at 062d99fb) and the Mode1c == 0 arm (by fall-through), so
    // the ConditionalNotify at 062d9a3f fires on both -- it is NOT exclusive to
    // mode 0.
    Thrust->SetAxisThrottle(Command);                             // 062d9a20
    Thrust->SetThrottle(1.0f, 0);                                 // 062d9a32
    Thrust->SetThrustersActive(true);                             // 062d9a3f
    return 0;
}

// FUN_062d9ae0, .text:062d9ae0-062d9aff (RET 4): `ADD ECX,0x14` (MissileWatcher), the
// IObjRW -> Watchable +8 with a null arm, `CMP EAX,[ECX]; JZ` dedup, then a tail JMP into
// BaseWatcher::set_pointer -- i.e. BaseWatcher::set.
void DodgeMissileIdea::SetWatchObject(void const* Object)
{
    Watchable const* const Target =
        (Object != nullptr)
            ? reinterpret_cast<Watchable const*>(static_cast<unsigned char const*>(Object) + 8)
            : nullptr;
    MissileWatcher.set(Target);
}

// Own vtable slot 4 -- LAB_062d9a70, Common.dll.txt 222758-222784,
// 26 instructions. "Stand down": hand every consumer a zero vector and clear
// the notify flag. Note the ORDER, which is the reverse of BreakAwayIdea::
// Commit's: Sync first, Refresh second.
//
// Retail re-zeroes the same 12-byte stack scratch before each of the first two
// calls (062d9a81-062d9a91 and 062d9aa9-062d9ab9) rather than reusing it, so
// neither helper can observe anything the other wrote; one local here is
// equivalent because neither helper writes through its Vector argument.
void DodgeMissileIdea::Commit()
{
    // Handle + 0x18 is recomputed for every call and never tested.
    Vector ZeroVector{ 0.0f, 0.0f, 0.0f };
    Handle->Thrust.SetNudgeDirection(ZeroVector);                       // 062d9a99
    ZeroVector.x = 0.0f;                                                          // 062d9aa9-062d9ab9
    ZeroVector.y = 0.0f;
    ZeroVector.z = 0.0f;
    Handle->Thrust.SetAxisThrottle(ZeroVector);                         // 062d9ac1
    Handle->Thrust.SetThrustersActive(false);                           // 062d9ace
}
