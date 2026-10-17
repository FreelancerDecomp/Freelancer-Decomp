// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- FormationBehavior (behavior type 10, "Formation").
// CMN-SPLIT stage 1 (2026-10-14): split out of common_behavior.cpp unchanged; the
// FollowerDirectiveRecord_* helpers it calls stay in common_behavior.cpp
// (common_behavior_internal.h).

#include "common_behavior.h"
#include "common_behavior_internal.h" // CmnBehavior:: follower helpers (common_behavior.cpp)
#include "common_aisteer.h"
#include "common_systemmanager.h" // SystemManager -- NavHelper's real type
#include "common_misc.h" // SinglePlayer() -- evaluate()'s multiplayer group test
#include "common_csys.h"  // Matrix, Transform, MatrixSetIdentity, Acquire3DMathEngine (Slot27/Slot28)
#include "common_iface.h" // IObjInspect / IObjRW / ObjInspectOf / ObjRwFromWatchable
#include "x86math.h"      // I3DMathEngine::TransformMatrix (+0x2c), FVector, FMatrix
#include "common_pathfind.h"

#include <cmath>
#include <cstdint>
#include <cstring>

using CmnBehavior::FollowerPlayerShipOrNull;

// ============================================================================
// FormationBehavior
//
// UpdateThreatFlag() is a faithful translation of Common.dll.c FUN_062cc3b0,
// this class's own vtable slot 3 (.rdata:0639fb70).
//
// The other two bodies that used to live in this section, Update() and
// EnterState(), were FUN_062c4900 and FUN_062c3d10 -- DockBehavior's, not this
// class's. They MOVED to src/Common/common_dockbehavior.cpp on 2026-09-17
// (claude:common-dockmove) as DockBehavior::UpdateApproachTail() and the full
// DockBehavior::EnterState(), together with the entire >= 0x2b8 field block they
// addressed and the sixteen FULLPORT-DEFERRED stand-ins they called. See
// common_behavior.h's FormationBehavior class comment and decomp-tracker items
// 1279/1285 for the RTTI/vtable/call-site/object-size proof.
// ============================================================================

// FUN_062cbfe0 (.text:062cbfe0-062cc01e, RET 4). FaceAxis2ac is not written by retail; the
// port zeroes it.
FormationBehavior::FormationBehavior(int InTypeId)
    : BaseFollowerBehavior(InTypeId)
    , StandoffDistance2a0(200.0f)          // 062cc007 0x43480000
    , CruiseStandoffDistance2a4(500.0f)    // 062cc011 0x43fa0000
    , bDirectionSet2a8(0)                  // 062cbffb
    , Pad2a9{}
    , FaceAxis2ac{ 0.0f, 0.0f, 0.0f }
{
    Flag280 = 0;                           // 062cbfef (again, after FUN_062cb2b0)
    Flag281 = 0;                           // 062cbff5
}

// ---- Small vector-math leaves used by FUN_062c4530 (CheckCanProceed) and by a wide
// spread of other still-unported callers (0x0632xxxx-0x0635xxxx range). Both are pure,
// self-contained x87 float10 leaves with no vtable/global-state dependency, so they are
// ported here for correctness even though CheckCanProceed itself stays a deferred stub
// below (its own gap is the unported watcher/nav-helper state, not this math).
//
// FUN_062c47a0 (decomp-tracker item 1282, Common.dll.c:101105): standard 3-float dot
// product, cdecl(float*, float*). Decomp matched the raw disassembly exactly.
//
// FUN_062c4780 (decomp-tracker item 1281, Common.dll.c:101097): Ghidra's decompiler
// produced an empty `void FUN_062c4780(void) { return; }` for this one -- WRONG. The
// raw listing (Common.dll.txt:189708-189722) shows a real thiscall(ECX) leaf that loads
// [ECX], [ECX+4], [ECX+8] and returns ECX . ECX (squared magnitude) in ST0; every call
// site (19 xrefs) pairs it with FUN_062c47a0-shaped dot products and treats the result
// as (float10)/(float). Ported from the raw instructions, not the (bogus) decomp.
// Both leave their result unrounded in ST0 (double here): VecDot3 sums (z + y) + x
// (062c47a8-062c47ba), VecMagSq3 sums (x + y) + z (062c4788-062c4796).
static double VecDot3(Vector const& A, Vector const& B)
{
    return ((double)A.z * B.z + (double)A.y * B.y) + (double)A.x * B.x;
}

static double VecMagSq3(Vector const& V)
{
    return ((double)V.x * V.x + (double)V.y * V.y) + (double)V.z * V.z;
}

// ---- FULLPORT-DEFERRED stand-ins for cross-cutting / not-yet-ported calls ----
//
// TRIMMED 2026-09-17 (claude:common-dockmove). This bank used to hold eighteen
// stand-ins. Every one of them existed only to serve Update()/EnterState(), which
// are DockBehavior's retail bodies (FUN_062c4900 / FUN_062c3d10) and have moved to
// src/Common/common_dockbehavior.cpp together with their stand-ins. What is left is
// the three helpers FormationBehavior's own remaining methods actually call.

// SetAlertLevel is GONE (2026-09-17, claude:w21-stubsweep). It was an empty body for
// FUN_062d88b0, and its stated reason -- "MoveIdea's destination field offset is not
// recovered here (common_idea.h)" -- was stale. common_idea.h recovered +0x44 as
// MoveIdea::Field44 and ports the setter as MoveIdea::SetField44(int); this class's
// `Move` member at +0x150 has been a typed MoveIdea for several waves. The receiver
// identification the old comment carried (`LEA ECX,[ESI+0x150]` at .text:062cc3f9)
// was correct all along -- only the callee was left disconnected.

// Slot 3, FUN_062cc3b0 (.text:062cc3b0-062cc40c).
void FormationBehavior::Slot3()
{
    BaseFollowerBehavior::Slot3();                                  // 062cc3b3 FUN_062cb540
    (void)ObjInspectOf(OwnerContext)->get_formation_offset(Offset290);   // 062cc3c4 slot 0xac
    if (NavHelper->SelfTarget.Iface->is_player())        // 062cc3dc slot 0xbc
    {
        Flag28c = 1;                                                // 062cc3eb
        MatchThrottle.SetFlag39(true);                              // 062cc3f2 FUN_062d6710
        Move.SetField44(2);                                         // 062cc3ff FUN_062d88b0
    }
    bDirectionSet2a8 = 0;                                           // 062cc404
}

// Slot 4, LAB_062cc410 (.text:062cc410-062cc466).
void FormationBehavior::Slot4()
{
    BaseFollowerBehavior::Slot4();                                  // 062cc414 FUN_062cb5e0
    if (!NavHelper->SelfTarget.Iface->is_player())       // 062cc42b slot 0xbc
        return;
    (void)NavHelper->GunConvergenceStateBytes.RequestRelease();     // 062cc43e FUN_062ecd30
    unsigned int const Id = ObjInspectOf(OwnerContext)->get_id();   // 062cc448 slot 0x20
    (void)ObjAffectOf(OwnerContext)->slot_60(1, &Id, -1);           // 062cc461 IObjAffect +0x60
}

// Slot 5, FUN_062cc030 (.text:062cc030-062cc0bf, RET 4, argument unread).
int FormationBehavior::Slot5(float Value)
{
    (void)Value;
    int Result = (Flag285 != 0) ? 1 : 2;                            // 062cc034-062cc043
    FGunConvergenceStateBlock* const Rec = &NavHelper->GunConvergenceStateBytes;   // +0x1364
    if (Rec->GetFollowLeader() != nullptr)                          // 062cc052 FUN_062ecad0
    {
        LeadWatcher.set(nullptr);                                   // LAB_062cc07e
    }
    else
    {
        IObjRW* const Default = Rec->GetFormationLeader();          // 062cc064 FUN_062ecab0
        LeadWatcher.set(Default != nullptr
            ? reinterpret_cast<Watchable const*>(reinterpret_cast<unsigned char*>(Default) + 8)
            : nullptr);
    }
    if (ObjRwFromWatchable(const_cast<Watchable*>(LeadWatcher.target())) != nullptr)
        Result = 1;                                                 // 062cc09e
    ModeSelector0xc = (Flag29 != 0) ? 3 : Result;                   // 062cc0a3-062cc0b8
    return ModeSelector0xc;
}

// Slot 6, LAB_062cc0d0 (.text:062cc0d0-062cc3a4, RET 4, argument unread).
EvaluateResult FormationBehavior::evaluate(unsigned int Context)
{
    (void)Context;
    IObjInspect const* const Owner = ObjInspectOf(OwnerContext);
    IObjRW* Target = nullptr;
    (void)Owner->get_target(Target);                                // 062cc0ed slot 0x74

    // 062cc0f0-062cc124: the owner's ship and its group id, or (0, 0).
    CObject const* OwnShip = Owner->cobject();
    unsigned int OwnGroup = 0;
    if (OwnShip != nullptr && (static_cast<unsigned int>(OwnShip->ObjectClass) & 0x503u) == 0x503u)
        OwnGroup = *reinterpret_cast<unsigned int const*>(reinterpret_cast<char const*>(OwnShip) + 0x1b4);
    else
        OwnShip = nullptr;

    if (Target == nullptr)
        return EvalSkip;                                            // 062cc12c
    bool bInLane = false;
    (void)Owner->is_using_tradelane(&bInLane);                      // 062cc140 slot 0x134
    if (bInLane)
        return EvalSkip;

    // 062cc161-062cc1eb: multiplayer, both player ships -> their group ids must match.
    if (!SinglePlayer())
    {
        CObject const* const A = FollowerPlayerShipOrNull(Owner->cobject());
        CObject const* const B = FollowerPlayerShipOrNull(ObjInspectOf(Target)->cobject());
        if (A != nullptr && B != nullptr &&
            *(reinterpret_cast<unsigned char const*>(A) + 0x2bd) != 0 &&
            *(reinterpret_cast<unsigned char const*>(B) + 0x2bd) != 0)
        {
            unsigned int const GroupA = *reinterpret_cast<unsigned int const*>(reinterpret_cast<char const*>(A) + 0x1b4);
            unsigned int const GroupB = *reinterpret_cast<unsigned int const*>(reinterpret_cast<char const*>(B) + 0x1b4);
            if (GroupA == 0 || GroupB == 0 || GroupA != GroupB)
                return EvalSkip;
        }
    }

    // LAB_062cc1f1: the target's type and behavior.
    unsigned int TypeBits = 0;
    if (Target->get_type(TypeBits) != 0)
        return EvalSkip;
    if ((TypeBits & 0x7f0000u) == 0 || (TypeBits & 0x200000u) != 0)
        return EvalSkip;
    int const BehaviorId = Target->get_behavior_id();               // 062cc226 slot 0x148
    if (BehaviorId == 0x11 || BehaviorId == 8)
        return EvalSkip;

    // LAB_062cc242: the leader chain must not come back to the owner. The out-param is
    // zeroed once, before the loop (062cc23e), as in Slot7.
    {
        IObjRW* Node = Target;
        IObjRW* Leader = nullptr;
        for (;;)
        {
            Node->get_follow_leader(Leader);                        // 062cc24b slot 0xb8
            if (Leader == nullptr)
                Node->get_formation_leader(Leader);                 // 062cc262 slot 0xb0
            if (Leader == Node)
                break;
            if (Leader != nullptr)
            {
                unsigned int const OwnerId = Owner->get_id();       // 062cc279, owner first
                if (Leader->get_id() == OwnerId)                    // 062cc284
                    return EvalSkip;                                // 062cc289
            }
            Node = Leader;
            if (Node == nullptr)
                break;
        }
    }

    // LAB_062cc299: FUN_062e6200 on the target.
    if ((GetObjectTypeOrZero(ObjInspectOf(Target)) & 0x7f0000u) == 0)
        return EvalSkip;

    // 062cc2b1-062cc2f8: against the owner's ship captured at the top.
    CObject const* const TargetShip = FollowerPlayerShipOrNull(ObjInspectOf(Target)->cobject());
    if (OwnShip != nullptr && TargetShip != nullptr &&
        *(reinterpret_cast<unsigned char const*>(TargetShip) + 0x2bd) != 0)
    {
        if (OwnGroup == 0)
            return EvalSkip;
        if (*reinterpret_cast<unsigned int const*>(reinterpret_cast<char const*>(TargetShip) + 0x1b4) != OwnGroup)
            return EvalSkip;
    }

    // LAB_062cc2fe: own cached centre (latch +0x157a, set before the call).
    SystemManager* const Sys = NavHelper;
    if (Sys->SelfTarget.bPosValid == 0)
    {
        Sys->SelfTarget.bPosValid = 1;
        Sys->SelfTarget.Iface->get_physical_radius(Sys->SelfTarget.CachedRadius,
                                                              Sys->SelfTarget.CachedPos);
    }
    // 062cc32c-062cc366: |target position - centre|^2 against 25000000.0f (DAT_0639fbd8),
    // summed on the x87 stack ((dx*dx + dz*dz) + dy*dy). TEST AH,0x41 + JP leaves on greater
    // or unordered.
    Vector const& TargetPos = ObjInspectOf(Target)->get_position(); // 062cc332 slot 0
    double const Dx = static_cast<double>(TargetPos.x) - Sys->SelfTarget.CachedPos.x;
    double const Dy = static_cast<double>(TargetPos.y) - Sys->SelfTarget.CachedPos.y;
    double const Dz = static_cast<double>(TargetPos.z) - Sys->SelfTarget.CachedPos.z;
    double const DistSq = (Dx * Dx + Dz * Dz) + Dy * Dy;
    if (!(DistSq <= 25000000.0))
        return EvalSkip;

    // 062cc368-062cc396: the target's attitude towards the owner, > -0.6f (DAT_0639f540)
    // returns 1 (`TEST AH,0x41; JZ` is taken on greater).
    float Attitude = 0.0f;
    (void)ObjInspectOf(Target)->get_attitude_towards(Attitude, Owner);   // 062cc37f slot 0x68
    if (Attitude > -0.6f)
        return EvalBlocked;                                         // EAX = 1
    return EvalSkip;
}

// Slot 10, LAB_062cd630: `OR byte [ECX+0x28],0x2; MOV AL,1; RET 4`.
bool FormationBehavior::Slot10(void* DirectiveOp)
{
    (void)DirectiveOp;
    Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
    return true;
}

// Slot 12, LAB_062cc990 (.text:062cc990-062ccae9, RET 4).
void FormationBehavior::set_direction(Vector const& Dir)
{
    // 062cc996-062cc9c1: |Dir|^2 on the x87 stack ((x*x + y*y) + z*z) against 0.0f;
    // TEST AH,0x41 + JNZ leaves (clearing +0x2a8) on not-greater.
    double const LenSq = (static_cast<double>(Dir.x) * Dir.x + static_cast<double>(Dir.y) * Dir.y)
                         + static_cast<double>(Dir.z) * Dir.z;
    if (!(LenSq > 0.0))
    {
        bDirectionSet2a8 = 0;                                       // LAB_062ccae0
        return;
    }
    bDirectionSet2a8 = 1;                                           // 062cc9ca
    FaceAxis2ac.x = -Dir.y;                                         // 062cc9d6
    FaceAxis2ac.z = 0.0f;                                           // 062cc9de (int 0)
    FaceAxis2ac.y = -Dir.x;                                         // 062cc9ea
    // 062cc9f0-062cca22: get_turn_sensitivity_factor() > 0.0 (double DAT_0639c430) scales
    // both by 1/factor; the reciprocal stays on the x87 stack, each product is rounded once.
    double const Factor = BehaviorMgrPtr->get_turn_sensitivity_factor();
    if (Factor > 0.0)
    {
        double const Inverse = 1.0 / Factor;                        // FDIVR 1.0f
        FaceAxis2ac.x = static_cast<float>(Inverse * FaceAxis2ac.x);
        FaceAxis2ac.y = static_cast<float>(Inverse * FaceAxis2ac.y);
    }
    // LAB_062cca26 / LAB_062cca7b: clamp each to [-1, 1]. Retail: not greater than -1.0f ->
    // -1.0f; else not less than 1.0f (TEST AH,0x5 + JP) -> 1.0f; else the value.
    FaceAxis2ac.x = !(FaceAxis2ac.x > -1.0f) ? -1.0f : (!(FaceAxis2ac.x < 1.0f) ? 1.0f : FaceAxis2ac.x);
    FaceAxis2ac.y = !(FaceAxis2ac.y > -1.0f) ? -1.0f : (!(FaceAxis2ac.y < 1.0f) ? 1.0f : FaceAxis2ac.y);
    FaceAxis2ac.z = -Dir.z;                                         // 062ccad6
}

// The NavHelper cached-physical-bounds latch, emitted twice inside Slot27 (062cc6b6-062cc6cf
// and 062cc748-062cc75f) and already ported byte-for-byte three other times in this port
// (FormationMoveFx::Update here, ComputeLeadFirePosition and its neighbour in
// common_aifirecontrol.cpp). +0x157a is the "already refreshed this frame" flag, +0x1580 the
// own-ship IObjInspect, slot 0x2c is get_physical_radius(float& Radius, Vector& Centre) with
// the radius landing at +0x1514 and the centre at +0x1508 -- push order verified at
// 062cc6c5/062cc6ce, where &+0x1514 is pushed LAST and is therefore argument one.
static void NavRefreshPhysicalBounds(SystemManager* Nav)
{
    // `MOV AL,[ESI+0x157a]; ADD ESI,0x14fc; TEST AL,AL; JNZ` -- the latch is set (`MOV
    // [ESI+0x7e],BL`) before the call; no null tests.
    if (Nav->SelfTarget.bPosValid != 0)
        return;
    Nav->SelfTarget.bPosValid = 1;
    (void)Nav->SelfTarget.Iface->get_physical_radius(Nav->SelfTarget.CachedRadius,
                                                               Nav->SelfTarget.CachedPos);
}

// FUN_062e8840 (`MOV AL,[ECX+0x78]; RET` on SystemManager::Timers) is
// SystemManagerTimerTable::IsTimer5Active (common_sysmgr_timertable.cpp).

// LAB_062cc470, decomp-tracker item 1399. Own vtable slot 27 (.rdata:0639fbd0).
// Full-body trace of Common.dll.txt 201121-201507, 062cc470 .. 062cc8e7 inclusive.
//
// FRAME LEDGER used throughout, because nearly every local below is quoted by its raw
// [ESP+N]: entry ESP == E; `SUB ESP,0x98` + `PUSH EBX` + `PUSH EBP` + (later)
// `PUSH ESI` + `PUSH EDI` put the body's ESP at L == E-0xa8. Every engine/math/idea call
// in this function is stdcall or thiscall (there is not one `ADD ESP,n` after a vtable
// call anywhere in the body), so L is stable and `[ESP+N]` means L+N except across the
// four explicit pushes and the one `SUB ESP,0xc`, which are called out where they matter.
void FormationBehavior::Slot27()
{
    // 062cc47a-062cc49f: re-point LeadWatcher at whatever the SystemManager's directive
    // record currently names. Retail's `CMP EAX,[EBX]; JZ; CALL BaseWatcher::set_pointer`
    // is precisely BaseWatcher::set(), which does that same dedup internally.
    // (FollowBehavior's own slot 27 does NOT have this step -- it only reads the watcher.)
    // NavHelper is dereferenced with no null test.
    {
        IObjRW* const Directed = NavHelper->GunConvergenceStateBytes.GetFormationLeader(); // FUN_062ecab0
        LeadWatcher.set(Directed != nullptr
            ? reinterpret_cast<Watchable const*>(reinterpret_cast<unsigned char*>(Directed) + 8)
            : nullptr);
    }

    // 062cc4a2-062cc4b1: two separate guards on the SAME raw slot -- `!= 0` then
    // `(raw - 8) != 0`. Both jump to the shared epilogue at LAB_062cc8df, which is reached
    // BEFORE `PUSH ESI/EDI`, so retail's stack stays balanced on this path.
    IObjRW* const Leader = ObjRwFromWatchable(
        const_cast<void*>(static_cast<void const*>(LeadWatcher.target())));
    if (Leader == nullptr)
        return;

    // 062cc4c6-062cc4de: the leader's own node transform via FUN_062e61f0, of which retail
    // keeps only nine dwords -- the 3x3 rotation, not the translation -- at L+0x78, copied
    // unconditionally.
    Matrix const LeaderBasis = Behavior_InvokeNodeSlotC(Leader)->Rotation;

    // 062cc4e9-062cc4f9: L+0x6c.
    Vector LeaderPos;
    GetIfacePositionOrZero(&LeaderPos, Leader);

    // 062cc4f4-062cc503: OwnerContext's primary vtable +0xac == IObjInspect slot 43,
    // get_formation_offset(Vector&), writing straight into this->FormationOffset (+0x290) --
    // the same out-param UpdateThreatFlag() above hands the same slot. No null test.
    (void)ObjInspectOf(OwnerContext)->get_formation_offset(Offset290);

    // 062cc509-062cc535: the LEADER's own formation offset, into a zeroed local at L+0x54.
    // Same slot, different receiver -- retail re-unwraps the watcher for it.
    Vector LeaderFormationOffset{ 0.0f, 0.0f, 0.0f };
    (void)ObjInspectOf(ObjRwFromWatchable(const_cast<void*>(static_cast<void const*>(
        LeadWatcher.target()))))->get_formation_offset(LeaderFormationOffset);

    // DAT_063fc538 is this function's cache of FUN_06391cf0 (the 3DMathEngine).
    I3DMathEngine* const Math = Acquire3DMathEngine();

    // 062cc54e-062cc560 then 062cc58a: anchor = LeaderPos - Rot(LeaderBasis, LeaderOffset).
    // FUN_06288800 is Vec3Sub (item 729, out = a - b) -- NOT the add at 062887d0; both
    // appear in this function, four bytes apart in .rdata order, and getting them the wrong
    // way round would silently double the offset instead of cancelling it. Verified from
    // the two bodies: 06288800 is FSUB throughout, 062887d0 is FADD throughout.
    Vector LeaderOffsetWorld{ 0.0f, 0.0f, 0.0f };
    Math->TransformMatrix(reinterpret_cast<FVector&>(LeaderOffsetWorld),
                          reinterpret_cast<FMatrix const&>(LeaderBasis),
                          reinterpret_cast<FVector const&>(LeaderFormationOffset)); // +0x2c
    Vector const Anchor{ LeaderPos.x - LeaderOffsetWorld.x,
                         LeaderPos.y - LeaderOffsetWorld.y,
                         LeaderPos.z - LeaderOffsetWorld.z };

    // 062cc5a5-062cc5dd: slot = anchor + Rot(LeaderBasis, OwnOffset). FUN_062887d0 == Vec3Add.
    Vector OwnOffsetWorld{ 0.0f, 0.0f, 0.0f };
    Math->TransformMatrix(reinterpret_cast<FVector&>(OwnOffsetWorld),
                          reinterpret_cast<FMatrix const&>(LeaderBasis),
                          reinterpret_cast<FVector const&>(Offset290)); // +0x2c
    Vector const SlotPoint{ OwnOffsetWorld.x + Anchor.x,
                            OwnOffsetWorld.y + Anchor.y,
                            OwnOffsetWorld.z + Anchor.z };

    // 062cc5f2-062cc600: L+0x60. FUN_062e6150 == GetNegatedIfaceDirection, i.e. MINUS the
    // leader's forward basis column.
    Vector LeaderBackward{ 0.0f, 0.0f, 0.0f };
    GetNegatedIfaceDirection(&LeaderBackward, Leader);

    // 062cc603-062cc63e: IObjInspect slot 23 (+0x5c) is
    // `is_cruise_active(bool& OutCruise, bool& OutTradeLane)`; retail pre-zeroes both
    // out-params (062cc60c/062cc611) and pushes &OutTradeLane first, &OutCruise last, so
    // OutCruise is argument one. The standoff picked here is added to the ship's own
    // physical radius further down.
    bool bCruise = false;
    bool bTradeLane = false;
    (void)ObjInspectOf(OwnerContext)->is_cruise_active(bCruise, bTradeLane);   // no null test
    float const Standoff = (bCruise && !bTradeLane) ? CruiseStandoffDistance2a4
                                                    : StandoffDistance2a0;

    // 062cc642-062cc6a1: the own ship (NavHelper->ExternalIface, slot 0xc get_transform)
    // forward column built (FUN_0628b030) and negated (FUN_062c5a70) into L+0x14, dotted
    // with GetNegatedIfaceDirection's L+0x60 as (z*z' + y*y') + x*x' on the x87 stack.
    // `FCOMP 0.0f; TEST AH,0x41; JNZ` sends <= 0.0f (and unordered) to LAB_062cc778.
    double Clearance = 0.0f;
    {
        Matrix const& OwnBasis = NavHelper->SelfTarget.Iface->get_transform().Rotation;
        Vector const OwnBackward{ -OwnBasis.E[0][2], -OwnBasis.E[1][2], -OwnBasis.E[2][2] };
        double const Facing = static_cast<double>(OwnBackward.z) * LeaderBackward.z
                            + static_cast<double>(OwnBackward.y) * LeaderBackward.y
                            + static_cast<double>(OwnBackward.x) * LeaderBackward.x;
        if (Facing > 0.0)
        {
            // 062cc6a7-062cc6cf: the latch-and-refresh of the cached physical bounds.
            NavRefreshPhysicalBounds(NavHelper);

            // 062cc6d2-062cc734: (SlotPoint - cached centre) into L+0x20, the own forward
            // column again (get_transform) into L+0x14 and its negation into L+0x9c. None of
            // the three is read again; reproduced as retail computes them.
            Vector const FromCentre{ SlotPoint.x - NavHelper->SelfTarget.CachedPos.x,
                                     SlotPoint.y - NavHelper->SelfTarget.CachedPos.y,
                                     SlotPoint.z - NavHelper->SelfTarget.CachedPos.z };
            Matrix const& OwnBasis2 = NavHelper->SelfTarget.Iface->get_transform().Rotation;
            Vector const OwnBackward2{ -OwnBasis2.E[0][2], -OwnBasis2.E[1][2], -OwnBasis2.E[2][2] };
            (void)FromCentre;
            (void)OwnBackward2;

            // 062cc739-062cc75f: the same latch-and-refresh block, emitted a second time.
            NavRefreshPhysicalBounds(NavHelper);

            // 062cc762-062cc776: radius + Standoff stays on the x87 stack; `FCOM 0.0f;
            // TEST AH,0x5; JP` keeps it when >= 0 or unordered, else 0.0f.
            double const Reach = static_cast<double>(NavHelper->SelfTarget.CachedRadius) + Standoff;
            if (!(Reach < 0.0))
                Clearance = Reach;
        }
    }

    // 062cc77e-062cc7a3: pull the formation slot back along the leader's local Z by
    // Clearance (`FLD [EDI+8]` copy; `FSUB ST0,ST1` with the unrounded clearance; FSTP).
    Vector const AdjustedOffset{ Offset290.x,
                                 Offset290.y,
                                 static_cast<float>(static_cast<double>(Offset290.z) - Clearance) };
    Vector AdjustedWorld{ 0.0f, 0.0f, 0.0f };
    Math->TransformMatrix(reinterpret_cast<FVector&>(AdjustedWorld),
                          reinterpret_cast<FMatrix const&>(LeaderBasis),
                          reinterpret_cast<FVector const&>(AdjustedOffset)); // +0x2c
    Vector const TargetPoint{ AdjustedWorld.x + Anchor.x,
                              AdjustedWorld.y + Anchor.y,
                              AdjustedWorld.z + Anchor.z };
    Face.SetPosition(TargetPoint);   // 062cc80f, FUN_062d7560

    // 062cc814-062cc842: FUN_062e8840 on &NavHelper->Timers (the byte at Timers+0x78). The
    // up-vector retail pushes is the literal (0, 0, 1): 0x3f800000 at 062cc83a.
    if (NavHelper->Timers.IsTimer5Active())
        Face.SetUpVector(Vector{ 0.0f, 0.0f, 1.0f });   // FUN_062d7640

    // 062cc847-062cc8a4: while bDirectionSet2a8 is set, the Face idea takes FaceAxis2ac and
    // the Move idea the own ship's forward column (ExternalIface slot 0xc, no null test).
    if (bDirectionSet2a8 != 0)
    {
        Matrix const& OwnBasis = NavHelper->SelfTarget.Iface->get_transform().Rotation;
        Face.SetAxis(FaceAxis2ac);   // 062cc894, FUN_062d75e0, arg is LEA EAX,[EBP+0x2ac]
        // 062cc877-062cc890: three FCHS'd components of the own-ship forward column,
        // built into the frame local retail then hands to MoveIdea::SetDirection.
        Move.SetDirection(Vector{ -OwnBasis.E[0][2],
                                  -OwnBasis.E[1][2],
                                  -OwnBasis.E[2][2] });   // 062cc8a4, FUN_062d5890
    }

    // 062cc8a9-062cc8da: tick both sub-objects through their own vtable slot 1 with
    // Behavior::DeltaTime (+0x1c), with the FormationMoveFx retarget in between.
    (void)Face.Update(DeltaTime);
    MatchThrottle.SetWatchAndPosition(Leader, SlotPoint);   // 062cc8cd, FUN_062d65f0
    (void)MatchThrottle.Update(DeltaTime);
}

// ----------------------------------------------------------------------------
// LAB_062cc8f0, decomp-tracker item 1400. FormationBehavior's OWN vtable slot 28
// (.rdata:0639fbd4 == PTR_FUN_0639fb64 + 0x70), 51 instructions, Common.dll.txt
// 201516-201570. PORTED 2026-09-17 (claude:w23-stubsweep2).
//
// UNTIL THIS PASS FormationBehavior DID NOT OVERRIDE SLOT 28 AT ALL, so every call landed on
// BaseFollowerBehavior's `virtual Vector Slot28() { return Vector{0,0,0}; }` -- the exact
// twin of the slot-27 hole commit d178e12 closed on this same class. The single caller is
// BaseFollowerBehavior::Slot7, the whole subtree's per-tick body, at .text:062cbc2d
// `LEA EAX,[ESP+0x64]; PUSH EAX; CALL [EDX+0x70]`, and it feeds the returned vector straight
// into FUN_062c50c0, which copies it to this->NestedPtr278 + 0x444. So for every
// FormationBehavior in the game the child delegate's target offset was being overwritten
// with (0,0,0) once per tick.
//
// FRAME LEDGER (entry ESP == E, return-buffer pointer at [E+4]): `SUB ESP,0x3c` + `PUSH ESI`
// + `PUSH EDI` put the body at E-0x44.
//   * E-0x3c .. E-0x31  the zeroed 12-byte local the slot-0xac call fills  (`[ESP+0x8]`)
//   * E-0x30 .. E-0x25  the TransformMatrix output                          (`[ESP+0x14]`)
//   * E-0x24 .. E-0x01  the nine dwords REP MOVSD copies out of the leader's transform
// The epilogue's `MOV EAX,[ESP+0x40]` at 062cc96d is [E+4], the incoming return buffer, and
// 062cc971-062cc984 copy E-0x30/E-0x2c/E-0x28 through it -- which is what makes this a
// by-value Vector return rather than the `void Slot28(void*)` its `RET 0x4` looks like.
//
// The shape is FollowBehavior::Slot28's (LAB_062cd4c0, item 1409, common_followbehavior.cpp)
// with one extra step in front: FollowBehavior transforms its own TargetOffset member, this
// class transforms whatever OwnerContext's slot 0xac writes into a freshly zeroed local.
Vector FormationBehavior::Slot28()
{
    // 062cc8fe-062cc916: three explicit zero dwords, then 062cc919 `CALL [EAX+0xac]` on
    // OwnerContext (+0x14) with a pointer to them. Slot 0xac is
    // IObjInspect::get_formation_offset(Vector&) -- the same slot, with a different
    // out-param, that UpdateThreatFlag() and Slot27() above both drive.
    // Note it is a LOCAL here, not this->FormationOffset (+0x290): retail's `LEA EDX,[ESP+8]`
    // is a frame address, where the other two sites use `LEA EDX,[ESI+0x290]`.
    Vector FormationOffsetLocal{ 0.0f, 0.0f, 0.0f };
    (void)ObjInspectOf(OwnerContext)->get_formation_offset(FormationOffsetLocal);   // no null test

    // 062cc91f-062cc936: `MOV ESI,[ESI+0x2c]` is LeadWatcher's target slot, and the
    // `LEA EAX,[ESI-8]`/`XOR EAX,EAX` pair is the module-wide Watchable adjustment.
    IObjRW* const Leader = ObjRwFromWatchable(
        const_cast<void*>(static_cast<void const*>(LeadWatcher.target())));

    // 062cc938-062cc94a: `MOV ECX,9; LEA EDI,[ESP+0x20]; REP MOVSD` -- nine dwords, i.e. the
    // 3x3 rotation only, not the translation, copied unconditionally.
    Matrix const LeaderBasis = Behavior_InvokeNodeSlotC(Leader)->Rotation;

    // 062cc938-062cc98a. DAT_063fc538 / FUN_06391cf0 is the lazily-cached 3DMathEngine
    // singleton (Acquire3DMathEngine, common_csys.h); slot 0x2c is
    // I3DMathEngine::TransformMatrix, pushed receiver-first per the DACOM_API convention.
    // Push order at 062cc95a-062cc969 is Out last-but-one and the source vector first, so the
    // argument order below is retail's: (Out, Basis, In).
    Vector Out;
    Acquire3DMathEngine()->TransformMatrix(reinterpret_cast<FVector&>(Out),
                                           reinterpret_cast<FMatrix const&>(LeaderBasis),
                                           reinterpret_cast<FVector const&>(FormationOffsetLocal));
    return Out;
}
