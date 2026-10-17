// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_guidebehavior.h"
#include "common_pathfind.h" // PathfindManager (IBehaviorManager+0x0C)
#include "common_systemmanager.h" // SystemManager::Targeting / ExternalIface (NotifyManagerGuideEvent)

// Update()'s steering/geometry leaves. ClampedDot3 (FUN_062e1630),
// GetIfacePositionOrZero (FUN_062e60e0), GetInterceptClearanceOfIface (FUN_062e4bb0)
// and GetSignedRadialClearanceToPoint (FUN_062e4ea0) are all declared here; Distance3
// (FUN_062e4f20) and VecNormalize3 (FUN_062e1680) arrive via common_ini.h, and
// ObjInspectOf/ObjRwFromWatchable via common_iface.h, both already pulled in by
// common_behavior.h.
#include "common_aisteer.h"

#include <cmath> // std::acos -- Update()'s 062cfa2e `_CIacos` call
#include <cstdint>
#include <cstring>

// FUN_062e74a0 (item 1798): the checked id lookup through the IObjDB at SystemManager+0x34,
// SystemManagerTimerTable::FindVerifiedObject.

// GuideBehavior -- see common_guidebehavior.h for the full RTTI/layout writeup this pass
// relied on. Ported from EXE/Disasm/Common.dll.txt (raw disassembly is authoritative in this
// module over Ghidra's Common.dll.c decompilation -- see this marathon's "ground truth
// precedence" rule).

// FUN_062cf230 (Common.dll.txt 205669-205723) -- constructor. Calls the shared base-init
// field-initialising FUN_062d2020 (it writes +0x04..+0x29, see Behavior in common_behavior.h) first, zeroes WatcherA, default-constructs the two REAL embedded
// sub-objects (GotoOp @+0x68, FollowOp @+0xd8 -- both already-ported types, common_pub.h),
// zeroes WatcherB, and finally (implicitly, via C++ construction) installs the vtable,
// matching retail's own `MOV dword ptr [ESI],PTR_FUN_0639feb4` followed by the five
// remaining scalar field writes.
GuideBehavior::GuideBehavior(int InTypeId)
    : Behavior()
    , TypeId(InTypeId)
    , CachedHandle08(0)
    , ModeSelector0xc(0)
    , UpdateResult10(0)
    , OwnerContextPtr14(nullptr)
    , Opaque18{}
    , DeltaTime1c(0.0f)
    , BehaviorMgrPtr20(nullptr)
    , NavHelperPtr24(nullptr)
    , Flags28(0)
    , Flag29(0)
    , Pad2a{}
    , WatcherA()
    , Field34(0.0f)
    , Flag38(1)
    , Pad39{}
    , Field3c(0.0f)
    , Field40(0)
    , Field44(0.0f)
    , Field48{}
    , Field54(0.0f)
    , Field58(0.0f)
    , Field5c(0.0f)
    , CachedPtr60(nullptr)
    , CachedPtr64(nullptr)
    , GotoOp()
    , FollowOp()
    , WatcherB()
    , Field104(10.0f)
    , Field108(0.0f)
    , Field10c(0.0f)
    , Field110(0.0f)
    , FieldLow114(-10.0f)
    , FieldHigh118(10.0f)
    , AngleThreshold11c(0.39269909f)
    , Flag120(0)
{
    ConstructBase(InTypeId);   // FUN_062d2020 -- Behavior's own field initialisation
    // retail: the ctor also performs `CMP dword ptr [this+0xfc],0; JZ skip; CALL
    // BaseWatcher::set_pointer(this+0xfc, nullptr)` right after installing the vtable --
    // CONFIRMED DEAD CODE (matching this marathon's LootBehavior ctor precedent): the
    // compared dword was JUST zeroed by this same ctor a few instructions earlier (WatcherB's
    // own zero-init) and nothing in between writes it, so the branch always takes the
    // "already null" path. Not reproduced here.
}

// Real (non-thunk) destructor path. FUN_062cf340 (Common.dll.txt 205775-205793) is the
// scalar-deleting-dtor thunk: it calls FUN_062cf360 (the REAL cleanup body, 205796-205835)
// then conditionally frees. FUN_062cf360 clears WatcherB (+0xfc) then WatcherA (+0x2c) --
// note retail clears them in that order -- before tail-calling the shared
// FUN_062d2080(this,-1) effect (FULLPORT-DEFERRED, same family-shared teardown helper
// documented on every other sibling's own dtor).
GuideBehavior::~GuideBehavior()
{
    WatcherB.set(nullptr);
    WatcherA.set(nullptr);
}

// Slot 1, LAB_062cf3e0: `JMP FUN_062d2090`.
bool GuideBehavior::SetContext(void* Owner, void* Manager, void* Sys)
{
    return BindContext(Owner, Manager, Sys);
}

// Slot 3, LAB_062cf3f0 (.text:062cf3f0-062cf4eb).
void GuideBehavior::Slot3()
{
    Behavior::Slot3();                                              // 062cf3f7 FUN_062d20d0
    CachedPtr60 = BehaviorMgrPtr20->BehaviorDispatchTable[2];       // 062cf3ff Manager+0x17c (Goto)
    CachedPtr64 = BehaviorMgrPtr20->BehaviorDispatchTable[17];      // 062cf408 Manager+0x1b8 (Follow)
    (void)CachedPtr60->Slot10(&GotoOp);                             // 062cf417
    CachedPtr60->Slot3();                                           // 062cf41f
    (void)CachedPtr64->Slot10(&FollowOp);                           // 062cf42e
    // 062cf431-062cf44d: on the follow behavior itself (retail writes its offsets directly):
    // +0x28c = 1, MatchThrottle.SetFlag39(1) (FUN_062d6710), Move.SetField44(2) (FUN_062d88b0).
    BaseFollowerBehavior* const Follow = static_cast<BaseFollowerBehavior*>(CachedPtr64);
    Follow->Flag28c = 1;
    Follow->MatchThrottle.SetFlag39(true);
    Follow->Move.SetField44(2);
    CachedPtr64->Slot3();                                           // 062cf457
    Field48 = GotoOp.Destination;                                   // 062cf45a-062cf46f (GotoOp+0x10)
    Field54 = GotoOp.Field_50;                                      // 062cf472 (GotoOp+0x50)
    Field108 = 0.0f;                                                // 062cf47b
    // 062cf485-062cf4db: |WatcherA position - Field48|, the sum ((dx*dx + dz*dz) + dy*dy) and
    // FSQRT on the x87 stack, one rounding into +0x10c.
    Vector Pos{ 0.0f, 0.0f, 0.0f };
    GetIfacePositionOrZero(&Pos, ObjRwFromWatchable(const_cast<Watchable*>(WatcherA.target())));
    double const Dx = static_cast<double>(Pos.x) - Field48.x;
    double const Dy = static_cast<double>(Pos.y) - Field48.y;
    double const Dz = static_cast<double>(Pos.z) - Field48.z;
    Flag120 = Flag38;                                               // 062cf4b7
    Field58 = 0.0f;                                                 // 062cf4bf
    Field5c = 0.0f;                                                 // 062cf4c8
    Field10c = static_cast<float>(std::sqrt((Dx * Dx + Dz * Dz) + Dy * Dy));
}

// Slot 4, FUN_062cf4f0 (.text:062cf4f0-062cf51e).
void GuideBehavior::Slot4()
{
    if (CachedPtr60 != nullptr)
    {
        CachedPtr60->Slot4();                                       // 062cf4fc
        Behavior_ReleaseDirectiveTarget(CachedPtr60);               // 062cf502 FUN_062d21c0
    }
    if (CachedPtr64 != nullptr)
    {
        CachedPtr64->Slot4();                                       // 062cf510
        Behavior_ReleaseDirectiveTarget(CachedPtr64);               // 062cf516 FUN_062d21c0
    }
    Behavior::Slot4();                                              // 062cf51e JMP FUN_062d20f0
}

// Slot 5, FUN_062cf530 (.text:062cf530-062cf5d3, RET 4, argument unread).
int GuideBehavior::Slot5(float Value)
{
    (void)Value;
    if ((Flags28 & 0x2) != 0)
    {
        IObjRW* const Lead = ObjRwFromWatchable(const_cast<Watchable*>(WatcherA.target()));
        if (Lead != nullptr)
        {
            // 062cf586-062cf595: FUN_062e4500(Sys+0x14fc, Lead, Field34).
            if (IsWithinInterceptRangeOfIface(&NavHelperPtr24->SelfTarget, Lead, Field34))
                ModeSelector0xc = (Flag29 != 0) ? 3 : 1;
            else
                ModeSelector0xc = (Flag29 != 0) ? 3 : 0;
            return ModeSelector0xc;
        }
        BehaviorMgrPtr20->SetDirectiveResult(0xe, 1);               // 062cf571 FUN_062d29a0
    }
    ModeSelector0xc = (Flag29 != 0) ? 3 : 2;                        // LAB_062cf539
    return ModeSelector0xc;
}

// Slot 10, LAB_062cfc70 (.text:062cfc70-062cfe15, RET 4). The argument is a
// pub::AI::DirectiveGuideOp; the FollowParams fields it reads (+0x8c/+0x90..+0x98) and the
// GotoParams fields (+0x68/+0x6c/+0x70) are taken by raw offset off the op.
bool GuideBehavior::Slot10(void* DirectiveOp)
{
    pub::AI::DirectiveGuideOp const* const Op = static_cast<pub::AI::DirectiveGuideOp const*>(DirectiveOp);
    unsigned char const* const OpBytes = static_cast<unsigned char const*>(DirectiveOp);
    CachedPtr60 = BehaviorMgrPtr20->BehaviorDispatchTable[2];       // 062cfc83
    CachedPtr64 = BehaviorMgrPtr20->BehaviorDispatchTable[17];      // 062cfc8d
    // 062cfc90-062cfcab: REP MOVSD of 0x1c and 9 dwords -- whole-object copies, vptr included.
    std::memcpy(static_cast<void*>(&GotoOp), &Op->GotoParams, sizeof(GotoOp));
    std::memcpy(static_cast<void*>(&FollowOp), &Op->FollowParams, sizeof(FollowOp));
    bool bResult = true;                                            // 062cfcba
    // 062cfcad-062cfcda: WatcherB = the object GotoOp+0x1c names (FUN_062e74a0).
    IObjRW* const GotoTarget = NavHelperPtr24->Timers.FindVerifiedObject(
        (unsigned const*)((unsigned char*)this + 0x84));
    WatcherB.set(GotoTarget != nullptr ? static_cast<Watchable const*>(GotoTarget) : nullptr);
    Field104 = Op->Field_A4;                                        // 062cfce5
    AngleThreshold11c = Op->Field_A8;                               // 062cfcf1
    Field110 = static_cast<float>(Op->Field_AC);                    // 062cfcf7 FILD
    FieldHigh118 = Op->Field_B0;                                    // 062cfd09
    FieldLow114 = Op->Field_B4;                                     // 062cfd18
    if (*reinterpret_cast<int const*>(OpBytes + 0x68) != 0)         // 062cfd1e
    {
        IObjRW* const Escort = NavHelperPtr24->Timers.FindVerifiedObject(
            (unsigned const*)(OpBytes + 0x68));
        WatcherA.set(Escort != nullptr ? static_cast<Watchable const*>(Escort) : nullptr);
        if (ObjRwFromWatchable(const_cast<Watchable*>(WatcherA.target())) != nullptr)
        {
            Field3c = *reinterpret_cast<float const*>(OpBytes + 0x6c);         // 062cfd65
            Field40 = *reinterpret_cast<unsigned const*>(OpBytes + 0x70);      // 062cfd6b
            // 062cfd6e-062cfdab: |op+0x90..+0x98| on the x87 stack ((x*x + y*y) + z*z); not
            // greater than 0.0f (TEST AH,0x41 JNZ) -> +0x44 = op+0x8c, else len + 100.0f
            // (DAT_0639fb58), one rounding.
            float const* const V = reinterpret_cast<float const*>(OpBytes + 0x90);
            double const Len = std::sqrt((static_cast<double>(V[0]) * V[0]
                                          + static_cast<double>(V[1]) * V[1])
                                         + static_cast<double>(V[2]) * V[2]);
            if (Len > 0.0)
                Field44 = static_cast<float>(Len + 100.0);
            else
                Field44 = *reinterpret_cast<float const*>(OpBytes + 0x8c);
            if (!CachedPtr60->Slot10(&GotoOp))                      // 062cfdc4
                bResult = false;
            if (!CachedPtr64->Slot10(&FollowOp))                    // 062cfddb
                bResult = false;
        }
        else
        {
            bResult = false;                                        // LAB_062cfde4
        }
    }
    // LAB_062cfded.
    Field34 = Op->Field_B8;                                         // 062cfdf3
    Flag38 = Op->Field_A0;                                          // 062cfdff
    Flags28 = static_cast<unsigned char>((Flags28 & ~0x2u) | (bResult ? 0x2u : 0u));   // 062cfe02-062cfe0f
    return bResult;
}

// Slot 21, LAB_062cf300 (.text:062cf300-062cf339, RET 4).
void GuideBehavior::Slot21(void* Arg)
{
    unsigned int const LeftId = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(Arg));
    IObjRW* const Lead = ObjRwFromWatchable(const_cast<Watchable*>(WatcherA.target()));
    if (Lead == nullptr)
        return;
    if (Lead->get_id() == LeftId)                                   // 062cf321 slot 0x20
        WatcherA.set(nullptr);
}

// Slot 24, LAB_062cf2f0: `MOV EAX,[ECX+0x2c]; TEST; JZ; ADD EAX,-8`.
IObjRW const* GuideBehavior::get_target()
{
    return ObjRwFromWatchable(const_cast<Watchable*>(WatcherA.target()));
}


// ----------------------------------------------------------------------------
// MOVED HERE 2026-09-17 (claude:w16-behavior): Update() (LAB_062cf5f0) and
// NotifyManagerGuideEvent (FUN_062d2a70) used to live in src/Common/common_stubs.cpp.
// The stated reason -- "Update() calls IsIdAmongOwnerFormationFollowers, which is
// file-static there" -- was the only thing holding them; FUN_062ec520's definition is
// now non-static and lived in common_behavior.cpp (declared in common_behavior.h; since CMN-GC an
// FGunConvergenceStateBlock member in common_gunconvergence.cpp), so both
// bodies sit in their own class's translation unit. Every other helper Update() calls
// (GetIfacePositionOrZero, GetInterceptClearanceOfIface, GetSignedRadialClearanceToPoint,
// Distance3, VecNormalize3, ClampedDot3, ObjInspectOf, ObjRwFromWatchable) was already
// header-declared and needed no change.
//
// Still open on this class: own vtable slots 3 and 10 (LAB_062cf3f0 Engage(),
// LAB_062cfc70 ApplyDirective(pub::AI::DirectiveGuideOp const&)) are NOT ported yet. Their
// former shared excuse -- "too many still-unidentified CachedPtr60/64 sub-object classes to
// safely vcall" -- is dead as of 2026-09-17: CachedPtr60/64 are sibling `Behavior*` objects
// out of IBehaviorManager::BehaviorDispatchTable (entries 2 and 18), see the field comment
// in common_guidebehavior.h. What remains genuinely unported for those two slots is
// FUN_062e74a0 (ApplyDirective's hardpoint-lookup-and-BaseWatcher-set helper) and the
// FUN_062d6710/FUN_062d88b0 pair Engage() uses. TODO: both slots are ordinary next-pass
// work now, not blocked.
// ----------------------------------------------------------------------------

// ===========================================================================
// GuideBehavior -- vtable slot 7 Update() (LAB_062cf5f0, decomp-tracker item 1461) and its
// private notification path NotifyManagerGuideEvent (FUN_062d2a70, item 1551).
//
// PORTED IN FULL 2026-09-17 (claude:common-guide) from EXE/Disasm/Common.dll.txt
// 206092-206730 and 211770-211808. The class, its layout and the per-step reasoning live in
// src/include/common_guidebehavior.h; this file holds the bodies.
//
// MOVED HERE from src/Common/common_stubs.cpp on 2026-09-17 (claude:w16-behavior). The
// original placement note read "this file holds only the bodies, because Update() calls
// IsIdAmongOwnerFormationFollowers above, which is file-static here" -- that was the whole
// reason, and it is gone: FUN_062ec520 is now non-static with a declaration in
// common_guidebehavior.h (see that header's trailing TODO -- it should end up in
// common_behavior.h). Nothing else in these three bodies was file-local to common_stubs.cpp.
//
// SPOT-RE-VERIFIED against the raw listing while moving (the transcription was NOT taken on
// trust -- that is where this marathon's mis-assignments hide). The x87 predicates, which are
// the recurring defect class, are transcribed CORRECTLY here, and unusually the original pass
// named the mask AND the jump at each site rather than the predicate alone, which is what made
// them checkable. Confirmed byte-for-byte at 062cf72d-062cf74e:
//   FLD float ptr [ESP+0x34]   ; the FUN_062e4f20/Distance3 result
//   FCOMP float ptr [ESP+0x30] ; the FUN_062e4ea0/GetSignedRadialClearanceToPoint result
//                              ; (pre-push [ESP+0x28], shifted by the two PUSHes at 062cf722)
//   FNSTSW AX; TEST AH,0x5; JP LAB_062cf74e   ; skips `MOV byte ptr [ESP+0x12],1`
// TEST AH,0x5 masks C0|C2, so PF is set for greater (0x00), equal (0x00) and unordered (0x05),
// and clear only for less (0x01) -- JP therefore jumps on NOT-less and the flag is set exactly
// when Distance3 < RadialClearance, which is what `bInsideRadial` below says. The
// TEST AH,0x41 + JP sites (062cfad2, and both tail tests) likewise jump on strictly-greater
// or unordered, since only 0x00 and 0x41 have even parity -- also transcribed correctly.
//
// The item's "GAP:AI-GUIDE-UPDATE -- not ported, too many unidentified helpers" marker was
// STALE, as this marathon's own standing lesson predicts. Every helper it named resolves
// today: FUN_062e60e0 GetIfacePositionOrZero, FUN_062e4bb0 GetInterceptClearanceOfIface,
// FUN_062e4ea0 GetSignedRadialClearanceToPoint, FUN_062e4f20 Distance3, FUN_062e1680
// VecNormalize3, FUN_062e1630 ClampedDot3, FUN_062ec520 IsIdAmongOwnerFormationFollowers,
// FUN_062d29a0 IBehaviorManager::SetDirectiveResult. The two "raw block vtbl" calls are
// IObjInspect slot 82 get_behavior_id (+0x148) and slot 44 get_formation_leader (+0xb0), and
// the "unidentified CachedPtr60/64 sub-object classes" are sibling `Behavior*` out of
// IBehaviorManager::BehaviorDispatchTable.
// ===========================================================================

// FUN_062d2a70 (item 1551). All four XREFs are in Update() below; the receiver is the
// IBehaviorManager (062cf9ae / 062cfa04 / 062cfb1a / 062cfb9d load [ESI+0x20] into ECX).
bool GuideBehavior::NotifyManagerGuideEvent(IBehaviorManager* BehaviorMgr, unsigned SelfId, unsigned WatchedId,
                                           int EventCode, float Clearance, Vector const* Point)
{
    // 062d2a70-062d2a84: Sys (+0x8) Targeting.AttackState (+0x1434) nonzero and behavior 0xe,
    // Guide's own index in the dispatch table.
    if (BehaviorMgr->Sys->Targeting.AttackState == 0 || BehaviorMgr->CurrentBehaviorIndex != 0xe)
        return false;

    // 062d2a86-062d2ab0: ContentCbPtr (+0x110) slot 2 with the five arguments re-pushed in order.
    if (BehaviorMgr->ContentCbPtr != nullptr)
    {
        BehaviorMgr->ContentCbPtr->lead_object_event(
            SelfId, WatchedId, (pub::AI::ContentCallback::LeadProgressType)EventCode, Clearance, *Point);
        return true;
    }

    // 062d2ab3-062d2ac3: EAX still holds Sys -- its ExternalIface->is_player() (+0xbc) for
    // effect, then false (the NotifyFollowEvent / NotifyTetherEvent fallback).
    (void)BehaviorMgr->Sys->SelfTarget.Iface->is_player();
    return false;
}

// Shared body of Update()'s eight guide-event sites (062cf90b-062cf913, 062cf96d-062cf975,
// 062cf99d-062cf9b3, 062cf9f5-062cfa09, 062cfaa7-062cfaaf, 062cfb0b-062cfb1f,
// 062cfb6b-062cfb73, 062cfb8e-062cfba2). All eight push the same five arguments in the same
// order; only the event code differs.
void GuideBehavior::FireGuideEvent(IObjRW* Watched, FAiTarget* NavElem, int EventCode,
                                   float Clearance)
{
    unsigned const SelfId = NavElem->Iface->get_id();   // retail: parked in EBX first
    unsigned const WatchedId = ObjInspectOf(Watched)->get_id();
    NotifyManagerGuideEvent(BehaviorMgrPtr20, SelfId, WatchedId, EventCode, Clearance, &Field48);
}

int GuideBehavior::Update()
{
    // ---- 062cf5f0-062cf62e: entry guards -------------------------------------------------
    // Retail tests `[this+0x2c] == 0` and then `[this+0x2c] - 8 == 0` separately; both
    // collapse into one null test on the adjusted pointer, because ObjRwFromWatchable maps
    // null -> null and the literal sentinel 8 -> null identically.
    IObjRW* const ObjA = ObjRwFromWatchable(const_cast<Watchable*>(WatcherA.target()));
    if (ObjA == nullptr)
    {
        // 062cf607: only THIS exit clears the flag bit; the two below deliberately do not.
        Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
        UpdateResult10 = 3;
        return 3;
    }
    if (OwnerContextPtr14 == nullptr)
    {
        UpdateResult10 = 3;
        return 3;
    }
    // 062cf622-062cf62e re-reads +0x2c and re-runs both tests. Nothing between can change it,
    // so the port evaluates ObjA once.

    FAiTarget* const NavElem = &NavHelperPtr24->SelfTarget;

    // ---- 062cf630-062cf6bd: refresh the guide destination point --------------------------
    // Retail reads the mode at raw offset +0x74, which is GotoOp + 0x0C.
    int const GotoMode = GotoOp.Field_0C;
    if (GotoMode == 0)
    {
        IObjRW* const ObjB = ObjRwFromWatchable(const_cast<Watchable*>(WatcherB.target()));
        if (ObjB == nullptr)
        {
            // 062cf648-062cf66c.
            Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
            BehaviorMgrPtr20->SetDirectiveResult(0xe, 1);
            UpdateResult10 = 2;
            return 2;
        }
        // 062cf66d is `TEST ECX,ECX; JNZ` on the mode dword that 062cf635 already proved to be
        // zero -- dead code in retail, reproduced as nothing rather than as an impossible branch.
        Vector WatchedBPos;
        GetIfacePositionOrZero(&WatchedBPos, ObjB); // FUN_062e60e0
        Field48 = WatchedBPos;                      // 062cf68d-062cf6a0, a 3-dword copy
    }
    else if (GotoMode == 1)
    {
        // 062cf6aa-062cf6bd: raw +0x78 == GotoOp + 0x10 == GotoOp.Destination.
        Field48 = GotoOp.Destination;
    }
    // any other mode: Field48 keeps last tick's value (retail falls straight through).

    // 062cf6c0-062cf6c8: raw +0xb8 == GotoOp + 0x50.
    Field54 = GotoOp.Field_50;

    // ---- 062cf6c9-062cf729: this tick's three steering measurements ----------------------
    Vector WatchedAPos;
    GetIfacePositionOrZero(&WatchedAPos, ObjA);                                  // FUN_062e60e0
    // 062cf703 / 062cf71a: both ST0 returns are rounded by `FSTP float`.
    float const Clearance = static_cast<float>(GetInterceptClearanceOfIface(NavElem, ObjA));            // FUN_062e4bb0
    float const RadialClearance = static_cast<float>(GetSignedRadialClearanceToPoint(NavElem, Field48)); // FUN_062e4ea0
    float const Dist = (float)Distance3(&WatchedAPos, &Field48);                 // FUN_062e4f20, FSTP float at 062cf729

    // 062cf72d-062cf74c. `TEST AH,0x5` with `JP` jumps on NOT-less, so the fall-through that
    // sets the byte is strictly-less. Retail keeps both of these in stack bytes [ESP+0x12] and
    // [ESP+0x13]; they gate the event table further down.
    bool const bInsideRadial = (Dist < RadialClearance);
    bool bLeaderIsSelf = false;

    // ---- 062cf74e-062cf7c2: "is the escorted object formed up on me?" --------------------
    if (ObjInspectOf(ObjA)->get_behavior_id() == 0xa) // vtbl +0x148 == IObjInspect slot 82
    {
        IObjRW* Leader = nullptr;                    // retail zeroes [ESP+0x18] before the call
        ObjInspectOf(ObjA)->get_formation_leader(Leader); // vtbl +0xb0 == slot 44; result unused
        if (Leader != nullptr)
        {
            unsigned const NavSelfId = NavElem->Iface->get_id();
            if (ObjInspectOf(Leader)->get_id() == NavSelfId)
            {
                Flag120 = 0;
                bLeaderIsSelf = true;
            }
        }
    }

    // ---- 062cf7c4-062cf7ef: ... or listed among my own formation followers ---------------
    if (NavHelperPtr24->GunConvergenceStateBytes.IsIdAmongOwnerFormationFollowers(
            ObjInspectOf(ObjA)->get_id())) // FUN_062ec520
    {
        Flag120 = 0;
    }

    // ---- 062cf7f1-062cf8b2: sample the escort's progress, at most once per Field104 -------
    // `TEST AH,0x41` + `JNZ` guards a strictly-greater test: retail falls through to the
    // sampling block when Field108 > Field104 and jumps to the accumulate arm otherwise.
    bool bSampled = false;
    if (Field108 > Field104)
    {
        // 062cf808-062cf81a.
        Field58 = Field10c - Dist;
        Field108 = 0.0f; // retail stores an integer zero, which is 0.0f's bit pattern

        // 062cf822-062cf841: the delta from the escorted ship's own position to the guide point.
        Vector Delta;
        Delta.x = Field48.x - WatchedAPos.x;
        Delta.y = Field48.y - WatchedAPos.y;
        Delta.z = Field48.z - WatchedAPos.z;

        // 062cf848-062cf878: vtbl +0xc == IObjInspect slot 3 get_transform; Transform::Rotation
        // sits at +0x00, so retail's [EAX+0x08]/[EAX+0x14]/[EAX+0x20] are E[0][2]/E[1][2]/E[2][2],
        // i.e. the matrix's third column, negated. (Same reading common_gotobehavior.cpp already uses
        // for GotoBehavior::Update's own facing extraction.)
        Matrix const& Rotation = ObjInspectOf(ObjA)->get_transform().Rotation;
        Vector NegForward;
        NegForward.x = -Rotation.E[0][2];
        NegForward.y = -Rotation.E[1][2];
        NegForward.z = -Rotation.E[2][2];

        // 062cf87c-062cf894. FUN_062e1680's own prologue reads param_2 as the source and writes
        // param_1, and returns param_1 -- so retail's (E-0xc, &Delta) is VecNormalize3's
        // (Out, Src), which this port spells (Src, Out).
        Vector NormDelta;
        VecNormalize3(&Delta, &NormDelta);                                // FUN_062e1680
        Field5c = std::acos(ClampedDot3(NegForward, NormDelta));          // FUN_062e1630 + _CIacos

        Field10c = Dist;
        bSampled = true;
    }
    else
    {
        // 062cf8a5-062cf8b2.
        Field108 = Field108 + DeltaTime1c;
    }

    bool bSkipRadialTermination = false;

    if (Flag120 != 0)
    {
        // ---- 062cf8c2-062cfa13: event table for the "escort is following me" state -------
        if (bSampled)
        {
            if (Field58 < FieldLow114) // 062cf8ca-062cf8d8, `TEST AH,5` + `JP` == not-less
            {
                if (!bInsideRadial)
                    FireGuideEvent(ObjA, NavElem, 8, Clearance);
                Flag120 = 0; // 062cfa0e, reached whether or not the event fired
            }
            else if (Field58 > FieldHigh118) // 062cf918-062cf926
            {
                if (Field5c > AngleThreshold11c) // 062cf92c-062cf93a
                {
                    if (!bInsideRadial)
                        FireGuideEvent(ObjA, NavElem, 6, Clearance);
                    Flag120 = 0;
                }
                else
                {
                    // 062cf97a-062cf9b8. The ONE arm that neither checks bInsideRadial nor
                    // clears Flag120 -- verified against the raw listing, not a transcription
                    // slip: it jumps straight to LAB_062cfa15, over LAB_062cfa0e.
                    FireGuideEvent(ObjA, NavElem, 5, Clearance);
                }
            }
            else if (Clearance < Field44) // 062cf9ba-062cf9c6
            {
                if (!bInsideRadial)
                    FireGuideEvent(ObjA, NavElem, 7, Clearance);
                Flag120 = 0;
            }
        }

        // ---- 062cfa15-062cfa51: hand the flying to the right sibling ---------------------
        if (Flag120 != 0)
        {
            // Still following: keep ticking CachedPtr64. Retail discards the slot-7 result here
            // (unlike the mirror-image branch below, which tests it against 2).
            *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(CachedPtr64) + 0x1c) = DeltaTime1c;
            CachedPtr64->Slot7();
        }
        else
        {
            // Transition to leading: park CachedPtr64, push GotoOp into CachedPtr60, engage it.
            CachedPtr64->Slot4();
            CachedPtr60->Slot10(&GotoOp);
            CachedPtr60->Slot3();
        }
    }
    else
    {
        // ---- 062cfa56-062cfba5: event table for the "escort is not following me" state ---
        if (bSampled)
        {
            if (bLeaderIsSelf) // 062cfa5e-062cfa64 -> LAB_062cfb31
            {
                FireGuideEvent(ObjA, NavElem, 4, Clearance);
            }
            else if (Field58 < FieldLow114 && !bInsideRadial) // 062cfa6a-062cfa80
            {
                FireGuideEvent(ObjA, NavElem, 3, Clearance);
            }
            else
            {
                // 062cfab4-062cfad0 interleaves two compares through one AH: the first FNSTSW
                // captures (Field58 vs FieldHigh118) and feeds `TEST AH,0x41`/`JNZ`; the second
                // FNSTSW (which does not disturb EFLAGS) captures (Clearance vs Field3c) and
                // feeds the following `TEST AH,0x5`/`JP` and, at 062cfb3d, a `TEST AH,0x1`
                // reading that same compare's C0 bit.
                bool const bFarAhead = (Field58 > FieldHigh118);
                bool const bInsideField3c = (Clearance < Field3c);          // TEST AH,0x5: C0 alone
                bool const bField3cC0 = !(Clearance >= Field3c);            // TEST AH,0x1: less OR unordered

                if (bFarAhead)
                {
                    if (bInsideField3c)
                    {
                        // 062cfad2-062cfae0: `TEST AH,0x41` + `JP` JUMPS on greater or
                        // unordered, so the event fires only when Field5c <= AngleThreshold11c.
                        if (Field5c <= AngleThreshold11c)
                        {
                            FireGuideEvent(ObjA, NavElem, 1, Clearance);
                            Flag120 = 1; // 062cfb24
                        }
                    }
                    else if (bInsideRadial) // 062cfb2d -> LAB_062cfb31
                    {
                        FireGuideEvent(ObjA, NavElem, 4, Clearance);
                    }
                }
                else if (!bField3cC0 && !bInsideRadial) // 062cfb3d-062cfb44, C0 also set on unordered
                {
                    FireGuideEvent(ObjA, NavElem, 2, Clearance);
                    Flag120 = 1; // 062cfb24, shared with event 1 via LAB_062cfb13
                }
            }
        }

        // ---- 062cfba7-062cfc0e: hand the flying to the right sibling ---------------------
        if (Flag120 != 0)
        {
            // Transition to following: park CachedPtr60, push FollowOp into CachedPtr64, engage it.
            CachedPtr60->Slot4();
            CachedPtr64->Slot10(&FollowOp);
            CachedPtr64->Slot3();
        }
        else
        {
            // Still leading: tick CachedPtr60. A result of 2 skips the radial termination test.
            *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(CachedPtr60) + 0x1c) = DeltaTime1c;
            if (CachedPtr60->Slot7() == 2)
                bSkipRadialTermination = true;
        }
    }

    // ---- 062cfbc7-062cfc66: is the directive still running? -------------------------------
    // Both `TEST AH,0x41` + `JP` tests below JUMP on strictly-greater (and on unordered) --
    // that is the opposite jump from the `JNZ` form used earlier in this function, and getting
    // the two confused is exactly the bug class this module has been burned by before.
    if (!bSkipRadialTermination)
    {
        // 062cfbd6: FCOMP of the unrounded ST0 return; `!(a <= b)` keeps the
        // unordered case on the jump side, as JP does.
        if (!(GetSignedRadialClearanceToPoint(NavElem, Field48) <= Field54)) // FUN_062e4ea0
        {
            UpdateResult10 = 1;
            return 1;
        }
    }
    // 062cfbe3-062cfc30: raw +0xc8 == GotoOp + 0x60.
    // 062cfc22: FCOMP of the unrounded ST0 return; JP is taken on greater OR
    // unordered, hence `!(a <= b)`.
    if (!(GetInterceptClearanceOfIface(NavElem, ObjA) <= GotoOp.Field_60))   // FUN_062e4bb0
    {
        UpdateResult10 = 1;
        return 1;
    }

    // 062cfc32-062cfc56.
    Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
    BehaviorMgrPtr20->SetDirectiveResult(0xe, 0);
    UpdateResult10 = 2;
    return 2;
}
