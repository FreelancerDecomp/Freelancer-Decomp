// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- the 0xC8-byte record at SystemManager+0x1364 (FGunConvergenceStateBlock,
// common_systemmanager.h): the owning ship's formation / follow-leader state.
// Retail run 0x062ebbc0-0x062ecd30; every function here is reached with ECX = Sys + 0x1364.
//
// CMN-SPLIT stage 1 (2026-10-14): FUN_062ecaf0 / FUN_062ec6a0 (common_stubs.cpp) became
// members; the construct / update / leader-watcher functions moved here from
// common_aisteer.cpp. CMN-GC (2026-10-14): the FUN_062ec590..650 accessors (from
// common_basewatcher.cpp) and the FollowerDirectiveRecord_* family with FUN_062ec520 (from
// common_behavior.cpp) became members. CMN-S2-AI3 (2026-10-14): the record is typed and the
// construct / update / leader-watcher functions are members too.

#include "common_systemmanager.h"
#include "common_cobject.h"  // CEqObj::get_behavior_interface (SyncFormationLeader)
#include "common_iface.h"    // IObjRW, IBehaviorManager, ObjInspectOf, ObjRwFromWatchable, ObjAffectOf
#include "common_behavior_internal.h" // CmnBehavior::FollowerExternalIfaceIsPlayer (gates A / B)

using CmnBehavior::FollowerExternalIfaceIsPlayer;

namespace
{
    // Retail's `W != 0 && W - 8 != 0` validity test on an embedded watcher.
    bool IsWatchingObject(BaseWatcher const& Watcher)
    {
        Watchable const* const W = Watcher.target();
        return W != nullptr && W != (Watchable const*)8;
    }

    IObjRW* WatchedObject(BaseWatcher const& Watcher)
    {
        return ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    }
}

// formerly FUN_062ecaf0 (item 1869; Common.dll.c 122149-122166). Pure field read: +0xC5 is
// the gate byte (non-zero -> null); the follow-leader slot +0x14 wins over the
// formation-leader slot +0x0C unless it holds one of the sentinels 0 / 8; the raw
// Watchable value minus 8 is the IObjRW (a 0 slot stays null).
IObjRW* FGunConvergenceStateBlock::GetCurrentLeader() const
{
    if (bLeaderSuppressed != 0)
        return nullptr;
    Watchable const* Slot = FollowLeaderWatcher.target();
    if (Slot == nullptr || Slot == (Watchable const*)8)
        Slot = FormationLeaderWatcher.target();
    return ObjRwFromWatchable(const_cast<Watchable*>(Slot));
}

// formerly FUN_062ec6a0 (Common.dll.c 0x62ec6a0): `MOV byte ptr [ECX+0xc5],1; RET`.
void FGunConvergenceStateBlock::SuppressCurrentLeader()
{
    bLeaderSuppressed = 1;
}

// Common.dll.c 0x062ebbc0 (FUN_062ebbc0). Item 1853. The two BaseWatcher members zero
// +0x0C..+0x1B (retail's first four dword stores). Owner, FollowerScratch and +0xBC are
// not written. Retail's own two `BaseWatcher::set_pointer(ptr, nullptr)` calls here are
// dead for a fresh construction (both watchers were zeroed just before) -- documented, not
// reproduced as real calls.
FGunConvergenceStateBlock::FGunConvergenceStateBlock()
{
    FormationNarrowAngle = 0.1f;   // 0x3dcccccd
    FormationWideAngle = 0.2f;     // 0x3e4ccccd
    Field_0xA4 = 200.0f;           // 0x43480000

    bBreakingFormation = 0;
    BreakFormationTimer = 0.0f;
    ForceAttackCycleTimer = 0.0f;
    bOrderPending = 0;
    bValueB0Valid = 0;
    ValueB0 = 0;
    bOrderCommitDirectly = 0;

    bValueB4Valid = 0;
    ValueB4 = 0;
    OrderLaneParam = 0;
    OrderObjectId = 0;
    OrderSecondObjectId = 0;
    bLeaderIdleFormation = 0;
    bForceAttackFormation = 0;
    bLeaderSuppressed = 0;
    bBreakApartNotified = 0;
    bReleaseRequested = 0;
}

// Common.dll.c 0x062ebcb0 (FUN_062ebcb0). Item 1854.
void FGunConvergenceStateBlock::Update(float Dt)
{
    // [this+8] is the owning SystemManager (re-read at every use in retail; nothing in this
    // body writes it). Its +0x1429 / +0x1370 are this record's own bLeaderSuppressed /
    // FormationLeaderWatcher, read through it.
    SystemManager* const Sys = Owner;
    pub::AI::Personality::FormationUseStruct const& Formation = Sys->PersonalityData.FormationParams;

    // 062ebcb7-062ebd03: while bOrderPending and not suppressed, ask the watched leader
    // (the follow watcher when it holds a valid target, else the formation watcher)
    // is_using_tradelane (IObjInspect +0x134) into a byte local pre-set to 0; a false
    // answer clears bOrderPending. A target of 0 or 8 skips the call.
    if (bOrderPending != 0 && bLeaderSuppressed == 0)
    {
        Watchable const* Watched = FollowLeaderWatcher.target();
        if (Watched == nullptr || ObjRwFromWatchable(const_cast<Watchable*>(Watched)) == nullptr)
            Watched = FormationLeaderWatcher.target();

        if (Watched != nullptr)
        {
            IObjRW* const Leader = ObjRwFromWatchable(const_cast<Watchable*>(Watched));
            if (Leader != nullptr)
            {
                bool bUsing = false;                                    // 062ebcf3
                (void)Leader->is_using_tradelane(&bUsing);              // 062ebcf7
                if (!bUsing)
                    bOrderPending = 0;
            }
        }
    }

    // 062ebd09: DL = bForceAttackFormation, kept in a register through 062ebdb9.
    unsigned char const Active = bForceAttackFormation;
    if (Active != 0)
    {
        // 062ebd13-062ebd35: FLD Dt; FADD [this]; FST [this]; FLD [+0x17d0]; FADD [+0x17cc];
        // FXCH; FCOMPP; TEST AH,0x41; JNZ. The unrounded sum is compared with the limit
        // (both double on the x87 stack); only strictly greater (fall-through) zeroes it.
        double const Sum = (double)Dt + (double)ForceAttackCycleTimer;
        ForceAttackCycleTimer = (float)Sum;
        double const Limit = (double)Formation.ForceAttackFormationUnactiveTime
                           + (double)Formation.ForceAttackFormationActiveTime;
        if (Sum > Limit)
            ForceAttackCycleTimer = 0.0f;

        // 062ebd3b-062ebd5b: Sys+0x1429 clear and the Watchable at Sys+0x1370 neither 0 nor 8
        // -> bBreakingFormation = 0.
        if (Sys->GunConvergenceStateBytes.bLeaderSuppressed == 0 &&
            IsWatchingObject(Sys->GunConvergenceStateBytes.FormationLeaderWatcher))
            bBreakingFormation = 0;
    }

    // Sys+0xEC: the break-formation damage trigger (timer table slot 9).
    if (Sys->Timers.Timers[9].Active != 0 && Active != 0 &&
        Sys->GunConvergenceStateBytes.bLeaderSuppressed == 0)
    {
        if (IsWatchingObject(Sys->GunConvergenceStateBytes.FormationLeaderWatcher))
            bBreakingFormation = 1;
    }

    // Sys+0x128: the timer table's BestValueSoFar, against the missile-reaction times.
    float const MissileValue = Sys->Timers.BestValueSoFar;
    if (MissileValue > 0.0f)
    {
        float const Threshold = Sys->Timers.Owner->PersonalityData.FormationParams.BreakFormationMissileReactionTime;
        if (MissileValue <= Threshold && Active != 0 && Sys->GunConvergenceStateBytes.bLeaderSuppressed == 0)
        {
            if (IsWatchingObject(Sys->GunConvergenceStateBytes.FormationLeaderWatcher))
                bBreakingFormation = 1;
        }
    }

    // 062ebddd-062ebde3: Sys+0xFC (the break-apart damage trigger, timer table slot 10)
    // clear -> the 062ebe71 branch, set -> 062ebde9.
    if (Sys->Timers.Timers[10].Active == 0)
    {
        // 062ebe71-062ebe9c: BestValueSoFar > 0 (TEST AH,0x41; JNZ) and <= [[Sys+0x38]+0x17e8]
        // (TEST AH,0x41; JP: greater or unordered jumps). Either test failing goes to
        // 062ebf17, which clears bBreakApartNotified.
        bool DoPulse = false;
        if (MissileValue > 0.0f)
        {
            float const Threshold =
                Sys->Timers.Owner->PersonalityData.FormationParams.BreakApartFormationMissileReactionTime;
            DoPulse = MissileValue <= Threshold;
        }

        if (!DoPulse)
        {
            bBreakApartNotified = 0;
        }
        else if (bBreakApartNotified == 0)
        {
            // 062ebea6-062ebed0: bBreakApartNotified = 1. The Duration local is Sys's OWN
            // +0x17e8 (`MOV EAX,[ECX+0x17e8]` with ECX = Sys), not the [Sys+0x38] copy compared
            // above. The out-param local is pre-zeroed, then the IObjRW behind Sys's Watcher
            // (+8 target - 8, no null test) get_formation_leader (+0xB0).
            bBreakApartNotified = 1;
            float const Duration = Formation.BreakApartFormationMissileReactionTime;
            IObjRW* Leader = nullptr;
            IObjRW* const Own = WatchedObject(Sys->Watcher);
            (void)Own->get_formation_leader(Leader);
            if (Leader != nullptr)                                      // 062ebeda
            {
                // 062ebede-062ebee8: receiver `MOV ECX,[ESI+8]; MOV ECX,[ECX+4]` --
                // Sys->Owner, the IBehaviorManager. Pushes (0, 1, Leader).
                Sys->Owner->QueueNotification(Leader, 1, 0.0f);
                // 062ebeed-062ebefc: `FCOMP 0.0f; TEST AH,0x41; JNZ` -- only strictly
                // greater makes the second call (062ebf10, pushes (Duration, 2, Leader)).
                if (Duration > 0.0f)
                    Sys->Owner->QueueNotification(Leader, 2, Duration);
            }
        }
    }
    else if (bBreakApartNotified == 0)                                   // 062ebde9-062ebdef
    {
        // 062ebdf5-062ebe1f: bBreakApartNotified = 1; Duration = Sys+0x17d8; pre-zeroed
        // out-param; get_formation_leader (+0xB0) on the IObjRW behind Sys's Watcher (no null
        // test).
        bBreakApartNotified = 1;
        float const Duration = Formation.BreakFormationDamageTriggerTime;
        IObjRW* Leader = nullptr;
        IObjRW* const Own = WatchedObject(Sys->Watcher);
        (void)Own->get_formation_leader(Leader);
        if (Leader != nullptr)                                          // 062ebe29
        {
            // 062ebe31-062ebe3b / 062ebe40-062ebe67: same receiver (Sys->Owner) and the same
            // strictly-greater gate on the second call.
            Sys->Owner->QueueNotification(Leader, 1, 0.0f);
            if (Duration > 0.0f)
                Sys->Owner->QueueNotification(Leader, 2, Duration);
        }
    }

    // 062ebf1d-062ebf37: bBreakingFormation set -> +4 = float(Dt + +4) (FLD/FADD/FSTP),
    // else +4 = 0.
    if (bBreakingFormation != 0)
        BreakFormationTimer = (float)((double)Dt + (double)BreakFormationTimer);
    else
        BreakFormationTimer = 0.0f;
}

// ----------------------------------------------------------------------------
// The two leader watchers. +0x0C watches the FORMATION leader, +0x14 the FOLLOW leader;
// the bytes +0xBC..+0xC5 are per-leader state dropped whenever the watched leader changes
// identity.
// ----------------------------------------------------------------------------

// .text:062ec917-062ec947 / 062eca1f-062eca4f: nine byte stores of 0 to +0xBC..+0xC4.
// Both callers then fall into the +0xC5 store their null-Target arm also reaches.
static void ClearLeaderStateBytes(FGunConvergenceStateBlock& B)
{
    B.Field_0xBC = 0;
    B.bOrderPending = 0;
    B.bValueB0Valid = 0;
    B.bValueB4Valid = 0;
    B.bBreakingFormation = 0;
    B.bLeaderIdleFormation = 0;
    B.bOrderCommitDirectly = 0;
    B.bForceAttackFormation = 0;
    B.bBreakApartNotified = 0;
}

// FUN_062ec870. Common.dll.txt 252105-252201.
void FGunConvergenceStateBlock::SetFormationLeader(IObjRW* Target)
{
    if (Target != nullptr)
    {
        // 062ec884-062ec8a5: our own ship's id first, then Target's.
        unsigned int const SelfId = Owner->SelfTarget.Iface->get_id();
        if (Target->get_id() == SelfId)
            return;

        // 062ec8ab-062ec8e2: Target's id first, then the watched leader's.
        if (IsWatchingObject(FormationLeaderWatcher))
        {
            unsigned int const TargetId = Target->get_id();
            if (WatchedObject(FormationLeaderWatcher)->get_id() != TargetId)
                bLeaderSuppressed = 0;
        }

        // 062ec8e8-062ec915: the watched leader's id first, then Target's.
        bool bSameLeader = false;
        if (IsWatchingObject(FormationLeaderWatcher))
        {
            unsigned int const WatchedId = WatchedObject(FormationLeaderWatcher)->get_id();
            bSameLeader = Target->get_id() == WatchedId;
        }

        if (!bSameLeader)
        {
            ClearLeaderStateBytes(*this);
            bLeaderSuppressed = 0;
        }
    }
    else
    {
        bLeaderSuppressed = 0; // 062ec94d
    }

    // 062ec953-062ec962: `Target ? Target + 8 : 0`, set_pointer only when it differs --
    // BaseWatcher::set is that same compare-then-set_pointer.
    FormationLeaderWatcher.set(static_cast<Watchable*>(Target));
}

// FUN_062ec970. Common.dll.txt 252211-252335.
void FGunConvergenceStateBlock::SetFollowLeader(IObjRW* Target)
{
    if (Target != nullptr)
    {
        // 062ec984-062ec9b6: Target is our own ship -- stop following. Retail's
        // `if ([+0x14] != 0) set_pointer(0)` is BaseWatcher::set(nullptr).
        unsigned int const SelfId = Owner->SelfTarget.Iface->get_id();
        if (Target->get_id() == SelfId)
        {
            FollowLeaderWatcher.set(nullptr);
        }
        else
        {
            // 062ec9bb-062ec9ea: the watched leader's id first, then Target's.
            if (IsWatchingObject(FollowLeaderWatcher))
            {
                unsigned int const WatchedId = WatchedObject(FollowLeaderWatcher)->get_id();
                if (WatchedId != Target->get_id())
                    bLeaderSuppressed = 0;
            }

            // 062ec9f0-062eca1d: same order.
            bool bSameLeader = false;
            if (IsWatchingObject(FollowLeaderWatcher))
            {
                unsigned int const WatchedId = WatchedObject(FollowLeaderWatcher)->get_id();
                bSameLeader = Target->get_id() == WatchedId;
            }

            if (!bSameLeader)
            {
                ClearLeaderStateBytes(*this);
                bLeaderSuppressed = 0;
            }

            FollowLeaderWatcher.set(static_cast<Watchable*>(Target)); // 062eca5b-062eca70
        }
    }
    else
    {
        bLeaderSuppressed = 0;            // 062eca55
        FollowLeaderWatcher.set(nullptr); // 062eca5b-062eca70 with EAX = 0
    }

    // 062eca75-062eca9f: tail JMP to the owning ship's IObjAffect slot 9 (+0x24),
    // `[[this+8]+8] - 8 + 4`, with the follow watcher's object (or null). The owning
    // ship is not null-tested; retail's null arm reads [4].
    IObjRW* const FollowLeader = WatchedObject(FollowLeaderWatcher);
    IObjRW* const OwnerObj = WatchedObject(Owner->Watcher);
    static_cast<IObjAffect*>(OwnerObj)->set_follow_leader(FollowLeader);
}

// FUN_062ec6b0. Common.dll.txt 251918-252089.
void FGunConvergenceStateBlock::SyncFormationLeader()
{
    // 062ec6b5-062ec6d6: Owner, its watched ship and that ship's -8 all non-null,
    // else LAB_062ec810.
    if (Owner != nullptr && IsWatchingObject(Owner->Watcher))
    {
        IObjRW* const OwnerObj = WatchedObject(Owner->Watcher);

        IObjRW* FormationLeader = nullptr; // local_4 = EBX = 0
        OwnerObj->get_formation_leader(FormationLeader); // +0xB0

        if (FormationLeader != nullptr)
        {
            // 062ec701-062ec725: our own ship's id first, then the leader's.
            unsigned int const SelfId = Owner->SelfTarget.Iface->get_id();
            bool const bLeaderIsSelf = FormationLeader->get_id() == SelfId;

            if (!bLeaderIsSelf && !FormationLeader->is_dying()) // +0x38
            {
                SetFormationLeader(FormationLeader);
                bLeaderIdleFormation = 0;

                // 062ec74e-062ec76f: the CEqObj cast (null when the class mask misses)
                // feeds get_behavior_interface with no null test -- it reads [this+0x194].
                CObject const* const LeaderObj = FormationLeader->cobject(); // +0x150
                CEqObj* LeaderEq = nullptr;
                if (LeaderObj != nullptr && (LeaderObj->ObjectClass & CObject::ClassEqObj) == CObject::ClassEqObj)
                    LeaderEq = (CEqObj*)LeaderObj;

                IBehaviorManager* const LeaderMgr = LeaderEq->get_behavior_interface();
                if (LeaderMgr == nullptr)
                {
                    bReleaseRequested = 0; // LAB_062ec85d
                    return;
                }

                // 062ec77e-062ec7b3: bLeaderIdleFormation = (directive[0] == 1 && directive[1] == 0).
                int const* const Directive = (int const*)LeaderMgr->get_current_directive();
                bool const bIdleFormation = Directive[0] == 1 && Directive[1] == 0;
                bReleaseRequested = 0;
                bLeaderIdleFormation = bIdleFormation ? 1 : 0;
                return;
            }

            // LAB_062ec7b4: is_dying is asked again (a second call on the dying path).
            if (FormationLeader->is_dying())
            {
                IObjRW* const Ship = WatchedObject(Owner->Watcher);
                static_cast<IObjAffect*>(Ship)->slot_6c(5, 0); // +0x6C
                SetFormationLeader(nullptr);
                bReleaseRequested = 0;
                return;
            }
        }
    }

    // LAB_062ec810. The owning ship is re-read without a null test (retail's null arm
    // reads [4]).
    if (bReleaseRequested == 0 && bLeaderSuppressed == 0 && IsWatchingObject(FormationLeaderWatcher) &&
        bValueB0Valid == 0)
    {
        IObjRW* const Ship = WatchedObject(Owner->Watcher);
        static_cast<IObjAffect*>(Ship)->slot_6c(5, 0);
    }

    SetFormationLeader(nullptr); // LAB_062ec855
    bReleaseRequested = 0;       // LAB_062ec85d
}

// ----------------------------------------------------------------------------
// FGunConvergenceStateBlock accessors (CMN-GC 2026-10-14: moved here from
// common_basewatcher.cpp and common_behavior.cpp; every retail call site passes
// ECX = SystemManager + 0x1364).
// ----------------------------------------------------------------------------

// formerly FUN_062ec590.
// Common.dll.txt .text:062ec590-062ec5a4, `RET 8`: `MOV EAX,[ESP+4]; MOV DL,[ESP+8];
// MOV [ECX+0xb4],EAX; MOV [ECX+0xbf],DL`. The flag is stored as given, not derived from the
// value. Sole caller: IBehaviorManager::Slot00 case 5 (.text:062d424d).
void FGunConvergenceStateBlock::SetValueB4(unsigned int Value, unsigned char Flag)
{
    ValueB4 = Value;          // .text:062ec598
    bValueB4Valid = Flag;     // .text:062ec59e
}

// formerly FUN_062ec5b0 (Common.dll.txt:251779-251792): `if (+0xbf) *Out = +0xb4;
// return +0xbf;` (bool in AL). Single call site (0x062cb8ec): BaseFollowerBehavior::Slot7.
unsigned char FGunConvergenceStateBlock::TryGetValueB4(unsigned int* Out) const
{
    unsigned char const Valid = bValueB4Valid;
    if (Valid)
        *Out = ValueB4;
    return Valid;
}

// formerly FUN_062ec5d0 (Common.dll.txt:251795-251814), RET 0x14:
//   +0xbd = P1; +0xa8 = P2; +0xac = P3; +0xc2 = P4; +0xb8 = P5;
// Sole call site (0x062d41e6): IBehaviorManager::Slot00 case 4.
void FGunConvergenceStateBlock::SetDirectiveOrder(unsigned char P1, unsigned int P2, unsigned int P3,
                                                  unsigned char P4, unsigned int P5)
{
    bOrderPending = P1;
    OrderObjectId = P2;
    OrderSecondObjectId = P3;
    bOrderCommitDirectly = P4;
    OrderLaneParam = P5;
}

// formerly FUN_062ec610 (Common.dll.txt:251827-251843): packs +0xa8 / +0xac / +0xbd / +0xc2 /
// +0xb8 into the caller's 0x10-byte block at +0x0 / +0x4 / +0x8 / +0x9 / +0xc. Sole call
// site (0x062cbac4): BaseFollowerBehavior::Slot7 (its FollowerDirectiveOrder block).
void FGunConvergenceStateBlock::GetDirectiveOrder(unsigned char* Out) const
{
    *(unsigned int*)(Out + 0x0) = OrderObjectId;
    *(unsigned int*)(Out + 0x4) = OrderSecondObjectId;
    Out[0x8] = bOrderPending;
    Out[0x9] = bOrderCommitDirectly;
    *(unsigned int*)(Out + 0xc) = OrderLaneParam;
}

// formerly FUN_062ec650.
// Common.dll.txt .text:062ec650-062ec676, `RET 4`: value == 0 -> +0xb0 = 0 and +0xbe = 0
// (062ec65a-062ec660); otherwise +0xb0 = value and +0xbe = 1 (062ec669-062ec66f). Callers:
// IBehaviorManager::Slot00 case 3 (.text:062d402c / 062d40d7) and BaseFollowerBehavior::Slot7
// (.text:062cbb18).
void FGunConvergenceStateBlock::SetValueB0(unsigned int Value)
{
    if (Value == 0)
    {
        ValueB0 = 0;
        bValueB0Valid = 0;
        return;
    }
    ValueB0 = Value;
    bValueB0Valid = 1;
}

// formerly CmnBehavior::FollowerDirectiveRecord_TryGetPointerB0.
// FUN_062ec680 (Common.dll.txt 251887-251898), RET 4. The getter half of FUN_062ec650;
// BaseFollowerBehavior::Slot7 is its only caller.
bool FGunConvergenceStateBlock::TryGetValueB0(unsigned int* Out) const
{
    if (bValueB0Valid != 0)
        *Out = ValueB0;
    return bValueB0Valid != 0;
}

// FUN_062ecc00 (Common.dll.txt 252515-252534), 20 instructions, RET 0. Gate B. The owner is
// read out of +0x08 (`MOV EAX,[ESI+8]` at .text:062ecc03).
bool FGunConvergenceStateBlock::FollowerDirectiveGateB() const
{
    SystemManager* const Sys = Owner;
    if (FollowerExternalIfaceIsPlayer(Sys))
        return false;
    if (bBreakingFormation == 0)
        return false;
    // .text:062ecc2c-062ecc3a: FLD [this+4]; FCOMP [Sys+0x181c]; FNSTSW AX;
    // TEST AH,0x5; JP <return false>. Mask 0x5 is C0|C2: AH&5 == 0x00 when ST0 > src
    // (even parity -> JP taken), 0x01 when ST0 < src (odd -> not taken), 0x40&5 == 0
    // when equal (JP taken) and 0x05 when unordered (even -> taken). So the JP
    // branches away on ">=-or-unordered", i.e. the function returns true exactly when
    // BreakFormationTimer is strictly less than FormationExitMaxTime.
    return BreakFormationTimer < Sys->PersonalityData.FormationParams.FormationExitMaxTime;
}

// FUN_062ecb20 (Common.dll.txt 252436-252514), 60 instructions, RET 0. Gate A.
// Callers: BuzzBehavior::EvaluateReadiness (.text:062c6121), EvadeBehavior (062c7b36),
// FaceBehavior (062c8c6a), StrafeBehavior (062c9735), TrailBehavior (062ca834) and
// BaseFollowerBehavior::Slot7 (062cbc64), all on NavHelper+0x1364.
bool FGunConvergenceStateBlock::FollowerDirectiveGateA() const
{
    SystemManager* const Sys = Owner;   // [ESI+8], no null test
    if (FollowerExternalIfaceIsPlayer(Sys))
        return false;
    // Sys+0x1958: JobParams.bForceAttackFormation.
    if (Sys->PersonalityData.JobParams.bForceAttackFormation == 0)
        return false;
    // Sys+0x1429 is this record's +0xc5 (0x1364 + 0xc5 == 0x1429) -- the same gate byte
    // FUN_062ecaf0 reads.
    if (bLeaderSuppressed != 0)
        return false;
    // Sys+0x1370 is +0xc: the formation-leader watcher, downcast to the whole IObjRW by -8.
    IObjRW* const Watched = WatchedObject(FormationLeaderWatcher);
    if (Watched == nullptr)
        return false;

    // .text:062ecb7b-062ecb8d: `if (+0xc0 != 0) goto LAB_062ecbbb; if (+0xc3 == 0) goto
    // LAB_062ecbbb;` -- so the fall-through below is reached ONLY on
    // (!bBreakingFormation && bForceAttackFormation), and LAB_062ecbbb on the complement.
    if (bBreakingFormation == 0 && bForceAttackFormation != 0)
    {
        IBehaviorManager* const Mgr = GetBehaviorManager(Watched);
        // .text:062ecb9c `CMP dword ptr [EAX+0xc0],0xf`: the leader's current behavior.
        if (Mgr != nullptr && Mgr->CurrentBehaviorIndex == 0xf)
            return false;
        // LAB_062ecba5. .text:062ecba8-062ecbb5: FLD [this]; FCOMP [Sys+0x17cc];
        // FNSTSW AX; TEST AH,0x41; JP <return false>. Mask 0x41 is C0|C3:
        // AH&0x41 == 0x00 when ST0 > src (zero bits, even parity -> JP taken), 0x01
        // when ST0 < src (odd -> not taken), 0x40 when equal (odd -> not taken) and
        // 0x41 when unordered (two bits, even -> taken). So the JP branches away on
        // "strictly greater or unordered" and this returns true exactly when
        // ForceAttackCycleTimer <= ForceAttackFormationActiveTime.
        return ForceAttackCycleTimer <= Sys->PersonalityData.FormationParams.ForceAttackFormationActiveTime;
    }

    // LAB_062ecbbb. `MOV ECX,[EAX+4]` with EAX still the SystemManager -- its own
    // Owner (the IBehaviorManager) -- then that object's +0xc0, no null test.
    if (Sys->Owner->CurrentBehaviorIndex != 0xa)
        return false;
    if (FollowerExternalIfaceIsPlayer(Sys))
        return false;
    if (bBreakingFormation == 0)
        return false;
    // .text:062ecbe8-062ecbf6: FLD [this+4]; FCOMP [Sys+0x181c]; TEST AH,0x5; JP
    // <return false> -- same mask and same jump as gate B, so likewise "true only
    // when strictly less".
    return BreakFormationTimer < Sys->PersonalityData.FormationParams.FormationExitMaxTime;
}

// The formation leader of the ship the record belongs to: `[[this+8]+8]` is
// SystemManager::Watcher's Watchable, `-8` (with retail's own null arm, .text:062ecc74-
// 062ecc7f) the IObjRW, and slot 0xb0 get_formation_leader fills a zeroed local. Retail
// dispatches through the downcast without testing it.
static IObjRW* FormationLeaderOfRecordOwner(SystemManager* Sys)
{
    IObjRW* const Self = WatchedObject(Sys->Watcher);
    IObjRW* Leader = nullptr;              // 062ecc6a `MOV [ESP+4],0`
    Self->get_formation_leader(Leader);    // 062ecc86 CALL [EAX+0xb0]
    return Leader;
}

// FUN_062ecc60, Common.dll.txt .text:062ecc60-062eccce (41 instructions, RET 4).
void FGunConvergenceStateBlock::NotifyFormationLeader1(float Delay)
{
    IObjRW* const Leader = FormationLeaderOfRecordOwner(Owner);
    if (Leader == nullptr)                                         // 062ecc90
        return;
    Owner->Owner->QueueNotification(Leader, 1, 0.0f);              // 062ecc94-062ecc9f
    // 062ecca4-062eccb3: FLD Delay; FCOMP 0.0f; TEST AH,0x41; JNZ <ret> -- the JNZ is
    // taken when Delay <= 0 or unordered, so the second notification needs Delay > 0.
    if (Delay > 0.0f)
        Owner->Owner->QueueNotification(Leader, 2, Delay);         // 062eccb5-062eccc7
}

// FUN_062ecce0, .text:062ecce0-062ecd26 (28 instructions, RET 0).
void FGunConvergenceStateBlock::NotifyFormationLeader2()
{
    IObjRW* const Leader = FormationLeaderOfRecordOwner(Owner);
    if (Leader == nullptr)                                         // 062ecd10
        return;
    Owner->Owner->QueueNotification(Leader, 2, 0.0f);              // 062ecd14-062ecd1f
}

// formerly FollowerDirectiveRecord_SetFlagC3.
// FUN_062ecc50, .text:062ecc50-062ecc5a (3 instructions, RET 4).
void FGunConvergenceStateBlock::SetFlagC3(unsigned char Value)
{
    bForceAttackFormation = Value;   // MOV AL,[ESP+4]; MOV [ECX+0xc3],AL
}

// formerly CmnBehavior::FollowerDirectiveRecord_OverrideTarget.
// FUN_062ecad0 (.text:062ecad0-062ecae7): the follow leader as an IObjRW, or null when the
// +0xc5 gate byte is set. Sibling of FUN_062ecab0 below.
IObjRW* FGunConvergenceStateBlock::GetFollowLeader() const
{
    if (bLeaderSuppressed != 0)
        return nullptr;
    return WatchedObject(FollowLeaderWatcher);
}

// FUN_062ecd30 (.text:062ecd30-062ecd8c, RET 0): +0xc6 = 1, then the record owner's ship
// (SystemManager::Watcher's target - 8) gets IObjAffect slot 0x60 (1, &own id, -1). With no
// watched ship retail dispatches through address 4 (`LEA ECX,[0+4]`, 062ecd77-062ecd87)
// and faults; the port dispatches through the same null-derived pointer, with no guard.
// formerly CmnBehavior::FollowerDirectiveRecord_RequestRelease.
int FGunConvergenceStateBlock::RequestRelease()
{
    SystemManager* const Sys = Owner;
    bReleaseRequested = 1;                                          // 062ecd37
    unsigned int const Id = Sys->SelfTarget.Iface->get_id();        // 062ecd4d
    IObjRW* const Self = WatchedObject(Sys->Watcher);
    return ObjAffectOf(Self)->slot_60(1, &Id, -1);                  // 062ecd71 / 062ecd87
}

// formerly CmnBehavior::FollowerDirectiveRecord_DefaultTarget.
// FUN_062ecab0 (.text:062ecab0-062ecac7, 3 call sites: 062c56f9, 062cc064, 062cc483): the
// formation leader, or null when the +0xc5 gate byte is set.
IObjRW* FGunConvergenceStateBlock::GetFormationLeader() const
{
    if (bLeaderSuppressed != 0)
        return nullptr;
    return WatchedObject(FormationLeaderWatcher);
}

// Common.dll.txt:251688-251749 (item 1856, FUN_062ec520, 52 insns), RET 4. Both call sites
// load the receiver as `MOV ECX,[ESI+0x24]; ADD ECX,0x1364` (GotoBehavior::Update 282027,
// GuideBehavior::Update 206292).
//
// The owning SystemManager (+0x08) is dereferenced with no test (062ec520-062ec523); only
// its watched object and the -8 are tested. That object's slot +0x14C get_formation_followers
// fills FollowerScratch (MaxCount 0x20); each follower's get_id (+0x20) is compared with
// WantedId.
//
// One retail quirk preserved verbatim: the count guard at 251719-251720 is `TEST EBX,EBX /
// JBE`, which after a TEST (CF always clear) tests ZF alone, so it rejects only an
// exactly-zero count; the loop counter is then compared with `CMP ESI,EBX / JC`, i.e.
// unsigned. Modelled as an unsigned count with a zero check.
bool FGunConvergenceStateBlock::IsIdAmongOwnerFormationFollowers(unsigned WantedId)
{
    IObjRW* const OwnerObj = WatchedObject(Owner->Watcher);
    if (OwnerObj == nullptr)
        return false;

    unsigned const Count = (unsigned)OwnerObj->get_formation_followers(FollowerScratch, 0x20);
    if (Count == 0)
        return false;

    for (unsigned Index = 0; Index < Count; ++Index)
    {
        if (FollowerScratch[Index] != nullptr && FollowerScratch[Index]->get_id() == WantedId)
            return true;
    }
    return false;
}
