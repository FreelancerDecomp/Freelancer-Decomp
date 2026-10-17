// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_facebehavior.h"
#include "common_pathfind.h" // PathfindManager (IBehaviorManager+0x0C)

#include "common_systemmanager.h"  // SystemManager, ThrustManager::CheckSteeringGate (FUN_062e92e0)
#include "common_aisteer.h"        // BearingAngleToIface, GetInterceptClearanceOfIface,
                                   // BuildPerpendicularFrame, BuildOrientationBasis
#include "common_iface.h"          // IObjInspect, ObjInspectOf
#include "common_pub.h"            // pub::AI::Personality::EngineKillUseStruct
#include "common_iustategraph.h"   // FindClosestHostileTarget

#include <cstdint>

// FUN_062e74a0 (item 1798) is SystemManagerTimerTable::FindVerifiedObject.

// FaceBehavior -- see common_facebehavior.h for the full RTTI/layout writeup
// this pass relied on. Ported from EXE/Disasm/Common.dll.txt (raw
// disassembly is authoritative in this module over Ghidra's Common.dll.c
// decompilation -- see this marathon's "ground truth precedence" rule).

// FUN_062c88e0 (Common.dll.txt 195675-195737) -- constructor. Calls the
// shared base-init helper FUN_062d2020 first (item 1529 -- NOT the
// REAL field-initialising base ctor: it writes +0x04..+0x29, see Behavior in common_behavior.h), zeroes TargetKind2c/
// Watcher30, default-constructs SubObjectA/B for real (MoveIdea/FaceIdea,
// identified 2026-09-11 d285:common-port -- see common_idea.h) plus a real
// AfterburnerIdea (SubObjectC, identified 2026-09-17 claude:w18-behavior --
// the "still-unidentified" note this line used to carry was stale, see the
// field comment in the header), installs the vtable (implicit, via C++
// construction), and finally zeroes/defaults the trailing own-field block
// (Flag1c5/Field1c8/Field1cc/Flag1b4/Flag1c4=1/Field1d0=0.9f/Field1b8/
// Field1bc/Field1c0/Flag1d4/Field1d8).
FaceBehavior::FaceBehavior(int InTypeId)
    : Behavior()
    , TypeId(InTypeId)
    , CachedHandle08(0)
    , ModeSelector0xc(0)
    , UpdateResult10(0)
    , ThreatSourcePtr14(nullptr)
    , Opaque18{}
    , DeltaTime1c(0.0f)
    , BehaviorMgrPtr20(nullptr)
    , NavHelperPtr24(nullptr)
    , Flags28(0)
    , Flag29(0)
    , Pad2a{}
    , TargetKind2c(0)
    , Watcher30()
    , OrientMatrix38()
    , TargetPosition5c{}
    , SubObjectA()
    , SubObjectB()
    , SubObjectC()
    , Flag1b4(0)
    , Pad1b5{}
    , Field1b8(0.0f)
    , Field1bc(0.0f)
    , Field1c0(0.0f)
    , Flag1c4(1)
    , Flag1c5(0)
    , Pad1c6{}
    , Field1c8(0.0f)
    , Field1cc(0.0f)
    , Field1d0(0.9f)
    , Flag1d4(0)
    , Pad1d5{}
    , Field1d8(0.0f)
{
    ConstructBase(InTypeId);   // FUN_062d2020 -- Behavior's own field initialisation
    // ALL THREE sub-objects now construct for real via the member-initializer
    // list above, with no Construct* stand-in left:
    //   .text:062c891f  LEA ECX,[ESI+0x68];    CALL FUN_062d4e60  -> MoveIdea
    //   .text:062c892f  LEA ECX,[ESI+0xe0];    CALL FUN_062d6750  -> FaceIdea
    //   .text:062c893f  LEA ECX,[ESI+0x1a0];   CALL FUN_062da500  -> AfterburnerIdea
    // The third was the long-standing "still-unidentified sub-object" gap;
    // closed 2026-09-17 (claude:w18-behavior) -- see the SubObjectC field
    // comment in the header. Note retail's ctor does NOT write +0x1b4 as part
    // of that call: FaceBehavior's own code stores Flag1b4 separately at
    // .text:062c896f, which is the independent confirmation that
    // AfterburnerIdea's 0x14 bytes end exactly where Flag1b4 begins.

    // Retail's dead-code `CMP [this+0x30],0; JZ` guard right after vtable
    // install (always taken, Watcher30 was just zeroed above) needs no
    // reproduction -- same convention already documented on LootBehavior's
    // own ctor.
}

// FUN_062c8a30 (Common.dll.txt 195815-195860) -- the real (non-thunk)
// destructor; FUN_062c8a10 (this class's vtable slot 0) is MSVC's scalar-
// deleting-destructor boilerplate that just calls this then conditionally
// frees the block.
FaceBehavior::~FaceBehavior()
{
    // Retail clears three BaseWatcher-shaped pointer fields via
    // BaseWatcher::set_pointer(nullptr), in this order: this+0xf4, this+0x7c,
    // this+0x30. WIRED FOR REAL 2026-09-11 (d285:common-port): +0xf4/+0x7c
    // are EXACTLY SubObjectB::Watcher/SubObjectA::Watcher (see header
    // comment) -- independent corroboration of the MoveIdea/FaceIdea
    // identification. this+0x30 (Watcher30) was already modeled for real.
    SubObjectB.Watcher.set(nullptr);
    SubObjectA.Watcher.set(nullptr);
    Watcher30.set(nullptr);

    // Retail then calls FUN_062d2080 (~Behavior, .text:062c8aa2): the implicit base
    // destructor call after this body.
}

// Own vtable slot 1 (offset 0x04). Common.dll.txt 195870-195913
// (LAB_062c8ac0).
bool FaceBehavior::SetContext(void* Param1, void* Param2, void* Param3)
{
    // .text:062c8ad5: `CALL FUN_062d2090(Param1, Param2, Param3)` -- Behavior::BindContext.
    // It writes +0x14 (ThreatSourcePtr14), +0x08, +0x24 (NavHelperPtr24) and +0x20
    // (BehaviorMgrPtr20); on false the function returns the binder's AL.
    if (!BindContext(Param1, Param2, Param3))
        return false;

    // WIRED FOR REAL (2026-09-11, d285:common-port): retail calls SubObjectB's
    // own vtable slot 0 here -- re-read directly from Common.dll.txt
    // 195886-195892 byte-for-byte: args are `(Param1, Param3, Param2)`, same
    // order every sibling's own SetContext uses (see e.g. DockBehavior::
    // SetContext). NOTE: unlike DockBehavior's SetContext, FaceBehavior's
    // retail body does NOT also call SubObjectA's vtable slot 0 (re-confirmed
    // this pass -- see SubObjectA's own field comment).
    SubObjectB.SetContext(Param1, Param3, Param2);

    // WIRED FOR REAL 2026-09-17 (claude:w18-behavior). .text:062c8af9-062c8b08 is
    // byte-for-byte the same three-push shape as SubObjectB's call twelve
    // instructions earlier (`PUSH EBP; PUSH EDI; PUSH EAX; CALL [EDX]`, last push
    // first => (Param1, Param3, Param2)), dispatched on this+0x1a0's slot 0. The
    // deferral reason recorded here -- "SubObjectC's real class is still
    // unidentified" -- was stale; it is an AfterburnerIdea, and slot 0 is
    // BaseIdea::SetContext, which AfterburnerIdea does not override.
    SubObjectC.SetContext(Param1, Param3, Param2);

    // PORTED 2026-09-17 (claude:w18-behavior). The recorded reason, "FUN_062e5b20
    // not yet ported", was stale: it is item 1763, ported as
    // Behavior_InvokeManagerSlotF0 in common_behavior.h, which even names
    // .text:062c8b15 -- THIS call site -- as one of the two sites still wanting it.
    // .text:062c8b0d-062c8b1a:
    //     PUSH 0x3f800000            ; 1.0f, pushed FIRST => LAST argument
    //     PUSH 0x0                   ; 0.0f
    //     PUSH ECX                   ; ThreatSourcePtr14, pushed LAST => FIRST argument
    //     CALL FUN_062e5b20 / FSTP float ptr [ESI+0x1cc] / ADD ESP,0xc
    Field1cc = Behavior_InvokeManagerSlotF0(ObjInspectOf(ThreatSourcePtr14), 0.0f, 1.0f);

    return true;
}

// Own vtable slot 3 (offset 0x0c). Common.dll.txt 196670-196695
// (FUN_062c9320).
void FaceBehavior::Slot3()
{
    Behavior::Slot3();                                              // 062c9324 FUN_062d20d0

    Flag1c5 = 0;                                                    // 062c9330
    Field1c8 = 0.0f;                                                // 062c9336 (dword 0)

    SetCruiseLock(0, 3);                                            // 062c933c FUN_062d21d0

    // FUN_062e91d0 on NavHelperPtr24 + 0x18 (SystemManager::Thrust), Flag1c4 == 0.
    NavHelperPtr24->Thrust.SetEngineActive(Flag1c4 == 0);

    Flag1b4 = 0;
    Field1b8 = 0.0f;
    Field1bc = 0.0f;
    Field1c0 = 0.0f;
    Field1d8 = 0.0f;
}

// Own vtable slot 4 (offset 0x10). Common.dll.txt 196706-196727
// (FUN_062c9380).
void FaceBehavior::Slot4()
{
    if ((Flags28 & 0x2) == 0 && Watcher30.target() != nullptr)
    {
        Watcher30.set(nullptr);
    }

    // WIRED FOR REAL (2026-09-11, d285:common-port): `SubObjectB->vtbl[0x10]()`
    // -- own vtable slot 4 (Commit()), now that SubObjectB is identified as FaceIdea.
    SubObjectB.Commit();

    // WIRED FOR REAL 2026-09-17 (claude:w18-behavior): .text:062c93af-062c93b5
    // `LEA ECX,[ESI+0x1a0]; CALL [EDX+0x10]` -- slot index 4 == Commit(), on
    // the AfterburnerIdea now identified at +0x1a0. Its body is a single
    // Behavior_ConditionalNotify(Handle + 0x18, 0), i.e. "afterburner off",
    // which is exactly what a Disengage() should do here.
    SubObjectC.Commit();

    Behavior::Slot4();                                              // 062c93bb JMP FUN_062d20f0
}

// Own vtable slot 5 (offset 0x14). Common.dll.txt 196016-196152
// (LAB_062c8c10).
int FaceBehavior::Slot5(float Unused)
{
    (void)Unused;
    // Fast path, 062c8c17-062c8c5e: Flags28 bit 0x2 set, TargetKind2c == 1 and Watcher30
    // watching (`Ptr && Ptr - 8`) -> Flag29 ? 3 : 1.
    if ((Flags28 & 0x2) != 0 && TargetKind2c == 1 && GetWatchedRawBlock() != nullptr)
    {
        int const Mode = Flag29 ? 3 : 1;
        ModeSelector0xc = Mode;
        return Mode;
    }

    // LAB_062c8c61. EBX = 2 is the pending result.
    int Result = 2;
    SystemManager* const Sys = NavHelperPtr24;
    if (!Sys->GunConvergenceStateBytes.FollowerDirectiveGateA())     // 062c8c6a FUN_062ecb20
    {
        // 062c8c77-062c8c93: IBehaviorManager+0xc (PathfindManager); a cached obstacle with
        // its flag set skips the scan. Retail also copies CachedObstacleId into a local that
        // nothing reads.
        PathfindManager* const Pathfind = BehaviorMgrPtr20->Pathfind;
        if (!(Pathfind->CachedObstacleId != 0 && Pathfind->bHasCachedObstacle))
        {
            // 062c8c99-062c8cc1: the closest hostile within EngineKillMaxTargetDistance (Sys+0x16e4).
            IObjInspect* const Closest = FindClosestHostileTarget(
                &Sys->Timers, Sys->PersonalityData.EngineKillParams.EngineKillMaxTargetDistance);
            Watcher30.set(static_cast<Watchable const*>(static_cast<IObjRW*>(Closest)));

            IObjRW* const Watched = GetWatchedRawBlock();
            if (Watched != nullptr)                                 // 062c8cc6-062c8cd5
            {
                // 062c8cd7-062c8cf9: get_type (+0x88); only ship-class objects (0x30000).
                uint Type;
                Watched->get_type(Type);
                if ((Type & 0x30000) != 0)
                {
                    // 062c8cfb-062c8d3c: ExternalIface->get_velocity() (+0x4, hidden return);
                    // (z*z + y*y) + x*x FCOMP 2500.0f (DAT_0639f9c8); `TEST AH,0x41; JNZ`
                    // skips when not greater.
                    Vector const V = Sys->SelfTarget.Iface->get_velocity();
                    double const SpeedSq = (static_cast<double>(V.z) * V.z + static_cast<double>(V.y) * V.y) +
                                           static_cast<double>(V.x) * V.x;
                    if (SpeedSq > 2500.0)
                    {
                        // 062c8d3e-062c8d4e: `NEG AL; SBB EAX,EAX; INC EAX` -> 0 when
                        // FUN_062e8830(Timers) is set, else 1.
                        Result = Sys->Timers.IsTimer3Active() ? 0 : 1;   // FUN_062e8830
                    }
                }
            }
        }
    }

    // LAB_062c8d51.
    if (Flag29)
        Result = 3;
    ModeSelector0xc = Result;
    return Result;
}

// Own vtable slot 6 (offset 0x18), LAB_062c8d80 (.text:062c8d80-062c8dcf, RET 4).
EvaluateResult FaceBehavior::evaluate(unsigned int Context)
{
    (void)Context;
    if (!PlayerTrailAvailable)                                      // 062c8d81
        return EvalSkip;                                            // 062c8dc8, 2

    // 062c8d8d-062c8db9: the owner's own target (get_target +0x74 into a zeroed local)
    // handed to the ship's TargetManager with SUBOBJ_ID_NONE (zero-extended).
    IObjRW* Target = nullptr;
    static_cast<IObjRW*>(ThreatSourcePtr14)->get_target(Target);
    NavHelperPtr24->Targeting.RetargetOwner(Target, SUBOBJ_ID_NONE);
    return EvalBlocked;                                             // 062c8dbe, 1
}

// ----------------------------------------------------------------------------
// Retail inlines this six-instruction block TWICE inside FaceBehavior::Update
// (.text:062c91c0-062c91e7 and .text:062c9223-062c924a, byte-identical). It is
// the same once-per-tick refresh FUN_062e6d40 (item 1794) performs on
// SystemManager's own cached physical extent, and is named after it rather than
// duplicated twice inline:
//     MOV  ESI,[EBP+0x24]            ; Sys
//     MOV  AL,[ESI+0x157a]           ; Flags1578[2], the "cache valid" latch
//     ADD  ESI,0x14fc
//     TEST AL,AL / JNZ               ; skip when already fresh
//     MOV  ECX,[ESI+0x84]            ; == Sys->ExternalIface (0x14fc+0x84 == 0x1580)
//     LEA  EAX,[ESI+0xc]  / PUSH EAX ; == &CachedPhysicalCenter (0x1508)
//     LEA  EAX,[ESI+0x18] / PUSH EAX ; == &CachedPhysicalRadius (0x1514)
//     MOV  byte ptr [ESI+0x7e],1     ; latch set BEFORE the call, as here
//     CALL [EDX+0x2c]                ; IObjInspect::get_physical_radius
// The last push is the first argument, which is why Radius precedes Center --
// matching common_iface.h's `get_physical_radius(float&, Vector&)` exactly.
static void EnsurePhysicalCacheFresh(SystemManager* Sys)
{
    if (Sys->SelfTarget.bPosValid != 0)
        return;
    Sys->SelfTarget.bPosValid = 1;
    Sys->SelfTarget.Iface->get_physical_radius(Sys->SelfTarget.CachedRadius,
                                                         Sys->SelfTarget.CachedPos);
}

// ----------------------------------------------------------------------------
// FaceBehavior::Update -- own vtable slot 7 (offset 0x1c).
// LAB_062c8de0, .text:062c8de0-062c9307, Common.dll.txt 196208-196656,
// 410 instructions.
//
// PORTED IN FULL 2026-09-17 (claude:w18-behavior). ADDRESS RANGE VERIFIED BY
// THIS PASS: 062c8de0-062c931b inclusive -- i.e. every instruction of the
// function plus its four-entry jump table at 062c9308. Nothing here is
// inherited from the prior note; each branch below cites the address that
// proves it.
//
// THE `simplified` NOTE THIS REPLACES WAS STALE ON EVERY SUBSTANTIVE POINT.
// It claimed the body was unportable because of an unidentified sub-object and
// six unported helpers. Checked one by one against the current tree:
//   * "a NavHelperPtr24-nested +0x14fc/+0x84 sub-object whose class is
//     unidentified" -- there is no nested sub-object and no double
//     indirection. `.text:062c8e0a ADD EAX,0x14fc; MOV EAX,[EAX+0x84]` is ONE
//     load of +0x1580, and +0x1580 is SystemManager::ExternalIface
//     (common_systemmanager.h). The three vtable slots called on it (+0x0c,
//     +0x2c, +0xbc) are IObjInspect::get_transform / get_physical_radius /
//     is_player in common_iface.h's RTTI-verified 85-slot table.
//   * FUN_062e1aa0 -- ported, BearingAngleToIface (item 1731).
//   * FUN_062e4bb0 -- ported, GetInterceptClearanceOfIface (item 1749).
//   * FUN_062e3460 -- ported, BuildPerpendicularFrame (item 1741).
//   * FUN_062e1f40 -- ported, BuildOrientationBasis.
//   * FUN_062e92e0 -- ported, CheckSystemManagerSteeringGate (item 1827).
//   * FUN_062d7510/7590/75c0/75e0 -- ported, FaceIdea's own non-virtual
//     setters SetWatchObject/SetDirection/SetBasis/SetAxis (items 1606-1610).
//   * FUN_062e91d0 -- ported, Behavior_NotifyRegistrationFlag (item 1825),
//     and this very file already called it from ResetApproachState().
// Six of those eight were already ported when the note was written; the other
// two landed earlier the same week. The last hold-out, SubObjectC, is not a
// gap either: FUN_062da500 is RTTI-named ".?AVAfterburnerIdea@@" in
// common_idea.h, which explicitly calls out "this marathon's SubObjectC" as
// its call site, and AfterburnerIdea::Update/::Commit are complete ports. So
// NOTHING in this function is deferred -- every retail call is a real call.
//
// WHAT THIS FUNCTION IS. The five floats it reads off NavHelperPtr24 at
// +0x16d4/+0x16d8/+0x16dc/+0x16e0/+0x16e4 are the five consecutive fields of
// pub::AI::Personality::EngineKillUseStruct, which sits at
// Personality+0x150 == SystemManager+0x1584+0x150 == absolute 0x16d4 -- an
// exact five-for-five fit against common_pub.h's already-static_asserted
// layout. So FaceBehavior is the ENGINE-KILL manoeuvre: cut the engines, coast,
// and rotate to face a target. That reading is self-consistent with every gate
// below (Field_08 as the "engine kill enabled" switch, Field_0C/Field_00/
// Field_04 as dwell timeouts on the two second-accumulators Field1d8/Field1c8,
// and Field_10 == 700.0 by default as a clearance distance compared against
// GetInterceptClearanceOfIface's metres).
//
// .rdata CONSTANTS, DECODED FROM RAW BYTES (never from the .c):
//   DAT_0639e2d8  Common.dll.txt:561291-561297 + the 0x3f at 0639e2df:
//                 00 00 00 60 fb 21 f9 3f == double 0x3FF921FB60000000.
//                 That is SINGLE-precision pi/2 (0x3FC90FDB) WIDENED to
//                 double, not the double nearest pi/2 (0x3FF921FB54442D18) --
//                 it overshoots by ~4.4e-08. Same trap as DAT_0639e2d0 in
//                 LaunchBehavior::Update; written out in full below.
//   DAT_0639f9d0  Common.dll.txt:562804-562811: 00 00 00 60 fb 21 c9 3f
//                 == double 0x3FC921FB60000000 == that same widened float
//                 constant divided by 8, i.e. single-precision pi/16.
//   DAT_06399408  0.0f (already decoded and cited by SetTargetOffset above).
//   0x3f7fbe77    an inline float immediate, not a .rdata symbol:
//                 0.998969435691833496 (== cos(2.603 degrees)).
int FaceBehavior::Update()
{
    // ---- Entry guard, 062c8de0-062c8e09. Abort-request flag, consumed and
    // ---- cleared here. Unchanged from the prior pass, re-verified.
    if (Flag1d4 != 0)
    {
        Flag1d4 = 0;
        UpdateResult10 = 2;
        return UpdateResult10;
    }

    SystemManager* const Sys = NavHelperPtr24;
    pub::AI::Personality::EngineKillUseStruct const& EngineKill =
        Sys->PersonalityData.EngineKillParams;

    // DAT_0639e2d8 / DAT_0639f9d0, spelled as the exact doubles their bytes
    // encode (see the constant table above).
    double const kWideBearingGate = 1.5707963705062866;    // DAT_0639e2d8, float pi/2 widened
    double const kFaceLockGate    = 0.19634954631328583;   // DAT_0639f9d0, float pi/16 widened

    // `MOV EBX,0x3` at 062c8e1a then `MOV [ESP+0x10],EBX` at 062c8e21: the
    // pending result lives in one stack slot, preloaded with 3, and the tail at
    // 062c92f2 reads it back. EBX stays live as the literal 3 and is reused at
    // 062c8fe6 and 062c90af, so those two sites really do mean 3.
    int Result = 3;

    // 062c8e25: `CALL [EDX+0xbc]` on ExternalIface == IObjInspect::is_player().
    // A player-flown ship skips the whole re-validation block and goes straight
    // to the switch with Result still 3.
    if (!Sys->SelfTarget.Iface->is_player())
    {
        // ---- 062c8e35-062c8ea4: four gates, any of which diverts to the
        // ---- re-validation block at LAB_062c8f0a. Written as a single
        // ---- short-circuit chain in retail's own evaluation order.
        void* const WatchedBlock = GetWatchedRawBlock();

        bool bStillTurning = false;
        if (Watcher30.target() != nullptr && WatchedBlock != nullptr)
        {
            // 062c8e51-062c8e62: `FLD [ECX+0x16dc]; FCOMP 0.0f; TEST AH,0x44;
            // JNP`. Mask 0x44 (C3|C2) with JNP is MSVC's EQUALITY idiom (equal
            // sets C3 alone -> one bit -> odd parity -> PF=0 -> JNP taken), so
            // the jump AWAY is taken when the field IS zero. Continue only when
            // the engine-kill enable field is non-zero.
            if (EngineKill.EngineKillUseAfterburner != 0.0f)
            {
                // 062c8e68-062c8e79: `FLD [EBP+0x1d8]; FCOMP [ECX+0x16e0];
                // TEST AH,0x5; JP`. Mask 0x05 (C0|C2): strictly-less sets C0
                // alone -> odd parity -> PF=0, so JNP would be "<" and the JP
                // actually coded here is ">=". Bail out once the turn has been
                // running longer than the personality allows.
                if (Field1d8 < EngineKill.EngineKillAfterburnerTime)
                {
                    // 062c8e8c-062c8ea4. NOTE THE ARGUMENT ORDER, which is the
                    // OPPOSITE of the second BearingAngleToIface call at
                    // 062c8f8d below: here `PUSH EAX(block)` comes first and
                    // `PUSH [EBP+0x14]` second, so the LAST push -- and hence
                    // the FIRST argument, Self -- is ThreatSourcePtr14, and the
                    // watched block is Target. This measures how far the threat
                    // source is off the watched object's bearing.
                    double const Bearing = BearingAngleToIface(ThreatSourcePtr14, WatchedBlock); // unrounded ST0

                    // `TEST AH,0x41; JNZ`: C0|C3 is set for less-or-equal, so
                    // the JNZ is the else-branch and the FALL-THROUGH taken
                    // here is STRICTLY GREATER. A wide bearing means the ship
                    // has not finished rotating yet.
                    bStillTurning = (Bearing > kWideBearingGate);
                }
            }
        }

        if (bStillTurning)
        {
            // ---- 062c8ea6-062c8f09: keep coasting and keep steering. Returns
            // ---- 0 directly, never reaching the switch.
            // 062c8ea6-062c8ec5: Field1d8 += DeltaTime1c, then SubObjectC's own
            // vtable slot 1 with the same delta.
            Field1d8 += DeltaTime1c;
            // Slot offset +0x04 is index 1 == BaseIdea::Update(float).
            // AfterburnerIdea's own override is "hold the afterburner on".
            SubObjectC.Update(DeltaTime1c);

            // 062c8ec8-062c8efa. The three `MOV dword ptr [ESP+0x18/0x1c/0x20],0`
            // stores land exactly on the buffer whose address was pushed one
            // instruction earlier (ESP had already dropped 4 by the PUSH), so
            // the argument really is a zero Vector -- FaceIdea tag 5, "face an
            // axis", with the axis cleared.
            Vector ZeroAxis;
            Vec3Assign(&ZeroAxis, 0.0f, 0.0f, 0.0f);
            SubObjectB.SetAxis(ZeroAxis);
            SubObjectB.Update(DeltaTime1c);

            UpdateResult10 = 0;
            return 0;
        }

        // ---- LAB_062c8f0a, 062c8f0a-062c9036: re-validate the watch.
        // 062c8f16: slot offset +0x10 is index 4 == Commit(), zero arguments --
        // "afterburner off", the counterpart of the slot-1 call above.
        SubObjectC.Commit();

        if (Flag1c5 != 0)
        {
            // 062c8f23-062c8f37: `TEST AH,0x5; JNP` -- strictly-less jumps to
            // LAB_062c8fd1, so the accumulator keeps running while it is under
            // EngineKillParams.EngineKillFaceTime.
            if (Field1c8 < EngineKill.EngineKillFaceTime)
            {
                // LAB_062c8fd1, 062c8fd1-062c8fe4.
                Result = 0;
                Field1c8 += DeltaTime1c;
            }
            else
            {
                // 062c8f3d-062c8f53. Give up on this watch entirely.
                Field1c8 = 0.0f;
                if (Watcher30.target() != nullptr)
                    Watcher30.set(nullptr);
                Result = 2;   // LAB_062c9030
            }
        }
        else if (Watcher30.target() != nullptr && GetWatchedRawBlock() != nullptr)
        {
            // 062c8f58-062c8f6d. Retail RE-READS [ESI] here (`MOV EAX,[ESI]`)
            // rather than reusing the value it loaded at 062c8e35, and this
            // point is reached only after the SubObjectC.Commit() above, so the
            // re-read is reproduced rather than hoisted.
            void* const FreshBlock = GetWatchedRawBlock();
            TargetKind2c = 1;

            // 062c8f74-062c8fa8. Retail expands the "watcher to raw block"
            // idiom three more times here and branches on each copy; every one
            // of those branches is DEAD given the two guards just passed. In
            // particular LAB_062c8faa's `FLD float ptr [ESP+0x10]` would
            // reinterpret the pending integer Result (3) as a denormal float
            // -- unreachable, and not reproduced. What survives is one call.
            //
            // ARGUMENT ORDER IS REVERSED relative to 062c8e8c: here
            // `PUSH ECX(ThreatSourcePtr14)` comes first and `PUSH EAX(block)`
            // second, so Self is the WATCHED BLOCK and Target is
            // ThreatSourcePtr14. Two calls to the same helper, opposite
            // operands, twelve instructions apart.
            double const Angle = BearingAngleToIface(FreshBlock, ThreatSourcePtr14); // unrounded ST0

            // 062c8fae-062c8fb9: `TEST AH,0x41; JNZ LAB_062c8fec`. JNZ fires on
            // less-or-equal, so a SMALL angle (inside pi/16) is the "locked on"
            // case and the fall-through below is the still-too-wide case.
            if (Angle > kFaceLockGate)
            {
                // 062c8fbb-062c8fcf: `TEST AH,0x5; JP LAB_062c8fe6` -- JP is
                // ">= or unordered", so anything but strictly-less ends the attempt.
                if (!(Field1c8 < EngineKill.EngineKillSearchTime))
                {
                    Result = 3;   // LAB_062c8fe6, `MOV [ESP+0x10],EBX`
                }
                else
                {
                    // Falls into LAB_062c8fd1, the same two stores as above.
                    Result = 0;
                    Field1c8 += DeltaTime1c;
                }
            }
            else
            {
                // ---- LAB_062c8fec, 062c8fec-062c902e: locked on.
                Flag1c5 = 1;
                Field1c8 = 0.0f;
                // `MOV dword ptr [ESP+0x10],0x1` at 062c8ffd is unconditional,
                // ahead of the JZ, so Result is 1 on both arms below.
                Result = 1;

                // 062c900f-062c9018: `PUSH block` then `PUSH NavHelper+0x14fc`,
                // so the FIRST argument is Sys+0x14fc. That address is a real
                // "nav element" view of this ship: GetInterceptClearanceOfIface
                // reads +0x0c/+0x18/+0x7e/+0x84 off it, which land exactly on
                // SystemManager::CachedPhysicalCenter (0x1508),
                // CachedPhysicalRadius (0x1514), Flags1578[2] (0x157a) and
                // ExternalIface (0x1580). Four-for-four.
                // 062c9020 compares the UNROUNDED ST0 return (FCOMP f[ECX+0x16e4]),
                // so the clearance stays double here.
                double const Clearance = GetInterceptClearanceOfIface(
                    &Sys->SelfTarget, GetWatchedRawBlock());

                // 062c9020-062c902e: `TEST AH,0x41; JNZ` -- fall-through is
                // strictly greater, which is LAB_062c9030's `Result = 2`
                // (unordered sets C0|C3 and skips, matching `>` on NaN).
                if (Clearance > EngineKill.EngineKillMaxTargetDistance)
                    Result = 2;
            }
        }
        // else: both guards failed at 062c8f5c/062c8f67 -- straight to the
        // switch with Result still 3.
    }

    // ---- LAB_062c9038, 062c9038-062c9044: switch on TargetKind2c - 1, with
    // ---- `CMP EAX,EBX(3); JA` sending anything outside 1..4 to the shared
    // ---- tail. Jump table at 062c9308: caseD_1/caseD_2/caseD_3/caseD_4 in
    // ---- index order 0,1,2,3, so the cases below are TargetKind2c 1,2,3,4.
    switch (TargetKind2c)
    {
    case 1:
    {
        // caseD_1, 062c904b-062c90b9.
        if (Flag1b4 != 0)
        {
            // 062c9055-062c9077. Retail really does call get_transform() and
            // DISCARD the returned Transform here -- no FLD follows, unlike
            // case 4 below. Reproduced for its (absent) side effects rather
            // than optimised away, so the call count matches retail.
            (void)Sys->SelfTarget.Iface->get_transform();

            // `LEA EAX,[EBP+0x1b8]` -- the three contiguous floats Field1b8/
            // Field1bc/Field1c0 read as one Vector. FaceIdea tag 5.
            Vector Axis;
            Vec3Assign(&Axis, Field1b8, Field1bc, Field1c0);
            SubObjectB.SetAxis(Axis);
        }
        else
        {
            // 062c9081-062c9096: FaceIdea tag 1, "watch an object".
            SubObjectB.SetWatchObject(GetWatchedRawBlock());

            // 062c909b-062c90b9: if the watch is empty, bail out with 3 without
            // running the shared tail at all -- this is the only case arm with
            // its own RET.
            if (GetWatchedRawBlock() == nullptr)
            {
                UpdateResult10 = 3;
                return 3;
            }
        }
        break;
    }
    case 2:
    {
        // caseD_2, 062c9112-062c92ad.
        if (Flag1b4 != 0)
        {
            // 062c9120-062c915c: build -MatrixColumn2(get_transform()). The
            // FLD/FXCH/FCHS/FSTP dance writes the three negated components to
            // one 12-byte stack Vector; offsets +8/+0x14/+0x20 into the 3x3 are
            // MatrixColumn2's own stride (common_csys.h).
            Vector const Col2 = MatrixColumn2(
                Sys->SelfTarget.Iface->get_transform().Rotation);
            Vector NegCol2;
            VecNegate3(&Col2, &NegCol2);

            // 062c9160-062c9187. STACK-DEPTH NOTE, because the operands are not
            // what they look like: `FLD ST0` duplicates x, then `FMUL ST1` is
            // x*x, `FLD ST2`/`FMUL ST3` is y*y and `FLD ST3`/`FMUL ST4` is z*z.
            // Every factor comes from the SAME vector -- this is |V|^2 of
            // (Field1b8, Field1bc, Field1c0), NOT a dot product against the
            // negated column just built. The three trailing `FSTP ST0`s drop
            // the x/y/z copies left on the stack.
            Vector Axis;
            Vec3Assign(&Axis, Field1b8, Field1bc, Field1c0);
            // (x*x + y*y) + z*z stays on the FPU (extended): double, not float.
            double const MagnitudeSquared = (static_cast<double>(Axis.x) * Axis.x
                                             + static_cast<double>(Axis.y) * Axis.y)
                                          + static_cast<double>(Axis.z) * Axis.z;

            // `TEST AH,0x41; JNZ LAB_062c919c` -- JNZ on <= 0 or unordered, so the
            // fall-through (tag 5, the explicit axis) is the strictly > 0 case.
            if (MagnitudeSquared > 0.0)
                SubObjectB.SetAxis(Axis);
            else
                SubObjectB.SetDirection(NegCol2);   // FaceIdea tag 3
        }
        else
        {
            // ---- LAB_062c91b1, 062c91b1-062c92a9.
            // 062c91b1-062c91be: `MOV ECX,[EBP+0x24]; ADD ECX,0x18` is
            // SystemManager::Thrust, i.e. ThrustManager::CheckSteeringGate.
            bool bFrameBuilt = false;
            if (!Sys->Thrust.CheckSteeringGate())
            {
                EnsurePhysicalCacheFresh(Sys);

                // 062c91ea-062c9213. TargetPosition5c (+0x5c/+0x60/+0x64) minus
                // CachedPhysicalCenter (Sys+0x14fc +0xc/+0x10/+0x14 == 0x1508).
                Vector Diff;
                Vec3Assign(&Diff,
                           TargetPosition5c.x - Sys->SelfTarget.CachedPos.x,
                           TargetPosition5c.y - Sys->SelfTarget.CachedPos.y,
                           TargetPosition5c.z - Sys->SelfTarget.CachedPos.z);

                // 062c91f1-062c9217: four pushes, cleaned with `ADD ESP,0x10`.
                // Last push first => (Direction, OutFrame, DotThreshold,
                // Reference) == (&Diff, &OrientMatrix38, 0x3f7fbe77, null).
                bFrameBuilt = BuildPerpendicularFrame(Diff, &OrientMatrix38, 0.999f /* 0x3f7fbe77 */, nullptr);
            }

            if (!bFrameBuilt)
            {
                // ---- LAB_062c9223, 062c9223-062c92a0: retail recomputes the
                // ---- cache check and the difference vector from scratch here
                // ---- rather than reusing the ones above.
                EnsurePhysicalCacheFresh(Sys);

                Vector Diff;
                Vec3Assign(&Diff,
                           TargetPosition5c.x - Sys->SelfTarget.CachedPos.x,
                           TargetPosition5c.y - Sys->SelfTarget.CachedPos.y,
                           TargetPosition5c.z - Sys->SelfTarget.CachedPos.z);

                // 062c9279-062c92a0. Four pushes again, `ADD ESP,0x10`; last
                // push first => (Out, Reference, Forward, UpHint) with
                // Reference being get_transform()'s own Rotation and UpHint
                // null. Retail then `MOV ESI,EAX; REP MOVSD` nine dwords into
                // OrientMatrix38 -- and FUN_062e1f40's tail (062e2570-062e258c)
                // reloads param_1 into EAX before returning, so EAX IS the out
                // buffer and copying from our own local is equivalent.
                Matrix Frame;
                BuildOrientationBasis(Frame,
                                      Sys->SelfTarget.Iface->get_transform().Rotation,
                                      Diff, nullptr);
                OrientMatrix38 = Frame;
            }

            // LAB_062c92a2/062c92a3: FaceIdea tag 4, "face along a basis".
            SubObjectB.SetBasis(OrientMatrix38);
        }
        break;
    }
    case 3:
        // caseD_3, 062c90ba-062c90be, which jumps straight into LAB_062c92a3.
        SubObjectB.SetBasis(OrientMatrix38);
        break;
    case 4:
    {
        // caseD_4 (the JUMP-TABLE one at 062c90c3, not the shared tail that
        // Ghidra gives the same name), 062c90c3-062c910d.
        Vector const Col2 = MatrixColumn2(
            Sys->SelfTarget.Iface->get_transform().Rotation);
        Vector NegCol2;
        VecNegate3(&Col2, &NegCol2);
        SubObjectB.SetDirection(NegCol2);   // FaceIdea tag 3
        break;
    }
    default:
        // The `JA` at 062c903e, taken for TargetKind2c outside 1..4 (including
        // 0, since the index is TargetKind2c - 1 and the compare is unsigned).
        break;
    }

    // ---- Shared tail, LAB_062c92ae at 062c92ae-062c9307.
    // 062c92ae-062c92c0: `SETZ CL` off Flag1c4, pushed as the bool argument,
    // with the receiver being SystemManager::Thrust again.
    Sys->Thrust.SetEngineActive(Flag1c4 == 0);

    // 062c92c5-062c92d5: SubObjectB's own vtable slot 1.
    SubObjectB.Update(DeltaTime1c);

    // 062c92d8-062c92f8: is_player() a second time. `MOV EAX,[ESP+0x10]` loads
    // the pending Result and the SETZ-free `MOV EAX,1` overrides it only when
    // the ship is player-flown.
    if (Sys->SelfTarget.Iface->is_player())
        Result = 1;

    UpdateResult10 = Result;
    return Result;
}

// Own vtable slot 10 (offset 0x28). Common.dll.txt 195918-196015
// (LAB_062c8b30).
bool FaceBehavior::Slot10(void* Message)
{
    // Every failure path is the shared tail at 062c8bf2 (the JZ at 062c8b3a, the JA at
    // 062c8b53 and both JZs at 062c8b8d/062c8b94 all encode rel32/rel8 to 062c8bf2).
    if (Message == nullptr)
    {
        Flags28 = static_cast<unsigned char>(Flags28 & ~0x2);
        return false;
    }

    char* M = static_cast<char*>(Message);
    Flag1c4 = static_cast<unsigned char>(M[0x44]);                  // 062c8b40
    int const Kind = *reinterpret_cast<int const*>(M + 0xc);
    TargetKind2c = Kind;                                            // 062c8b4c

    switch (Kind)
    {
    case 1:
    {
        // 062c8b60-062c8b84: FUN_062e74a0(Sys+0x34, Message+0x10), Watcher30 = T ? T+8 : 0
        // (CMP/JZ dedup); then the watch must be live (`Ptr && Ptr - 8`).
        void* const T = NavHelperPtr24->Timers.FindVerifiedObject((unsigned const*)(M + 0x10));
        Watcher30.set(T != nullptr ? reinterpret_cast<Watchable const*>(static_cast<char*>(T) + 8)
                                   : nullptr);
        if (GetWatchedRawBlock() == nullptr)
        {
            Flags28 = static_cast<unsigned char>(Flags28 & ~0x2);
            return false;
        }
        Flags28 = static_cast<unsigned char>(Flags28 | 0x2);        // 062c8b96
        return true;
    }
    case 2:
    {
        Vector const& Src = *reinterpret_cast<Vector const*>(M + 0x14);
        TargetPosition5c = Src;
        Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
        return true;
    }
    case 3:
    {
        OrientMatrix38 = *reinterpret_cast<Matrix const*>(M + 0x20);
        Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
        return true;
    }
    case 4:
        Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
        TargetKind2c = 4;
        return true;
    default:
        Flags28 = static_cast<unsigned char>(Flags28 & ~0x2);
        return false;
    }
}

// Own vtable slot 12 (offset 0x30). Common.dll.txt 196731-196841
// (FUN_062c93c0).
void FaceBehavior::set_direction(Vector const& Offset)
{
    // FULLY PORTED 2026-09-17 (claude:common-behavior-fields). The two
    // GAP:AI-FACE-STATE-MACHINE markers that used to sit in this function are
    // gone, not softened -- see the declaration comment in common_facebehavior.h
    // for why each premise was false. Retail constants, read as raw .rdata
    // bytes (ground truth, not the .c pseudo-decompile):
    //     DAT_06399408 = 00 00 00 00                  -> 0.0f
    //     DAT_06399404 = 00 00 80 bf                  -> -1.0f
    //     DAT_0639a1d0 = 00 00 80 3f                  -> +1.0f
    //     DAT_0639c430 = 00 00 00 00 00 00 00 00      -> 0.0 (double)

    // .text:062c93c3-062c93fd: TargetKind2c==2 goes straight to the set path;
    // TargetKind2c==1 goes there only when Dot(Offset,Offset) > 0.0f; anything
    // else falls through to the clear path.
    if (TargetKind2c != 2)
    {
        float const MagnitudeSquared = Offset.x * Offset.x + Offset.y * Offset.y + Offset.z * Offset.z;
        if (TargetKind2c != 1 || !(MagnitudeSquared > 0.0f))
        {
            Flag1b4 = 0;
            return;
        }
    }

    Flag1b4 = 1;
    Field1b8 = -Offset.y;
    Field1bc = -Offset.x;
    Field1c0 = Offset.z;

    // .text:062c9429-062c9464. `MOV ECX,[ESI+0x20]; CALL FUN_062d4b20` ==
    // BehaviorMgrPtr20->get_turn_sensitivity_factor(). The FCOM against the
    // all-zero double at DAT_0639c430 plus `TEST AH,0x41; JNZ` skips the rescale
    // unless the factor is STRICTLY greater than zero (JNZ fires on C0|C3, i.e.
    // less-or-equal); the FDIVR then leaves 1.0f / Factor on the stack.
    // The reciprocal stays on the x87 stack (`FDIVR; FLD ST0; FMUL [1b8]; FSTP; FMUL [1bc];
    // FSTP`), so it is not rounded to float before the two products.
    float const TurnSensitivity = BehaviorMgrPtr20->get_turn_sensitivity_factor();
    if (TurnSensitivity > 0.0f)
    {
        double const Rescale = 1.0 / static_cast<double>(TurnSensitivity);
        Field1b8 = static_cast<float>(Rescale * Field1b8);
        Field1bc = static_cast<float>(Rescale * Field1bc);
    }

    // .text:062c9468-062c9523: each axis clamped into [-1.0f, +1.0f]. Retail
    // spells this out as three FCOMPs per axis (the third re-tests the lower
    // bound after the upper-bound branch falls through, a compiler artifact with
    // no observable effect), so a plain clamp reproduces it exactly. Note the
    // lower-bound test is <= rather than <: `TEST AH,0x41; JNZ` at 062c9476
    // takes the "use -1.0f" branch on equal as well as less.
    if (!(Field1b8 > -1.0f))
        Field1b8 = -1.0f;
    else if (!(Field1b8 < 1.0f))
        Field1b8 = 1.0f;

    if (!(Field1bc > -1.0f))
        Field1bc = -1.0f;
    else if (!(Field1bc < 1.0f))
        Field1bc = 1.0f;
}

// Own vtable slot 13 (offset 0x34). Common.dll.txt 196853-196855
// (LAB_062c9530).
void FaceBehavior::set_throttle(float Throttle)
{
    (void)Throttle;
    Flag1d4 = 1;
}

// Own vtable slot 21 (offset 0x54). Common.dll.txt 195748-195776
// (LAB_062c89c0, RET 4): drop the watch when the watched object's get_id (+0x20) equals
// the argument.
void FaceBehavior::Slot21(void* Arg)
{
    IObjRW* const Watched = GetWatchedRawBlock();
    if (Watched == nullptr)                                         // 062c89c3-062c89d0
        return;
    if (Watched->get_id() != static_cast<unsigned int>(reinterpret_cast<uintptr_t>(Arg)))
        return;                                                     // 062c89e4
    Watcher30.set(nullptr);                                         // 062c89ea-062c89f3
}

// Own vtable slot 24 (offset 0x60). Common.dll.txt 195781-195789 (LAB_062c8a00).
IObjRW const* FaceBehavior::get_target()
{
    return GetWatchedRawBlock();
}

IObjRW* FaceBehavior::GetWatchedRawBlock() const
{
    return ObjRwFromWatchable(const_cast<Watchable*>(Watcher30.target()));
}
