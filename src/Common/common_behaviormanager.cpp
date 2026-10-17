// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- IBehaviorManager (ctor/dtor, the exported behavior-control API, the
// notification queue, UpdateBehaviorTick and the one virtual Slot00) and
// ConnectBehaviorManagerObjectDb.
// CMN-SPLIT stage 1 (2026-10-14): split out of common_iface.cpp unchanged.

#include "common_iface.h"
#include "common_behavior.h" // class Behavior -- what BehaviorDispatchTable holds
#include "common_dockbehavior.h" // DockBehavior -- BehaviorDispatchTable[7] (get_docking_port)

#include "common_aisteer.h" // IsWithinInterceptRangeOfIface -- UpdateBehaviorTick real-body pass
#include "common_cobjdb.h"
#include "common_csys.h"   // Acquire3DMathEngine, MatrixColumn2 (update_current_behavior_direction)
#include "x86math.h"       // I3DMathEngine::TransposeTransform, FVector, FMatrix
#include "common_goods.h"
#include "common_iustategraph.h" // GetAiTargetList / GetClosestTrailingShipDistance --
                                  // the SystemManager+0x34 AI target list that
                                  // get_closest_trailing_ship tail-calls into

#include "common_misc.h" // SinglePlayer() -- UpdateBehaviorTick's docking-target probe
#include "common_newchar.h"
#include "common_pub.h"
#include "common_pathfind.h" // PathfindManager -- IBehaviorManager+0x0C
#include "common_universe.h"
#include "dacom.h"

// UpdateAttackTargetOverride (FUN_062ed780, item 1896) is TargetManager::UpdateAttackTargetOverride
// (common_targetmanager.cpp); FUN_062ecaf0 (item 1869) is
// FGunConvergenceStateBlock::GetCurrentLeader (common_gunconvergence.cpp) and FUN_062e8830 (item 1811) is
// SystemManagerTimerTable::IsTimer3Active (common_sysmgr_timertable.cpp) -- CMN-SPLIT A2.

namespace
{
    // The one local with a destructor in IBehaviorManager::UpdateBehaviorTick (FuncInfo
    // 063aeb40, a single unwind entry): it holds &Sys->Timers (ESP+0x1c, stored at
    // .text:062d2b5f) after the tick has set Timers.bKeepCache = 1 (.text:062d2b63). Its
    // destructor is FUN_062d3410 -- `MOV EAX,[ECX]; MOV byte [EAX+0x1d],0; RET` -- reached
    // out of line only from Unwind@06395120; both normal exits inline it (.text:062d2b80,
    // .text:062d33ec-062d33f0). Retail gives it no name.
    struct FTimerTableKeepCacheScope
    {
        explicit FTimerTableKeepCacheScope(SystemManagerTimerTable* InTable)
            : Table(InTable)
        {
        }

        ~FTimerTableKeepCacheScope()
        {
            Table->bKeepCache = 0;
        }

        FTimerTableKeepCacheScope(FTimerTableKeepCacheScope const&) = delete;
        FTimerTableKeepCacheScope& operator=(FTimerTableKeepCacheScope const&) = delete;

        SystemManagerTimerTable* Table;
    };
}

#include <intrin.h> // __debugbreak -- UpdateBehaviorTick's retail INT3 (.text:062d32fd)
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>

// Behavior::Slot5 used to be declared `int Slot5(void* Arg)`, so this file carried a
// bit-copy helper to push a float through a pointer parameter. The declaration is fixed
// (common_behavior.h, 2026-09-17) and the helper is gone -- Slot5 is called directly.
// The decisive evidence, for anyone re-reading this: both call sites push a plain dword,
// which proves nothing on its own, but the manager does `FADD float ptr [ESI+0xdc]` /
// `FSTP float ptr [ESI+0xdc]` at .text:062d2df2-062d2dfc, fifty instructions earlier in
// this same function, and that accumulator is what .text:062d31b8 pushes.

// ============================================================================
// IBehaviorManager — constructor / destructor (not exported). FUN_062d2220 and
// FUN_062d2530, both traced store by store against Common.dll.txt 210975-211195
// and 211196-211330 (2026-09-24, claude-code:w3-behaviormgr-sys).
// ============================================================================

IBehaviorManager::IBehaviorManager(IObjRW* Owner)
{
    // Fields retail's ctor never writes (operator_new garbage in retail) are zeroed first so
    // the port is deterministic; every store retail DOES make follows below, in listing order.
    // retail: vptr 063a037c (.text:062d22bf) -- the compiler installs the port's one-slot
    // vftable (virtual Slot00) before this body runs.
    memset(BehaviorScores, 0, sizeof(BehaviorScores));
    memset(PadBd, 0, sizeof(PadBd));
    memset(OpaqueD9, 0, sizeof(OpaqueD9));
    BehaviorTimerC = 0.0f;
    Field0xF8 = 0;
    memset(OpaqueFa, 0, sizeof(OpaqueFa));
    memset(Pad131, 0, sizeof(Pad131));
    memset(Pad151, 0, sizeof(Pad151));
    memset(Opaque16a, 0, sizeof(Opaque16a));
    memset(Opaque171, 0, sizeof(Opaque171));
    for (int Index = 0; Index < 21; ++Index)
        BehaviorDispatchTable[Index] = nullptr;
    Flags1c8 = 0;
    memset(Pad1c9, 0, sizeof(Pad1c9));

    // .text:062d223f-062d2261: the current-directive storage (two zero dwords and a -1),
    // the attack-override timer pair.
    int const MinusOne = -1;
    memset(DirectiveStorage, 0, 8);
    memcpy(DirectiveStorage + 8, &MinusOne, sizeof(int));
    AttackOverrideTimer = 0.0f;
    AttackOverridePeriod = 1.0f;

    // .text:062d226f-062d2271: the +0xF0 BaseWatcher is zeroed (Watched, Next).
    static_assert(offsetof(IBehaviorManager, WatchablePtr) == 0xF0, "IBehaviorManager+0xF0 watcher");
    static_assert(offsetof(IBehaviorManager, OpaqueF4) == 0xF4, "IBehaviorManager+0xF4 watcher link");
    static_assert(sizeof(BaseWatcher) == 8, "BaseWatcher is Watched + Next");
    WatchablePtr = nullptr;
    memset(OpaqueF4, 0, sizeof(OpaqueF4));

    // .text:062d2274-062d229b: the notification queue (a VC6 std::list). +0x158 is the list's
    // empty allocator object -- retail copies the low byte of the ctor argument into it
    // (`MOV AL,[param_1]; MOV [ESI+0x158],AL`), a meaningless byte nothing ever reads; the
    // port zeroes it. Then the sentinel node operator_new(0x24), self-linked, and a zero count.
    memset(Pad158, 0, sizeof(Pad158));
    InitNotificationQueue();

    // .text:062d22a7-062d22cb
    Opaque168[0] = 1;
    DockingPortFlag = 0;
    DockingPortStatus = 0;
    TickSuppressedFlag = 0;
    StateGraph = nullptr;

    // .text:062d22cb-062d22de: bind the +0xF0 watcher to the owner's Watchable sub-object:
    // `EAX = Owner ? Owner+8 : 0; if (EAX != [EDI]) set_pointer(EAX)` == BaseWatcher::set.
    reinterpret_cast<BaseWatcher*>(&WatchablePtr)->set(
        Owner != nullptr ? static_cast<Watchable const*>(Owner) : nullptr);

    // .text:062d22e3-062d2338. Retail ORs bit 0 into a byte it never initialised; the port
    // zeroed it above, so the result is 1.
    ObjectDb = nullptr;
    Pathfind = nullptr;
    BehaviorDirty = 0;
    Field0xF8 = 1;
    Flags1c8 = static_cast<unsigned char>(Flags1c8 | 1);
    for (int Index = 0; Index < 5; ++Index)
        DirectiveCallbacks[Index] = nullptr;
    ContentCbPtr = nullptr;
    Field118 = 0;
    Field11c = -1;

    // .text:062d2327-062d2369: operator_new(0x580) + FUN_062db3b0(this) -> +0x0C.
    // PathfindManager's ctor queries the watched object without a null test, exactly as here.
    Pathfind = new PathfindManager(this);

    // .text:062d235f-062d239c: operator_new(0x195c) + FUN_062e6260 -> +0x08, then
    // FUN_062e6490(Owner, this).
    Sys = new SystemManager();
    (void)InitSystemManager(Sys, Owner, this);

    // .text:062d23a1-062d23d4: build all 21 behaviours and bind each one. The call is
    // `table[i]->vtbl[1](Owner, this, Sys)` with no null test on the CREATE_BEHAVIOR result.
    for (int Index = 0; Index < 21; ++Index)
    {
        BehaviorDispatchTable[Index] = CREATE_BEHAVIOR(Index);
        (void)BehaviorDispatchTable[Index]->SetContext(Owner, this, Sys);
    }

    // .text:062d23dc
    CurrentBehaviorIndex = 0;

    // .text:062d23e2-062d2466: three phase offsets, each `(float)rand() * (1/32768)
    // (DAT_063997d0 == 0x38000000) * Period`, rand() called in this order. Periods: 3.0f
    // (DAT_063a0340), 0.2f (DAT_063a0344), 1.0f (EBP). BehaviorTimerC and the attack
    // period are rewritten between the draws; the period ends as DAT_063a0348 == 1.0f.
    BehaviorTimerA = static_cast<float>(std::rand()) * (1.0f / 32768.0f) * 3.0f;
    BehaviorTimerC = 0.0f;                                   // .text:062d2426
    AttackOverridePeriod = 1.0f;                             // .text:062d242c
    BehaviorTimerB = static_cast<float>(std::rand()) * (1.0f / 32768.0f) * 0.2f;
    AttackOverrideTimer = static_cast<float>(std::rand()) * (1.0f / 32768.0f) * 1.0f;
    AttackOverridePeriod = 1.0f;                             // .text:062d2460-062d2466

    // .text:062d2442-062d2490: IObjInspect::is_player (slot 47, +0xBC) on the owner.
    if (Owner != nullptr && static_cast<IObjInspect*>(Owner)->is_player())
        Field120 = 0x13;
    else
        Field120 = 6;

    // .text:062d2494-062d2506
    LevelCameraFlag = 1;
    LastCommandedDirection.x = 0.0f;
    LastCommandedDirection.y = 0.0f;
    LastCommandedDirection.z = 0.0f;
    ShipUpDirFlag = 0;
    ShipUpDirection.x = 0.0f;
    ShipUpDirection.y = 1.0f;
    ShipUpDirection.z = 0.0f;
    CameraUp.x = 0.0f;
    CameraUp.y = 1.0f;
    CameraUp.z = 0.0f;
    UserTurningInputState = 0;
    memset(Opaque14c, 0, sizeof(Opaque14c));
    CameraLevelStatus = 0;
    ManeuverMask = 0;
    LockManeuversByte = 0;
    InSightsTargetId = 0;
    TurnSensitivity = 0.75f;                                 // 0x3f400000
    ContentCbCreationType = 3;
}

IBehaviorManager::~IBehaviorManager()
{
    // FUN_062d2530 .text:062d2558-062d256d: CurrentBehaviorIndex back to -1 when set.
    if (CurrentBehaviorIndex != -1)
        CurrentBehaviorIndex = -1;

    // .text:062d2573-062d25c4: when +0x0C is set, PathfindManager's (inlined) destructor --
    // its two BaseWatchers, +0x218 then +0x1C8 -- and operator delete; then +0x0C = 0.
    delete Pathfind;
    Pathfind = nullptr;

    // .text:062d25bf-062d2665: when +0x08 is set, SystemManager's inlined destructor (see
    // ~SystemManager for the store-by-store order) and operator delete; then +0x08 = 0.
    delete Sys;
    Sys = nullptr;

    // .text:062d2668-062d2685: every non-null behaviour gets its scalar deleting destructor
    // (vtbl[0](1)) and its slot is cleared.
    for (int Index = 0; Index < 21; ++Index)
    {
        if (BehaviorDispatchTable[Index] != nullptr)
        {
            delete BehaviorDispatchTable[Index];
            BehaviorDispatchTable[Index] = nullptr;
        }
    }

    // .text:062d2687-062d26c6: every queued node through FUN_062d4c50, then the sentinel is
    // freed and Head/Count are zeroed -- ClearNotificationQueue.
    ClearNotificationQueue();

    // .text:062d26c9-062d26e6: the +0xF0 BaseWatcher's destructor (`if ([+0xf0])
    // set_pointer(0)`), unlinking this manager from the owner's watcher chain.
    reinterpret_cast<BaseWatcher*>(&WatchablePtr)->set(nullptr);
}

// FUN_062d2830, item 1547. Common.dll.txt 211484-211525 (35 insns), `RET 4`.
void ConnectBehaviorManagerObjectDb(IBehaviorManager* Self, IObjDB* ObjectDb)
{
    Self->ObjectDb = ObjectDb;                                // .text:062d2840

    // .text:062d2846-062d286a: walk the table from entry 0 while the entry is non-null, at
    // most 21 entries, calling Behavior slot 2 (+0x08) with the stored database (re-read
    // from +0xEC on every pass).
    for (int Index = 0; Index < 21; ++Index)
    {
        if (Self->BehaviorDispatchTable[Index] == nullptr)
            break;
        Self->BehaviorDispatchTable[Index]->Slot2(Self->ObjectDb);
    }

    SetSystemManagerSharedListHead(Self->Sys, ObjectDb);      // .text:062d286c-062d2870
    SetPathfindObjectDb(Self->Pathfind, ObjectDb);            // .text:062d2875-062d2879
}

// ============================================================================
// IBehaviorManager — exported methods
// ============================================================================

bool IBehaviorManager::allow_head_tracking()
{
    // Ghidra @ 0x743E0: if CurrentBehaviorIndex != -1, vcall behavior+0x4c
    // Lines 214317-214325:
    //   MOV EAX, [this+0xc0]
    //   CMP EAX, -1; JZ return_false
    //   MOV ECX, [this+EAX*4+0x174] (behavior ptr)
    //   MOV EAX, [ECX] (vtable)
    //   JMP [EAX+0x4c]
    // No null test on the entry (.text:062d43eb-062d43f4 tail-jumps straight through it);
    // the ctor fills all 21 slots.
    if (CurrentBehaviorIndex == -1)
        return false;
    return BehaviorDispatchTable[CurrentBehaviorIndex]->allow_head_tracking();
}

bool IBehaviorManager::cancel_behavior(CancelRequestType Request)
{
    // Ghidra @ 0x743C0: cancel current behavior
    // Lines 214297-214305:
    //   MOV EAX, [this+0xc0]; CMP EAX,-1; JZ return_true
    //   MOV ECX, [this+EAX*4+0x174] (behavior ptr)
    //   MOV EAX, [ECX] (vtable); JMP [EAX+0x50]
    //   return_true: MOV AL,1; RET
    // No null test on the entry (.text:062d43cb-062d43d4); the ctor fills all 21 slots.
    if (CurrentBehaviorIndex == -1)
        return true;
    Behavior* const Current = BehaviorDispatchTable[CurrentBehaviorIndex];
    // 062d43c0 TAIL-JUMPS into slot 0x50, so the behavior is handed the very
    // CancelRequestType this method was called with. The port used to drop it.
    return Current->cancel_behavior(Request);
}

bool IBehaviorManager::enable_all_maneuvers()
{
    // Ghidra @ 0x74A90: clear maneuver mask
    ManeuverMask = 0;
    return true;
}

bool IBehaviorManager::enable_maneuver(int Maneuver, bool bEnable)
{
    // Ghidra @ 0x74A40 (L107402-107420): only Maneuver == -1 is rejected; the bit is
    // `2 << (Maneuver & 0x1f)` (x86 SHL masks the count to 5 bits) -- i.e. bit
    // (Maneuver+1) mod 32, NOT bit Maneuver. This was previously ported as a plain
    // `1u << Maneuver` with an added `< 32` bounds check, which (a) toggled the wrong
    // bit for every Maneuver value and (b) rejected Maneuver>=32 instead of retail's
    // aliasing wraparound. Confirmed this is the bit scheme external_player_evaluate
    // already reads (`ManeuverMask & (2u << (PlayerIndex & 0x1f))`), so this fix makes
    // the two consistent.
    if (Maneuver == -1)
        return false;
    unsigned Bit = 2u << (static_cast<unsigned>(Maneuver) & 0x1f);
    if (bEnable) {
        ManeuverMask &= ~Bit;
    } else {
        ManeuverMask |= Bit;
    }
    return true;
}

EvaluateResult IBehaviorManager::external_player_evaluate(int PlayerIndex)
{
    // .text:062d3f40-062d3f82, RET 0x4. 062d3f42-062d3f56: LockManeuversByte (+0xbc) set and
    // PlayerIndex != CurrentBehaviorIndex (+0xc0) -> 2. 062d3f58-062d3f65: ManeuverMask
    // (+0xb8) & (2 << (PlayerIndex & 0x1f)) -> 2. 062d3f67-062d3f7a: tail-jump into
    // BehaviorDispatchTable[PlayerIndex] (+0x174) vtable +0x18 (evaluate) with the raw dword
    // at +0xdc (BehaviorTimerC) replacing the argument -- no null test on the entry (the ctor
    // builds all 21).
    if ((LockManeuversByte == 0 || PlayerIndex == CurrentBehaviorIndex) &&
        ((ManeuverMask & (2u << (static_cast<unsigned int>(PlayerIndex) & 0x1f))) == 0))
    {
        Behavior* const Current = BehaviorDispatchTable[PlayerIndex];
        unsigned int ContextParam = 0;
        std::memcpy(&ContextParam, &BehaviorTimerC, sizeof(ContextParam));
        return Current->evaluate(ContextParam);
    }
    return EvalSkip;
}

bool IBehaviorManager::get_camera_level_status()
{
    // Ghidra @ 0x74310: if (CameraLevelStatus == 1) return LevelCameraFlag; return false;
    if (CameraLevelStatus == 1)
        return LevelCameraFlag != 0;
    return false;
}

float IBehaviorManager::get_closest_trailing_ship() const
{
    // Retail is a three-instruction tail-call thunk (Common.dll.txt:214999-215007,
    // decomp-tracker item 12392):
    //   .text:062d4a30  MOV ECX,dword ptr [ECX + 0x8]   ; this->Sys
    //   .text:062d4a33  ADD ECX,0x34                    ; &Sys->Timers, the AI target list
    //   .text:062d4a36  JMP FUN_062e7820
    // FUN_062e7820 is ported as GetClosestTrailingShipDistance (item 1802,
    // common_iustategraph.h) and SystemManager+0x34 is reached through
    // GetAiTargetList. Retail has no null check here, and neither does the port: the ctor
    // always builds Sys.
    return GetClosestTrailingShipDistance(GetAiTargetList(Sys));
}

IDirectiveInfo const* IBehaviorManager::get_current_directive()
{
    // Ghidra @ 0x73E70: return this+0xc4
    return reinterpret_cast<IDirectiveInfo const*>(DirectiveStorage);
}

IObjRW const* IBehaviorManager::get_debugger_target() const
{
    // Common.dll.txt 214940-214958 (.text:062d49d0-062d4a00), 17 instructions:
    //   062d49d3  CMP [ESI+0xc0],-1 / JZ -> XOR EAX,EAX; RET
    //   062d49de  ECX = BehaviorDispatchTable[CurrentBehaviorIndex]; CALL [[ECX]+0x60]
    //             (Behavior slot 24, get_target) -- no null test on the entry
    //   062d49ea  TEST EAX,EAX / JNZ -> return it
    //   062d49ee  ECX = Sys + 0x142c; JMP FUN_062ed5a0 == TargetManager::GetTarget
    if (CurrentBehaviorIndex == -1)
        return nullptr;
    IObjRW const* const Target = BehaviorDispatchTable[CurrentBehaviorIndex]->get_target();
    if (Target != nullptr)
        return Target;
    return Sys->Targeting.GetTarget();
}

bool IBehaviorManager::get_docking_port(IObjRW const*& OutPort, int& OutIndex)
{
    // .text:062d4b30-062d4b80, RET 0x8 (Common.dll.txt 215148-215175). Every failure path is
    // `XOR AL,AL; RET 8` (062d4b7e) and leaves OutPort / OutIndex untouched:
    //   CurrentBehaviorIndex (+0xc0) != 7, DockingPortFlag (+0x169) == 0,
    //   DockingPortStatus (+0x16c) != 1, or [this+0x190] == 0.
    if (CurrentBehaviorIndex != 7 || DockingPortFlag == 0 || DockingPortStatus != 1)
        return false;

    // `MOV EAX,[this+0x190]` is BehaviorDispatchTable[7] with the index constant-folded (the
    // CMP [this+0xc0],7 above), i.e. the DockBehavior; retail does test it (062d4b52).
    DockBehavior const* const Dock = static_cast<DockBehavior const*>(BehaviorDispatchTable[7]);
    if (Dock == nullptr)
        return false;

    // 062d4b56-062d4b6b: Watcher324's watched pointer (+0x324); non-null -> minus 8 (the
    // Watchable base inside IObjRW), else 0, into OutPort. 062d4b6d-062d4b77: Field43c
    // (+0x43c) into OutIndex. true.
    Watchable const* const Watched = Dock->Watcher324.target();
    OutPort = Watched != nullptr ? static_cast<IObjRW const*>(Watched) : nullptr;
    OutIndex = Dock->Field43c;
    return true;
}

unsigned IBehaviorManager::get_parent_id()
{
    // Common.dll.txt 214981-214986 (.text:062d4a10), a six-instruction tail call:
    //   MOV EAX,[ECX+0x8]; ADD EAX,0x14fc; MOV EAX,[EAX+0x84]; MOV EDX,[EAX];
    //   MOV ECX,EAX; JMP [EDX+0x20]
    // 0x14fc + 0x84 == 0x1580 == SystemManager::ExternalIface (static_assert-pinned in
    // common_systemmanager.h), and IObjInspect +0x20 is get_id(). No null test.
    return Sys->SelfTarget.Iface->get_id();
}

void IBehaviorManager::get_personality(pub::AI::Personality& OutPersonality)
{
    // Ghidra @ 0x74430: copy personality struct
    // Lines 214372-214381:
    //   MOV ESI, [this+0x8] (Sys); ADD ESI, 0x1584
    //   MOV EDI, param_1 (OutPersonality)
    //   MOV ECX, 0xf6; REP MOVSD (copy 0xf6*4 = 0x3D8 = 984 bytes, matching
    //   sizeof(pub::AI::Personality) — see common_pub.h for the full layout)
    // SystemManager is now really ported (common_systemmanager.h) -- Sys's
    // PersonalityData member sits at exactly this same +0x1584 offset
    // (static_assert-checked there), so this can now read it directly
    // instead of via raw pointer arithmetic.
    // No Sys test (.text:062d4431): the ctor always builds it.
    OutPersonality = Sys->PersonalityData;
}

pub::AI::ScanResponse IBehaviorManager::get_scan_response(uint Type)
{
    // Common.dll.txt 211849-211851 (.text:062d2b00), three instructions:
    //   MOV ECX,[ECX+0x8]; ADD ECX,0x142c; JMP FUN_062eeb70
    // i.e. SystemManager::Targeting.GetScanResponse(Type) -- see TargetManager in
    // common_iface.h and its body in common_targetmanager.cpp.
    return Sys->Targeting.GetScanResponse(Type);
}

const Vector IBehaviorManager::get_ship_up_direction()
{
    // Ghidra @ 0x74370: copy Vector from this+0x134 to output
    // Lines 214230-214241:
    //   ADD ECX, 0x134; copy 3 dwords to hidden return ptr
    return ShipUpDirection;
}

IStateGraph const* IBehaviorManager::get_state_graph()
{
    // ICF-folded @ 0x71AD0: return *(this+4)
    return StateGraph;
}

SystemManager* IBehaviorManager::get_sys()
{
    // ICF-folded @ 0x466A0: return *(this+8)
    return Sys;
}

bool IBehaviorManager::get_user_turning_input_state()
{
    // Ghidra @ 0x743B0: read byte at this+0x150
    // Line 214278: MOV AL, [this+0x150]; RET
    return UserTurningInputState != 0;
}

bool IBehaviorManager::lock_maneuvers(bool bLock)
{
    // Common.dll.c 0x74b90: this[0xbc] = bLock; return true; -- writes LockManeuversByte
    // directly, does NOT touch ManeuverMask (+0xb8) at all.
    LockManeuversByte = bLock;
    return true;
}

void IBehaviorManager::refresh_state_graph()
{
    // Ghidra @ 0x72890: refresh state graph
    // Lines 211544-211552:
    //   MOV EAX, [this+0x4] (StateGraph); TEST EAX,EAX; JZ ret
    //   MOV EAX, [EAX] (first field = graph ID)
    //   PUSH 1 (bForce=true); PUSH EAX (graphId); CALL set_state_graph
    if (StateGraph != nullptr)
    {
        int GraphId = *reinterpret_cast<int*>(StateGraph);
        set_state_graph(GraphId, true);
    }
}

void IBehaviorManager::reset_current_behavior_direction()
{
    // Ghidra @ 0x73B10: reset behavior direction
    // Lines 213214-213235:
    //   MOV EAX, [ESI+0xc0]; BehaviorDirty = 1
    //   MOV ECX, [ESI+EAX*4+0x174] (behavior ptr)
    //   If non-null: create zero vector {0,0,0}, vcall+0x30(behavior, &zeroVec)
    //   Zero [ESI+0x124], [ESI+0x128], [ESI+0x12c]
    BehaviorDirty = 1;
    // 062d3b16-062d3b2c: no -1 test here, unlike every sibling -- with no current behavior
    // retail reads the dword at +0x170 (TickSuppressedFlag and its padding) as the entry.
    // Очень странно, but that is retail's read; only the loaded pointer is null-tested.
    Behavior* const BehaviorObj = *(Behavior* const*)((char const*)BehaviorDispatchTable + CurrentBehaviorIndex * 4);
    if (BehaviorObj != nullptr)
    {
        Vector ZeroDir = {0.0f, 0.0f, 0.0f};
        BehaviorObj->set_direction(ZeroDir);
    }
    // Zero fields at +0x124, +0x128, +0x12C
    LastCommandedDirection.x = 0.0f;
    LastCommandedDirection.y = 0.0f;
    LastCommandedDirection.z = 0.0f;
}

void IBehaviorManager::set_camera_mode(IBehaviorManager::IBehaviorCameraMode Mode)
{
    // Ghidra @ 0x7A4D0: store mode to this+0x154
    // Lines 223708-223710:
    //   MOV EAX, param_1; MOV [this+0x154], EAX; RET
    CameraLevelStatus = static_cast<int>(Mode);
}

void IBehaviorManager::set_content_callback(pub::AI::ContentCallback* Callback)
{
    // .text:062d3ef0..062d3f0d: store the pointer to [this+0x110]; when it is
    // non-null, a __thiscall virtual call through [vptr+0x24] (get_creation_type,
    // ECX = Callback, no stack arguments) and the result to [this+0x114].
    // The earlier port called slot 9 through a __stdcall pointer, pushing the
    // object as a stack argument that the callee's plain RET never pops.
    ContentCbPtr = Callback;
    if (Callback != nullptr)
    {
        ContentCbCreationType = static_cast<unsigned>(Callback->get_creation_type());
    }
}

bool IBehaviorManager::set_current_directive(IDirectiveInfo const& Dir,
                                              pub::AI::BaseOp const* Op)
{
    // .text:062d3d70-062d3e60 (Common.dll.txt 213539-213634), RET 8. Rewritten 2026-09-24
    // (claude-code:w4-behaviormgr-stubs): the earlier body skipped both FUN_062d3420 calls
    // and FUN_062ec970 ("BLOCKED"), and on every other directive returned true without
    // touching a behavior. All three are ported now: FUN_062d3420 is SetCurrentBehavior,
    // FUN_062ec970 is SetFollowLeader (common_gunconvergence.cpp).
    //
    // 062d3d79-062d3d8e: the three dwords go to +0xC4 first; every branch reads them back.
    std::memcpy(DirectiveStorage, &Dir, sizeof(DirectiveStorage));
    int* const Stored = reinterpret_cast<int*>(DirectiveStorage);
    int const* const Incoming = reinterpret_cast<int const*>(&Dir);

    if (Incoming[0] == 0)
    {
        // 062d3d9c-062d3de0: no directive. Zero +0xCC, cancel the current behavior (slot
        // 23, +0x5C -- no null test on the table entry), fall back to Field120 without a
        // directive argument, then drop the follow leader.
        Stored[2] = 0;
        if (CurrentBehaviorIndex != -1)
            BehaviorDispatchTable[CurrentBehaviorIndex]->cancel_notify();
        SetCurrentBehavior(static_cast<int>(Field120), false, nullptr);
        Sys->GunConvergenceStateBytes.SetFollowLeader(nullptr);
        return true;
    }

    // 062d3de3-062d3e08: +0xC8 == 1 and the new directive is type 1 -- switch straight to
    // the behavior index it carries (Dir+8), handing it Op. The result is discarded.
    if (Stored[1] == 1 && Incoming[0] == 1)
    {
        SetCurrentBehavior(Incoming[2], false, const_cast<pub::AI::BaseOp*>(Op));
        return true;
    }

    // 062d3e0b-062d3e23: cancel all 21 behaviors, no null test.
    for (int Index = 0; Index < 21; ++Index)
        BehaviorDispatchTable[Index]->cancel_notify();

    // 062d3e25-062d3e44: a type-1 directive forces the behavior at +0xCC with Op and
    // returns SetCurrentBehavior's own result (AL is left as the callee set it).
    if (Stored[0] == 1)
        return SetCurrentBehavior(Stored[2], true, const_cast<pub::AI::BaseOp*>(Op));

    // 062d3e47-062d3e60: anything else falls back to Field120; returns local_1 = 1.
    SetCurrentBehavior(static_cast<int>(Field120), false, nullptr);
    return true;
}

void IBehaviorManager::set_directive_callback(pub::AI::DirectiveCallback* Callback)
{
    // Ghidra @ 0x73EB0: set directive callback
    // Lines 213705-213728:
    //   Loop i=0..4 over DirectiveCallbacks[i] at this+0xFC+i*4:
    //     If slot is null OR slot == Callback → store Callback in that slot, return
    //   (If all 5 full and none match, callback is dropped)
    for (int I = 0; I < 5; ++I)
    {
        if (DirectiveCallbacks[I] == nullptr || DirectiveCallbacks[I] == Callback)
        {
            DirectiveCallbacks[I] = Callback;
            return;
        }
    }
}

void IBehaviorManager::clear_directive_callback(pub::AI::DirectiveCallback* Callback)
{
    // Ghidra @ 0x62d3f10. Loop i=0..4 over DirectiveCallbacks[i] at this+0xFC+i*4:
    // zero any slot equal to Callback. Unlike set_directive_callback, this does NOT
    // return on the first match -- retail scans and clears all 5 slots.
    for (int I = 0; I < 5; ++I)
    {
        if (DirectiveCallbacks[I] == Callback)
        {
            DirectiveCallbacks[I] = nullptr;
        }
    }
}

pub::AI::OP_RTYPE IBehaviorManager::set_directive_priority(
    pub::AI::DirectivePriority Priority)
{
    // Ghidra @ 0x73E80
    if (*reinterpret_cast<int*>(DirectiveStorage) != 0)
    {
        *reinterpret_cast<pub::AI::DirectivePriority*>(DirectiveStorage + 4) = Priority;
        return static_cast<pub::AI::OP_RTYPE>(1);
    }
    return static_cast<pub::AI::OP_RTYPE>(3);
}

void IBehaviorManager::set_personality(pub::AI::Personality const& Personality)
{
    // Ghidra @ 0x74400: copy personality to Sys+0x1584
    // Lines 214340-214352:
    //   MOV EDI, [EAX+0x8] (Sys); ADD EDI, 0x1584
    //   MOV ECX, 0xf6; REP MOVSD (copy 0xf6*4 = 0x3D8 = 984 bytes, matching
    //   sizeof(pub::AI::Personality) — see common_pub.h for the full layout)
    //   CALL FUN_062e6950(Sys) — notify personality changed
    // No Sys test (.text:062d4408): the ctor always builds it.
    Sys->PersonalityData = Personality;
    // FUN_062e6950 (item 1788): rebuilds the weapon fire-control config, the eleven
    // Personality-derived timer thresholds and the RepairManager's helper thresholds.
    NotifySystemManagerPersonalityChanged(Sys);
}

void IBehaviorManager::set_ship_up_direction(Vector const& Dir)
{
    // Ghidra @ 0x74350: copy Vector to this+0x134
    // Lines 214208-214216:
    //   ADD ECX, 0x134; copy 3 dwords from param_1
    ShipUpDirection = Dir;
}

void IBehaviorManager::set_state_graph(int GraphId, bool bForce)
{
    // Ghidra @ 0x728B0: pub::StateGraph::get_state_graph_internal
    // Lines 211576-211658:
    //   If StateGraph != null && *StateGraph == GraphId && !bForce → return
    //   WatchablePtr vcall+0xbc (check valid); if true, clear Flags1c8 bit 0
    //   WatchablePtr vcall+0x88 → get flags; if bits & 0xFDFF → clear Flags1c8 bit 0
    //   Call pub::StateGraph::get_state_graph_internal(GraphId) → new graph ptr
    //   If new ptr == old StateGraph → return
    //   Update StateGraph = new ptr
    //   If null → FDUMP warning
    //   Else → call set_current_directive with empty directive

    if (StateGraph != nullptr)
    {
        int CurrentGraphId = *reinterpret_cast<int*>(StateGraph);
        if (CurrentGraphId == GraphId && !bForce)
            return;
    }

    // 062d28d2-062d28f9: the watched object (`[this+0xf0]`, `raw ? raw-8 : 0`, called with
    // no null test) is asked is_player() (slot +0xbc); for the player the graph id becomes 0
    // and Flags1c8 bit 0 is cleared.
    int EffectiveGraphId = GraphId;
    IObjInspect const* const Self = ObjInspectOf(ObjRwFromWatchable(WatchablePtr));
    if (Self->is_player())
    {
        Flags1c8 &= 0xFE;
        EffectiveGraphId = 0;
    }

    // 062d28ff-062d2927: get_type (slot +0x88) into bForce's own stack slot, then
    // `TEST [ESP+0x1c],0xfdff` -- any type bit other than 0x200 clears Flags1c8 bit 0 too.
    unsigned int Type = static_cast<unsigned int>(bForce);   // the reused param_2 dword
    ObjInspectOf(ObjRwFromWatchable(WatchablePtr))->get_type(Type);
    if ((Type & 0xfdff) != 0)
        Flags1c8 &= 0xFE;

    // 062d292e-062d293c: same graph -> nothing more.
    IStateGraph const* NewGraph = pub::StateGraph::get_state_graph_internal(EffectiveGraphId);
    if (NewGraph == StateGraph)
        return;
    StateGraph = const_cast<IStateGraph*>(NewGraph);
    if (NewGraph == nullptr)
    {
        // 062d2945-062d296f.
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
              "E:\\FL\\Scratch\\Source\\Common\\Ai\\IBehaviorManager.cpp", 0xce,
              "IBehaviorManager: state graph not found in database!");
        return;
    }

    // LAB_062d2972: set_current_directive({0, 0, -1}, nullptr) -- the stack directive's
    // dwords are written as [+0]=0, [+4]=0, [+8]=0xffffffff (062d297b-062d298b).
    IDirectiveInfo Dir;
    int const DirDwords[3] = { 0, 0, -1 };
    std::memcpy(Dir.Data, DirDwords, sizeof(DirDwords));
    set_current_directive(Dir, nullptr);
}

void IBehaviorManager::set_turn_sensitivity(float Sensitivity)
{
    // Common.dll.c 0x062d4aa0: clamp to [0, 0.9], then store 1.0 - clamped.
    // (Not a plain clamped setter -- the stored field is an inverted factor,
    // consumed directly by get_turn_sensitivity_factor / 0x062d4b20.)
    // 062d4aa4-062d4b0b: store, then `FCOMP 0.0f; TEST AH,0x41; JNZ` -> 0.0f on <=, == and
    // unordered; else `FCOMP 0.9f (DAT_0639c470); TEST AH,0x5; JP` -> 0.9f on >=; the third
    // (`<= 0` again) test is dead. Stored as 1.0f - clamped.
    TurnSensitivity = Sensitivity;
    if (!(TurnSensitivity > 0.0f)) {
        TurnSensitivity = 0.0f;
    } else if (!(TurnSensitivity < 0.9f)) {
        TurnSensitivity = 0.9f;
    }
    TurnSensitivity = 1.0f - TurnSensitivity;
}

float IBehaviorManager::get_turn_sensitivity_factor() const
{
    // Common.dll.c 0x062d4b20: FLD [ECX+0x164]; RET
    return TurnSensitivity;
}

void IBehaviorManager::set_user_turning_input_state(bool bState)
{
    // Ghidra @ 0x743A0: store byte to this+0x150
    // Line 214266: MOV [this+0x150], AL; RET
    UserTurningInputState = bState ? 1 : 0;
}

void IBehaviorManager::submit_camera_up(Vector const& Up)
{
    // Ghidra @ 0x74330: copy Vector to this+0x140
    // Lines 214188-214196:
    //   ADD ECX, 0x140; copy 3 dwords from param_1
    CameraUp = Up;
}

void IBehaviorManager::update_current_behavior_afterburner(bool bOn)
{
    // Ghidra @ 0x73C60: vcall+0x40 on current behavior
    // Lines 213378-213391:
    //   Check CurrentBehaviorIndex != -1
    //   Load behavior ptr from [this+EAX*4+0x174]
    //   Set BehaviorDirty = 1
    //   JMP [vtable+0x40]
    if (CurrentBehaviorIndex == -1)
        return;
    Behavior* const Current = BehaviorDispatchTable[CurrentBehaviorIndex];
    if (Current == nullptr)
        return;
    BehaviorDirty = 1;
    Current->set_afterburner(bOn);
}

void IBehaviorManager::update_current_behavior_auto_avoidance(bool bOn)
{
    // Common.dll.txt 213497-213501 (.text:062d3d40-062d3d4e):
    //   MOV EAX,[ESP+4]; MOV ECX,[ECX+0x8]; MOV [ESP+4],EAX; ADD ECX,0x18;
    //   JMP 06263bd0
    // ECX = &Sys->Thrust, and 06263bd0 is the ICF-folded byte setter Ghidra names
    // CNudgeEngine::Activate -- on this receiver it writes ThrustManager::bAutoAvoidance.
    // (The old port read "CNudgeEngine at Sys+0x18" off that label and did nothing.)
    Sys->Thrust.SetAutoAvoidance(bOn);
}

void IBehaviorManager::update_current_behavior_auto_level(bool bOn)
{
    // Common.dll.txt 213456-213476 (.text:062d3cf0-062d3d2e), 18 instructions:
    //   062d3cfa  ECX = [ESI+8] + 0x18; PUSH bOn; CALL FUN_062e92d0
    //             -> ThrustManager::SetSteeringFallbackFlag on &Sys->Thrust
    //   062d3d06  a zeroed 12-byte local, ECX = this, CALL update_current_behavior_direction
    Sys->Thrust.SetSteeringFallbackFlag(bOn ? 1 : 0);

    Vector ZeroDir = {0.0f, 0.0f, 0.0f};
    update_current_behavior_direction(ZeroDir);
}

void IBehaviorManager::update_current_behavior_brake_reverse(bool bOn)
{
    // Ghidra @ 0x73CC0: vcall+0x48 on current behavior
    // Lines 213430-213443: same dispatch pattern, vtable offset 0x48
    if (CurrentBehaviorIndex == -1)
        return;
    Behavior* const Current = BehaviorDispatchTable[CurrentBehaviorIndex];
    if (Current == nullptr)
        return;
    BehaviorDirty = 1;
    Current->brake_reverse(bOn);
}

void IBehaviorManager::update_current_behavior_cruise(bool bOn)
{
    // Ghidra @ 0x73BE0: update cruise state
    if (CurrentBehaviorIndex == -1)
        return;
    Behavior* const Current = BehaviorDispatchTable[CurrentBehaviorIndex];
    if (Current == nullptr)
        return;

    // 062d3bfa-062d3c1f: Pathfind (+0x0C) non-null and FUN_062de160
    // (PathfindManager::IsCruiseUnobstructed) true -> byte +0xd8 = 1, slot 0x38 set_cruise.
    if (Pathfind != nullptr && Pathfind->IsCruiseUnobstructed())
    {
        BehaviorDirty = 1;
        Current->set_cruise(bOn);
        return;
    }

    // 062d3c22-062d3c4c: fallback through the watched owner, `[+0xf0] ? [+0xf0]-8 : 0`,
    // IObjAffect slot 0x6c(4, 0) -- both arms dispatch, no null test.
    ObjAffectOf(ObjRwFromWatchable(WatchablePtr))->slot_6c(4, 0);
}

void IBehaviorManager::update_current_behavior_direction(Vector const& Dir)
{
    // FUN_062d3690 (exported), Common.dll.txt 062d3690-062d3ae5, 292 instructions, RET 4.
    // Frame: `SUB ESP,0xb8` + EBX/EBP, E = ESP after them. E+0x0b bIsZero, E+0x0c Clamped.

    // 062d3696-062d3719: x and y clamped to [-1, 1] against the doubles DAT_063a03f0 (-1.0)
    // and DAT_06399410 (1.0) -- `TEST AH,0x5; JP` keeps x unless strictly below -1, `TEST
    // AH,0x41; JNZ` keeps it unless strictly above 1. z is not clamped.
    Vector Clamped = Dir;
    if (Clamped.x < -1.0) Clamped.x = -1.0f;
    if (Clamped.x > 1.0) Clamped.x = 1.0f;
    if (Clamped.y < -1.0) Clamped.y = -1.0f;
    if (Clamped.y > 1.0) Clamped.y = 1.0f;

    // 062d3719-062d3764: bIsZero = ((z*z + y*y) + x*x == 0.0f) (`TEST AH,0x44; JNP` -- the
    // equality idiom, unordered counts as not equal); UserTurningInputState = !bIsZero.
    bool const bIsZero = ((static_cast<double>(Clamped.z) * Clamped.z + static_cast<double>(Clamped.y) * Clamped.y) +
                          static_cast<double>(Clamped.x) * Clamped.x) == 0.0;
    UserTurningInputState = bIsZero ? 0 : 1;

    // DAT_063a033c (0x3f34fdf4, 0.707f) and DAT_063a0350 (0x3dcccccd, 0.1f).
    unsigned int const LevelThresholdBits = 0x3f34fdf4u;
    float LevelThreshold;
    std::memcpy(&LevelThreshold, &LevelThresholdBits, sizeof(LevelThreshold));
    unsigned int const BlendBits = 0x3dcccccdu;
    float Blend;
    std::memcpy(&Blend, &BlendBits, sizeof(Blend));

    if (CurrentBehaviorIndex != -1 && BehaviorDispatchTable[CurrentBehaviorIndex] != nullptr)
    {
        Behavior* const BehaviorObj = BehaviorDispatchTable[CurrentBehaviorIndex];
        BehaviorDirty = 1;   // 062d3780, stored before the CameraLevelStatus branch

        if (CameraLevelStatus != 1 || LevelCameraFlag == 0)
        {
            // LAB_062d3aaf.
            ShipUpDirFlag = 0;
            BehaviorObj->set_direction(Clamped);   // slot +0x30
        }
        else
        {
            // 062d379b-062d37c9: the watched ship (`raw ? raw-8 : 0`, unguarded) frame,
            // Rotation copied to E+0x78 (ShipRotation).
            IObjInspect const* const Ship = ObjInspectOf(ObjRwFromWatchable(WatchablePtr));
            Matrix const ShipRotation = Ship->get_transform().Rotation;

            // 062d37cb-062d380b: E+0x3c is set to the identity (Frame).
            Matrix Frame{};
            Frame.E[0][0] = 1.0f;
            Frame.E[1][1] = 1.0f;
            Frame.E[2][2] = 1.0f;

            if (LevelCameraFlag != 0)
            {
                // 062d3819-062d3843: a second get_transform copy (E+0x9c).
                Matrix const ShipRotation2 = Ship->get_transform().Rotation;
                if (bIsZero)
                {
                    // 062d384b-062d3891: |dot(column 2, (0, 1, 0))| (FUN_062d3af0, then
                    // FUN_062c47a0's (z*z' + y*y') + x*x' left on the x87 stack, FABS) against
                    // DAT_063a033c: `TEST AH,0x41; JNZ` skips unless the threshold is
                    // strictly greater.
                    Vector const WorldUp{ 0.0f, 1.0f, 0.0f };
                    Vector const Column2 = MatrixColumn2(ShipRotation2);
                    double const DotUp = (static_cast<double>(Column2.z) * WorldUp.z +
                                          static_cast<double>(Column2.y) * WorldUp.y) +
                                         static_cast<double>(Column2.x) * WorldUp.x;
                    if (LevelThreshold > std::fabs(DotUp))
                    {
                        // 062d3893-062d38d8: the same re-level RelevelShipUpDirection does.
                        // The (0, 1, 0) temporary is built in Frame's first three floats
                        // (`LEA ECX,[ESP+0x44]` at depth E-8), so they are clobbered too.
                        LevelCameraFlag = 0;
                        if (Sys->Thrust.CheckSteeringGate())
                        {
                            Frame.E[0][0] = 0.0f;
                            Frame.E[0][1] = 1.0f;
                            Frame.E[0][2] = 0.0f;
                            ShipUpDirection = Vector{ 0.0f, 1.0f, 0.0f };
                            ShipUpDirFlag = 0;
                        }
                        LevelCameraFlag = 1;
                    }
                }
            }
            // LAB_062d38df / LAB_062d38e7: with the stick deflected, the up hint is the
            // camera's up (+0x140).
            if (!bIsZero)
                ShipUpDirection = CameraUp;

            // LAB_062d3903-062d396e: Forward = -(column 2 of ShipRotation) (E+0x30), UpHint =
            // a copy of ShipUpDirection (E+0x18), threshold = (float)(0.1f + 0.707f)
            // (DAT_063a0350 + DAT_063a033c, FADD then FSTP float); FUN_062e3460 builds Frame.
            Vector const Forward{ -ShipRotation.E[0][2], -ShipRotation.E[1][2], -ShipRotation.E[2][2] };
            Vector const UpHint = ShipUpDirection;
            float const FrameThreshold = static_cast<float>(static_cast<double>(Blend) + LevelThreshold);
            Vector Steer;
            if (BuildPerpendicularFrame(Forward, &Frame, FrameThreshold, &UpHint))
            {
                // 062d3980-062d3a18: ShipRotation^T applied to Frame's columns 0 and 1
                // (I3DMathEngine::TransposeTransform, +0x38).
                I3DMathEngine* const Math = Acquire3DMathEngine();
                FVector const Column0{ Frame.E[0][0], Frame.E[1][0], Frame.E[2][0] };
                FVector Local0;
                Math->TransposeTransform(Local0, reinterpret_cast<FMatrix const&>(ShipRotation), Column0);
                FVector const Column1{ Frame.E[0][1], Frame.E[1][1], Frame.E[2][1] };
                FVector Local1;
                Math->TransposeTransform(Local1, reinterpret_cast<FMatrix const&>(ShipRotation), Column1);

                // 062d3a18-062d3a87: x = L0.x*cx - L1.x*cy and y = L1.y*cy - L0.y*cx on the
                // x87 stack (one FSTP float each); the computed z (L1.z + L0.z) is overwritten
                // by the clamped z at 062d3a83-062d3a87.
                Steer.x = static_cast<float>(static_cast<double>(Local0.X) * Clamped.x -
                                             static_cast<double>(Local1.X) * Clamped.y);
                Steer.y = static_cast<float>(static_cast<double>(Local1.Y) * Clamped.y -
                                             static_cast<double>(Local0.Y) * Clamped.x);
                Steer.z = Clamped.z;
            }
            else
            {
                // LAB_062d3a91.
                Steer = Vector{ 0.0f, 0.0f, 0.0f };
            }
            BehaviorObj->set_direction(Steer);   // LAB_062d3aba, slot +0x30
        }
    }

    // LAB_062d3ac2: always store the clamped direction, even on early-out.
    LastCommandedDirection = Clamped;
}

void IBehaviorManager::update_current_behavior_engage_engine(bool bOn)
{
    // Ghidra @ 0x73BB0 / raw asm L213280-213291 (tail JMP [EAX+0x3c], no other writes):
    // dispatch pattern, vtable offset 0x3c. NOTE: unlike every sibling
    // update_current_behavior_* function, retail does NOT set BehaviorDirty here --
    // confirmed against the raw disassembly (no MOV [ECX+0xd8],1 anywhere in this
    // function). A previous version of this port incorrectly added that write; removed.
    if (CurrentBehaviorIndex == -1)
        return;
    Behavior* const Current = BehaviorDispatchTable[CurrentBehaviorIndex];
    if (Current == nullptr)
        return;
    Current->engage_engine(bOn);
}

void IBehaviorManager::update_current_behavior_slide_strafe_burst(StrafeDir Dir)
{
    // Ghidra @ 0x73C90: vcall+0x44 on current behavior
    // Lines 213401-213412: same dispatch pattern, vtable offset 0x44
    if (CurrentBehaviorIndex == -1)
        return;
    Behavior* const Current = BehaviorDispatchTable[CurrentBehaviorIndex];
    if (Current == nullptr)
        return;
    BehaviorDirty = 1;
    Current->slide_strafe_burst(Dir);
}

void IBehaviorManager::update_current_behavior_throttle(float Throttle)
{
    // Ghidra @ 0x73B80: vcall+0x34 on current behavior
    // Lines 213257-213270:
    //   Check CurrentBehaviorIndex != -1
    //   Load behavior ptr; if null → return
    //   Set BehaviorDirty = 1
    //   JMP [vtable+0x34]
    if (CurrentBehaviorIndex == -1)
        return;
    Behavior* const Current = BehaviorDispatchTable[CurrentBehaviorIndex];
    if (Current == nullptr)
        return;
    BehaviorDirty = 1;
    Current->set_throttle(Throttle);
}

void IBehaviorManager::update_level_camera(bool bEnable)
{
    // Ghidra @ 0x73D60: store byte to this+0xF9
    // Lines 213523-213525:
    //   MOV AL, param_1; MOV [this+0xf9], AL; RET
    LevelCameraFlag = bEnable ? 1 : 0;
}

// ============================================================================
// IBehaviorManager — notification / delayed-callback queue
// ============================================================================
//
// See NotificationNode's own header comment (common_iface.h) for the full
// struct-layout writeup that grounds everything below. This section ports the
// queue's real operations: alloc/link/unlink primitives, the per-tick expiry
// walk, and the two delayed-op kinds (QueueNotification/InsertNotification) a
// node's Mode selects. 2026-09-11, d294:common-port, decomp-tracker items
// 1553 (FUN_062d2b10, the dependent) / 1560-1570 (this cluster).

void IBehaviorManager::InitNotificationQueue()
{
    // Bootstraps a self-referencing sentinel node -- FUN_062d4cf0(0, 0)
    // (Common.dll.txt 215396-215418) produces exactly this shape when both of
    // its "next"/"prev" arguments are null. FUN_062d4bf0/FUN_062d4c90's
    // teardown loops (Common.dll.txt 215253-215297 / 215342-215386) both
    // explicitly free() this same pointer at the very end, proving it is a
    // real, separately heap-allocated node -- not data embedded inline at
    // +0x158 (that offset is never read or written by any function in this
    // cluster; see the field's own comment in common_iface.h). The retail ctor
    // that performs this bootstrap (FUN_062d2220) was analyzed by a prior pass
    // without this queue in view; wiring it into our own ctor here reproduces
    // the required invariant (Head always dereferenceable, self-pointing when
    // empty) without needing to re-derive FUN_062d2220's own exact instruction
    // sequence.
    NotificationQueueHead = AllocNotificationNode(nullptr, nullptr);
    NotificationCount = 0;
}

NotificationNode* IBehaviorManager::AllocNotificationNode(
    NotificationNode* NextIfAny, NotificationNode* PrevIfAny)
{
    // FUN_062d4cf0, Common.dll.txt 215396-215418. Raw operator_new(0x24);
    // retail leaves WatcherA/WatcherB/Field1C/Timer uninitialized here,
    // relying on the caller to fill them in immediately afterward
    // (QueueNotification/InsertNotification do so directly below; the
    // sentinel bootstrap above never touches them again). Constructing via
    // placement-new instead keeps BaseWatcher's own invariant (Watched/Next
    // both start null) intact, behaviorally identical to retail's explicit
    // zeroing wherever a payload actually gets filled in (FUN_062d4d60).
    // malloc (not ::operator new) so the matching teardown can use plain
    // free() -- retail's own FUN_06391d5a is a free() wrapper (confirmed
    // project-wide, see the other free() sites tagged FUN_06391d5a, e.g. common_animdb.cpp).
    void* Mem = malloc(sizeof(NotificationNode));
    NotificationNode* Node = new (Mem) NotificationNode();
    Node->Next = (NextIfAny != nullptr) ? NextIfAny : Node;
    Node->Prev = (PrevIfAny != nullptr) ? PrevIfAny : Node;
    return Node;
}

NotificationNode* IBehaviorManager::InsertNotificationNodeBefore(NotificationNode* Anchor)
{
    // Consolidates FUN_062d4cf0 + its caller-side linking, which retail
    // duplicates twice: inlined directly inside FUN_062d4520 (Common.dll.txt
    // 214575-214599) and factored out via a direct FUN_062d4cf0 call inside
    // FUN_062d4750 (Common.dll.txt 214799-214819). Both are the identical
    // "insert new node immediately before Anchor" doubly-linked-list sequence:
    // new.next=Anchor; new.prev=Anchor.prev; Anchor.prev=new; oldPrev.next=new.
    NotificationNode* OldPrev = Anchor->Prev;
    NotificationNode* Node = AllocNotificationNode(Anchor, OldPrev);
    Anchor->Prev = Node;
    OldPrev->Next = Node;
    ++NotificationCount;
    return Node;
}

void IBehaviorManager::ReleaseNotificationWatchers(NotificationNode* Node)
{
    // FUN_062d46f0, Common.dll.txt 214665-214704. Retail: if WatcherA/WatcherB's
    // own Watched field is non-null, call set_pointer(nullptr) on it. Ported via
    // the public BaseWatcher::set() API -- identical effect, and the same
    // substitution common_basewatcher.cpp already documents for other embedded
    // BaseWatchers in this module (FUN_062c3040/FUN_062c5c20/FUN_062d27b0)
    // rather than needing friend access to protected set_pointer.
    // Reverse member order, as retail's inlined payload dtor: ECX = node+8, `[ESI+0xc]` (node+0x14,
    // WatcherB) is released first at 062d470d-062d4721, then `[ESI+0x4]` (node+0xC, WatcherA) at
    // 062d4726-062d473b.
    Node->WatcherB.set(nullptr);
    Node->WatcherA.set(nullptr);
}

void IBehaviorManager::ClearNotificationQueue()
{
    // FUN_062d4bf0 (Common.dll.txt 215253-215297) and FUN_062d4c90 (215342-
    // 215386) are the same full-queue teardown loop, differing only in how
    // each retires a node's watchers -- FUN_062d4bf0 calls FUN_062d46f0
    // (ReleaseNotificationWatchers, i.e. set(nullptr)), FUN_062d4c90 calls
    // BaseWatcher::~BaseWatcher directly, which is ITSELF implemented as
    // set(nullptr) (common_basewatcher.cpp) -- so both are behaviorally identical
    // and collapse to one ported body here. Walks from the sentinel's own
    // Next around to itself, unlinking+freeing each real node and
    // decrementing NotificationCount, then frees the sentinel itself and
    // zeroes Head/Count -- matching both functions' shared tail exactly.
    NotificationNode* Sentinel = NotificationQueueHead;
    NotificationNode* Cur = Sentinel->Next;
    while (Cur != Sentinel)
    {
        NotificationNode* Next = Cur->Next;
        Cur->Prev->Next = Cur->Next;
        Cur->Next->Prev = Cur->Prev;
        ReleaseNotificationWatchers(Cur);
        free(Cur); // FUN_06391d5a
        --NotificationCount;
        Cur = Next;
    }
    free(Sentinel); // FUN_06391d5a
    NotificationQueueHead = nullptr;
    NotificationCount = 0;
}

// FUN_062d4c50 (std::list::erase(Out, Node), Common.dll.txt 215307-215338) has no body of its
// own here: its only retail caller is ~IBehaviorManager's teardown loop (.text:062d26ac), and
// the loop body above is that erase -- successor captured, unlink, watchers, free, count--.

void IBehaviorManager::UpdateNotificationQueue(float Dt)
{
    // FUN_062d4450, Common.dll.txt 214387-214479. Walks every real node once,
    // decrementing its Timer by Dt; a node whose Timer is now <= 0.0f fires the
    // op its own Mode selected at insert time, then the node is unlinked and
    // freed and NotificationCount is decremented. A node that hasn't expired is
    // simply left in place and the walk continues.
    //
    // 062d4453-062d445e: `MOV EAX,[EBX+0x15c]; MOV ESI,[EAX]; CMP ESI,EAX; JZ` -- the sentinel is
    // dereferenced unconditionally (the ctor always builds it, only the dtor frees it).
    NotificationNode* Cur = NotificationQueueHead->Next;
    while (Cur != NotificationQueueHead)                                  // 062d450b CMP ESI,[EBX+0x15c]
    {
        // 062d4466-062d447e: FLD [ESI+0x20]; FSUB Dt; FSTP [ESI+0x20]; FLD [ESI+0x20];
        // FCOMP 0.0f (DAT_06399408); FNSTSW AX; TEST AH,0x41; JP 062d4509.
        // AH&0x41 has even parity when it is 0x00 (Timer > 0) or 0x41 (unordered): JP is taken
        // then, and 062d4509 just steps to Cur->Next. Odd parity (C0 alone: Timer < 0, or C3
        // alone: Timer == 0) falls through into the dispatch.
        Cur->Timer -= Dt;
        if (Cur->Timer > 0.0f || Cur->Timer != Cur->Timer)
        {
            Cur = Cur->Next;                                              // 062d4509 MOV ESI,[ESI]
            continue;
        }

        // Expired -- fire the delayed op this node's Mode selected, passing
        // the raw (pre "+8 Watchable adjust") pointer recovered from the
        // relevant watcher (Watched-8, matching BaseWatcher(IObjRW*)'s own
        // "+8" convention in reverse) and Field1C as Extra, both with
        // Duration == 0.0f -- which in both callees is the IMMEDIATE path
        // (fan-out / switch), never another queue insert.
        if (Cur->Mode == 0)
        {
            // caseD_0 (Common.dll.txt 214437-214452): WatcherA supplies the
            // raw target (or null), Field1C forwarded as Extra.
            Watchable const* WatchedA = Cur->WatcherA.target();
            IObjRW* RawA = (WatchedA != nullptr)
                ? reinterpret_cast<IObjRW*>(const_cast<char*>(
                      reinterpret_cast<char const*>(WatchedA) - 8))
                : nullptr;
            QueueNotification(RawA, Cur->Field1C, 0.0f);
        }
        else if (Cur->Mode == 1)
        {
            // caseD_1 (Common.dll.txt 214417-214436): WatcherB supplies the
            // raw target (or null), Field1C forwarded as Extra.
            Watchable const* WatchedB = Cur->WatcherB.target();
            IObjRW* RawB = (WatchedB != nullptr)
                ? reinterpret_cast<IObjRW*>(const_cast<char*>(
                      reinterpret_cast<char const*>(WatchedB) - 8))
                : nullptr;
            InsertNotification(RawB, Cur->Field1C, 0.0f);
        }
        // Mode >= 2: no-op -- the node is simply retired (Common.dll.txt
        // 214427: JNZ past both dispatch cases straight to the shared cleanup).

        // 062d44d9: `MOV EDI,[ESI]` -- the successor is read AFTER the dispatch, not before.
        NotificationNode* Next = Cur->Next;
        Cur->Prev->Next = Cur->Next;
        Cur->Next->Prev = Cur->Prev;
        ReleaseNotificationWatchers(Cur);
        free(Cur); // FUN_06391d5a
        --NotificationCount;
        Cur = Next;                                                       // 062d4505 MOV ESI,EDI
    }
}

void IBehaviorManager::QueueNotification(IObjRW* Target, unsigned Extra, float Duration)
{
    // FUN_062d4520, Common.dll.txt 214483-214649. Entry gates on Duration
    // (retail param_3) -- "notify Target's formation followers, now or after Duration seconds":
    //   062d4526-062d454d: FLD Duration; FCOMP 0.0f (DAT_06399408); FNSTSW AX;
    //   TEST AH,0x41; JNZ 062d468b.
    // JNZ is taken when C0 or C3 is set: Duration < 0, Duration == 0, or unordered -- the
    // IMMEDIATE fan-out at 062d468b. Only Duration > 0 (C0 = C3 = 0) falls through into the
    // node insert, which re-enters here with Duration 0 once the timer runs out
    // (UpdateNotificationQueue, Mode 0). Until 2026-10-09 the port had this gate inverted:
    // Duration == 0 inserted a node with Timer 0, the walk fired it, it inserted another,
    // and with two nodes queued the walk never reached the sentinel (ASan OOM, CMN-BUG8).
    if (Duration > 0.0f)
    {
        // 062d4553-062d4689: two on-stack watchers -- local_64 (-> WatcherA) gets Target+8
        // (or null), local_5c (-> WatcherB) stays null; local_68 (Mode) = 0, local_54 (Field1C)
        // = Extra, local_50 (Timer) = Duration. Node operator_new(0x24) inlined at
        // 062d460d-062d4639 and linked before the sentinel, FUN_062d4d60 copies the payload,
        // NotificationCount++ at 062d4649-062d4653.
        NotificationNode* Node = InsertNotificationNodeBefore(NotificationQueueHead);
        Node->Mode = 0;
        Node->WatcherA.set((Target != nullptr)
            ? reinterpret_cast<Watchable const*>(reinterpret_cast<char const*>(Target) + 8)
            : nullptr);
        Node->WatcherB.set(nullptr);
        Node->Field1C = Extra;
        Node->Timer = Duration;
        return;
    }

    // 062d468b-062d4694: no Target -> nothing.
    if (Target == nullptr)
        return;
    // 062d4696-062d46a5: `PUSH 0x10; LEA EAX,[ESP+0x34] (local_4c, 0x40 bytes); PUSH EAX;
    // CALL [EDX+0x14c]` -- IObjInspect slot 83 get_formation_followers(Out, 16).
    IObjRW* FollowerBuf[16];
    int const Count = ObjInspectOf(Target)->get_formation_followers(FollowerBuf, 16);
    // 062d46a7-062d46ca: `TEST EDI,EDI; JLE` then for I in [0, Count): every non-null follower
    // gets FUN_062d4750 (InsertNotification) with this call's Extra and Duration 0 -- the
    // follower's own manager runs the Extra switch immediately.
    for (int I = 0; I < Count; ++I)
    {
        if (FollowerBuf[I] != nullptr)
            InsertNotification(FollowerBuf[I], Extra, 0.0f);
    }
}

void IBehaviorManager::InsertNotification(IObjRW* Target, unsigned Extra, float Duration)
{
    // FUN_062d4750, Common.dll.txt 214706-214920. Same dual-mode entry gate as
    // QueueNotification:
    //   062d4756-062d477b: FLD Duration; FCOMP 0.0f (DAT_06399408); FNSTSW AX;
    //   TEST AH,0x41; JNZ 062d48d0.
    // JNZ is taken when C0 or C3 is set (Duration < 0, == 0, or unordered): the IMMEDIATE path at
    // 062d48d0, which resolves Target's owner (IObjInspect slot +0x150 cobject) to a CEqObj
    // (ObjectClass & 0x103 == 0x103), fetches its IBehaviorManager via
    // CEqObj::get_behavior_interface() and switches on Extra. Only Duration > 0 falls through
    // into the node insert (Mode 1), which re-enters here with Duration 0 when it expires.
    // The port had this gate inverted until 2026-10-09 (CMN-BUG8).
    if (Duration > 0.0f)
    {
        NotificationNode* Node = InsertNotificationNodeBefore(NotificationQueueHead);
        Node->Mode = 1;
        // WatcherA is seeded from this manager's own WatchablePtr (+0xf0),
        // validated against the literal-8 sentinel the same way this
        // codebase's established idiom does elsewhere (common_facebehavior.cpp/
        // common_lootbehavior.cpp: `W != nullptr && (W - 8) != nullptr`) --
        // Common.dll.txt 214764-214781.
        Watchable const* SelfWatched = reinterpret_cast<Watchable const*>(WatchablePtr);
        bool SelfValid = (SelfWatched != nullptr)
            && (reinterpret_cast<char const*>(SelfWatched) - 8 != nullptr);
        Node->WatcherA.set(SelfValid ? SelfWatched : nullptr);
        Node->WatcherB.set((Target != nullptr)
            ? reinterpret_cast<Watchable const*>(reinterpret_cast<char const*>(Target) + 8)
            : nullptr);
        Node->Field1C = Extra;      // 062d484f local_14
        Node->Timer = Duration;     // 062d4853 local_10
        return;
    }

    if (Target == nullptr)
        return;
    CObject const* Owner = ObjInspectOf(Target)->cobject();
    if (Owner == nullptr)
        return;
    // ObjectClass & 0x103 == 0x103 -> CEqObj-derived, the same bitmask idiom
    // this file already establishes (see InspectEqObj() above, and the file-top
    // comment block on CObject::ObjectClass masks).
    CObject* OwnerObj = const_cast<CObject*>(Owner);
    if ((static_cast<unsigned>(OwnerObj->ObjectClass) & 0x103) != 0x103)
        return;
    IBehaviorManager* Mgr = static_cast<CEqObj*>(OwnerObj)->get_behavior_interface();
    if (Mgr == nullptr)
        return;

    switch (Extra)
    {
    case 0:
        // caseD_0 (Common.dll.txt 214864-214937): only when the target
        // manager's own CurrentBehaviorIndex == 0xa, call `[Mgr+0x19c]`'s vtable
        // slot +0x1c with no arguments, then fall through into case 1 exactly
        // like retail (no break before the shared FUN_062ecc50-equivalent call).
        // CORRECTED 2026-09-17: +0x19C is not a separate "Field19c" callback
        // object, it is BehaviorDispatchTable[10] with the index constant-folded
        // against the `== 0xa` guard on the line above (0x174 + 10*4 == 0x19C),
        // and slot +0x1c is Behavior::Slot7 -- so this is an ordinary typed
        // virtual call, not a raw dispatch into an unidentified class.
        if (Mgr->CurrentBehaviorIndex == 0xa && Mgr->BehaviorDispatchTable[0xa] != nullptr)
        {
            Mgr->BehaviorDispatchTable[0xa]->Slot7();
        }
        [[fallthrough]];
    case 1:
        // 062d493e-062d4949: FUN_062ecc50(Mgr->Sys + 0x1364, 0) -- the record's +0xc3 byte.
        Mgr->Sys->GunConvergenceStateBytes.SetFlagC3(0);
        break;
    case 2:
        // 062d4961-062d496c: FUN_062ecc50(Mgr->Sys + 0x1364, 1).
        Mgr->Sys->GunConvergenceStateBytes.SetFlagC3(1);
        break;
    case 3:
        {
            // caseD_3 (Common.dll.txt 214894-214907): re-enter
            // QueueNotification on the SAME manager, Target = Mgr's own
            // WatchablePtr (-8 adjusted, i.e. the raw object, or null if
            // WatchablePtr was null -- no literal-8 sentinel re-check on this
            // particular path, matching Common.dll.txt 214895-214901 exactly),
            // Extra=1, Duration=0.0f.
            Watchable const* SelfWatched = reinterpret_cast<Watchable const*>(Mgr->WatchablePtr);
            IObjRW* Raw = (SelfWatched != nullptr)
                ? reinterpret_cast<IObjRW*>(const_cast<char*>(
                      reinterpret_cast<char const*>(SelfWatched) - 8))
                : nullptr;
            Mgr->QueueNotification(Raw, 1, 0.0f);
        }
        break;
    default:
        break;
    }
}

void IBehaviorManager::UpdateBehaviorTick(float Dt)
{
    // FUN_062d2b10, Common.dll.txt 211855-212600 (665 instructions). This item
    // (decomp-tracker 1553) was `simplified` because its own entry guard
    // depended on the whole notification-queue subsystem above, which didn't
    // exist yet. It now does -- so the guard clause is ported in full here.
    //
    // Guard clause (Common.dll.txt 211864-211897):
    //   MOV AL,[this+0x170]; TEST AL,AL; JNZ <early return>
    //   MOV EAX,[this+0xf0]; TEST EAX,EAX; JZ <early return>
    //   ADD EAX,-8; TEST EAX,EAX; JZ <early return>       (WatchablePtr not null, not literal 8)
    //   EDI = this->Sys + 0x34 (&Sys->Timers); [EDI+0x1d] = 1
    //   CALL FUN_062d4450(this, Dt)                        -- UpdateNotificationQueue
    //   if (this->StateGraph == 0) { [EDI+0x1d] = 0; <early return> }
    //   -- else falls into the real tick body (NOT ported here, see below).
    if (TickSuppressedFlag != 0)
        return;
    if (WatchablePtr == nullptr)
        return;
    if (reinterpret_cast<char*>(WatchablePtr) - 8 == nullptr)
        return;
    // Sys is dereferenced unconditionally past this point, as in retail: the ctor always
    // builds it (FUN_062d2220 .text:062d235f-062d2399) and only the dtor clears it.

    // .text:062d2b55-062d2b63 `MOV EDI,[ESI+8]; ADD EDI,0x34; MOV byte ptr
    // [EDI+0x1d],1`. Sys->Timers+0x1d is no longer an unnamed byte in an opaque
    // blob: it is SystemManagerTimerTable::bKeepCache, the AI target list's
    // "return the cached list instead of rebuilding it" flag (2026-09-17 merge,
    // see that struct's MERGE NOTE). Holding it set for the duration of the
    // real tick body -- retail clears it again at .text:062d2b80 (the
    // no-state-graph bail-out) and at .text:062d33f0 (the tick's own tail) --
    // means the list is built at most once per tick and every
    // BuildAiTargetList call inside the tick reuses that one build.
    // .text:062d2b55-062d2b6c: the flag is set, then the scope that clears it on every later
    // exit (FUN_062d3410) is live from the EH state store at 062d2b6c on.
    Sys->Timers.bKeepCache = 1;
    FTimerTableKeepCacheScope const KeepCacheScope(&Sys->Timers);

    UpdateNotificationQueue(Dt);

    if (StateGraph == nullptr)
    {
        return;                                                     // 062d2b80
    }

    // Common.dll.txt 211898-211904 (LAB_062d2b89, straight-line, unconditional):
    //   MOV ECX,[this+0x8]; PUSH Dt; CALL FUN_062e66c0        -- Sys's own per-tick Update
    //   MOV ECX,[this+0x8]+0x1364; CALL FUN_062ec6b0          -- SyncFormationLeader
    UpdateSystemManager(Sys, Dt);
    Sys->GunConvergenceStateBytes.SyncFormationLeader();

    // Common.dll.txt 211905-211911: bool query through Sys->ExternalIface's own
    // vtable+0xbc slot (is_player), spilled to [ESP+0x13]. No null test, as in retail:
    // UpdateSystemManager above has just re-resolved ExternalIface from the (live)
    // watched object.
    bool const ExternalIfaceFlag = Sys->SelfTarget.Iface->is_player();

    // Common.dll.txt 211912-211990 (.text:062d2bbc-062d2ccd): everything up to
    // LAB_062d2cd7 hangs off `TEST byte ptr [ESI+0x1c8],0x1; JZ LAB_062d2cd7` -- the
    // attack-override timer AND the target branch after it. Until 2026-09-24 only the
    // timer was gated and the target branch was a TODO.
    if ((Flags1c8 & 1) != 0)
    {
        // Wrap-around AttackOverrideTimer/Period pair (this+0xe0/+0xe4). Timer always
        // accumulates Dt; only once it strictly exceeds Period (FCOMP+FNSTSW; TEST
        // AH,0x41; JNZ <skip> -- JNZ fires on less-than, equal or unordered) is it
        // re-wrapped via fmod and UpdateAttackTargetOverride(Sys+0x142C) (FUN_062ed780) fired.
        // .text:062d2bc9-062d2bdd: FLD Dt; FADD [+0xe0]; FST to a local and to +0xe0 (both
        // rounded), FCOMP [+0xe4] on the unrounded ST0; fmod runs on the rounded local.
        double const SumD = static_cast<double>(Dt) + static_cast<double>(AttackOverrideTimer);
        AttackOverrideTimer = static_cast<float>(SumD);
        if (SumD > static_cast<double>(AttackOverridePeriod))
        {
            AttackOverrideTimer = static_cast<float>(std::fmod(static_cast<double>(AttackOverrideTimer),
                                                               static_cast<double>(AttackOverridePeriod)));
            Sys->Targeting.UpdateAttackTargetOverride();
        }

        // LAB_062d2c0d-062d2c1f: FUN_062ed5a0 = TargetManager::GetTarget; null skips
        // straight to LAB_062d2cd7 (InSightsTargetId is NOT cleared on that path).
        IObjRW* const Target = Sys->Targeting.GetTarget();
        if (Target != nullptr)
        {
            // 062d2c25-062d2c3f: FUN_062ed5e0 = GetSubTarget, pushed with Dt and Target to
            // FUN_062eb670 = Sys->GunGroups.RunFireControlTick, which reads only the Target
            // argument (the other two are unread).
            (void)Sys->Targeting.GetSubTarget();
            Sys->GunGroups.RunFireControlTick(Target);

            // 062d2c44-062d2ccd: report the target to the content callback once it is in
            // sights -- ContentCbPtr set, our ship not the player (is_player again, +0xbc),
            // FUN_062e5190 = IsAlignedAndOutsideHeadingCone(Sys+0x14fc, Target, -0.7f
            // [PUSH 0xbf333333]) and FUN_062e4500 = IsWithinInterceptRangeOfIface(Sys+0x14fc,
            // Target, 600.0f [DAT_063a034c = 0x44160000]). Any failed test zeroes +0xE8.
            FAiTarget* const SelfRecord = &Sys->SelfTarget;
            bool bInSights = false;
            if (ContentCbPtr != nullptr
                && !Sys->SelfTarget.Iface->is_player()
                && IsAlignedAndOutsideHeadingCone(SelfRecord, Target, -0.7f)
                && IsWithinInterceptRangeOfIface(SelfRecord, Target, 600.0f))
            {
                bInSights = true;
            }

            if (bInSights)
            {
                // 062d2ca3-062d2ccb: get_id is called twice, once for the compare and once
                // for the store; ship_in_sights_event is ContentCallback slot +0x0C.
                if (InSightsTargetId != Target->get_id())
                {
                    unsigned int const TargetId = Target->get_id();
                    InSightsTargetId = TargetId;
                    ContentCbPtr->ship_in_sights_event(TargetId);
                }
            }
            else
            {
                InSightsTargetId = 0; // LAB_062d2ccd
            }
        }
    }

    // Common.dll.txt 211991-212002 (LAB_062d2cd7): if ExternalIface's own bool query was
    // false, run the gun-group scan + ready-hardpoint fire dispatch.
    if (!ExternalIfaceFlag)
    {
        // 062d2cdf-062d2cf7: FUN_062e9c50 = GunGroups.ScanAutoTurretTargets(Dt), then
        // FUN_062ea050 = GunGroups.FireAutoTurrets.
        Sys->GunGroups.ScanAutoTurretTargets(Dt);
        Sys->GunGroups.FireAutoTurrets();
    }

    // .text:062d2cfc-062d2d00: FUN_062dd760 = PathfindManager::Update(Dt) on +0x0C.
    Pathfind->Update(Dt);

    // Common.dll.txt 212006-212040 (.text:062d2d05-062d2d5b): WatchablePtr is re-read
    // and downcast (`W ? W - 8 : 0`) with no null test before its vtable+0xbc
    // (is_player) query -- the null arm reads address 0, as in retail. When true, a
    // nonzero LastCommandedDirection (x*x+y*y+z*z against DAT_06399408 = 0.0f; TEST
    // AH,0x44 / JNP skips only on "equal", so NaN still dispatches -- `!=` does the same)
    // is re-dispatched through update_current_behavior_direction (item 71378). The
    // `WatchableQueryObj != nullptr` guard that stood here was ours; it is removed.
    if (ObjRwFromWatchable(WatchablePtr)->is_player())
    {
        // 062d2d24-062d2d56: (x*x + y*y) + z*z stays on the x87 stack for the compare.
        double const MagSq = static_cast<double>(LastCommandedDirection.x) * LastCommandedDirection.x
                           + static_cast<double>(LastCommandedDirection.y) * LastCommandedDirection.y
                           + static_cast<double>(LastCommandedDirection.z) * LastCommandedDirection.z;
        if (MagSq != 0.0)
            update_current_behavior_direction(LastCommandedDirection);
    }

    // Common.dll.txt 212042-212072 (LAB_062d2d60 continuation): a
    // ContentCbPtr-gated flag reconciliation. NewContentFlag = (Sys+0x1434
    // == 2) && (CurrentBehaviorIndex == 2) && (DirectiveStorage's first
    // dword == 0 || DirectiveStorage's second dword == 1). Sys+0x1434 is
    // SystemManager::Targeting.AttackState (TargetManager+0x08, ctor
    // FUN_062ed570; common_iface.h). If NewContentFlag differs from the stored Field0xF8 byte,
    // stores it and calls ContentCbPtr->scan_state_change(flag) -- slot +0x18,
    // .text:062d2dc0, one pushed dword whose low byte is the flag. (An older
    // note here read this as a mismatch with request_flee_destination; that
    // was common_pub.h's slot order being wrong, corrected 2026-09-23.)
    if (ContentCbPtr != nullptr)
    {
        int const* DirDwords = reinterpret_cast<int const*>(DirectiveStorage);
        bool NewContentFlag = (Sys->Targeting.AttackState == 2) && (CurrentBehaviorIndex == 2)
                             && (DirDwords[0] == 0 || DirDwords[1] == 1);
        if (NewContentFlag != (Field0xF8 != 0))
        {
            Field0xF8 = NewContentFlag ? 1 : 0;
            ContentCbPtr->scan_state_change(NewContentFlag);
        }
    }

    // ========================================================================
    // Common.dll.txt 212074-212600 -- the behavior dispatch tail. PORTED FOR
    // REAL 2026-09-17 (claude:common-tick-lane, item 1553), replacing a
    // ~480-instruction TODO(FULLPORT-DEFERRED).
    //
    // The prior comment here called Behavior vtable slots 0x14/0x24/0x2c/0x68
    // "genuinely unidentified". They are not: class Behavior in
    // common_behavior.h declares all four (Slot5 +0x14, Slot9 +0x24,
    // Slot11 +0x2c, Slot26 +0x68), with bodies recovered from retail. Every
    // dispatch below is therefore a typed virtual call. The four helper
    // functions the same comment listed as "not-yet-checked" are all ported
    // too: FUN_062d3420 is SetCurrentBehavior (below, item 1555),
    // FUN_062e8830 / FUN_062e8870 / FUN_062ecaf0 are items 1811 / 1815 / 1869,
    // all ready_unverified.
    // ========================================================================

    // Common.dll.txt 212074-212096: BehaviorTimerA/B/C accumulator trio (see
    // common_iface.h for the full per-field writeup). Retail always stores
    // the raw Dt-accumulated sum to BehaviorTimerA first, then conditionally
    // overwrites it with fmodf(Sum, 3.0f) once Sum reaches or exceeds 3.0f
    // (DAT_063a0340 == 0x40400000 == 3.0f) -- reproduced in that same order.
    // The new BehaviorTimerC value is additionally kept in a local: retail
    // spills it to [ESP+0x14] here and feeds it to Behavior::Slot5 further
    // down (212137), which is the evidence that Slot5's one argument is a
    // float -- Behavior::Slot5 takes it directly (see common_behavior.h).
    //
    // bTimerAWrapped (retail's CL) and bReevaluate (retail's byte at
    // [ESP+0x13]) both start clear here and both get set by the wrap.
    bool bTimerAWrapped = false;
    bool bReevaluate = false;
    // .text:062d2dc3-062d2e0d: A's sum is FST'd to a local and to +0xd0 and stays on the x87
    // stack; B and C are summed and stored; then the unrounded A is compared with 3.0f
    // (TEST AH,0x1 / JNZ skips "less" and unordered), and fmod runs on the rounded local.
    float const NewTimerC = Dt + BehaviorTimerC;
    {
        double const NewTimerAExt = static_cast<double>(Dt) + BehaviorTimerA;
        float const NewTimerA = static_cast<float>(NewTimerAExt);
        BehaviorTimerA = NewTimerA;
        BehaviorTimerB = Dt + BehaviorTimerB;
        BehaviorTimerC = NewTimerC;
        if (NewTimerAExt >= 3.0)
        {
            BehaviorTimerA = static_cast<float>(std::fmod(static_cast<double>(NewTimerA), 3.0));
            bTimerAWrapped = true;
            bReevaluate = true;
        }
    }

    // Common.dll.txt 212097-212103: retail's byte at [ESP+0x34] (the incoming
    // Dt parameter slot, dead by this point and reused as a local -- watch the
    // stack-depth accounting, this is NOT still Dt). True only when the current
    // directive's first dword is set and its second is clear.
    int* const DirDwords = reinterpret_cast<int*>(DirectiveStorage);
    bool const bDirectiveArmed = (DirDwords[0] != 0) && (DirDwords[1] == 0);

    // Common.dll.txt 212104-212113: force-reset BehaviorTimerA to 0 whenever
    // CurrentBehaviorIndex==6, and treat that as a wrap for the two flags.
    // bCurrentIsIndex6 is retail's BL, live only until 212176 re-uses EBX.
    bool bCurrentIsIndex6 = false;
    if (CurrentBehaviorIndex == 6)
    {
        bTimerAWrapped = true;
        BehaviorTimerA = 0.0f;
        bReevaluate = true;
        bCurrentIsIndex6 = true;
    }

    // Common.dll.txt 212114-212118: the two entry guards for the
    // CurrentBehaviorIndex dispatch. Both fall through to LAB_062d33ec, which
    // just clears Sys->Timers.bKeepCache (KeepCacheScope) and returns.
    if (CurrentBehaviorIndex == -1)
    {
        return;
    }
    Behavior* CurrentBehavior = BehaviorDispatchTable[CurrentBehaviorIndex];
    if (CurrentBehavior == nullptr)
    {
        return;
    }

    // Common.dll.txt 212119-212167: only when the 3-second timer just wrapped
    // (or CurrentBehaviorIndex==6 forced it), poll the active behavior. A
    // result of 4 means "this behavior is done" and triggers an immediate
    // transition back to Field120 plus a DirectiveCallbacks[] fan-out.
    if (bTimerAWrapped)
    {
        int const Polled = CurrentBehavior->Slot5(NewTimerC);

        // 212141-212147: a directive that is armed but whose behavior did not
        // itself ask for re-evaluation suppresses the wrap-driven one.
        if (!bCurrentIsIndex6 && bDirectiveArmed)
            bReevaluate = false;

        if (Polled == 4)
        {
            // 212148-212158. CurrentBehaviorIndex is re-read here because
            // Slot5 may have changed it; that is also why retail bothers to
            // test it against -1 again on a path the entry guard above
            // already excluded.
            int const FinishedIndex = CurrentBehaviorIndex;
            Field118 = 2;
            Field11c = FinishedIndex;
            if (FinishedIndex != -1)
            {
                DirDwords[0] = 0;
                SetCurrentBehavior(static_cast<int>(Field120), true, nullptr);
                NotifyDirectiveCallbacks();
            }
            Field11c = -1;                                          // LAB_062d33e2
            return;
        }
    }

    // Common.dll.txt 212180-212199 (LAB_062d2f04): the active behavior's own
    // status word decides whether a re-evaluation is wanted. 2 and 3 force one
    // on, 0 forces one off, everything else leaves the wrap's verdict alone.
    // Retail keeps this in EBX for the next ~90 instructions.
    int const BehaviorStatus = CurrentBehavior->Slot9();
    if (BehaviorStatus == 2 || BehaviorStatus == 3)
        bReevaluate = true;
    if (BehaviorStatus == 0)
        bReevaluate = false;

    // Common.dll.txt 212200-212226: two AI-target-list probes on Sys->Timers,
    // each of which forces a re-evaluation unless the behavior they would
    // switch to is already current. Skipped entirely while a directive is
    // armed. FUN_062e8830 (item 1811) is a two-instruction `return
    // Timers[0x58] != 0` with no owning class recovered, so it is still called
    // by its retail name; its argument is an int in that port's signature.
    if (!bDirectiveArmed)
    {
        if (Sys->Timers.IsTimer3Active() && CurrentBehaviorIndex != 0xf)   // FUN_062e8830
            bReevaluate = true;
        if (SystemManagerTimerTableHasMissileReactionTarget(&Sys->Timers) && CurrentBehaviorIndex != 5)
            bReevaluate = true;
    }

    // Common.dll.txt 212227-212600: everything from here to the epilogue is
    // gated on the watched object being the player. Retail re-derives
    // WatchablePtr-8 rather than reusing the value from the top of the tick
    // (.text:062d2f67-062d2f7a: `W ? W - 8 : 0`, then is_player with no null test --
    // the null arm reads address 0; the port's `WatchedObj != nullptr` guard is gone).
    IObjRW* const WatchedObj = ObjRwFromWatchable(WatchablePtr);
    if (WatchedObj->is_player())
    {
        // 212236-212243: a behavior that reported status 2 or 3 is abandoned
        // for Field120 right away on the player's own ship.
        if (BehaviorStatus == 2 || BehaviorStatus == 3)
            SetCurrentBehavior(static_cast<int>(Field120), true, nullptr);

        // 212244-212248: optimistic defaults, overwritten below.
        Opaque168[0] = 1;
        DockingPortFlag = 0;
        DockingPortStatus = 0;
        bReevaluate = false;

        // 212249-212259: 0x13 (19) is the one index with no docking-probe
        // handling at all -- a real table index, inside the 21-entry table.
        if (CurrentBehaviorIndex != -1 && CurrentBehaviorIndex != 0x13)
        {
            // .text:062d2fd4-062d2fdf: the entry is loaded and called (+0x2c) with no null
            // test; the port's `Active != nullptr` guard is gone (this brace is now a scope).
            Behavior* const Active = BehaviorDispatchTable[CurrentBehaviorIndex];
            {
                // 212260-212276.
                Opaque168[0] = Active->Slot11() ? 1 : 0;
                if (BehaviorStatus == 0 || LockManeuversByte != 0)
                    DockingPortFlag = 1;

                // 212277-212379: resolve the docking target. Two completely
                // different sources depending on which behavior is running.
                IObjRW* Target = nullptr;
                bool bProximityGated = false;
                if (CurrentBehaviorIndex == 7)
                {
                    // 212285-212291: the dock behavior hands its target over
                    // directly.
                    Target = static_cast<IObjRW*>(Active->Slot26());
                }
                else if (CurrentBehaviorIndex == 0xa)
                {
                    // 212295-212303: FUN_062ecaf0 (item 1869) on the
                    // Sys+0x1364 sub-object returns the directive-override
                    // target, already "-8"-adjusted.
                    bProximityGated = true;
                    IObjRW* const Partner =
                        Sys->GunConvergenceStateBytes.GetCurrentLeader();   // FUN_062ecaf0
                    if (Partner != nullptr)
                    {
                        if (SinglePlayer())
                        {
                            // 212310-212345: single player reaches through the
                            // partner ship's OWN behavior manager and asks its
                            // dock behavior for the target. `[EAX+0x190]` there
                            // is BehaviorDispatchTable[7] with the index folded
                            // against the `== 7` test right above it -- see
                            // common_iface.h.
                            CObject const* const PartnerObj = ObjInspectOf(Partner)->cobject();
                            if (PartnerObj != nullptr
                                && (static_cast<unsigned>(PartnerObj->ObjectClass) & 0x503) == 0x503)
                            {
                                IBehaviorManager* const PartnerMgr =
                                    static_cast<CEqObj*>(const_cast<CObject*>(PartnerObj))->get_behavior_interface();
                                // .text:062d3074-062d3089: the manager is null-tested, its
                                // table[7] is not.
                                if (PartnerMgr != nullptr && PartnerMgr->CurrentBehaviorIndex == 7)
                                {
                                    Target = static_cast<IObjRW*>(
                                        PartnerMgr->BehaviorDispatchTable[7]->Slot26());
                                }
                            }
                        }
                        else
                        {
                            // 212346-212360: multiplayer just asks the partner
                            // for its own current target.
                            if (ObjInspectOf(Partner)->get_behavior_id() == 7)
                                ObjInspectOf(Partner)->get_target(Target);
                        }
                    }
                }

                // 212364-212450 (LAB_062d30af).
                if (Target != nullptr)
                {
                    unsigned TargetType = 0;
                    ObjInspectOf(Target)->get_type(TargetType);

                    // 212372-212421: the 0xa path additionally requires the
                    // target to be within 1000 units (DAT_063a03ec == 1.0e6f,
                    // a squared distance). `TEST AH,0x5; JP` skips on
                    // greater-or-unordered, so equality also skips: the test
                    // really is a strict `<`.
                    bool bInRange = true;
                    if (bProximityGated)
                    {
                        // .text:062d30d2-062d3122: WatchablePtr is re-read (`W ? W - 8 : 0`),
                        // both get_position (slot 0) calls are made in this order, and the
                        // differences and (dz*dz + dy*dy) + dx*dx stay on the x87 stack.
                        Vector const& SelfPos = ObjRwFromWatchable(WatchablePtr)->get_position();
                        Vector const& TgtPos = ObjInspectOf(Target)->get_position();
                        double const Dx = static_cast<double>(TgtPos.x) - SelfPos.x;
                        double const Dy = static_cast<double>(TgtPos.y) - SelfPos.y;
                        double const Dz = static_cast<double>(TgtPos.z) - SelfPos.z;
                        bInRange = ((Dz * Dz + Dy * Dy) + Dx * Dx) < 1000000.0f;
                    }

                    if (bInRange)
                    {
                        // 212443-212450: bit 7 of the target's type word picks
                        // status 2 over status 1.
                        DockingPortStatus = ((TargetType & 0x80) != 0) ? 2 : 1;
                    }
                }
            }
        }
    }

    // Common.dll.txt 212451-212462 (LAB_062d3138): a locked-maneuvers manager
    // never re-evaluates.
    if (LockManeuversByte != 0)
        bReevaluate = false;

    if (bReevaluate)
    {
        // --------------------------------------------------------------------
        // Common.dll.txt 212463-212617: the behavior re-evaluation pass. Scores
        // all 21 candidates, then picks one -- deterministically if any scored
        // exactly 1.0, otherwise by a weighted random draw over the prefix sums.
        // This loop is the primary evidence that BehaviorDispatchTable holds 21
        // entries (`CMP EDI,0x15; JL`) and that the state graph's transition
        // matrix at StateGraph+0x48 is 21x21 (`IMUL EDX,EDX,0x15`).
        // --------------------------------------------------------------------
        int ScoreCount = 0;
        bool bFoundUnityWeight = false;
        for (int Index = 0; Index < 21; ++Index)
        {
            Behavior* const Candidate = BehaviorDispatchTable[Index];

            // Retail re-reads BOTH StateGraph (`MOV EAX,[ESI+4]`) and
            // CurrentBehaviorIndex (`MOV EDX,[ESI+0xc0]`) on every iteration,
            // at the top of the loop body -- reproduced here rather than
            // hoisted, because Behavior::Slot5 below can change either.
            float const* const TransitionWeights =
                reinterpret_cast<float const*>(reinterpret_cast<char const*>(StateGraph) + 0x48);
            float Weight = TransitionWeights[CurrentBehaviorIndex * 21 + Index];

            // 212483-212489: an armed directive naming this very behavior gets
            // a token weight of 0.001f (DAT at 0x3a83126f) instead of whatever
            // the graph says -- enough to stay in the running, not enough to
            // beat a real transition.
            if (DirDwords[0] == 1 && DirDwords[2] == Index)
                Weight = 0.001f;

            // 212490-212495: `FCOMP` against a double 0.0 with the
            // `TEST AH,0x44; JNP` equality idiom -- a zero-weight transition is
            // simply not allowed and the candidate is never even polled.
            if (Weight == 0.0)
                continue;

            // Retail polls without a null check (the port's former guard is gone).

            // 212496-212519: the candidate's own verdict overrides the graph
            // weight. 0 means "definitely me" (1.0f, DAT_0639a1d0); 2..4 mean
            // "definitely not me" (0.0f, DAT_06399408, which the `> 0` test
            // immediately below then rejects); anything else keeps the weight.
            int const Verdict = Candidate->Slot5(BehaviorTimerC);
            float Value;
            if (Verdict == 0)
                Value = 1.0f;
            else if (Verdict >= 2 && Verdict <= 4)
                Value = 0.0f;
            else
                Value = Weight;

            // 212520-212524: `FCOM` double 0.0 with `TEST AH,0x41; JNZ` --
            // keeps strictly-positive values only.
            if (!(Value > 0.0))
                continue;

            BehaviorScores[ScoreCount].Weight = Value;
            BehaviorScores[ScoreCount].BehaviorIndex = Index;
            if (Value == 1.0)
                bFoundUnityWeight = true;
            ++ScoreCount;
        }

        if (ScoreCount == 0)
        {
            // 212589-212606: nothing is eligible. Retire any armed directive
            // and fall back to Field120 unless it is already current.
            if (DirDwords[0] == 1)
            {
                Field118 = 2;
                Field11c = DirDwords[2];
                DirDwords[0] = 0;
            }
            if (CurrentBehaviorIndex != static_cast<int>(Field120))
                SetCurrentBehavior(static_cast<int>(Field120), false, nullptr);
        }
        else
        {
            int Chosen = -1;
            if (bFoundUnityWeight)
            {
                // 212654-212668 (LAB_062d3300): when anything scored exactly
                // 1.0 the draw is skipped entirely and the LAST such entry
                // wins (retail keeps overwriting EDX without breaking).
                for (int Index = 0; Index < ScoreCount; ++Index)
                {
                    if (BehaviorScores[Index].Weight == 1.0)
                        Chosen = BehaviorScores[Index].BehaviorIndex;
                }
                SetCurrentBehavior(Chosen, false, nullptr);
            }
            else
            {
                // 212612-212653: turn the weights into prefix sums in place,
                // then draw. Retail's constants, all byte-decoded from .rdata:
                // DAT_063997d0 = 0x38000000 = 1/32768 (rand()'s own range),
                // DAT_0639b984 = 0x3727C5AC = 1.0e-5f,
                // DAT_0639c3f0 = the double 1.0e-5.
                // .text:062d3287-062d32a7: the running total stays on the x87 stack
                // (unrounded) while each prefix sum is stored as a float; the total is
                // rounded to float once, at 062d32a7.
                double Total = 0.0;
                for (int Index = 0; Index < ScoreCount; ++Index)
                {
                    float const Own = BehaviorScores[Index].Weight;
                    BehaviorScores[Index].Weight = static_cast<float>(Total + static_cast<double>(Own));
                    Total = static_cast<double>(Own) + Total;
                }
                float const TotalF = static_cast<float>(Total);

                // .text:062d32af-062d32d7: FILD rand(); FMUL 1/32768f; (Total - 1e-5f);
                // FMULP; FADD double 1e-5; FSTP float -- one rounding, at the end.
                double const Unit = static_cast<double>(rand()) * static_cast<double>(1.0f / 32768.0f);
                float const Pick = static_cast<float>(
                    (static_cast<double>(TotalF) - static_cast<double>(1.0e-5f)) * Unit + 1.0e-5);

                // .text:062d32e0-062d32f2: walk up to the first prefix sum strictly greater
                // than the draw (`TEST AH,0x41; JZ` out), bounded by the TABLE size 0x15 like
                // retail, not by ScoreCount -- so stale entries past ScoreCount are read, and
                // falling off the end reads the dword at BehaviorScores+21*8+4 (+0xBC). The
                // old `ScoreCount - 1` bound is gone.
                int Picked = 0;
                while (Picked < 21 && !(BehaviorScores[Picked].Weight > Pick))
                    ++Picked;
                Chosen = *reinterpret_cast<int const*>(
                    reinterpret_cast<char const*>(BehaviorScores) + Picked * 8 + 4);

                // .text:062d32f4-062d32ff: a chosen index of -1 hits INT3 (an assertion);
                // execution past it jumps to the SetCurrentBehavior call (the `EB 1F` at
                // 062d32fe lands on 062d331f) with the -1 still in EDX. The port used to
                // skip the transition instead.
                if (Chosen == -1)
                    __debugbreak();
                SetCurrentBehavior(Chosen, false, nullptr);
            }
        }
    }

    // Common.dll.txt 212700-212727 (LAB_062d332b): hand the accumulated
    // BehaviorTimerB over to the active behavior and reset it, either every
    // 0.2s (DAT_063a0344 == 0x3E4CCCCD == 0.2f), or immediately when
    // BehaviorDirty is set, or when this tick re-evaluated. Retail reads the
    // table entry (.text:062d333d) with no range or null test and always calls its
    // +0x20 slot at 062d337e; the port's former range/null guard is gone (this brace
    // is now a scope).
    Behavior* const TickBehavior = BehaviorDispatchTable[CurrentBehaviorIndex];
    {
        if (BehaviorTimerB >= 0.2f || BehaviorDirty != 0 || bReevaluate)
        {
            // Behavior+0x1c, reached through the accessor rather than by raw
            // offset since 2026-09-17. It is deliberately NOT a data member:
            // Behavior's ctor (FUN_062d2020) writes the whole +0x04..+0x29
            // block, so that block is the base class's, but nineteen classes
            // across common_*behavior.h currently re-declare it on themselves,
            // each pinned by an offsetof assert that only passes because
            // sizeof(Behavior) == 4. Hoisting the block would shift all
            // nineteen at once, so it has to happen in one commit across every
            // one of those files -- see the write-up on common_behavior.h.
            // The offset itself is doubly corroborated: the ctor's dword zero,
            // and .text:062d335d `FLD [ESI+0xd4]; FSTP float [EDI+0x1c];
            // CALL [EAX+0x1c]` through a bare Behavior*.
            TickBehavior->SetTickDelta(BehaviorTimerB);
            TickBehavior->Slot7();
            BehaviorTimerB = 0.0f;
            BehaviorDirty = 0;
        }

        // 212728-212731: an unconditional end-of-tick poke at the active
        // behavior (retail's base body for this slot is a bare RET).
        TickBehavior->Slot8();
    }

    // Common.dll.txt 212732-212745: a pending directive result whose behavior
    // is either the one still running or index 8 completes now -- transition
    // back to Field120 and notify. Anything else just clears the pending slot.
    if (Field11c != -1 && (Field11c == CurrentBehaviorIndex || Field11c == 8))
    {
        DirDwords[0] = 0;
        SetCurrentBehavior(static_cast<int>(Field120), true, nullptr);
        NotifyDirectiveCallbacks();
    }
    Field11c = -1;

    // LAB_062d33ec: KeepCacheScope's destructor releases the AI target list's keep-cache
    // flag on the way out.
}

void IBehaviorManager::NotifyDirectiveCallbacks()
{
    // The DirectiveCallbacks[5] fan-out UpdateBehaviorTick runs at both of its
    // transition-completed sites (Common.dll.txt 212168-212180 and
    // 212570-212586 -- two byte-identical unrolled-by-5 loops, factored into
    // one helper here). Retail's dispatch is `PUSH [this+0x118]; PUSH
    // [this+0x11C]; CALL [*Cb]`, i.e. vtable slot 0 with Field11c as the first
    // argument and Field118 as the second.
    //
    // RESOLVED 2026-09-17: common_pub.h has been corrected, so the two arguments
    // this call site used to drop are passed for real. DirectiveCallback now has
    // OnDirectiveComplete in slot 0 -- its only virtual -- and no virtual
    // destructor, matching retail. The proof is this very loop
    // (.text:062d2ee1-062d2efd): `MOV ECX,[EDI]` / `MOV EAX,[ESI+0x118]` /
    // `MOV EDX,[ECX]` / `PUSH EAX` / `MOV EAX,[ESI+0x11c]` / `PUSH EAX` /
    // `CALL dword ptr [EDX]` -- slot 0, two arguments -- with `MOV EBX,0x5` /
    // `DEC EBX` / `ADD EDI,4` bounding it at five entries. Arguments are pushed
    // right-to-left, so Field11c is the first parameter and Field118 the second.
    for (int Index = 0; Index < 5; ++Index)
    {
        pub::AI::DirectiveCallback* const Cb = DirectiveCallbacks[Index];
        if (Cb != nullptr)
            Cb->OnDirectiveComplete(Field11c, Field118);
    }
}

// FUN_062d29a0 (Common.dll.txt 211669-211673), all five instructions:
// `MOV EAX,[ESP+8]; MOV EDX,[ESP+4]; MOV [ECX+0x118],EAX; MOV [ECX+0x11c],EDX; RET 8`.
void IBehaviorManager::SetDirectiveResult(int Word11c, int Word118)
{
    Field118 = Word118;   // 062d29a8
    Field11c = Word11c;   // 062d29ae
}

// FUN_062d35c0 (Common.dll.txt 062d35c0-062d368c), all 60 instructions.
void IBehaviorManager::RelevelShipUpDirection(bool bApply)
{
    // 062d35c6-062d35ce.
    if (LevelCameraFlag == 0)
        return;

    // 062d35d4-062d35fd: the watched ship (`raw ? raw-8 : 0`, called unguarded), its
    // get_transform() (+0x0c) Rotation copied to a 9-dword local -- before bApply is tested.
    Matrix const Rotation = ObjInspectOf(ObjRwFromWatchable(WatchablePtr))->get_transform().Rotation;
    if (!bApply)
        return;

    // 062d3607-062d3630: |(E22 + E02) * 0.0f + E12| on the x87 stack (the forward axis's y,
    // compiled as a dot product with (0, 1, 0)), against 0x3f34fdf4: `FLD K; FCOMPP;
    // TEST AH,0x41; JNZ` returns unless K is strictly greater.
    double const ForwardY =
        std::fabs((static_cast<double>(Rotation.E[2][2]) + Rotation.E[0][2]) * 0.0f + Rotation.E[1][2]);
    unsigned int const LevelThresholdBits = 0x3f34fdf4u;      // DAT_063a033c, 0.707f
    float LevelThreshold;
    std::memcpy(&LevelThreshold, &LevelThresholdBits, sizeof(LevelThreshold));
    if (!(LevelThreshold > ForwardY))
        return;

    // 062d3632-062d3681: LevelCameraFlag is cleared around the gate query (FUN_062e92e0 on
    // Sys+0x18 reads it back through get_camera_level_status).
    LevelCameraFlag = 0;
    if (Sys->Thrust.CheckSteeringGate())
    {
        ShipUpDirection = Vector{ 0.0f, 1.0f, 0.0f };
        ShipUpDirFlag = 0;
    }
    LevelCameraFlag = 1;
}

// FUN_062d2ad0 (Common.dll.txt 211824-211838), all 14 instructions.
bool IBehaviorManager::RequestFleeDestination(uint ObjectId,
                                              pub::AI::ContentCallback::FleeReason Reason,
                                              pub::AI::ContentCallback::FleeDestinationType& OutType,
                                              Vector& OutPosition, uint& OutParam, float& OutRadius)
{
    // 062d2ad2-062d2ade: `MOV ECX,[EAX+0x110]; TEST; JZ; MOV EAX,[ECX]; JMP [EAX+0x10]` -- the
    // callback's slot 4 receives this function's six stack arguments unchanged.
    if (ContentCbPtr != nullptr)
        return ContentCbPtr->request_flee_destination(ObjectId, Reason, OutType, OutPosition,
                                                      OutParam, OutRadius);
    // 062d2ae1-062d2afb: [this+8]+0x14fc+0x84 == Sys->ExternalIface, `CALL [EDX+0xbc]`
    // (IObjInspect::is_player), result discarded, `XOR AL,AL`. No null tests in retail.
    (void)Sys->SelfTarget.Iface->is_player();
    return false;
}

// FUN_062d29c0 (Common.dll.txt 211684-211719), all 26 instructions.
bool IBehaviorManager::NotifyFollowEvent(uint ObjectId1, uint ObjectId2,
                                         pub::AI::ContentCallback::FollowStatusType Status,
                                         float Distance)
{
    // 062d29c0-062d29d4: Sys+0x1434 (Targeting.AttackState) nonzero and behaviour 0x11.
    if (Sys->Targeting.AttackState == 0 || CurrentBehaviorIndex != 0x11)
        return false;
    // 062d29d6-062d29fa: the callback's slot 0 with the four arguments re-pushed in order.
    if (ContentCbPtr != nullptr)
    {
        ContentCbPtr->follow_event(ObjectId1, ObjectId2, Status, Distance);
        return true;
    }
    // 062d29fd-062d2a0d: Sys->ExternalIface->is_player() (+0xbc) for effect, then false.
    (void)Sys->SelfTarget.Iface->is_player();
    return false;
}

// FUN_062d2a10 (Common.dll.txt 211721-211760), all 29 instructions.
bool IBehaviorManager::NotifyTetherEvent(uint ObjectId1, uint ObjectId2,
                                         pub::AI::ContentCallback::TetherStatusType Status,
                                         Vector const& Position)
{
    // 062d2a10-062d2a2b: Targeting.AttackState nonzero and behaviour 2 or 7.
    if (Sys->Targeting.AttackState == 0)
        return false;
    if (CurrentBehaviorIndex != 2 && CurrentBehaviorIndex != 7)
        return false;
    // 062d2a2d-062d2a52: the callback's slot 1 (+0x04) with the four arguments.
    if (ContentCbPtr != nullptr)
    {
        ContentCbPtr->tether_object_event(ObjectId1, ObjectId2, Status, Position);
        return true;
    }
    // 062d2a55-062d2a65: EDX still holds Sys -- its ExternalIface->is_player() for effect.
    (void)Sys->SelfTarget.Iface->is_player();
    return false;
}

bool IBehaviorManager::SetCurrentBehavior(int NewIndex, bool bForce, void* DirectiveArg)
{
    // FUN_062d3420 (decomp-tracker item 1555), Common.dll.txt 212628-212771, 128
    // instructions, `RET 0xc`. Ten callers (062d2ed1/062d2f9e/062d3326/062d33b4 inside
    // UpdateBehaviorTick, 062d3dc6/062d3dfe/062d3e3c/062d3e54 in set_current_directive,
    // 062d4159/062d42c6 in LAB_062d3f90).
    //
    // 062d3427-062d3440: Sys->ExternalIface (0x14fc + 0x84) -> is_player() (+0xbc). No null
    // test on ExternalIface.
    int EffectiveIndex = NewIndex;   // EBP

    if (Sys->SelfTarget.Iface->is_player())
    {
        // 062d344e-062d3457: `MOV EAX,2; SHL EAX,CL` with CL = NewIndex, tested against
        // ManeuverMask -- bit (NewIndex + 1) -- then LockManeuversByte; either refuses.
        if ((ManeuverMask & (2u << (static_cast<unsigned>(NewIndex) & 31))) != 0)
            return false;
        if (LockManeuversByte != 0)
            return false;

        // 062d3471-062d3503: behavior 6 as the player becomes Field120 instead, and every
        // steering input on the ThrustManager (Sys+0x18) is released on the way.
        if (NewIndex == 6)
        {
            EffectiveIndex = static_cast<int>(Field120);
            ThrustManager* const Thrust = &Sys->Thrust;
            Thrust->SetThrottle(0.0f, 2);                           // FUN_062e89a0(0, 2)
            Thrust->SetThrottle(0.0f, 0);                           // FUN_062e89a0(0, 0)
            // Retail re-zeroes the same 12-byte stack scratch before each of the next two
            // calls (062d34a9-062d34b9 and 062d34d1-062d34e1).
            Vector Zero;
            Zero.x = 0.0f;
            Zero.y = 0.0f;
            Zero.z = 0.0f;
            Thrust->SetAxisThrottle(Zero);                           // FUN_062e8c20
            Zero.x = 0.0f;
            Zero.y = 0.0f;
            Zero.z = 0.0f;
            Thrust->SetNudgeDirection(Zero);                         // FUN_062e8ce0
            Thrust->SetStrafeDir(StrafeNone);                        // FUN_062e9030(0)
            Thrust->SetThrustersActive(false);                       // FUN_062e9120(0)
        }
    }

    // 062d3508-062d3522 (LAB_062d3508): already there and nobody forced it -> true.
    if (CurrentBehaviorIndex == EffectiveIndex && !bForce)
        return true;

    // 062d3530-062d354f (LAB_062d3530): an index of -1 leaves its pointer at 0; no other
    // bound is checked.
    Behavior* const Outgoing = (CurrentBehaviorIndex != -1)
                                   ? BehaviorDispatchTable[CurrentBehaviorIndex]
                                   : nullptr;
    Behavior* const Incoming = (EffectiveIndex != -1)
                                   ? BehaviorDispatchTable[EffectiveIndex]
                                   : nullptr;

    bool bResult = true;             // 062d3538 `MOV BL,1`

    // 062d354f-062d3555: Behavior slot 4 (+0x10) on the outgoing entry.
    if (Outgoing != nullptr)
        Outgoing->Slot4();

    // 062d3558-062d3572: with a directive, the incoming entry's slot 10 (+0x28) answers
    // (`MOV BL,AL`); then its slot 3 (+0x0c) runs regardless.
    if (Incoming != nullptr)
    {
        if (DirectiveArg != nullptr)
            bResult = Incoming->Slot10(DirectiveArg);
        Incoming->Slot3();
    }

    // 062d3575: commit.
    CurrentBehaviorIndex = EffectiveIndex;

    // 062d357b-062d35a9: WatchablePtr - 8 (or 0 -- then the vtable read faults; no guard),
    // cobject() (+0x150); a non-null CObject whose class has all of 0x103 (a CEqObj) gets
    // the new index in CEqObj+0x1a0.
    IObjRW* const WatchedObj = ObjRwFromWatchable(WatchablePtr);
    CObject const* const Owned = WatchedObj->cobject();
    if (Owned != nullptr && (static_cast<unsigned>(Owned->ObjectClass) & 0x103) == 0x103)
    {
        static_cast<CEqObj*>(const_cast<CObject*>(Owned))->BehaviorIndex = EffectiveIndex;
    }

    return bResult;                  // 062d35af `MOV AL,BL`
}

// The retail helpers on SystemManager+0x1364 that Slot00 below calls are
// FGunConvergenceStateBlock members (common_gunconvergence.cpp): FUN_062ec590 SetValueB4,
// FUN_062ec5d0 SetDirectiveOrder, FUN_062ec650 SetValueB0, FUN_062ec6a0 SuppressCurrentLeader.

// LAB_062d3f90 -- vftable 063a037c slot 0, the only virtual of retail IBehaviorManager.
// Common.dll.txt 213832-214170 (.text:062d3f90-062d42eb), thiscall, `RET 4`; jump table at
// .text:062d42f0 (8 entries). Ported 1:1 2026-09-24 (claude-code:w5-server-06cf).
//   062d3f95-062d3fa1  CurrentBehaviorIndex == -1 -> return
//   062d3fa7-062d3fb6  Type = [Event]; (unsigned) Type > 7 -> return; switch
// In every case `Rec` is Sys+0x1364 and `Data` is [Event+4].
void IBehaviorManager::Slot00(FBehaviorManagerEvent const* Event)
{
    if (CurrentBehaviorIndex == -1)                                         // .text:062d3fa1
        return;
    if (static_cast<unsigned int>(Event->Type) > 7u)                        // .text:062d3fb0
        return;

    unsigned char* const Data = static_cast<unsigned char*>(Event->Data);
    FGunConvergenceStateBlock* const Rec = &Sys->GunConvergenceStateBytes;  // Sys + 0x1364
    switch (Event->Type)
    {
    case 0:
        // 062d4271-062d4284: FUN_062e86d0(Sys+0x34, Data).
        AccumulateSystemManagerTimerLevel(&Sys->Timers, Data);
        return;

    case 1:
        // 062d4259-062d426e: `ECX = Sys+0x34; PUSH [Data]; CALL 062ed730`. The callee is the
        // ICF-folded `RET 4` Ghidra labels MetaList<GoodDesc>::operator= -- no body, so the
        // case only reads [Data] (kept: it faults on a bad pointer in retail too).
        {
            unsigned int const volatile Unused = *reinterpret_cast<unsigned int const*>(Data);
            (void)Unused;
        }
        return;

    case 2:
    {
        // 062d4165-062d41a2. DirectiveStorage dword 0 (+0xc4) == 2 and [Data+8] == dword 2
        // (+0xcc): +0xc4 = 0, +0xcc = -1 (EDX, still the -1 from 062d3f9b), Field118 = 0,
        // Field11c = the old +0xcc.
        int* const Dir = reinterpret_cast<int*>(DirectiveStorage);
        if (Dir[0] != 2)                                                    // .text:062d416c
            return;
        int const OldTarget = Dir[2];
        if (*reinterpret_cast<int const*>(Data + 8) != OldTarget)           // .text:062d417e
            return;
        Dir[0] = 0;                                                         // .text:062d4187
        Dir[2] = -1;                                                        // .text:062d418d
        Field118 = 0;                                                       // .text:062d4193
        Field11c = OldTarget;                                               // .text:062d4199
        return;
    }

    case 3:
    {
        // 062d3fbd-062d4162. EBX = Data from here on.
        // [Data+4] / [Data+8] are re-read from memory at every use, as retail does.
        unsigned int& LeftId = *reinterpret_cast<unsigned int*>(Data + 8);
        unsigned int const& LeftFlags = *reinterpret_cast<unsigned int const*>(Data + 4);

        Sys->GunGroups.ClearGunTargets();                                   // .text:062d3fc9

        // 062d3fce-062d3ffa: the watched object's cobject() (+0x150), no null test on the
        // watcher itself; a CShip (& 0x503) gets leaving_system_update(&[Data+8]).
        CObject const* const WatchedCobj =
            ObjInspectOf(ObjRwFromWatchable(WatchablePtr))->cobject();     // .text:062d3fe1
        if (WatchedCobj != nullptr &&
            (static_cast<unsigned int>(WatchedCobj->ObjectClass) & 0x503u) == 0x503u)
            static_cast<CShip*>(const_cast<CObject*>(WatchedCobj))->leaving_system_update(LeftId); // .text:062d4002

        // 062d4007-062d402c: ExternalIface (Sys+0x14fc+0x84) get_id (+0x20) == [Data+8] ->
        // FUN_062ec650(Rec, 0).
        if (LeftId == Sys->SelfTarget.Iface->get_id())            // .text:062d401c
            Rec->SetValueB0(0);                                             // .text:062d402c

        ClearGunConvergenceStateWatcherIfTarget(Sys, static_cast<int>(LeftId)); // .text:062d4039

        // 062d403e-062d4063: all 21 dispatch-table entries; non-null ones get slot 21
        // (+0x54) with the dword [Data+8].
        for (int Index = 0; Index < 21; ++Index)
        {
            Behavior* const Entry = BehaviorDispatchTable[Index];
            if (Entry != nullptr)
                Entry->Slot21(reinterpret_cast<void*>(static_cast<uintptr_t>(LeftId))); // .text:062d405c
        }

        if ((LeftFlags & 0xc40u) != 0)                                      // .text:062d406c
        {
            // 062d4072-062d4092: Leader = 0; WatchablePtr-8 (or 0, not tested)
            // ->get_formation_leader(Leader) (+0xb0); its return value is ignored.
            IObjRW* Leader = nullptr;
            ObjInspectOf(ObjRwFromWatchable(WatchablePtr))->get_formation_leader(Leader);
            if (Leader != nullptr &&                                        // .text:062d4098
                ObjInspectOf(Leader)->get_id() == LeftId)                   // .text:062d40a2
            {
                // 062d40a4-062d40d7: the leader is not the watched object itself (ids
                // compared, the leader's read first) -> FUN_062ec650(Rec, [Data]).
                IObjRW* const Self = ObjRwFromWatchable(WatchablePtr);
                unsigned int const LeaderId = ObjInspectOf(Leader)->get_id(); // .text:062d40bb
                if (LeaderId != ObjInspectOf(Self)->get_id())               // .text:062d40c9
                    Rec->SetValueB0(*reinterpret_cast<unsigned int const*>(Data)); // .text:062d40d7
            }
            // 062d40dc-062d40f0: the current behavior's slot 22 (+0x58) gets the event
            // itself; the entry is not null-tested.
            BehaviorDispatchTable[CurrentBehaviorIndex]->Slot22(
                const_cast<FBehaviorManagerEvent*>(Event));                 // .text:062d40f0
        }

        // 062d40f3-062d4159: flags & 0x840, ExternalIface is_player() (+0xbc), the current
        // behavior is 10 and ExternalIface get_id() == [Data+8] -> SetCurrentBehavior(
        // Field120, true, 0).
        if ((LeftFlags & 0x840u) == 0)                                      // .text:062d40fb
            return;
        if (!Sys->SelfTarget.Iface->is_player())                 // .text:062d411b
            return;
        if (CurrentBehaviorIndex != 0xa)                                    // .text:062d4128
            return;
        if (LeftId != Sys->SelfTarget.Iface->get_id())            // .text:062d4146
            return;
        SetCurrentBehavior(static_cast<int>(Field120), true, nullptr);      // .text:062d4159
        return;
    }

    case 4:
    {
        // 062d41a5-062d4202. FUN_062ecaf0(Rec) returns the record's watched object (already
        // -8 adjusted) or 0; its get_id() == [Data+0xc] -> FUN_062ec5d0(Rec, byte [Data],
        // [Data+0x10], [Data+0x14], byte [Data+2], [Data+4]). Then, unconditionally, the
        // current behavior's slot 22 with the event (entry not null-tested).
        IObjRW* const Watched = Sys->GunConvergenceStateBytes.GetCurrentLeader();   // .text:062d41b1 FUN_062ecaf0
        if (Watched != nullptr)
        {
            if (Watched->get_id() ==
                *reinterpret_cast<unsigned int const*>(Data + 0xc))         // .text:062d41c4
                Rec->SetDirectiveOrder(Data[0],
                                       *reinterpret_cast<unsigned int const*>(Data + 0x10),
                                       *reinterpret_cast<unsigned int const*>(Data + 0x14),
                                       Data[2],
                                       *reinterpret_cast<unsigned int const*>(Data + 4)); // .text:062d41e6
        }
        BehaviorDispatchTable[CurrentBehaviorIndex]->Slot22(
            const_cast<FBehaviorManagerEvent*>(Event));                     // .text:062d41fb
        return;
    }

    case 5:
    {
        // 062d4205-062d4256. Same watched object; get_id() == [Data+8] and
        // [Data+4] & 0x120 -> FUN_062ec590(Rec, [Data], byte [Data+0xc]).
        IObjRW* const Watched = Sys->GunConvergenceStateBytes.GetCurrentLeader();   // .text:062d4211 FUN_062ecaf0
        if (Watched == nullptr)
            return;
        if (Watched->get_id() !=
            *reinterpret_cast<unsigned int const*>(Data + 8))               // .text:062d4228
            return;
        if ((*reinterpret_cast<unsigned int const*>(Data + 4) & 0x120u) == 0) // .text:062d4235
            return;
        Rec->SetValueB4(*reinterpret_cast<unsigned int const*>(Data),
                        Data[0xc]);                                         // .text:062d424d
        return;
    }

    case 6:
        // 062d4287-062d429a: FUN_062e8720(Sys+0x34, Data).
        AppendSystemManagerTimerListEntry(&Sys->Timers, Data);
        return;

    case 7:
        // 062d429d-062d42eb. ExternalIface get_id() == [Data] -> SetCurrentBehavior(
        // Field120, false, 0), then TickSuppressedFlag = 1; otherwise FUN_062ec6a0(Rec)
        // (sets Rec+0xc5).
        if (*reinterpret_cast<unsigned int const*>(Data) ==
            Sys->SelfTarget.Iface->get_id())                      // .text:062d42b5
        {
            SetCurrentBehavior(static_cast<int>(Field120), false, nullptr); // .text:062d42c6
            TickSuppressedFlag = 1;                                         // .text:062d42cc
            return;
        }
        Sys->GunConvergenceStateBytes.SuppressCurrentLeader();               // .text:062d42e2 FUN_062ec6a0
        return;
    }
}
