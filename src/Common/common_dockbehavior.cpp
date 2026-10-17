// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_dockbehavior.h"
#include "common_misc.h"   // SinglePlayer() -- UpdateApproachTail()'s case 0xb calls it
#include "common_systemmanager.h"  // SystemManagerGunGroupManager::SetShieldActive -- FUN_062e9a60
#include "common_gotobehavior.h"   // ComputeFormationFollowerEnvelope / ProxWatcherComputeSpeedCap
#include "common_aisteer.h"        // FindObjectDockingAtBase (FUN_062e7400) -- CheckClearanceProgress
#include "common_csys.h"           // Transform, Acquire3DMathEngine -- SetContext's hpmount offset
#include "x86math.h"               // I3DMathEngine::TransposeTransform, FVector, FMatrix
#include "common_cobject.h"        // CObject, CShip::request_exit_tradelane -- Disengage
                                   // -- AdvancePreamble's two speed-cap sources
                           // directly (Common.dll.txt 062c5025)

#include <cmath>
#include <cstdint>
#include <cstdlib>   // rand -- EnterState(2)'s spread
#include <cstring>

// Retail pushes float parameters to the MoveIdea/FaceIdea setters as raw dword
// bit patterns (`PUSH 0x3eb2b8c3`), so the call sites below reinterpret rather
// than convert -- the same convention common_idea.cpp uses.
static float BitsToFloat(unsigned int Bits)
{
    float Result;
    std::memcpy(&Result, &Bits, sizeof(Result));
    return Result;
}

// The "-8 header" idiom, used at five sites inside UpdateApproachTail()/EnterState():
// a BaseWatcher's watched payload pointer is the middle of an allocation whose object
// header sits eight bytes earlier, and retail reaches the object by subtracting 8 and
// treating null either side as "no object". Retail writes it as
// `MOV EAX,[ESI+0x324]; TEST EAX,EAX; JZ ...; ADD EAX,-8`.
static void* HeaderBlockFromWatched(Watchable const* Watched)
{
    if (Watched == nullptr)
        return nullptr;
    return const_cast<void*>(
        static_cast<void const*>(reinterpret_cast<char const*>(Watched) - 8));
}

// DockBehavior -- see common_dockbehavior.h for the full RTTI/layout writeup
// this pass relied on. Ported from EXE/Disasm/Common.dll.txt (raw disassembly
// is authoritative in this module over Ghidra's Common.dll.c decompilation --
// see this marathon's "ground truth precedence" rule, and item 1278's own
// prior notes about a Ghidra register-reuse bug in this exact neighborhood).

// ----------------------------------------------------------------------------
// FUN_062c2e60 (Common.dll.txt 187332-187388), thiscall, RET 4 -- constructor.
//   062c2e83  FUN_062d2020(TypeId)               Behavior's field initialisation
//   062c2e91  FUN_062d4e60 on +0x30              MoveIdea        SubObjectA
//   062c2ea1  FUN_062d6750 on +0xa8              FaceIdea        SubObjectB
//   062c2eb1  FUN_062d9bf0 on +0x168             SplineMoveIdea  SubObjectC
//   062c2ec1  FUN_062da500 on +0x2dc             AfterburnerIdea GotoDirective.Idea
//   062c2ecc  DirectiveDockOp ctor on +0x2f4     DockOp
//   062c2ed1  +0x324 = +0x328 = 0                Watcher324
//   062c2ee8  ICRSplineSegment ctor on +0x348    Spline
//   062c2eed  +0x450 = +0x454 = 0                ProxWatcher450.Watcher
//   062c2efd  +0x45c = +0x458 = 0                ProxWatcher450.Unknown0c / Unknown08
//   062c2f09  +0x320 = 0, +0x32c = 0, byte +0x39b = 0
//   062c2f1b  both vtables (implicit here).
// Nothing else is written: GotoDirective.NavPilotPtr (+0x2f0) is first stored by
// RecomputeApproach (.text:062c373b), the remaining fields by SetContext / EnterState /
// RefreshApproachState. Those members are therefore left default-initialised, as in
// retail's operator new block.
DockBehaviorFields::DockBehaviorFields(int InTypeId)
{
    ConstructBase(InTypeId);   // 062c2e83 FUN_062d2020 -- writes TypeId (+0x04) among the rest
}

DockBehavior::DockBehavior(int InTypeId)
    : DockBehaviorFields(InTypeId)
    , SubObjectA()
    , SubObjectB()
    , SubObjectC()
    , DockOp()
    , ModeState320(0)
    , Watcher324()
    , StateFlags32c(0)
    , Spline()
    , Flag39b(0)
{
    // 062c2eed-062c2f03: ProxWatcher450's watcher dwords are zeroed by its own default
    // ctor; the two floats after it are zeroed as raw dwords.
    ProxWatcher450.Unknown0c = 0.0f;
    ProxWatcher450.Unknown08 = 0.0f;
}

// FUN_062c30d0 (Common.dll.txt 187641-187718), the destructor proper; FUN_062c3020
// (item 1267) is the scalar-deleting wrapper. Retail is MSVC's member teardown in
// reverse declaration order, each step an inlined ~BaseWatcher (`if (Watched)
// set_pointer(0)`): +0x450 ProxWatcher450.Watcher, +0x324 Watcher324, then
// SubObjectC's ~SplineMoveIdea (+0x208 Face.Watcher, +0x190 Move.Watcher -- the same
// body as the out-of-line FUN_062c3060), +0xbc SubObjectB.Watcher, +0x44
// SubObjectA.Watcher, and finally FUN_062d2080 (~Behavior). The compiler-generated
// member and base destructors reproduce exactly that sequence, so the body is empty.
DockBehavior::~DockBehavior()
{
}

// Own vtable slot 5 (offset 0x14). Common.dll.txt 188111-188123 (LAB_062c35c0).
// All 10 instructions: EAX = (byte +0x28 & 2) ? 1 : 2; byte +0x29 -> 3; [+0xc] = EAX; the
// mode is also the return value (EAX), RET 4 (the manager's float timer is not read).
int DockBehavior::SetModeFromFlags()
{
    int Mode = (Flags28 & 0x2) ? 1 : 2;
    if (Flag29 != 0)
        Mode = 3;
    ModeSelector0xc = Mode;
    return Mode;
}

// Vtable slots 13/14 (LAB_062c5650 / LAB_062c5670, 7 instructions each):
// `MOV ECX,[ECX+0x2f0]; TEST ECX,ECX; JZ ret; ...; JMP [[ECX]+0x34 / +0x38]`.
void DockBehavior::set_throttle(float Throttle)
{
    Behavior* const Pilot = static_cast<Behavior*>(GotoDirective.NavPilotPtr);
    if (Pilot != nullptr)
        Pilot->set_throttle(Throttle);
}

void DockBehavior::set_cruise(bool bOn)
{
    Behavior* const Pilot = static_cast<Behavior*>(GotoDirective.NavPilotPtr);
    if (Pilot != nullptr)
        Pilot->set_cruise(bOn);
}

// Vtable slots 16/17 (LAB_062c5690 / LAB_062c56b0): `MOV EAX,[ECX+0x2f0]; TEST EAX,EAX;
// JZ ret; JMP FUN_062d2110 / FUN_062d21b0` -- Behavior's own bodies on this object.
void DockBehavior::set_afterburner(bool bOn)
{
    if (GotoDirective.NavPilotPtr != nullptr)
        Behavior::set_afterburner(bOn);
}

void DockBehavior::slide_strafe_burst(StrafeDir Dir)
{
    if (GotoDirective.NavPilotPtr != nullptr)
        Behavior::slide_strafe_burst(Dir);
}

// Own vtable slot 11 (offset 0x2c). Common.dll.txt 187523-187537 (LAB_062c3000).
bool DockBehavior::IsClearanceState() const
{
    int Mode = ModeState320;
    return Mode != 6 && Mode != 7 && Mode != 8 && Mode != 10;
}

// Own vtable slot 24/26 (offsets 0x60/0x68). Common.dll.txt 187499-187507
// (LAB_062c2fe0).
void* DockBehavior::GetWatchedRawBlock() const
{
    char const* Watched = reinterpret_cast<char const*>(Watcher324.target());
    if (Watched == nullptr)
        return nullptr;
    return const_cast<char*>(Watched) - 8;
}

// Own vtable slot 7 (offset 0x1c), Update/dispatch entry point.
// Common.dll.txt 188633-188787 (LAB_062c3b70), 141 instructions.
int DockBehavior::Update()
{
    // 062c3b76-062c3b8a: `TEST byte ptr [ESI+0x28],1` -> result 2.
    if (Flags28 & 0x1)
    {
        UpdateResult10 = 2;
        return 2;
    }

    // 062c3b8b-062c3ba9 / LAB_062c3cf5: no owner, no watched object, or a watched pointer
    // of exactly 8 -> result 3.
    void const* Watched = Watcher324.target();
    if (ThreatSourcePtr14 == nullptr || Watched == nullptr ||
        reinterpret_cast<char const*>(Watched) - 8 == nullptr)
    {
        UpdateResult10 = 3;
        return 3;
    }

    IObjRW* const Owner = static_cast<IObjRW*>(ThreatSourcePtr14);

    // 062c3baf-062c3c1b: for a player owner (+0xbc is_player) with StateFlags32c bit 7
    // (`MOV AL,[+0x32c]; TEST AL,AL; JNS` skips), read the docking target's
    // get_status(float&) (+0x40) into a local preset to 1.0f. FCOMP 0.0f with
    // TEST AH,0x41 + JZ: strictly greater clears Flag39b; otherwise (not greater or
    // unordered) the first time only, Flag39b = 1 and the owner's IObjAffect
    // slot_6c(1, 0).
    if (Owner->is_player() && static_cast<signed char>(StateFlags32c & 0xff) < 0)
    {
        IObjRW* const Target = ObjRwFromWatchable(const_cast<Watchable*>(Watcher324.target()));
        float Health = 1.0f;
        Target->get_status(Health);
        if (Health > 0.0f)
        {
            Flag39b = 0;
        }
        else if (Flag39b == 0)
        {
            Flag39b = 1;
            static_cast<IObjAffect*>(Owner)->slot_6c(1, 0);
        }
    }

    // 062c3c22-062c3c66: |target velocity|^2, (x*x + y*y) + z*z on the x87 stack,
    // against 0.3f (DAT_0639f484); TEST AH,0x41 + JNZ: not greater goes on to the
    // approach tail.
    {
        IObjRW* const Target = ObjRwFromWatchable(const_cast<Watchable*>(Watcher324.target()));
        Vector const Velocity = Target->get_velocity();
        double const SpeedSq = (static_cast<double>(Velocity.x) * Velocity.x +
                                static_cast<double>(Velocity.y) * Velocity.y) +
                               static_cast<double>(Velocity.z) * Velocity.z;
        if (SpeedSq > static_cast<double>(0.3f))
        {
            // 062c3c68-062c3c9b: the target is moving -- release both Ideas, result 1.
            SubObjectB.Commit();          // [+0xa8]+0x10
            SubObjectA.SetField44(0);     // FUN_062d88b0 on +0x30
            SubObjectA.Commit();          // [+0x30]+0x10
            UpdateResult10 = 1;
            return 1;
        }
    }

    // LAB_062c3c9c-062c3ce3: the proximity watcher at +0x450 against the target's
    // get_position() (+0x00), re-read for each call.
    unsigned char* const ProxWatcher = reinterpret_cast<unsigned char*>(&ProxWatcher450);
    ProxWatcherRecomputeZoneFlags(
        ProxWatcher, Field24,
        ObjRwFromWatchable(const_cast<Watchable*>(Watcher324.target()))->get_position());
    ProxWatcherNotifyZoneChange(
        ProxWatcher, Field24, Field20,
        ObjRwFromWatchable(const_cast<Watchable*>(Watcher324.target()))->get_position());

    // 062c3ce8-062c3cf4: FUN_062c4900 with `this`; its EAX is the return value.
    return UpdateApproachTail();
}

// FUN_062c3590 (Common.dll.txt 188078-188095), all 13 instructions: ESI = this+0x2f4,
// `MOV ECX,0xb; REP MOVSD` into the argument, then `MOV AL,[EAX+0x28]; SHR AL,1; AND AL,1`.
// The copy is a raw dword copy (pub::AI::DirectiveDockOp's operator= is deleted, as in
// retail, so the vptr travels with the bytes exactly as REP MOVSD carries it).
bool DockBehavior::CopyDockOp(pub::AI::DirectiveDockOp* Out) const
{
    std::memcpy(static_cast<void*>(Out), static_cast<void const*>(&DockOp), 0xb * 4);
    return ((Flags28 >> 1) & 1) != 0;
}

// IObjAffect::RequestCB override -- own single secondary-vtable slot.
// Common.dll.txt 190855-190928 (LAB_062c53f0), 66 instructions. Re-read in
// full this pass (2026-09-11) -- now faithfully translated, including the
// shared-tail EnterState() calls (see the class-level comment's 2026-09-11
// follow-up: this is the 2nd of now 5 confirmed direct DockBehavior call
// sites into FUN_062c3d10).
bool DockBehavior::OnResult(int Param1, int Param2, int Param3)
{
    // outer_this = ESI-0x2c in retail (ESI is the RequestCB-adjusted `this`
    // the secondary-vtable thunk passes in); `this` here already IS
    // outer_this since we're a plain member function, not the raw thunk.
    if (Param1 == 0)
    {
        // `CMP dword[ESP+0x14],0x3; JZ ...; MOV BL,1; RET 0xc` -- no side
        // effect, just an early true.
        if (Param2 == 3)
            return true;

        // retail: `MOV [ESI+0x410],ECX` == outer+0x43c = Field43c = Param3.
        Field43c = Param3;

        // retail: `MOV EAX,[ESI+0x2f8]` == outer+0x324 = Watcher324's raw
        // Watched pointer, then the usual "-8 header, null either way is
        // invalid" test.
        void const* Watched = Watcher324.target();
        bool const bInvalidBlock =
            (Watched == nullptr) || (reinterpret_cast<char const*>(Watched) - 8 == nullptr);

        if (bInvalidBlock || Param3 == -1)
        {
            EnterState(0xd);
            Flag39a = 0; // retail: `MOV byte[ESI+0x36e],BL` == outer+0x39a.
            return false;
        }

        // FUN_062c50e0 on the outer object.
        RefreshApproachState();

        // retail: `TEST [ESI+0x300],0x840` == outer+0x32c = StateFlags32c.
        if (StateFlags32c & 0x840)
            EnterState(6);
        else
            EnterState(2);
        return false;
    }

    if (Param1 == 2)
    {
        if (Param2 == 4)
        {
            if (Param3 == -1)
            {
                EnterState(0xd);
                Flag39a = 0;
                return false;
            }
            // retail: `MOV [ESI+0x3dc],0x41800000` == outer+0x408 = 16.0f.
            Field408 = 16.0f;
            EnterState(6);
            return false;
        }
        if (Param2 == 1)
        {
            EnterState(0xd);
            Flag39a = 0;
            return false;
        }
        return false;
    }

    // Any other Param1 value: retail falls straight through to `RET 0xc`
    // with AL=0 and no side effects.
    return false;
}

// ----------------------------------------------------------------------------
// Newly ported this pass (2026-09-11) -- see common_dockbehavior.h's
// class-level comment (2026-09-11 follow-up) for the evidence trail these
// rely on.
// ----------------------------------------------------------------------------

// Own vtable slot 1 (offset 0x04). Common.dll.txt 187726-187871 (LAB_062c31c0).
bool DockBehavior::SetContext(void* Param1, void* Param2, void* Param3)
{
    // retail: `CALL FUN_062d2090(this, Param1, Param2, Param3)` -- the shared
    // Behavior-family context initializer (see DockBehaviorFields'
    // ThreatSourcePtr14/Field20/Field24 comments). Ported inline here since
    // it's a small (24-instruction), fully-decoded leaf with no cross-cutting
    // dependency of its own:
    //   if (Param1 == nullptr || Param3 == nullptr) return false;  // retail's 2nd null check is on P3
    //   ThreatSourcePtr14 = Param1;
    //   CachedHandle08 = Param1->vtbl[0x20](Param1);   // a getter call
    //   Field24 = Param3;
    //   Field20 = Param2;
    //   return true;
    // NOTE: parameter-to-field mapping confirmed directly from FUN_062d2090's
    // raw disassembly (not just call-site order) -- P2 and P3 land on
    // Field20/Field24 in the OPPOSITE order a naive "P1/P2/P3 -> +0x14/+0x20/
    // +0x24" reading would suggest (this function's own P3 stack slot is read
    // BEFORE its P2 slot, into Field24 first).
    // 2026-10-08: the shared binder is Behavior::BindContext now (common_behavior.cpp).
    bool const bOk = BindContext(Param1, Param2, Param3);          // 062c31d9
    if (!bOk)
        return false;                   // .text:062c3367, AL == 0

    // retail: clear the "hpmount lookup failed last tick"-shaped flags before
    // the (deferred) recompute branch.
    AbortFlag398 = 0;
    Flag39b = 0;

    // WIRED FOR REAL (2026-09-11, d285:common-port): retail calls SubObjectA/
    // B's own vtable slot 0 here -- re-read directly from Common.dll.txt
    // 187744-187774 byte-for-byte: both calls push the SAME 3 arguments,
    // `(ThreatSourcePtr14, Field24, Param2)` -- Param2 is this function's own
    // raw incoming 2nd parameter (captured into EDI at function entry, BEFORE
    // the FUN_062d2090 call above, and never reloaded from +0x20/Field20
    // afterward; Field20 holds the identical value since FUN_062d2090 sets
    // Field20=Param2, but retail's own register-level source really is the
    // still-live Param2, not a reload of the field). Argument order confirmed
    // by push order (ECX=ThreatSourcePtr14 pushed last = 1st param, matching
    // SetContext(InObject, InHandle, InContext)).
    SubObjectA.SetContext(ThreatSourcePtr14, Field24, Param2);     // 062c320a
    SubObjectA.SetMode(0);                                          // 062c320f FUN_062d5980(0)
    GotoDirective.Idea.SetContext(ThreatSourcePtr14, Field24, Param2); // 062c3229 (+0x2dc slot 0)
    SubObjectB.SetContext(ThreatSourcePtr14, Field24, Param2);     // 062c3240

    // WIRED FOR REAL 2026-09-17 (claude:abi-pass50): Common.dll.txt
    // 187778-187784 (`.text:062c3245 MOV EDX,[ESI+0x168]`, `062c3250 LEA
    // ECX,[ESI+0x168]`, `062c3257 CALL dword ptr [EDX]`) -- SubObjectC's own
    // vtable slot 0, pushing the SAME three arguments as SubObjectA/SubObjectB
    // above. SubObjectC is a SplineMoveIdea (common_idea.h), so slot 0 is
    // BaseIdea::SetContext and this is now a typed call.
    SubObjectC.SetContext(ThreatSourcePtr14, Field24, Param2);

    // CORRECTED 2026-09-11 (d285:common-port): re-reading Common.dll.txt
    // 187783-187789 byte-for-byte while wiring the calls above -- this call
    // site actually zeroes the 3 unconfirmed dwords at +0x444/+0x448/+0x44c
    // (Opaque444), NOT SubStateValue440(+0x440)/Field330(+0x330) as the prior
    // pass's port claimed (no instruction anywhere in this function touches
    // either of those two fields). Fixed here; SubStateValue440/Field330
    // remain untouched by SetContext(), matching their own field comments
    // ("Cleared by RecomputeApproach()/Disengage()" -- not SetContext()).
    // (Three zero dwords == a zero Vector; Field444 since 2026-10-08.)
    Field444.x = 0.0f;
    Field444.y = 0.0f;
    Field444.z = 0.0f;
    Flags28 = static_cast<unsigned char>(Flags28 & ~0x2);

    // 062c3271-062c328d: owner->get_hardpoint("hpmount", &Pos, nullptr) (IObjInspect +0xc0).
    // A non-zero result (no such hardpoint) zeroes the +0x430 offset (LAB_062c3351).
    Vector HpPos;   // [ESP+0x18], left uninitialised by retail
    if (ObjInspectOf(ThreatSourcePtr14)->get_hardpoint("hpmount", &HpPos, nullptr) != 0)
    {
        unsigned int* const Offset430 = reinterpret_cast<unsigned int*>(Opaque430);
        Offset430[0] = 0;                                            // 062c3355
        Offset430[1] = 0;                                            // 062c335b
        Offset430[2] = 0;                                            // 062c3361
        return bOk;
    }

    // 062c3293-062c32ba: the SystemManager's physical-radius cache (+0x14fc block), the same
    // inlined refresh LaunchBehavior::EnsureNestedNavPositionCached reproduces.
    SystemManager* const Sys = static_cast<SystemManager*>(Field24);
    if (Sys->SelfTarget.bPosValid == 0)
    {
        Sys->SelfTarget.bPosValid = 1;                                       // 062c32b3
        Sys->SelfTarget.Iface->get_physical_radius(Sys->SelfTarget.CachedRadius,
                                                              Sys->SelfTarget.CachedPos);
    }

    // 062c32bd-062c32fa: Delta = hardpoint - cached centre (each FSTP float), stored to
    // +0x430 and kept in a local.
    Vector Delta;
    Delta.x = HpPos.x - Sys->SelfTarget.CachedPos.x;
    Delta.y = HpPos.y - Sys->SelfTarget.CachedPos.y;
    Delta.z = HpPos.z - Sys->SelfTarget.CachedPos.z;
    std::memcpy(Opaque430, &Delta, sizeof(Delta));

    // 062c32fd-062c3345: Matrix = ExternalIface->get_transform() (+0xc, rotation at offset 0);
    // I3DMathEngine (lazily cached DAT_063fc528 via FUN_06391cf0)->TransposeTransform(
    // &Delta, Matrix, +0x430) -- Dst is the local, Source is +0x430 -- then the local is
    // copied back into +0x430.
    Transform const& OwnerXform = Sys->SelfTarget.Iface->get_transform();
    I3DMathEngine* const Math = Acquire3DMathEngine();
    Vector Source;
    std::memcpy(&Source, Opaque430, sizeof(Source));
    Math->TransposeTransform(reinterpret_cast<FVector&>(Delta),
                             reinterpret_cast<FMatrix const&>(OwnerXform.Rotation),
                             reinterpret_cast<FVector const&>(Source));
    std::memcpy(Opaque430, &Delta, sizeof(Delta));                   // 062c3330-062c3345
    return bOk;                                                      // AL = saved byte
}

// Own vtable slot 3 (offset 0x0c). Common.dll.txt 188246-188420 (LAB_062c3720).
void DockBehavior::RecomputeApproach()
{
    // retail: `MOV [this+0x399],0; MOV [this+0x39a],0; MOV [this+0x39b],0;
    // MOV [this+0x2f0],0` -- clears Flag399/Flag39a/Flag39b and (a second,
    // independent write -- NOT through GotoDirective.NavPilotPtr's own
    // release sequence, just a raw zero) GotoDirective.NavPilotPtr.
    Flag399 = 0;
    Flag39a = 0;
    Flag39b = 0;
    GotoDirective.NavPilotPtr = nullptr;

    // .text:062c3741: `CALL FUN_062d20d0` -- Behavior's own slot 3 body.
    Behavior::Slot3();

    void const* Watched = Watcher324.target();
    bool const bInvalidBlock =
        (Watched == nullptr) || (reinterpret_cast<char const*>(Watched) - 8 == nullptr);
    if (bInvalidBlock)
    {
        EnterState(0xd);                                             // 062c375b
        return;
    }

    Field43c = -1;                                                   // 062c3773
    CombatFlag344 = 0;                                               // 062c377d (byte)

    // .text:062c3766-062c37a5: FUN_062e5af0(owner, 1.0f, 1) (get_speed, Engine) is FSTP'd to
    // a float stack slot and passed to FUN_062e5ab0(owner, speed) (the coast distance at
    // 1000.0f); the result times 1.4f (DAT_0639f510 == 0x3fb33333) is FSTP'd to Field33c.
    {
        float const Speed = Behavior_InvokeManagerSlotD8(ObjInspectOf(ThreatSourcePtr14), 1.0f,
                                                         IObject::ThrustEquipEngine);
        Field33c = static_cast<float>(
            static_cast<double>(Behavior_QueryFixedFalloffEffect(ThreatSourcePtr14, Speed)) * 1.4f);
    }

    // .text:062c37a1-062c37ad: FUN_062d21d0(this, 1, 3) -- Behavior::SetCruiseLock.
    SetCruiseLock(1, 3);

    // .text:062c37b2-062c37ba: `MOV EAX,[EBP+0x32c]; TEST AL,AL; JNS LAB_062c383b` -- the
    // block below runs when bit 7 of the low byte is SET (the previous port had the two
    // branches swapped).
    if (static_cast<signed char>(StateFlags32c) < 0)
    {
        // 062c37bc-062c37e8: owner->get_lane_start(watched object, &TargetShipPtr388,
        // &DestPoint3cc, &Facing38c) (IObjInspect +0x138; the object is target - 8, or null).
        IObjInspect const* const WatchedObj = ObjInspectOf(Watched != nullptr
            ? static_cast<void const*>(reinterpret_cast<char const*>(Watched) - 8)
            : nullptr);
        ObjInspectOf(ThreatSourcePtr14)->get_lane_start(
            WatchedObj, *static_cast<IObjInspect const**>(static_cast<void*>(&TargetShipPtr388)),
            DestPoint3cc, Facing38c);
        // 062c37ee-062c3806: LookMatrixYup(Facing38c) copied (9 dwords) into OrientMatrix3d8.
        OrientMatrix3d8 = LookMatrixYup(Facing38c);
        // 062c3808-062c3834: FUN_062d4e50(&SubObjectA, object->get_id()) where the object is
        // Watcher324's target - 8 re-read; on a null target retail calls through a null
        // pointer (062c3829 XOR ECX,ECX / MOV EDX,[ECX]) -- reproduced literally.
        Watchable const* const Watched2 = Watcher324.target();
        void const* const Obj2 = Watched2 != nullptr
            ? static_cast<void const*>(reinterpret_cast<char const*>(Watched2) - 8)
            : nullptr;
        SubObjectA.PublishTargetId(ObjInspectOf(Obj2)->get_id());
    }
    else if ((StateFlags32c & 0x40) != 0) // LAB_062c383b `TEST AL,0x40`
    {
        // .text:062c3841 `MOV dword ptr [EBP + 0x43c],EBX` with EBX held at 0
        // by the function's entry `XOR EBX,EBX` (.text:062c3724). This write
        // was missing from the port: Field43c is set to -1 unconditionally
        // earlier (.text:062c3773, `MOV [EBP+0x43c],0xffffffff`), and retail
        // resets it to 0 again here, on this branch only, right before the
        // call below. Found 2026-09-17 while wiring this method's closing gate.
        Field43c = 0;

        RefreshApproachState();                                      // 062c3847 FUN_062c50e0
    }

    // WIRED FOR REAL (2026-09-17, claude:common-orchestrator). This block used
    // to be FULLPORT-DEFERRED on the claim that "ThreatSourcePtr14's class is
    // still unidentified", and that claim is now stale: ThreatSourcePtr14 is an
    // IObjRW. Four independent vcall offsets in this very function agree on it,
    // which is what makes it an identification and not a guess --
    //   .text:062c3851  CALL [EAX+0xbc]  -> IObjInspect slot 47, is_player()
    //   .text:062c381b  CALL [EDX+0x20]  -> IObjInspect slot  8, get_id()
    //   (deferred above)  vtbl[0x138]    -> IObjInspect slot 78
    //   .text:062c38b9  CALL [EDX+0x6c] on LEA ECX,[EAX+4]
    //                                    -> IObjAffect  slot 27 on the +4
    //                                       sub-object, whose own catalog note
    //                                       already records it being called on
    //                                       "the IObjAffect sub-object of a
    //                                       watched IObjRW".
    // IObjInspect at +0 and IObjAffect at +4 is exactly IObjRW's RTTI-proven
    // base layout (see the ClassHierarchyDescriptor note in common_iface.h).
    //
    // The gate constant is likewise no longer opaque. DAT_063fc518 is not a
    // datum in the image at all -- it is built at startup by the CRT
    // initializer LAB_062c2e40, registered at .data:063ea2f0:
    //     .text:062c2e40  FLD  float ptr [.rdata:DAT_0639f48c]
    //     .text:062c2e46  FMUL float ptr [.rdata:DAT_0639f48c]
    //     .text:062c2e4c  FSTP float ptr [.data:DAT_063fc518]
    // and DAT_0639f48c's four bytes are 00 40 1C 46 = 0x461C4000 = 10000.0f.
    // So the gate is a squared 10000.0f range test, compared against a squared
    // distance -- retail never takes the square root.
    //
    // Polarity, read from the listing rather than assumed: .text:062c389b
    // `FCOMP [DAT_063fc518]` / `TEST AH,0x5` / `JNP LAB_062c38c0`. TEST AH,0x5
    // masks C0|C2; JNP is taken when exactly one of them is set, i.e. C0=1 with
    // C2=0, which is the ordered strictly-less-than case. LAB_062c38c0 pushes 1
    // and LAB_062c38c2's fallthrough pushes 0xd, so CLOSER than the range
    // continues the approach and FARTHER aborts it. (Note this is the 0x5/JNP
    // less-than idiom, NOT the 0x44/JP equality idiom this module has been
    // bitten by before.)
    float const kApproachRangeSq = 10000.0f * 10000.0f;   // DAT_063fc518 == 1e8f exactly

    int NextState = 1;                                       // LAB_062c38c0 `PUSH 1`
    IObjInspect const* const ThreatObject = ObjInspectOf(ThreatSourcePtr14);
    if (ThreatObject->is_player())                           // 062c3851 / 062c3859 JZ
    {
        // 062c385b-062c3879: Watcher324 re-read, object = target - 8 (or null, no test);
        // owner->get_position() first, then the object's (both IObjInspect slot 0).
        Watchable const* const Watched3 = Watcher324.target();
        void const* const Obj3 = Watched3 != nullptr
            ? static_cast<void const*>(reinterpret_cast<char const*>(Watched3) - 8)
            : nullptr;
        Vector const& ThreatPos = ThreatObject->get_position();
        Vector const& WatchedPos = ObjInspectOf(Obj3)->get_position();

        // 062c387b-062c3899: (object - owner) componentwise as float subtractions on the
        // FPU, then (dx*dx + dy*dy) + dz*dz kept on the stack (double here).
        double const Dx = static_cast<double>(WatchedPos.x) - ThreatPos.x;
        double const Dy = static_cast<double>(WatchedPos.y) - ThreatPos.y;
        double const Dz = static_cast<double>(WatchedPos.z) - ThreatPos.z;
        double const DistSq = Dx * Dx + Dy * Dy + Dz * Dz;
        if (!(DistSq < kApproachRangeSq))                    // 062c38ac JNP: less -> 1
        {
            // 062c38ae-062c38bc: owner's IObjAffect (+4) slot 0x6c(0, 0), then PUSH 0xd.
            ObjAffectOf(ThreatSourcePtr14)->slot_6c(0, 0);
            NextState = 0xd;
        }
    }
    EnterState(NextState);                                   // 062c38c4

    // 062c38c9-062c38f7: FUN_062d5a80(&SubObjectA, &zero) -- Offset4c = 0 -- and
    // SetAngle38(pi) (0x40490fdb) on the same MoveIdea.
    Vector Zero;
    Zero.x = 0.0f;
    Zero.y = 0.0f;
    Zero.z = 0.0f;
    SubObjectA.Offset4c = Zero;
    SubObjectA.SetAngle38(3.14159274f);
    // 062c38fc-062c3904: MaxApproachSpeed46c = DAT_0639f47c (1000.0f).
    MaxApproachSpeed46c = 1000.0f;
}

// Own vtable slot 4 (offset 0x10). Common.dll.txt 188421-188627 (LAB_062c3910).
// FULLY RE-READ AND CORRECTED 2026-09-11 (d285:common-port) while wiring the
// SubObjectA/B Commit() calls -- see the 3-point CORRECTED note on this
// method's declaration in common_dockbehavior.h for the prior pass's bugs
// this rewrite fixes (missing ModeState320==0 gate/alt-tail, inverted
// StateFlags32c branches, GotoWatcherPtr's spurious null-write).
void DockBehavior::Disengage()
{
    // retail: `CMP [this+0x320],0; JZ LAB_062c3afc` -- the WHOLE
    // lock_maneuvers/NavPilot-release block and everything past it is gated
    // on ModeState320 != 0. When it's already 0, retail takes a short,
    // completely separate tail instead (reproduced in the early-return below).
    if (ModeState320 == 0)
    {
        if (ApproachClearedFlag334 == 0)
            return;

        // LAB_062c3afc (byte +0x334 set): +0x440 = 0, +0x330 = -1, byte +0x334 = 0, then the
        // owner's IObjAffect slot 0x60 (0, &id, 0) with Watcher324's object id.
        SubStateValue440 = 0;
        Field330 = -1;
        ApproachClearedFlag334 = 0;
        CancelOwnerRequest(0);
        return;
    }

    // 062c3925-062c3929: IBehaviorManager::lock_maneuvers(false) on Field20.
    static_cast<IBehaviorManager*>(Field20)->lock_maneuvers(false);

    // 062c392e-062c3948: with a Goto pilot, its Behavior slot 4, FUN_062d21c0 on it, and
    // the pointer is cleared.
    if (GotoDirective.NavPilotPtr != nullptr)
    {
        static_cast<Behavior*>(GotoDirective.NavPilotPtr)->Slot4();  // 062c393a
        Behavior_ReleaseDirectiveTarget(GotoDirective.NavPilotPtr);  // 062c3943
        GotoDirective.NavPilotPtr = nullptr;                         // 062c3948
    }

    // 062c394e-062c3957: `MOV AL,[+0x32c]; TEST AL,AL; JNS LAB_062c3a01`.
    if (static_cast<signed char>(StateFlags32c) < 0)
    {
        SubObjectA.SetField44(2);                                    // 062c3964 FUN_062d88b0

        // 062c3969-062c399d: AbortFlag398 set and Flag399 clear -> the owner's CObject,
        // reduced to a CShip when (+0x4c & 0x503) == 0x503 (else null), and
        // CShip::request_exit_tradelane on it (thiscall on a possibly-null pointer, as
        // retail).
        if (AbortFlag398 != 0 && Flag399 == 0)
        {
            CObject const* const Obj = ObjInspectOf(ThreatSourcePtr14)->cobject(); // +0x150
            CShip* Ship = nullptr;
            if (Obj != nullptr &&
                (*reinterpret_cast<unsigned int const*>(reinterpret_cast<char const*>(Obj) + 0x4c) &
                 0x503) == 0x503)
                Ship = const_cast<CShip*>(static_cast<CShip const*>(Obj));
            Ship->request_exit_tradelane();
        }

        // 062c39a2-062c39fc: Flag39a set -> owner IObjAffect slot 0x60 (2, &id, 0).
        if (Flag39a != 0)
            CancelOwnerRequest(2);
    }
    else
    {
        // LAB_062c3a01: level 2 when the SystemManager's external interface is a player
        // and ModeState320 == 1, else 0.
        SystemManager* const Sys = static_cast<SystemManager*>(Field24);
        int const Level = (Sys->SelfTarget.Iface->is_player() && ModeState320 == 1) ? 2 : 0;
        SubObjectA.SetField44(Level);                                // 062c3a30 FUN_062d88b0

        // 062c3a35-062c3a9b: unless Flags28 bit 0 is set, the approach reset and the
        // owner's IObjAffect slot 0x60 (0, &id, 0).
        if ((Flags28 & 0x1) == 0)
        {
            SubStateValue440 = 0;
            Field330 = -1;
            ApproachClearedFlag334 = 0;
            CancelOwnerRequest(0);
        }
    }

    // LAB_062c3a9e, the shared tail.
    Flag39a = 0;                                                     // 062c3aaa
    AbortFlag398 = 0;                                                // 062c3ab0
    GotoDirective.Idea.Commit();                                     // 062c3ab6 (+0x2dc slot 4)
    SubObjectB.Commit();                                             // 062c3ac5 (+0xa8)
    SubObjectA.Commit();                                             // 062c3acc (+0x30)
    SetCruiseLock(1, 3);                                             // 062c3ad5 FUN_062d21d0
    ((SystemManager*)Field24)->GunGroups.SetShieldActive(1);         // 062c3add-062c3ae5 FUN_062e9a60
    Behavior::Slot4();                                               // 062c3aec FUN_062d20f0
    ModeState320 = 0;                                                // 062c3af2
}

// The three `IObjAffect slot 0x60` sites in Disengage (062c39ae-062c39f9,
// 062c3a51-062c3a9b, 062c3b1a-062c3b64) share one shape: Id = Watcher324's object
// (target - 8; a null target with a non-null -8 is impossible, and on the re-read retail
// calls through a null pointer when the target is gone -- reproduced literally) ->get_id,
// or 0 when the target or its -8 is null; then the owner's IObjAffect (+4) slot 0x60
// (RequestType, &Id, 0).
void DockBehavior::CancelOwnerRequest(int RequestType)
{
    unsigned int Id = 0;
    Watchable const* const Watched = Watcher324.target();
    if (Watched != nullptr && reinterpret_cast<char const*>(Watched) - 8 != nullptr)
    {
        Watchable const* const Again = Watcher324.target();
        void const* const Obj = Again != nullptr
            ? static_cast<void const*>(reinterpret_cast<char const*>(Again) - 8)
            : nullptr;
        Id = ObjInspectOf(Obj)->get_id();
    }
    ObjAffectOf(ThreatSourcePtr14)->slot_60(RequestType, &Id, 0);
}

// Own vtable slot 6 (offset 0x18). Common.dll.txt 188138-188244 (LAB_062c35f0).
// LAB_062c35f0 (Common.dll.txt 188138-188244), vtable slot 6 (evaluate), RET 4 -- the
// manager's context dword is not read. All 100 instructions traced. +0x14 is the owner
// object (the binder FUN_062d2090 stores it), read as an IObjInspect.
int DockBehavior::CheckClearanceProgress()
{
    if (AbortFlag398 != 0)                                           // 062c35f7
        return 1;

    // 062c360e-062c3629: the owner's target into a zeroed local; none -> 2.
    IObjRW* Candidate = nullptr;
    ObjInspectOf(ThreatSourcePtr14)->get_target(Candidate);          // +0x74
    if (Candidate == nullptr)
        return 2;

    unsigned int Type = 0;   // retail leaves [ESP+0x10] unset; get_type always writes it
    Candidate->get_type(Type);                                       // +0x88 (062c3638)
    if ((Type & 0x600de0) != 0)                                      // 062c3642
    {
        if ((Type & 0x8c0) == 0)                                     // 062c364d JNZ -> checks
        {
            if ((Type & 0x120) == 0)                                 // 062c3654
                return 2;
            if (!Candidate->has_dock_hardpoints())                   // +0xc4 (062c365f)
                return 2;
        }
    }
    else
    {
        // LAB_062c36db: `TEST AL,0x3` on the type's low byte.
        if ((Type & 0x3) == 0)
            return 2;
        unsigned int Base = 0;                                       // 062c36e8
        Candidate->get_base(Base);                                   // +0x8c
        if (Base == 0)
            return 2;
        // 062c36fe-062c370c: FUN_062e7400(NavHelper+0x34, Candidate); zero -> 2.
        if (static_cast<SystemManager*>(Field24)->Timers.FindObjectDockingAtBase(Candidate) == nullptr)
            return 2;
    }

    // LAB_062c3669: get_status into a local seeded with 1.0f; FCOMP 0.0f (DAT_06399408) /
    // TEST AH,0x41 / JNZ -> 2 on less, equal or unordered, i.e. go on only when > 0.
    float Health = 1.0f;
    Candidate->get_status(Health);                                   // +0x40
    if (!(Health > 0.0f))
        return 2;

    // 062c3690-062c36cc: |velocity|^2 = (x*x + y*y) + z*z on the FPU stack; FCOMP 0.3f
    // (DAT_0639f484 == 0x3e99999a) / TEST AH,0x5 / JNP -> 1 exactly on "less".
    Vector const Velocity = Candidate->get_velocity();               // +0x04
    double const SpeedSq = static_cast<double>(Velocity.x) * Velocity.x +
                           static_cast<double>(Velocity.y) * Velocity.y +
                           static_cast<double>(Velocity.z) * Velocity.z;
    return (SpeedSq < 0.3f) ? 1 : 2;
}

// Own vtable slot 12 (offset 0x30). Common.dll.txt 191005-191094 (LAB_062c5540).
// All 81 instructions traced. NavPilotPtr (+0x2f0) is the manager's dispatch-table Goto
// behavior (EnterState case 1 loads it from IBehaviorManager+0x17c), so its +0x30 / +0x28
// calls are Behavior::set_direction / Behavior::Slot10.
void DockBehavior::SubmitGotoDirective(Vector const& ReferencePoint)
{
    Behavior* const Pilot = static_cast<Behavior*>(GotoDirective.NavPilotPtr);
    if (Pilot == nullptr)                                            // 062c554c
        return;

    // 062c5554-062c5581: |Dir|^2 = (x*x + y*y) + z*z on the FPU; FCOMP 0.0f / TEST AH,0x44 /
    // JP. C3|C2: only "equal" gives odd parity (no jump); greater, less and unordered jump
    // to LAB_062c563d, which forwards the direction to the pilot unchanged.
    double const MagSq = static_cast<double>(ReferencePoint.x) * ReferencePoint.x +
                         static_cast<double>(ReferencePoint.y) * ReferencePoint.y +
                         static_cast<double>(ReferencePoint.z) * ReferencePoint.z;
    if (!(MagSq == 0.0))
    {
        Pilot->set_direction(ReferencePoint);                        // 062c5640 [EAX+0x30]
        return;
    }

    // A zero direction: send the pilot a goto directive instead.
    pub::AI::DirectiveGotoOp Op;                                     // 062c558b
    Op.Field_50 = 1000.0f;                       // 062c5590 .data:DAT_063ebdb4 (0x447a0000)
    Op.Field_58 = 1;                                                 // 062c55a1
    // 062c5599-062c55c2: the ProxWatcher450 object (target - 8) -> get_id, else 0.
    void* const ProxObj = HeaderBlockFromWatched(ProxWatcher450.Watcher.target());
    if (ProxObj != nullptr)
        Op.Field_5C = ObjInspectOf(ProxObj)->get_id();               // [EDX+0x20]
    else
        Op.Field_5C = 0;
    Op.Field_60 = ProxWatcher450.Unknown08;                          // 062c55c2-062c55d0
    Op.Field_64 = ProxWatcher450.Unknown0c;                          // 062c55d4-062c55da

    if ((StateFlags32c & 0xc0) != 0)                                 // 062c55c8 byte test
    {
        Op.Field_0C = 1;                                             // 062c55ee
        Op.Destination = DestPoint3cc;                               // 062c55e0-062c55fe
    }
    else
    {
        // LAB_062c5604: Field_0C = 0, Field_1C = Watcher324's object (target - 8, no null
        // test on the result) ->get_id.
        Op.Field_0C = 0;
        Watchable const* const Watched = Watcher324.target();
        void const* const Obj = Watched != nullptr
            ? static_cast<void const*>(reinterpret_cast<char const*>(Watched) - 8)
            : nullptr;
        Op.Field_1C = static_cast<int32_t>(ObjInspectOf(Obj)->get_id()); // 062c561f
    }

    Pilot->Slot10(&Op);                                              // 062c5633 [EAX+0x28]
}

// Own vtable slot 22 (offset 0x58). Common.dll.txt 190940-190996 (LAB_062c54b0).
// All 51 instructions traced. The one argument is a two-dword message {TypeCode, Payload}.
void DockBehavior::OnWatchedObjectDestroyed(void* Message)
{
    int const TypeCode = *static_cast<int const*>(Message);              // 062c54b7
    unsigned char const* const Payload =
        *reinterpret_cast<unsigned char const* const*>(static_cast<char const*>(Message) + 4);

    if (TypeCode == 4)
    {
        // 062c54bf-062c54fa: no null test on the payload. Byte 0 must be clear, the owner's
        // id must equal Payload+0xc and Watcher324's object's id (target - 8, no null test on
        // the result) must equal Payload+0x10.
        if (Payload[0] != 0)
            return;
        if (ObjInspectOf(ThreatSourcePtr14)->get_id() !=
            *reinterpret_cast<unsigned int const*>(Payload + 0xc))
            return;
        Watchable const* const Watched = Watcher324.target();
        void const* const Obj = Watched != nullptr
            ? static_cast<void const*>(reinterpret_cast<char const*>(Watched) - 8)
            : nullptr;
        if (ObjInspectOf(Obj)->get_id() != *reinterpret_cast<unsigned int const*>(Payload + 0x10))
            return;
        Flag399 = 1;                                                     // 062c54f3
        EnterState(0xc);                                                 // 062c54fa
        return;
    }

    if (TypeCode == 3)
    {
        // LAB_062c5504: a null payload is ignored; otherwise the SystemManager's external
        // interface id must equal Payload+8. No Flag399 store on this path.
        if (Payload == nullptr)
            return;
        SystemManager* const Sys = static_cast<SystemManager*>(Field24);
        if (*reinterpret_cast<unsigned int const*>(Payload + 0x8) !=
            Sys->SelfTarget.Iface->get_id())
            return;
        EnterState(0xc);                                                 // 062c552e
    }
    // Any other TypeCode: LAB_062c5533, RET 4.
}

// Common.dll.txt 188802-189470 / Common.dll.c:100735 FUN_062c3d10, decomp-tracker
// item 1279. NOT a vtable slot anywhere in the module; a plain non-virtual member of
// this class, called with an unadjusted `this` from RecomputeApproach()
// (.text:062c375b / 062c38c4), OnWatchedObjectDestroyed() and OnResult()
// (.text:062c5440..062c552e).
//
// MOVED HERE IN FULL 2026-09-17 (claude:common-dockmove). This function used to exist
// twice in the port: a two-case stub here, and the complete ten-case translation on
// FormationBehavior in common_behavior.cpp. There was never a reason for two copies --
// FormationBehavior's object is 0x2b8 bytes and this body writes as far as +0x46c, so
// it was never that class's. The complete translation is below, unchanged apart from
// field renames onto this class's own members. The "extending EnterState() to the
// remaining cases is a documented follow-up" note that stood here is discharged.
//
// The full translation was re-derived from the raw listing on 2026-09-17 by
// claude:common-formation, which fixed five things every earlier pass had wrong:
//   * the +0x2dc pre-switch call was a double deref; retail loads the vtable from
//     [this+0x2dc] and passes `this+0x2dc` itself (062c3d5e-062c3d6a).
//   * case 1 set the DirectiveGotoOp's +0x50 to 0.0f "from a global tunable"; the
//     tunable (.data:DAT_063ebdb4) is 1000.0f (062c3dad), and retail also fills
//     +0xc/+0x10..0x18 or +0x1c, +0x58, +0x5c, +0x60 and +0x64, then clears
//     NavPilotPtr[0x36c] after submitting (062c3e6f).
//   * case 4 called the disengage/threat-refresh pair; retail resets the MoveIdea and
//     the FaceIdea (062c419a-062c41b8).
//   * cases 2/6/7/9 drove the wrong vectors and dropped every DeviceTRC::bind_to_dc
//     radius and every MaxApproachSpeed46c write. The radii are 64.0f / 10.0f /
//     15.0f / 25.0f and Field408*DAT_0639f518; the timeouts are 10.0f (0x41200000) and
//     18.0f (.data:DAT_063ebdb8), never the generic 1000.0f those branches were left at.
//   * cases 0xc/0xd dropped the Flags28 |= 1 / own-slot-0x10 / Flags28 &= ~2 sequence
//     that precedes the FUN_062d29a0 call (062c44be-062c44ea).
void DockBehavior::EnterState(int NewState)
{
    if (NewState != 1 && ModeState320 == 1)
    {
        // 062c3d34-062c3d3c: the Goto pilot's Behavior slot 4, no null test in retail.
        static_cast<Behavior*>(GotoDirective.NavPilotPtr)->Slot4();
        // 062c3d45: FUN_062d21c0 with ECX == NavPilotPtr's value.
        Behavior_ReleaseDirectiveTarget(GotoDirective.NavPilotPtr);
        GotoDirective.NavPilotPtr = nullptr;                         // 062c3d53
        // 062c3d4a-062c3d59: ThrustManager::engage_cruise(false) on Field24 + 0x18.
        static_cast<SystemManager*>(Field24)->Thrust.engage_cruise(false);
        // 062c3d5e-062c3d6a: the embedded AfterburnerIdea at +0x2dc, BaseIdea slot 4.
        GotoDirective.Idea.Commit();
    }

    // 062c3d71: .rdata:DAT_0639f47c == 0x447a0000 == 1000.0f, unconditional.
    MaxApproachSpeed46c = 1000.0f;
    ModeState320 = NewState;

    switch (NewState)
    {
    case 1:
    {
        // caseD_1 at 062c3d95: +0x17c of the manager is BehaviorDispatchTable[2], the Goto
        // behavior. Retail reads it with no null test (the old port had one).
        GotoDirective.NavPilotPtr = static_cast<IBehaviorManager*>(Field20)->BehaviorDispatchTable[2];

        pub::AI::DirectiveGotoOp Op;
        // 062c3dad: .data:DAT_063ebdb4 == 0x447a0000 == 1000.0f.
        Op.Field_50 = 1000.0f;
        Op.Field_58 = 1;                              // 062c3dc3, BL

        // 062c3dbf-062c3de8: the ProxWatcher450 handle, "-8 header", slot 0x20.
        void* ProxObj = HeaderBlockFromWatched(ProxWatcher450.Watcher.target());
        if (ProxObj != nullptr)
        {
            Op.Field_5C = ((IObjRW*)ProxObj)->get_id();
        }
        else
        {
            Op.Field_5C = 0;
        }

        // 062c3de8 / 062c3df8: +0x458 is ProxWatcher450.Unknown08, read as a float;
        // FUN_062c2ad0(this+0x450, 0) is the far threshold unscaled (PUSH EBP == 0).
        Op.Field_60 = ProxWatcher450.Unknown08;
        Op.Field_64 = QueryHandleRange(&ProxWatcher450, 0);

        if ((StateFlags32c & 0xc0) != 0)
        {
            // 062c3e0d: Field_0C = 1, Destination <- the Vector at +0x3cc.
            Op.Field_0C = 1;
            Op.Destination = DestPoint3cc;
        }
        else
        {
            // LAB_062c3e2d: Field_0C = 0, Field_1C <- Watcher324's block, slot 0x20.
            Op.Field_0C = 0;
            // 062c3e2d-062c3e47: get_id() (+0x20) on `raw ? raw - 8 : 0` with no test, as retail.
            Op.Field_1C = static_cast<int32_t>(
                ObjRwFromWatchable(const_cast<Watchable*>(Watcher324.target()))->get_id());
        }

        // 062c3e4b-062c3e6f: the Goto behavior's slot 10 with the op, its slot 3, then its
        // +0x36c byte cleared -- all on [this+0x2f0] with no null test (the old port had one).
        GotoBehavior* const Pilot = static_cast<GotoBehavior*>(
            static_cast<Behavior*>(GotoDirective.NavPilotPtr));
        Pilot->Slot10(&Op);                                          // 062c3e58 vtbl+0x28
        Pilot->Slot3();                                              // 062c3e63 vtbl+0x0c
        Pilot->Flag36c = 0;                                          // 062c3e6f
        return;
    }
    case 2:
    {
        // caseD_2 at 062c3e80.
        SetCruiseLock(1, 3);   // FUN_062d21d0(this, EBX == 1, 3)

        if ((StateFlags32c & 0xc0) != 0)
        {
            // 062c3e97-062c4148, ported 2026-10-08 (claude-code:cmn-ai5) from the x87 sequence.
            // Each FSTP dword is a float below; everything kept on the FPU stack is double.
            NavHelperRefreshLeadPoint();                             // 062c3e97-062c3ebd
            SystemManager* const Sys = static_cast<SystemManager*>(Field24);

            // 062c3ec0-062c3eeb: D = own cached centre - DestPoint3cc, each FSTP float.
            Vector Delta;
            Delta.x = static_cast<float>(static_cast<double>(Sys->SelfTarget.CachedPos.x) - DestPoint3cc.x);
            Delta.y = static_cast<float>(static_cast<double>(Sys->SelfTarget.CachedPos.y) - DestPoint3cc.y);
            Delta.z = static_cast<float>(static_cast<double>(Sys->SelfTarget.CachedPos.z) - DestPoint3cc.z);

            // 062c3eef-062c3f23: the watched object's get_transform() (+0x0c) -- retail calls it
            // on `raw ? raw - 8 : 0` without a test -- rotation column 0, then FUN_062e6010
            // (Behavior_GetIfaceMaxScalarAttribute) on the same object.
            IObjRW* const Watched = ObjRwFromWatchable(const_cast<Watchable*>(Watcher324.target()));
            Matrix const& Rot0 = Watched->get_transform().Rotation;
            float const Col0x = Rot0.E[0][0];
            float const Col0y = Rot0.E[1][0];
            float const Col0z = Rot0.E[2][0];
            double const Size = Behavior_GetIfaceMaxScalarAttribute(Watched);

            // 062c3f28-062c3f79: Offset = column0 * Size * 2 * (1/3) (DAT_0639f520 == 0x3eaaaaab).
            // x stays on the FPU throughout; y and z are FSTP'd after the multiply by Size,
            // and z once more after the doubling.
            double const OneThird = 0.3333333432674408;
            Vector Offset;
            Offset.x = static_cast<float>((static_cast<double>(Col0x) * Size * 2.0) * OneThird);
            float const ScaledY = static_cast<float>(static_cast<double>(Col0y) * Size);
            Offset.y = static_cast<float>((static_cast<double>(ScaledY) * 2.0) * OneThird);
            float const ScaledZ = static_cast<float>(static_cast<double>(Col0z) * Size);
            float const DoubledZ = static_cast<float>(static_cast<double>(ScaledZ) * 2.0);
            Offset.z = static_cast<float>(static_cast<double>(DoubledZ) * OneThird);

            // 062c3f7d-062c3fd8: ((Oy*Dy + Ox*Dx) + Oz*Dz) against 0.0f; TEST AH,0x41 + JP
            // keeps the offset on greater or unordered, otherwise it is negated.
            double const Facing = static_cast<double>(Offset.y) * Delta.y
                                + static_cast<double>(Offset.x) * Delta.x
                                + static_cast<double>(Offset.z) * Delta.z;
            if (Facing <= 0.0)
            {
                Offset.x = -Offset.x;
                Offset.y = -Offset.y;
                Offset.z = -Offset.z;
            }

            // 062c3fdc-062c4042: Spread = lerp(-128, 128, rand() * (1/32768)) (DAT_0639f490 ==
            // 128.0f, DAT_063997d0 == 1/32768f); the rand() product is stored as a double,
            // the result as a float: ((A - (-A)) * t) + (-A).
            float const SpreadHalf = 128.0f;
            float const SpreadLow = -SpreadHalf;
            double const RandUnit = static_cast<double>(rand()) * 3.0517578125e-05;
            float const Spread = static_cast<float>(
                (static_cast<double>(SpreadHalf) - SpreadLow) * RandUnit + SpreadLow);

            // 062c3ffb-062c4072: rotation column 1 of a second get_transform() call, times Spread,
            // plus Offset. x goes through a float temporary first; z stays on the FPU.
            Matrix const& Rot1 = Watched->get_transform().Rotation;
            float const Col1x = Rot1.E[0][1];
            float const Col1y = Rot1.E[1][1];
            float const Col1z = Rot1.E[2][1];
            float const SpreadX = static_cast<float>(static_cast<double>(Col1x) * Spread);
            float const JitterX = static_cast<float>(static_cast<double>(SpreadX) + Offset.x);
            float const JitterY = static_cast<float>(static_cast<double>(Col1y) * Spread + Offset.y);
            double const JitterZ = static_cast<double>(Col1z) * Spread + Offset.z;

            // 062c4076-062c40ed: InterceptPoint39c = DestPoint3cc + (64 - 200) * Facing38c +
            // jitter (DAT_0639f480 == 64.0f, DAT_0639f488 == 200.0f). Facing x stays on the FPU,
            // y and z are FSTP'd after the multiply, z once more after adding Dest.z.
            double const Back = 64.0 - 200.0;
            double const BackX = Back * Facing38c.x;
            float const BackY = static_cast<float>(Back * Facing38c.y);
            float const BackZ = static_cast<float>(Back * Facing38c.z);
            double const BaseX = BackX + DestPoint3cc.x;
            double const BaseY = static_cast<double>(BackY) + DestPoint3cc.y;
            float const BaseZ = static_cast<float>(static_cast<double>(BackZ) + DestPoint3cc.z);
            InterceptPoint39c.x = static_cast<float>(BaseX + JitterX);
            InterceptPoint39c.y = static_cast<float>(BaseY + JitterY);
            InterceptPoint39c.z = static_cast<float>(static_cast<double>(BaseZ) + JitterZ);

            // 062c40f2-062c4148: Field338 from the watched object's size -- times 0.5f
            // (DAT_0639c13c) when the type's 0x80 bit is set (JNS on the low byte), times 1/3
            // when only 0x40 is; neither leaves it alone.
            unsigned int const TypeBits = StateFlags32c;
            if ((TypeBits & 0x80) != 0)
                Field338 = static_cast<float>(
                    static_cast<double>(Behavior_GetIfaceMaxScalarAttribute(Watched)) * 0.5);
            else if ((TypeBits & 0x40) != 0)
                Field338 = static_cast<float>(
                    static_cast<double>(Behavior_GetIfaceMaxScalarAttribute(Watched)) * OneThird);
        }

        // LAB_062c4151 -- the unconditional tail, reached from both branches.
        // Receiver is `LEA EDI,[ESI+0x30]` for everything but the FUN_062d7560.
        SubObjectA.SetMode(0);                           // 062c4157 FUN_062d5980
        SubObjectA.SetRange3c(64.0f);                    // 062c4165 bind_to_dc fold, .rdata:DAT_0639f480 == 0x42800000
        SubObjectA.SetPosition(InterceptPoint39c);       // 062c4173 FUN_062d5840
        SubObjectB.SetPosition(InterceptPoint39c);       // 062c417f FUN_062d7560
        SubObjectA.SetField44(0);                        // 062c4188 FUN_062d88b0
        return;
    }
    case 4:
        // caseD_4 at 062c419a -- three calls, all on the sub-objects. The two
        // `CALL [<vtbl>+0x10]` are BaseIdea slot 4 == Commit() on each.
        SubObjectA.SetField44(0);                        // 062c41a0 FUN_062d88b0
        SubObjectA.Commit();                             // 062c41a9 [EAX+0x10], ECX == this+0x30
        SubObjectB.Commit();                             // 062c41b8 [EDX+0x10], ECX == this+0xa8
        return;
    case 6:
        // caseD_6 at 062c41c8. `TEST AL,0xc0` then `TEST AH,8` on the same dword.
        if ((StateFlags32c & 0xc0) != 0)
        {
            SubObjectB.SetAngleWeight8c(BitsToFloat(0x3ec90fdbu)); // 062c41df FUN_062d93c0, pi/8
            SubObjectB.SetPosition(DestPoint3cc);           // 062c41ed FUN_062d7560
            SubObjectA.SetPosition(DestPoint3cc);           // 062c41f8 FUN_062d5840
            SubObjectA.SetRange3c(10.0f);                   // 062c4204 bind_to_dc fold, 0x41200000
            SubObjectA.SetField44(2);                       // 062c420d FUN_062d88b0
            MaxApproachSpeed46c = 10.0f;               // 062c4213
            return;
        }
        if ((StateFlags32c & 0x800) != 0)
            break;                                          // 062c422c -> default

        // LAB_062c4229 tail. FUN_062d75c0 is FaceIdea::SetBasis (nine dwords) on Orient3a8.
        SubObjectB.SetBasis(Orient3a8);                     // 062c423f FUN_062d75c0
        SubObjectA.SetPosition(InterceptPoint39c);          // 062c4250 FUN_062d5840
        SubObjectA.SetRange3c(10.0f);                       // 062c425e bind_to_dc fold, .data:DAT_063ebdbc
        SubObjectA.SetMode(1);                              // 062c4266 FUN_062d5980, EBX == 1
        MaxApproachSpeed46c = 18.0f;                   // .data:DAT_063ebdb8 == 0x41900000
        // FUN_062d5a80 copies three dwords into receiver+0x4c == MoveIdea::Offset4c
        // (.text:062d5a80-062d5a97); it is NOT one of the tagged target setters.
        SubObjectA.Offset4c = *static_cast<Vector const*>(RawAt(0x430)); // 062c427f FUN_062d5a80
        return;
    case 7:
        // caseD_7 at 062c4291.
        SetCruiseLock(0, 3);
        SubObjectA.SetField44(2);                           // 062c42a2 FUN_062d88b0
        SubObjectA.Offset4c = *static_cast<Vector const*>(RawAt(0x430)); // 062c42b0 FUN_062d5a80
        if (CombatFlag344 != 0)
        {
            SubObjectB.SetPosition(SavedPosition3fc);       // 062c42cc FUN_062d7560
            SubObjectA.SetMode(0);                          // 062c42d5 FUN_062d5980
            SubObjectA.SetPosition(SavedPosition3fc);       // 062c42dd FUN_062d5840
            SubObjectA.SetRange3c(25.0f);                   // 062c42eb bind_to_dc fold, .data:DAT_063ebdc4
            return;
        }
        // LAB_062c42fd.
        SubObjectB.SetBasis(OrientMatrix3d8);               // 062c4304 FUN_062d75c0
        SubObjectA.SetPosition(DestPoint3cc);               // 062c4312 FUN_062d5840
        SubObjectA.SetRange3c(15.0f);                       // 062c4320 bind_to_dc fold, .data:DAT_063ebdc0
        SubObjectA.SetMode(1);                              // 062c4328 FUN_062d5980
        MaxApproachSpeed46c = 18.0f;                   // .data:DAT_063ebdb8
        return;
    case 8:
        // caseD_8 at 062c4345. FUN_062da400's receiver is the SplineMoveIdea
        // at +0x168 (062c4358), not the behavior.
        BuildFinalApproachSpline();                         // 062c4347 FUN_062c56d0
        SetCruiseLock(0, 3);
        SubObjectC.SetField158(2);                          // 062c435e FUN_062da400
        return;
    case 9:
    {
        // caseD_9 at 062c4370.
        SetCruiseLock(0, 3);
        SubObjectA.SetPosition(SavedPosition3fc);           // 062c4386 FUN_062d5840
        SubObjectA.Offset4c = *static_cast<Vector const*>(RawAt(0x430)); // 062c4394 FUN_062d5a80
        // 062c4399-062c43a8: FLD float Field408, FMUL double DAT_0639f518 (0.6), FSTP dword as
        // the bind_to_dc argument -- one rounding of the double product (the old port
        // multiplied two floats).
        SubObjectA.SetRange3c(static_cast<float>(
            static_cast<double>(Field408) * kEnterState9RadiusScale));      // 062c43ab

        if ((StateFlags32c & 0xc0) != 0)
        {
            SubObjectB.SetDirection(Facing38c);             // 062c43c7 FUN_062d7590
            SubObjectA.SetField44(2);                       // 062c43d0 FUN_062d88b0
            return;
        }
        if ((StateFlags32c & 0x800) != 0)
        {
            // LAB_062c43e2's taken branch. Every receiver here is `MOV ECX,EDI`
            // with EDI == this+0x30 (LEA at 062c4380), i.e. SubObjectA.
            SubObjectA.SetMode(0);                          // 062c43eb FUN_062d5980
            // FUN_062d5a70 writes MoveIdea+0x59, NOT the +0x58 SetEnabled()
            // writes -- two adjacent bytes, two different one-instruction
            // functions ten bytes apart. See MoveIdea::SetField59.
            SubObjectA.SetField59(0);                       // 062c43f4 FUN_062d5a70
            SubObjectA.SetAngle38(BitsToFloat(0x3eb2b8c3u)); // 062c4400 set_origin_h fold
            SubObjectA.SetField44(2);                       // 062c4409 FUN_062d88b0
            SubObjectB.SetPosition(SavedPosition3fc);       // 062c4415 FUN_062d7560
            return;
        }
        // LAB_062c4427. Field20 is IBehaviorManager::lock_maneuvers' receiver -- item
        // 1277 calls that function on it BY LINKED SYMBOL NAME, which is what settled
        // the field's type. Stored as void*, so the cast is explicit here.
        // Retail has no null test on Field20 here (the old port did).
        static_cast<IBehaviorManager*>(Field20)->lock_maneuvers(true);   // 062c442b
        SubObjectA.SetField44(0);                           // 062c4434 FUN_062d88b0
        if (CombatFlag344 != 0)
        {
            SubObjectB.SetPosition(SavedPosition3fc);       // 062c444a FUN_062d7560
            SubObjectA.SetMode(0);                          // 062c4453 FUN_062d5980
            return;
        }
        // LAB_062c4465. +0x40c's width is unproven, same as +0x3a8 -- raw reach,
        // but FUN_062d75c0 copies nine dwords, so the span is a 3x3 Matrix.
        SubObjectB.SetBasis(*static_cast<Matrix const*>(RawAt(0x40c))); // 062c446c FUN_062d75c0
        SubObjectA.SetMode(1);                              // 062c4474 FUN_062d5980
        MaxApproachSpeed46c = 18.0f;                   // .data:DAT_063ebdb8
        if ((StateFlags32c & 0x20) != 0)
            break;                                          // 062c448c -> default
        // 062c448e-062c4499: FUN_062e9a60 = Field24->GunGroups.SetShieldActive, 0 here (the
        // Disengage site pushes 1).
        ((SystemManager*)Field24)->GunGroups.SetShieldActive(0);
        return;
    }
    case 0xb:
        // caseD_b at 062c44ab: `MOV dword ptr [ESI+0x340],EBP` with EBP == 0.
        StateTimer340 = 0.0f;
        return;
    case 0xc:
        // caseD_c at 062c44d1.
        Flags28 = static_cast<unsigned char>(Flags28 | 1);
        Disengage();                                        // own vtbl slot 4 (offset 0x10)
        Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
        static_cast<IBehaviorManager*>(Field20)->SetDirectiveResult(7, 0);   // FUN_062d29a0
        return;
    case 0xd:
        // caseD_d at 062c44be -- same sequence, result 2.
        Flags28 = static_cast<unsigned char>(Flags28 | 1);
        Disengage();
        Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
        static_cast<IBehaviorManager*>(Field20)->SetDirectiveResult(7, 2);   // FUN_062d29a0
        return;
    default:
        // caseD_3/caseD_5/caseD_a and anything outside 1..0xd: nothing beyond the
        // MaxApproachSpeed46c/ModeState320 writes above.
        break;
    }
}

// ----------------------------------------------------------------------------
// FULLPORT-DEFERRED sub-object constructors -- see common_dockbehavior.h's
// class-level comment and the GotoDirective field comment.
// SubObjectA/B (FUN_062d4e60/FUN_062d6750) are IDENTIFIED as real MoveIdea/
// FaceIdea (2026-09-11, d285:common-port) and SubObjectC (FUN_062d9bf0) as a
// real SplineMoveIdea (2026-09-17, claude:abi-pass50) -- none of the three
// needs a stand-in any more, see the ctor above. GotoDirective's embedded object
// (FUN_062da500) is an AfterburnerIdea member since 2026-10-08.

// ----------------------------------------------------------------------------
// FULLPORT-DEFERRED stand-ins shared (by name/role, not by literal code) with
// FormationBehavior's own equivalents in common_behavior.cpp -- see each
// call site above for the retail behavior each one stands in for.
// ----------------------------------------------------------------------------
// ReleaseGotoDirective is GONE (2026-09-17, claude:orch-dockpreamble). It was an
// empty `static void ReleaseGotoDirective()` -- and `static` was the reason it could
// not be written: retail's FUN_062d21c0 is a __thiscall whose first instruction is
// `MOV AL,[ECX+0x28]`, and at both of this class's call sites ECX is the value of
// GotoDirective.NavPilotPtr rather than `this`. It is five instructions and is now
// Behavior_ReleaseDirectiveTarget (common_behavior.h/.cpp), shared with the two
// GuideBehavior sites that had the same body stubbed under a second guessed name.
// SetCruiseLock (FUN_062d21d0) is Behavior::SetCruiseLock now (common_behavior.cpp).
// SetAlertLevel is GONE (2026-09-17, claude:w21-stubsweep). It was a guessed
// name for FUN_062d88b0, whose receiver is SubObjectA at every one of this
// class's eleven call sites, and which is MoveIdea::SetField44(int) --
// a real, ported one-line method in common_idea.h. Every site now calls it.
// FUN_062d29a0 is IBehaviorManager::SetDirectiveResult (common_behaviormanager.cpp), called on Field20.

// FUN_062c50e0 (Common.dll.txt, .text:062c50e0-062c53e7), 207 instructions, all traced.
void DockBehavior::RefreshApproachState()
{
    // 062c50e9-062c510c: Field43c < -1 (signed JL) or no Watcher324 object -> return.
    if (Field43c < -1)
        return;
    Watchable const* const Watched = Watcher324.target();
    if (Watched == nullptr || reinterpret_cast<char const*>(Watched) - 8 == nullptr)
        return;

    // 062c5112-062c5148: object->get_dock_hardpoints(Field43c, &SubStateValue440, &A, &B,
    // &C, &Field408) (IObjInspect +0xc8; the object is re-read as target - 8).
    IObjInspect const* const Obj = ObjInspectOf(reinterpret_cast<char const*>(Watched) - 8);
    Transform XformA;   // [ESP+0x40] local_90 -- left uninitialised by retail
    Transform XformB;   // [ESP+0x70] local_60
    Transform XformC;   // [ESP+0xa0] local_30
    Obj->get_dock_hardpoints(Field43c, reinterpret_cast<TERMINAL_TYPE*>(&SubStateValue440),
                             &XformA, &XformB, &XformC, &Field408);
    SavedPosition3fc = XformA.Translation;                           // 062c514e-062c5165
    Orient40c = XformA.Rotation;                                     // 062c5175-062c5184
    DestPoint3cc = XformB.Translation;                               // 062c516f-062c519b
    OrientMatrix3d8 = XformB.Rotation;                               // 062c51a5-062c51b4
    InterceptPoint39c = XformC.Translation;                          // 062c51b6-062c51d1
    Orient3a8 = XformC.Rotation;                                     // 062c51d4-062c51e6

    CombatFlag344 = 0;                                               // 062c51f3
    if ((StateFlags32c & 0x200100) == 0)                             // 062c51e8 / 062c51fb
    {
        // 062c5201-062c5256: D1 = DestPoint3cc - InterceptPoint39c, D2 = SavedPosition3fc -
        // DestPoint3cc (float subtractions, Vec3Assign).
        Vector D1;
        D1.x = DestPoint3cc.x - InterceptPoint39c.x;
        D1.y = DestPoint3cc.y - InterceptPoint39c.y;
        D1.z = DestPoint3cc.z - InterceptPoint39c.z;
        Vector D2;
        D2.x = SavedPosition3fc.x - DestPoint3cc.x;
        D2.y = SavedPosition3fc.y - DestPoint3cc.y;
        D2.z = SavedPosition3fc.z - DestPoint3cc.z;

        // 062c525b-062c5299: acos(ClampedDot3(normalize(D1), normalize(D2))) (D2 normalized
        // first); FCOMP DAT_0639f53c (0x3dc90fdb == float pi / 32) / TEST AH,0x5 / JP --
        // the block below runs only when the angle is strictly smaller.
        Vector N2;
        VecNormalize3(&D2, &N2);
        Vector N1;
        VecNormalize3(&D1, &N1);
        double const Angle = std::acos(ClampedDot3(N1, N2));
        if (Angle < 3.14159274f / 32.0f)
        {
            // 062c529f-062c5343: S = (colZ(Orient40c) + colZ(OrientMatrix3d8)) +
            // colZ(Orient3a8), each sum FSTP'd to float through Vec3Assign.
            Vector Sum1;
            Sum1.x = Orient40c.E[0][2] + OrientMatrix3d8.E[0][2];
            Sum1.y = Orient40c.E[1][2] + OrientMatrix3d8.E[1][2];
            Sum1.z = Orient40c.E[2][2] + OrientMatrix3d8.E[2][2];
            Vector Sum2;
            Sum2.x = Sum1.x + Orient3a8.E[0][2];
            Sum2.y = Sum1.y + Orient3a8.E[1][2];
            Sum2.z = Sum1.z + Orient3a8.E[2][2];
            // 062c5343-062c537e: ClampedDot3(D2, -Sum2) (D2 un-normalized); FCOMP 0.9999f
            // (DAT_0639f538 == 0x3f7ff972) / TEST AH,0x41 / JNZ -- strictly greater sets
            // the flag.
            Vector NegSum;
            NegSum.x = -Sum2.x;
            NegSum.y = -Sum2.y;
            NegSum.z = -Sum2.z;
            if (ClampedDot3(D2, NegSum) > 0.9999f)
                CombatFlag344 = 1;                                   // 062c5380
        }
    }

    // LAB_062c5387: with StateFlags32c bit 6, Facing38c = normalize(SavedPosition3fc -
    // DestPoint3cc).
    if ((StateFlags32c & 0x40) != 0)
    {
        Vector D3;
        D3.x = SavedPosition3fc.x - DestPoint3cc.x;
        D3.y = SavedPosition3fc.y - DestPoint3cc.y;
        D3.z = SavedPosition3fc.z - DestPoint3cc.z;
        Vector N3;
        VecNormalize3(&D3, &N3);
        Facing38c = N3;                                              // 062c53c6-062c53dc
    }
}

// ----------------------------------------------------------------------------
// FUN_062c4900 -- Common.dll.txt 189892-190605 / Common.dll.c:101167,
// decomp-tracker item 1285. Update()'s non-virtual tail; see the declaration in
// common_dockbehavior.h for the ownership proof and the move history.
//
// MOVED HERE 2026-09-17 (claude:common-dockmove) from common_behavior.cpp, where it
// was hosted on FormationBehavior. The body is the 2026-09-17 re-derivation by
// claude:common-formation, unchanged apart from renaming its fields onto this class's
// own members -- it already addressed this class's sub-object offsets through raw
// accessors, which are now the real typed members SubObjectA/B/C.
//
// Each of these was wrong in the port before that re-derivation, and is now what the
// retail bytes say:
//   * case 2's give-up gate tested only `OwnerContext == nullptr`; retail tests
//     `TargetShipPtr388 == 0 && !ObjInspect(ThreatSourcePtr14)->slot_bc() &&
//     (char)StateFlags32c < 0` (062c4a2c-062c4a4b), and derefs the context
//     unconditionally.
//   * LAB_062c4c04's watcher result was allowed to fall into the "enter hold" tail
//     even when it was not 1; retail goes to the default case (062c4c2f).
//   * case 3 wrote the +0x2a8 flag; retail writes +0x39a (062c4cb2), and the tail is
//     an IObjAffect slot-0x5c call with a mode of 2 (only when the StateFlags32c sign
//     bit is set) or 0, not a behavior-done notification.
//   * case 8 drove the MoveFx sub-object; retail drives the SplineMoveIdea at +0x168
//     for the tick, the reset and the pi/16 angle read alike (062c4dad-062c4de7).
//   * case 9's clearance test was inverted: retail enters state 0xd when the
//     IObjAffect slot-0x64 query returns NON-zero (062c4f44).
//   * case 0xb's threshold is 3.0f, or .rdata:DAT_0639e884 == 10.0f (bytes 00 00 20 41)
//     on the not-single-player, player-owner branch (062c5011 / 062c503d).
int DockBehavior::UpdateApproachTail()
{
    // Retail keeps the eventual result in a stack local ([ESP+0x14], preset to
    // EBX == 1) and stores it into UpdateResult10@0x10 on every exit.
    UpdateResult10 = 1;
    AdvancePreamble();  // FUN_062c47f0, ECX == this

    float const DeltaTime = GetTickDelta();   // Behavior's own +0x1c, see common_behavior.h

    if (ModeState320 == 1)
    {
        // 062c4921-062c493b: the Goto behavior's +0x1c tick delta = ours, then its slot 7
        // (vtable +0x1c); anything but 2 leaves through the default case. Retail tests
        // NavPilotPtr for null nowhere (the old port did, 2026-10-08).
        Behavior* const Pilot = static_cast<Behavior*>(GotoDirective.NavPilotPtr);
        *reinterpret_cast<float*>(reinterpret_cast<char*>(Pilot) + 0x1c) = DeltaTime;
        if (Pilot->Slot7() != 2)
            return UpdateResult10;

        if ((StateFlags32c & 0xc0) != 0)
        {
            EnterState(2);
            UpdateResult10 = 1;
            return UpdateResult10;
        }
        // retail: goto LAB_062c4c47 (shared with case 2's "else" tail)
        EnterState(3);
        UpdateResult10 = 1;
        return UpdateResult10;
    }

    bool bAborted = false;
    if (ModeState320 != 0xc)
    {
        // 062c496b-062c499d: get_status(float& Health) (+0x40) into a local preset to 1.0f;
        // FCOMP 0.0f with TEST AH,0x41 + JNZ aborts on not-greater OR unordered. Retail calls
        // it on `raw ? raw - 8 : 0` with no test, and so does the port.
        float Health = 1.0f;
        ObjRwFromWatchable(const_cast<Watchable*>(Watcher324.target()))->get_status(Health);
        if (!(Health > 0.0f))
            bAborted = true;
    }

    {
        void* ProxWatched = const_cast<void*>(
            static_cast<void const*>(ProxWatcher450.Watcher.target()));
        if (bAborted ||
            ((ProxWatched != nullptr) && (ProxWatched != reinterpret_cast<void*>(8)) &&
             ((ProxWatcher450.Flags14 & 2) == 0)))
        {
            // LAB_062c49b9: `LEA EDI,[ESI+0x30]` is the receiver for all three.
            SubObjectA.SetField44(0);     // 062c49bf FUN_062d88b0
            SubObjectA.Commit();          // 062c49c6 [EDI]+0x10, BaseIdea slot 4
            SubObjectB.Commit();          // 062c49d5 [EAX]+0x10 on this+0xa8
            UpdateResult10 = 1;
            return UpdateResult10;
        }
    }

    if (!CheckCanProceed())
    {
        Disengage();            // own vtbl slot 4 (offset 0x10), LAB_062c3910
        RecomputeApproach();    // own vtbl slot 3 (offset 0x0c), LAB_062c3720
        UpdateResult10 = 1;
        return UpdateResult10;
    }

    switch (ModeState320)
    {
    case 2:
    {
        // 062c4a2c-062c4a4b. All three conditions, in retail's order; ThreatSourcePtr14 is
        // dereferenced with no null test (the old port had one, removed 2026-10-08).
        if ((TargetShipPtr388 == nullptr) &&
            !ObjInspectOf(ThreatSourcePtr14)->is_player() &&                 // vtbl+0xbc
            (static_cast<char>(StateFlags32c) < 0))
        {
            EnterState(0xc);
            UpdateResult10 = 1;
            return UpdateResult10;
        }

        bool bPastDock = false;
        if ((StateFlags32c & 0xc0) != 0)
        {
            // 062c4a72-062c4af7 (ported 2026-10-08; the dot was never evaluated before, so
            // the dot < 0 exit to LAB_062c4c35 was missing): radius-cache refresh, D = cached
            // centre - DestPoint3cc through Vec3Assign (FSTP float each), then
            // ((Dz*Fz + Dy*Fy) + Dx*Fx) on the FPU against 0.0f with TEST AH,0x5 + JP: >= 0 or
            // unordered falls to LAB_062c4c04.
            NavHelperRefreshLeadPoint();
            SystemManager* const Sys = static_cast<SystemManager*>(Field24);
            Vector Delta;
            Delta.x = Sys->SelfTarget.CachedPos.x - DestPoint3cc.x;
            Delta.y = Sys->SelfTarget.CachedPos.y - DestPoint3cc.y;
            Delta.z = Sys->SelfTarget.CachedPos.z - DestPoint3cc.z;
            double const Along = static_cast<double>(Delta.z) * Facing38c.z
                               + static_cast<double>(Delta.y) * Facing38c.y
                               + static_cast<double>(Delta.x) * Facing38c.x;
            // 062c4afd-062c4b08: FCOMP 200.0f (DAT_0639f488), TEST AH,0x41 + JNZ to
            // LAB_062c4c35 on not-greater -- always taken for a negative value, so
            // 062c4b0e-062c4bfe is unreachable in retail (same shape as CheckCanProceed).
            bPastDock = Along < 0.0;
        }

        if (!bPastDock)
        {
            // LAB_062c4c04. The FaceIdea ticks first, its result discarded; the MoveIdea's
            // result decides between the tail and the default case.
            (void)SubObjectB.Update(DeltaTime);
            if (SubObjectA.Update(DeltaTime) != 1)
                break;   // 062c4c2f JZ switchD_062c4a25::caseD_4
        }

        // LAB_062c4c35.
        EnterState(((StateFlags32c & 0x8c0) == 0) ? 6 : 3);
        UpdateResult10 = 1;
        return UpdateResult10;
    }
    case 3:
    {
        // 062c4c5d. Retail's EBP (the slot-0x5c mode argument) and the [ESP+0x1c]
        // out-slot both start at 0 and are only filled in on the sign-bit branch.
        int SlotMode = 0;
        int TargetQuery = 0;
        if (static_cast<char>(StateFlags32c) < 0)
        {
            // 062c4c6f-062c4c88: SavedPosition3fc@0x3fc <- DestPoint3cc@0x3cc, copied
            // dword by dword.
            SavedPosition3fc = DestPoint3cc;
            // 062c4c8b-062c4c96: TargetShipPtr388's get_id (IObjInspect slot 8, +0x20), no
            // null test.
            TargetQuery = static_cast<int>(ObjInspectOf(TargetShipPtr388)->get_id());
            SlotMode = 2;
        }
        if (ApproachClearedFlag334 != 0)
        {
            // 062c4ca9 -> LAB_062c4cfe, shared with case 2's hold exit.
            EnterState(6);
            UpdateResult10 = 1;
            return UpdateResult10;
        }

        EnterState(4);
        Flag39a = 1;   // 062c4cb2.

        // 062c4cb8-062c4cd1: the docking object's get_id (+0x20) on `W ? W - 8 : 0`, no null
        // test.
        int const EngineQuery = static_cast<int>(
            ObjInspectOf(HeaderBlockFromWatched(Watcher324.target()))->get_id());

        // 062c4cd5-062c4cec: five-argument call on ThreatSourcePtr14's IObjAffect
        // sub-object (`LEA ECX,[EAX+4]`), slot 0x5c, with `this+0x2c` last -- which is
        // this object's own IObjAffect::RequestCB base sub-object, so the cast below is
        // the typed equivalent of retail's LEA. Pushes, right to left: EDI (the
        // RequestCB), 0, &[ESP+0x1c] (TargetQuery), &[ESP+0x20] (EngineQuery), EBP
        // (SlotMode). Slot 23's shape is fixed by Server.dll's Ship body .text:06d02670
        // (RET 0x14; both id pointers are dereferenced as dwords) -- see common_iface.h.
        // The result in EAX is not read (062c4cef reloads EAX from the stack). No null
        // test on ThreatSourcePtr14 (the port's former guard is gone).
        (void)ObjAffectOf(ThreatSourcePtr14)->slot_5c(
            SlotMode, reinterpret_cast<unsigned const*>(&EngineQuery),
            reinterpret_cast<unsigned const*>(&TargetQuery), 0,
            static_cast<IObjAffect::RequestCB*>(this));
        UpdateResult10 = 1;
        return UpdateResult10;
    }
    case 6:
        // 062c4d14: `TEST AH,8` on the dword at +0x32c, i.e. bit 0x800.
        if ((StateFlags32c & 0x800) != 0)
        {
            EnterState(9);          // LAB_062c4dca, shared with case 8
            UpdateResult10 = 1;
            return UpdateResult10;
        }
        {
            int MoveResult = SubObjectA.Update(DeltaTime);
            int FaceResult = SubObjectB.Update(DeltaTime);
            if (((StateFlags32c & 0xc0) != 0) && (FaceResult == 1))
            {
                EnterState(8);
                UpdateResult10 = 1;
                return UpdateResult10;
            }
            if (MoveResult != 1)
                break;              // 062c4d6c JNZ default
            EnterState((CombatFlag344 != 0) ? 9 : 7);
            UpdateResult10 = 1;
            return UpdateResult10;
        }
    case 7:
        // 062c4e0e. Face first, then MoveIdea; both results but only the second one is
        // tested. Falls into case 10's tail either way.
        (void)SubObjectB.Update(DeltaTime);
        if (SubObjectA.Update(DeltaTime) == 1)
            EnterState(9);
        [[fallthrough]];
    case 10:
        // caseD_a: retail stores EBP, which is 0 on every path reaching here.
        UpdateResult10 = 0;
        return UpdateResult10;
    case 8:
    {
        // 062c4daa. The receiver is `LEA EDI,[ESI+0x168]` -- SubObjectC, this class's
        // SplineMoveIdea -- for the tick, the reset and the angle read alike.
        if (SubObjectC.Update(DeltaTime) == 1)
        {
            SubObjectC.Commit();              // 062c4dc5, vtbl slot 0x10 == BaseIdea slot 4
            EnterState(9);                    // falls into LAB_062c4dca
            UpdateResult10 = 1;
            return UpdateResult10;
        }
        // 062c4de2: FUN_062da3f0 (SplineMoveIdea::GetFaceAngle) on the SAME +0x168 receiver,
        // compared against .rdata:DAT_0639f534 == 0x3e490fdb == 0.19634955f (pi/16).
        // `TEST AH,5; JNP` leaves for the default case only when strictly less; unordered
        // builds the spline.
        if (!(SubObjectC.GetFaceAngle() < 0.19634955f))
        {
            BuildFinalApproachSpline();       // FUN_062c56d0, ECX == this
            UpdateResult10 = 1;
            return UpdateResult10;
        }
        break;
    }
    case 9:
    {
        // caseD_9 at 062c4e4c. Retail spills EBP (== 0) into the result slot first, so
        // every exit from this case that does not set it again returns 0.
        UpdateResult10 = 0;
        (void)SubObjectB.Update(DeltaTime);
        int MoveResult = SubObjectA.Update(DeltaTime);

        if (static_cast<char>(StateFlags32c) < 0)
        {
            if (MoveResult != 1)
            {
                // 062c4e86: SavedPosition3fc + Facing38c * .rdata:DAT_0639f530, which
                // is 0xc1c80000 == -25.0f, hence the subtraction.
                // x and y products stay on the FPU until the add; z's is FSTP'd first.
                Vector RetreatPoint;
                RetreatPoint.x = static_cast<float>(static_cast<double>(Facing38c.x) * -25.0 + SavedPosition3fc.x);
                RetreatPoint.y = static_cast<float>(static_cast<double>(Facing38c.y) * -25.0 + SavedPosition3fc.y);
                float const BackZ = static_cast<float>(static_cast<double>(Facing38c.z) * -25.0);
                RetreatPoint.z = static_cast<float>(static_cast<double>(BackZ) + SavedPosition3fc.z);
                // 062c4e95-062c4edd: FUN_062e53e0(&RetreatPoint, ThreatSourcePtr14, 0.0f) ==
                // IsPointOutsideHeadingCone (was the always-true RunCollisionProbe stub).
                if (!IsPointOutsideHeadingCone(RetreatPoint, ObjInspectOf(ThreatSourcePtr14), 0.0f))
                    break;          // 062c4ee7 JZ default
            }

            // LAB_062c4ef2. Both `CALL [<vtbl>+0x10]` == BaseIdea slot 4, Commit().
            SubObjectA.Commit();
            SubObjectB.Commit();
            // 062c4f01-062c4f17: ThreatSourcePtr14's IObjAffect sub-object, slot 18
            // set_target(0, SUBOBJ_ID_NONE, -1). WIRED 2026-09-24: this used to go through
            // the empty NotifyBehaviorDone stub, whose other two call sites (FUN_062d29a0)
            // are a different function.
            (void)ObjAffectOf(ThreatSourcePtr14)->set_target(nullptr, SUBOBJ_ID_NONE, -1);

            // 062c4f1a-062c4f3f: IObjAffect slot 25, go_tradelane(<-8 header of the
            // watched block>, TargetShipPtr388, false, 0.0f) -- the two `PUSH 0` are the
            // bool and the float. Retail enters state 0xd when this returns NON-zero
            // (062c4f44 JNZ). WIRED 2026-09-24: this used to call the RunCollisionProbe
            // stub (FUN_062e53e0's stand-in), which always returned true.
            void* EngineObj = HeaderBlockFromWatched(Watcher324.target());
            if (ObjAffectOf(ThreatSourcePtr14)->go_tradelane(
                    static_cast<IObjInspect const*>(EngineObj),
                    static_cast<IObjInspect const*>(TargetShipPtr388), false, 0.0f) != 0)
            {
                EnterState(0xd);    // LAB_062c4f81
                return UpdateResult10;
            }
            // FUN_062d4520 == IBehaviorManager::QueueNotification (item 1561, 133
            // instructions, ported in common_behaviormanager.cpp). WIRED FOR REAL 2026-09-17
            // (claude:w21-stubsweep) -- it used to run through an empty
            // `FormationBehavior::ReleaseDockPoint(void*, int, int) {}` stub, whose
            // guessed signature had folded the RECEIVER into the first parameter and
            // lost the real first argument entirely.
            //   .text:062c4f46  MOV  ECX,[ESI+0x14]   <- pushed as arg 1
            //   .text:062c4f49  PUSH EAX (== 0)       <- arg 3, Duration
            //   .text:062c4f4a  PUSH EAX (== 0)       <- arg 2, Extra
            //   .text:062c4f4b  PUSH ECX
            //   .text:062c4f4c  MOV  ECX,[ESI+0x20]   <- the RECEIVER, Field20
            //   .text:062c4f4f  CALL FUN_062d4520
            static_cast<IBehaviorManager*>(Field20)->QueueNotification(
                static_cast<IObjRW*>(ThreatSourcePtr14), 0, 0.0f);
            SubObjectA.SetField44(2);   // 062c4f58 FUN_062d88b0
            EnterState(10);
            AbortFlag398 = 1;       // +0x398
            Flag39a = 1;            // +0x39a
            return UpdateResult10;
        }

        // LAB_062c4f99.
        if (MoveResult != 1)
            break;                  // 062c4f9b JNZ default
        if (SubStateValue440 != 3 && SubStateValue440 != 4 && SubStateValue440 != 5)
        {
            EnterState(0xb);
            return UpdateResult10;
        }
        // LAB_062c4fce. Both `CALL [<vtbl>+0x10]` == BaseIdea slot 4, Commit().
        SubObjectA.Commit();
        SubObjectB.Commit();
        // 062c4fdd-062c4ff3: the same IObjAffect::set_target(0, SUBOBJ_ID_NONE, -1).
        (void)ObjAffectOf(ThreatSourcePtr14)->set_target(nullptr, SUBOBJ_ID_NONE, -1);
        EnterState(0xc);
        return UpdateResult10;
    }
    case 0xb:
    {
        // caseD_b at 062c500e.
        StateTimer340 += DeltaTime;
        float Threshold = 3.0f;     // 062c5011, immediate 0x40400000
        if (!SinglePlayer() && ObjInspectOf(ThreatSourcePtr14)->is_player())
        {
            // 062c503d: .rdata:DAT_0639e884 = 00 00 20 41 = 10.0f. (The listing shows only
            // its first two bytes before a mis-sliced string at 0639e886; a 2026-09-17 note
            // read that as 0.0f.) No null test on ThreatSourcePtr14.
            Threshold = 10.0f;
        }
        // 062c5049: FCOMP StateTimer340, `TEST AH,5; JP` leaves for the default case
        // unless Threshold is strictly less than StateTimer340.
        if (Threshold < StateTimer340)
        {
            Disengage();            // own vtbl slot 4 (offset 0x10)
            RecomputeApproach();    // own vtbl slot 3 (offset 0x0c)
            UpdateResult10 = 1;
            return UpdateResult10;
        }
        break;
    }
    case 0xc:
    case 0xd:
        UpdateResult10 = 2;
        break;
    default:
        break;
    }

    return UpdateResult10;
}

// ----------------------------------------------------------------------------
// FULLPORT-DEFERRED stand-ins that came here 2026-09-17 with
// UpdateApproachTail()/EnterState(). Each stands in for a real retail callee that is
// not ported yet; the call site, its order and its arguments are faithful.
// ----------------------------------------------------------------------------

// FUN_062c4530 (.text:062c4530-062c4759), ported 2026-10-08 (claude-code:cmn-ai5); it was a
// stub returning true. A jump table on ModeState320 - 2 (062c454a, 7 entries):
//   mode 2       -> the range test (caseD_0, 062c4710)
//   modes 3, 6   -> caseD_1 (062c4599)
//   mode 8       -> caseD_6 (062c4551): range test only when Flag39a or ApproachClearedFlag334
//   modes 4/5/7 and anything else (JA) -> true
// The range test (LAB_062c4569 / 062c4710): FUN_062e4500 (IsWithinInterceptRangeOfIface) on
// SystemManager+0x14fc and Watcher324's object with float(1000.0f (.data DAT_063ebdb4) * 1.05
// (double DAT_0639f528)).
bool DockBehavior::CheckCanProceed()
{
    SystemManager* const Sys = static_cast<SystemManager*>(Field24);
    switch (ModeState320)
    {
    case 3:
    case 6:
    {
        if ((StateFlags32c & 0xc0) == 0)
            break;                                                   // 062c45a0 -> range test
        // 062c45a2-062c45c2: cache refresh (FUN_062c47c0), D = centre - DestPoint3cc
        // (FUN_06288800, one float subtraction per component).
        NavHelperRefreshLeadPoint();
        Vector Delta;
        Delta.x = Sys->SelfTarget.CachedPos.x - DestPoint3cc.x;
        Delta.y = Sys->SelfTarget.CachedPos.y - DestPoint3cc.y;
        Delta.z = Sys->SelfTarget.CachedPos.z - DestPoint3cc.z;
        // 062c45c7-062c45f7: ((z*Fz + y*Fy) + x*Fx) on the FPU; FCOM 0.0f, TEST AH,0x5 + JP
        // returns false on >= 0 or unordered.
        double const Along = static_cast<double>(Delta.z) * Facing38c.z
                           + static_cast<double>(Delta.y) * Facing38c.y
                           + static_cast<double>(Delta.x) * Facing38c.x;
        if (!(Along < 0.0))
            return false;                                            // LAB_062c4705
        // 062c45fd-062c4608: FCOMP 200.0f (DAT_0639f488), TEST AH,0x41 + JNZ -> range test on
        // not-greater. Along is negative here, so that jump is always taken and the block
        // 062c460e-062c46fc (a second cache refresh, -Facing38c, FUN_062c47a0 / FUN_06288830 /
        // FUN_062c4780) is unreachable in retail; it is not reproduced.
        break;
    }
    case 8:
        if (Flag39a == 0 && ApproachClearedFlag334 == 0)
            return false;                                            // 062c4563 -> LAB_062c4748
        break;
    case 2:
        break;                                                       // caseD_0 062c4710
    default:
        return true;                                                 // caseD_2 / JA
    }

    // The range test (LAB_062c4569 and 062c4710-062c4746).
    IObjRW* const Watched = ObjRwFromWatchable(const_cast<Watchable*>(Watcher324.target()));
    float const Range = static_cast<float>(1000.0 * 1.05);           // FLD f DAT_063ebdb4, FMUL double
    return IsWithinInterceptRangeOfIface(&Sys->SelfTarget, Watched, Range);
}

// ----------------------------------------------------------------------------
// FUN_062c47f0 (item 1284), Common.dll.txt 189786-189875, .text:062c47f0 through
// the `RET` at .text:062c48f0, 76 instructions, read end to end this pass.
//
// PORTED FOR REAL 2026-09-17 (claude:orch-dockpreamble). It had been
// `static void DockBehavior::AdvancePreamble() {}`, and `static` was itself the
// defect rather than a side effect of one: retail's second instruction is
// `MOV ESI,ECX` and the body then reads SEVEN of this class's fields
// (+0x14, +0x24, +0x30, +0xa8, +0x168, +0x450, +0x46c). A receiverless
// declaration cannot express any of that, so the empty body was structurally
// locked in and no amount of grepping for stubs would have suggested otherwise.
// The one call site, UpdateApproachTail()'s first line, runs every tick.
//
// THE OLD COMMENT'S READING WAS WRONG, not merely incomplete. It said the
// function "reads MaxApproachSpeed46c and .rdata:DAT_0639f47c, i.e. it is the
// timeout countdown". Nothing here counts down. The function computes this tick's
// LINEAR and ANGULAR speed caps and hands them to the three Idea sub-objects:
//
//   LinearCap  = MaxApproachSpeed46c                      ; 062c47fc
//   AngularCap = DAT_0639f47c == 0x447a0000 == 1000.0f    ; 062c47f6
//   if (ComputeFormationFollowerEnvelope(Field24 + 0x1364, &Envelope))   ; 062c4839
//   {
//       LinearCap  = min(LinearCap,  Envelope.Speed);        ; 062c4842-062c4855
//       AngularCap = min(AngularCap, Envelope.AngularSpeed); ; 062c4859-062c486c
//   }
//   if (HeaderBlockFromWatched(ProxWatcher450) != null)                  ; 062c4870-062c4885
//       LinearCap = min(LinearCap, ProxWatcherComputeSpeedCap(...));     ; 062c4887-062c48be
//   SubObjectA.SetThrottleScale(LinearCap);    ; 062c48c7  LEA ECX,[ESI+0x30]
//   SubObjectB.SetThrottleScale(AngularCap);   ; 062c48d4  LEA ECX,[ESI+0xa8]
//   SubObjectC.SetThrottleScale(LinearCap);    ; 062c48e0  LEA ECX,[ESI+0x168]
//
// That is also why the +0x46c field is renamed MaxApproachSpeed46c this pass:
// this is its only reader in the DLL, and it uses the value as a speed.
//
// BOTH MIN-CLAMPS ARE `TEST AH,0x5` + `JNP` (062c484c/062c484f and
// 062c4863/062c4866). Per this module's x87 table the JNP fires on strictly-less,
// and the jump SKIPS the assignment, so the incumbent is kept only when it is
// already the smaller -- i.e. `x = min(x, y)`. Naming the mask without the jump
// would invert both of these, which is exactly how this cluster has produced real
// defects before.
//
// THE PROXIMITY CAP IS CALLED TWICE IN RETAIL (062c4898 and 062c48b9) with
// byte-identical receivers and arguments -- the register shuffle before each push
// differs, the pushed values do not. That is MSVC's `x = min(x, f(...))` when f
// returns in ST0: `FCOMP` POPS the value it compares, so the compiler has nothing
// left to store and recomputes it. Both calls are reproduced (2026-10-08 cmn-ai10):
// the compare uses the unrounded first result, the store the rounded second one.
// The gate on it is `TEST AH,0x41` + `JZ` at 062c48a3: ZF is set when neither C3
// (equal) nor C0 (less) survives the mask, i.e. when the returned cap is strictly
// GREATER than the incumbent -- and that is the case that jumps past the store.
void DockBehavior::AdvancePreamble()
{
    // 062c47f6: the angular cap starts at this module's standing "no limit"
    // sentinel, the same DAT_0639f47c EnterState() seeds MaxApproachSpeed46c with.
    float LinearCap = MaxApproachSpeed46c;
    float AngularCap = 1000.0f;

    // 062c4819-062c4831: retail zeroes the out-struct's first four dwords and lets
    // the callee write the trailing byte, so Flag10 is deliberately left alone.
    FormationFollowerEnvelope Envelope;
    Envelope.Speed = 0.0f;
    Envelope.Throttle = 0.0f;
    Envelope.AngularSpeed = 0.0f;
    Envelope.AngularThrottle = 0.0f;

    // 062c4806-062c4839: ECX == Field24 + 0x1364, the GunConvergenceState blob.
    // This caller reads only +0x00 and +0x08; GotoBehavior::Update reads the other
    // three.
    if (((SystemManager*)Field24)->GunConvergenceStateBytes.ComputeFormationFollowerEnvelope(&Envelope))
    {
        if (!(LinearCap < Envelope.Speed))
            LinearCap = Envelope.Speed;
        if (!(AngularCap < Envelope.AngularSpeed))
            AngularCap = Envelope.AngularSpeed;
    }

    // 062c4870-062c4885: the same "-8 header" idiom as the five sites above --
    // `MOV EAX,[ESI+0x450]; TEST EAX,EAX; JZ; ADD EAX,-8; TEST EAX,EAX; JZ`.
    if (HeaderBlockFromWatched(ProxWatcher450.Watcher.target()) != nullptr)
    {
        // 062c488d-062c4896: PUSH 0x3f800000 (1.0f), PUSH 1, PUSH [ESI+0x24],
        // PUSH [ESI+0x14]. Last push is the first argument, so OwnerContext comes
        // from +0x14 and NavHelper from +0x24 -- matching the declaration in
        // common_gotobehavior.h rather than the offset order. Retail calls it TWICE
        // (an inlined min): 062c4898 FCOMPs the unrounded result against LinearCap,
        // `TEST AH,0x41; JZ` skips only when it is strictly greater, and 062c48b9
        // calls again with the same arguments and FSTPs that result.
        if (!(ProxWatcherComputeSpeedCap(reinterpret_cast<unsigned char*>(&ProxWatcher450),
                                         ThreatSourcePtr14, Field24, 1, 1.0f) >
              static_cast<double>(LinearCap)))
        {
            LinearCap = static_cast<float>(ProxWatcherComputeSpeedCap(
                reinterpret_cast<unsigned char*>(&ProxWatcher450), ThreatSourcePtr14, Field24, 1, 1.0f));
        }
    }

    // 062c48c2-062c48e6. The linear cap goes to BOTH move-style Ideas; only the
    // FaceIdea gets the angular one.
    SubObjectA.SetThrottleScale(LinearCap);
    SubObjectB.SetThrottleScale(AngularCap);
    SubObjectC.SetThrottleScale(LinearCap);
}

// FUN_062c56d0 (.text:062c56d0-062c5a6d), ported 2026-10-08 (claude-code:cmn-ai5). Builds the
// final-approach Catmull-Rom segment at +0x348 and gives it to SubObjectC. Every component is
// one float operation stored through FUN_0628b030 (Vec3Assign) or FUN_062887d0 (Vector add),
// so plain float arithmetic matches; only the two lengths stay on the FPU through FSQRT and
// the scale, and are kept in double.
void DockBehavior::BuildFinalApproachSpline()
{
    SystemManager* const Sys = static_cast<SystemManager*>(Field24);

    // 062c56e1-062c5761: FormationOffset starts zero. When FUN_062ecab0 on Sys+0x1364 finds no
    // formation leader, it is the owner's get_formation_offset (+0xac) rotated into world space
    // by the 3DMathEngine's TransformMatrix (+0x2c) with ExternalIface's get_transform().
    Vector FormationOffset;
    FormationOffset.x = 0.0f;
    FormationOffset.y = 0.0f;
    FormationOffset.z = 0.0f;
    // 062c56f9 FUN_062ecab0: null when the +0xc5 gate byte is set, else the +0xc watcher's
    // object (raw - 8, null when raw is 0).
    bool const bHasLeader = Sys->GunConvergenceStateBytes.GetFormationLeader() != nullptr;
    if (!bHasLeader)
    {
        ObjInspectOf(ThreatSourcePtr14)->get_formation_offset(FormationOffset);   // 062c570c
        Transform const& ShipXform = Sys->SelfTarget.Iface->get_transform(); // 062c5724
        Vector Rotated;
        Acquire3DMathEngine()->TransformMatrix(reinterpret_cast<FVector&>(Rotated),
                                               reinterpret_cast<FMatrix const&>(ShipXform),
                                               reinterpret_cast<FVector const&>(FormationOffset));
        FormationOffset = Rotated;                                   // 062c574d-062c5761
    }

    // 062c5765-062c57a1: refresh the cached centre (FUN_062c47c0), P1 = centre + offset.
    NavHelperRefreshLeadPoint();
    Spline.P1.x = Sys->SelfTarget.CachedPos.x + FormationOffset.x;
    Spline.P1.y = Sys->SelfTarget.CachedPos.y + FormationOffset.y;
    Spline.P1.z = Sys->SelfTarget.CachedPos.z + FormationOffset.z;

    // 062c57a4-062c5804: P2 = (DestPoint3cc + Field444) + offset.
    Vector DockPoint;
    DockPoint.x = DestPoint3cc.x + Field444.x;
    DockPoint.y = DestPoint3cc.y + Field444.y;
    DockPoint.z = DestPoint3cc.z + Field444.z;
    Spline.P2.x = DockPoint.x + FormationOffset.x;
    Spline.P2.y = DockPoint.y + FormationOffset.y;
    Spline.P2.z = DockPoint.z + FormationOffset.z;

    // 062c5807-062c58b3: |P2 - P1| twice (the same difference is rebuilt), summed on the FPU
    // as (z*z + y*y) + x*x, FSQRT, times 0.5f (DAT_063ebdac) and 2.5f (DAT_063ebdb0).
    Vector Span;
    Span.x = Spline.P2.x - Spline.P1.x;
    Span.y = Spline.P2.y - Spline.P1.y;
    Span.z = Spline.P2.z - Spline.P1.z;
    double const SpanLength = std::sqrt(static_cast<double>(Span.z) * Span.z
                                        + static_cast<double>(Span.y) * Span.y
                                        + static_cast<double>(Span.x) * Span.x);
    float const LeadIn = static_cast<float>(SpanLength * 0.5);
    float const LeadOut = static_cast<float>(SpanLength * 2.5);

    // 062c58b7-062c5901: the ship's forward axis, rotation column 2 of ExternalIface's
    // get_transform(), through FUN_062c5a70 (negate) and back (FCHS) -- an exact identity.
    Transform const& Xform = Sys->SelfTarget.Iface->get_transform();
    Vector Forward;
    Forward.x = Xform.Rotation.E[0][2];
    Forward.y = Xform.Rotation.E[1][2];
    Forward.z = Xform.Rotation.E[2][2];

    // 062c5906-062c599b: P0 = (P1 + Forward * LeadIn) + offset.
    Vector Ahead;
    Ahead.x = Forward.x * LeadIn;
    Ahead.y = Forward.y * LeadIn;
    Ahead.z = Forward.z * LeadIn;
    Vector Start;
    Start.x = Ahead.x + Spline.P1.x;
    Start.y = Ahead.y + Spline.P1.y;
    Start.z = Ahead.z + Spline.P1.z;
    Spline.P0.x = Start.x + FormationOffset.x;
    Spline.P0.y = Start.y + FormationOffset.y;
    Spline.P0.z = Start.z + FormationOffset.z;

    // 062c59a1-062c5a51: P3 = (P2 + Facing38c * LeadOut) + offset.
    Vector Behind;
    Behind.x = LeadOut * Facing38c.x;
    Behind.y = LeadOut * Facing38c.y;
    Behind.z = LeadOut * Facing38c.z;
    Vector End;
    End.x = Behind.x + Spline.P2.x;
    End.y = Behind.y + Spline.P2.y;
    End.z = Behind.z + Spline.P2.z;
    Spline.P3.x = End.x + FormationOffset.x;
    Spline.P3.y = End.y + FormationOffset.y;
    Spline.P3.z = End.z + FormationOffset.z;

    Spline.calculate_arclength();                                    // 062c5a56
    SubObjectC.SetSpline(&Spline);                                   // 062c5a62 FUN_062da380
}

// Common.dll.txt 062c4a72-062c4a98 (identical copies at 062c4b0e and 062c3e97).
// The SystemManager's physical-radius cache: +0x157a (Flags1578[2], == +0x14fc + 0x7e) set
// once, then ExternalIface->get_physical_radius (+0x2c) into CachedPhysicalRadius (+0x1514)
// and CachedPhysicalCenter (+0x1508). Retail tests neither Field24 nor ExternalIface; the two
// "DEVIATION" null guards that stood here are gone (2026-10-08).
void DockBehavior::NavHelperRefreshLeadPoint()
{
    SystemManager* const Sys = static_cast<SystemManager*>(Field24);
    if (Sys->SelfTarget.bPosValid != 0)
        return;
    Sys->SelfTarget.bPosValid = 1;
    Sys->SelfTarget.Iface->get_physical_radius(Sys->SelfTarget.CachedRadius,
                                                          Sys->SelfTarget.CachedPos);
}

// ---- The sub-object stand-ins are GONE (2026-09-17, claude:w21-stubsweep) ----
//
// EvaluateWatcher(SubObject, Delta) was a guessed name for the `CALL [<vtbl>+0x4]`
// this body makes nine times. Offset 0x4 is BaseIdea slot 1 == Update(float), and
// all three receivers (+0x30 MoveIdea, +0xa8 FaceIdea, +0x168 SplineMoveIdea) are
// typed members of this class with real, ported overrides. The stub returned the
// literal 1, so every `== 1` progress test in UpdateApproachTail() took the
// "manoeuvre complete" arm unconditionally. Call sites now dispatch for real.
//
// ResetWatcher(SubObject) was the same mistake at `CALL [<vtbl>+0x10]`, i.e.
// BaseIdea slot 4 == Commit(). Seven call sites, all now real.
//
// SetOffsetVector/SetMoveIdeaMode/SetMoveIdeaRadius/SetMoveIdeaEnabled/
// SetMoveIdeaAngle were five guessed names covering EIGHT distinct retail
// functions, every one of them already ported in common_idea.h:
//   FUN_062d5840 -> MoveIdea::SetPosition       FUN_062d7560 -> FaceIdea::SetPosition
//   FUN_062d5980 -> MoveIdea::SetMode           FUN_062d7590 -> FaceIdea::SetDirection
//   .text:062a66b0 -> MoveIdea::SetRange3c      FUN_062d75c0 -> FaceIdea::SetBasis
//   .text:063275f0 -> MoveIdea::SetAngle38      FUN_062d5a80 -> MoveIdea::Offset4c store
// The "SetMoveIdeaEnabled" name was wrong twice over: .text:062d5a70 writes
// MoveIdea+0x59 (SetField59), not the +0x58 byte SetEnabled writes.
// The names are deleted rather than kept, so nothing preserves the guess.

// .rdata:DAT_0639f518 == 0x3fe3333333333333 == 0.6 (a double in retail, and here).
double const DockBehavior::kEnterState9RadiusScale = 0.6;
// FUN_062c2ad0 (.text:062c2ad0-062c2ade, RET 4), receiver the proximity watcher: FLD its
// far threshold (+0x0c) and, when the byte argument is set, FMUL its cruise ratio (+0x10).
// The caller FSTPs the result to a float, so one float rounding is the whole precision.
// (Was a stub returning 0.0f, 2026-10-08.)
float DockBehavior::QueryHandleRange(void* HandleField, int Mode)
{
    DockProximityWatcher const* const Watcher = static_cast<DockProximityWatcher const*>(HandleField);
    if (static_cast<unsigned char>(Mode) != 0)
        return static_cast<float>(static_cast<double>(Watcher->Unknown0c) * Watcher->Unknown10);
    return Watcher->Unknown0c;
}
// PlayApproachWarning (FUN_062d93c0) and RequestBehaviorSwitch (FUN_062da400) are
// GONE too (2026-09-17, claude:w21-stubsweep). Both were guessed names, and both
// targets are real: FUN_062d93c0 is FaceIdea::SetAngleWeight8c (item 1632) and
// FUN_062da400 is SplineMoveIdea::SetField158 (item 1654) -- common_idea.h exposes
// both fields and both setters now, so the two call sites dispatch for real.

// FUN_062e74a0 (item 1798): the checked id lookup through the IObjDB at SystemManager+0x34,
// SystemManagerTimerTable::FindVerifiedObject (common_sysmgr_timertable.cpp).

// Slot 10, FUN_062c3370 (.text:062c3370-062c3587, RET 4). The argument is a
// pub::AI::DirectiveDockOp. Always returns true.
bool DockBehavior::Slot10(void* DirectiveOp)
{
    SystemManagerTimerTable* const ObjDb = &static_cast<SystemManager*>(Field24)->Timers;
    // 062c337c-062c3387: REP MOVSD of 0xb dwords -- the whole op, vptr included.
    std::memcpy(static_cast<void*>(&DockOp), DirectiveOp, sizeof(DockOp));
    ModeState320 = 0;                                               // 062c3398
    StateFlags32c = 0;                                              // 062c339e
    // 062c3389-062c33c4: Watcher324 = the object DockOp.DockObjectId names.
    IObjRW* const Dock = static_cast<IObjRW*>(
        ObjDb->FindVerifiedObject((unsigned const*)&DockOp.DockObjectId));
    Watcher324.set(Dock != nullptr ? static_cast<Watchable const*>(Dock) : nullptr);
    IObjRW* const Watched = ObjRwFromWatchable(const_cast<Watchable*>(Watcher324.target()));
    if (Watched == nullptr)
    {
        // LAB_062c3573.
        Flags28 = static_cast<unsigned char>((Flags28 & 0xfd) | 0x1);
        return true;
    }

    // 062c33e8-062c3404: an object docking at that base replaces it (FUN_062e7400), only when
    // one is found and it differs.
    IObjRW* const Docking = ObjDb->FindObjectDockingAtBase(Watched);
    if (Docking != nullptr)
        Watcher324.set(static_cast<Watchable const*>(Docking));

    // 062c3409-062c342c: +0x32c = FUN_062e6200(the watched object).
    StateFlags32c = GetObjectTypeOrZero(
        ObjRwFromWatchable(const_cast<Watchable*>(Watcher324.target())));
    // 062c341f-062c3437: +0x388 = the object DockOp.Field_10 names (stored raw).
    TargetShipPtr388 = ObjDb->FindVerifiedObject((unsigned const*)&DockOp.Field_10);
    ApproachClearedFlag334 = DockOp.Field_14;                       // 062c3445
    Field408 = -1.0f;                                               // 062c344b
    if (DockOp.Field_14 != 0)
    {
        Field330 = DockOp.DockPortIndex;                            // 062c345f
        Field43c = DockOp.DockPortIndex;                            // 062c3465
        RefreshApproachState();                                     // 062c346b FUN_062c50e0
    }
    else
    {
        SubStateValue440 = 0;                                       // 062c3472
        Field330 = -1;                                              // 062c3478
    }

    // 062c3482-062c34e8: the proximity watcher on the object DockOp.Field_1C names, with the
    // op's +0x20/+0x24 floats; its flag words cleared.
    IObjRW* const Prox = static_cast<IObjRW*>(
        ObjDb->FindVerifiedObject((unsigned const*)&DockOp.Field_1C));
    ProxWatcher450.Unknown08 = DockOp.Field_20;
    ProxWatcher450.Unknown0c = DockOp.Field_24;
    ProxWatcher450.Watcher.set(Prox != nullptr ? static_cast<Watchable const*>(Prox) : nullptr);
    ProxWatcher450.Flags14 = 0;
    ProxWatcher450.SavedFlags18 = 0;
    IObjRW* const ProxObj = ObjRwFromWatchable(const_cast<Watchable*>(ProxWatcher450.Watcher.target()));
    if (ProxObj != nullptr)
    {
        // 062c34f4-062c3503: op+0x28 as a float against 0.0 (double DAT_0639c430); TEST
        // AH,0x41 + JNZ -> not greater takes the speed-ratio branch.
        float const Ratio = *reinterpret_cast<float const*>(&DockOp.Field_28);
        if (Ratio > 0.0)
        {
            ProxWatcher450.Unknown10 = Ratio;                       // 062c3509
        }
        else
        {
            // LAB_062c350e: own ship (SystemManager::Watcher target - 8) get_speed(1.0f, 4),
            // rounded to float, divided by the prox object's get_speed(1.0f, 1) (FDIVR).
            SystemManager* const Sys = static_cast<SystemManager*>(Field24);
            IObjRW* const Self = ObjRwFromWatchable(const_cast<Watchable*>(Sys->Watcher.target()));
            float const OwnSpeed = Behavior_InvokeManagerSlotD8(
                ObjInspectOf(Self), 1.0f, IObject::ThrustEquipCruise);      // PUSH 0x4
            double const ProxSpeed = Behavior_InvokeManagerSlotD8(
                ObjInspectOf(ProxObj), 1.0f, IObject::ThrustEquipEngine);   // PUSH 0x1
            ProxWatcher450.Unknown10 = static_cast<float>(OwnSpeed / ProxSpeed);
        }
        // LAB_062c3544: TEST AH,0x5 + JP keeps the value on >= 1.0f or unordered.
        if (ProxWatcher450.Unknown10 < 1.0f)
            ProxWatcher450.Unknown10 = 1.0f;
    }
    // LAB_062c355b.
    Flags28 = static_cast<unsigned char>((Flags28 & 0xfe) | 0x2);
    return true;
}

// Slot 21, FUN_062c2f60 (.text:062c2f60-062c2fd5, RET 4): release Watcher324 and then the
// proximity watcher when their object's id equals the argument (the object id the manager's
// loop at .text:062d405c passes).
void DockBehavior::Slot21(void* Arg)
{
    unsigned int const LeftId = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(Arg));
    IObjRW* const Watched = ObjRwFromWatchable(const_cast<Watchable*>(Watcher324.target()));
    if (Watched != nullptr && Watched->get_id() == LeftId)          // 062c2f8f slot 0x20
        Watcher324.set(nullptr);
    IObjRW* const Prox = ObjRwFromWatchable(const_cast<Watchable*>(ProxWatcher450.Watcher.target()));
    if (Prox != nullptr && Prox->get_id() == LeftId)                // 062c2fbd slot 0x20
        ProxWatcher450.Watcher.set(nullptr);
}

// DockBehavior::SetDestPointOffset -- formerly FUN_062c50c0 (the dissolved common_stubs.cpp), Common.dll.c
// 0x62c50c0: `MOV EAX,[param_1]; MOV [this+0x444],EAX` x3, RET 4 -- the three dwords of the
// argument copied into Field444. Its single call site (.text:062cbc3e, BaseFollowerBehavior's
// vtable slot 0x10 FUN_062cb5e0) passes ECX = *(ESI+0x278) = BaseFollowerBehavior::DockDelegate,
// which FUN_062cb5e0 loads from IBehaviorManager::BehaviorDispatchTable[7] (+0x190, behaviour
// type 7 "Dock", GDescDock) -- a DockBehavior; and 0x062c50c0 sits inside DockBehavior's own
// address run (0x062c2ad0-0x062c56d0). That settles the receiver the old stub comment left open.
void DockBehavior::SetDestPointOffset(Vector const& Offset)
{
    Field444 = Offset;
}
