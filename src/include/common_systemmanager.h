// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_goods.h"   // BaseWatcher
#include "common_pub.h"     // pub::AI::Personality
// TargetManager (SystemManager+0x142C, by value). common_iface.h includes this header right
// after TargetManager's definition, so whichever of the two a translation unit names first,
// TargetManager is complete before SystemManager is read.
#include "common_iface.h"

#include <cstddef>          // offsetof

class SystemManager;    // forward decl: SystemManagerTimerTable::Owner back-pointer
class IBehaviorManager; // forward decl: SystemManager::Owner back-pointer (pointer-only use here)
struct IObjDB;          // common_cobjdb.h -- SystemManager::ObjectDb (pointer-only use here)
struct IObjInspect;     // common_iface.h -- FAiTarget's two interface pointers

// ============================================================================
// FAiTarget -- the 0x88-byte AI-target cache record. One instance is embedded in
// SystemManager at +0x14FC (SelfTarget, the own ship); the timer table's
// std::vector<FAiTarget> (SystemManagerTimerTable::Targets, 0x88 stride, built by
// BuildAiTargetList FUN_062e7580) holds the others. Every cached slice is filled
// lazily from Iface on first use and guarded by its own byte (+0x7C..+0x7F); the
// owner clears all four guards once per tick (InitSystemManager / UpdateSystemManager,
// FAiTarget::Init for vector elements).
// ============================================================================
struct FAiTarget
{
    // +0x00/+0x04, guarded by bAttitudeValid: AttitudeRefIface->
    // get_attitude_towards_symmetrical(CachedAttitude, Iface, CachedReverseAttitude)
    // (GetCachedRange, FUN_062e6a80). Both 0.0f when either interface is null or the
    // call fails. Consumers gate on `<= -0.6f` / `<= -0.2f` (hostile enough).
    float CachedAttitude;
    float CachedReverseAttitude;

    // +0x08, guarded by bFlagsValid: Iface->get_type(CachedFlags) (GetFlags).
    unsigned CachedFlags;

    // +0x0C/+0x18, guarded by bPosValid: one Iface->get_physical_radius(CachedRadius,
    // CachedPos) call (RefreshPos; the radius out-param is pushed first).
    Vector CachedPos;
    float CachedRadius;

    // +0x1C..+0x7B, guarded by bCornersValid: world-space bounding-box corners from
    // TransformBoundingBoxCorners (FUN_062e4910) over Iface's transform and extents.
    Vector CachedCorners[8];

    unsigned char bAttitudeValid;   // +0x7C
    unsigned char bFlagsValid;      // +0x7D
    unsigned char bPosValid;        // +0x7E
    unsigned char bCornersValid;    // +0x7F

    // +0x80: the reference object the attitude pair is measured against (the list
    // owner's own ship). +0x84: this record's object; every other slice comes from it.
    IObjInspect* AttitudeRefIface;
    IObjInspect* Iface;

    // FUN_062d09b0, item 1470, thiscall RET 8: writes both interface pointers (attitude
    // reference first) and clears the four guards.
    void Init(IObjInspect const* NewAttitudeRef, IObjInspect const* NewIface);

    // FUN_062ca1b0, item 1367: `if (!bFlagsValid) { Iface->get_type(CachedFlags);
    // bFlagsValid = 1; } return CachedFlags;` -- the guard is raised AFTER the vcall.
    unsigned GetFlags();

    // FUN_062c47c0, item 1283: `if (!bPosValid) { bPosValid = 1;
    // Iface->get_physical_radius(CachedRadius, CachedPos); }` -- the guard is raised
    // BEFORE the vcall. Most retail call sites inline it.
    void RefreshPos();
};
static_assert(sizeof(FAiTarget) == 0x88, "FAiTarget must match retail's 0x88-byte AI-target cache record");
static_assert(offsetof(FAiTarget, CachedFlags) == 0x08, "FAiTarget+0x08 CachedFlags");
static_assert(offsetof(FAiTarget, CachedPos) == 0x0C, "FAiTarget+0x0C CachedPos");
static_assert(offsetof(FAiTarget, CachedRadius) == 0x18, "FAiTarget+0x18 CachedRadius");
static_assert(offsetof(FAiTarget, CachedCorners) == 0x1C, "FAiTarget+0x1C CachedCorners");
static_assert(offsetof(FAiTarget, bAttitudeValid) == 0x7C, "FAiTarget+0x7C bAttitudeValid");
static_assert(offsetof(FAiTarget, bFlagsValid) == 0x7D, "FAiTarget+0x7D bFlagsValid");
static_assert(offsetof(FAiTarget, bPosValid) == 0x7E, "FAiTarget+0x7E bPosValid");
static_assert(offsetof(FAiTarget, bCornersValid) == 0x7F, "FAiTarget+0x7F bCornersValid");
static_assert(offsetof(FAiTarget, AttitudeRefIface) == 0x80, "FAiTarget+0x80 AttitudeRefIface");
static_assert(offsetof(FAiTarget, Iface) == 0x84, "FAiTarget+0x84 Iface");

// ============================================================================
// SystemManager — Common.dll.c FUN_062e6260 (SystemManager::SystemManager),
// Common.dll.c 116823-116884 / raw disasm Common.dll.txt 241928-242031.
// Decomp-tracker item 1779 (ctor) and the whole 0x062e6260-0x062ea220
// SystemManager-internal cluster (items 1780-1837/1879/1890 among others).
//
// IDENTIFICATION EVIDENCE (unchanged from the prior forward-declaration note
// in common_iface.h, repeated here as this is now the real definition site):
//   1) Sole call site FUN_062d2220 (IBehaviorManager's own ctor) does
//      `operator_new(0x195C)` then `FUN_062e6260()` and stores the result at
//      IBehaviorManager+0x08 -- exactly IBehaviorManager::Sys's documented
//      offset (common_iface.h).
//   2) This ctor default-constructs an embedded pub::AI::Personality at
//      +0x1584 -- the exact offset IBehaviorManager::get_personality /
//      set_personality already read/write via `Sys+0x1584`.
//   3) 0x1584 + sizeof(pub::AI::Personality) (0x3D8) == 0x195C, exactly the
//      operator_new size above -- Personality is SystemManager's LAST member.
//
// FULL STRUCTURAL TRACE (this pass, 2026-09-11): every sub-object the ctor
// default-constructs, in construction order, with the byte range it occupies
// (derived directly from the LEA/CALL sequence in the raw disassembly, i.e.
// "next sub-object's LEA offset minus this one's" = this one's real size):
//
//   +0x00           int    Unknown0        -- never written by this ctor
//   +0x04  IBehaviorManager* Owner         -- zeroed here, but only at the
//                     very end of the ctor; the REAL back-pointer to the
//                     owning IBehaviorManager is set right after, by
//                     FUN_062e6490 (item 1785, still blocked on the gun-
//                     hardpoint resolver family -- see that function's own
//                     notes) via `MOV [ESI+0x4],EAX` where EAX is the
//                     IBehaviorManager* passed down from its own ctor
//                     (Common.dll.txt:242148-242149). CONFIRMED 2026-09-11
//                     (d29x:common-port, item 1827): this is exactly the
//                     field FUN_062e92e0 dereferences as `*(P+4)` and feeds
//                     to IBehaviorManager::get_camera_level_status /
//                     ->CurrentBehaviorIndex, where P turns out to be
//                     the embedded ThrustManager's own Owner back-pointer
//                     (see +0x18 below) -- so `P+4` there IS this SAME Owner
//                     field, byte for byte.
//   +0x08  (0x08)   BaseWatcher Watcher    -- zeroed (both pointer fields)
//   +0x10           IObjDB* ObjectDb       -- zeroed at the very end
//   +0x14           byte   Unknown14 (+pad)-- zeroed at the very end
//   +0x18  (0x1C)   ThrustManager Thrust   -- CORRECTED 2026-09-17
//                     (claude:common-thrustmanager). This ctor zeroes only the
//                     first dword (`XOR EBX,EBX` / `MOV [ESI+0x18],EBX`,
//                     Common.dll.txt:241949/241955); the rest of the record is
//                     left alone here and initialised later by FUN_062e6490
//                     (item 1785), whose `MOV [ESI+0x18],ESI` at
//                     Common.dll.txt:242201 is NOT a "self-pointer" store at
//                     all -- it writes ThrustManager::Owner, which is a
//                     SystemManager*, and for the embedded instance that
//                     happens to be `this`. The 2026-09-11 "ArmedSelf" reading
//                     of this dword and the "0x18-byte opaque gap" that
//                     followed it are both superseded: the whole +0x18..+0x34
//                     range is one RTTI-attested retail class, whose float at
//                     +0x1C and zeroed Vector at +0x20..+0x2C the earlier note
//                     already spotted as "possibly a float+Vector pair". See
//                     the ThrustManager struct below for the full derivation,
//                     including how it settles items 1786/1796's long-standing
//                     "SystemManager+0x18 size contradiction".
//   +0x34  (0xF8)   SystemManagerTimerTable Timers
//                     ctor = FUN_062e6af0, item 1792. CONFIRMED (byte-exact,
//                     see the struct below) to embed 11x SustainedThresholdTimer
//                     (the same struct already ported in common_aisteer.h for
//                     the AI fire-control cluster) plus a small header/tail.
//   +0x12C (0x1238) SystemManagerGunGroupManager GunGroups
//                     ctor = FUN_062e9330, item 1828. Two 0x40-slot,
//                     0x20-byte-stride gun-group arrays (primary/secondary
//                     hardpoint records) plus the already-ported
//                     LauncherFireCacheSlot[16] array at GunGroups+0x1098
//                     (CheckLauncherLineOfFireCached, item 1838,
//                     common_aifirecontrol.cpp) -- this CONFIRMS that struct's
//                     owner is SystemManager itself, at exactly this offset.
//   +0x1364 (0xC8)  byte GunConvergenceState[0xC8]
//                     ctor = FUN_062ebbc0 = the ALREADY-PORTED
//                     ConstructGunConvergenceState() (common_aisteer.h/.cpp,
//                     item 1853) -- reused directly, not re-implemented. Its
//                     own highest touched byte (+0xC6) rounds up to exactly
//                     the 0xC8 the ctor's LEA math implies here.
//   +0x142C (0x6C)  TargetManager Targeting (common_iface.h)
//                     ctor = FUN_062ed570, item 1890 -- TargetManager::
//                     TargetManager(). Its vftable .rdata:063a0888 is
//                     TargetManager's own (RTTI .?AVTargetManager@@, base
//                     IObjInspect::SubtargetEnumerator): { enumerate_equip
//                     LAB_062ee9d0, enumerate_subpart 0x0632c0c0 }. The old
//                     note here filed it under an "IUStateGraph cluster"; there
//                     is no such class (see common_targetmanager.cpp's FUN_062ed780
//                     cluster header). Ported for real 2026-09-24.
//   +0x1498         4 bytes, not written by this ctor: the first dword of the
//                     loot sub-object at +0x1498 (FUN_062e6490 stores the
//                     SystemManager into it, .text:062e6641; LootBehavior
//                     passes Sys+0x1498 to FUN_062ece40). Owner1498 below.
//   +0x149C (0x08)  BaseWatcher Watcher149C -- zeroed
//   +0x14A4         short  Unknown14A4 (+pad) -- SUBOBJ_ID_NONE (0)
//   +0x14A8 (0x54)  RepairManager Repair
//                     ctor = inline in FUN_062e6260 itself (items 1780-1783,
//                     1879). RTTI-attested; see the RepairManager comment below.
//                     Its +0x50 CheckTimer (absolute +0x14F8) is not written by
//                     this ctor.
//   +0x14FC (0x88)  FAiTarget SelfTarget -- the own ship's AI-target record; every
//                     intercept-clearance helper's "Elem" is `Sys + 0x14fc`. The ctor
//                     writes only its four guards and two interface pointers
//                     (.text:062e63a9-062e63c7); InitSystemManager and the tick fill
//                     AttitudeRefIface (+0x157C) and Iface (+0x1580). Iface is the own
//                     ship's IObjInspect (live debug 2026-09-15: server.dll's `Ship`).
//   +0x1584 (0x3D8) pub::AI::Personality PersonalityData
//                     Already-real type (common_pub.h). Matches
//                     IBehaviorManager::get_personality/set_personality's
//                     existing `Sys+0x1584` raw-offset access (common_behaviormanager.cpp).
//
// Total: 0x195C bytes, matching the ctor's sole caller's `operator_new(0x195C)`
// exactly (see static_assert below).
//
// Retail has no out-of-line destructor: ~IBehaviorManager (FUN_062d2530) inlines it at
// .text:062d25bf-062d265d as the member unwind (see ~SystemManager in
// common_systemmanager.cpp).
// ============================================================================

// ---------------------------------------------------------------------------
// ThrustManager — SystemManager+0x18, 0x1C bytes. RTTI-attested retail class:
// the exported mangled name `?engage_cruise@ThrustManager@@QAEX_N@Z`
// (ordinal 2476 in the Common.dll export table) names the one method retail
// itself labels, at .text:062e90a0.
//
// 2026-09-17 (claude:common-thrustmanager) -- THIS STRUCT RESOLVES THE
// "SystemManager+0x18 size contradiction" decomp-tracker items 1786/1796
// carried since 2026-09-11. That contradiction rested on a mis-read of item
// 1786's own call site: it claimed FUN_062e72d0 (item 1796) is invoked with
// ECX == SystemManager+0x18, which would make ThrustManager reach 0xBC bytes
// past +0x18 and collide with Timers at +0x34. It is not. The real ECX comes
// from a SPILLED STACK SLOT, and the spill is +0x34, not +0x18:
//
//   entry: SUB ESP,0xc; PUSH EBX; PUSH EBP; PUSH ESI; PUSH EDI
//          => baseline ESP == entry_esp - 0x1C, and [baseline+0x20] == param_1
//             (independently confirmed by .text:062e6743 reading param_1 there)
//   .text:062e67c5  PUSH EBP                      ; ESP = baseline - 4
//   .text:062e67c6  MOV [ESP+0x1c],ECX            ; => [baseline+0x18] = ECX
//                                                 ;    ECX = LEA [ESI+0x34] (Timers)
//   ... every intervening CALL is balanced (each `RET n` cleans its own
//       arguments; the one cdecl call, FUN_062ed460, is followed by its own
//       `ADD ESP,0xc`) so ESP is back at baseline by ...
//   .text:062e68ab  MOV ECX,[ESP+0x18]            ; => ECX = ESI+0x34 = &Timers
//   .text:062e68af  CALL FUN_062e72d0
//
// The sibling spill two lines earlier, `MOV [ESP+0x18],ECX` at .text:062e675d
// with ECX = LEA [ESI+0x12c], is read back at .text:062e6897 as [ESP+0x14]
// and feeds RefreshWeaponFireControlConfig(&GunGroups) -- the two spills are
// adjacent stack slots and the earlier pass conflated them. FUN_062e72d0's
// own first instruction `MOV EAX,[ECX+4]` then reading a Personality at
// EAX+0x1770 settles it on its own: SystemManagerTimerTable::Owner is at +0x04
// and IS a SystemManager*, whereas ThrustManager+0x04 is a float. So item
// 1796's `this` is &Timers and ThrustManager is not involved in it at all.
//
// ThrustManager IS embedded at SystemManager+0x18 and IS only 0x1C bytes,
// exactly filling the range the port previously modelled as
// `SystemManager* ArmedSelf` + `unsigned char Opaque1C[0x18]`. Layout derived
// from the initialiser block in FUN_062e6490 (item 1785, Common.dll.txt
// 242201-242214, `this` == ESI == the SystemManager) plus every method that
// takes one as ECX:
//
//   .text:062e653c  MOV  [ESI+0x18],ESI          ; Owner      = the SystemManager
//   .text:062e654f  MOV  [ESI+0x1c],0x40a00000   ; MaxThrottle = 5.0f
//   .text:062e6556  MOV  [ESI+0x20],0            ; \
//   .text:062e6559  MOV  [ESI+0x24],0            ;  > AxisThrottle = (0,0,0)
//   .text:062e655c  MOV  [ESI+0x28],0            ; /
//   .text:062e654b  MOV  byte [ESI+0x2c],0       ; bCruiseEngaged   = false
//   .text:062e6562  MOV  byte [ESI+0x2d],1       ; bCruiseUnobstructed = true
//   .text:062e6547  MOV  byte [ESI+0x2e],0       ; bEngineActive    = false
//   .text:062e653f  MOV  byte [ESI+0x2f],1       ; bAutoLevel       = true
//   .text:062e6543  MOV  byte [ESI+0x30],1       ; bAutoAvoidance   = true
//   .text:062e658e  MOV  byte [ESI+0x30],0       ;   ... cleared again when the
//                                                ;   watched object reports
//                                                ;   is_player() (vtable+0xbc)
//
// Nothing anywhere writes or reads +0x19..+0x1B, so the record ends at 0x19
// and pads to 0x1C -- which is exactly where Timers begins. No contradiction
// remains, and SystemManager's established +0x34 Timers offset is untouched.
//
// WHY THE OLD "ArmedSelf" READING WAS *BYTE*-EQUIVALENT: retail stores `this`
// (the SystemManager) into +0x18 because that dword is ThrustManager::Owner,
// not because SystemManager keeps a self-pointer. Everything the previous
// model derived from it still reads the same four bytes; only the name and
// the reason changed. CheckSystemManagerSteeringGate / SetSystemManagerOpaqueFlag0x2F
// are kept below with their old signatures (sibling lanes call them) and now
// forward to the real ThrustManager members.
//
// Every method below is __thiscall on the ThrustManager in retail (callers form
// `ECX = Sys + 0x18`); they were free `Behavior_*` functions taking a `void*
// ComponentThis` until CMN-S2 AI4 (2026-10-14). Definitions: common_thrustmanager.cpp.
// ---------------------------------------------------------------------------
// Retail exports only engage_cruise (no class-level export: no operator=).
struct ThrustManager
{
    // +0x00: back-pointer to the owning SystemManager. Read as `*(this)` by
    // every method, then `Owner->Watcher` (+0x08) is resolved through the
    // module-wide `-8` Watchable->IObjRW downcast (ObjRwFromWatchable).
    SystemManager* Owner;

    // +0x04: upper clamp applied to SetThrottle's incoming throttle value
    // (`FCOMP [ESI+0x4]`, .text:062e89dd / .text:062e89ef). Initialised to 5.0f.
    // TODO: retail's own name unknown; "the value a requested throttle is clamped
    // down to" is all the evidence supports.
    float MaxThrottle;

    // +0x08..+0x13 (formerly CachedPosition): SetAxisThrottle (FUN_062e8c20) refreshes it
    // in place from IObjInspect::get_axis_throttle, compares it against the requested
    // axis throttle with a squared-delta epsilon (DAT_063a07e4) and, on any axis
    // differing, calls IObjAffect::set_axis_throttle and copies the request in.
    // Zeroed by the initialiser.
    Vector AxisThrottle;

    // +0x14: cached "cruise is currently commanded ON" byte. engage_cruise
    // writes it; SetThrottle reads and writes it.
    unsigned char bCruiseEngaged;

    // +0x15: "cruise is unobstructed" (formerly Field_0x15). Set to 1 by the initialiser and
    // re-armed each tick from PathfindManager::IsCruiseUnobstructed (FUN_062de160: no
    // avoidance contact and no obstacle reaction running; SystemManager tick .text:062e6783).
    // engage_cruise refuses an ENGAGE while it is 0 (062e90a6-062e90b3);
    // SetThrottle downgrades Mode 2 to Mode 0 on it. The name is derived from
    // that writer; retail's own name is not exported.
    unsigned char bCruiseUnobstructed;

    // +0x16 (formerly Field_0x16): the engine activation last requested through
    // SetEngineActive (FUN_062e91d0) -- compared at .text:062e922a, stored at
    // .text:062e92bc. Zeroed by the initialiser; nothing else touches it.
    unsigned char bEngineActive;

    // +0x17: the auto-level switch (formerly Field_0x17). Its only writer after the
    // initialiser is SetSteeringFallbackFlag (FUN_062e92d0), reached solely from the export
    // IBehaviorManager::update_current_behavior_auto_level(bool) (.text:062d3cf0-062d3d01,
    // `ECX = [this+8] + 0x18`), the same way bAutoAvoidance below belongs to
    // update_current_behavior_auto_avoidance. Read as CheckSteeringGate's final fallback.
    unsigned char bAutoLevel;

    // +0x18: the auto-avoidance switch. Set to 1 by the initialiser, then
    // cleared when the watched object answers true to IObjInspect::is_player()
    // (vtable+0xbc) -- a player starts with it off. The exported
    // IBehaviorManager::update_current_behavior_auto_avoidance(bool) is
    // `MOV ECX,[ECX+8]; ADD ECX,0x18; JMP 06263bd0` (.text:062d3d40-062d3d4e),
    // and 06263bd0 is the ICF-folded `MOV AL,[ESP+4]; MOV [ECX+0x18],AL;
    // RET 4` Ghidra labels CNudgeEngine::Activate -- i.e. this byte. Read by
    // item 1786's own tick at absolute SystemManager+0x30. Renamed from
    // Field_0x18 2026-09-24 after that export; the retail member name is not
    // known.
    unsigned char bAutoAvoidance;

    unsigned char Pad0x19[3];

    // .text:062e90a0, retail symbol `?engage_cruise@ThrustManager@@QAEX_N@Z`.
    // `QAE` == __thiscall, and the body ends in `RET 0x4` for its single bool
    // argument -- declared as an ordinary member so the compiler emits exactly
    // that convention rather than a hand-written __stdcall thunk.
    COMMON_API void engage_cruise(bool bEngage);

    // .text:062e92d0 (item 1826) and .text:062e92e0 (item 1827). Both are
    // __thiscall members of THIS class, not of SystemManager -- their sole
    // callers compute `SystemManager + 0x18` and then index +0x17 off it.
    void SetSteeringFallbackFlag(unsigned char Value);
    bool CheckSteeringGate();

    // 0x06263bd0, the ICF-folded `MOV AL,[ESP+4]; MOV [ECX+0x18],AL; RET 4`
    // that Ghidra labels CNudgeEngine::Activate (also CSteering::Activate,
    // StateMachineMessage::set_delivered, EquipDesc::set_temporary). Reached on
    // this class only through IBehaviorManager::
    // update_current_behavior_auto_avoidance's tail jump with ECX = Sys+0x18.
    // Stores bOn into bAutoAvoidance and leaves it in AL.
    bool SetAutoAvoidance(bool bOn);

    // formerly Behavior_NotifyRegistrationFlag -- FUN_062e91d0 (item 1825), RET 4. Unless the
    // owner ship is missing: when the ship is the player, or bActive differs from
    // bEngineActive, finds the ship's engine (find_equipment(&Id, 1, EquipTypeEngine)); when
    // one exists, is not destroyed and its active bit differs from bActive, switches it
    // through IObjAffect::slot_4c(&Id, bActive, -1). Then bEngineActive = bActive.
    void SetEngineActive(bool bActive);

    // formerly Behavior_RefreshCachedPositionIfMoved -- FUN_062e8c20 (item 1820), RET 4.
    // Unless the owner ship is missing or get_axis_throttle(AxisThrottle) fails: when
    // Wanted differs from the refreshed cache on some axis by more than the epsilon,
    // set_axis_throttle(Wanted) and AxisThrottle = Wanted.
    void SetAxisThrottle(Vector const& Wanted);

    // formerly Behavior_RefreshCachedValueIfChanged -- FUN_062e9030 (item 1823), RET 4.
    // SetEngineActive(true); then, when get_strafe_dir succeeds and answers something other
    // than Dir, set_strafe_dir(Dir).
    void SetStrafeDir(StrafeDir Dir);

    // formerly Behavior_ConditionalNotify -- FUN_062e9120 (item 1824), RET 4. Nothing while
    // the ship cruises outside a trade lane. Otherwise SetEngineActive(true) when bActive,
    // and the thrusters (afterburner) are switched through IObjAffect::slot_54(bActive, -1)
    // unless are_thrusters_active already answers bActive.
    void SetThrustersActive(bool bActive);

    // formerly Behavior_UpdateCruiseGate -- FUN_062e89a0 (item 1819), RET 8. Mode 2 sets the
    // throttle (clamped to MaxThrottle, then [0, 1.2]) and, for a positive throttle on an
    // unobstructed path, engages cruise; Mode 0 disengages cruise and sets the throttle
    // clamped to [-1, 1]. Mode 2 falls back to 0 while !bCruiseUnobstructed; any other Mode
    // does nothing. False only when the owner ship is missing.
    bool SetThrottle(float Throttle, int Mode);

    // formerly Behavior_SyncPredictedPosition -- FUN_062e8ce0 (item 1821), RET 4.
    // SetEngineActive(true); turns the world direction WorldDir (normalised when longer
    // than 1) into ship space and hands it to IObjAffect::set_nudge_vec unless the ship's
    // current nudge already matches it within 1e-6 on every axis.
    void SetNudgeDirection(Vector const& WorldDir);

    // formerly BlendNudgeIntoPredictedDirection -- FUN_062e8e60 (item 1822), RET 8. Blends
    // the ship's current nudge (in world space, dropped unless it points within acos(0.99)
    // of WorldDir) with WorldDir scaled by 1.5 * DeltaTime * |WorldDir| when WorldDir is the
    // longer of the two, and hands the result to SetNudgeDirection.
    void BlendNudgeDirection(Vector const& WorldDir, float DeltaTime);

private:
    // The owner's watched ship: `[Owner+8] ? [Owner+8] - 8 : 0` (the -8 Watchable
    // adjustment), which every method above re-evaluates before each use.
    IObjRW* OwnerShip() const;
};

static_assert(sizeof(ThrustManager) == 0x1C,
    "ThrustManager occupies SystemManager+0x18..+0x34 exactly (Timers starts at +0x34)");
static_assert(offsetof(ThrustManager, MaxThrottle) == 0x04, "ThrustManager+0x04 MaxThrottle");
static_assert(offsetof(ThrustManager, AxisThrottle) == 0x08, "ThrustManager+0x08 AxisThrottle");
static_assert(offsetof(ThrustManager, bCruiseEngaged) == 0x14, "ThrustManager+0x14 bCruiseEngaged");
static_assert(offsetof(ThrustManager, bCruiseUnobstructed) == 0x15, "ThrustManager+0x15 bCruiseUnobstructed");
static_assert(offsetof(ThrustManager, bEngineActive) == 0x16, "ThrustManager+0x16 bEngineActive");
static_assert(offsetof(ThrustManager, bAutoLevel) == 0x17, "ThrustManager+0x17 bAutoLevel");
static_assert(offsetof(ThrustManager, bAutoAvoidance) == 0x18, "ThrustManager+0x18 bAutoAvoidance");

// FUN_062e6490's own ThrustManager initialiser block (Common.dll.txt
// 242201-242214). Kept separate from any constructor because retail runs it
// LATE -- SystemManager's real ctor leaves this whole range alone, and this
// block only runs once the owner's Watcher has a live, -8-adjustable target.
void InitThrustManager(ThrustManager* Self, SystemManager* Owner);

// ---------------------------------------------------------------------------
// FAiTargetVector — the MSVC 7.0 `std::vector<FAiTarget>` embedded at
// SystemManagerTimerTable+0x0C (absolute SystemManager+0x40). Defined here
// rather than in common_iustategraph.h so that the timer table can hold it by
// value; that header's own FAiTargetList is an alias of the table (see below).
// ---------------------------------------------------------------------------
struct FAiTargetVector
{
    // +0x00: the allocator member. `std::allocator<T>` is empty but still
    // occupies its own (padded) slot as the vector's first member in this
    // compiler's layout, which is why Begin sits at +0x04 and not +0x00.
    // Retail's ctor writes exactly one byte here (FUN_062e6af0,
    // .text:062e6b16 `MOV byte ptr [ESI+0xc],AL`).
    unsigned char AllocatorPad[4];
    FAiTarget* Begin;   // +0x04  _First
    FAiTarget* End;     // +0x08  _Last  (one past the live range)
    FAiTarget* Cap;     // +0x0C  _End   (one past the allocated range)
};
static_assert(sizeof(FAiTargetVector) == 0x10, "FAiTargetVector must be 0x10 bytes");
static_assert(offsetof(FAiTargetVector, Begin) == 0x04, "FAiTargetVector+0x04 Begin");
static_assert(offsetof(FAiTargetVector, End) == 0x08, "FAiTargetVector+0x08 End");
static_assert(offsetof(FAiTargetVector, Cap) == 0x0C, "FAiTargetVector+0x0C Cap");

// ---------------------------------------------------------------------------
// SustainedThresholdTimer -- 0x10 bytes; eleven of them sit in SystemManagerTimerTable at
// +0x28 (retail ctor FUN_062e6af0 writes {0, 100.0f, 1.0f, 0.0f} into each).
// ---------------------------------------------------------------------------
struct SustainedThresholdTimer
{
    unsigned char Active;       // +0x00 latched once Level exceeds TriggerThreshold
    float TriggerThreshold;     // +0x04
    float Duration;             // +0x08 how long the latch holds
    float Accumulator;          // +0x0C time spent latched

    // formerly UpdateSustainedThresholdTimer(Timer, DeltaTime, Level) -- FUN_062e6a20 (item
    // 1790), thiscall, RET 8. Latches when Level > 0 and Level > TriggerThreshold; while
    // latched, accumulates DeltaTime and answers true until the unrounded sum exceeds Duration,
    // which clears the latch and answers false. Definition: common_aifirecontrol.cpp.
    bool Update(float DeltaTime, float Level);
};
static_assert(sizeof(SustainedThresholdTimer) == 0x10, "SustainedThresholdTimer must match retail's 16-byte raw layout");

// ---------------------------------------------------------------------------
// SystemManagerTimerTable — SystemManager+0x34. Ctor: Common.dll.c 0x062e6af0
// (FUN_062e6af0), item 1792, Common.dll.c 117302-117380.
//
// MERGE NOTE (2026-09-17, claude:common-crossfile-harvest). This struct used to
// open with two blind blobs, `Unknown00[4]` and `Unknown08[0x18]`, the latter
// annotated "includes an embedded JustifyNode-adjacent render-cache pair at
// relative +0x0C/+0x10/+0x14/+0x1C ... Genuinely NOT just padding, but not
// fully named". That whole span is now named: it is the AI "hostile candidate"
// target list the 0x062e7580-0x062e8400 cluster works on (items 1800/1802/1805,
// ported in common_iustategraph.{h,cpp}), which independently resolved its own
// owner to SystemManager+0x34 -- the same object. The two models are merged
// here; common_iustategraph.h's FAiTargetList is now an alias of this struct.
//
// Byte-level proof, all from this table's own retail constructor FUN_062e6af0
// (Common.dll.txt, .text:062e6af0-):
//   .text:062e6b0d  XOR EBX,EBX                    ; EBX = 0 for everything below
//   .text:062e6b16  MOV byte ptr  [ESI + 0xc],AL   ; the vector's 1-byte allocator
//   .text:062e6b19  MOV dword ptr [ESI + 0x10],EBX ; Targets.Begin = null
//   .text:062e6b1c  MOV dword ptr [ESI + 0x14],EBX ; Targets.End   = null
//   .text:062e6b1f  MOV dword ptr [ESI + 0x18],EBX ; Targets.Cap   = null
//   .text:062e6b22  MOV byte ptr  [ESI + 0x1d],BL  ; bKeepCache = 0
// -- i.e. a default-constructed empty vector at +0x0C, exactly where
// FAiTargetVector puts _First/_Last/_End. The already-ported members agree:
//   * ArmSystemManagerTimerTable (item 1793) passes &(this+0x08) as
//     find_equipment's `unsigned short*` out-buffer -> +0x08 is a 16-bit
//     equipment id, and it is the scanner whose scan list this cluster rebuilds;
//   * SetSystemManagerSharedListHead (item 1787) does exactly
//     `bBuilt = 0; End = Begin;` on Timers+0x1C / Timers+0x14 / Timers+0x10 --
//     an empty-the-cached-list, which is what the old "conditional reset of the
//     same two dwords" note was describing without knowing it;
//   * item 1786's "JustifyNode::Execute call over that exact range" is the ICF
//     fold of the vector's own destroy-a-range helper (DestroyAiTargetRange,
//     common_iustategraph.h) -- not a render cache.
// ---------------------------------------------------------------------------
struct SystemManagerTimerTable
{
    // +0x00: never written by the ctor; SetSystemManagerSharedListHead (item
    // 1787, FUN_062e68e0) stores its IObjDB* argument here, alongside the copy
    // it puts in SystemManager::ObjectDb. Read by FUN_062e8720
    // (AppendSystemManagerTimerListEntry, .text:062e8747 `MOV ECX,[EBX]`), which
    // calls its slot 1 FindById.
    IObjDB* ObjectDb;

    // +0x04: back-pointer to the owning SystemManager. NOT written by this
    // ctor (left null/uninitialized at construction time) -- set later by a
    // still-unported per-tick "arm" call (item 1793) -- but read directly
    // (no smart-handle -8 adjustment, unlike the "Iface" convention used
    // elsewhere in this cluster) by items 1794/1795/1807/1815/1837, all
    // 2026-09-11 d250:common-port findings. CONFIRMED via FUN_062e6d40's own
    // raw disasm: `MOV ESI,[EBX+4]` then `TEST AL,[ESI+0x157a]` directly,
    // matching SystemManager+0x157a byte-for-byte with no -8 correction.
    SystemManager* Owner;

    // +0x08: the equipment id whose status gates the whole list rebuild.
    // ArmSystemManagerTimerTable (item 1793) fills it via
    // IObjInspect::find_equipment(&ScannerEquipId, 1, 0x80000), and
    // BuildAiTargetList passes `&this->ScannerEquipId` as
    // get_equipment_status's `unsigned short const&` argument
    // (.text:062e7633 `LEA EAX,[ESI+8]`) -- which is what pins the type.
    unsigned short ScannerEquipId;
    unsigned char Pad_0x0A[2];

    // +0x0C (absolute SystemManager+0x40): the target list itself.
    FAiTargetVector Targets;

    // +0x1C: set once the list has been built at least once. Not written by
    // the ctor; cleared by items 1787 and 1793.
    unsigned char bBuilt;
    // +0x1D: when clear, the list is dropped and rebuilt on every call; when
    // set (together with bBuilt) the cached list is returned untouched. Also
    // forwarded verbatim as get_scanlist's `bFlag` argument, where it means
    // "do not clear the scanner's cache first".
    unsigned char bKeepCache;
    unsigned char Pad_0x1E[2];

    // +0x20: per-tick multiplier for Level's passive decay (item 1795's own
    // `Level -= DecayRate * UnknownScale_0xD8 * DeltaTime` formula).
    float DecayRate;

    // +0x24: shared "signal level" compared against every one of the 11
    // SustainedThresholdTimer slots' own TriggerThreshold each tick (item
    // 1795), and accumulated into by item 1807 (AccumulateSystemManagerTimerLevel)
    // whenever a matching, in-range candidate is found.
    float Level;

    // +0x28..+0xD8: 11 SustainedThresholdTimer records, each constructed to {Active=0,
    // TriggerThreshold=100.0f, Duration=1.0f, Accumulator=0.0f}. Their thresholds come from
    // RefreshSystemManagerTimerThresholdsFromPersonality (slot 6 = fire_guns_damage_trigger,
    // 7 = drop_mines_damage_trigger, 8 = fire_missiles_damage_trigger, ...).
    SustainedThresholdTimer Timers[11];

    float UnknownScale_0xD8;       // +0xD8 = 0.5f, role not identified
    unsigned char Reserved0xDC;    // +0xDC uninitialized-in-retail artifact byte (Ghidra's `local_11`)
    unsigned char Pad0xDD[3];
    void* ListHeadNode;            // +0xE0: heap-allocated (operator_new(0x10)) empty
                                    // self-referential 2-pointer circular-list sentinel
    int ListCount;                 // +0xE4 = 0
    // +0xE8: a real BaseWatcher, IDENTIFIED 2026-09-17 from FUN_062e6d40
    // (item 1794), which calls `BaseWatcher::set(&Timers[0xE8], nullptr)` at
    // .text:062e6d4f and `BaseWatcher::set_pointer(&Timers[0xE8], Cand+8)` at
    // .text:062e6ea1. It holds the current best missile-reaction candidate.
    // Stored as raw bytes so ConstructSystemManagerTimerTable's whole-struct memset
    // stays valid. Same reinterpret_cast pattern as ClearGunConvergenceStateWatcherIfTarget.
    unsigned char BestCandidateWatcherBytes[0x08]; // +0xE8..+0xF0
    int Gap0xF0;                   // +0xF0 genuine gap, never written by the ctor
    float BestValueSoFar;          // +0xF4 = FLT_MAX sentinel

    // FUN_062d2700 (item 1544), the table's own out-of-line destructor (retail reaches it
    // from the SystemManager ctor's EH unwind funclets 0639507b / 06395369 as
    // `ADD ECX,0x34; JMP FUN_062d2700`; IBehaviorManager's dtor inlines the same steps).
    // The +0xE8 watcher is cleared, the candidate list is torn down (every node erased,
    // the sentinel freed, +0xE0 / +0xE4 zeroed), then the Targets buffer is freed and its
    // three pointers zeroed.
    ~SystemManagerTimerTable();

    // The Active byte of Timers[0..5] (+0x28 + 0x10 * N); retail `MOV AL,[ECX+disp]; RET`.
    // Defined in common_sysmgr_timertable.cpp (CMN-SPLIT 2026-10-14).
    bool IsTimer0Active() const;    // formerly FUN_062e8810 ([ECX+0x28])
    bool IsTimer1Active() const;    // formerly FUN_062e8820 ([ECX+0x38])
    bool IsTimer2Active() const;    // formerly FUN_062e8860 ([ECX+0x48])
    bool IsTimer3Active() const;    // formerly FUN_062e8830 ([ECX+0x58])
    bool IsTimer4Active() const;    // formerly FUN_062e8850 ([ECX+0x68])
    bool IsTimer5Active() const;    // formerly FUN_062e8840 ([ECX+0x78])

    // formerly FUN_062e74a0 (item 1798). ObjectDb->FindById(Id), returned only when the found
    // object's get_id() equals *Id; null when *Id is 0, ObjectDb is null or nothing is found.
    IObjRW* FindVerifiedObject(unsigned const* Id) const;

    // formerly the free FindObjectDockingAtBase(ObjDbSlot, BaseIface) -- FUN_062e7400 (item
    // 1797), thiscall on Sys+0x34, RET 4: for a moon/planet base (type & 3), walks ObjectDb's
    // docking rings (type mask 0x20) and returns the first whose get_dock_target equals the
    // base's get_base id; null otherwise. Defined in common_aiintercept.cpp.
    IObjRW* FindObjectDockingAtBase(IObjInspect const* BaseIface) const;

    // formerly the free HasTargetInForwardArc(Self, MaxAngle, MaxRange) -- FUN_062e8400 (item
    // 1806), RET 8. True when some target of BuildAiTargetList with a type in 0x7f0000 lies
    // ahead of the own ship (Owner's +0x14FC record) within MaxRange, moves faster than 10,
    // heads the same way (forward dot > 0.95f), is hostile (attitude <= -0.6f) and lies within
    // MaxAngle radians of the own forward axis. Definition: common_aifirecontrol.cpp.
    bool HasTargetInForwardArc(float MaxAngle, float MaxRange);

    // formerly ObstacleCandidateSource_Resolve(Self, OutParam, FilterMask) -- FUN_062e74e0
    // (item 1799), RET 8. Unless the own ship (Owner->Watcher) is missing or its scanner
    // (ScannerEquipId) reports EquipStatusFlagDestroyed, OutList = the ship's
    // get_scanlist(FilterMask, bKeepCache); true when that answered 0. Callers test OutList.
    // Definition: common_obstacleavoidance.cpp.
    bool GetOwnScanList(ScanList const*& OutList, unsigned FilterMask);
};
static_assert(sizeof(SystemManagerTimerTable) == 0xF8,
    "SystemManagerTimerTable must match retail's 0xF8-byte sub-object (SystemManager+0x34)");
static_assert(offsetof(SystemManagerTimerTable, ObjectDb) == 0x00, "SystemManagerTimerTable+0x00 ObjectDb");
static_assert(offsetof(SystemManagerTimerTable, Owner) == 0x04, "SystemManagerTimerTable+0x04 Owner");
static_assert(offsetof(SystemManagerTimerTable, ScannerEquipId) == 0x08, "SystemManagerTimerTable+0x08 ScannerEquipId");
static_assert(offsetof(SystemManagerTimerTable, Targets) == 0x0C, "SystemManagerTimerTable+0x0C Targets (the std::vector<FAiTarget> header)");
static_assert(offsetof(SystemManagerTimerTable, bBuilt) == 0x1C, "SystemManagerTimerTable+0x1C bBuilt");
static_assert(offsetof(SystemManagerTimerTable, bKeepCache) == 0x1D, "SystemManagerTimerTable+0x1D bKeepCache");
static_assert(offsetof(SystemManagerTimerTable, DecayRate) == 0x20, "SystemManagerTimerTable+0x20 DecayRate");
static_assert(offsetof(SystemManagerTimerTable, Level) == 0x24, "SystemManagerTimerTable+0x24 Level");
static_assert(offsetof(SystemManagerTimerTable, Timers) == 0x28, "SystemManagerTimerTable+0x28 Timers");
static_assert(offsetof(SystemManagerTimerTable, ListHeadNode) == 0xE0, "SystemManagerTimerTable+0xE0 ListHeadNode");
static_assert(offsetof(SystemManagerTimerTable, BestCandidateWatcherBytes) == 0xE8,
    "SystemManagerTimerTable+0xE8 BestCandidateWatcher (a real BaseWatcher)");
static_assert(offsetof(SystemManagerTimerTable, BestValueSoFar) == 0xF4, "SystemManagerTimerTable+0xF4 BestValueSoFar");

void ConstructSystemManagerTimerTable(SystemManagerTimerTable* Self);

// 2026-09-11 d250:common-port -- SystemManagerTimerTable-scoped functions from
// the 0x062e6260-0x062ea450 blocked-item pass, ported now that this table's
// own layout (above) is real. See common_sysmgr_timertable.cpp for each one's
// full raw-disasm derivation.

// FUN_062e8720, item 1808. Source is an id block {OwnerId, missile id}
// (IBehaviorManager::Slot00 event 6, sent by Server StarSystem::create_guided).
// Looks the missile up with Self->ObjectDb->FindById(&Source[1]) and appends a
// node watching it onto the circular sentinel list at
// Self->ListHeadNode/Self->ListCount.
void AppendSystemManagerTimerListEntry(SystemManagerTimerTable* Self, void* Source);

// FUN_062e86d0, item 1807. If Candidate's id (its own +8 field) matches the
// owning SystemManager's current ExternalIface id, and Candidate's state
// (+0x14) is in the open range (1,8), accumulates Candidate's value (+0x18)
// into Self->Level.
void AccumulateSystemManagerTimerLevel(SystemManagerTimerTable* Self, void* Candidate);

// FUN_062e8870, item 1815. True if the current best-candidate distance
// (Self->BestValueSoFar) falls under any of the owning SystemManager's
// pub::AI::Personality::MissileReaction range thresholds.
bool SystemManagerTimerTableHasMissileReactionTarget(SystemManagerTimerTable const* Self);

// FUN_062e6c60, item 1793. Arms the timer table for a newly-attached Owner:
// stores Self->Owner once Owner->Watcher holds a live object, looks up the
// object's scanner (find_equipment(&ScannerEquipId, 1, 0x80000), slot 0x114),
// and on success sets DecayRate = MaxHitPts + MaxShield -- the second out-param
// of get_status(float&, float&) (slot 0x3C) plus the second out-param of
// get_shield_status(float&, float&, bool&) (slot 0x44). CORRECTED 2026-09-24:
// the port used to skip the slot-0x44 call ("argument shape not pinned down")
// and summed get_status's two out-params instead, i.e. HitPts + MaxHitPts.
bool ArmSystemManagerTimerTable(SystemManagerTimerTable* Self, SystemManager* Owner);

// ---------------------------------------------------------------------------
// SystemManagerGunGroupManager -- SystemManager+0x12C, 0x1238 bytes; ctor FUN_062e9330
// (item 1828). No vtable and no RTTI, so retail's class name is unknown. Every method below
// is thiscall on &SystemManager::GunGroups in retail.
//
// It runs the AI's guns and mine droppers: ResolveHardpoints fills Guns from
// find_equipment(EquipTypeGun) and MineDroppers from find_equipment(EquipTypeMineDropper);
// RefreshFromPersonality copies the +0x103C..+0x1088 tunables from the Personality's
// GunParams / MineParams / MissileParams (field names below follow the INI keys Content's
// personality loader reads into them); UpdateTimers advances every slot's burst phase; the
// Update*State methods fire. The countermeasure droppers have their own block at +0x1218.
// ---------------------------------------------------------------------------

// One gun or mine-dropper record, 0x20 bytes, natural padding. The ctor writes +0x02..+0x04 = 0,
// BurstTime 3.0f, NoBurstTime 1.5f, FireInterval 0.5f, Phase 0 and +0x1C = 0; the padding
// (+0x05..+0x07, +0x1D..+0x1F) is never written.
struct FGunGroupSlot
{
    // MunitionKind: PopulateSlots maps get_equipment_val(EquipValMunitionKind) 0 / 1 / 2 / 3 to
    // 0 / 1 / 2 / 4 and forces mine droppers to 3; 5 until a slot is classified.
    enum : int
    {
        KindPlain = 0,              // munition without a seeker
        KindSeeker = 1,             // munition with a seeker
        KindHpType10 = 2,           // munition HpType 0xa
        KindMineDropper = 3,
        KindCruiseDisruptor = 4,
        KindUnclassified = 5,
    };

    unsigned short HardpointId;     // +0x00
    unsigned char bAutoTurret;      // +0x02 EquipValAutoTurret (formerly "Flag2" / "active byte")
    unsigned char bSeekerMunition;  // +0x03 EquipValSeeker
    unsigned char bFireTick;        // +0x04 UpdateTimers: set on the tick Phase crosses a FireInterval boundary
    int MunitionKind;               // +0x08
    float BurstTime;                // +0x0C Phase runs up to this ...
    float NoBurstTime;              // +0x10 ... then restarts at -NoBurstTime
    float FireInterval;             // +0x14
    float Phase;                    // +0x18 >= 0 inside a burst
    unsigned char Field_0x1C;       // +0x1C only the ctor's zero store is known. TODO: meaning
};
static_assert(sizeof(FGunGroupSlot) == 0x20, "FGunGroupSlot sizeof 0x20");
static_assert(offsetof(FGunGroupSlot, bAutoTurret) == 0x02, "FGunGroupSlot+0x02 bAutoTurret");
static_assert(offsetof(FGunGroupSlot, bSeekerMunition) == 0x03, "FGunGroupSlot+0x03 bSeekerMunition");
static_assert(offsetof(FGunGroupSlot, bFireTick) == 0x04, "FGunGroupSlot+0x04 bFireTick");
static_assert(offsetof(FGunGroupSlot, MunitionKind) == 0x08, "FGunGroupSlot+0x08 MunitionKind");
static_assert(offsetof(FGunGroupSlot, BurstTime) == 0x0C, "FGunGroupSlot+0x0C BurstTime");
static_assert(offsetof(FGunGroupSlot, NoBurstTime) == 0x10, "FGunGroupSlot+0x10 NoBurstTime");
static_assert(offsetof(FGunGroupSlot, FireInterval) == 0x14, "FGunGroupSlot+0x14 FireInterval");
static_assert(offsetof(FGunGroupSlot, Phase) == 0x18, "FGunGroupSlot+0x18 Phase");
static_assert(offsetof(FGunGroupSlot, Field_0x1C) == 0x1C, "FGunGroupSlot+0x1C");

// One line-of-fire cache record (GunGroups+0x1098, sixteen of them), 0x18 bytes, natural
// padding. Keyed by (TargetId, HardpointId); see IsLineOfFireBlocked and UpdateTimers.
struct FLauncherFireCacheSlot
{
    unsigned char InUse;            // +0x00
    unsigned TargetId;              // +0x04
    unsigned short HardpointId;     // +0x08
    float Age;                      // +0x0C since claimed or last recomputed; evicted past 10 s
    float RefreshTimer;             // +0x10 while fresh, counts to 1 s, then NeedsRefresh
    unsigned char NeedsRefresh;     // +0x14 stale: the next query recomputes
    unsigned char bBlocked;         // +0x15 the cached answer
};
static_assert(sizeof(FLauncherFireCacheSlot) == 0x18, "FLauncherFireCacheSlot sizeof 0x18");
static_assert(offsetof(FLauncherFireCacheSlot, TargetId) == 0x04, "FLauncherFireCacheSlot+0x04 TargetId");
static_assert(offsetof(FLauncherFireCacheSlot, HardpointId) == 0x08, "FLauncherFireCacheSlot+0x08 HardpointId");
static_assert(offsetof(FLauncherFireCacheSlot, Age) == 0x0C, "FLauncherFireCacheSlot+0x0C Age");
static_assert(offsetof(FLauncherFireCacheSlot, RefreshTimer) == 0x10, "FLauncherFireCacheSlot+0x10 RefreshTimer");
static_assert(offsetof(FLauncherFireCacheSlot, NeedsRefresh) == 0x14, "FLauncherFireCacheSlot+0x14 NeedsRefresh");
static_assert(offsetof(FLauncherFireCacheSlot, bBlocked) == 0x15, "FLauncherFireCacheSlot+0x15 bBlocked");

// SystemManager+0x1344 (GunGroups+0x1218), 0x20 bytes: switches the ship's countermeasure
// droppers on and off from Personality CMParams. Methods are thiscall on this block.
struct FCmDropperWatcher
{
    SystemManager* Owner;           // +0x00
    float Timer;                    // +0x04
    unsigned char bActive;          // +0x08 the state last applied to every dropper
    int Count;                      // +0x0C
    unsigned short Ids[8];          // +0x10 (Bind lets find_equipment write up to 0x40 ids)

    // formerly BindHardpointIdWatcher(Self, Object) -- FUN_062eb9d0 (item 1850), RET 4. Rejects
    // a null owner or one whose Watcher holds no object; stores the owner. A player ship
    // answers true there. Otherwise clears the state, collects the ship's countermeasure
    // dropper ids through find_equipment(Ids, 0x40, EquipTypeCmDropper), counts them up to
    // the first zero and switches them on (SetActive(1)).
    bool Bind(SystemManager* NewOwner);

    // formerly UpdateWatcherTimer(Self, Dt) -- FUN_062eba70 (item 1851), RET 4. Not for a
    // player ship: Timer cycles through CMParams.CountermeasureUnactiveTime, SetActive(1)
    // below CountermeasureActiveTime and, unless the ship is a player, SetActive(0) between
    // the two.
    void Update(float Dt);

    // formerly SetWatcherActive(Self, NewState) -- FUN_062ebb10 (item 1852), RET 4. Unless
    // bActive already equals NewState, every dropper whose active bit differs is switched
    // through IObjAffect slot_4c(Id, NewState, -1); then bActive = NewState.
    void SetActive(unsigned char NewState);
};
static_assert(sizeof(FCmDropperWatcher) == 0x20, "FCmDropperWatcher sizeof 0x20");
static_assert(offsetof(FCmDropperWatcher, Timer) == 0x04, "FCmDropperWatcher+0x04 Timer");
static_assert(offsetof(FCmDropperWatcher, bActive) == 0x08, "FCmDropperWatcher+0x08 bActive");
static_assert(offsetof(FCmDropperWatcher, Count) == 0x0C, "FCmDropperWatcher+0x0C Count");
static_assert(offsetof(FCmDropperWatcher, Ids) == 0x10, "FCmDropperWatcher+0x10 Ids");

struct SystemManagerGunGroupManager
{
    int LastGunFireResult;              // +0x00 RunFireControlTick stores UpdateGunFireState's result
    SystemManager* Owner;               // +0x04 InitSystemManager (FUN_062e6490) writes it
    // +0x08: a BaseWatcher on the current target. Raw bytes because the ctor memsets the whole
    // block; TargetWatcher() is the typed view.
    unsigned char TargetWatcherBytes[8];
    unsigned Field_0x10;                // +0x10 bits 0x30000000 hold all fire; no writer in Common.dll. TODO: meaning
    Vector AimOffset;                   // +0x14 the aim point in the target's own frame
    float AimRefreshTimer;              // +0x20 aim point recomputed once this is <= 0 (UpdateGunFireState)
    float CruiseHeldTime;               // +0x24 how long the target has been cruising (UpdateTimers)
    float ScanTimer;                    // +0x28 ScanAutoTurretTargets
    float ScanPeriod;                   // +0x2C 1.0f
    FGunGroupSlot Guns[0x40];           // +0x30
    int GunCount;                       // +0x830
    int Field_0x834;                    // +0x834 written -1 by the ctor and InitSystemManager; never read
    FGunGroupSlot MineDroppers[0x40];   // +0x838
    int MineDropperCount;               // +0x1038

    // +0x103C..+0x1088, refreshed by RefreshFromPersonality (ctor defaults in FUN_062e9330).
    float GunFireIntervalTime;              // +0x103C GunParams gun_fire_interval_time
    float GunFireIntervalVariance;          // +0x1040 gun_fire_interval_variance_percent
    float AutoTurretIntervalTime;           // +0x1044 auto_turret_interval_time
    float GunFireBurstIntervalTime;         // +0x1048 gun_fire_burst_interval_time
    float GunFireBurstIntervalVariance;     // +0x104C gun_fire_burst_interval_variance_percent
    float GunFireNoBurstIntervalTime;       // +0x1050 gun_fire_no_burst_interval_time
    float AutoTurretBurstIntervalTime;      // +0x1054 auto_turret_burst_interval_time
    float AutoTurretNoBurstIntervalTime;    // +0x1058 auto_turret_no_burst_interval_time
    float AutoTurretBurstIntervalVariance;  // +0x105C auto_turret_burst_interval_variance_percent
    float MissileLaunchIntervalTime;        // +0x1060 MissileParams missile_launch_interval_time
    float MissileLaunchIntervalVariance;    // +0x1064 missile_launch_interval_variance_percent
    float MissileLaunchConeAngle;           // +0x1068 missile_launch_cone_angle
    float MissileLaunchRange;               // +0x106C missile_launch_range
    float MissileLaunchAllowOutOfRange;     // +0x1070 missile_launch_allow_out_of_range, the bool as a float
    float MineLaunchInterval;               // +0x1074 MineParams mine_launch_interval
    float MineLaunchConeAngle;              // +0x1078 mine_launch_cone_angle
    float Field_0x107C;                     // +0x107C ctor 0.0f; no other access. TODO: meaning
    float GunFireAccuracyConeAngle;         // +0x1080 GunParams gun_fire_accuracy_cone_angle
    float GunRange;                         // +0x1084 longest plain-gun range * jittered gun_range_threshold
    int bFireAllGuns;                       // +0x1088 fire_style MULTIPLE -> 1, SINGLE -> 0
    float Field_0x108C;                     // +0x108C ctor pi/32; no other access. TODO: meaning
    float WideConeAngle;                    // +0x1090 ctor pi/32; UpdateMissileFireState's wide aim angle
    unsigned char bEnabled;                 // +0x1094 ctor 1; FireAutoTurrets / RunFireControlTick need it
    FLauncherFireCacheSlot LineOfFireCache[16]; // +0x1098
    FCmDropperWatcher CmDroppers;           // +0x1218

    // Inlined into ~SystemManager at .text:062d2608-062d2614 (`LEA ECX,[Sys+0x134]; PUSH 0;
    // CALL BaseWatcher::set`), between the +0x1364 record and the timer table.
    ~SystemManagerGunGroupManager() { TargetWatcher().set(nullptr); }

    BaseWatcher& TargetWatcher() { return *(BaseWatcher*)TargetWatcherBytes; }
    BaseWatcher const& TargetWatcher() const { return *(BaseWatcher const*)TargetWatcherBytes; }

    // `[[this+4]+8] ? -8 : 0` -- the own ship (Owner->Watcher's object), or null. Retail
    // dereferences it untested at most sites; so do the callers.
    IObjRW* OwnShip() const;
    // `[this+8] ? -8 : 0` -- the current target, or null.
    IObjRW* CurrentTarget() const;

    // formerly RandomizeSystemManagerGunGroupJitter(GunGroups, SlotMinus2) -- FUN_062e9500
    // (item 1829), RET 4. Re-rolls Slot's BurstTime / NoBurstTime / FireInterval from the
    // tunables for its MunitionKind (cruise disruptors take MissileParams.AntiCruiseMissileIntervalTime), then
    // Phase = FireInterval * rand - FireInterval.
    void RandomizeSlotTiming(FGunGroupSlot* Slot);

    // formerly PopulateGunGroupHardpointRecords -- FUN_062e9770 (item 1830), RET 0x10. Copies
    // up to MaxCount ids (stopping at SUBOBJ_ID_NONE) into Slots, then classifies each slot
    // (MunitionKind, bAutoTurret, bSeekerMunition) and switches inactive ones on for a
    // non-player ship. Returns the count.
    int PopulateSlots(unsigned short const* SourceIds, int MaxCount, FGunGroupSlot* Slots, bool bMineDroppers);

    // formerly ResolveSystemManagerGunGroupHardpoints -- FUN_062e9990 (item 1831). Fills Guns
    // and MineDroppers from the own ship's equipment; false only without an own ship.
    bool ResolveHardpoints();

    // formerly AnnounceSystemManagerGunGroupHardpoint -- FUN_062e9a60 (item 1832), RET 4. The
    // own ship's first shield (find_equipment(&Id, 1, EquipTypeShield)) is switched on or off
    // (IObjAffect slot_4c(&Id, Mode != 0, -1)). False only without an own ship.
    bool SetShieldActive(int Mode);

    // formerly RetargetSystemManagerGunGroups -- FUN_062e9ae0 (item 1833), RET 4. Watches
    // NewTarget, tells the own ship (set_target) and, for a non-player ship, points every gun
    // at it (slot_44). False for a null NewTarget.
    bool Retarget(IObjRW* NewTarget);

    // formerly ClearSystemManagerGunGroupTargets -- FUN_062e9c00 (item 1834): every gun's
    // target cleared (slot_44 with null).
    void ClearGunTargets();

    // formerly ScanGunGroupTargets -- FUN_062e9c50 (item 1835), RET 4. Once per ScanPeriod each
    // auto-turret is pointed at the nearest pointable hostile in 1.2 * GunRange; then
    // Targeting.SetAttackState(0 if a hostile was seen, else 2) unless the own type is in 0x7f0000.
    void ScanAutoTurretTargets(float Dt);

    // formerly UpdateSystemManagerGunGroupTimers -- FUN_062ea220 (item 1837), RET 4. Ages the
    // line-of-fire cache, CruiseHeldTime and every slot's burst phase.
    void UpdateTimers(float Dt);

    // formerly FireReadyGunGroupHardpoints -- FUN_062ea050 (item 1836). Every auto-turret in
    // its fire tick (or all of them under the fire-guns damage reaction) fires at its own
    // gun's target when CanFireLauncherAtTarget answers 1 and it points there within pi/60.
    bool FireAutoTurrets();

    // formerly CheckLauncherLineOfFireCached -- FUN_062ea450 (item 1838), RET 8. A cached
    // (per target id and hardpoint) answer to "is a ship between this launcher and Target";
    // a stale record is recomputed through PhySys::FindRayCollisions.
    bool IsLineOfFireBlocked(IObjRW* Target, unsigned short HardpointId);

    // FUN_062ea770 (item 1839), RET 0xc. 1 = can fire; 5 = out of RangeThreshold; 7 = line of
    // fire blocked; 2 / 0 / 4 / 3 from the launcher's EquipStatus; 2 without target or ship.
    int CanFireLauncherAtTarget(unsigned short HardpointId, IObjRW* Target, float RangeThreshold);

    // FUN_062ea960 (item 1840), RET 0x18. The lead position on Target (UseDirectAim: the own
    // ship's lead-fire position for SubtargetId or for the ship), scattered inside a random
    // cone of SpreadAngle shaped by gun_fire_accuracy_power(_npc). UnusedSubobjSlot is pushed
    // by both callers and never read.
    void ComputeLeadFirePosition(Vector* OutPos, IObjRW* Target, unsigned short UnusedSubobjSlot,
                                 unsigned char UseDirectAim, float SpreadAngle, unsigned short SubtargetId);

    // formerly UpdateGunGroupFireState -- FUN_062eac70 (item 1841), RET 4 (argument unread).
    // Fires every plain, non-turret gun in its fire tick (or overridden) at the target's
    // refreshed aim point; returns the last CanFireLauncherAtTarget result (8 when
    // Targeting.SelectionPriority bit 0 is clear, 0 without ship or target).
    int UpdateGunFireState();

    // formerly UpdateSecondaryGunFireState -- FUN_062eb070 (item 1843), RET 4 (argument
    // unread). Launches seekers, HpType-0xa munitions and cruise disruptors under their gates.
    int UpdateMissileFireState();

    // formerly UpdateNearFieldGunFireState -- FUN_062eb390 (item 1844), RET 4 (argument
    // unread). Drops mines while the ship moves faster than 20 and either the drop-mines damage
    // reaction is on or a hostile follows inside the mine cone and range.
    unsigned UpdateMineDropState();

    // formerly ResetSecondaryWeaponSlotAimState -- FUN_062eb550 (item 1845). Fires every mine
    // dropper once (IObjAffect slot_34(Id, 1, null, -1)).
    void DropAllMines();

    // formerly ComputeWeaponSlotStat -- FUN_062eb5a0 (item 1846), RET 8. Average and longest
    // EquipValRange over the plain guns; false when there is none.
    bool ComputeGunRangeStats(float* OutAverage, float* OutMax);

    // formerly RefreshWeaponFireControlConfig -- FUN_062eb6c0 (item 1848). Copies the
    // tunables from the owner's Personality -- or, for a non-player ship following a formation
    // leader that has a behaviour manager, from the leader's -- and re-rolls every slot.
    bool RefreshFromPersonality();

    // FUN_062eb910 (item 1849). True when some gun is a plain, seeker or HpType-0xa launcher
    // that is neither destroyed nor disabled.
    bool HasReadyWeaponSlot();

    // formerly RunWeaponFireControlTick -- FUN_062eb670 (item 1847), RET 0xc (two arguments
    // unread). Retarget(Target), then the gun, missile and mine passes.
    bool RunFireControlTick(IObjRW* Target);
};
static_assert(sizeof(SystemManagerGunGroupManager) == 0x1238,
    "SystemManagerGunGroupManager must match retail's 0x1238-byte sub-object (SystemManager+0x12C)");
static_assert(offsetof(SystemManagerGunGroupManager, TargetWatcherBytes) == 0x08, "GunGroups+0x08 TargetWatcher");
static_assert(offsetof(SystemManagerGunGroupManager, Field_0x10) == 0x10, "GunGroups+0x10");
static_assert(offsetof(SystemManagerGunGroupManager, AimOffset) == 0x14, "GunGroups+0x14 AimOffset");
static_assert(offsetof(SystemManagerGunGroupManager, AimRefreshTimer) == 0x20, "GunGroups+0x20 AimRefreshTimer");
static_assert(offsetof(SystemManagerGunGroupManager, CruiseHeldTime) == 0x24, "GunGroups+0x24 CruiseHeldTime");
static_assert(offsetof(SystemManagerGunGroupManager, ScanTimer) == 0x28, "GunGroups+0x28 ScanTimer");
static_assert(offsetof(SystemManagerGunGroupManager, ScanPeriod) == 0x2C, "GunGroups+0x2C ScanPeriod");
static_assert(offsetof(SystemManagerGunGroupManager, Guns) == 0x30, "GunGroups+0x30 Guns");
static_assert(offsetof(SystemManagerGunGroupManager, GunCount) == 0x830, "GunGroups+0x830 GunCount");
static_assert(offsetof(SystemManagerGunGroupManager, Field_0x834) == 0x834, "GunGroups+0x834");
static_assert(offsetof(SystemManagerGunGroupManager, MineDroppers) == 0x838, "GunGroups+0x838 MineDroppers");
static_assert(offsetof(SystemManagerGunGroupManager, MineDropperCount) == 0x1038, "GunGroups+0x1038 MineDropperCount");
static_assert(offsetof(SystemManagerGunGroupManager, GunFireIntervalTime) == 0x103C, "GunGroups+0x103C");
static_assert(offsetof(SystemManagerGunGroupManager, MissileLaunchRange) == 0x106C, "GunGroups+0x106C");
static_assert(offsetof(SystemManagerGunGroupManager, MineLaunchConeAngle) == 0x1078, "GunGroups+0x1078");
static_assert(offsetof(SystemManagerGunGroupManager, GunFireAccuracyConeAngle) == 0x1080, "GunGroups+0x1080");
static_assert(offsetof(SystemManagerGunGroupManager, GunRange) == 0x1084, "GunGroups+0x1084 GunRange");
static_assert(offsetof(SystemManagerGunGroupManager, bFireAllGuns) == 0x1088, "GunGroups+0x1088 bFireAllGuns");
static_assert(offsetof(SystemManagerGunGroupManager, WideConeAngle) == 0x1090, "GunGroups+0x1090 WideConeAngle");
static_assert(offsetof(SystemManagerGunGroupManager, bEnabled) == 0x1094, "GunGroups+0x1094 bEnabled");
static_assert(offsetof(SystemManagerGunGroupManager, LineOfFireCache) == 0x1098, "GunGroups+0x1098 LineOfFireCache");
static_assert(offsetof(SystemManagerGunGroupManager, CmDroppers) == 0x1218, "GunGroups+0x1218 CmDroppers");

// FUN_062e9330 (item 1828): the gun groups' constructor, run by SystemManager's ctor.
void ConstructSystemManagerGunGroupManager(SystemManagerGunGroupManager* Self);

// ---------------------------------------------------------------------------
// RepairManager / PotionHelper / HullHelper / ShieldHelper -- SystemManager+0x14A8,
// 0x54 bytes. RTTI-attested retail classes (2026-09-24, claude-code:w3-behaviormgr-sys).
//
// This range used to be modelled as an opaque 0x50-byte "SystemManagerTaggedFactorComplex"
// with three "unattested" vtables and a GetFactor hook whose internal call was claimed to be
// "provably unreachable". Every part of that was wrong, and the RTTI settles it. Each vtable
// is preceded by its RTTICompleteObjectLocator (Common.dll.txt 564278-564298):
//   .rdata:063a07b4 -> COL 063a9d94 -> RepairManager::RTTI_Type_Descriptor, base
//                     IObjInspect::CargoEnumerator (063a9da0/063a9d48)
//                     vtable 063a07b8 = { FUN_062ecf60 }                       -- enumerate
//   .rdata:063a07bc -> COL 063a9d34 -> ShieldHelper (base PotionHelper)
//                     vtable 063a07c0 = { LAB_062e6430, LAB_062e6440 }
//   .rdata:063a07c8 -> COL 063a9cd4 -> HullHelper   (base PotionHelper)
//                     vtable 063a07cc = { LAB_062e6400, FUN_062e6410 }
//   .rdata:063a07d4 -> COL           -> PotionHelper, vtable 063a07d8 = { _purecall, _purecall }
// The three "vtables" are therefore two-slot tables; nothing reaches past slot 1. The old
// "self-vtable call at +0x40/+0x48" is a call on the ARGUMENT, not on `this`:
// FUN_062e6410 is `MOV ECX,[ESP+param_1]; ... CALL [EAX+0x40]`, i.e.
// Object->get_status(float&) (IObjInspect slot 16), and LAB_062e6440 is
// Object->get_shield_status(float&, bool&) (slot 18).
//
// What the family does: RepairManager is the AI's nanobot / shield-battery user. Every three
// seconds SystemManager's tick (FUN_062e66c0) zeroes both helpers' Count and hands the manager
// to IObjInspect::enumerate_cargo; enumerate() tallies cargo whose archetype class is
// RepairKit (0x1a) into Hull and ShieldBattery (0x1b) into Shield, remembering the equipment
// id. Each tick the two helpers then run PotionHelper::Update (FUN_062eceb0): when the health
// ratio GetRatio() returns falls to UseAtRatio or below, wait PreUseDelay, use the item
// through IObjAffect slot 0x58 (EquipId, 1, -1), then wait PostUseDelay before re-arming.
// The three thresholds of each helper are refreshed from Personality::RepairParams by
// RefreshRepairManagerFromPersonality (FUN_062ecfb0).
//
// Layout, from SystemManager's ctor (.text:062e62fc-062e6394) and FUN_062e6490's
// initialiser (.text:062e667f-062e6688):
//   +0x00 vptr (063a07b8)
//   +0x04 SystemManager* Owner     -- written only by FUN_062e6490
//   +0x08 HullHelper   Hull        -- 0x24 bytes
//   +0x2C ShieldHelper Shield      -- 0x24 bytes
//   +0x50 float CheckTimer         -- SystemManager+0x14F8; zeroed by FUN_062e6490, advanced
//                                     by FUN_062e66c0. NOT written by SystemManager's ctor.
// ---------------------------------------------------------------------------

// PotionHelper -- abstract base (RTTI .?AUPotionHelper@@), vtable 063a07d8 = two _purecall
// slots. No virtual destructor: retail's table has exactly the two slots below.
struct PotionHelper
{
    // Retail's ctor stores, in the order they appear at .text:062e62fc-062e6321 (Hull) and
    // .text:062e6352-062e6384 (Shield): State = 0, EquipId = SUBOBJ_ID_NONE, Count = 0,
    // Timer = 0, PreUseDelay = 2.0f, PostUseDelay = 5.0f, UseAtRatio = 0.35f. Type is not
    // written by the ctor (only slot 0 writes it); the port zeroes it for determinism.
    PotionHelper();

    // Slot 0 (+0x00). HullHelper: LAB_062e6400 `MOV [ECX+8],0x1a; RET`; ShieldHelper:
    // LAB_062e6430 `MOV [ECX+8],0x1b; RET`. No caller in Common.dll dispatches this slot.
    virtual void SetType() = 0;
    // Slot 1 (+0x04). `RET 4`, returns the float in ST0: the object's current health ratio
    // for the resource this helper restores.
    virtual float GetRatio(IObjRW const* Object) = 0;

    // FUN_062eceb0 (item 1878), non-virtual, `RET 8`: (float Dt, IObjRW* Object).
    void Update(float Dt, IObjRW* Object);

    int State;                  // +0x04  0 = armed, 1 = waiting PreUseDelay, 2 = waiting PostUseDelay
    int Type;                   // +0x08  Archetype class type (0x1a / 0x1b), written by SetType
    unsigned short EquipId;     // +0x0C  id of the last matching cargo item enumerate() saw
    unsigned char Pad0E[2];
    int Count;                  // +0x10  matching cargo count, re-tallied every CheckTimer period
    float PreUseDelay;          // +0x14  state 1 -> use once Timer >= this
    float PostUseDelay;         // +0x18  state 2 -> 0 once Timer >= this
    float UseAtRatio;           // +0x1C  state 0 -> 1 once GetRatio() <= this
    float Timer;                // +0x20
};
static_assert(sizeof(PotionHelper) == 0x24, "PotionHelper sizeof 0x24 (Shield at RepairManager+0x2C)");
static_assert(offsetof(PotionHelper, State) == 0x04, "PotionHelper+0x04 State");
static_assert(offsetof(PotionHelper, Type) == 0x08, "PotionHelper+0x08 Type");
static_assert(offsetof(PotionHelper, EquipId) == 0x0C, "PotionHelper+0x0C EquipId");
static_assert(offsetof(PotionHelper, Count) == 0x10, "PotionHelper+0x10 Count");
static_assert(offsetof(PotionHelper, PreUseDelay) == 0x14, "PotionHelper+0x14 PreUseDelay");
static_assert(offsetof(PotionHelper, PostUseDelay) == 0x18, "PotionHelper+0x18 PostUseDelay");
static_assert(offsetof(PotionHelper, UseAtRatio) == 0x1C, "PotionHelper+0x1C UseAtRatio");
static_assert(offsetof(PotionHelper, Timer) == 0x20, "PotionHelper+0x20 Timer");

// HullHelper (RTTI .?AUHullHelper@@), vtable 063a07cc.
struct HullHelper : PotionHelper
{
    void SetType() override;                             // LAB_062e6400 (item 1780)
    float GetRatio(IObjRW const* Object) override;       // FUN_062e6410 (item 1781)
};

// ShieldHelper (RTTI .?AUShieldHelper@@), vtable 063a07c0.
struct ShieldHelper : PotionHelper
{
    void SetType() override;                             // LAB_062e6430 (item 1782)
    float GetRatio(IObjRW const* Object) override;       // LAB_062e6440 (item 1783)
};

// RepairManager (RTTI .?AURepairManager@@), vtable 063a07b8, base IObjInspect::CargoEnumerator.
struct RepairManager : IObjInspect::CargoEnumerator
{
    // Retail's ctor installs the vptr and builds the two helpers; Owner and CheckTimer are
    // left alone (FUN_062e6490 writes them). The port zeroes both for determinism.
    RepairManager();

    // FUN_062ecf60 (item 1879), the single slot. `RET 0xc`, returns AL = 1 on every path.
    bool enumerate(unsigned short const& Id, Archetype::Equipment const* Arch, int Count) override;

    SystemManager* Owner;       // +0x04
    HullHelper Hull;            // +0x08
    ShieldHelper Shield;        // +0x2C
    float CheckTimer;           // +0x50
};
static_assert(sizeof(RepairManager) == 0x54, "RepairManager sizeof 0x54 (SystemManager+0x14A8..+0x14FB)");
static_assert(offsetof(RepairManager, Owner) == 0x04, "RepairManager+0x04 Owner");
static_assert(offsetof(RepairManager, Hull) == 0x08, "RepairManager+0x08 Hull");
static_assert(offsetof(RepairManager, Shield) == 0x2C, "RepairManager+0x2C Shield");
static_assert(offsetof(RepairManager, CheckTimer) == 0x50, "RepairManager+0x50 CheckTimer");

// FUN_062ecfb0, item 1880. Raw disasm Common.dll.txt 253015-253078 (57 insns), `this` == the
// RepairManager at both call sites (.text:062e68b6 and .text:062e696c). Re-seeds both helpers'
// PreUseDelay / PostUseDelay / UseAtRatio from a pub::AI::Personality's RepairParams --
// normally the owning SystemManager's own, but, when the owner is not player-controlled, its
// FormationParams.bLeaderMakesMeTougher byte is set, GunConvergenceStateBytes[0xC5] is clear and the
// watched object at GunConvergenceStateBytes+0x0C resolves to a CEqObj with a behavior
// manager, that object's SystemManager instead (and the owner's Unknown14 latch is set).
// Always returns true, matching retail's unconditional `MOV AL,1`.
bool RefreshRepairManagerFromPersonality(RepairManager* Self);

// ---------------------------------------------------------------------------
// SystemManager+0x1364, the 0xC8-byte formation / follow-leader record (its ctor is
// FUN_062ebbc0). Every function on it is reached with ECX = Sys + 0x1364; definitions in
// common_gunconvergence.cpp unless noted.
//
// Its destructor is FUN_062d27d0 (item 1546): retail reaches it from the SystemManager
// ctor's EH unwind funclets 06395097 / 06395385 (`ADD ECX,0x1364; JMP FUN_062d27d0`), and
// IBehaviorManager's dtor inlines the same two steps (.text:062d25e2-062d2603): the
// BaseWatcher at +0x14, then the one at +0x0C -- the members' reverse declaration order.
// ---------------------------------------------------------------------------
struct FormationFollowerEnvelope; // common_gotobehavior.h

struct FGunConvergenceStateBlock
{
    // +0x00: += Dt every Update while bForceAttackFormation; zeroed once the unrounded sum
    // exceeds FormationParams.ForceAttackFormationActiveTime + ...UnactiveTime. Gate A
    // compares it with the active time.
    float ForceAttackCycleTimer;
    // +0x04: += Dt every Update while bBreakingFormation, else 0. Gates A / B compare it
    // with FormationParams.FormationExitMaxTime.
    float BreakFormationTimer;
    // +0x08: the owning SystemManager (InitSystemManager, .text:062e6621).
    SystemManager* Owner;
    BaseWatcher FormationLeaderWatcher;   // +0x0C SetFormationLeader
    BaseWatcher FollowLeaderWatcher;      // +0x14 SetFollowLeader
    // +0x1C: get_formation_followers' 32-entry out-array (IsIdAmongOwnerFormationFollowers,
    // ComputeFormationFollowerEnvelope).
    IObjRW* FollowerScratch[0x20];
    // +0x9C / +0xA0: the two alignment-angle thresholds (radians) of
    // ComputeFormationFollowerEnvelope (.text:062ec3b3 / 062ec3ce); ctor 0.1f / 0.2f.
    float FormationNarrowAngle;
    float FormationWideAngle;
    float Field_0xA4;                     // +0xA4 ctor 200.0f; no reader in Common.dll. TODO: meaning
    // +0xA8..+0xB8, +0xBD, +0xC2: the order SetDirectiveOrder stores and GetDirectiveOrder
    // copies out (BaseFollowerBehavior::Slot7's FollowerDirectiveOrder).
    unsigned OrderObjectId;               // +0xA8 the dock candidate's id
    unsigned OrderSecondObjectId;         // +0xAC a second object's id
    unsigned ValueB0;                     // +0xB0 SetValueB0 / TryGetValueB0
    unsigned ValueB4;                     // +0xB4 SetValueB4 / TryGetValueB4
    unsigned OrderLaneParam;              // +0xB8 float bits, go_tradelane's 4th argument
    unsigned char Field_0xBC;             // +0xBC only ever cleared (with +0xBD..+0xC4). TODO: meaning
    unsigned char bOrderPending;          // +0xBD; Update clears it once the leader leaves the lane
    unsigned char bValueB0Valid;          // +0xBE
    unsigned char bValueB4Valid;          // +0xBF
    unsigned char bBreakingFormation;     // +0xC0 drives BreakFormationTimer
    unsigned char bLeaderIdleFormation;   // +0xC1 SyncFormationLeader: the leader's directive is {1, 0}
    unsigned char bOrderCommitDirectly;   // +0xC2
    unsigned char bForceAttackFormation;  // +0xC3 SetFlagC3; JobParams.bForceAttackFormation copies
    unsigned char bBreakApartNotified;    // +0xC4 Update's one-shot leader notification latch
    unsigned char bLeaderSuppressed;      // +0xC5 SuppressCurrentLeader; hides both leaders
    unsigned char bReleaseRequested;      // +0xC6 RequestRelease
    unsigned char Pad_0xC7;

    // FUN_062ebbc0 (item 1853), formerly the free ConstructGunConvergenceState(void*).
    FGunConvergenceStateBlock();
    ~FGunConvergenceStateBlock();

    // FUN_062ebcb0 (item 1854), RET 4, formerly the free UpdateGunConvergenceState(void*, Dt);
    // called from UpdateSystemManager.
    void Update(float Dt);

    // FUN_062ec870 / FUN_062ec970 / FUN_062ec6b0, formerly free functions over `void* Self`.
    void SetFormationLeader(IObjRW* Target);
    void SetFollowLeader(IObjRW* Target);
    void SyncFormationLeader();

    // formerly FUN_062ecaf0 (item 1869), RET 0, every caller `ECX = Sys + 0x1364`. Null while
    // bLeaderSuppressed; otherwise the follow leader unless its watcher holds 0 or 8, else
    // the formation leader (Watchable - 8 = the IObjRW; 0 stays null).
    IObjRW* GetCurrentLeader() const;

    // formerly FUN_062ec6a0, `MOV byte ptr [ECX+0xc5],1; RET` (caller IBehaviorManager::Slot00,
    // .text:062d42e2): sets the +0xC5 gate GetCurrentLeader tests; SetFormationLeader /
    // SetFollowLeader clear it again.
    void SuppressCurrentLeader();

    // The accessors below are thiscall on this block at every retail call site (`ADD ECX,0x1364`
    // or `LEA ECX,[Sys+0x1364]` on the owning SystemManager). Definitions:
    // common_gunconvergence.cpp. +0x08 is the owning SystemManager, +0x0C the formation-leader
    // watcher (SetFormationLeader), +0x14 the follow-leader watcher (SetFollowLeader).

    // FUN_062ec520 (item 1856), RET 4: is WantedId among the owning ship's formation followers
    // (fetched into this block's 32-entry scratch array at +0x1C).
    bool IsIdAmongOwnerFormationFollowers(unsigned WantedId);

    // formerly FUN_062ec590 (item 1857), RET 8: +0xB4 = Value, +0xBF = Flag (stored as given).
    void SetValueB4(unsigned int Value, unsigned char Flag);

    // formerly FUN_062ec5b0 (item 1858), RET 4: when +0xBF is set, *Out = +0xB4; returns +0xBF.
    unsigned char TryGetValueB4(unsigned int* Out) const;

    // formerly FUN_062ec5d0 (item 1859), RET 0x14: +0xBD = P1, +0xA8 = P2, +0xAC = P3,
    // +0xC2 = P4, +0xB8 = P5 -- the order block GetDirectiveOrder copies out.
    void SetDirectiveOrder(unsigned char P1, unsigned int P2, unsigned int P3, unsigned char P4,
                           unsigned int P5);

    // formerly FUN_062ec610 (item 1860), RET 4: copies +0xA8 / +0xAC / +0xBD / +0xC2 / +0xB8 to
    // Out +0x00 / +0x04 / +0x08 / +0x09 / +0x0C (a 0x10-byte block).
    void GetDirectiveOrder(unsigned char* Out) const;

    // formerly FUN_062ec650 (item 1861), RET 4: +0xB0 = Value, +0xBE = (Value != 0).
    void SetValueB0(unsigned int Value);

    // formerly CmnBehavior::FollowerDirectiveRecord_TryGetPointerB0 / FUN_062ec680 (item 1862),
    // RET 4: when +0xBE is set, *Out = +0xB0; returns +0xBE.
    bool TryGetValueB0(unsigned int* Out) const;

    // formerly CmnBehavior::FollowerDirectiveRecord_DefaultTarget / FUN_062ecab0 (item 1867):
    // null while +0xC5 is set, else the formation leader (+0x0C watcher's object).
    IObjRW* GetFormationLeader() const;

    // formerly CmnBehavior::FollowerDirectiveRecord_OverrideTarget / FUN_062ecad0 (item 1868):
    // null while +0xC5 is set, else the follow leader (+0x14 watcher's object).
    IObjRW* GetFollowLeader() const;

    // FUN_062ecb20 (item 1870) and FUN_062ecc00 (item 1871), RET 0, return AL. Retail meaning
    // unknown; the predicates are written out at the definitions.
    bool FollowerDirectiveGateA() const;
    bool FollowerDirectiveGateB() const;

    // formerly FollowerDirectiveRecord_SetFlagC3 / FUN_062ecc50 (item 1872), RET 4: +0xC3 = Value.
    void SetFlagC3(unsigned char Value);

    // FUN_062ecc60 (item 1873, RET 4) / FUN_062ecce0 (item 1874, RET 0): the owning ship's
    // formation leader, when there is one, gets QueueNotification(Leader, 1, 0) and then
    // (Leader, 2, Delay) when Delay > 0 / only (Leader, 2, 0) from the owning IBehaviorManager.
    void NotifyFormationLeader1(float Delay);
    void NotifyFormationLeader2();

    // formerly CmnBehavior::FollowerDirectiveRecord_RequestRelease / FUN_062ecd30 (item 1875),
    // RET 0: +0xC6 = 1, then the owning ship's IObjAffect slot 0x60 (1, &ExternalIface id, -1).
    int RequestRelease();

    // formerly the free function ComputeFormationFollowerEnvelope(unsigned char*, ...) /
    // FUN_062ebf50 (item 1855), RET 4, `ECX = Sys + 0x1364` at all three call sites
    // (062c4806, 062d9d35, 062fe2bd). Folds every formation follower of the owning ship into
    // one speed / throttle / angular envelope; false (leaving *Out as written so far) when the
    // owner has no watched object or no followers. Definition: common_gotobehavior.cpp.
    bool ComputeFormationFollowerEnvelope(FormationFollowerEnvelope* Out);
};
static_assert(sizeof(FGunConvergenceStateBlock) == 0xC8, "FGunConvergenceStateBlock sizeof 0xC8");
static_assert(offsetof(FGunConvergenceStateBlock, Owner) == 0x08, "FGunConvergenceStateBlock+0x08 Owner");
static_assert(offsetof(FGunConvergenceStateBlock, FormationLeaderWatcher) == 0x0C, "FGunConvergenceStateBlock+0x0C");
static_assert(offsetof(FGunConvergenceStateBlock, FollowLeaderWatcher) == 0x14, "FGunConvergenceStateBlock+0x14");
static_assert(offsetof(FGunConvergenceStateBlock, FollowerScratch) == 0x1C, "FGunConvergenceStateBlock+0x1C");
static_assert(offsetof(FGunConvergenceStateBlock, FormationNarrowAngle) == 0x9C, "FGunConvergenceStateBlock+0x9C");
static_assert(offsetof(FGunConvergenceStateBlock, OrderObjectId) == 0xA8, "FGunConvergenceStateBlock+0xA8");
static_assert(offsetof(FGunConvergenceStateBlock, OrderLaneParam) == 0xB8, "FGunConvergenceStateBlock+0xB8");
static_assert(offsetof(FGunConvergenceStateBlock, bOrderPending) == 0xBD, "FGunConvergenceStateBlock+0xBD");
static_assert(offsetof(FGunConvergenceStateBlock, bForceAttackFormation) == 0xC3, "FGunConvergenceStateBlock+0xC3");
static_assert(offsetof(FGunConvergenceStateBlock, bReleaseRequested) == 0xC6, "FGunConvergenceStateBlock+0xC6");

// ---------------------------------------------------------------------------
// SystemManager itself.
// ---------------------------------------------------------------------------
class SystemManager
{
public:
    SystemManager();
    ~SystemManager();

    int Unknown0;                                    // +0x00 never written by the ctor
    // +0x04: back-pointer to the owning IBehaviorManager. Zeroed by this
    // ctor, set for real by FUN_062e6490 (item 1785) right after
    // construction -- see this class's own header comment above for the
    // full derivation (2026-09-11, item 1827 investigation).
    IBehaviorManager* Owner;                           // +0x04
    BaseWatcher Watcher;                               // +0x08
    // +0x10: the object database the owner is connected to -- written only by
    // SetSystemManagerSharedListHead (FUN_062e68e0, .text:062e68e6), which
    // ConnectBehaviorManagerObjectDb hands the manager's own IObjDB. Read by
    // TargetManager::GetScanResponse (FUN_062eeb70, .text:062eeb73-062eeb80) as
    // `[Owner+0x10]` and dispatched through IObjDB +0x04 (FindById). Retyped from
    // `int Unknown10` 2026-09-24; same four bytes.
    IObjDB* ObjectDb;                                  // +0x10
    unsigned char Unknown14;                           // +0x14
    unsigned char Pad15[3];
    // +0x18..+0x34: the embedded ThrustManager (see that struct above for the
    // full derivation). CORRECTED 2026-09-17 (claude:common-thrustmanager):
    // this range used to be modelled as `SystemManager* ArmedSelf` plus an
    // 0x18-byte `Opaque1C` blob. The dword at +0x18 really is
    // ThrustManager::Owner -- which is why FUN_062e6490 stores `this` into it
    // -- and the rest of the "opaque" gap is that class's own MaxThrottle /
    // AxisThrottle / five flag bytes. Byte-for-byte identical to the old
    // model; only the naming changed. The old `ArmedSelf != nullptr` test
    // survives as `Thrust.Owner != nullptr`, which is still exactly retail's
    // "has this ThrustManager been initialised yet" question.
    ThrustManager Thrust;                              // +0x18

    SystemManagerTimerTable Timers;                    // +0x34
    SystemManagerGunGroupManager GunGroups;            // +0x12C
    FGunConvergenceStateBlock GunConvergenceStateBytes; // +0x1364 (ctor FUN_062ebbc0)
    TargetManager Targeting;                           // +0x142C (FUN_062ed570, common_iface.h)
    // +0x1498..+0x14A7: a small sub-object LootBehavior hands to FUN_062ece40 as
    // Sys+0x1498. Its first dword is a SystemManager back-pointer written only by
    // FUN_062e6490 (.text:062e6641), +0x149C is a BaseWatcher (the ctor zeroes it at
    // .text:062e62dc-062e62de; the owning IBehaviorManager's dtor clears it first, at
    // .text:062d25dd), and +0x14A4 is an equipment id: the ctor seeds it with
    // SUBOBJ_ID_NONE (.text:062e62f8) and FUN_062e6490 fills it through
    // find_equipment(&Id, 1, 0x100000) (.text:062e6657-062e6665). Typed 2026-09-24 from
    // two ints ("Unknown149C/14A0") -- same bytes.
    SystemManager* Owner1498;                          // +0x1498 not written by the ctor
    BaseWatcher Watcher149C;                           // +0x149C
    unsigned short Unknown14A4;                        // +0x14A4
    unsigned char Pad14A6[2];

    // +0x14A8..+0x14FB: RepairManager (see above). Its +0x50 CheckTimer is
    // SystemManager+0x14F8, which the old model counted as the first dword of
    // "Opaque14F8[0x10]".
    RepairManager Repair;                              // +0x14A8

    // +0x14FC..+0x1583: the own ship's FAiTarget record. Every intercept-clearance
    // helper takes `Sys + 0x14fc` as its Elem, and the inlined refreshes address it as
    // [ESI+0xc]/[ESI+0x18]/[ESI+0x7e]/[ESI+0x84] off ESI = Sys+0x14fc (e.g. FUN_062e6d40,
    // .text:062e6daa-062e6dce). InitSystemManager (.text:062e6507) and the tick
    // (.text:062e66fa-062e6718) store the owner's object into AttitudeRefIface and Iface
    // and clear the four guards; the ctor zeroes only the guards and the two pointers
    // (.text:062e63a9-062e63c7).
    FAiTarget SelfTarget;                              // +0x14FC

    pub::AI::Personality PersonalityData;               // +0x1584
};

static_assert(sizeof(SystemManager) == 0x195C,
    "SystemManager must match retail's 0x195C-byte size (operator_new(0x195c) at the sole call site, FUN_062d2220)");
static_assert(offsetof(SystemManager, Owner) == 0x04, "SystemManager+0x04 Owner");
static_assert(offsetof(SystemManager, Watcher) == 0x08, "SystemManager+0x08 Watcher");
static_assert(offsetof(SystemManager, ObjectDb) == 0x10, "SystemManager+0x10 ObjectDb");
static_assert(offsetof(SystemManager, Thrust) == 0x18, "SystemManager+0x18 ThrustManager");
static_assert(offsetof(SystemManager, Timers) == 0x34, "SystemManager+0x34 Timers");
static_assert(offsetof(SystemManager, GunGroups) == 0x12C, "SystemManager+0x12C GunGroups");
static_assert(offsetof(SystemManager, GunConvergenceStateBytes) == 0x1364, "SystemManager+0x1364 GunConvergenceStateBytes");
static_assert(offsetof(SystemManager, Targeting) == 0x142C, "SystemManager+0x142C Targeting");
static_assert(offsetof(SystemManager, Owner1498) == 0x1498, "SystemManager+0x1498 Owner1498");
static_assert(offsetof(SystemManager, Watcher149C) == 0x149C, "SystemManager+0x149C Watcher149C");
static_assert(offsetof(SystemManager, Unknown14A4) == 0x14A4, "SystemManager+0x14A4 Unknown14A4");
static_assert(offsetof(SystemManager, Repair) == 0x14A8, "SystemManager+0x14A8 RepairManager");
static_assert(offsetof(SystemManager, SelfTarget) == 0x14FC, "SystemManager+0x14FC SelfTarget");
static_assert(offsetof(SystemManager, PersonalityData) == 0x1584, "SystemManager+0x1584 PersonalityData");

// 2026-09-11 d250:common-port -- SystemManager-scoped functions from the
// 0x062e6260-0x062ea450 blocked-item pass, ported now that the class layout
// above is real (superseding the stale "whole-class-deferral" blocker note
// still quoted on several sibling items in the decomp tracker). See
// common_systemmanager.cpp for each one's full raw-disasm derivation.

// FUN_062e68e0, item 1787. Stores one shared list-head pointer into two
// slots: Self->ObjectDb (.text:062e68e6) and Timers::ObjectDb
// (.text:062e68ef). Its tail `LEA ECX,[EDX+0x142c]; JMP 0x062ed730` hands
// NewHead to a TargetManager member whose body is the ICF-folded `RET 4`
// (the same address Ghidra labels MetaList<GoodDesc>::operator=) -- it writes
// nothing. (Until 2026-09-24 the port copied NewHead over the first dword of
// SystemManager+0x142C, i.e. TargetManager's vfptr.) If Timers::Owner names a live SystemManager whose own Watcher
// has a target, and NewHead is non-null, it also empties the cached AI target
// list -- `Timers.bBuilt = 0; Timers.Targets.End = Timers.Targets.Begin;`.
// (That pair used to be described here as "a small render-cache pair"; see the
// MERGE NOTE on SystemManagerTimerTable for why that reading was wrong.)
void SetSystemManagerSharedListHead(SystemManager* Self, void* NewHead);

// FUN_062e6990, item 1789. Clears either of GunConvergenceStateBytes' two
// embedded BaseWatcher fields (common_aisteer.h ConstructGunConvergenceState:
// Self+0x0C / Self+0x14, i.e. SystemManager+0x1370 / +0x1378) if its
// currently-watched target's id matches TargetId.
void ClearGunConvergenceStateWatcherIfTarget(SystemManager* Self, int TargetId);

// FUN_062e92d0, item 1826, is ThrustManager::SetSteeringFallbackFlag (declared on
// the struct above). Its sole caller IBehaviorManager::update_current_behavior_auto_level
// computes ECX = Sys+0x18 because that IS the ThrustManager sub-object, and since
// 2026-09-24 calls the member directly; the SetSystemManagerOpaqueFlag0x2F wrapper that
// used to sit here is removed.

// FUN_062e92e0, item 1827, is ThrustManager::CheckSteeringGate: retail passes `Sys + 0x18`,
// the sub-object's own address. The CheckSystemManagerSteeringGate(Sys) wrapper that used to
// sit here is removed (CMN-S2 AI4); callers write Sys->Thrust.CheckSteeringGate().

// FUN_062e72d0, item 1796. `this` == &SystemManager::Timers (proven in
// ThrustManager's header comment above). Rebuilds all eleven
// SustainedThresholdTimer slots' TriggerThreshold/Duration pairs from the
// owning SystemManager's pub::AI::Personality, scaling every threshold by
// Timers::DecayRate. Returns retail's own unconditional `MOV AL,1`.
bool RefreshSystemManagerTimerThresholdsFromPersonality(SystemManagerTimerTable* Self);

// FUN_062e6950, item 1788. The "personality changed" notification
// IBehaviorManager::set_personality fires: refresh the weapon fire-control
// config, rebuild the timer thresholds above, refresh the RepairManager's
// thresholds (RefreshRepairManagerFromPersonality), then mirror one status byte off the gun-convergence
// state's watched object.
void NotifySystemManagerPersonalityChanged(SystemManager* Self);

// FUN_062e6d40, item 1794. `this` == &Timers. Walks the intrusive candidate
// list AppendSystemManagerTimerListEntry builds, garbage-collecting dead nodes
// as it goes, picks the nearest candidate that is (a) in front of the owner
// (positive dot of owner-to-candidate against the candidate's velocity),
// (b) inside Personality.MissileReaction.EvadeMissileDistance of the owner's cached
// physical centre, and (c) nearer than anything seen so far -- then leaves it
// in Timers::BestCandidateWatcher and writes a closing-rate estimate into
// Timers::BestValueSoFar (FLT_MAX when nothing qualified). `UnusedArg` is
// retail's own unread stack argument: the body is `RET 4` but nothing ever
// reads entry_esp+4 (verified by scanning every [ESP+0x40..0x5f] reference in
// Common.dll.txt 243032-243272; the only two are scratch LEAs). Its caller,
// item 1795, passes its own DeltaTime.
void UpdateSystemManagerCandidateScan(SystemManagerTimerTable* Self, float UnusedArg);

// FUN_062e7010, item 1795. `this` == &Timers. Runs the candidate scan above,
// advances all eleven SustainedThresholdTimer slots against Timers::Level, then
// decays Level by DecayRate * UnknownScale_0xD8 * DeltaTime, clamped at zero.
void UpdateSystemManagerTimerTable(SystemManagerTimerTable* Self, float DeltaTime);

// FUN_062e6490, item 1785. `RET 8`, thiscall on the SystemManager, called once from
// IBehaviorManager's ctor (.text:062d239c) right after SystemManager::SystemManager:
// (IObjRW* Object, IBehaviorManager* Manager). Binds Watcher to Object, stores Manager in
// Owner, caches the object's id in Unknown0, arms the timer table and -- only when Watcher
// then holds a live object -- initialises the ThrustManager, the gun groups, the
// gun-convergence state, the +0x1498 block (owner + the first equipment id of type mask
// 0x100000) and the RepairManager, and binds the
// hardpoint-id watcher at +0x1344. TargetManager::Attach runs unconditionally. Returns
// AL = 1 (the caller ignores it).
bool InitSystemManager(SystemManager* Self, IObjRW* Object, IBehaviorManager* Manager);

// FUN_062e66c0, item 1786. `RET 4`, thiscall on the SystemManager: its per-tick update,
// called from IBehaviorManager::UpdateBehaviorTick (.text:062d2b8d).
void UpdateSystemManager(SystemManager* Self, float Dt);
