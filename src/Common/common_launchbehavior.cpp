// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_launchbehavior.h"

#include <cmath>
#include <cstring>

// NavHelperPtr24 is a SystemManager* (see NestedNavQuery0xbc below for the proof
// chain); common_iface.h supplies IObjInspect / ObjInspectOf for the two vtable
// slots retail calls on SystemManager::ExternalIface from this file.
#include "common_systemmanager.h"
#include "common_iface.h"

// ComputeLaunchApproachTransform() needs Transform/Matrix and the 3DMathEngine
// singleton. DAT_063fc538's cached pointer at .text:062cdb9e-062cdbac is filled by
// `CALL FUN_06391cf0`, and FUN_06391cf0's whole body (.text:06391cf0-06391d11) is
//     SUB ESP,0x10 / [ESP+8]="3DMathEngine" / [ESP+0xc]=0 / [ESP+4]=0xc
//     CALL dword ptr [->DACOM.DLL::DACOM_Acquire]
// -- i.e. it is the SAME acquire already ported as Acquire3DMathEngine(), the one
// AnimDB::Update uses at .text:0627e162 to fill its own cache DAT_063fc018.
#include "common_csys.h"   // Transform / Matrix / Acquire3DMathEngine
#include "x86math.h"       // I3DMathEngine::TransposeTransform, FVector, FMatrix
#include "common_ini.h"    // VecNormalize3 (FUN_062e1680) -- StartLaunchSequence
#include "common_aisteer.h" // ClampedDot3 (FUN_062e1630), GetObjectTypeOrZero (FUN_062e6200)
#include "common_cobjdb.h"  // IObjDB::GetZoneDb -- FetchSystemId
#include "common_universe.h" // Universe::ISystem::GetId -- FetchSystemId

// FUN_062e74a0 (item 1798) -- ApplyDirective's checked id lookup -- is
// SystemManagerTimerTable::FindVerifiedObject (common_sysmgr_timertable.cpp).

// FUN_062ce320 (.text:062ce320-062ce35d, thiscall, RET 4): rotates the (x, z) pair of each
// of the three rows of a 3x3 basis by Angle, in place. FLD float Angle / FCOS, FLD Angle /
// FSIN, then per row: x' = x*cos - sin*z (FLD ST0 / FMUL ST3 / FLD ST2 / FMUL [z] / FSUBP),
// z' = cos*z + x*sin with the ORIGINAL x (FLD ST2 / FMUL [z] / FXCH / FMUL ST2 / FADDP); both
// stored as float, the products and the sums stay on the FPU stack (double here). Update()
// case 5 and StartLaunchSequence() inline the identical loop with the double constant
// DAT_0639e2d0 (== the float pi 0x40490fdb widened), so they share this body.
static void RotateBasisRowsXZ(Vector* Rows, double Angle)
{
    double const Cos = std::cos(static_cast<double>(Angle));
    double const Sin = std::sin(static_cast<double>(Angle));
    for (int Row = 0; Row < 3; ++Row)
    {
        double const X = Rows[Row].x;
        double const Z = Rows[Row].z;
        Rows[Row].x = static_cast<float>(X * Cos - Sin * Z);
        Rows[Row].z = static_cast<float>(Cos * Z + X * Sin);
    }
}

// LaunchBehavior -- see common_launchbehavior.h for the full RTTI/layout
// writeup this pass relied on. Ported from EXE/Disasm/Common.dll.txt (raw
// disassembly is authoritative in this module over Ghidra's Common.dll.c
// decompilation -- see this marathon's "ground truth precedence" rule).

// ----------------------------------------------------------------------------
// FUN_062cd640 (Common.dll.txt 202851-202895) -- constructor. Calls the
// shared base-init field-initialising FUN_062d2020 (it writes +0x04..+0x29, see Behavior in common_behavior.h) first, then default-constructs
// SubObjectA/B for real (MoveIdea/FaceIdea, identified 2026-09-11
// d285:common-port -- see common_idea.h), zeroes the confirmed fields, and
// sets the two float thresholds.
LaunchBehavior::LaunchBehavior(int InTypeId)
    : Behavior()
    , TypeId(InTypeId)
    , CachedHandle08(0)
    , ModeSelector0xc(0)
    , UpdateResult10(0)
    , ThreatSourcePtr14(nullptr)
    , Field18(nullptr)
    , DeltaTime1c(0.0f)
    , BehaviorMgrPtr20(nullptr)
    , NavHelperPtr24(nullptr)
    , Flags28(0)
    , Flag29(0)
    , Pad2a{}
    , SubObjectA()
    , SubObjectB()
    , ModeState164(0)
    , Watcher168()
    , CachedSystemId170(0)
    , TargetType174(0)
    , DirectiveField178(0)
    , Flag17c(0)
    , Pad17d{}
    , Field180(3.5f)
    , Field184(0.0f)
    , Field188(0.0f)
    , Field18c(0.0f)
    , Field190{}
    , Field19c{}
    , LaunchBasis{}
    , Field1cc{}
    , ApproachBasis2{}
    , Field1fc{}
    , ApproachBasis3{}
    , Field22c(15.0f)
    , Flag230(0)
    , Flag231(0)
{
    ConstructBase(InTypeId);   // FUN_062d2020 -- Behavior's own field initialisation
    // SubObjectA/B are IDENTIFIED (2026-09-11, d285:common-port) as real
    // MoveIdea/FaceIdea (common_idea.h) -- their own real constructors
    // (FUN_062d4e60/FUN_062d6750) now run for real via the member-
    // initializer list above (`SubObjectA()`/`SubObjectB()`), matching
    // retail's own `CALL FUN_062d4e60`/`CALL FUN_062d6750` with ECX =
    // &SubObjectA/&SubObjectB exactly -- no separate Construct* stand-in call
    // needed anymore.
}

// FUN_062cd6e0 (Common.dll.txt 202899-202916) is the scalar-deleting-dtor
// thunk: calls FUN_062cd700 (the real cleanup body) then conditionally
// frees. FUN_062cd700 (202920-202966) genuinely clears three BaseWatcher-
// shaped fields (+0x168, +0xb8, +0x40) then tail-calls the shared
// FUN_062d2080(this,-1). WIRED FOR REAL 2026-09-11 (d285:common-port): +0xb8
// and +0x40 are EXACTLY SubObjectB::Watcher (FaceIdea+0x14 == 0xa4+0x14 ==
// 0xb8) and SubObjectA::Watcher (MoveIdea+0x14 == 0x2c+0x14 == 0x40) --
// independent corroboration of the MoveIdea/FaceIdea identification (see
// common_idea.h), now that both sub-objects expose a real `Watcher` field.
LaunchBehavior::~LaunchBehavior()
{
    // Order re-verified from Common.dll.txt 202934-202956: +0x168, then
    // +0xb8 (SubObjectB), then +0x40 (SubObjectA).
    Watcher168.set(nullptr);
    SubObjectB.Watcher.set(nullptr);
    SubObjectA.Watcher.set(nullptr);

    // Retail then calls FUN_062d2080 (~Behavior, .text:062cd778): the implicit base
    // destructor call after this body.
}

// Own vtable slot 1 (offset 0x04). Common.dll.txt 202969-203032
// (LAB_062cd790), 45 instructions.
bool LaunchBehavior::SetContext(void* Param1, void* Param2, void* Param3)
{
    // 062cd7a6: FUN_062d2090 = Behavior::BindContext; false -> return it.
    if (!BindContext(Param1, Param2, Param3))
        return false;

    // WIRED FOR REAL (2026-09-11, d285:common-port): retail calls SubObjectA's
    // own vtbl slot 0 here -- re-read directly from Common.dll.txt 202985-
    // 203005 byte-for-byte: both SubObjectA's and SubObjectB's calls push the
    // SAME 3 arguments, `(ThreatSourcePtr14, NavHelperPtr24, Param2)` -- Param2
    // is this function's own raw incoming 2nd parameter, captured into a
    // register BEFORE the FUN_062d2090 call above (same pattern as
    // DockBehavior::SetContext, common_dockbehavior.cpp).
    SubObjectA.SetContext(ThreatSourcePtr14, NavHelperPtr24, Param2);

    // 062cd7c9: FUN_062d5980(&SubObjectA, 0) = MoveIdea::SetMode(0).
    ReleaseApproachStep(0);

    SubObjectB.SetContext(ThreatSourcePtr14, NavHelperPtr24, Param2);

    // CORRECTED 2026-09-17 (claude:w16-behavior). Retail, Common.dll.txt
    // 203007-203020:
    //   EAX = [ESI+0x24]; EAX += 0x14fc; EAX = [EAX+0x84]
    //   EDX = [EAX]; ECX = EAX; CALL dword ptr [EDX+0xbc]
    //   TEST AL,AL; JZ LAB_062cd814
    //   MOV dword ptr [ESI+0x18c],0x41700000   ; 15.0f, predicate true
    //   LAB_062cd814:
    //   MOV dword ptr [ESI+0x18c],0x42700000   ; 60.0f, predicate false
    // NO LONGER A GAP AT ALL as of 2026-09-17 (claude:w18-behavior). The TODO that
    // stood here -- "identify the +0x14fc/+0x84 pointee's class so
    // NestedNavQuery0xbc() can return the real answer" -- rested on a misreading of
    // the addressing: there is no pointee to identify, because there is no second
    // dereference. NestedNavQuery0xbc() now returns the real answer
    // (IObjInspect::is_player() on SystemManager::ExternalIface); see its definition
    // below for the instruction-level disproof.
    //
    // So this line now means what retail means: a player-flown ship gets a 15-second
    // launch timeout and an NPC gets 60. The 2026-09-17 (claude:w16-behavior) fix that
    // made the store unconditional was right and is unchanged -- only the predicate
    // was standing in.
    Field18c = NestedNavQuery0xbc() ? 15.0f : 60.0f;

    return true;
}

// Own vtable slot 3 (offset 0x0c). Common.dll.txt 203040-203079
// (FUN_062cd830), 30 instructions.
void LaunchBehavior::Engage()
{
    // .text:062cd833: `CALL FUN_062d20d0` -- Behavior's own slot 3 body.
    Behavior::Slot3();

    // .text:062cd838-062cd846: the current system's id through the manager's object database.
    CachedSystemId170 = FetchSystemId();

    // .text:062cd84c-062cd871: `EAX = Watcher168 target; EAX = EAX ? EAX-8 : 0;
    // CALL FUN_062e6200(EAX)` (== GetObjectTypeOrZero, which null-checks its argument itself).
    Watchable const* Watched168 = Watcher168.target();
    void const* RawBlock = Watched168 ? (reinterpret_cast<unsigned char const*>(Watched168) - 8) : nullptr;
    TargetType174 = GetObjectTypeOrZero(ObjInspectOf(RawBlock));

    // .text:062cd863-062cd877: `MOV ECX,[ESI+0x24]; PUSH 0; ADD ECX,0x12c;
    // CALL FUN_062e9a60` -- GunGroups.SetShieldActive (item 1832).
    ((SystemManager*)NavHelperPtr24)->GunGroups.SetShieldActive(0);

    ModeState164 = 1;
    Flag17c = 0;
    Field184 = 0.0f;
    Field188 = 0.0f;

    // .text:062cd87c-062cd8a7: `CALL FUN_062d21d0(this, 0, 3)` -- Behavior::SetCruiseLock.
    SetCruiseLock(0, 3);
}

// Own vtable slot 4 (offset 0x10). Common.dll.txt 203083-203112
// (FUN_062cd8b0), 30 instructions.
void LaunchBehavior::Disengage()
{
    // WIRED FOR REAL (2026-09-11, d285:common-port): retail calls SubObjectA's
    // and SubObjectB's own vtbl slot 4 (offset 0x10, "release"-shaped, i.e.
    // BaseIdea::Commit()) here, in that order -- re-verified from
    // Common.dll.txt 203085-203092.
    ReleaseSubObject(&SubObjectA);
    ReleaseSubObject(&SubObjectB);

    // .text:062cd8cb-062cd8cd: `MOV ECX,ESI; CALL FUN_062d20f0` -- Behavior's own slot 4 body
    // (PathfindManager::Field_064 = 0, Flag29 = 0).
    Behavior::Slot4();

    // .text:062cd8d2-062cd8dd: `MOV ECX,[ESI+0x24]; PUSH 1; ADD ECX,0x12c;
    // CALL FUN_062e9a60` -- GunGroups.SetShieldActive.
    ((SystemManager*)NavHelperPtr24)->GunGroups.SetShieldActive(1);

    // .text:062cd8e2-062cd8e8: `CALL FUN_062d21d0(this, 1, 3)` -- Behavior::SetCruiseLock.
    SetCruiseLock(1, 3);

    Flags28 = static_cast<unsigned char>((Flags28 & ~0x2) | 0x1);

    // .text:062cd8f0-062cd8fe: `MOV ECX,[ESI+0x20]; PUSH 0; PUSH 8; CALL FUN_062d29a0`.
    static_cast<IBehaviorManager*>(BehaviorMgrPtr20)->SetDirectiveResult(8, 0);
}

// Own vtable slot 5 (offset 0x14). Common.dll.txt 203124-203147
// (LAB_062cd910), 34 instructions, fully self-contained.
int LaunchBehavior::SetModeFromFlags()
{
    int Mode = (Flags28 & 0x2) ? 0 : 2;
    if (UpdateResult10 == 2 || UpdateResult10 == 3)
        Mode = 2;
    if (Flag29 != 0)
    {
        ModeSelector0xc = 3;
        return 3;
    }
    ModeSelector0xc = Mode;
    return Mode;
}

// Own vtable slot 10 (offset 0x28). Common.dll.txt 203650-203700
// (FUN_062cdf30), 46 instructions.
bool LaunchBehavior::ApplyDirective(void const* Param)
{
    // .text:062cdf38-062cdf59: `FUN_062e74a0(NavHelperPtr24+0x34, Param+0xc)`, then
    // EAX = found ? found+8 : 0; `CMP EAX,[EDI]; JZ; set_pointer(EAX)` == BaseWatcher::set.
    void* Found = FindLaunchHardpoint(NavHelperPtr24, Param);
    void* Adjusted = Found ? (static_cast<unsigned char*>(Found) + 8) : nullptr;
    Watchable const* AdjustedWatchable = static_cast<Watchable const*>(Adjusted);

    if (AdjustedWatchable != Watcher168.target())
        Watcher168.set(AdjustedWatchable);

    // retail: `ECX = Param->+0x10; this->DirectiveField178 = ECX;`
    DirectiveField178 = *reinterpret_cast<int const*>(static_cast<unsigned char const*>(Param) + 0x10);

    Watchable const* Watched168 = Watcher168.target();
    void const* RawBlock = Watched168 ? (reinterpret_cast<unsigned char const*>(Watched168) - 8) : nullptr;

    if (RawBlock != nullptr)
    {
        Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
        UpdateResult10 = 1;
        return true;
    }

    Flags28 = static_cast<unsigned char>(Flags28 & ~0x2);
    return false;
}

// Own vtable slot 7 (offset 0x1c). Common.dll.txt 203151-203638
// (LAB_062cd950), 457 instructions. GAP:AI-LAUNCH-STATE-MACHINE.
//
// [2026-09-11 d277:common-port] Hand-traced directly from the raw bytes (no
// Ghidra decompile exists for this function at all). Full retail shape:
//
// [2026-09-11 d312:common-port] UPDATE: re-verified case 4/5/6's own payload
// call sites against the raw bytes one more time (Common.dll.txt 203388-
// 203620) and wired 6 of the previously-deferred payload helpers for real --
// ReleaseApproachStep/SetApproachOffset/PushApproachBasisToSubObjectB/
// SetApproachMode/SetHardpointCone/AdjustNavHelperThrottle (see each helper's
// own header/impl comment for the byte-exact evidence). No control-flow
// change from the below -- only what each named helper now DOES internally.
//
// [2026-09-17 claude:w16-behavior] THREE CORRECTIONS, each from re-reading the
// raw bytes rather than trusting the two prior passes' own "re-verified" notes:
//   (a) the Field188/Field18c entry gate was INVERTED (203186-203191);
//   (b) case 1's Field184/Field180 gate was INVERTED (203203-203209);
//   (c) the pi-rotation's sin() term is -8.742278e-08, not 0 (203572-203590,
//       DAT_0639e2d0 is FLOAT pi widened to double, so it overshoots true pi).
// (a) and (b) are the same mistake twice: `TEST AH,0x41` + `JZ`/`JNZ` names the
// STRICTLY-GREATER case in the fall-through, not the `<=` case. Both had been
// transcribed as `<=`, which made the launch abort on tick 2 and skip mode 1's
// 3.5-second dwell entirely. See each site's own inline comment.
// A fourth correction lives in SetContext(): retail's Field18c store was being
// skipped along with its deferred predicate.
//
//   if (ThreatSourcePtr14==nullptr || Watcher168.target()==nullptr ||
//       Watcher168.target()-8==nullptr) { UpdateResult10=3; return 3; }
//   void* fresh = Field18->vtbl[2]()->vtbl[0]();   // SAME chain as Engage()'s
//                                                   // own FetchCachedResult()
//   if (CachedResultPtr170 != fresh) { UpdateResult10=2; return 2; }
//   if (Field188 > Field18c) { UpdateResult10=3; return 3; }   // launch timed out
//   Field188 += DeltaTime1c;
//   switch (ModeState164) { case 1..6: ...; default: UpdateResult10=0; return 0; }
//
// (the raw JA/JNZ byte-targets for EVERY "out of range" and "abort mid-case"
// branch were individually resolved this pass and ALL land on the same
// 062cdc79 plain tail -- `UpdateResult10=0; return 0;`, ModeState164 left
// untouched -- confirmed by computing each relative-jump's absolute target
// by hand; Ghidra's listing displays that address with the SAME label text
// as the real case-6 entry point at 062cdea8, which is a separate address
// and a genuinely different body -- an easy trap this pass avoided by
// checking raw bytes, not the display text).
//
// Case bodies (ModeState164 value before the switch):
//   1: `if (Field184>Field180) ModeState164=2; Field184+=DeltaTime1c;
//       UpdateResult10=0; return 0;` -- fully self-contained, ported for real.
//      (Dwell in mode 1 until Field180 == 3.5f seconds have accumulated.)
//   2: computes an "approach transform" via ComputeLaunchApproachTransform()
//      (FULLPORT-DEFERRED, see that method's own declaration comment), then
//      ModeState164=3 and falls straight into case 3's OWN body in the same
//      call (confirmed: no RET between them in the raw bytes).
//   3: `if (DirectiveField178>=0) { StartLaunchSequence(); ModeState164=4;
//       return 0; } else if (==-1) { ModeState164=2; return 0; }
//       else if (==-2) { Disengage(); UpdateResult10=3; return 3; }
//       else { UpdateResult10=0; return 0; }` -- fully self-contained aside
//      from StartLaunchSequence() itself; ported for real.
//   4: `if (Flag17c) { SetApproachSpeedParam(25); ReleaseApproachStep(0);
//       ModeState164=5; return 0; } else { <SubObjectA/B tick sequence>;
//       if (TickSubObject(SubObjectA)!=1) return 0; SetApproachSpeedParam(15);
//       ModeState164=5; return 0; }` -- control flow ported for real, sub-
//      object payload calls FULLPORT-DEFERRED.
//   5: SubObjectA/B tick + a nested-position dot-product gate (CONFIRMED pure
//      FPU math, ported for real -- see inline comment below) that can bail
//      early; otherwise releases SubObjectA, resumes input, optionally
//      throttles (Flag230), then either rotates LaunchBasis by pi radians
//      (Flag231 set -- CONFIRMED pure FPU math, ported for real) and moves to
//      mode 6, or sets Flags28 bits and returns 2 staying in mode 5.
//   6: `SetHardpointCone(pi/8); if (TickSubObject(SubObjectB)!=1) return 0;
//       Flags28|=1; Flags28&=~2; UpdateResult10=2; return 2;` -- control flow
//      ported for real, sub-object payload calls FULLPORT-DEFERRED.
int LaunchBehavior::Update()
{
    if (ThreatSourcePtr14 == nullptr)
    {
        UpdateResult10 = 3;
        return 3;
    }
    Watchable const* Watched168 = Watcher168.target();
    if (Watched168 == nullptr || (reinterpret_cast<unsigned char const*>(Watched168) - 8) == nullptr)
    {
        UpdateResult10 = 3;
        return 3;
    }

    // .text:062cd980-062cd994: the system id read the same way Engage() cached it.
    if (CachedSystemId170 != FetchSystemId())
    {
        UpdateResult10 = 2;
        return 2;
    }

    // CORRECTED 2026-09-17 (claude:w16-behavior): this test was INVERTED in the
    // prior transcription. Retail, Common.dll.txt 203186-203191:
    //   FLD  float ptr [EBX+0x188]      ; ST0 = Field188
    //   FCOMP float ptr [EBX+0x18c]     ; compare Field188 against Field18c, pop
    //   FNSTSW AX
    //   TEST AH,0x41                    ; C0 (0x01) | C3 (0x40)
    //   JZ   LAB_062cdefa               ; -> UpdateResult10 = 3; return 3
    // ZF is set exactly when neither C0 nor C3 is set, i.e. the strictly-greater
    // case, so the JZ (the abort) is taken for `Field188 > Field18c`. The port
    // previously read `Field188 <= Field18c`, which aborted the launch on the
    // very first tick (Field188 starts at 0 and Field18c is a 15s/60s timeout).
    if (Field188 > Field18c)
    {
        UpdateResult10 = 3;
        return 3;
    }
    Field188 += DeltaTime1c;

    switch (ModeState164)
    {
    case 1:
    {
        // CORRECTED 2026-09-17 (claude:w16-behavior): this test was INVERTED in
        // the prior transcription. Retail, Common.dll.txt 203203-203209:
        //   FLD   float ptr [EBX+0x184]   ; ST0 = Field184
        //   FCOMP float ptr [EBX+0x180]   ; compare Field184 against Field180, pop
        //   FNSTSW AX
        //   TEST  AH,0x41                 ; C0 (0x01) | C3 (0x40)
        //   JNZ   LAB_062cda0c            ; -> SKIP the ModeState164 = 2 store
        // The JNZ (skip) is taken for `less` (C0) or `equal` (C3); the
        // FALL-THROUGH that performs `MOV dword ptr [EBX+0x164],0x2` is the
        // strictly-greater case. So the store happens on `Field184 > Field180`.
        // The port previously read `Field184 <= Field180`, which advanced out of
        // mode 1 immediately instead of after Field180 (== 3.5f, ctor) seconds.
        if (Field184 > Field180)
            ModeState164 = 2;
        Field184 += DeltaTime1c;
        UpdateResult10 = 0;
        return 0;
    }
    case 2:
    {
        // .text:062cda2f-062cdbdd. NO LONGER A GAP as of 2026-09-17
        // (claude:w20-updates): the whole payload is ported -- see
        // ComputeLaunchApproachTransform() below for the ESP-depth derivation of
        // every Transform out-buffer, which is the one thing the previous pass
        // deliberately left unwritten.
        ComputeLaunchApproachTransform();
        // 062cdbdd `MOV dword ptr [EBX+0x164],EBP` with EBP == 3 (preloaded at
        // 062cd9e3). Reached both by falling out of the payload AND by the
        // get_hardpoint-success `JNZ LAB_062cdbdd` at 062cdb1a, which is why the
        // early return inside the helper still lands here. Retail then falls
        // straight through into case 3 -- there is no jump at 062cdbe3.
        ModeState164 = 3;
        [[fallthrough]];
    }
    case 3:
    {
        if (DirectiveField178 >= 0)
        {
            // retail: `CALL FUN_062cdfa0(this)` -- FULLPORT-DEFERRED.
            StartLaunchSequence();
            ModeState164 = 4;
            UpdateResult10 = 0;
            return 0;
        }
        if (DirectiveField178 == -1)
        {
            ModeState164 = 2;
            UpdateResult10 = 0;
            return 0;
        }
        if (DirectiveField178 == -2)
        {
            // retail: `CALL this->vtbl[4]()` -- own vtable slot 4 = Disengage().
            Disengage();
            UpdateResult10 = 3;
            return 3;
        }
        UpdateResult10 = 0;
        return 0;
    }
    case 4:
    {
        if (Flag17c != 0)
        {
            SetApproachSpeedParam(25.0f);
            ReleaseApproachStep(0);
            ModeState164 = 5;
            UpdateResult10 = 0;
            return 0;
        }

        PushApproachBasisToSubObjectB(&ApproachBasis2[0]);
        TickSubObject(&SubObjectB, DeltaTime1c);
        SetApproachOffset(&Field1cc);
        SetApproachSpeedParam(5.0f);
        ReleaseApproachStep(1);
        SetApproachThrottleScale(Field22c);
        SetApproachMode(2);
        int TickResult = TickSubObject(&SubObjectA, DeltaTime1c);
        if (TickResult != 1)
        {
            UpdateResult10 = 0;
            return 0;
        }
        SetApproachSpeedParam(15.0f);
        ModeState164 = 5;
        UpdateResult10 = 0;
        return 0;
    }
    case 5:
    {
        PushApproachBasisToSubObjectB(&LaunchBasis[0]);
        TickSubObject(&SubObjectB, DeltaTime1c);
        SetApproachOffset(&Field19c);
        SetApproachMode(2);
        int TickResult = TickSubObject(&SubObjectA, DeltaTime1c);

        // .text:062cdd4f-062cdd83: Delta1 = Field19c - Field1cc, each component FSTP'd to a
        // float stack slot (ESP+0x10/0x14/0x18), interleaved with the cache-latch test.
        float const Delta1X = Field19c.x - Field1cc.x;
        float const Delta1Y = Field19c.y - Field1cc.y;
        float const Delta1Z = Field19c.z - Field1cc.z;

        // .text:062cdd57-062cdd9d: ESI = SystemManager+0x14fc; if Flags1578[2] (+0x7e) is
        // clear, the inlined FUN_062e6d40 cache refresh runs (latch, get_physical_radius).
        SystemManager* const Sys = static_cast<SystemManager*>(NavHelperPtr24);
        if (Sys->SelfTarget.bPosValid == 0)
            EnsureNestedNavPositionCached();

        // .text:062cdda0-062cddb5: Delta2 = CachedPhysicalCenter - Field19c is loaded onto the
        // FPU stack unconditionally and never stored (double here); `CMP EAX,1 / JZ` on the
        // SubObjectA tick result then skips the dot test entirely (the three values are popped
        // at 062cdde5-062cdde9).
        double const Delta2X = static_cast<double>(Sys->SelfTarget.CachedPos.x) - Field19c.x;
        double const Delta2Y = static_cast<double>(Sys->SelfTarget.CachedPos.y) - Field19c.y;
        double const Delta2Z = static_cast<double>(Sys->SelfTarget.CachedPos.z) - Field19c.z;
        if (TickResult != 1)
        {
            // .text:062cddba-062cddce: (d1.z*d2.z + d1.y*d2.y) + d1.x*d2.x on the FPU stack;
            // FCOMP 0.0f (DAT_06399408) / TEST AH,0x41 / JNZ -> return 0 when dot <= 0 or
            // unordered, i.e. continue only on a strictly positive dot.
            double const Dot = (Delta1Z * Delta2Z + Delta1Y * Delta2Y) + Delta1X * Delta2X;
            if (!(Dot > 0.0))
            {
                UpdateResult10 = 0;
                return 0;
            }
        }

        ReleaseSubObject(&SubObjectA);                                   // 062cddf0 SubObjectA slot 4
        // .text:062cddf3-062cddfe: FUN_062e9a60 = GunGroups.SetShieldActive(1).
        ((SystemManager*)NavHelperPtr24)->GunGroups.SetShieldActive(1);

        if (Flag230 != 0)
            AdjustNavHelperThrottle(0.5f);                                // 062cde0d-062cde1a

        if (Flag231 != 0)
        {
            // .text:062cde29-062cde6d: the pi rotation of LaunchBasis (EAX = EBX+0x1b0),
            // DAT_0639e2d0 == double 0x400921FB60000000 (the float pi widened).
            RotateBasisRowsXZ(LaunchBasis, 3.1415927410125732);

            ModeState164 = 6;
            UpdateResult10 = 0;
            return 0;
        }

        Flags28 = static_cast<unsigned char>((Flags28 & ~0x2) | 0x1);
        UpdateResult10 = 2;
        return 2;
    }
    case 6:
    {
        // retail: `PUSH 0x3ec90fdb` -- a compile-time float literal
        // (0.39269909f, pi/8), not loaded from a named .rdata symbol.
        SetHardpointCone(0.39269909f);
        PushApproachBasisToSubObjectB(&LaunchBasis[0]);
        int TickResult = TickSubObject(&SubObjectB, DeltaTime1c);
        if (TickResult != 1)
        {
            UpdateResult10 = 0;
            return 0;
        }
        Flags28 = static_cast<unsigned char>((Flags28 & ~0x2) | 0x1);
        UpdateResult10 = 2;
        return 2;
    }
    default:
        UpdateResult10 = 0;
        return 0;
    }
}

// ----------------------------------------------------------------------------
// FULLPORT-DEFERRED helpers -- see header comment and each call site above
// for the confirmed retail role of each. All not-yet-ported cross-cutting
// helpers shared with sibling classes elsewhere in this family.
// ----------------------------------------------------------------------------
// The +0x18 chain of Engage() (.text:062cd838-062cd844) and Update() (.text:062cd980-
// 062cd98c): `ECX = [this+0x18]; CALL [[ECX]+8]; ECX = EAX; CALL [[EAX]+0]`, no null tests.
// +0x18 is written by Behavior's own vtable slot 2 (Behavior::Slot2, the ICF-folded setter
// 0629afd0) from ConnectBehaviorManagerObjectDb (FUN_062d2830), which hands every behavior
// the manager's IObjDB; IObjDB slot 2 (+0x08) is GetZoneDb, the current Universe::ISystem,
// and ISystem slot 0 returns its id. So Update() aborts with 2 when the ship has changed
// systems since Engage().
unsigned LaunchBehavior::FetchSystemId() const
{
    Universe::ISystem const* const System = Field18->GetZoneDb();
    return System->GetId();
}

// .text:062cdf38-062cdf43: `ECX = [ESI+0x24] + 0x34; PUSH Param+0xc; CALL FUN_062e74a0` --
// the checked id lookup through the object database at SystemManager+0x34 (item 1798,
// SystemManagerTimerTable::FindVerifiedObject; same call FollowBehavior / FormationBehavior make).
void* LaunchBehavior::FindLaunchHardpoint(void* NavHelper, void const* Param)
{
    return static_cast<SystemManager*>(NavHelper)->Timers.FindVerifiedObject(
        (unsigned const*)(static_cast<unsigned char const*>(Param) + 0xc));
}

// ----------------------------------------------------------------------------
// SetContext()'s Field18c selector. SETTLED AND PORTED FOR REAL 2026-09-17
// (claude:w18-behavior), replacing the `return false` stub and its premise.
//
// THE PREMISE WAS TWICE WRONG. It described the chain as
// `[[NavHelperPtr24+0x14fc]+0x84]->vtbl[0xbc]()` -- a DOUBLE indirection through
// an "unidentified nested NavHelper sub-object". Reading .text:062cd7e5-062cd7f7
// byte for byte:
//     MOV EAX,dword ptr [ESI + 0x24]     ; NavHelperPtr24
//     ADD EAX,0x14fc
//     MOV EAX,dword ptr [EAX + 0x84]     ; ONE load, of +0x1580
//     MOV EDX,dword ptr [EAX]
//     MOV ECX,EAX
//     CALL dword ptr [EDX + 0xbc]
// `ADD EAX,0x14fc` followed by `MOV EAX,[EAX+0x84]` is a SINGLE load of
// NavHelperPtr24+0x1580 -- the 0x14fc is just how MSVC spelled the addend after
// reusing that base register. There is no nested object and nothing to identify:
// +0x1580 is SystemManager::ExternalIface, the only field at that offset in the
// module (common_systemmanager.h makes exactly this argument in its own
// FUN_062e92e0 writeup), and slot +0xbc is IObjInspect::is_player() [slot 47] in
// common_iface.h's RTTI-verified table -- a bare bool with no arguments, which
// matches this call site's zero pushes.
//
// NavHelperPtr24 being a SystemManager* is corroborated three more ways within
// this same class: AdjustNavHelperThrottle passes +0x18 (SystemManager::Thrust) to
// Behavior_UpdateCruiseGate; ResumeUserInput passes +0x12c; and
// EnsureNestedNavPositionCached below reads +0x14fc's +0xc/+0x18/+0x7e, which are
// CachedPhysicalCenter / CachedPhysicalRadius / Flags1578[2].
//
// FaceBehavior::Update (.text:062c8e0a and .text:062c92d8) contains the
// byte-identical chain and was ported against the same identification this pass.
//
// So the answer is: a PLAYER-flown ship gets Field18c = 15.0f (0x41700000) and an
// NPC gets 60.0f (0x42700000) -- a much shorter launch timeout for the player,
// which is what the gameplay would want.
bool LaunchBehavior::NestedNavQuery0xbc()
{
    // .text:062cd7e5-062cd7f7: [[+0x24] + 0x1580] slot 0xbc, no null test.
    SystemManager const* const Sys = static_cast<SystemManager const*>(NavHelperPtr24);
    return Sys->SelfTarget.Iface->is_player();
}

// ----------------------------------------------------------------------------
// Update() (vtable slot 7) payload helpers, GAP:AI-LAUNCH-STATE-MACHINE. See
// each one's header declaration comment for the retail FUN_ address / vtable
// slot it stands in for. Genuine remaining gaps left as documented no-ops;
// 6 of these are now WIRED FOR REAL (2026-09-11, d312:common-port) -- see
// each one's own comment below.
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// PORTED FOR REAL 2026-09-17 (claude:w20-updates). Retail: case 2's whole payload,
// .text:062cda2f-062cdbdd. The previous pass left this a no-op because "several
// ESP-relative Transform buffers overlap and are re-read at different offsets after
// further pushes"; the buffers do NOT in fact overlap, and here is the arithmetic
// that settles it, derived from the function's own prologue rather than assumed.
//
// Let B be ESP after LAB_062cd950's prologue (`SUB ESP,0xd0` then PUSH EBX/EBP/ESI/
// EDI). At .text:062cda2f, ESP == B. The six pushes of the get_dock_hardpoints call
// walk ESP down by 4 each, so each LEA has to be read at the depth it executes at:
//     062cda42  PUSH 0                             ESP = B-4
//     062cda44  LEA EDX,[ESP+0x60]  -> B+0x5c      ESP = B-8     (XformC)
//     062cda49  LEA EDX,[ESP+0x94]  -> B+0x8c      ESP = B-0xc   (XformB)
//     062cda51  LEA EDX,[ESP+0x38]  -> B+0x2c      ESP = B-0x10  (XformA)
//     062cda5c  PUSH 0 / PUSH [EBX+0x178]          ESP = B-0x18
//     062cda5f  CALL [EAX+0xc8]  (stdcall, 6 args) ESP = B
// Last push is the first argument, so the call is
//     get_dock_hardpoints(DirectiveField178, nullptr, &XformA, &XformB, &XformC, nullptr)
// with XformA at B+0x2c, XformB at B+0x8c, XformC at B+0x5c. sizeof(Transform)==0x30
// and its Vector sits at +0x24, which is exactly why the three discrete 3-dword
// stores read B+0x50 (A+0x24), B+0xb0 (B+0x24) and B+0x80 (C+0x24) while the three
// 9-dword REP MOVSDs read B+0x2c, B+0x8c and B+0x5c -- the matrices. B+0x2c..B+0x5b,
// B+0x5c..B+0x8b and B+0x8c..B+0xbb are three disjoint 0x30-byte slots; nothing
// overlaps. The retail store order, .text:062cda65-062cdb01, is
//     Field1fc <- A.Translation   ApproachBasis3 <- A.Rotation
//     Field1cc <- B.Translation   ApproachBasis2 <- B.Rotation
//     Field19c <- C.Translation   LaunchBasis    <- C.Rotation
// The receiver is ECX == Watcher168's target minus 8 (.text:062cda2f-062cda40), the
// same "-8 watcher back-adjustor to the owning IObjRW" the entry gate already tested.
//
// The hardpoint lookup at .text:062cdb03-062cdb12 is on ThreatSourcePtr14, not on the
// watcher target, and at that point ESP is B-4 (one live push) then B-8:
//     062cdaea  LEA EDX,[ESP+0xbc]  -> B+0xbc   (Matrix out)
//     062cdb08  LEA EDX,[ESP+0x24]  -> B+0x20   (Vector out)
//     062cdb0d  PUSH "hpmount"
// so get_hardpoint("hpmount", &Pos at B+0x20, &Orient at B+0xbc). `TEST EAX,EAX /
// JNZ LAB_062cdbdd` at 062cdb18 means a SUCCESSFUL lookup skips everything below and
// goes straight to the ModeState164 = 3 store -- the tail runs only on FAILURE.
//
// The tail, .text:062cdb20-062cdbd8, reuses the scratch triple at B+0x10..B+0x18
// twice, and that is the other thing that made it look tangled:
//     - B+0x10..B+0x18 first receives hardpointPos - CachedPhysicalCenter (the FSUBs
//       at 062cdb4a-062cdb7c read the hardpoint Pos back out of B+0x20);
//     - Field190 is stored from it (062cdb77-062cdb84);
//     - it is then passed as the Dst out-param of the vtbl+0x38 call
//       (062cdbb5 `LEA EDX,[ESP+0x18]` at ESP == B-8 -> B+0x10) while Field190 itself
//       is the Source, and Field190 is re-stored from it afterwards
//       (062cdbbe-062cdbd1).
// FUN_062d5a80 (item 1587, 9 instructions: three float stores into receiver+0x4c) is
// then called with ECX == EBX+0x2c == &SubObjectA, whose +0x4c is MoveIdea::Offset4c.
void LaunchBehavior::ComputeLaunchApproachTransform()
{
    // .text:062cda2f-062cda40: `EAX = [EBX+0x168]; if (EAX) ECX = EAX-8; else ECX = 0`,
    // then an unguarded `MOV EAX,[ECX]` (Update()'s entry gate has already proved both
    // halves non-null by the time case 2 runs).
    Watchable const* const Watched = Watcher168.target();
    void const* const Owner = reinterpret_cast<unsigned char const*>(Watched) - 8;

    // Retail leaves these three uninitialised too -- get_dock_hardpoints fills them.
    Transform XformA;
    Transform XformB;
    Transform XformC;
    ObjInspectOf(Owner)->get_dock_hardpoints(DirectiveField178, nullptr,
                                             &XformA, &XformB, &XformC, nullptr);

    Field1fc = XformA.Translation;                                   // 062cda65-062cda7c
    std::memcpy(ApproachBasis3, &XformA.Rotation, sizeof(ApproachBasis3));  // 062cda86-062cda95
    Field1cc = XformB.Translation;                                   // 062cda97-062cdab0
    std::memcpy(ApproachBasis2, &XformB.Rotation, sizeof(ApproachBasis2));  // 062cdac1-062cdad3
    Field19c = XformC.Translation;                                   // 062cdad5-062cdae7
    std::memcpy(LaunchBasis, &XformC.Rotation, sizeof(LaunchBasis));        // 062cdaf2-062cdb01

    Vector HardpointPos;
    Matrix HardpointOrient;
    if (ObjInspectOf(ThreatSourcePtr14)->get_hardpoint("hpmount", &HardpointPos,
                                                       &HardpointOrient) != 0)
        return;  // 062cdb18 `TEST EAX,EAX / JNZ LAB_062cdbdd` -- non-zero skips the tail

    SystemManager* const Sys = static_cast<SystemManager*>(NavHelperPtr24);

    // 062cdb23-062cdb47: byte-for-byte the same inlined cache refresh case 5 uses,
    // gated on the same latch (SystemManager+0x157a == Flags1578[2]).
    if (Sys->SelfTarget.bPosValid == 0)
        EnsureNestedNavPositionCached();

    // 062cdb4a-062cdb84
    Field190.x = HardpointPos.x - Sys->SelfTarget.CachedPos.x;
    Field190.y = HardpointPos.y - Sys->SelfTarget.CachedPos.y;
    Field190.z = HardpointPos.z - Sys->SelfTarget.CachedPos.z;

    // 062cdb87-062cdb9c: ExternalIface->vtbl[0x0c]. Slot +0x00C is
    // IObjInspect::get_transform() returning `Transform const&` (common_iface.h notes
    // it is ICF-folded with slot 4, get_orientation(), which is why its result can be
    // consumed as a bare Matrix -- Transform::Rotation is at offset 0).
    Transform const& OwnerXform = Sys->SelfTarget.Iface->get_transform();

    // 062cdb9e-062cdbac: the lazily cached DAT_063fc538 (FUN_06391cf0); the port's single
    // shared cache lives inside Acquire3DMathEngine (common_csys.cpp).
    I3DMathEngine* const Math = Acquire3DMathEngine();

    // 062cdbb1-062cdbbb, last push first: (Dst = B+0x10, Matrix = the transform above,
    // Source = &Field190). Slot +0x38 == I3DMathEngine::TransposeTransform, i.e. the
    // world-space hardpoint offset rotated INTO the owner's local frame.
    Vector Rotated = {0.0f, 0.0f, 0.0f};
    Math->TransposeTransform(reinterpret_cast<FVector&>(Rotated),
                             reinterpret_cast<FMatrix const&>(OwnerXform.Rotation),
                             reinterpret_cast<FVector const&>(Field190));
    Field190 = Rotated;  // 062cdbbe-062cdbd1

    // 062cdbd4-062cdbd8: FUN_062d5a80(&SubObjectA, &Field190) == item 1587's
    // three-float store into receiver+0x4c, which on a MoveIdea is Offset4c.
    SubObjectA.Offset4c = Field190;
}

// FUN_062cdfa0 (.text:062cdfa0-062ce2f0, item 1425), called only from Update() case 3
// (.text:062cdbef), which ignores the returned AL. Re-reads the dock hardpoints of the
// launch terminal and decides whether the approach is already straight enough to skip the
// alignment stage (Flag17c), then special-cases the terminal type.
//
// Stack map (B = ESP after SUB ESP,0xf4 and the four register pushes): the
// get_dock_hardpoints call at .text:062ce000 receives, last push first,
// (DirectiveField178, &Type = B+0x40, &XformA = B+0x74, &XformB = B+0xa4, &XformC = B+0xd4,
// null). The stores that follow (.text:062ce006-062ce0b2) are the same mapping as Update()
// case 2: Field1fc/ApproachBasis3 <- A, Field1cc/ApproachBasis2 <- B, Field19c/LaunchBasis <- C.
// Then (FUN_0628b030 = Vec3Assign of float differences, FUN_062e1680 = VecNormalize3(Out,
// Src), FUN_062e1630 = ClampedDot3, FUN_062887d0 = Vec3Add(Out, A, B)):
//   Dir1 (B+0x44) = normalize(Field1cc - Field1fc)                     062ce0b4-062ce0e5
//   Dir2 (B+0x68) = normalize(Field19c - Field1cc)                     062ce0ea-062ce122
//   Dot1 (B+0x38, FSTP float) = ClampedDot3(Dir1, Dir2)                062ce127-062ce136
//   Dot2 (B+0x10, FSTP float) = ClampedDot3(ApproachBasis2 column z, Dir1)  062ce13a-062ce162
//   Sum  (B+0x20) = ApproachBasis3 column z + ApproachBasis2 column z  062ce166-062ce1be
//   Dir3 (B+0x5c) = normalize(Sum + LaunchBasis column z)              062ce1c3-062ce1fc
//   Dot3 (left on the FPU stack) = ClampedDot3(Dir1, Dir3)             062ce201-062ce20b
// ("column z" = the +0x8/+0x14/+0x20 floats, i.e. Rows[0..2].z.)
// Flag17c = Dot1 > 0.999 && Dot2 > 0.99 && Dot3 > 0.9999 (doubles DAT_0639fd40 /
// DAT_0639dfe8 / DAT_0639fd38; each FCOMP + TEST AH,0x41 + JNZ takes the "clear" side on
// less, equal or unordered, so the store of 1 is the strictly-greater fall-through).
bool LaunchBehavior::StartLaunchSequence()
{
    // .text:062cdfa9-062cdfbc: watcher target, then target-8, both must be non-null.
    Watchable const* const Watched = Watcher168.target();
    if (Watched == nullptr || (reinterpret_cast<unsigned char const*>(Watched) - 8) == nullptr)
        return false;
    void const* const Owner = reinterpret_cast<unsigned char const*>(Watched) - 8;

    // Retail leaves all four out-buffers uninitialised; get_dock_hardpoints fills them.
    TERMINAL_TYPE Type;
    Transform XformA;
    Transform XformB;
    Transform XformC;
    ObjInspectOf(Owner)->get_dock_hardpoints(DirectiveField178, &Type,           // 062ce000
                                             &XformA, &XformB, &XformC, nullptr);
    Field1fc = XformA.Translation;                                               // 062ce015
    std::memcpy(ApproachBasis3, &XformA.Rotation, sizeof(ApproachBasis3));       // 062ce047
    Field1cc = XformB.Translation;                                               // 062ce04b
    std::memcpy(ApproachBasis2, &XformB.Rotation, sizeof(ApproachBasis2));       // 062ce07e
    Field19c = XformC.Translation;                                               // 062ce086
    std::memcpy(LaunchBasis, &XformC.Rotation, sizeof(LaunchBasis));             // 062ce0b2

    Vector Scratch;
    Scratch.x = Field1cc.x - Field1fc.x;                                         // 062ce0cf-062ce0d3
    Scratch.y = Field1cc.y - Field1fc.y;
    Scratch.z = Field1cc.z - Field1fc.z;
    Vector Dir1;
    VecNormalize3(&Scratch, &Dir1);                                              // 062ce0e5

    Scratch.x = Field19c.x - Field1cc.x;                                         // 062ce104-062ce110
    Scratch.y = Field19c.y - Field1cc.y;
    Scratch.z = Field19c.z - Field1cc.z;
    Vector Dir2;
    VecNormalize3(&Scratch, &Dir2);                                              // 062ce122

    float const Dot1 = ClampedDot3(Dir1, Dir2);                                  // 062ce131 / 062ce136

    Vector Basis2Z;
    Basis2Z.x = ApproachBasis2[0].z;                                             // 062ce13e-062ce158
    Basis2Z.y = ApproachBasis2[1].z;
    Basis2Z.z = ApproachBasis2[2].z;
    float const Dot2 = ClampedDot3(Basis2Z, Dir1);                               // 062ce15d / 062ce162

    Vector LaunchZ;
    LaunchZ.x = LaunchBasis[0].z;                                                // 062ce16a-062ce186
    LaunchZ.y = LaunchBasis[1].z;
    LaunchZ.z = LaunchBasis[2].z;
    Vector Basis3Z;
    Basis3Z.x = ApproachBasis3[0].z;                                             // 062ce1a8-062ce1ba
    Basis3Z.y = ApproachBasis3[1].z;
    Basis3Z.z = ApproachBasis3[2].z;
    Vector Sum;
    Sum.x = Basis3Z.x + Basis2Z.x;                                               // 062ce1be (Vec3Add)
    Sum.y = Basis3Z.y + Basis2Z.y;
    Sum.z = Basis3Z.z + Basis2Z.z;

    Scratch.x = Sum.x + LaunchZ.x;                                               // 062ce1e2-062ce1ea
    Scratch.y = Sum.y + LaunchZ.y;
    Scratch.z = Sum.z + LaunchZ.z;
    Vector Dir3;
    VecNormalize3(&Scratch, &Dir3);                                              // 062ce1fc

    double const Dot3 = ClampedDot3(Dir1, Dir3);                                 // 062ce20b, kept on ST0

    double const StraightDot1 = 0.999;   // DAT_0639fd40
    double const StraightDot2 = 0.99;    // DAT_0639dfe8
    double const StraightDot3 = 0.9999;  // DAT_0639fd38
    if (Dot1 > StraightDot1 && Dot2 > StraightDot2 && Dot3 > StraightDot3)       // 062ce210-062ce240
        Flag17c = 1;                                                             // 062ce242
    else
        Flag17c = 0;                                                             // 062ce24d

    // .text:062ce254-062ce262: switch (Type - 1), unsigned `CMP EAX,7 / JA` -> plain return.
    switch (static_cast<int>(Type))
    {
    case 1:
        Flag230 = 1;                                                             // 062ce269
        [[fallthrough]];
    case 6:
    case 7:
    case 8:
        // .text:062ce270-062ce2ad: the pi rotation, inlined, on ApproachBasis3 (EDI ==
        // EBP+0x208 since 062ce16d, EAX = EDI+8).
        RotateBasisRowsXZ(ApproachBasis3, 3.1415927410125732);                  // DAT_0639e2d0
        // .text:062ce2af-062ce2c4: FUN_062ce320(pi) on ESI == &ApproachBasis2 and on
        // EBX == &LaunchBasis (both pointers loaded at 062ce13a / 062ce166).
        RotateBasisRowsXZ(ApproachBasis2, static_cast<double>(3.14159274f));     // PUSH 0x40490fdb
        RotateBasisRowsXZ(LaunchBasis, static_cast<double>(3.14159274f));        // PUSH 0x40490fdb
        return true;
    case 2:
    case 3:
    case 4:
    case 5:
        Flag17c = 0;                                                             // 062ce2d6
        Flag231 = 1;                                                             // 062ce2dd
        return true;
    default:
        return true;                                                             // 062ce2e4
    }
}
// ----------------------------------------------------------------------------
// PORTED FOR REAL 2026-09-17 (claude:w18-behavior). The recorded reason --
// "ICF-mislabeled `DeviceTRC::bind_to_dc`, genuinely unresolved anywhere else in
// this port" -- was stale: common_idea.h already names this exact fold.
//
// The three call sites (.text:062cdcc4, 062cdd00 and case 4's, all with
// ECX == EBX+0x2c == &SubObjectA) resolve to .text:062a66b0, whose ENTIRE body is
//     MOV EAX,dword ptr [ESP+0x4]
//     MOV dword ptr [ECX+0x3c],EAX
//     RET 0x4
// -- a two-instruction dword store, the same class of ICF-folded trivial setter
// the 2026-09-11 pass already wired for FUN_062d88b0 and FUN_062d93c0. Ghidra
// labels 062a66b0 `CmnAsteroid::CAsteroidCube::set_size` (21 XREFs, the surviving
// copy of a fold), and `DeviceTRC::bind_to_dc` is a THIRD name for the same
// bytes -- neither has anything to do with this receiver. SubObjectA is a
// confirmed MoveIdea and +0x3c is its ArrivalRange, for which common_idea.h
// declares MoveIdea::SetRange3c(float) and explicitly records the fold address
// ".text:062a66b0 (= DeviceTRC::bind_to_dc)".
//
// NOTE ON THE NAME: this parameter is a RANGE, not a speed -- retail's constants
// at these sites (25.0f / 5.0f / 15.0f, i.e. 0x41c80000 / 0x40a00000 / 0x41700000)
// are approach distances in metres, and the field's own constructor default is
// 200.0f. The method name is left alone because other lanes' call sites use it.
void LaunchBehavior::SetApproachSpeedParam(float Value)
{
    SubObjectA.SetRange3c(Value);
}

// FUN_062d5980(SubObjectA, Flag) -- MoveIdea::SetMode (SubObjectA is the MoveIdea at +0x2c).
void LaunchBehavior::ReleaseApproachStep(int Flag)
{
    SubObjectA.SetMode(Flag);
}

// FUN_062d5840(SubObjectA, &Vector) -- MoveIdea::SetPosition.
void LaunchBehavior::SetApproachOffset(void const* OffsetVector)
{
    SubObjectA.SetPosition(*static_cast<Vector const*>(OffsetVector));
}

// FUN_062d75c0(this=SubObjectB, param1=SrcVectorBlock). Re-read directly from
// raw bytes this pass (Common.dll.txt 219363-219378):
//   this->+0xa0 = 4;
//   memcpy(this->+0x34, *param1, 36);   // 9-dword REP MOVSD, SrcVectorBlock -> this
// this->+0x34 (9 dwords) and this->+0xa0 (1 dword) land exactly on FaceIdea's
// own confirmed ZeroBlock34[9] / FieldA0 fields (common_idea.h). CORRECTED
// data-flow direction from this method's prior "FetchSubObjectPosition" name
// (the prior stub's OWN effect -- a no-op -- was never wrong, only its
// documented direction): retail pushes the caller's own approach-basis
// vectors INTO FaceIdea's internal cache, not the reverse.
void LaunchBehavior::PushApproachBasisToSubObjectB(void const* SrcVectorBlock)
{
    SubObjectB.TargetKind = 4;
    std::memcpy(SubObjectB.ZeroBlock34, SrcVectorBlock, sizeof(SubObjectB.ZeroBlock34));
}

// ----------------------------------------------------------------------------
// FUN_062d5a20 (decomp-tracker item 1585), .text:062d5a20-062d5a62, 26 insns.
// PORTED FOR REAL 2026-09-17 (claude:w18-behavior).
//
// ITEM 1585's `blocked` REASON IS STALE. It reads: "the blocker is that Field04's
// real interface/class is not identified anywhere in this port -- calling through
// an unnamed vtable slot with an unknown calling convention/return type would be
// guessing". Both halves have since been answered by other lanes:
//   * Field04 is BaseIdea::Object (common_idea.h) -- the receiver's own IObjRW,
//     whose IObjInspect sub-object sits at offset 0. Not unidentified.
//   * Slot 0xd4 is IObjInspect::get_projected_throttle(float, ThrustEquipType),
//     slot 53 of the RTTI-rebuilt table in common_iface.h (identified 2026-09-16,
//     cross-DLL ABI pass 30), and the trampoline FUN_062e5b70 is itself a
//     complete port -- Behavior_InvokeManagerSlotD4, item 1765, common_behavior.h,
//     whose own doc comment names THIS function as one of its two call sites.
// Nothing is being guessed here. (Item 1585 is its own tracker item and belongs
// to whoever owns FormationMoveFx; what is captured below is this call site's
// semantics on a confirmed MoveIdea receiver, the same convention the 2026-09-11
// pass used for FUN_062d88b0 and FUN_062d93c0.)
//
// Retail body, ECX == &SubObjectA at .text:062cdcdb:
//     EAX = [ESI+0x34]                      ; MoveIdea::Mode
//     switch: 0 -> 1, 1 -> 3, 2 -> 4, default -> param_1's raw dword
//     ECX = [ESI+0x4]                       ; BaseIdea::Object
//     PUSH selector / PUSH param_1 / PUSH ECX   ; last push first => (Obj, Value, Selector)
//     CALL FUN_062e5b70
//     FSTP [ESI+0x40]                       ; MoveIdea::Throttle
//
// The selector ladder is the confirmation that this reading is right: 1/3/4 are
// exactly ThrustEquipEngine / ThrustEquipTradelane / ThrustEquipCruise
// (common_cequip.h), so MoveIdea::Mode is choosing WHICH propulsion system's
// projected throttle to ask for. A coincidence would not land on three meaningful
// enumerators in order.
//
// THE `default` ARM IS NOT REPRODUCED, DELIBERATELY: it forwards param_1's raw
// float BIT PATTERN as the ThrustEquipType enum. It is unreachable -- MoveIdea::Mode
// is only ever written by MoveIdea::SetMode, which stores 0, 1 or 2 (common_idea.h),
// and by the ctor, which zeroes it. Passing arbitrary bits as an enum to model a
// dead branch would be worse than documenting it.
// SUPERSEDED 2026-09-17 (claude:orch-dockpreamble): item 1585 is no longer carried
// inline here. FUN_062d5a20 is now a real method on its real receiver --
// MoveIdea::SetThrottleScale (common_idea.h/.cpp) -- because a SECOND call site
// turned up, DockBehavior::AdvancePreamble at .text:062c48ca, and one function
// transcribed into two behaviour classes is the duplication this module keeps
// paying for. The derivation above stands; it just lives with the class now.
//
// The name also changes with it. `SetApproachRadius(float Radius)` was wrong about
// what the value IS: it is not a radius, it is the throttle scale that
// get_projected_throttle is asked to project. Field22c is passed to it, and this
// header's own comment on Field22c inherited the same misreading.
void LaunchBehavior::SetApproachThrottleScale(float Scale)
{
    SubObjectA.SetThrottleScale(Scale);
}

// FUN_062d88b0(SubObjectA, Mode) -- decomp-tracker item 1620 confirms this
// function's entire body is `*(uint*)(this+0x44) = param`. Offset +0x44 lands
// exactly on MoveIdea's own confirmed StopStyle (formerly Field44, common_idea.h).
void LaunchBehavior::SetApproachMode(int Mode)
{
    SubObjectA.StopStyle = Mode;
}

// WIRED FOR REAL (2026-09-11, d285:common-port): SubObjectA/B's own vtbl
// slot 1 (offset 0x4) / slot 4 (offset 0x10) -- BaseIdea::Update(float)/
// Commit() -- now that both sub-objects are identified as MoveIdea/FaceIdea.
// Real virtual dispatch via `BaseIdea*` serves either sub-object with one
// definition (see header comment).
int LaunchBehavior::TickSubObject(BaseIdea* SubObject, float DeltaTime) { return SubObject->Update(DeltaTime); }
void LaunchBehavior::ReleaseSubObject(BaseIdea* SubObject) { SubObject->Commit(); }

// ----------------------------------------------------------------------------
// PORTED FOR REAL 2026-09-17 (claude:w18-behavior). .text:062cdd89-062cdd9d,
// five instructions, reached only when case 5's `[ESI+0x7e]` gate (ESI ==
// NavHelperPtr24+0x14fc) is clear:
//     MOV  ECX,[ESI+0x84]            ; == NavHelperPtr24+0x1580 == ExternalIface
//     LEA  EAX,[ESI+0xc]  / PUSH EAX ; == &CachedPhysicalCenter  (0x14fc+0xc  == 0x1508)
//     LEA  EAX,[ESI+0x18] / PUSH EAX ; == &CachedPhysicalRadius  (0x14fc+0x18 == 0x1514)
//     MOV  byte ptr [ESI+0x7e],1     ; == Flags1578[2]           (0x14fc+0x7e == 0x157a)
//     CALL [EDX+0x2c]                ; IObjInspect::get_physical_radius(float&, Vector&)
// The last push is the first argument, which is why Radius precedes Center and
// matches common_iface.h's declared parameter order. The latch is set BEFORE the
// call, as reproduced.
//
// The recorded deferral reason -- "NavHelper's own nested-object class, shared
// unidentified gap across this whole family" -- was stale twice over: there is no
// nested object (the +0x14fc/+0x84 pair is one load, see NestedNavQuery0xbc
// above), and this exact five-instruction block is FUN_062e6d40's own cache
// refresh (item 1794), inlined. FaceBehavior::Update inlines it twice at
// .text:062c91d3-062c91e7 and .text:062c9236-062c924a, byte for byte.
void LaunchBehavior::EnsureNestedNavPositionCached()
{
    SystemManager* const Sys = static_cast<SystemManager*>(NavHelperPtr24);
    Sys->SelfTarget.bPosValid = 1;
    Sys->SelfTarget.Iface->get_physical_radius(Sys->SelfTarget.CachedRadius,
                                                         Sys->SelfTarget.CachedPos);
}

// FUN_062d93c0(SubObjectB, Angle) -- decomp-tracker item 1632 confirms this
// function's entire body is `*(uint*)(this+0x8c) = param`. Offset +0x8c lands
// exactly on FaceIdea's own confirmed FacingTolerance (formerly AngleWeight8c,
// common_idea.h). Item
// 1632's own `blocked` status is about its OTHER 4 XREFs (a different,
// unidentified owning class in an unrelated weapon/equipment factory switch)
// -- not this call site, where the owning object (SubObjectB) is confirmed.
void LaunchBehavior::SetHardpointCone(float Angle)
{
    SubObjectB.FacingTolerance = Angle;
}

// FUN_062e89a0(NavHelperPtr24+0x18, Value, 0) = ThrustManager::SetThrottle (Common.dll.txt
// 203537-203541).
void LaunchBehavior::AdjustNavHelperThrottle(float Value)
{
    ((SystemManager*)NavHelperPtr24)->Thrust.SetThrottle(Value, 0);
}
