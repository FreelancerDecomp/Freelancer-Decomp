// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll FaceIdea (ctor 0x062d6750, Update LAB_062d68b0, setters 0x062d7510-).
// See src/include/common_idea.h.
//
// CMN-SPLIT stage 1 (2026-10-14): split out of common_idea.cpp (ctor, SetContext, Commit,
// setters) and common_aisteer.cpp (Update), code unchanged.

#include "common_idea.h"
#include "common_idea_internal.h"
#include "common_aisteer.h"   // BearingAngleToPoint, GetIfacePositionOrZero
#include "common_aisteer_internal.h"
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
using namespace CmnAiSteer;

// ----------------------------------------------------------------------------
// FaceIdea
// ----------------------------------------------------------------------------

// FUN_062d6750, Common.dll.txt 218090-218143 (53 instructions, no Ghidra
// decompile). See common_idea.h's own class-level comment for the full
// field-by-field citation.
FaceIdea::FaceIdea()
    : BaseIdea()
    , Watcher()
    , Position1c{}
    , Direction28{}
    , ZeroBlock34{}
    , Vector58{}
    , Field64(0)
    , Opaque65{}
    , FacingTolerance(BitsToFloat(0x3dc90fdbu))
    // 062d67af-062d67eb: retail really does call FUN_062e5ba0(this->Object, 1.0f)
    // here and FSTP the result into +0x90 -- with Object still null, because the
    // BaseIdea sub-object zeroed it two instructions earlier. That helper's whole
    // body is a null guard plus one vtable call (see ProjectedAxisThrottleXY), so
    // the result is 0.0f and this is now REAL code rather than the
    // FULLPORT-DEFERRED 0.0f that used to stand here. Note the asymmetry with
    // SetThrottleScale(), which puts the raw argument in +0x90 and the derived
    // value in +0x94 -- the opposite way round. That is retail's own, not a
    // transcription slip; see the setter's declaration comment.
    , Field90(Behavior_InvokeManagerSlotF8(ObjInspectOf(Object), 1.0f))   // FUN_062e5ba0
    , TurnThrottle(1.0f)               // 0x3f800000
    , AngleWeight98(BitsToFloat(0x3f1c28f6u))
    , AngleWeight9c(BitsToFloat(0x3e490fdbu))
    , TargetKind(0)
    , OpaqueA4{}
    , RemainingAngle(3.14159274f)        // 0x40490fdb
    , PadAc(0)
    , bUpValid(0)     // NOT ctor-initialized in retail; zeroed here per this
                       // marathon's documented "safe over-init" convention.
    , PadAe{}
    , UpB0{}
    , ThresholdBc(BitsToFloat(0x3f7ae148u))
{
}

// Own vtable slot 0 override -- LAB_062d6820 (item 1602), Common.dll.txt
// 218148-218196, 46 insns, RET 0xc.
bool FaceIdea::SetContext(void* InObject, void* InHandle, void* InContext)
{
    // 062d6820-062d684b: the BaseIdea body inlined (null Object or Handle -> false; Handle,
    // Object, Context, Attached = 1).
    if (!BaseIdea::SetContext(InObject, InHandle, InContext))
        return false;

    IObjInspect const* const Obj = ObjInspectOf(Object);

    // 062d6834-062d6853: Rate = FUN_062e5b20(Object, 1.0f, 0.0f), `FSTP float`.
    float const Rate = Behavior_InvokeManagerSlotF0(Obj, 1.0f, 0.0f);

    // 062d6857-062d6870: AngleWeight98 = FUN_062e5950(Object, 0.0f, 0.0f, Rate, 1000.0f)
    // (DAT_063a0400 = 0x447a0000), `FSTP float`.
    AngleWeight98 = Behavior_GetAngularDistanceTravelled(Obj, 0.0f, 0.0f, Rate, 1000.0f);

    // 062d6876-062d6886: FUN_062e5b50(Object, 1.0f) -- the third push (0) is not read by the
    // callee; the ST0 result is dropped (`FSTP ST0`).
    (void)Behavior_InvokeManagerSlotF4(Obj, 1.0f);

    // 062d688e-062d6898: AngleWeight9c = 0x3e490fdb (0.19634955f, pi/16), bUpValid = 0.
    AngleWeight9c = BitsToFloat(0x3e490fdbu);
    bUpValid = 0;
    return true;
}

// Own vtable slot 1 override -- LAB_062d68b0 (item 1603), FaceIdea::Update, is defined
// at the end of this file; the steering helpers it shares with DodgeIdea::Update are in
// common_aisteer.cpp (common_aisteer_internal.h).

// Own vtable slot 4 override -- LAB_062d74e0, Common.dll.txt 219257-219267, 11
// instructions. Ported in full 2026-09-17; see common_idea.h's slot-4 section for
// why FUN_062e8c20's call shape here is no longer in doubt.
void FaceIdea::Commit()
{
    // 062d74e3-062d74fe: ECX = Handle + 0x18 (never tested), the pushed 12-byte stack
    // Vector zeroed.
    Vector ZeroVector{ 0.0f, 0.0f, 0.0f };
    Handle->Thrust.SetAxisThrottle(ZeroVector); // 062d7506
}

// ---------------------------------------------------------------------------
// FaceIdea's non-virtual setter family. Every one is RET 4 and every one stamps
// the same tag field, TargetKind -- see common_idea.h's "FaceIdea's NON-VIRTUAL
// setter family" block for the receiver proofs and the tag table.
// ---------------------------------------------------------------------------

// FUN_062d7510 (item 1606), Common.dll.txt 219277-219297, 19 instructions.
void FaceIdea::SetWatchObject(void const* Object2)
{
    if (Object2 == nullptr)
    {
        // LAB_062d7538-062d7559: tag 0, then the same dedup-guarded clear
        // (`CMP [ECX],0` before BaseWatcher::set_pointer) every BaseWatcher user
        // in this family writes by hand.
        TargetKind = 0;
        Watcher.set(nullptr);
        return;
    }

    // 062d7518-062d7533: tag 1, then watch Object2 + 8 -- the IObjRW -> Watchable
    // adjustment, identical to MoveIdea::SetWatchObject's.
    TargetKind = 1;
    Watcher.set(reinterpret_cast<Watchable const*>(
        static_cast<unsigned char const*>(Object2) + 8));
}

// FUN_062d7560 (item 1607), Common.dll.txt 219311-219320, 10 instructions.
void FaceIdea::SetPosition(Vector const& Point)
{
    TargetKind = 2;        // 062d7564
    Position1c = Point; // 062d756e-062d757e, three plain dword copies to [ECX+0x1c]
}

// FUN_062d7590 (item 1608), Common.dll.txt 219340-219349, 10 instructions. No
// renormalization, unlike MoveIdea::SetDirection -- the copy is the whole body.
void FaceIdea::SetDirection(Vector const& Dir)
{
    TargetKind = 3;         // 062d7594
    Direction28 = Dir;   // 062d759e-062d75ae, to [ECX+0x28]
}

// FUN_062d75c0 (item 1609), Common.dll.txt 219369-219378, 10 instructions.
void FaceIdea::SetBasis(Matrix const& Basis)
{
    TargetKind = 4;   // 062d75c6
    // 062d75d0-062d75d8: `LEA EDI,[ECX+0x34]; MOV ECX,0x9; REP MOVSD`. Nine
    // dwords is exactly sizeof(Matrix) and exactly the extent of this class's
    // own ctor-zeroed REP STOSD block.
    std::memcpy(ZeroBlock34, &Basis, sizeof(ZeroBlock34));
}

// FUN_062d75e0 (item 1610), Common.dll.txt 219388-219397, 10 instructions. This
// is one of the three writes tracker item 1309 handed over as landing inside a
// span this header marked `Opaque58`.
void FaceIdea::SetAxis(Vector const& Axis)
{
    TargetKind = 5;      // 062d75e4
    Vector58 = Axis;  // 062d75ee-062d75fe, to [ECX+0x58]
}

// FUN_062d7610 (item 1611), Common.dll.txt 219416-219428, 13 instructions.
void FaceIdea::SetThrottleScale(float Value)
{
    // 062d761e: the RAW argument goes to +0x90 ...
    Field90 = Value;
    // ... and 062d7624-062d7629: the DERIVED value to +0x94. The constructor
    // assigns these two the other way round; that asymmetry is retail's own and
    // is documented at this function's declaration.
    TurnThrottle = Behavior_InvokeManagerSlotF8(ObjInspectOf(Object), Value);   // FUN_062e5ba0
}

// FUN_062d7640 (item 1612), Common.dll.txt 219445-219472, 27 instructions. The
// other two writes item 1309 handed over, both inside the old `OpaqueAc` span.
void FaceIdea::SetUpVector(Vector const& Up)
{
    // 062d7644-062d7671: three `FCOMP DAT_06399408 (0.0f); TEST AH,0x44; JP`
    // gates. `TEST AH,0x44` masks C3|C2, and of the four FCOMP outcomes only
    // "equal" leaves a single bit set there, so PF is clear exactly on equality
    // and the JP fires on "not equal" -- an EQUALITY test, which is the same
    // idiom tracker item 1309 had to correct a `< 0.0f` misreading of.
    if (Up.x == 0.0f && Up.y == 0.0f && Up.z == 0.0f)
    {
        bUpValid = 0;   // 062d7673, and nothing else is written
        return;
    }

    bUpValid = 1;   // LAB_062d767d
    UpB0 = Up;      // 062d7684-062d7697, to [ECX+0xb0]
}

// .text:062d93c0 (item 1632), 3 instructions: `MOV [ECX+0x8c],EAX; RET 4`.
// POSSIBLE ICF FOLD with DodgeIdea's / BreakAwayIdea's own +0x8c -- see the
// declaration.
void FaceIdea::SetAngleWeight8c(float Value)
{
    FacingTolerance = Value;
}

// .text:062d9490, 3 instructions: `MOV [ECX+0x94],EAX; RET 4`. Receiver proved
// FaceIdea at .text:062fe34f (GotoBehavior's this+0xa4). SAME fold caveat.
void FaceIdea::SetScale94(float Value)
{
    TurnThrottle = Value;
}

// ============================================================================
// FaceIdea::Update / DodgeIdea::Update -- LAB_062d68b0 (item 1603) /
// LAB_062d7950 (item 1618). See common_aisteer.h for the class-identification
// summary. This section turns both from `simplified` (fully hand-disassembled,
// no C++) into real ports.
//
// CLASS IDENTITY SETTLED 2026-09-17 (claude:common-steering-close), by RTTI, not
// by position and not by a shared method address (which would prove nothing --
// /OPT:ICF folds byte-identical bodies across unrelated classes):
//
//   0x062d68b0  FaceIdea's ctor (0x062d6750, item 1601) installs the vtable at
//               .rdata:063a04ac (`MOV [ESI],PTR_LAB_063a04ac` at 062d6782). Its
//               RTTI Complete Object Locator sits at [vtable-4] == 063a04a8 ->
//               063a9a90, whose +0x0C pTypeDescriptor is
//               .data:FaceIdea::RTTI_Type_Descriptor. Slot 1 of that vtable is
//               0x062d68b0. => FaceIdea::Update.
//   0x062d7950  DodgeIdea's ctor (0x062d77d0, item 1616) installs the vtable at
//               .rdata:063a04c8 (`MOV [ESI],PTR_LAB_063a04c8` at 062d7819). COL
//               at [vtable-4] == 063a04c4 -> 063a9af4, +0x0C ->
//               .data:DodgeIdea::RTTI_Type_Descriptor. Slot 1 is 0x062d7950.
//               => DodgeIdea::Update. The positional hunt recorded in item
//               1618's notes was therefore right, and is now proved.
//
// The family's shared 6-slot vtable layout, read off all four sibling tables
// (063a04ac FaceIdea / 063a04c8 DodgeIdea / 063a04f8 BreakAwayIdea / 063a0524):
//   [0] SetContext("Bind")  [1] Update  [2] (ICF-folded, shared)
//   [3] GetKind             [4] Commit  [5] (ICF-folded, shared)
// For the record, 0x062d8a50 (item 1629) is slot 1 of 063a04f8, the table
// BreakAwayIdea's ctor installs at 062d89b6, whose COL 063a04f4 -> 063a9b40
// names BreakAwayIdea -- so that item's own positional guess is proved too.
//
// Also read out of DodgeIdea's own RTTI while settling this, and worth
// recording because common_idea.h currently says something slightly different:
// DodgeIdea's ClassHierarchyDescriptor (DAT_063a9ae4) has numBaseClasses == 3
// with BaseClassArray 063a9ad4 = [DodgeIdea, BaseIdea, DirectionPicker]. The
// DirectionPicker BaseClassDescriptor (063a9aa4) carries PMD.mdisp == 0x14. So
// the thing at +0x14 is a BASE CLASS named `DirectionPicker`, not a member named
// `FDirectionPicker`, and DodgeIdea's ctor initialises it inline (no LEA
// ECX,[ESI+0x14] / CALL pair exists in that ctor -- the only such pair is
// `LEA ECX,[ESI+0x28]` for a BaseWatcher). The +0x14 offset and the 0x20 size
// item 1616 recorded are both correct; only the kind and the spelling of the
// name are not. FaceIdea's own CHD (DAT_063a9a80) has numBaseClasses == 2 and
// does NOT include DirectionPicker.
//
// Shared plumbing: both classes read `this+0x8` as a SystemManager* and
// reach ExternalIface via `Sys+0x14fc+0x84` (== SystemManager::ExternalIface,
// common_systemmanager.h), with a cached position/PosValid pair living at
// Sys+0x14fc+0xc/+0x14fc+0x7e (== Sys+0x1508 / Sys+0x157a, inside
// SystemManager's still-opaque Opaque14F8/Flags1578 blocks). `this+0xc` is an
// IBehaviorManager* (confirmed: passed directly as `this` to
// IBehaviorManager::get_ship_up_direction/get_camera_level_status at several
// call sites this pass).
// ============================================================================


// FaceIdea::Update -- LAB_062d68b0, item 1603, vtable 063a04ac slot 1
// (RTTI-proved, see the section header above). Field offsets (raw, `void* Self`
// convention --
// see common_aisteer.h):
//   +0x04 void*             Object          -- IObjRW*, Bind()-set (FULLPORT-DEFERRED[OBJ])
//   +0x08 SystemManager*    Sys
//   +0x0C IBehaviorManager* Manager
//   +0x14 void*             TargetWatcher   -- BaseWatcher-style "-8" smart handle (case 1)
//   +0x1C..+0x24 Vector     FallbackTargetA -- case 2's own raw fallback target
//   +0x28..+0x30 Vector     FallbackDirB    -- case 3's own raw direction (used directly, no subtraction)
//   +0x34..+0x58 Matrix     RawFrame        -- case 4's raw 9-float frame, copied verbatim
//   +0x58..+0x60 Vector     BlendInputA     -- case 5/default's own local target vector
//   +0x64 unsigned char     UseRawTargetFlag -- gates case 1/2's raw-vs-watcher choice
//   +0x6C/+0x78/+0x84 float FallbackX/Y/Z   -- case 2's OWN secondary fallback triple (non-contiguous, 0xC apart)
//   +0x8C float             AccumThreshold  -- compared against +0xA8 for the return 0 vs 1 decision
//   +0x94 float             SteerMagnitudeCap  -- RESOLVED 2026-09-17
//         (claude:common-1740-dodgeface). The tail (062d7366-062d7406) compares
//         the steering vector's SQUARED length against this squared and, when it
//         is over, renormalizes the vector to exactly this length. It was
//         previously recorded as "ThrottleBlendParam, consumed only inside the
//         still-open throttle tail".
//   +0xA0 int                TargetKind     -- 6-way case selector (0..5; >5 falls to the default/caseD_6 path)
//   +0xA8 float              Accum          -- running "alignment" accumulator, compared to +0x8C
//   +0xAD unsigned char      BlendPending   -- gates the tail's extra TransformVectorByMatrix blend-add
//   +0xB0..+0xB8 Vector      BlendInputB    -- tail's own fixed-target floats, read only when BlendPending is set
//
// Dt is part of the vtable's Update(float) contract but this variant's own
// 962-instruction body never actually reads its incoming stack argument --
// confirmed by direct raw-disasm trace this pass (no periodic reroll timer
// exists on this variant, unlike the large sibling below).
int FaceIdea::Update(float /*DeltaTime*/)
{
    // Re-traced 2026-10-08 (cmn-ai8) against Common.dll.txt 062d68b0-062d74a9 and made a
    // FaceIdea member (it was the raw-offset free function SteeringTaskSmall_Update).
    // Frame: S = ESP after SUB 0x22c / PUSH EBX,EBP,ESI,EDI; SteerVector [S+0x10] is
    // zeroed at entry, WorkingFrame is [S+0x50].
    unsigned char* const B = reinterpret_cast<unsigned char*>(this);

    // 062d68ba-062d68d2: no Object -> 3.
    if (Object == nullptr)
        return 3;

    SystemManager* const Sys = Handle;
    IBehaviorManager* const Manager = static_cast<IBehaviorManager*>(Context);
    // 062d68d5-062d68e1: EBX = Manager->Pathfind + 0x214 (its Field_214 record).
    PathfindManager* const Pathfind = Manager->Pathfind;

    // NOTE 2026-09-17 (claude:common-dodge-face): an entry-time
    // `CachedPos = CachedIfacePosition(Sys)` snapshot used to live here and is
    // gone. Retail never snapshots it -- every consumer re-reads Sys+0x1508
    // AFTER its own RefreshCachedIfacePosition(Sys) call, so a snapshot taken
    // before the refresh is a stale value by construction. Its last remaining
    // reader (case 1) was rewritten this pass; the rest already re-read.
    // [S+0x50]. A tag above 5 (caseD_6) jumps straight to the shared tail with this frame
    // never written -- uninitialised stack in retail (UB, unreachable: every setter and the
    // ctor write 0..5); the port value-initialises it.
    Matrix WorkingFrame{};
    // [S+0x10], zeroed at 062d68ea-062d68fa.
    Vector SteerVector{ 0.0f, 0.0f, 0.0f };

    // ---- Shared "no target" fallback (LAB_062d6a4a): refresh the cached
    // iface position, then build a frame from GetIfacePositionOrZero(this+0xc)'s
    // own result relative to CachedPos.
    //
    // CORRECTION (2026-09-11, item 1827 investigation): FUN_062e92e0 is now
    // fully identified and ported as CheckSystemManagerSteeringGate
    // (common_systemmanager.h/.cpp) -- SystemManager+0x18 (ArmedSelf) turned
    // out to be a conditionally-armed SELF-pointer, not a pointer to some
    // other object; see that header's own comment for the full derivation.
    // An EARLIER draft of this comment claimed retail's return value here
    // "gates nothing observable" -- that was WRONG, disproven by direct
    // re-trace of the raw bytes this pass: `TEST AL,AL` / `JZ` right after
    // the call (Common.dll.txt:062d6a5e/062d6a63) genuinely selects between
    // TWO DIFFERENT frame-construction paths.
    //
    // CORRECTION #2 (2026-09-11, d300:common-port, item 1603 notes): an
    // EARLIER draft of THIS comment also claimed both paths "funnel through
    // the still-unidentified FUN_0628b030" -- also WRONG. FUN_0628b030 is
    // unambiguously item 763 (Vec3Assign), re-traced byte-exact at every
    // call site in this function this pass; the unusual-looking "reserved-
    // stack-slot" codegen is just the compiler FSTP-ing FPU results directly
    // into pre-reserved argument slots instead of push-after-compute. RelDir
    // = Sub(FallbackPos, CachedPos) below IS the byte-exact port of that
    // step already. That left the branch selection as the recorded gap: the
    // TRUE-gate path (get_ship_up_direction + FUN_062e3730/
    // SteeringFrameFromRelativeDir) versus the FALSE-gate path
    // (Common.dll.txt:062d6bc2-062d6c4e), which was believed to reach a second,
    // 2-argument method at ExternalIface vtable slot 0xc before feeding
    // FUN_062e1f40/BuildOrientationBasis. It does not -- see immediately below.
    //
    // CLOSED 2026-09-17 (claude:common-steering-close). The "2-arg slot 0xc
    // method" this comment previously chased DOES NOT EXIST -- see the long
    // refutation block next to IfaceOrientation() above. The two dwords pushed
    // before `CALL [EDX+0xc]` at 218510-218514 are BuildOrientationBasis's
    // arguments 4 and 3; the slot-0xc call itself is the ordinary zero-argument
    // IObjInspect::get_transform(). Both gate branches are now wired for real.
    //
    // Fully re-traced this pass (Common.dll.txt:218340-218404 for the TRUE
    // gate, 218405-218440 for the FALSE gate). The two branches share every
    // input and differ in exactly one respect:
    //   TRUE : SteeringFrameFromRelativeDir(Out, Reference, Delta, UpHint =
    //          IBehaviorManager::get_ship_up_direction())   [FUN_062e3730]
    //   FALSE: BuildOrientationBasis       (Out, Reference, Delta, UpHint =
    //          nullptr)                                     [FUN_062e1f40]
    //
    // BUG FIX, same pass: Delta's first term was wrong here. Retail resolves
    // the position from the TARGET WATCHER at this->0x14, via the module-wide
    // `-8` Watchable->IObjRW adjustment, on BOTH branches identically
    // (`MOV EAX,[EBP+0x14]; ... ADD EAX,-0x8 ...; CALL FUN_062e60e0` at
    // 062d6a84-062d6a9b for the TRUE gate and 062d6bdd-062d6bf4 for the
    // FALSE gate). This port passed `Manager` instead -- a different object
    // entirely, so the fallback steered toward the behaviour manager's own
    // position rather than the watched target's.
    auto NoTargetFallback = [&]()
    {
        bool const Gate = Sys->Thrust.CheckSteeringGate(); // FUN_062e92e0, item 1827
        RefreshCachedIfacePosition(Sys);

        // Delta = TargetWatcherPosition - Sys->CachedIfacePosition. The
        // subtraction is the FSUB against ESI+0xc/+0x10/+0x14 with
        // ESI == Sys+0x14fc, i.e. Sys+0x1508 == CachedIfacePosition.
        Vector TargetPos{};
        GetIfacePositionOrZero(&TargetPos, ObjRwFromWatchable(*reinterpret_cast<void* const*>(B + 0x14)));
        Vector const RelDir = Sub(TargetPos, CachedIfacePosition(Sys));

        Matrix* const Reference = IfaceOrientation(Sys);
        if (Gate)
        {
            Vector const Up = Manager->get_ship_up_direction();
            SteeringFrameFromRelativeDir(WorkingFrame, *Reference, RelDir, Up);
        }
        else
        {
            BuildOrientationBasis(WorkingFrame, *Reference, RelDir, nullptr);
        }
    };

    // 062d68db: MOV EAX,[EBP+0xa0].
    switch (TargetKind)
    {
    case 0:
        return 3;

    case 1:
    {
        void* const Watcher = *reinterpret_cast<void* const*>(B + 0x14);
        if (Watcher == nullptr)
            return 2;
        IObjRW* const Adjusted = ObjRwFromWatchable(Watcher);
        if (Adjusted == nullptr)
            return 2;

        // Slot 2 is get_angular_velocity(), returned by value through the hidden
        // pointer (062d694f-062d6956, `MOV EDX,[ECX]; LEA EAX,[ESP+0x1c]; PUSH EAX;
        // CALL [EDX+0x8]`).
        Vector const AngularVelocity = Adjusted->get_angular_velocity();

        // BUG FIX 2026-09-17 (claude:common-dodge-face). BOTH of this gate's
        // conditions were INVERTED in the previous port. Re-derived instruction
        // by instruction from Common.dll.txt:218232-218241:
        //
        //   062d6959  MOV AL,[EBP+0x64] ; TEST AL,AL ; JZ LAB_062d6a4a
        //       -> flag CLEAR jumps to the no-target fallback. The old port took
        //          the fallback only when the flag was SET.
        //   062d6980  FCOMP double 0.01 (DAT_0639e6b0) ; FNSTSW AX
        //   062d6988  TEST AH,0x5 ; JP LAB_062d6a4a
        //       -> TEST AH,0x5 masks C0 (0x1) and C2 (0x4). JP fires on EVEN
        //          parity, i.e. neither bit set, i.e. NOT-below and ordered, i.e.
        //          LenSq >= 0.01 jumps to the fallback. The old port took the
        //          fallback on LenSq < 0.01, the exact opposite.
        //
        // So the precise-aim path below is retail's NEARLY-STATIONARY-target path
        // (small angular velocity), not its "in range" path, and it is reached
        // only when the +0x64 flag is set. The old comment's "degenerate position"
        // reading was wrong on both counts: this reads angular velocity, not a
        // position, and a large value is what disqualifies it.
        // 062d6964-062d698b: (y*y + z*z) + x*x on the x87 stack against the double 0.01;
        // unordered also takes the fallback.
        double const AngularSq = (static_cast<double>(AngularVelocity.y) * AngularVelocity.y +
                                  static_cast<double>(AngularVelocity.z) * AngularVelocity.z) +
                                 static_cast<double>(AngularVelocity.x) * AngularVelocity.x;
        if (Field64 == 0 || !(AngularSq < 0.01))
        {
            NoTargetFallback();
            break;
        }

        // WIRED FOR REAL 2026-09-17 (claude:common-dodge-face), closing this
        // item's residual (a). Its recorded blocker was STALE on both halves:
        //   * FUN_062e61f0 (item 1774) is no longer a "blocked thunk" -- it has
        //     been ready_unverified and ported as Behavior_InvokeNodeSlotC since
        //     2026-09-11.
        //   * FUN_062e8ce0 (item 1821) is NOT ON THIS PATH AT ALL. Grepping every
        //     call in this function's listing range (218200-219229) shows 062e8ce0
        //     is never called from FaceIdea::Update; the recorded "1774 -> 1821"
        //     chain was item 1774's own call-graph, not this call site's.
        // Full trace, Common.dll.txt:218291-218344 (.text:062d6991-062d6a45):
        //   Node       = ObjRwFromWatchable(this->0x14)          (062d6991-062d699f)
        //   NodeXf     = Behavior_InvokeNodeSlotC(Node)          (062d69a0)
        //   NodeUp     = (NodeXf[0x4], NodeXf[0x10], NodeXf[0x1c])
        //                = column 1 of the node's Transform's 3x3 part, staged as
        //                  a Vector at frame+0x38 (062d69a5-062d69c2; the three
        //                  FLDs are 0xC apart, so this is a COLUMN, not a row)
        //   RefreshCachedIfacePosition(Sys)                      (062d69c6-062d69e1)
        //   TargetPos  = GetIfacePositionOrZero(Node)            (062d69e4-062d69fb)
        //   Delta      = TargetPos - CachedIfacePosition(Sys)    (062d6a00-062d6a0d,
        //                FUN_06288800 == Vec3Sub, item 729, ready_verified)
        //   BuildOrientationBasis(Out, get_transform(), Delta, &NodeUp)
        //                                                        (062d6a18-062d6a40)
        // Argument roles confirmed by ESP-delta accounting: `LEA ECX,[ESP+0x38]`
        // at 062d6a18 (depth E) resolves to the same physical slot the three FSTPs
        // above wrote, and the site's own shared `ADD ESP,0x10` at LAB_062d6b06
        // confirms four caller-cleaned __cdecl dwords for one call.
        Transform const* const NodeTransform = Behavior_InvokeNodeSlotC(Adjusted);
        // Transform is Matrix-then-Vector with Rotation at +0 (common_csys.h), so retail's
        // +0x4/+0x10/+0x1c are E[0][1]/E[1][1]/E[2][1].
        Vector NodeUp;
        NodeUp.x = NodeTransform->Rotation.E[0][1];
        NodeUp.y = NodeTransform->Rotation.E[1][1];
        NodeUp.z = NodeTransform->Rotation.E[2][1];

        RefreshCachedIfacePosition(Sys);

        Vector TargetPos;
        GetIfacePositionOrZero(&TargetPos, Adjusted);
        Vector const RelDir = Sub(TargetPos, CachedIfacePosition(Sys));

        BuildOrientationBasis(WorkingFrame, *IfaceOrientation(Sys), RelDir, &NodeUp);
        break;
    }

    case 2:
    {
        // REWRITTEN 2026-09-17 (claude:common-steering-close) after a full
        // instruction-by-instruction re-trace of caseD_2 (Common.dll.txt:
        // 218516-218649) with stack-depth accounting. Two separate errors in
        // the previous port are fixed, plus the previously-unwired FALSE gate.
        //
        // ERROR 1 -- the non-contiguous (+0x6C, +0x78, +0x84) triple is NOT a
        // target position. It is the UpHint argument. Retail stages it into
        // ESP+0x44/+0x48/+0x4C (062d6c5e-062d6c7b) and then passes
        // `LEA ECX,[ESP+0x44]; PUSH ECX` as BuildOrientationBasis's argument 4
        // (062d6ccc-062d6cdb). The previous port fed it in as the target
        // position, which is what the steering aimed at.
        //
        // ERROR 2 -- the target position is ALWAYS this->0x1c/0x20/0x24, on
        // all three of this case's paths, not just the flag-clear one. All
        // three compute the identical Delta via the same FSUB-against-
        // Sys+0x1508 + Vec3Assign sequence (062d6c9d-062d6cc4, 062d6d40-
        // 062d6d5f, 062d6dbc-062d6de3).
        //
        // So the real shape is: Delta is common; the only thing the two gates
        // choose is which frame builder runs and with which UpHint.
        //   this->0x64 != 0        : BuildOrientationBasis(.., &UpHintTriple)
        //   flag 0, gate TRUE      : SteeringFrameFromRelativeDir(.., ShipUp)
        //   flag 0, gate FALSE     : BuildOrientationBasis(.., nullptr)
        bool const UseRawUpHint = *reinterpret_cast<unsigned char const*>(B + 0x64) != 0;
        bool Gate = false;
        if (!UseRawUpHint)
        {
            // LAB_062d6cfe (218565-218570). FUN_062e92e0 ==
            // CheckSystemManagerSteeringGate (item 1827).
            Gate = Sys->Thrust.CheckSteeringGate();
        }

        // Common to all three paths: refresh the cache, then
        // Delta = FallbackTargetA(this->0x1c..0x24) - Sys->CachedIfacePosition.
        RefreshCachedIfacePosition(Sys);
        Vector const RelDir = Sub(*reinterpret_cast<Vector const*>(B + 0x1c), CachedIfacePosition(Sys));

        Matrix* const Reference = IfaceOrientation(Sys);
        if (UseRawUpHint)
        {
            // The three fields really are 0xC apart, not packed. They are read with three
            // separate FLDs (062d6c5e/062d6c68/062d6c75).
            Vector const UpHint{ *reinterpret_cast<float const*>(B + 0x6c),
                                  *reinterpret_cast<float const*>(B + 0x78),
                                  *reinterpret_cast<float const*>(B + 0x84) };
            BuildOrientationBasis(WorkingFrame, *Reference, RelDir, &UpHint);
        }
        else if (Gate)
        {
            Vector const Up = Manager->get_ship_up_direction();
            SteeringFrameFromRelativeDir(WorkingFrame, *Reference, RelDir, Up);
        }
        else
        {
            BuildOrientationBasis(WorkingFrame, *Reference, RelDir, nullptr);
        }
        break;
    }

    case 3:
    {
        // WIRED FOR REAL 2026-09-17 (claude:common-steering-close). Real body
        // Common.dll.txt:218662-218692 (caseD_3). The blocker recorded here
        // through three earlier passes -- "the FALSE branch calls slot 0xc with
        // TWO pushed args, so it is a second unidentified method" -- is
        // DISPROVED. See the refutation block next to IfaceOrientation() above:
        // the `PUSH 0x0` and `LEA ECX,[EBP+0x28]; PUSH ECX` at 062d6e6c-062d6e71
        // are BuildOrientationBasis's arguments 4 and 3, pushed before the
        // argument-2-producing zero-argument get_transform() call, and the
        // site's own `ADD ESP,0x10` (062d6b06, shared via LAB_062d6b06)
        // confirms exactly four caller-cleaned __cdecl dwords for one
        // BuildOrientationBasis call.
        //
        // Both branches use the same Reference (get_transform()) and the same
        // Forward (&this->0x28, FallbackDirB, read-only -- no subtraction
        // against the cached position on this case, unlike cases 1/2). They
        // differ only in the frame builder and the UpHint:
        //   gate TRUE  (062d6e29-062d6e5a): SteeringFrameFromRelativeDir with
        //              UpHint = IBehaviorManager::get_ship_up_direction()
        //   gate FALSE (062d6e5f-062d6e80): BuildOrientationBasis with
        //              UpHint = nullptr
        bool const Gate = Sys->Thrust.CheckSteeringGate(); // FUN_062e92e0, item 1827
        Matrix* const Reference = IfaceOrientation(Sys);
        if (Gate)
        {
            Vector const Up = Manager->get_ship_up_direction();
            SteeringFrameFromRelativeDir(WorkingFrame, *Reference, Direction28, Up);
        }
        else
        {
            BuildOrientationBasis(WorkingFrame, *Reference, Direction28, nullptr);
        }
        break;
    }

    case 4:
        // Raw 9-float frame, copied verbatim (no orientation rebuild at all).
        WorkingFrame = *reinterpret_cast<Matrix const*>(B + 0x34);
        break;

    default:
        // caseD_6 (JA at 062d6902): a tag above 5 goes straight to the shared tail.
        break;

    case 5:
    {
        // STRUCTURAL FIX (2026-09-11, d280:common-port): re-traced caseD_5's
        // full raw body this pass (Common.dll.txt:218699-218808) while
        // investigating a get_angular_throttle XREF, and found it does NOT
        // fall through to the shared HaveFrame/NoTargetFallback/throttle-tail
        // logic below at all -- every internal branch (the ExternalIface
        // vtable+0xbc flag gate, IBehaviorManager::get_camera_level_status,
        // and FUN_062e92e0(this->0x18) [item 1827] triple-gated attempt at
        // ComputeBankedSteeringFrame(FUN_062e37e0, item 1744) vs. a
        // FUN_062e2c00 fallback) converges at LAB_062d6fb7, which calls
        // FUN_062e8c20(this->0x18) [confirmed == Behavior_RefreshCached-
        // PositionIfMoved, item 1820, ready -- see the shared tail's own use
        // of it below] and then hits a hardcoded `XOR EAX,EAX` before RET --
        // i.e. retail ALWAYS returns 0 for TargetKind>=5 and discards
        // FUN_062e8c20's own return value. An earlier draft of this port
        // incorrectly `break;`-ed into the shared tail for this case;
        // corrected to return 0 directly, matching retail.
        //
        // FULLPORT-DEFERRED NO LONGER, 2026-09-17 (claude:common-1740-dodgeface).
        // The recorded reason -- "its real inputs were traced far enough to
        // confirm the CONTROL FLOW but not far enough to safely wire the call
        // itself" -- is retired: the three paths were re-traced by ESP depth
        // this pass (E := post-prologue ESP; every path converges on the single
        // `ADD ESP,0x14` at 062d6fba, i.e. five caller-cleaned dwords), and the
        // result they all produce is NOT discarded -- it is the Vector at E+0x10
        // that Behavior_RefreshCachedPositionIfMoved receives at 062d6fbd. The
        // old port passed a zero DummyCache there instead.
        //
        //   Common (218700-218709): a 9-dword REP MOVSD copy of
        //   get_transform()'s Rotation into E+0x50 -- a LOCAL frame, not this
        //   function's WorkingFrame, which is why nothing here escapes except
        //   through E+0x10.
        //   Blend := this->0x58/0x5c/0x60 (BlendInputA), staged into E+0x10 by
        //   all three paths before they diverge.
        //
        //   A (218726-218746, all three gates true):
        //       ComputeBankedSteeringFrame(Sys+0x14fc,
        //           get_ship_up_direction(), LocalFrame, Blend, Mode = 0)
        //   B (218747-218770, is_player but camera/gate false):
        //       Blend = TransformVectorByMatrix(get_transform(), Blend), with a
        //       SECOND, fresh slot-0xc fetch at 062d6f44; skips 062e37e0.
        //   C (218771-218793, not the player): byte-for-byte the same call as A,
        //       reached through its own copy of the argument staging.
        //
        // A and C are identical in effect, so the three collapse to the same
        // is_player-shaped two-way gate the shared tail's own banked refresh
        // uses. Mode is 0 here, not the 1 that tail passes.
        // 062d6e92-062d6eb5: WorkingFrame = get_transform()'s rotation (9 dwords).
        WorkingFrame = *IfaceOrientation(Sys);

        // 062d6ec6-062d6eed: is_player() (+0xbc) false, or get_camera_level_status() and
        // the steering gate both true -> banked frame (Mode 0) on SteerVector = Vector58;
        // otherwise SteerVector = FUN_062e2c00(fresh get_transform(), Vector58).
        bool bBanked = !Sys->SelfTarget.Iface->is_player();
        if (!bBanked)
            bBanked = Manager->get_camera_level_status() && Sys->Thrust.CheckSteeringGate();
        if (bBanked)
        {
            SteerVector = Vector58;
            Vector const Up = Manager->get_ship_up_direction();
            ComputeBankedSteeringFrame(&Sys->SelfTarget,Up, WorkingFrame,
                                       SteerVector, 0);
        }
        else
        {
            SteerVector = TransformVectorByMatrix(*IfaceOrientation(Sys), Vector58);
        }

        // 062d6fb7-062d6fd6: FUN_062e8c20(Handle + 0x18, &SteerVector); return 0.
        Sys->Thrust.SetAxisThrottle(SteerVector);
        return 0;
    }
    }

    // ---- Shared throttle/bank tail. CITATION FIX (2026-09-11,
    // d280:common-port), WIRED FOR REAL this pass (2026-09-11, d282:common-
    // port) now that FUN_062e1f40 is identified (item 1734,
    // ready_unverified) as BuildOrientationBasis. Re-traced the real
    // structure while chasing a get_angular_throttle XREF at 0x062d70a7:
    //   1. Common.dll.txt:062d6b06-062d70a6 -- the TRUE shared convergence
    //      for cases 0-4/NoTargetFallback (confirmed via every case body's
    //      own JMP LAB_062d6b06/LAB_062d6b00): a 9-float copy of
    //      WorkingFrame into a local buffer, then a flag test
    //      (`*(Manager+0xc)+0x214` bit 0x2, Common.dll.txt:218418-218419)
    //      gating an EXTRA pass that, when taken, can OVERWRITE that local
    //      copy with a freshly-recomputed BuildOrientationBasis(...,
    //      Forward=<a Manager+0xc-relative vector distinct from
    //      WorkingFrame's own source>, UpHint=nullptr) result
    //      (Common.dll.txt:218420-218845) gated behind FUN_062e92e0.
    //      CLOSED 2026-09-17 (claude:common-dodge-face). Every reason this
    //      recompute was ever deferred for is now dead:
    //        * FUN_062e92e0 = CheckSystemManagerSteeringGate (item 1827), real.
    //        * FUN_062e60e0 = GetIfacePositionOrZero (item 1771), real -- and
    //          it is NOT EVEN ON THIS PATH; the recompute reads its target
    //          point straight out of the Manager sub-object, no call at all.
    //        * FUN_0628b030 = Vec3Assign (item 763), real, and here it is just
    //          the compiler FSTP-ing three already-computed floats into a
    //          pre-reserved argument slot.
    //      The one genuinely open question this item's notes recorded -- "the
    //      recompute's own Forward source" -- is now answered from the raw
    //      bytes. EBX is set ONCE at function entry (062d68d5-062d68e1:
    //      `MOV EAX,[EBP+0xc]; MOV EBX,[EAX+0xc]; ADD EBX,0x214`), so
    //      EBX == (char*)*(Manager+0xc) + 0x214. The gate is `TEST byte ptr
    //      [EBX],0x2` (062d6b16) -- a BIT TEST on the byte at SubObj+0x214,
    //      not the `== 2` integer compare the sibling DodgeIdea port uses at
    //      its own analogous site. Forward is then read as three floats at
    //      EBX+0x18/+0x1c/+0x20 (062d6b59/6b6d/6b77 on the true arm,
    //      062d6ff4/7008/7012 on the false arm), i.e. a Vector at
    //      SubObj+0x22c, minus Sys+0x1508 (ESI+0xc/+0x10/+0x14 with
    //      ESI == Sys+0x14fc) -- the cached iface position, refreshed
    //      immediately before on both arms.
    //      The recompute's result overwrites the local 9-float copy rather
    //      than WorkingFrame itself: LAB_062d704a's `LEA EDI,[ESP+0x60]` at
    //      depth E-0x10 resolves to the identical physical slot LAB_062d6b0b's
    //      `LEA EDI,[ESP+0x50]` at depth E writes, which is also the buffer
    //      step 2 passes to get_angular_throttle. Modelled below as a named
    //      local `ThrottleFrame`.
    //   2. 0x062d70a7 -- a SECOND TurnHelper::get_angular_throttle call
    //      site, common to ALL of cases 0-4/NoTargetFallback (not
    //      case-specific the way the sibling Large variant's two sites are).
    //      Independently re-derived this pass directly from the raw operand
    //      trace (Common.dll.txt:218865-218878), confirming d280's citation:
    //      OutAngle=&this->0xa8 (writes directly into the Accum field --
    //      the LEA at 218866 computes [EBP+0xa8] and is pushed LAST, i.e.
    //      it is the hidden-return-adjacent first explicit __cdecl param
    //      per the mangled signature `?get_angular_throttle@TurnHelper@@YA
    //      ?AVVector@@ABVMatrix@@0PAM@Z`), B=the same local Matrix buffer
    //      step 1 copies WorkingFrame into (LEA ECX,[ESP+0x54] at 218871
    //      resolves, after accounting for the intervening PUSH at 218867,
    //      to the identical physical stack slot LAB_062d6b06's own
    //      `LEA EDI,[ESP+0x50]`/REP MOVSD at 218411-218416 writes --
    //      confirmed by hand ESP-delta accounting, not guessed), A=a fresh
    //      IfaceOrientation(Sys) re-fetch (CALL [ExternalIface_vt+0xc] at
    //      218874, same zero-arg-thiscall-returning-Matrix* shape already
    //      established for that slot elsewhere in this cluster). Its Vector
    //      result is copied to frame slot E+0x10
    //      (Common.dll.txt:218879-218884).
    //      CORRECTION 2026-09-17 (claude:common-1740-dodgeface): the claim that
    //      this Vector is "not consumed by any other observable effect in this
    //      function, so it is intentionally discarded here" was WRONG, and it
    //      was wrong in a way that hid the whole tail. E+0x10 is this
    //      function's working steering vector: it is the banked-frame refresh's
    //      BankVector argument, it is FUN_062e2c70's by-value Vector argument,
    //      FUN_062e2c70's result is written straight back over it, the
    //      magnitude cap and the BlendPending add both operate on it, and it is
    //      what Behavior_RefreshCachedPositionIfMoved finally receives
    //      (`LEA EAX,[ESP+0x10]` at 062d746f, depth E). It is now carried for
    //      real as the local `SteerVector`.
    //   3. Common.dll.txt:062d7141-062d734d -- the lead-angle computation
    //      (VecNormalize3 x2 + ClampedDot3 + acos) feeding FUN_062e2c70 at
    //      0x062d734d. WIRED FOR REAL 2026-09-17
    //      (claude:common-1740-dodgeface): item 1740 is ported above as
    //      LimitAxisThrottleForTurn, and so is the FlagBc-gated
    //      ComputeBankedSteeringFrame call this step follows. Two stale claims
    //      in the old text are retracted with it: this step does NOT take Dt as
    //      an input (this variant never reads its Dt argument at all -- see the
    //      note on the signature above; the two floats FUN_062e2c70 receives
    //      are an acos result and either 0.35f or this->0x98), and the
    //      ComputeBankedSteeringFrame attempt is not blocked by item 1740 at
    //      all -- it runs BEFORE it, on its own gate.
    //   4. Converges on Behavior_RefreshCachedPositionIfMoved (item 1820,
    //      ready) on Handle + 0x18 -- corrected 2026-09-17, this said
    //      "this->0x18", which is the receiver bug ComponentThisOf() now
    //      fixes -- and the final Accum-vs-Threshold return (whose polarity was
    //      also inverted and is fixed below).
    // Step 1: the 9-float working copy, and the flag-gated recompute that can
    // replace it. Common.dll.txt:218411-218445 and 218820-218849.
    // The recompute writes the same [S+0x50] slot every case filled.
    Matrix& ThrottleFrame = WorkingFrame;
    // 062d6b16: TEST byte [Pathfind+0x214],0x2 (a bit test on Field_214's low byte).
    if ((static_cast<unsigned char>(Pathfind->Field_214) & 0x2) != 0)
    {
        bool const Gate = Sys->Thrust.CheckSteeringGate(); // FUN_062e92e0, item 1827
        RefreshCachedIfacePosition(Sys);

        // Forward = Pathfind->Field_22C (EBX+0x18/+0x1c/+0x20) - CachedIfacePosition(Sys),
        // each FSUB FSTP'd to float.
        Vector const RecomputeDir = Sub(Pathfind->Field_22C, CachedIfacePosition(Sys));

        Matrix* const RecomputeReference = IfaceOrientation(Sys);
        if (Gate)
        {
            // 062d6b85-062d6bb8: SteeringFrameFromRelativeDir with
            // UpHint = IBehaviorManager::get_ship_up_direction().
            Vector const Up = Manager->get_ship_up_direction();
            SteeringFrameFromRelativeDir(ThrottleFrame, *RecomputeReference, RecomputeDir, Up);
        }
        else
        {
            // 062d7020-062d7045: the same call with UpHint = nullptr
            // (`PUSH 0x0` is BuildOrientationBasis's argument 4).
            BuildOrientationBasis(ThrottleFrame, *RecomputeReference, RecomputeDir, nullptr);
        }
    }

    // LAB_062d705a-062d707b: for the player ship (Sys+0x1580 ->is_player()), the manager
    // (this->Context) re-levels its up direction: FUN_062d35c0(1).
    if (Sys->SelfTarget.Iface->is_player())
        Manager->RelevelShipUpDirection(true);

    // Step 2. CORRECTION 2026-09-17 (claude:common-1740-dodgeface): this
    // result is NOT dead and is no longer discarded. It is the working steering
    // vector the whole rest of the function carries -- retail copies it into
    // frame slot E+0x10 at 218879-218884, and E+0x10 is the very slot the
    // banked-frame refresh below takes as its BankVector argument, that step 3
    // reads back as FUN_062e2c70's by-value Vector argument, that step 3's
    // result is written back into, and that the final
    // Behavior_RefreshCachedPositionIfMoved receives (`LEA EAX,[ESP+0x10]` at
    // 062d746f, depth E).
    // OutAngle is &this->0xa8 (RemainingAngle), written by get_angular_throttle.
    SteerVector = TurnHelper::get_angular_throttle(*IfaceOrientation(Sys), ThrottleFrame, &RemainingAngle);

    // Step 2b -- the banked-frame refresh, WIRED 2026-09-17
    // (claude:common-1740-dodgeface). Its only recorded blocker was that
    // "its real inputs ... were traced far enough to confirm the CONTROL FLOW
    // but not far enough to safely wire the call itself". They are traced now,
    // by ESP depth, from Common.dll.txt:218912-218927 (E := post-prologue ESP):
    //   PUSH 0x1                     -> Mode = 1
    //   LEA EAX,[ESP+0x14] @E-4      -> E+0x10, i.e. &SteerVector (BankVector)
    //   LEA ECX,[ESP+0x58] @E-8      -> E+0x50, i.e. &ThrottleFrame (WorkingFrame)
    //   LEA EDX,[ESP+0x38] @E-0xc    -> E+0x2c, the hidden-return buffer for
    //                                   get_ship_up_direction, whose returned
    //                                   pointer is then pushed as TargetDir
    //   PUSH Sys+0x14fc              -> Elem
    // and `ADD ESP,0x14` confirms the five caller-cleaned dwords.
    //
    // The gate around it (218885-218911) is a real three-way, not a simple AND:
    // retail asks is_player() TWICE. If is_player() and both
    // get_camera_level_status() and CheckSystemManagerSteeringGate() agree, it
    // refreshes; if is_player() and either disagrees, it SKIPS; and if the
    // object is not the player it refreshes unconditionally (062d7112's
    // `TEST AL,AL; JNZ LAB_062d7141` is the only path that skips on the second
    // query). Both is_player() calls are issued as retail does.
    bool bRefreshBankedFrame = Sys->SelfTarget.Iface->is_player() && Manager->get_camera_level_status() &&
                               Sys->Thrust.CheckSteeringGate();
    if (!bRefreshBankedFrame)
        bRefreshBankedFrame = !Sys->SelfTarget.Iface->is_player();
    if (bRefreshBankedFrame)
    {
        Vector const Up = Manager->get_ship_up_direction();
        ComputeBankedSteeringFrame(&Sys->SelfTarget,Up,
                                   ThrottleFrame, SteerVector, 1);
    }

    // Step 3 -- WIRED 2026-09-17 (claude:common-1740-dodgeface), closing this
    // item's residual (c). FUN_062e2c70 is ported above as
    // LimitAxisThrottleForTurn, so the whole 062d7141-062d734d block can be
    // written for real. Two call shapes converge on the one call site at
    // 062d734d and differ only in two arguments:
    //   062d72ee (the lead-angle path)  TurnAngle = acos(...),  ReferenceAngle = 0.35f
    //   062d731b (the fallback path)    TurnAngle = 0.0f,       ReferenceAngle = this->0x98
    // Both pass bank = true, both pass the Vector at E+0x10 by value, both take
    // &ThrottleFrame (E+0x50) as the target frame and `this->0x4` as the object,
    // and both write the result straight back over E+0x10 (062d7352-062d7362).
    //
    // The gate is 062d7141-062d7179: the watcher handle must be non-null both
    // before and after its "-8" adjustment, and the watched object's own
    // velocity magnitude (GetIfaceVectorMagnitude, item 1773 -- vtable slot 4)
    // must exceed 1.0 (DAT_06399410, bytes 00 00 00 00 00 00 f0 3f == 1.0
    // double; `TEST AH,0x41 / JNZ` branches away on below-or-equal).
    IObjInspect const* const BoundObject = static_cast<IObjInspect const*>(*reinterpret_cast<void* const*>(B + 0x4));
    IObjRW* const LeadNode = ObjRwFromWatchable(*reinterpret_cast<void* const*>(B + 0x14));

    Vector LimitedSteer{ 0.0f, 0.0f, 0.0f };
    if (LeadNode != nullptr && GetIfaceVectorMagnitude(LeadNode) > 1.0)
    {
        // 062d717f-062d72d3. Two RefreshCachedIfacePosition idioms, two
        // GetIfacePositionOrZero calls on the same node, and one get_velocity
        // (slot 0x4 at 062d7241) -- the second delta leads the target by one
        // second of its own velocity, and the angle between the two deltas is
        // how far the aim point has to swing.
        RefreshCachedIfacePosition(Sys);
        Vector NodePos{};
        GetIfacePositionOrZero(&NodePos, LeadNode);
        Vector const DirectDelta = Sub(NodePos, CachedIfacePosition(Sys));

        RefreshCachedIfacePosition(Sys);
        Vector NodePosAgain{};
        GetIfacePositionOrZero(&NodePosAgain, LeadNode);
        Vector const NodeVelocity = LeadNode->get_velocity();
        // 062d7244-062d727f: x and y FSTP the (pos + vel) sum to float before subtracting
        // the cached centre; z subtracts from the unrounded sum.
        Vector const Center = CachedIfacePosition(Sys);
        float const LeadX = NodePosAgain.x + NodeVelocity.x;
        float const LeadY = NodePosAgain.y + NodeVelocity.y;
        Vector LeadDelta;
        LeadDelta.x = LeadX - Center.x;
        LeadDelta.y = LeadY - Center.y;
        LeadDelta.z = static_cast<float>((static_cast<double>(NodePosAgain.z) + NodeVelocity.z) - Center.z);

        Vector DirectDir{};
        Vector LeadDir{};
        VecNormalize3(&DirectDelta, &DirectDir);
        VecNormalize3(&LeadDelta, &LeadDir);
        float const LeadAngle = std::acos(ClampedDot3(DirectDir, LeadDir));

        // 062d72d7-062d72e5: FUN_062e5ba0 (item 1766) ==
        // Obj->get_projected_axis_throttle_XY(LeadAngle), and retail throws the
        // result away on the spot (`FSTP ST0` at 062d72e5). Issued anyway, for
        // parity with retail's own call.
        (void)Behavior_InvokeManagerSlotF8(BoundObject, LeadAngle);

        LimitAxisThrottleForTurn(LimitedSteer, BoundObject, SteerVector, &ThrottleFrame,
                                 LeadAngle, 0.35f, true); // 0x3eb33333 == 0.35f
    }
    else
    {
        LimitAxisThrottleForTurn(LimitedSteer, BoundObject, SteerVector, &ThrottleFrame,
                                 0.0f, *reinterpret_cast<float const*>(B + 0x98), true);
    }
    SteerVector = LimitedSteer;

    // 062d7366-062d7406: cap the steering vector's magnitude at this->0x94.
    // Retail compares the SQUARED length against 0x94 squared (FCOMPP at
    // 062d7391, `TEST AH,0x41 / JNZ` skipping on below-or-equal) and only then
    // pays for the sqrt. This resolves what the header called
    // "ThrottleBlendParam, consumed only inside the still-open throttle tail".
    // LenSq = (z*z + y*y) + x*x and Cap*Cap stay on the x87 stack; the scale is
    // 1/sqrt(LenSq). x multiplies its unrounded x*Inv by Cap; y and z FSTP x*Inv first.
    double const SteerSq = (static_cast<double>(SteerVector.z) * SteerVector.z +
                            static_cast<double>(SteerVector.y) * SteerVector.y) +
                           static_cast<double>(SteerVector.x) * SteerVector.x;
    double const MagnitudeCap = TurnThrottle;
    if (SteerSq > MagnitudeCap * MagnitudeCap)
    {
        double const Inv = 1.0 / std::sqrt(SteerSq);
        float const UnitY = static_cast<float>(SteerVector.y * Inv);
        float const UnitZ = static_cast<float>(SteerVector.z * Inv);
        SteerVector.x = static_cast<float>(MagnitudeCap * (SteerVector.x * Inv));
        SteerVector.y = static_cast<float>(static_cast<double>(UnitY) * MagnitudeCap);
        SteerVector.z = static_cast<float>(static_cast<double>(UnitZ) * MagnitudeCap);
    }

    // 062d740a-062d7468: the bUpValid-gated extra term: SteerVector +=
    // FUN_062e2c00(fresh get_transform(), UpB0), each FADD FSTP'd to float.
    if (bUpValid != 0)
        SteerVector = Add(SteerVector, TransformVectorByMatrix(*IfaceOrientation(Sys), UpB0));

    // 062d746c-062d747e: bUpValid = 0 before FUN_062e8c20(Handle + 0x18, &SteerVector).
    bUpValid = 0;
    Sys->Thrust.SetAxisThrottle(SteerVector);

    // RETURN-POLARITY FIX, same pass. Retail (062d7483-062d7499):
    //   FLD [EBP+0xa8]; FCOMP [EBP+0x8c]; FNSTSW AX; TEST AH,0x5; JP LAB_062d6fca
    // LAB_062d6fca is `XOR EAX,EAX; ... RET 4`, i.e. return 0. TEST AH,0x5 masks
    // C0 (0x1) and C2 (0x4); JP fires on EVEN parity, i.e. neither set, i.e.
    // Accum >= Threshold -> RETURN 0. The fall-through (C0 set, Accum <
    // Threshold) returns 1. The port had this exactly backwards, reporting
    // "finished" while the residual steering angle was still large and
    // "still working" once it had converged.
    return (RemainingAngle < FacingTolerance) ? 1 : 0;
}
