// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- BaseFollowerBehavior (the shared base of FormationBehavior and FollowBehavior):
// ctor/dtor, the per-tick Slot7 body and its slot overrides.
// CMN-SPLIT stage 1 (2026-10-14): split out of common_behavior.cpp unchanged; the
// FollowerDirectiveRecord_* helpers it calls stay in common_behavior.cpp
// (common_behavior_internal.h).

#include "common_behavior.h"
#include "common_behavior_internal.h" // CmnBehavior:: follower helpers (common_behavior.cpp)
#include "common_aisteer.h" // GetIfacePositionOrZero, IsWithinInterceptRangeOfIface
#include "common_systemmanager.h" // SystemManager -- BaseFollowerBehavior::NavHelper's real type
#include "common_misc.h" // SinglePlayer() -- Slot7's multiplayer group test
#include "common_csys.h"  // Matrix, Transform
#include "common_iface.h" // IObjInspect / IObjRW / ObjInspectOf / ObjRwFromWatchable
#include "x86math.h"      // FVector, FMatrix
#include "common_pathfind.h" // PathfindManager
#include "common_dockbehavior.h" // DockBehavior::SetDestPointOffset (FUN_062c50c0)

#include <cmath>
#include <cstdint>
#include <cstring>

using CmnBehavior::FollowerExternalIfaceIsPlayer;
using CmnBehavior::FollowerPlayerShipOrNull;

// ============================================================================
// BaseFollowerBehavior
// ============================================================================

// FUN_062cb2b0 (.text:062cb2b0-062cb37f, RET 4). FUN_062d2020(TypeId) is ConstructBase; the
// five Ideas are the member constructors in declaration order (EH states 2..6), LeadWatcher
// is zeroed, then +0x281/+0x280/+0x285/+0x28c/+0x278/+0x282/+0x27c/+0x284/+0x283 = 0 and the
// vptr. Retail leaves +0x288, +0x290..+0x29b and +0x29c unwritten; the port zeroes them.
BaseFollowerBehavior::BaseFollowerBehavior(int InTypeId)
    : TypeId(InTypeId)
    , CachedHandle08(0)
    , ModeSelector0xc(2)
    , UpdateResult(1)
    , OwnerContext(nullptr)
    , Opaque18{}
    , DeltaTime(0.0f)
    , BehaviorMgrPtr(nullptr)
    , NavHelper(nullptr)
    , Flags28(0)
    , Flag29(0)
    , Pad2a{}
    , LeadWatcher()
    , MatchThrottle()
    , Face()
    , Move()
    , BreakAway()
    , Afterburner()
    , DockDelegate(nullptr)
    , GotoDelegate(nullptr)
    , Flag280(0)
    , Flag281(0)
    , Flag282(0)
    , Flag283(0)
    , Flag284(0)
    , Flag285(0)
    , Pad286{}
    , Field288(0)
    , Flag28c(0)
    , Pad28d{}
    , Offset290{ 0.0f, 0.0f, 0.0f }
    , Field29c(0)
{
    ConstructBase(InTypeId);   // 062cb2d3 FUN_062d2020
}

// FUN_062cb3b0: guarded set_pointer(0) on +0x1f0 (BreakAway's picker watcher), +0x164
// (Move), +0xa4 (Face), +0x48 (MatchThrottle), +0x2c (LeadWatcher) -- the members' own
// BaseWatcher destructors in reverse declaration order -- then FUN_062d2080 (~Behavior).
BaseFollowerBehavior::~BaseFollowerBehavior()
{
}

// ============================================================================
// BaseFollowerBehavior::Slot7 -- LAB_062cb700, decomp-tracker item 1390.
//
// 488 instructions, hand-translated from EXE\Disasm\Common.dll.txt 199822-200354.
// There is NO Ghidra decompilation for this function; every branch below is read
// off the raw listing, and every x87 comparison was re-derived from the status-word
// flag table rather than copied from any prose (see each one's own comment).
//
// WHAT IT IS. The per-tick body of the whole BaseFollowerBehavior subtree: slot 7 of
// .rdata:0639fb00 (BaseFollowerBehavior), 0639fb80 (FormationBehavior) and 0639fc00
// (FollowBehavior), all three holding the identical address. IBehaviorManager::
// UpdateBehaviorTick stores its accumulated float into the behavior's own +0x1c and
// then dispatches here (.text:062d335d-062d336a).
//
// WHAT IT DOES, in retail's own order:
//   1. Bail out (result 3) with no owner; bail out (result 0) if the "order consumed"
//      latch at +0x285 is set.
//   2. Re-point LeadWatcher at whatever the SystemManager's AI-directive record
//      (SystemManager+0x1364) currently names as the override target.
//   3. Skip the whole validation block when the SystemManager's external interface
//      reports is_player() false.
//   4. Validate the watched leader: it must exist; in multiplayer, if both the owner
//      and the leader are player ships they must share the same group id; the leader's
//      get_type() bits must have something in 0x7f0000 and nothing in 0x200000; its
//      get_behavior_id() must not be 0x11 or 8; and walking its follow/formation leader
//      chain must not come back round to the owner. Every failure yields result 3.
//   5. If the record has a pending interrupt (its +0xbf latch), reset both idea
//      sub-objects and yield result 1.
//   6. Run the child goto delegate (+0x27c) while the +0x283 latch is set and the
//      leader is close and slow, and the child dock delegate (+0x278) while +0x282 is
//      set; either may end the tick with its own result.
//   7. Otherwise read the record's current dock order, and either hand it to the dock
//      delegate as a pub::AI::DirectiveDockOp, or -- when already inside the dock
//      candidate's physical radius -- commit it through the owner's IObjAffect slot
//      0x64.
//   8. With nothing pending at all, pick between the subclass's own Slot27() and the
//      final-leg payload FUN_062cbcf0 on three gate checks.
//
// WHAT IT SETTLES. This is the only caller of the two virtuals BaseFollowerBehavior
// adds to Behavior's 27, and both signatures now have a live call site in the port:
//   * Slot27() at .text:062cbcb9 `MOV EAX,[ESI]; MOV ECX,ESI; CALL [EAX+0x6c]` -- no
//     argument is pushed and EAX is overwritten with EBX before the epilogue, so it is
//     `void Slot27()`.
//   * Slot28() at .text:062cbc2d `LEA EAX,[ESP+0x64]; PUSH EAX; CALL [EDX+0x70]`,
//     whose returned EAX is then handed straight to FUN_062c50c0 -- the MSVC hidden
//     return-buffer protocol, so it is `Vector Slot28()` and not `void Slot28(void*)`.
// ============================================================================

// Already-ported helpers this body calls, all defined elsewhere in this DLL. Local
// extern "C" declarations of __fastcall functions must sit at global scope (MSVC
// C2598 otherwise) -- same convention this file already uses for FUN_062e8850.
// FUN_062ecaf0 / 5b0 / 610 / 650 / 680 / ecb20 / ecc00 are FGunConvergenceStateBlock members
// (common_gunconvergence.cpp): GetCurrentLeader, TryGetValueB4, GetDirectiveOrder, SetValueB0,
// TryGetValueB0, FollowerDirectiveGateA / B.
// FUN_062e74a0 (item 1798) is SystemManagerTimerTable::FindVerifiedObject.
// FUN_062c50c0 is DockBehavior::SetDestPointOffset (common_dockbehavior.cpp); DockDelegate is
// BehaviorDispatchTable[7], the Dock behaviour.

// The 0x10-byte block FUN_062ec610 (Common.dll.txt 251832-251842) copies out of the
// AI-directive record: record +0xa8 -> +0x00, +0xac -> +0x04, +0xbd -> +0x08,
// +0xc2 -> +0x09, +0xb8 -> +0x0c (FGunConvergenceStateBlock::GetDirectiveOrder). Named
// positionally; what this body proves about it is written on each field.
struct FollowerDirectiveOrder
{
    unsigned int Field00;      // record +0xa8. Resolved by id into the dock candidate and
                               // written to DirectiveDockOp::DockObjectId.
    unsigned int Field04;      // record +0xac. Resolved by id into a second object and
                               // written to DirectiveDockOp::Field_10.
    unsigned char Field08;     // record +0xbd. "There is an order pending" -- gates the
                               // whole block at .text:062cbad1/062cbb33.
    unsigned char Field09;     // record +0xc2. When 1, skip the range test entirely and
                               // commit immediately; also passed on as go_tradelane's bool.
    unsigned char Pad0A[2];
    float Field0C;             // record +0xb8. Passed on as IObjAffect::go_tradelane's 4th
                               // argument, which Server's Ship body forwards to
                               // CShip::go_tradelane's `float` (.text:06d028c0), so a float.
};
static_assert(sizeof(FollowerDirectiveOrder) == 0x10, "FollowerDirectiveOrder must match FUN_062ec610's 0x10-byte output block");


int BaseFollowerBehavior::Slot7()
{
    // .text:062cb70b-062cb713. EBX is loaded from the same +0x14 later at 062cb79c;
    // one variable here, since retail re-reads the identical field.
    IObjRW* const Owner = static_cast<IObjRW*>(OwnerContext);
    if (Owner == nullptr)
    {
        UpdateResult = 3;   // LAB_062cb715
        return 3;
    }

    if (Flag285 != 0)
    {
        UpdateResult = 0;   // .text:062cb728-062cb741
        return 0;
    }

    SystemManager* const Sys = NavHelper;   // not null-tested from here on (as retail)
    FGunConvergenceStateBlock* const Rec = &Sys->GunConvergenceStateBytes;   // Sys + 0x1364

    // .text:062cb742-062cb768. FUN_062ecaf0 hands back an already "-8"ed IObjRW*
    // (or 0); retail adds 8 back to get the Watchable sub-object the watcher stores,
    // and only re-points the watcher when the value actually changed -- which is
    // exactly BaseWatcher::set()'s own guard.
    {
        IObjRW* const Override = Sys->GunConvergenceStateBytes.GetCurrentLeader();   // FUN_062ecaf0(Rec)
        LeadWatcher.set(static_cast<Watchable const*>(Override));   // null stays null
    }

    // .text:062cb76a-062cb784. Everything down to LAB_062cb8de is skipped wholesale
    // when the SystemManager's external interface is not a player's.
    if (FollowerExternalIfaceIsPlayer(Sys))
    {
        IObjRW* Node = ObjRwFromWatchable(
            const_cast<Watchable*>(LeadWatcher.target()));
        if (Node == nullptr)
        {
            UpdateResult = 3;
            return 3;
        }

        // .text:062cb7a2-062cb832. Multiplayer only: when BOTH ends are player ships
        // with their +0x2bd flag set, their +0x1b4 group ids must both be non-zero and
        // equal, otherwise this behavior refuses the tick. CShip's own confirmed
        // bIsPlayer sits at +0x2bd (common_cobject.h); +0x1b4 is CShip::GroupId, read here
        // by offset because FollowerPlayerShipOrNull hands back a CObject.
        if (!SinglePlayer())
        {
            CObject const* const OwnerShip = FollowerPlayerShipOrNull(Owner->cobject());
            CObject const* const LeaderShip = FollowerPlayerShipOrNull(Node->cobject());
            if (OwnerShip != nullptr && LeaderShip != nullptr &&
                *(reinterpret_cast<unsigned char const*>(OwnerShip) + 0x2bd) != 0 &&
                *(reinterpret_cast<unsigned char const*>(LeaderShip) + 0x2bd) != 0)
            {
                unsigned int const OwnerGroup =
                    *reinterpret_cast<unsigned int const*>(reinterpret_cast<char const*>(OwnerShip) + 0x1b4);
                unsigned int const LeaderGroup =
                    *reinterpret_cast<unsigned int const*>(reinterpret_cast<char const*>(LeaderShip) + 0x1b4);
                if (OwnerGroup == 0 || LeaderGroup == 0 || OwnerGroup != LeaderGroup)
                {
                    UpdateResult = 3;
                    return 3;
                }
            }
        }

        // LAB_062cb835. get_type is IObjInspect slot 34 (+0x88) and returns 0 on
        // success, so a non-zero result is the failure path here.
        unsigned int TypeBits = 0;
        if (Node->get_type(TypeBits) != 0)
        {
            UpdateResult = 3;
            return 3;
        }
        if ((TypeBits & 0x7f0000u) == 0 || (TypeBits & 0x200000u) != 0)
        {
            UpdateResult = 3;
            return 3;
        }
        int const BehaviorId = Node->get_behavior_id();   // slot 82, +0x148
        if (BehaviorId == 0x11 || BehaviorId == 8)
        {
            UpdateResult = 3;
            return 3;
        }

        // LAB_062cb888. Walk the leader chain: follow-leader first (slot 46, +0xb8),
        // formation-leader (slot 44, +0xb0) only when that left the out-param null.
        // NOTE the out-param is zeroed ONCE, before the loop (.text:062cb884), never
        // inside it -- so a getter that declines to write leaves the previous leader
        // in place. Reproduced exactly.
        IObjRW* Leader = nullptr;
        for (;;)
        {
            Node->get_follow_leader(Leader);
            if (Leader == nullptr)
                Node->get_formation_leader(Leader);
            if (Leader == Node)
                break;                       // .text:062cb8b4 -> LAB_062cb8de
            if (Leader != nullptr)
            {
                // get_id is slot 8 (+0x20) on both receivers; retail calls the owner's
                // first (062cb8ba), then the leader's (062cb8c9).
                unsigned int const OwnerId = Owner->get_id();
                if (Leader->get_id() == OwnerId)
                {
                    UpdateResult = 3;
                    return 3;
                }
            }
            Node = Leader;
            if (Node == nullptr)
                break;
        }
    }

    // LAB_062cb8de. FUN_062ec5b0 answers the record's +0xbf latch and, when set,
    // copies +0xb4 out; retail never reads that copy back, so neither does this.
    {
        unsigned int PendingB4 = 0;
        if (Rec->TryGetValueB4(&PendingB4) != 0)
        {
            // .text:062cb8f5-062cb90a: both Ideas' slot 4 (Commit).
            MatchThrottle.Commit();
            Face.Commit();
            UpdateResult = 1;
            return 1;
        }
    }

    // LAB_062cb922. From here on retail keeps a literal 1 in EBX; every "result 1"
    // exit below is that register.
    IObjRW* const Watched = ObjRwFromWatchable(
        const_cast<Watchable*>(LeadWatcher.target()));
    if (Watched == nullptr)
    {
        UpdateResult = 2;   // LAB_062cbcd0
        return 2;
    }

    if (Flag283 != 0)
    {
        // Every path through this block that does not return early lands on
        // LAB_062cba4a, which clears the latch -- so the clear is written once, at the
        // bottom, rather than duplicated on each of retail's three jumps to it.
        //
        // .text:062cb959-062cb973. cdecl, args pushed right-to-left, so the call is
        // IsWithinInterceptRangeOfIface(Sys + 0x14fc, Watched, 200.0f) -- 0x43480000.
        if (!IsWithinInterceptRangeOfIface(&Sys->SelfTarget, Watched, 200.0f))
        {
            // .text:062cb98e-062cb9ba. get_velocity (slot 1, +0x04) returns a Vector by
            // value through the hidden buffer retail pushes; the x87 block squares and
            // sums the three components, then FCOMPs against DAT_0639fb58. That
            // constant's raw .rdata bytes are 00 00 c8 42 (Common.dll.txt 562959-562962)
            // == 0x42c80000 == 100.0f, i.e. a speed threshold of 10.0 units.
            // TEST AH,0x5 + JP: taken on ">=-or-unordered" (see FollowerDirectiveGateB
            // for the full flag-table derivation), and the JP target is the
            // clear-the-latch path -- so the goto delegate only runs while the leader is
            // slower than that. Written !(a < b) to keep retail's unordered behaviour.
            // The sum stays on the x87 stack (extended), hence double.
            Vector const Vel = Watched->get_velocity();
            double const SpeedSq = (static_cast<double>(Vel.x) * Vel.x
                                    + static_cast<double>(Vel.y) * Vel.y)
                                   + static_cast<double>(Vel.z) * Vel.z;
            if (SpeedSq < 100.0)
            {
                if (Flag284 == 0)
                {
                    // .text:062cb9ca-062cba21.
                    pub::AI::DirectiveGotoOp Op;
                    Op.Field_0C = 0;
                    Op.Field_1C = static_cast<int32_t>(Watched->get_id());
                    Op.Field_50 = 100.0f;   // 0x42c80000
                    GotoDelegate->Slot10(&Op);
                    GotoDelegate->Slot3();
                    // Retail writes the literal 1 here and then tests it
                    // (`MOV AL,BL; TEST AL,AL; MOV [ESI+0x284],BL; JZ ...`), so the
                    // branch is dead and control always continues below.
                    Flag284 = 1;
                }
                // LAB_062cba23. The tick delta is handed down the same way
                // IBehaviorManager hands it to this behavior: store to the child's own
                // +0x1c, then dispatch its slot 7. +0x27c is not null-tested.
                GotoDelegate->SetTickDelta(DeltaTime);
                int const ChildResult = GotoDelegate->Slot7();
                if (ChildResult == 1 || ChildResult == 0)
                {
                    UpdateResult = ChildResult;   // LAB_062cbcd5
                    return ChildResult;
                }
            }
        }
        Flag283 = 0;   // LAB_062cba4a
    }
    else if (Flag284 != 0)
    {
        // LAB_062cba53.
        Flag284 = 0;
        GotoDelegate->Slot4();
    }

    // LAB_062cba6f.
    if (Flag282 != 0 && DockDelegate != nullptr)
    {
        DockDelegate->SetTickDelta(DeltaTime);
        int const ChildResult = DockDelegate->Slot7();
        if (ChildResult == 1 || ChildResult == 0)
        {
            UpdateResult = ChildResult;
            return ChildResult;
        }
        Flag282 = 0;
        DockDelegate->Slot4();
    }

    // LAB_062cbab6.
    FollowerDirectiveOrder Order;
    std::memset(&Order, 0, sizeof(Order));
    Rec->GetDirectiveOrder((unsigned char*)&Order);

    bool RunPendingLookup = false;
    if (Flag281 != 0)
    {
        if (Order.Field08 != 0 || Flag280 == 0)
        {
            UpdateResult = 0;   // LAB_062cbb23
            return 0;
        }
        Flag280 = 0;
        Flag281 = 0;
        RunPendingLookup = true;   // falls into LAB_062cbaf3
    }
    else if (Order.Field08 == 0)
    {
        RunPendingLookup = true;   // .text:062cbb35 JZ LAB_062cbaf3
    }

    if (!RunPendingLookup)
    {
        // LAB_062cbb37. STACK-DEPTH NOTE: at .text:062cbb51 the `LEA EDX,[ESP+0x18]`
        // runs with one dword already pushed, so it names entry-relative +0x14 -- NOT
        // the +0x18 block FUN_062ec610 just filled. The two arguments of the vcall are
        // therefore (&float @+0x14, &Vector @+0x28), which is exactly
        // IObjInspect::get_physical_radius(float& Radius, Vector& Center) at slot 11
        // (+0x2c) -- the identical pair the NavHelper refresh below writes into
        // SystemManager::CachedPhysicalRadius / CachedPhysicalCenter.
        IObjInspect const* const Candidate =
            Sys->Timers.FindVerifiedObject(&Order.Field00);   // FUN_062e74a0
        // Not null-tested: retail dispatches through the result at .text:062cbb4d.
        float CandidateRadius = 0.0f;
        Vector CandidateCenter = { 0.0f, 0.0f, 0.0f };
        Candidate->get_physical_radius(CandidateRadius, CandidateCenter);

        bool CommitDirectly = (Order.Field09 == 1);   // .text:062cbb5b CMP [ESP+0x21],BL
        if (!CommitDirectly)
        {
            // .text:062cbb61-062cbb87 -- the same "refresh NavHelper's cached physical
            // centre unless the +0x157a latch says this frame already did"
            // idiom DockBehavior::NavHelperRefreshLeadPoint documents.
            if (Sys->SelfTarget.bPosValid == 0)
            {
                Sys->SelfTarget.bPosValid = 1;
                Sys->SelfTarget.Iface->get_physical_radius(Sys->SelfTarget.CachedRadius,
                                                                      Sys->SelfTarget.CachedPos);
            }
            // .text:062cbb8a-062cbbb1: the differences, squares and FSQRT stay on the x87
            // stack (extended), so double; ((dz*dz + dx*dx) + dy*dy) in retail's order.
            double const Dx = static_cast<double>(Sys->SelfTarget.CachedPos.x) - CandidateCenter.x;
            double const Dy = static_cast<double>(Sys->SelfTarget.CachedPos.y) - CandidateCenter.y;
            double const Dz = static_cast<double>(Sys->SelfTarget.CachedPos.z) - CandidateCenter.z;
            double const Distance = std::sqrt((Dz * Dz + Dx * Dx) + Dy * Dy);
            // .text:062cbbb7-062cbbc0: FCOMP [ESP+0x14]; TEST AH,0x5; JP LAB_062cbc02.
            // Same mask/jump pair as the two gates above: the JP is taken on
            // ">=-or-unordered", and its target is the issue-a-dock-directive path. So
            // the fall-through -- commit directly -- is "strictly inside the radius".
            CommitDirectly = (Distance < CandidateRadius);
        }

        if (CommitDirectly)
        {
            // LAB_062cbbc2. Second id -> object lookup, then the owner's own IObjAffect
            // sub-object (IObjRW's second base, at +4) slot 0x64.
            void* const Secondary = Sys->Timers.FindVerifiedObject(&Order.Field04);   // FUN_062e74a0
            IObjAffect* const Affect = static_cast<IObjAffect*>(Owner);
            // .text:062cbbdc-062cbbec: IObjAffect slot 25, go_tradelane (named 2026-09-24 from
            // Server.dll's Ship body .text:06d028c0, a forwarder to CShip::go_tradelane).
            // The third push is a dword read at [ESP+0x25] == Field09 plus the pad bytes; the
            // callee only uses its low byte, so the bool Field09 carries is what arrives.
            int const Committed =
                Affect->go_tradelane(Candidate, static_cast<IObjInspect const*>(Secondary),
                                     Order.Field09 != 0, Order.Field0C);
            if (Committed != 0)
            {
                UpdateResult = 1;   // LAB_062cbc54
                return 1;
            }
            Flag281 = 1;
            UpdateResult = 0;
            return 0;
        }

        // LAB_062cbc02. +0x278 is not null-tested on this path.
        {
            pub::AI::DirectiveDockOp Op;
            Op.DockObjectId = Order.Field00;
            Op.Field_10 = static_cast<int32_t>(Order.Field04);
            DockDelegate->Slot10(&Op);
            // .text:062cbc2b-062cbc3e. Slot28 returns a Vector by value; retail hands
            // the returned buffer straight to FUN_062c50c0, which copies three dwords
            // into the delegate's own +0x444.
            Vector const Offset = Slot28();
            static_cast<DockBehavior*>(DockDelegate)->SetDestPointOffset(Offset);   // FUN_062c50c0
            DockDelegate->Slot3();
        }
        Flag282 = 1;
        UpdateResult = 1;
        return 1;
    }

    // LAB_062cbaf3.
    {
        unsigned int PendingB0 = 0;
        if (Rec->TryGetValueB0(&PendingB0))
        {
            Rec->SetValueB0(0);
            Flag285 = 1;
            UpdateResult = 0;   // LAB_062cbb23
            return 0;
        }
    }

    // LAB_062cbc64. Retail evaluates gate B a second time on the path that just
    // evaluated it -- .text:062cbc76 and .text:062cbc88 are two separate calls to the
    // same argument-free predicate. Reproduced verbatim rather than folded, because
    // FollowerDirectiveGateB dispatches a virtual and is not provably side-effect-free.
    bool ReachedPayload = false;
    if (!Rec->FollowerDirectiveGateA() || Rec->FollowerDirectiveGateB())
    {
        if (Rec->FollowerDirectiveGateB() &&
            *(reinterpret_cast<unsigned char*>(Sys) + 0x1958) != 0)
        {
            ReachedPayload = true;
        }
    }

    if (ReachedPayload)
    {
        UpdateTickPayload(DeltaTime);   // FUN_062cbcf0, item 1391
    }
    else
    {
        Slot27();   // LAB_062cbcb9, CALL [EAX+0x6c]
    }
    UpdateResult = 1;
    return 1;
}

// FUN_062cbcf0 (.text:062cbcf0-062cbe7f, RET 4). The switch value is
// Personality.FormationParams.FormationExitMode (SystemManager+0x1810), compared unsigned against 3.
void BaseFollowerBehavior::UpdateTickPayload(float Delta)
{
    SystemManager* const Sys = NavHelper;
    pub::AI::Personality::FormationUseStruct const& Params = Sys->PersonalityData.FormationParams;
    switch ((unsigned int)Params.FormationExitMode)
    {
    case 1:
        (void)Afterburner.Update(Delta);                    // 062cbd21 [+0x264] slot 1
        // falls through into case 0 (caseD_1 runs straight into caseD_0)
    case 0:
    {
        IObjRW* const Leader = ObjRwFromWatchable(const_cast<Watchable*>(LeadWatcher.target()));
        // 062cbd34-062cbd5b: the cached-physical-bounds latch (+0x157a), set before the call.
        if (Sys->SelfTarget.bPosValid == 0)
        {
            Sys->SelfTarget.bPosValid = 1;
            Sys->SelfTarget.Iface->get_physical_radius(Sys->SelfTarget.CachedRadius,
                                                                  Sys->SelfTarget.CachedPos);
        }
        // 062cbd5e-062cbd92: own centre minus the leader's position (FUN_062e60e0).
        Vector LeaderPos{ 0.0f, 0.0f, 0.0f };
        GetIfacePositionOrZero(&LeaderPos, Leader);
        Vector const Away{ Sys->SelfTarget.CachedPos.x - LeaderPos.x,
                           Sys->SelfTarget.CachedPos.y - LeaderPos.y,
                           Sys->SelfTarget.CachedPos.z - LeaderPos.z };
        Face.SetDirection(Away);                            // 062cbd96 FUN_062d7590
        Face.SetScale94(Params.FormationExitTopTurnBreakAwayThrottle);                   // 062cbda7 FUN_062d9490, +0x1814
        (void)Face.Update(Delta);                           // 062cbdb1 slot 1
        // 062cbdb4-062cbdfb: minus the own forward column (+0x08/+0x14/+0x20 of the transform).
        Matrix const& OwnBasis = Sys->SelfTarget.Iface->get_transform().Rotation;
        Move.SetDirection(Vector{ -OwnBasis.E[0][2], -OwnBasis.E[1][2], -OwnBasis.E[2][2] }); // FUN_062d5890
        (void)Move.Update(Delta);                           // 062cbe05 slot 1
        return;
    }
    case 2:
    {
        // 062cbe12-062cbe44: FUN_062e8c20(&zero) then FUN_062e89a0(-1.0f, 0) on Sys+0x18.
        Vector Zero{ 0.0f, 0.0f, 0.0f };
        Sys->Thrust.SetAxisThrottle(Zero);
        (void)Sys->Thrust.SetThrottle(-1.0f, 0);
        return;
    }
    case 3:
        BreakAway.RollThrottle = Params.FormationExitRollOutrunThrottle;           // 062cbe60 FUN_062d94a0, +0x1818
        BreakAway.BreakStyle = 0;                           // 062cbe69 063275d0 `MOV [ECX+0x34],EAX` (Outrun)
        (void)BreakAway.Update(Delta);                      // 062cbe77 slot 1
        return;
    default:
        return;                                             // caseD_4
    }
}

// Slot 1, LAB_062cb470 (.text:062cb470-062cb52e, RET 0xc).
bool BaseFollowerBehavior::SetContext(void* Owner, void* Manager, void* Sys)
{
    bool const bBound = BindContext(Owner, Manager, Sys);           // 062cb486 FUN_062d2090
    if (bBound)
    {
        LeadWatcher.set(nullptr);                                   // 062cb495-062cb4a1
        // Each Idea gets (this->Owner, this->Sys, Manager); the results are discarded.
        (void)Face.SetContext(OwnerContext, NavHelper, Manager);          // 062cb4bc
        (void)MatchThrottle.SetContext(OwnerContext, NavHelper, Manager); // 062cb4cf
        MatchThrottle.SetFlag3a(true);                              // 062cb4d5 FUN_062d6720
        MatchThrottle.SetFlag3b(true);                              // 062cb4de FUN_062d6730
        (void)Move.SetContext(OwnerContext, NavHelper, Manager);          // 062cb4f8
        (void)BreakAway.SetContext(OwnerContext, NavHelper, Manager);     // 062cb50f
        (void)Afterburner.SetContext(OwnerContext, NavHelper, Manager);   // 062cb526
    }
    return bBound;
}

// Slot 3, FUN_062cb540 (.text:062cb540-062cb5d5).
void BaseFollowerBehavior::Slot3()
{
    Behavior::Slot3();                                              // 062cb544 FUN_062d20d0
    Flag285 = 0;
    Field288 = 0;
    Flag281 = 0;
    Flag280 = 0;
    NavHelper->Thrust.SetStrafeDir(StrafeNone);                     // 062cb56a FUN_062e9030
    NavHelper->Thrust.SetThrustersActive(false);                    // 062cb576 FUN_062e9120
    SetCruiseLock(1, 3);                                            // 062cb581 FUN_062d21d0
    Flag282 = 0;
    DockDelegate = BehaviorMgrPtr->BehaviorDispatchTable[7];        // 062cb58f Manager+0x190
    Flag284 = 0;
    if (NavHelper->SelfTarget.Iface->is_player())        // 062cb5b3 slot 0xbc
        Flag283 = 1;
    GotoDelegate = BehaviorMgrPtr->BehaviorDispatchTable[2];        // 062cb5c7 Manager+0x17c
}

// Slot 4, FUN_062cb5e0 (.text:062cb5e0-062cb6c1).
void BaseFollowerBehavior::Slot4()
{
    Field29c = 0;                                                   // 062cb5ee
    if (Flag281 != 0)
    {
        IObjInspect const* const Owner = ObjInspectOf(OwnerContext);
        // 062cb5f6-062cb627: slot 0xbc (is_player), then cobject() reduced to a ship.
        if (Owner->is_player())
        {
            CObject const* const Object = Owner->cobject();
            if (Object != nullptr &&
                (static_cast<unsigned int>(Object->ObjectClass) & 0x503u) == 0x503u)
                static_cast<CShip*>(const_cast<CObject*>(Object))->request_exit_tradelane();
        }
    }
    Flag281 = 0;                                                    // 062cb638
    Face.Commit();                                                  // 062cb63e
    MatchThrottle.Commit();                                         // 062cb647
    Move.Commit();                                                  // 062cb656
    BreakAway.Commit();                                             // 062cb665
    Afterburner.Commit();                                           // 062cb674
    if (Flag282 != 0 && DockDelegate != nullptr)
        DockDelegate->Slot4();                                      // 062cb68b
    if (Flag284 != 0 && GotoDelegate != nullptr)
        GotoDelegate->Slot4();                                      // 062cb6a2
    Flag282 = 0;
    Flag284 = 0;
    GotoDelegate = nullptr;
    DockDelegate = nullptr;
    Behavior::Slot4();                                              // 062cb6c1 JMP FUN_062d20f0
}

// Slot 8, LAB_062cb6d0: unless the dock delegate runs or a lane order is out, MatchThrottle's
// slot 2 with DeltaTime.
void BaseFollowerBehavior::Slot8()
{
    if (Flag282 != 0 || Flag281 != 0)
        return;
    MatchThrottle.Slot2(DeltaTime);                                 // 062cb6f0 [+0x34]+0x8
}

// Slot 17, LAB_062cbfc0: `MOV ECX,[ECX+0x24]; MOV [ESP+4],0; ADD ECX,0x18; JMP FUN_062e9030`
// -- the base Behavior body with its argument overwritten by 0.
void BaseFollowerBehavior::slide_strafe_burst(StrafeDir Dir)
{
    (void)Dir;
    NavHelper->Thrust.SetStrafeDir(StrafeNone);
}

// Slot 21, LAB_062cf300 (.text:062cf300-062cf339, RET 4).
void BaseFollowerBehavior::Slot21(void* Arg)
{
    unsigned int const LeftId = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(Arg));
    IObjRW* const Lead = ObjRwFromWatchable(const_cast<Watchable*>(LeadWatcher.target()));
    if (Lead == nullptr)
        return;
    if (Lead->get_id() == LeftId)                                   // 062cf321 slot 0x20
        LeadWatcher.set(nullptr);                                   // 062cf32a-062cf333
}

// Slot 22, FUN_062cbea0 (.text:062cbea0-062cbfb0, RET 4).
void BaseFollowerBehavior::Slot22(void* Arg)
{
    FBehaviorManagerEvent const* const Event = static_cast<FBehaviorManagerEvent const*>(Arg);
    if (Flag282 != 0 && DockDelegate != nullptr)
        DockDelegate->Slot22(Arg);                                  // 062cbebf [+0x278]+0x58
    if (Event->Type != 4)
        return;
    FBehaviorTradelaneEvent const* const Lane =
        static_cast<FBehaviorTradelaneEvent const*>(Event->Data);
    // 062cbecb-062cbee9: the record's watched object (FUN_062ecaf0 on NavHelper+0x1364), its
    // id or 0.
    IObjRW* const Watched = NavHelper->GunConvergenceStateBytes.GetCurrentLeader();
    unsigned int WatchedId = 0;
    if (Watched != nullptr)
        WatchedId = Watched->get_id();
    if (Lane->bEnteringLane == 0 && WatchedId == Lane->ShipId && Lane->bExitRequested != 0)
    {
        bool bInLane = false;
        (void)ObjInspectOf(OwnerContext)->is_using_tradelane(&bInLane);   // 062cbf09 slot 0x134
        if (bInLane)
        {
            // 062cbf17-062cbf3b: cobject() reduced to a ship (null when it is not one), then
            // CShip::request_exit_tradelane on it unconditionally -- with ECX == 0 when it is
            // not a ship, a deterministic null dereference reproduced without a guard.
            CObject const* const Object = ObjInspectOf(OwnerContext)->cobject();
            CShip* Ship = nullptr;
            if (Object != nullptr && (Object->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
                Ship = (CShip*)Object;
            Ship->request_exit_tradelane();
        }
        else if (Flag282 != 0 && DockDelegate != nullptr)
        {
            Flag282 = 0;                                            // LAB_062cbf42
            DockDelegate->Slot4();
        }
    }

    // LAB_062cbf62: the own ship (ExternalIface) left a lane.
    if (NavHelper->SelfTarget.Iface->get_id() != Lane->ShipId)
        return;
    if (Lane->bEnteringLane != 0)
        return;
    Flag280 = 1;                                                    // 062cbf89
    if (Flag282 != 0)
        return;
    unsigned int Zero = 0;                                          // the argument slot, reused
    (void)ObjAffectOf(OwnerContext)->slot_60(2, &Zero, 0);          // 062cbfab IObjAffect +0x60
}
