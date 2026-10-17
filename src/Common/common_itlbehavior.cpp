// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_itlbehavior.h"
#include "common_cobject.h"  // CObject::ObjectClass, CShip::request_exit_tradelane
#include "common_systemmanager.h"  // SystemManager::Thrust -- TurnOff

// FUN_062e74a0 (decomp-tracker item 1798) -- the NavHelper+0x34 "resolve and verify by id"
// lookup -- is SystemManagerTimerTable::FindVerifiedObject (common_sysmgr_timertable.cpp).


// InstantTradeLaneBehavior -- see common_itlbehavior.h for the full RTTI/
// layout writeup this pass relied on. Ported from EXE/Disasm/Common.dll.txt
// (raw disassembly is authoritative in this module over Ghidra's
// Common.dll.c decompilation -- see this marathon's "ground truth
// precedence" rule).

// ----------------------------------------------------------------------------
// FUN_062cee50 (Common.dll.txt 205207-205221) -- constructor. Calls the
// shared TractorArm-family base-init helper FUN_062d2020 first (item 1529,
// already a REAL field-initialising base ctor -- it writes +0x04..+0x29 (see Behavior in common_behavior.h); the long-recorded "no-op stub" reading was wrong,
// same as DockBehavior's ctor), then installs both vtables and does nothing
// else. Every field is zero-initialized here anyway (documented gap vs.
// retail, which leaves everything but the two vtable pointers untouched in
// this particular constructor -- see the header's class-level comment).
// (The block above documents InstantTradeLaneBehavior::InstantTradeLaneBehavior, defined just
// below this base ctor.)

// FUN_062cee50 forwards its own type-id argument to FUN_062d2020 at .text:062cee58.
InstantTradeLaneBehaviorFields::InstantTradeLaneBehaviorFields(int InTypeId)
    : TypeId(InTypeId)
{
    ConstructBase(InTypeId);   // FUN_062d2020 -- Behavior's own field initialisation
}

InstantTradeLaneBehavior::InstantTradeLaneBehavior(int InTypeId)
    : InstantTradeLaneBehaviorFields(InTypeId)
    , State30(0)
    , SubObjPtr34(nullptr)
    , LanePtr38(nullptr)
    , LaneStart3c()
    , LaneEnd48()
    , Flags54(0)
    , Flag55(0)
    , Flag56(0)
    , Pad57(0)
    , Field58(0)
{
}

// Scalar-deleting-destructor thunk (FUN_062c7090, item 1320, ICF-shared
// boilerplate) calls thunk_FUN_062d2080 directly with no class-specific
// cleanup at all (Common.dll.txt 193351-193362) -- this class owns no
// BaseWatcher-shaped sub-objects to tear down, unlike DockBehavior.
InstantTradeLaneBehavior::~InstantTradeLaneBehavior()
{
    // Retail calls thunk_FUN_062d2080 (~Behavior, a vptr reset); the port's base destructor
    // runs implicitly. Nothing this class owns needs tearing down beyond that.
}

// Own vtable slot 1 (offset 0x04). Common.dll.txt 205222-205244 (LAB_062cee70).
//
// CLOSED 2026-09-17 (claude:common-largeship-itl) -- fully real, no deferral left. See the
// header for why this was renamed from ResetIfTargetQueryFailed and re-signed from two void
// parameters to three plus a bool return (retail's `RET 0xc` and `MOV AL,1`/`XOR AL,AL`).
//
// Argument recovery from the raw bytes, since the old declaration had it wrong: at entry
// [ESP+4]/[ESP+8]/[ESP+0xc] are Param1/Param2/Param3; retail reads Param3 into EAX and
// Param1 into EDX, pushes ESI, re-reads Param2 into ECX, then pushes EAX, ECX, EDX -- so
// FUN_062d2090 receives them in their original order (.text:062cee70-062cee84).
//
// FUN_062d2090 is Behavior::BindContext (Param1 owner -> +0x14, its id -> +0x08, Param3 ->
// +0x24 nav helper, Param2 -> +0x20 manager; false when Param1 or Param3 is null).
bool InstantTradeLaneBehavior::SetContext(void* Param1, void* Param2, void* Param3)
{
    bool const bInstalled = BindContext(Param1, Param2, Param3);     // 062cee84 FUN_062d2090

    // `XOR ECX,ECX; CMP AL,CL; JZ LAB_062ceea1` -- the JZ skips the clears, so they run on
    // the TRUE path. The old port had this branch's polarity documented correctly but never
    // executed it, because it hard-coded the gate to false.
    if (bInstalled)
    {
        Flag55 = 0;
        Flag56 = 0;
        Flags54 = 0;
        SubObjPtr34 = nullptr;
        LanePtr38 = nullptr;
        Field58 = 0;
    }

    return bInstalled;
}

// Own vtable slot 3 (offset 0x0c). Common.dll.txt 205303-205322
// (FUN_062cef10, decomp-tracker item 1445).
void InstantTradeLaneBehavior::TurnOff()
{
    // 2026-10-08 (claude-code:cmn-ai3), all 16 instructions: `[ESI+0x14]` null -> RET;
    // FUN_062d21d0(0, 3) (Behavior::SetCruiseLock); +0x30 = 0; FUN_062d20d0
    // (Behavior::Slot3); ThrustManager::engage_cruise(false) on [ESI+0x24]+0x18.
    if (TargetPtr14 == nullptr)
        return;
    SetCruiseLock(0, 3);                                                    // 062cef1e
    State30 = 0;                                                            // 062cef25
    Behavior::Slot3();                                                      // 062cef2c
    NavHelperPtr24->Thrust.engage_cruise(false);   // 062cef39
}

// Own vtable slot 4 (offset 0x10). Common.dll.txt 205326-205368
// (FUN_062cef40).
void InstantTradeLaneBehavior::RequestExitTradeLane()
{
    // `TEST ECX,ECX; JZ LAB_062cefa0` -- the null-check on TargetPtr14 gates
    // the ENTIRE function body, including the FUN_062d20f0/SubObjPtr34-clear/
    // FUN_062d21d0 tail below (confirmed: the JZ target is the function's
    // final POP/RET, not a mid-function rejoin point).
    if (TargetPtr14 == nullptr)
        return;

    bool const bWasStatusSet = (Flags54 != 0);
    Flag55 = 0;
    Flag56 = 0;
    Flags28 |= 0x1;

    if (bWasStatusSet)
    {
        Flags54 = 0;

        // CLOSED 2026-09-17 (claude:common-largeship-itl). The old reason here --
        // "TargetPtr14's real interface is not identified, so this port cannot resolve a
        // genuine CShip*" -- is disproved. TargetPtr14 is an IObjRW* (see its field note in
        // the header), so vtbl+0x150 is IObjInspect::cobject(), returning a CObject const*;
        // and the "+0x4c" the old note described as an opaque status mask is simply
        // CObject::ObjectClass, which common_cobject.h has had typed and static_asserted at
        // that exact offset all along. Retail, .text:062cef66-062cef87:
        CObject const* const Obj = TargetPtr14->cobject();

        CShip* Ship = nullptr;
        if (Obj != nullptr && (Obj->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
            Ship = (CShip*)Obj;

        // Retail reaches `MOV ECX,EAX; CALL CShip::request_exit_tradelane` on BOTH paths
        // (062cef83-062cef87), i.e. with a null `this` whenever cobject() is null or fails the
        // class test -- a deterministic null dereference, reproduced without a guard.
        Ship->request_exit_tradelane();
    }

    // LAB_062cef8c: FUN_062d20f0 (Behavior::Slot4 -- Pathfind->Field_064 = 0, Flag29 = 0),
    // +0x34 = 0, FUN_062d21d0(0, 3) (Behavior::SetCruiseLock). Both calls were deferred
    // until 2026-10-08.
    Behavior::Slot4();
    SubObjPtr34 = nullptr;
    SetCruiseLock(0, 3);
}

// Own vtable slot 5 (offset 0x14). Common.dll.txt 205256-205297
// (LAB_062ceeb0).
int InstantTradeLaneBehavior::UpdateMode()
{
    int Mode = 2;

    // `TEST EAX,EAX; JZ LAB_062ceeee` -- TargetPtr14==nullptr skips BOTH the
    // Flags54 check AND the SubObjPtr34 vcall below, falling straight
    // through to the Flag29 check.
    if (TargetPtr14 != nullptr)
    {
        if (Flags54 != 0)
            Mode = 1;

        if (SubObjPtr34 != nullptr)
        {
            // CLOSED 2026-09-17 (claude:common-largeship-itl). SubObjPtr34's interface IS
            // identified now -- it is an IObjInspect const* (proven by Update()'s state-0
            // case passing it as get_lane_start's `Self` argument), so vtbl+0x88 is
            // IObjInspect::get_type(uint& OutType), slot 34. Retail,
            // .text:062ceed4-062ceee7: the call, then `MOV AL,[ESP+8]; TEST AL,AL; JNS` --
            // it reads only the out-parameter's LOW BYTE and branches on that byte's sign
            // bit, so the predicate is `(Type & 0x80) != 0`, not a signed test of the whole
            // dword as an earlier reading of this code had it. get_type's int return value
            // is discarded.
            unsigned SubObjectType = 0;
            SubObjPtr34->get_type(SubObjectType);
            if ((SubObjectType & 0x80u) != 0)
                Mode = 1;
        }
    }

    if (Flag29 != 0)
        Mode = 3;

    ModeSelector0xc = Mode;
    return Mode;
}

// Own vtable slot 7 (offset 0x1c), Update/dispatch entry point.
// Common.dll.txt 205382-205514 (LAB_062cefb0), 66 instructions across a
// 7-way (State30 0-6) switch.
int InstantTradeLaneBehavior::Update()
{
    // `TEST byte ptr [ESI+0x28],1; JZ ...` -- early-out if the exit-tradelane
    // bit is already set.
    if (Flags28 & 0x1)
    {
        UpdateResult10 = 2;
        return UpdateResult10;
    }

    // `MOV ECX,[ESI+0x14]; TEST ECX,ECX; JNZ dispatch` -- no target yet.
    if (TargetPtr14 == nullptr)
    {
        UpdateResult10 = 3;
        return UpdateResult10;
    }

    // retail: `MOV EDX,[ESI+0x30]; CMP EDX,6; JA caseD_1; JMP [EDX*4 + switchdata]`
    // (.text:062cefdf-062ceff0). States 0..6; anything above 6 joins case 1.
    //
    // THIS SWITCH IS REAL CODE AS OF 2026-09-17 (claude:common-largeship-itl). It had been one
    // FULLPORT-DEFERRED block on the grounds that state 0 dispatches "through `*this`'s own
    // vtable at byte offset 0x138, which exceeds this class's own 27-slot vtable, so the real
    // target is not resolvable". That was a register misread: ECX is loaded with TargetPtr14
    // at .text:062cefcc (the guard above) and is NEVER reloaded before or inside the switch,
    // so 0x138 and 0x64 below are slots on TargetPtr14, not on this object. See the header.
    switch (State30)
    {
    case 0:
    {
        // .text:062ceff7-062cf04d.
        if (LanePtr38 == nullptr)
        {
            // `LEA EDX,[ESI+0x48]; PUSH; LEA EDX,[ESI+0x3c]; PUSH; LEA EDX,[ESP+0x10]; PUSH;
            //  MOV EDX,[ESI+0x34]; MOV [ESP+0x14],0; MOV EAX,[ECX]; PUSH EDX;
            //  CALL [EAX+0x138]` -- IObjInspect::get_lane_start(Self, OutLane, OutStart,
            // OutEnd), slot 78. The pushes are right-to-left, so OutEnd (+0x48) going on the
            // stack first makes it the LAST argument.
            IObjInspect const* OutLane = nullptr;
            int const LaneResult = TargetPtr14->get_lane_start(
                SubObjPtr34, OutLane, LaneStart3c, LaneEnd48);

            // `TEST EAX,EAX; JZ LAB_062cf02f` -- ZERO is the success path. (An earlier
            // summary of this function had the polarity inverted, calling result==0 the
            // "vcall failure".)
            if (LaneResult != 0)
            {
                UpdateResult10 = 3;
                return 3;
            }
            LanePtr38 = OutLane;
        }

        // LAB_062cf036 -- also the direct target when LanePtr38 was already set.
        UpdateResult10 = 1;
        State30 = 3;
        Flag56 = 1;
        Flag55 = 0;
        return 1;
    }

    case 3:
    {
        // .text:062cf04e-062cf09c. `MOV EDX,[ECX+0x4]` reads the vtable pointer of the
        // IObjAffect sub-object at TargetPtr14+4; `ADD ECX,0x4` makes it the call's `this`.
        // Arguments, right-to-left off the stack: (SubObjPtr34, LanePtr38, 1, Field58).
        // That is IObjAffect slot 25, go_tradelane(EntranceRing, ExitRing, bFastExit, Speed)
        // (named 2026-09-24 from Server.dll's Ship body .text:06d028c0).
        int const AffectResult = TargetPtr14->go_tradelane(SubObjPtr34, LanePtr38, true, Field58);

        // `TEST EAX,EAX; JNZ LAB_062cf08d` -- zero is success.
        if (AffectResult != 0)
        {
            // LAB_062cf08d: NOTE UpdateResult10 is set from EDI, which is still 1 here --
            // it is only zeroed on the success path below. (An earlier summary said this
            // path yields 0; it does not.)
            UpdateResult10 = 1;
            State30 = 6;
            return 1;
        }

        // FUN_062d4520 is decomp-tracker item 1561, ported for real (ready_unverified) as
        // IBehaviorManager::QueueNotification(IObjRW* Target, unsigned Extra, float Duration)
        // in common_behaviormanager.cpp -- the "unprocessed" note that used to stand here was stale.
        // Retail pushes EAX (== 0) twice then TargetPtr14, with ECX = BehaviorMgrPtr20, so
        // Extra == 0 and Duration == 0.0f (the zero dword is a valid 0.0f bit pattern) -- the
        // immediate path: TargetPtr14's formation followers get notification 0 now.
        // .text:062cf069-062cf072: no null test on [ESI+0x20] before the call.
        BehaviorMgrPtr20->QueueNotification(TargetPtr14, 0, 0.0f);

        UpdateResult10 = 0;
        Flags54 = 1;
        State30 = 4;
        return 0;
    }

    case 5:
    case 6:
    {
        // .text:062cf09d-062cf0c5. Both cases set Flags28 bit 0x1, call this object's OWN
        // vtable slot 4 (`CALL [EDX+0x10]` with ECX = ESI = this -- genuinely self-referential
        // here, unlike the switch's other dispatches), then write the two manager fields. The
        // only difference between them is the value handed to FUN_062d29a0: 2 for state 6,
        // 0 for state 5.
        // Captured BEFORE the call: retail selects the constant at the case label
        // (`PUSH 0x2` in caseD_6 / `PUSH 0x0` in caseD_5) and only then converges on
        // LAB_062cf0bb, so the value must not be re-derived from State30 afterwards.
        int const ManagerArg118 = (State30 == 6) ? 2 : 0;

        Flags28 = static_cast<unsigned char>(Flags28 | 0x1);
        Slot4();                                                   // `CALL [EDX+0x10]`, virtual
        BehaviorMgrPtr20->SetDirectiveResult(9, ManagerArg118);    // FUN_062d29a0
        UpdateResult10 = 2;
        return 2;
    }

    case 1:
    case 2:
    case 4:
    default:
        // caseD_1/2/4 and states above 6 (`JA caseD_1`) land on LAB_062cf0ca with EDI = 1 from
        // .text:062cefe5 (only cases 5/6 load EDI = 2, at 062cf0c5) -- still running.
        UpdateResult10 = 1;
        return 1;
    }
}

// Own vtable slot 10 (offset 0x28). Common.dll.txt 205524-205562
// (LAB_062cf0f0).
bool InstantTradeLaneBehavior::AttachDirective(void* Directive)
{
    // CLOSED 2026-09-17 (claude:common-largeship-itl) -- all 37 instructions are real now.
    // The GAP:AI-MANEUVER-EXECUTOR tag and the "FUN_062e74a0 is unprocessed" reason were
    // BOTH stale: decomp-tracker item 1798 was closed on 2026-09-11 and the function is
    // defined for real as SystemManagerTimerTable::FindVerifiedObject (common_sysmgr_timertable.cpp). Retail (.text:062cf0f3-062cf14a):
    //   Flags28 &= ~0x1;                                              (`AND DL,0xfe`)
    //   SubObjPtr34 = FUN_062e74a0(NavHelperPtr24 + 0x34, Directive + 0xc);
    //   LanePtr38   = FUN_062e74a0(NavHelperPtr24 + 0x34, Directive + 0x10);
    //   Field58     = *(float*)(Directive + 0x14);   (a dword copy; go_tradelane's Speed)
    //   Flags28 = SubObjPtr34 ? (Flags28 | 0x2) : (Flags28 & ~0x2);
    //   return SubObjPtr34 != nullptr;
    // Both lookups use this+0x24 (the NavHelper), NOT this+0x20 -- re-confirmed against the
    // raw bytes this pass (`MOV ECX,[ESI+0x24]; ADD ECX,0x34` at .text:062cf0f6/062cf105 and
    // again at .text:062cf114/062cf117). Easy to conflate: both are plain pointer fields at
    // adjacent offsets.
    Flags28 &= ~0x1;

    char* const DirectiveBytes = static_cast<char*>(Directive);
    SystemManagerTimerTable const* const LookupThis = &NavHelperPtr24->Timers;

    SubObjPtr34 = LookupThis->FindVerifiedObject((unsigned const*)(DirectiveBytes + 0xc));
    LanePtr38 = LookupThis->FindVerifiedObject((unsigned const*)(DirectiveBytes + 0x10));

    Field58 = *reinterpret_cast<float*>(DirectiveBytes + 0x14);

    if (SubObjPtr34 != nullptr)
        Flags28 |= 0x2;
    else
        Flags28 &= ~0x2;

    return SubObjPtr34 != nullptr;
}

// Own vtable slot 22 (offset 0x58). Common.dll.txt 205627-205661
// (LAB_062cf1d0).
void InstantTradeLaneBehavior::HandleTargetEvent(void* Event)
{
    // Retail's tagged-event shape: `struct { int Tag; void* Payload; } const*`.
    // Only proceeds when Tag == 4 and Payload->FirstByte == 0.
    struct TaggedEvent { int Tag; unsigned char* Payload; };
    TaggedEvent const* Ev = static_cast<TaggedEvent const*>(Event);

    if (Ev->Tag != 4 || Ev->Payload[0] != 0)                         // 062cf1d7-062cf1e3, no null test
        return;

    // CLOSED 2026-09-17 (claude:common-largeship-itl). TargetPtr14 is an IObjRW*, so
    // vtbl+0x20 is IObjInspect::get_id() -- the very slot the family's own shared context
    // initializer (item 1532) reaches through the same offset on the same kind of pointer.
    // Retail, .text:062cf1e5-062cf1f0.
    if (TargetPtr14->get_id() != *reinterpret_cast<unsigned const*>(Ev->Payload + 0xc))
        return;

    // `MOV dword ptr [ESI+0x30],5` -- State30=5 is set UNCONDITIONALLY once
    // both guards above pass, regardless of SubObjPtr34 (confirmed via raw
    // disasm: the SubObjPtr34 null-check comes AFTER this write, gating only
    // the vcall block below, not the State30 assignment itself).
    State30 = 5;

    if (SubObjPtr34 == nullptr)
        return;

    // .text:062cf208-062cf222. SubObjPtr34's vtbl+0x20 is also IObjInspect::get_id(); the
    // id it returns is stashed in a stack slot whose ADDRESS is then handed to the
    // IObjAffect sub-object at TargetPtr14+4, slot 0x60, as (2, &SavedId, 0). Retail reuses
    // its own incoming parameter slot as that scratch (`MOV [ESP+0xc],0` at .text:062cf1fe
    // pre-zeroes it), which is why the old reading saw a "localbuf" with no declaration.
    // Slot 24's signature is declared in common_iface.h since 2026-09-24 (Server.dll's Ship
    // body .text:06d027a0, RET 0xc).
    unsigned SavedId = SubObjPtr34->get_id();
    (void)TargetPtr14->slot_60(2, &SavedId, 0);
}

// IObjAffect::RequestCB override -- own single secondary-vtable slot.
// Common.dll.txt 205566-205611 (LAB_062cf150), 47 instructions.
bool InstantTradeLaneBehavior::OnResult(int /*Param1*/, int /*Param2*/, int Param3)
{
    // Retail (outer_this recovered via a further -0x18 adjustment from the
    // already-adjusted RequestCB `this`, i.e. outer+0x14 relative to the
    // secondary vtable's own base -- confirmed against TargetPtr14's offset):
    Flag55 = 0;

    // `CMP EAX,-1; JZ LAB_062cf174` -- Param3 != -1 is the FALL-THROUGH path
    // (not the jump target); this is the simple, fully self-contained branch.
    if (Param3 != -1)
    {
        Flag56 = 1;
        State30 = 3;
        return false;
    }

    // Param3 == -1 (LAB_062cf174):
    State30 = 6;

    // Retail also re-tests `Flag55 != 0` here (`CMP [ESI+0x29],BL; JNZ ...`),
    // but Flag55 was unconditionally zeroed two instructions above with no
    // write in between -- that comparison is PROVABLY always-false (dead
    // code in retail itself), so only Flag56 actually gates the block below.
    if (Flag56 != 0)
    {
        // CLOSED 2026-09-17 (claude:common-largeship-itl). Two corrections to the old note.
        // (1) The slot is 0x20, not 0x8: .text:062cf18a reads `MOV ECX,[ESI+0x8]` -- and
        //     since ESI here is the ADJUSTED RequestCB `this` (outer + 0x2c), [ESI+0x8] is
        //     outer+0x34, i.e. SubObjPtr34 itself; the call is `CALL [EAX+0x20]`, which on
        //     an IObjInspect const* is get_id(). The "0x8" in the old note was the field
        //     offset being mistaken for the vtable slot.
        // (2) Both owning interfaces ARE identified now: SubObjPtr34 is IObjInspect const*
        //     and TargetPtr14 (reached at [ESI-0x18] == outer+0x14) is IObjRW*, whose
        //     IObjAffect sub-object sits at +4. Same (2, &SavedId, 0) shape as
        //     HandleTargetEvent()'s call, with the id written into the scratch slot first
        //     (`MOV [ESP+0x14],EAX` at .text:062cf21a / .text:062cf19a-062cf1a2 here).
        // Both calls are unguarded in retail (.text:062cf185-062cf1a2).
        unsigned SavedId = SubObjPtr34->get_id();
        (void)TargetPtr14->slot_60(2, &SavedId, 0);

        // Retail then unconditionally clears Flag55/Flag56.
        Flag55 = 0;
        Flag56 = 0;
    }

    if (Flags54 != 0)
    {
        Flags54 = 0;
        State30 = 6;
    }

    return false;
}
