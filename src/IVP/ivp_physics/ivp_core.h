// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_ini.h" // For Vector
#include <cstdint>
#include "../ivp_utility/ivp_math.h"
#include "../ivp_utility/ivp_u_vector.h"

// Forward declarations
class IVP_Environment;
class IVP_Real_Object;
class IVP_Controller;
struct IVP_Friction_Info_For_Core; // ivp_mindist.h
class IVP_Simulation_Unit;

// Simulation timestamp: a split-precision pair, base seconds plus a fraction in [0,1).
//
// RETYPED 2026-09-17, uint32_t -> float. The comment above always said "2 floats" while the
// members were integers, which forced roughly twenty-five
// `*reinterpret_cast<const float*>(&t.time_base)` spellings at the read sites -- and those
// casts hid a live defect at the WRITE side (ivp_environment.cpp, the PSI advance), where the
// pair was stored with `static_cast<uint32_t>(value)`. That truncates the number and then
// stores the integer, while every reader interprets the same dword as float BITS: a base of
// 123.75 became the integer 123, which reads back as ~1.7e-43.
//
// The type is settled by the instructions, not by the old comment -- IVP code loads both
// halves of IVP_Environment::current_time (+0xF8/+0xFC) with `FLD float ptr`:
//     .text:06346205 / .text:0634620d      (IVP_Core)
//     .text:06351acf / .text:06351ad8
//     .text:0635a518 / .text:0635a537
// and writes them with FST/FSTP. Size is unchanged, so every offsetof assert still holds and
// the surviving reinterpret_casts remain correct, merely redundant.
// A class, not a struct: retail's export ?GetCurrentTime@PhySys@@YA?AVIVP_Time@@I@Z
// mangles the return type as `class IVP_Time` (V).
class IVP_Time
{
public:
    float time_base;
    float time_offset;
};
static_assert(sizeof(IVP_Time) == 0x08, "IVP_Time size");

// ============================================================================
// IVP_Core_AxisLock -- PORT-LOCAL NAME. Retail's own class identity for this
// object is NOT established; the name describes the only behavior two
// independent retail call sites agree on (see below), and every field is
// typed strictly from the instruction that touches it.
//
// D-claude:common-ivp2 pass (2026-09-17). This is the object IVP_Core holds
// at +0x10. That slot used to be declared `IVP_Real_Object* PhysicalObject`
// in this header; that claim is DISPROVEN:
//
//   1. IVP_Core's own constructor never writes +0x10. FUN_06347550
//      (Common.dll.txt:411011-411033, the zero-init half called first by
//      FUN_06347660) `REP STOSD`s 0x6f dwords, then writes exactly two
//      pointer-shaped fields: the owning IVP_Real_Object goes into
//      Objects' inline slot at +0x58 (`MOV [ECX+EDX*4],EAX` with
//      ECX = Objects.elems = this+0x58), and `owner->+0x18` goes into
//      Environment at +0x0C. +0x10 stays 0.
//   2. FUN_06354120 (item 3208, ported this pass as
//      IVP_ApplyContactReactionPair) uses `Core->+0x10` as the BASE of a
//      3x4 float matrix -- `FLD [ECX + EAX*4 + 0x8]`, `+0x18`, `+0x28` at
//      Common.dll.txt:06354200/0635420f/0635421a -- and writes a world
//      position into `[ECX+0x48]` (:06354508-0635450d). On an
//      IVP_Real_Object those offsets are next_object/prev_object/parent/
//      object_name/environment/first_child and hull_manager respectively.
//   3. The same function reads `*(Core->+0x10 + 4)` as a POINTER and
//      dereferences ITS +0x74 (:063541fa-063541fd). IVP_Real_Object+0x04
//      is IVP_Object::object_type, a 0..3 tag -- dereferencing it would be
//      a wild read.
//   4. FUN_06351700 (IVP_Core_IntegrateRotationStep, ivp_core.cpp) reaches
//      the identical chain from a confirmed IVP_Core: Common.dll.txt:
//      063518dd `MOV ECX,[EDI+0x10]` then :063518fc-063518ff
//      `MOV EAX,[ECX+4]; MOV EAX,[EAX+0x74]`, and uses the result to index
//      that core's OWN RotSpeed (`FLD [EDI + EAX*4 + 0x94]`). So +0x74 of
//      the +0x04 record is a 0/1/2 AXIS INDEX, and a core that owns one of
//      these objects rotates about a single locked axis -- hence the name.
//
// Only the fields the two traced call sites actually touch are named; the
// object certainly continues past +0x68, so no sizeof() is asserted.
struct IVP_Core_AxisLock
{
    void*        Field_0x00;      // +0x00: never read by either traced site.
    // +0x04: the record that carries the locked-axis index at ITS +0x74 and,
    // at ITS +0x8c, a pointer to a pointer to an IVP_Real_Object
    // (Common.dll.txt:0635422d-06354235 `MOV ECX,[EDX+0x8c]; MOV EDX,[ECX];
    // MOV ECX,[EDX+0x98]` -- +0x98 being IVP_Real_Object::core settles that
    // the doubly-dereferenced thing is a real object). Its own class is NOT
    // identified, so it stays void* and both sub-fields are reached through
    // the documented raw offsets below, matching this port's existing
    // convention for unidentified neighbours.
    void*        Owner;           // +0x04
    static const int OwnerAxisIndexOffset = 0x74; // int, 0/1/2
    static const int OwnerObjectSlotOffset = 0x8c; // IVP_Real_Object**

    IVP_U_Matrix LockFrame;       // +0x08 (0x40 bytes); column [axis] is what both sites read
    IVP_U_Point  ContactPoint;    // +0x48: world-space contact position, written by FUN_06354120
    float        ContactImpulse;  // +0x58: written by FUN_06354120 (0 on the in-cone path)
    IVP_Time     LastUpdateTime;  // +0x5c: copied verbatim from Environment::current_time (+0xF8)
    void*        Field_0x64;      // +0x64: pointer/flag; non-null selects FUN_06354120's scaled branch
};
static_assert(offsetof(IVP_Core_AxisLock, Owner) == 0x04, "IVP_Core_AxisLock+0x04");
static_assert(offsetof(IVP_Core_AxisLock, LockFrame) == 0x08, "IVP_Core_AxisLock+0x08");
static_assert(offsetof(IVP_Core_AxisLock, ContactPoint) == 0x48, "IVP_Core_AxisLock+0x48");
static_assert(offsetof(IVP_Core_AxisLock, ContactImpulse) == 0x58, "IVP_Core_AxisLock+0x58");
static_assert(offsetof(IVP_Core_AxisLock, LastUpdateTime) == 0x5c, "IVP_Core_AxisLock+0x5c");
static_assert(offsetof(IVP_Core_AxisLock, Field_0x64) == 0x64, "IVP_Core_AxisLock+0x64");

// Complete full retail IVP_Core structure (0x1BC = 444 bytes)
class IVP_Core
{
public:
    // +0x00
    unsigned int Flags;                       // +0x00: Bitfield status & movement flags
    // +0x04. RENAMED 2026-09-17 from `MaxAngularSpeed`, which was wrong. This is IVP's
    // upper_limit_radius -- the radius of the sphere bounding this core -- and it is
    // distinct from BoundingRadius at +0x14C. Three independent lines of evidence, two of
    // them reported by separate lanes that had not seen each other's work:
    //   * It is ADDED to a collision range to form the OV-tree insertion bound and then
    //     SUBTRACTED back out of the insertion result to form the hull key --
    //     .text:0634ec97 `FADD float ptr [ESI + 0x4]` and .text:0634eceb
    //     `FSUB float ptr [ESI + 0x4]`, where ESI is `MOV ESI,[EBX + 0x98]`
    //     (.text:0634ec3d) and IVP_Real_Object+0x98 is `IVP_Core* core`. Receiver proven,
    //     not assumed from the offset.
    //   * common_physys.cpp had already recorded, independently, that retail uses this
    //     field as a radius (see its case-3 comment).
    //   * The apparent counter-evidence dissolves: `DAngularLimitFactor = 0.5f / this`
    //     (ivp_core.cpp) is exactly how a RADIUS converts a linear half-displacement into
    //     an angle, so that expression argues FOR a radius rather than against it.
    // SetSpeedLimits(), which writes this and SpeedLimit together, is correspondingly a
    // set-limits(radius, speed) pair, not two speeds.
    // OPEN TODO: nothing in this port writes it except SetSpeedLimits -- it still wants a
    // real initializer from whatever retail computes the bounding sphere in.
    float UpperLimitRadius;                   // +0x04
    float SpeedLimit;                        // +0x08: Speed limit
    IVP_Environment* Environment;             // +0x0C: Simulation environment

    // +0x10: see IVP_Core_AxisLock above for why this is NOT the
    // `IVP_Real_Object* PhysicalObject` back-pointer it was declared as
    // until 2026-09-17. The owning object is reached through Objects[0]
    // (+0x58), which is what IVP_Core's own constructor actually fills.
    IVP_Core_AxisLock* AxisLock;              // +0x10
    Vector RotInertia;                       // +0x14: Moment of inertia vector (Ix, Iy, Iz)
    float Mass;                              // +0x20: Mass

    // +0x24
    Vector RotSpeedDamp;                     // +0x24: Angular velocity damping (3 floats)
    float CenterOfMassX;                     // +0x30: Center of mass offset / padding
    Vector InvRotInertia;                    // +0x34: Inverse moment of inertia (1/Ix, 1/Iy, 1/Iz)
    float InvMass;                           // +0x40: Inverse mass (1/Mass)
    float SpeedDamp;                         // +0x44: Linear velocity damping

    // +0x48
    float DAngularLimitFactor;               // +0x48: Angular limit factor (0.5f / UpperLimitRadius)
    // +0x4C: real owned pointer, not padding -- confirmed by IVP_Core::~IVP_Core
    // (FUN_063477e0, Common.dll.txt:411308-411311): freed unconditionally via
    // free()/FUN_06391d5a and zeroed as one of the destructor's real cleanup
    // steps, right after the IVP_Simulation_Unit detach (see
    // IVP_Core_Remove_Object in ivp_core.cpp). Identity/purpose of what it
    // points to was not determined this pass (no constructor site was in
    // scope) -- open item for a follow-up lane.
    // IDENTIFIED 2026-10-08 (cmn-ivp3): IVP's spin_clipping, a per-axis bound on |RotSpeed|.
    // IVP_Calc_Next_PSI_Solver::calc_next_PSI_matrix (FUN_06351310 :06351336-:063513c5)
    // clamps RotSpeed.x/y/z into [-[+0x4c]->x, [+0x4c]->x] etc. when it is non-null.
    Vector* SpinClipping;

    // +0x50: IVP_U_Vector_Fixed<IVP_Real_Object,1> -- confirmed Fixed<1>, not
    // Fixed<2> as an earlier lane guessed, this pass (D76, 2026-09-09): the
    // real destructor's own inline-buffer-liveness check (FUN_063477e0,
    // Common.dll.txt:0x063478ae `LEA EDX,[ESI+0x58]; CMP EAX,EDX` against
    // Objects.elems) tests `elems == this+1` where `this` is Objects' own
    // 8-byte IVP_U_Vector header at +0x50 -- i.e. the inline buffer starts at
    // +0x58 and is exactly ONE T* slot wide (IVP_U_Vector_Fixed<T,1>), ending
    // at +0x5C. +0x5C is therefore NOT part of that buffer: it is a separate,
    // independently alloc'd field (ObjectHashMap below) with its own
    // dedicated create/insert/lookup/remove/destroy call family gated purely
    // on Core->Flags, never on Objects' population -- see ObjectHashMap's own
    // comment for the full evidence chain. No real union/overlap here, just
    // an earlier lane's Fixed<2> guess being one slot too wide. Same
    // structural pattern as Controllers/OwningSimUnit below (Fixed<1> + one
    // dedicated trailing field, together filling a 16-byte block).
    IVP_U_Vector_Fixed<IVP_Real_Object, 1> Objects; // +0x50 (12 bytes: 8-byte header + 1 inline slot)
    // +0x5C: Robin-Hood hashmap pointer (HashMap063525f0 struct / FUN_063525f0
    // ctor, FUN_06352620 dtor, FUN_063526d0 insert, FUN_063527e0 remove,
    // FUN_06352930 lookup -- all IVP_VHash_Store members in
    // src/IVP/ivp_utility/ivp_vhash.cpp, decomp-tracker item 3178), lazily
    // allocated (operator_new(0x14) + FUN_063525f0(2)) ONLY for cores where
    // (Flags & 0xC) == 4. Confirmed by FUN_06347c40 (lookup gate,
    // Common.dll.txt:411632-411653), FUN_06347c80 (lazy-create-then-insert
    // gate, :411671-411731), FUN_06347d20 (remove gate; the non-4 path there
    // just zeroes this field unconditionally, :411740-411754), and freed in
    // the real destructor's Step 2 (FUN_063477e0, :411273-411287, now wired
    // in ivp_core.cpp). Every one of these gates on the exact same
    // `(Flags & 0xC) == 4` test as the destructor, so a core that ever
    // populates this field provably never takes the destructor's `else`
    // branch (the Controllers vtable-notify loop) and vice versa -- the two
    // uses are mutually exclusive by construction, not a real union. What the
    // hashmap's (key,value) pairs represent was not traced further this pass
    // (plausibly a per-attached-object lookup for compound/merged cores,
    // given IVP_Core's own object vector sits immediately above) -- open TODO
    // for a follow-up lane, along with porting FUN_06347c40/c80/d20 themselves
    // as real IVP_Core methods.
    //
    // CLOSED (D-target-ObjectHashMap pass, 2026-09-10): FUN_06347c40/c80/d20
    // are now ported below as find_object_hash_entry()/
    // register_object_hash_entry()/unregister_object_hash_entry(). The
    // (key,value) semantics are now confirmed generic (key = a caller-
    // supplied candidate's own +0x8 field, value = the candidate pointer
    // itself) but the candidate's own owning class is still not identified,
    // so it stays an untyped void* entry -- see those methods' own comments
    // near the end of this class for the full trace.
    void* ObjectHashMap;                     // +0x5C
    uint32_t MovementState;                  // +0x60: Movement type / state enum
    // +0x64: D93 pass (2026-09-09) found this is NOT padding -- retail
    // FUN_06358070 (IVP_Impact_Solver::FinalizeImpact, decomp-tracker item
    // 3270) and FUN_063572b0 (CommitCoreVelocityImmediate, item 3266) both
    // access it as a 16-bit signed counter (ADD/DEC/MOVSX word ptr
    // [Core+0x64], Common.dll.txt:06358085-0635808c and :0635730a), bumped
    // by FinalizeImpact per-core at the start of every call (by
    // 1-TypeCode(Flags), TypeCode = sign-extended Flags bits[3:2]) and
    // decremented once more on the real deferred-commit path -- a pending
    // in-flight-impact refcount. Renamed from "Pad64"; the upper 16 bits of
    // the old 4-byte pad are still genuinely unconfirmed, kept as Pad66.
    int16_t PendingImpactCount;              // +0x64: pending-impact refcount, see FinalizeImpact/item 3270
    int16_t Pad66;                           // +0x66: unconfirmed, was folded into the old 4-byte Pad64
    IVP_Time TimeOfLastPsi;                  // +0x68: Timestamp of last simulation step (8 bytes)
    float IDeltaTime;                        // +0x70: Inverse delta time (1.0f / dt)

    // +0x74
    Vector RotSpeedChange;                   // +0x74: Angular acceleration / impulse delta accumulator
    float Pad80;                             // +0x80: Padding
    Vector SpeedChange;                      // +0x84: Linear acceleration / impulse delta accumulator
    float Pad90;                             // +0x90: Padding

    // +0x94
    // RESOLVED (follow-up pass, 2026-09-09; was flagged as an OPEN
    // CONTRADICTION by D110/item 3282 and independently re-confirmed by item
    // 3276/FUN_06358680). The "`Cores[N]->+0x98` dereferenced as a pointer"
    // reports (FUN_06358d60/item 3282, FUN_06358680/item 3276, and
    // FUN_06356bc0 in ivp_mindist_limiter.cpp/item 3276) were all tracing the
    // SAME retail idiom: `this` there is `IVP_Impact_Solver_Long_Term`, and what those
    // functions actually dereference at `+0x98` is NOT an `IVP_Core` field at
    // all -- it is `IVP_Impact_Solver_Long_Term::Objects[N]->core`, i.e. IVP_Real_Object's
    // own `core` member (ivp_real_object.h:47, `IVP_Core* core; // +0x98`),
    // independently confirmed by FUN_06343700 (Common.dll.txt:404281,
    // 404323-404325, IVP_Merge_Object_Into_Core's core reassignment). The
    // `IVP_Impact_Solver_Long_Term` field previously (mis)declared `IVP_Core* Cores[2]` at
    // +0x30/+0x34 has been retyped to `IVP_Real_Object* Objects[2]` in
    // ivp_mindist.h -- see the class-level comment above `IVP_Impact_Solver_Long_Term` there
    // for the full evidence trail (also covers the ESI+0x14/+0x18/+0x1c/+0x20/
    // +0x94/+0x98/+0x9c/+0xa4/+0xa8/+0xac field-read pattern below: those ARE
    // a genuine IVP_Core's RotInertia/Mass/RotSpeed/Speed, read off the
    // CORRECT pointer, `Objects[N]->core`, not off `Objects[N]` itself).
    // RotSpeed.y at +0x98 was correct all along; NO retyping needed here.
    // (The item 2857/FUN_06347240 data point the prior version of this
    // comment cited -- 3 sequential FLD/FSTP floats at +0x94/+0x98/+0x9c,
    // never a pointer dereference, Common.dll.txt:06347265-06347372 -- was
    // consistent with this resolution all along and needed no reconciling.)
    Vector RotSpeed;                         // +0x94: Angular velocity in object space (OS)
    float PadA0;                             // +0xA0: Padding
    Vector Speed;                            // +0xA4: Linear velocity in world space
    float PadB0;                             // +0xB0: Padding

    // +0xB4
    Vector PosWorldFCore;                    // +0xB4: Position in world coordinates
    float PadC0;                             // +0xC0: Padding
    Vector DeltaWorldFCore;                  // +0xC4: Position delta step in world space
    float PadD0;                             // +0xD0: Padding

    // +0xD4
    IVP_U_Quat QWorldFCore;                  // +0xD4: Current orientation quaternion
    IVP_U_Quat QWorldFCoreNext;              // +0xE4: Next orientation quaternion

    // +0xF4
    IVP_U_Matrix MWorldFCore;                // +0xF4: 4x4 Transformation matrix to world space (64 bytes)

    // +0x134
    Vector RotAxisOS;                        // +0x134: Rotation axis in object space (1.0, 0.0, 0.0)
    float Pad140;                            // +0x140: Padding
    float Margin;                            // +0x144: Collision margin
    float MaxSurfaceDeviation;               // +0x148: Surface deviation limit
    float BoundingRadius;                    // +0x14C: Bounding radius

    // +0x150: same Fixed<1>-plus-dedicated-field pattern as Objects above,
    // now SETTLED (D76, 2026-09-09) rather than left as the "genuine union
    // question" this comment used to pose. The destructor's own inline-buffer
    // check for THIS vector (Common.dll.txt:0x0634787f `LEA ECX,[ESI+0x158];
    // CMP EAX,ECX` against Controllers.elems) tests `elems == this+1` for
    // Controllers' 8-byte header at +0x150 -- inline buffer starts at +0x158
    // and is exactly ONE slot wide (IVP_U_Vector_Fixed<T,1>), ending at
    // +0x15C. OwningSimUnit below is that dedicated trailing field (also
    // independently confirmed by IVP_Core::~IVP_Core, FUN_063477e0,
    // Common.dll.txt:411302, reading it directly with no dependency on
    // Controllers' population), not a 2nd inline Controllers slot -- no real
    // union/overlap, an earlier lane's Fixed<2> guess was one slot too wide,
    // same as Objects.
    IVP_U_Vector_Fixed<IVP_Controller, 1> Controllers; // +0x150 (12 bytes)
    // +0x15C: this Core's owning IVP_Simulation_Unit*. Read directly by the
    // real destructor's Step 3 (FUN_063477e0, Common.dll.txt:411302:
    // `MOV ECX,[ESI+0x15c]` right before `CALL FUN_06348cb0` =
    // IVP_Simulation_Unit::detach_core) and by ensure_in_simulation()/
    // revive_sleeping_core() below. Previously read only informally via a
    // generic `PadControllers[1]` pad member (D72); given a real, properly
    // typed name now that Controllers' own Fixed<1> size is settled. This is
    // NOT the same field as the +0x1B8 field further down, which D72 already
    // flagged as an unconfirmed, likely-stale SimUnit guess from an earlier
    // lane -- kept and renamed there (not deleted) rather than assumed to be
    // this same pointer duplicated.
    IVP_Simulation_Unit* OwningSimUnit;      // +0x15C

    // +0x160
    IVP_Time TimeOfLastMotion;               // +0x160: Last motion timestamp (8 bytes)
    IVP_Time TimeOfLastLongMotion;           // +0x168: Last long motion timestamp (8 bytes)
    IVP_U_Quat QLastMotion;                  // +0x170: Last motion orientation snapshot (16 bytes)
    IVP_U_Quat QLastLongMotion;              // +0x180: Last long motion orientation snapshot (16 bytes)
    Vector PosLastMotion;                    // +0x190: Last motion position snapshot (12 bytes)
    float Pad19C;                            // +0x19C: Padding
    Vector PosLastLongMotion;                // +0x1A0: Last long motion position snapshot (12 bytes)
    float Pad1AC;                            // +0x1AC: Padding
    // +0x1B0: Union-find "next" pointer for merged cores. When two cores are welded
    // together (e.g. by a fixed constraint), one core's chain of these pointers is
    // relinked to point at the other, so walking this field to its null terminator
    // (see FUN_063557c0 / get_master_core()) finds the current representative core
    // for the merged group. Null means this core is its own master. Was previously
    // (incorrectly) modeled as unused padding.
    IVP_Core* MergedCoreWhichReplacedIt;

    // +0x1B4
    void* FastSimUnitData;                   // +0x1B4: Temp simulation buffer pointer
    // +0x1B8: NOT independently confirmed as an IVP_Core field at all -- flagged
    // this pass (D72, 2026-09-09), renamed this pass (D76, 2026-09-09) to stop
    // it colliding with the now-settled REAL SimUnit pointer's name. The real,
    // destructor-confirmed owning-IVP_Simulation_Unit pointer
    // (FUN_063477e0, Common.dll.txt:411302: `MOV ECX,dword ptr[ESI+0x15c]` right
    // before `CALL FUN_06348cb0`, i.e. IVP_Simulation_Unit::detach_core) lives at
    // +0x15C and is now properly named/typed above as `OwningSimUnit` (D76
    // settled the Controllers-vector Fixed<1> layout question this field's old
    // comment left open). This +0x1B8 field's every disassembly xref is in
    // unrelated 0x0627xxxx/0x062cxxxx (PhySys/other) address ranges -- never an
    // IVP-internals (0x0634xxxx) call site -- so it is very likely a stale guess
    // from an earlier lane, not a real second SimUnit pointer, and definitely not
    // OwningSimUnit's +0x15C duplicated. Left in place (unused anywhere else in
    // the port) rather than deleted outright; a follow-up lane should either find
    // real IVP_Core-specific evidence for its actual identity or remove/repurpose
    // it.
    //
    // UPDATE (D-cluster-06346800 pass, 2026-09-10): the "every xref is outside
    // 0x0634xxxx" claim above is now STALE -- FUN_06347be0 (item 2868,
    // notify_objects_and_cache_env_field() below) writes it directly at
    // Common.dll.txt:06347bef (`MOV dword ptr[EDI+0x1b8],ECX` where
    // ECX=Environment->field_114), a genuine IVP-internals (0x0634xxxx) site.
    // This alone still doesn't pin the field's concrete type/identity (only
    // ever read back via this same raw 32-bit slot elsewhere in the traced
    // callers), so it stays untyped/unrenamed pending a follow-up lane -- but
    // it IS confirmed live IVP_Core state, not purely a stale earlier-lane guess.
    void* Unconfirmed_0x1B8;                 // +0x1B8: NOT IVP_Core's SimUnit -- see OwningSimUnit at +0x15C instead

    // Default constructor: matches this class's original implicit default
    // constructor (only the two Fixed<1> vector headers, Objects/Controllers,
    // get initialized via their own generic constructors; every other field
    // is left uninitialized, exactly as before this pass added the real,
    // parameterized retail constructor below). NOT a retail function --
    // preserved solely so this port's separate PhySys reimplementation
    // (common_physys.cpp's BuildIVP_Real/BuildIVP_Sphere, `new IVP_Core()`)
    // keeps compiling and behaving identically; that code path populates
    // every field itself rather than going through retail's own construction
    // chain (see IVP_Real_Object::EnsureInSimulation's own comment on why
    // PhySys is a deliberately separate, from-scratch system).
    IVP_Core() = default;

    // FUN_06347550 + FUN_06347660 (Common.dll.txt:410996-411070 /
    // 411103-411216, decomp-tracker items 2862/2864). D-ivpenv-staticobject
    // pass (2026-09-10): IVP_Core's real constructor, traced instruction-by-
    // instruction across both functions (FUN_06347660 is a thin wrapper that
    // immediately delegates most of the work to FUN_06347550, then finishes
    // the object's Flags/pose/OwningSimUnit-manager-registration setup
    // itself). Confirmed the owning class for the previously-`blocked` items
    // 2865 (FUN_063477c0) and 2837 (FUN_06346870) along the way -- see those
    // items' own updated notes.
    //
    // Retail sequence (Owner is the IVP_Real_Object this Core is being built
    // for -- FUN_06347660's own param_1, forwarded unchanged into
    // FUN_06347550):
    //   1. `REP STOSD` zeroes all 0x6F dwords (== sizeof(IVP_Core), confirmed
    //      by static_assert below) of `this` -- modeled here as memset, since
    //      this constructor assumes raw (not necessarily zeroed) storage, the
    //      same way retail's own caller hands it a bare operator_new block.
    //   2. Objects (+0x50): re-run its Fixed<1> inline-buffer init (the
    //      memset above wipes out whatever a member default-constructor would
    //      have set), then `Objects.Add(Owner)` -- this Core's sole initial
    //      attached object -- and `Environment = Owner->environment`
    //      (IVP_Object::environment, +0x18, confirmed: `MOV EAX,[EAX+0x18]`
    //      immediately follows the Add in FUN_06347550's own bytes).
    //   3. OwningSimUnit (+0x15C): `operator_new(sizeof(IVP_Simulation_Unit))`
    //      (0x24 bytes, confirmed -- see ivp_simulation_unit.h's corrected
    //      size), construct it (FUN_06348e10, ported as
    //      IVP_Simulation_Unit's own constructor), add this Core to its
    //      Cores vector (FUN_06348ec0, byte-identical to the already-generic
    //      IVP_U_Vector<T>::Add -- called directly rather than duplicated),
    //      then force both the new Unit's State and this Core's own
    //      MovementState low byte to 8 ("static/inactive").
    //   4. FUN_06349180 (Common.dll.txt:414427-414455, decomp-tracker item
    //      2932): called with `ECX=this(Core)`, stack arg = the raw dword at
    //      `*(Environment+0x0)` -- i.e. whatever value sits at the START of
    //      IVP_Environment::collision_filter (+0x00, still an unidentified
    //      class per decomp-tracker item 3031's own open gap; that field's
    //      "vtable tag" is flagged there as a widely ICF-folded generic
    //      constant, not confirmed to be a real per-instance object). Traced
    //      far enough to see it does `Core->Controllers.Add(param_1)` then
    //      tail-calls FUN_063491d0 (unexamined) to register the same pointer
    //      into OwningSimUnit's per-controller bucket map. NOT reproduced
    //      here: `Core->Controllers` already gates a real virtual-dispatch
    //      loop in the destructor (see ivp_core.cpp's `IVP_Core_Remove_Object`,
    //      `Ctrl->core_is_going_to_be_deleted_event(Core)`), so inserting an
    //      unconfirmed raw pointer there risks a genuinely invalid virtual
    //      call rather than just a wrong-but-harmless field -- left as a
    //      documented no-op (same convention as
    //      IVP_DEFERRED_RefreshNarrowPhaseCacheSlot in ivp_real_object.cpp)
    //      pending a follow-up pass that identifies collision_filter's real
    //      class and FUN_063491d0's own behavior.
    //   5. Back in FUN_06347660: Flags = (Flags & ~0xC) | ((FlagsHi & 3) << 2)
    //      (FlagsHi = its own param_4), then
    //      Push_Onto_Manager(Environment->time_manager, OwningSimUnit)
    //      (FUN_06349400, already ported/exposed as
    //      IVP_Simulation_Unit_Push_Onto_Manager).
    //   6. QWorldFCore (+0xD4) = *InitialOrientation (param_2, a raw 16-byte
    //      copy), then IVP_U_Quat::normize() (FUN_06350fb0, confirmed
    //      byte-identical to the already-generic IVP_U_Quat::normize() --
    //      called directly), then QWorldFCoreNext (+0xE4) = the now-
    //      normalized QWorldFCore (a second raw 16-byte copy).
    //   7. PosWorldFCore (+0xB4) = *InitialPosition (param_3), DeltaWorldFCore
    //      (+0xC4) = {0,0,0}, then QWorldFCore.set_matrix(&MWorldFCore)
    //      (FUN_06350bc0, already ported/generic -- writes only the 3x3
    //      rotation block, rows at +0xF4/+0x104/+0x114) followed by
    //      MWorldFCore.vv (+0x124, the matrix's own translation slot) =
    //      *InitialPosition again (a second raw copy of the same position).
    //   8. SetSpeedLimits(1000.0f, 1000.0f) (FUN_06346870, see below --
    //      confirmed retail default).
    //   9. Flags = (Flags & ~0x3) | (FlagsLo & 0x3) (FlagsLo = its own
    //      param_5) -- combined with step 5, the freshly constructed Core's
    //      Flags low nibble is entirely `(FlagsHi&3)<<2 | (FlagsLo&3)`, every
    //      other Flags bit staying 0 from the step-1 zero-fill.
    // Returns `this` in retail (constructor-style `return this` pattern,
    // Common.dll.txt:063477a7-063477a8); not modeled here since a real C++
    // constructor has no return value.
    IVP_Core(IVP_Real_Object* Owner, const IVP_U_Quat* InitialOrientation,
             const IVP_U_Point* InitialPosition, uint32_t FlagsHi, uint32_t FlagsLo);

    // FUN_06346870 (Common.dll.txt:409632-409642, decomp-tracker item 2837,
    // 5 insns). D-ivpenv-staticobject pass (2026-09-10): RE-ATTRIBUTED from
    // "simplified / unidentified owner class" -- this pass's IVP_Core
    // constructor trace confirmed `this` is IVP_Core at both of this
    // function's real call sites reachable from this cluster (one inside the
    // constructor above; the other inside FUN_06345b20/item 2817, still
    // out of scope for this pass, but that function's own `this` was already
    // independently identified there as `*(int*)(Owner+0x98)` == `Owner->core`,
    // i.e. the SAME IVP_Core this constructor builds). Trivial 2-field
    // setter: `*(this+4)=NewUpperLimitRadius; *(this+8)=NewSpeedLimit;` --
    // UpperLimitRadius/SpeedLimit are already-named fields above. Called from
    // this constructor with the confirmed retail default {1000.0f, 1000.0f}
    // (Common.dll.txt:06347780-06347785, raw bits 0x447a0000 == 1000.0f).
    void SetSpeedLimits(float NewUpperLimitRadius, float NewSpeedLimit);

    // Core methods
    void ensure_in_simulation();
    void revive_sleeping_core();

    // FUN_06349180 (Common.dll.txt:414429-414455, decomp-tracker item 2932,
    // 22 insns). NOT one of this pass's own 19 assigned items (D-cluster-
    // 06348f60, 2026-09-10), but a direct dependency of this cluster's own
    // FUN_06349040/item 2930 (calls this once per core in a loop) -- ported
    // here too since its own sole remaining blocker (item 2933) was just
    // closed by this same pass (see
    // IVP_Simulation_Unit_Register_Core_With_Controller, ivp_simulation_unit.h).
    // Appends `Controller` onto this->Controllers (generic Realloc-if-full +
    // append + increment, same as IVP_U_Vector<T>::Add() elsewhere in this
    // port -- reused directly rather than hand-duplicated), then tail-calls
    // IVP_Simulation_Unit_Register_Core_With_Controller(OwningSimUnit,
    // Controller, this) -- the exact structural counterpart of
    // detach_controller() below removing it.
    void register_controller(IVP_Controller* Controller);

    // FUN_06347040 (.text:06347040-063470d9): IVP_Core::freeze_simulation_core -- stop
    // movement (FUN_06346fb0), then per object (backward): movement byte = 8, mindist
    // manager RecheckOvElement, bring the hull values to "now" with both gradients zeroed
    // (the hull's edit time is NOT updated), DrainAndShift, release the narrow-phase cache
    // slot when +0x40 is set (FUN_0634d510).
    void freeze_simulation_core();

    // FUN_0634b590 (.text:0634b590-0634b5dd): per object (backward) build {environment,
    // object} and fire it through the cluster manager's per-object listeners
    // (FUN_0634af70, slot 3) and the environment's object listeners (FUN_0634cf00, slot 3).
    void fire_event_object_frozen();

    // FUN_0634b5e0 (.text:0634b5e0-0634b5f0): freeze_simulation_core then
    // fire_event_object_frozen. Called per core when a simulation unit goes to sleep
    // (FUN_063495e0 :0634965b).
    void freeze_and_fire_object_frozen();

    // ---- cmn-phy6 block (2026-10-08) ----
    // FUN_06346290 (.text:06346290-06346331, RET 8), the SDK's
    // get_surface_speed: SpeedWsOut = Speed + (MWorldFCore * RotSpeed) x
    // (PositionWs - MWorldFCore.vv). Callers 06343940 (core split) and
    // PhySys 0628d0dc. Only x/y/z of SpeedWsOut are written.
    void get_surface_speed(const IVP_U_Point* PositionWs, IVP_U_Point* SpeedWsOut) const;
    // ---- end cmn-phy6 block ----

    // FUN_06349110 (Common.dll.txt:414380-414424, decomp-tracker item 2931,
    // 37 insns). D-cluster-06348f60 pass (2026-09-10). Removes `Controller`
    // from this->Controllers (backward linear search + shift, same idiom as
    // IVP_U_Vector<T>::RemoveElement -- this port reuses
    // Controllers.RemoveElement() directly rather than hand-duplicating),
    // then unconditionally calls
    // IVP_Simulation_Unit_Detach_Core_From_Controller_Bucket(OwningSimUnit,
    // Controller, this) (ivp_simulation_unit.h, item 2934) -- retail has NO
    // null-guard on OwningSimUnit here (Common.dll.txt:0634916f-06349175),
    // reproduced as-is; every real caller traced this pass (item 2926's
    // FUN_06348f60 loop, and this cluster's own FUN_06349040/item 2930) only
    // ever calls this on cores that already have a live OwningSimUnit.
    // Defined in ivp_core.cpp (not inline here) since it needs
    // ivp_simulation_unit.h, which itself includes this header -- avoids the
    // circular include.
    void detach_controller(IVP_Controller* Controller);

    // FUN_063470e0 (Common.dll.txt:410541-410589, decomp-tracker item 2855).
    // D-target-2989 pass (2026-09-10): re-traced term-by-term while porting
    // item 2856 (FUN_06347170, this method's only *new* caller). Combines
    // Environment->current_time's two raw dwords (+0xF8/+0xFC), REINTERPRETED
    // as floats and ADDED together (`Sum = (float&)time_offset +
    // (float&)time_base`), splits that sum into an integer part (via `ftol`)
    // and a fractional remainder, and writes BOTH parts (as raw float bit
    // patterns, not the original dwords) into TimeOfLastMotion (+0x160/+0x164)
    // AND TimeOfLastLongMotion (+0x168/+0x16c). Retail recomputes the exact
    // same ftol/split TWICE, once per target field pair (Common.dll.txt:
    // 06347109-0634712c for TimeOfLastMotion, :0634713a-06347159 for
    // TimeOfLastLongMotion) -- a provably pure, side-effect-free recomputation
    // of the identical value, so this port computes it once and writes both
    // (same simplification precedent as recalc_interpolated_frame's own header
    // comment above). CORRECTS an earlier pass's inline copy inside
    // ensure_in_simulation() below, which just copied the raw current_time
    // dwords straight across with NO int/frac split -- that was NOT a faithful
    // inlining of this function despite item 2855's old "Inlined in
    // ensure_in_simulation" note; ensure_in_simulation() now calls this real
    // implementation instead.
    void update_time_of_last_motion();

    // FUN_06346e70 (Common.dll.txt:410293-410326, decomp-tracker item 2850,
    // 29 insns). Two confirmed callers pass different pairs: FUN_06346fb0's
    // tail (ivp_core.cpp, "TODO: Synchronize simulation time snapshot")
    // passes {Environment->delta_time, Environment->inv_delta_time} (+0xAC/
    // +0xB0) directly; FUN_0634b480 (item 2989's revive_pass() below) instead
    // passes a locally-computed {Alpha, 1/Alpha}, falling back to the same
    // Environment pair when Alpha is too close to 0 (~1e-10f, DAT_063a53d8) to
    // safely divide. Retail's own body (`MOV EAX,[ESP+4]; MOV EDX,[EAX+4]; MOV
    // [ECX+0x70],EDX`) only ever reads the SECOND element of the pair --
    // DeltaTime is kept as a parameter purely for call-site fidelity (every
    // caller computes and passes it) even though this function itself never
    // reads it.
    void synchronize_simulation_time_snapshot(float DeltaTime, float InvDeltaTime);

    // FUN_06347170 (Common.dll.txt:410593-410665, decomp-tracker item 2856,
    // 63 insns). D-target-2989 pass (2026-09-10): full trace. Sets
    // MovementState's low byte to 1 ("awake"), calls
    // update_time_of_last_motion() above, then sets TimeOfLastPsi directly to
    // Environment->current_time (+0xF8/+0xFC raw dword copy). Retail actually
    // computes TimeOfLastPsi as `current_time - delta_time` (int/frac split,
    // same ftol idiom as update_time_of_last_motion) and writes THAT first,
    // calls update_time_of_last_motion() (which only reads Environment->
    // current_time, never TimeOfLastPsi), and then unconditionally overwrites
    // TimeOfLastPsi again with the RAW original current_time dwords right
    // after (Common.dll.txt:063471fe-06347203) -- the intermediate
    // `current_time - delta_time` value is never read by anything before
    // being clobbered, so it is provably dead and correctly omitted here (same
    // "pure, no observable difference" simplification precedent as
    // recalc_interpolated_frame/update_time_of_last_motion above). Finally
    // walks Core->Objects backwards, force-setting each attached
    // IVP_Real_Object's flags(+0x78) low byte to 1 and calling FUN_063454e0
    // (decomp-tracker item 2799) on each.
    //
    // FUN_063454e0 itself is FULLPORT-DEFERRED here: traced this pass and
    // found it calls FUN_0634ebd0 (decomp-tracker item 3111, 411 insns) on
    // `object->core->Environment->mindist_manager`, then folds the result back
    // into Core->MovementState via a bit-XOR idiom depending on
    // MovementState's PRE-call low byte. FUN_0634ebd0 is independently
    // confirmed `blocked` (large hash-table builder over an unidentified
    // vtable-bearing struct reached via IVP_Real_Object::field_90, itself
    // depending on 3 further unprocessed callees) -- not safe to guess. Item
    // 2799 updated to `blocked` (depends_on [3111]) this pass.
    void mark_active_and_sync_objects();

    // FUN_0634b480 (.text:0634b480-0634b58d; Ipion revive_for_simulation's
    // per-core step). Returns grow_friction_system()'s result; the caller
    // (IVP_Simulation_Unit_Revive_If_Sleeping, 06349490) restarts its core scan
    // on 1. cmn-ivp3d 2026-10-08.
    int revive_pass();

    // FUN_063563d0 (.text:063563d0-063564f5, no arguments; Ipion
    // IVP_Core::grow_friction_system): for every exact mindist of every object
    // of this core, try to put it into a friction system; returns 1 when a new
    // contact point was created. cmn-ivp3d 2026-10-08.
    int grow_friction_system();

    // FUN_063461a0: world-space velocity of a point offset from this core's
    // origin by LeverArmOS (object space), given explicit linear/angular
    // velocity vectors: OutWorldSpeed = MWorldFCore.rows * (AngularVelocityOS x LeverArmOS)
    // + LinearVelocityWS. Generic over the velocity source -- of its 13 call
    // sites only one (FUN_06346be0) passes this core's own Speed/RotSpeed, so
    // both vectors stay explicit parameters rather than being read from `this`.
    void calc_point_speed_ws(const Vector* LeverArmOS, const Vector* LinearVelocityWS,
                              const Vector* AngularVelocityOS, Vector* OutWorldSpeed) const;

    // FUN_06346be0: convenience wrapper using this core's own current Speed/RotSpeed.
    void calc_speed_at_object_position(const Vector* LeverArmOS, Vector* OutWorldSpeed) const;

    // FUN_06346cf0 (Common.dll.txt 410138-410194, decomp-tracker item 2847).
    // Fused helper IVP_Impact_Solver::PushContactPoint (ivp_impact_solver.cpp,
    // ported from retail FUN_06357400, decomp-tracker item 3268) calls twice
    // per invocation, once per dynamic core.
    //
    // CORRECTED 2026-09-09 (D82 pass): an earlier pass ported this with only
    // 4 stack params, using the SAME vector for both the cross product and
    // the linear-delta scale. Retail's RET 0x14 (Common.dll.txt:410195)
    // unambiguously pops 5 stack dwords, not 4 -- re-tracing the full
    // instruction sequence (410150-410195) term-by-term shows param_2 and
    // param_3 are genuinely DIFFERENT vectors representing the same physical
    // impulse in two different frames:
    //   - param_2 (PointImpulseOS) is crossed with LeverArmOS to build the
    //     angular delta -- both operands must be in OBJECT space, since
    //     InvRotInertia (+0x34) is only diagonal in the core's own body
    //     frame, and OutAngularDelta is added directly into a solver
    //     RotSpeed accumulator that's object-space per calc_point_speed_ws's
    //     own confirmed convention above.
    //   - param_3 (PointImpulseWS) is scaled uniformly by InvMass (+0x40) to
    //     build the linear delta, and must stay in WORLD space since it's
    //     added directly into a solver Speed accumulator (world-space).
    //     Rotation doesn't affect a uniform scalar's magnitude, but it very
    //     much affects which frame the resulting vector is expressed in.
    // Callers pass PointImpulseOS = MWorldFCore->vimult(PointImpulseWS) (the
    // same impulse, rotated into this core's object space) -- see
    // ivp_impact_solver.cpp::PushContactPoint for the concrete transform.
    void calc_impulse_deltas(const Vector* LeverArmOS, const Vector* PointImpulseOS,
                              const Vector* PointImpulseWS,
                              Vector* OutLinearDelta, Vector* OutAngularDelta) const;

    // FUN_06346790 (Common.dll.txt 409518-409567, decomp-tracker item 2835).
    // D89 pass (2026-09-09): the per-core "effective mass along a direction"
    // leaf that IVP_Impact_Solver's FUN_06358560/FUN_06358630 (items
    // 3274/3275) call to populate EffectiveMass0/EffectiveMass1 (formerly
    // mislabeled Pad04/Pad08 -- see ivp_mindist.h). NOT the same computation
    // as PushContactPoint's own TotalEffectiveMassInv accumulation despite
    // sharing the calc_impulse_deltas/calc_point_speed_ws call chain -- see
    // the full trace writeup in ivp_core.cpp above the definition. Returns
    // double: retail leaves the result unrounded in ST0, the caller rounds.
    double calc_effective_mass_along_direction(const Vector* LeverArmOS,
                                                const Vector* ImpulseDirOS,
                                                const Vector* ImpulseDirWS) const;

    // FUN_063463f0 (Common.dll.txt:409276-409353, decomp-tracker item 2831,
    // 54 insns, zero callees). D-cluster-ivp-adjacent pass (2026-09-10): a
    // fully self-contained conservative effective-mass estimate over a
    // lever arm, with no explicit direction argument -- unlike
    // calc_effective_mass_along_direction above (which needs a concrete
    // impulse direction in both OS and WS), this takes the WORST case
    // (maximum) over the 3 principal-axis rotational contributions implied
    // by LeverArm alone:
    //   TermX = (LeverArm.y^2 + LeverArm.z^2) * InvRotInertia.x
    //   TermY = (LeverArm.z^2 + LeverArm.x^2) * InvRotInertia.y
    //   TermZ = (LeverArm.x^2 + LeverArm.y^2) * InvRotInertia.z
    //   return 1.0f / (max(TermX,TermY,TermZ) + ((Flags & 0x300) ? 1.0f : InvMass))
    // Every field this function reads (InvRotInertia/InvMass/Flags) is
    // already named above; no callees to resolve. Retail name/purpose
    // (likely a conservative per-contact time-step or solver-stability
    // bound) not recovered -- this port's own name.
    double calc_conservative_effective_mass(const Vector* LeverArm) const; // returned in ST0 unrounded

    // FUN_06347240 (Common.dll.txt:410682-410784, decomp-tracker item 2857),
    // Ipion synchronize_with_rot_z: saves RotSpeed/QWorldFCoreNext into an
    // IVP_Old_Sync_Rot_Z record from the frozen-core arena (FastSimUnitData),
    // moves QWorldFCoreNext/MWorldFCore to the current time and -- gated on
    // `(Flags & 0xC0) == 0` -- refreshes RotSpeed from the PRE-interpolation
    // delta quaternion via asin. Full trace in ivp_core.cpp. Callers:
    // IVP_Ledge_Mindist::do_impact (ivp_mindist_limiter.cpp) and
    // IVP_Impact_System::synchronize_core_if_simulated (ivp_impact.cpp).
    void recalc_interpolated_frame();

    // FUN_06347c40/FUN_06347c80/FUN_06347d20 (Common.dll.txt:411624-411755,
    // decomp-tracker items 2870/2871/2872). D-target-ObjectHashMap pass
    // (2026-09-10): full raw-disasm trace resolving the last open TODO left
    // on ObjectHashMap's own field comment above -- ports the field's real
    // find/insert/remove gate trio as IVP_Core methods. See ivp_core.cpp for
    // the complete evidence trail (calling-convention proof that ObjectHashMap
    // really is FUN_06352930's own "self" container pointer, not an unrelated
    // value carried through by coincidence).
    //
    // All three are dual-mode on `(Flags & 0xC) == 4`: hashmap mode treats
    // ObjectHashMap as a real Robin-Hood container (HashMap063525f0, item
    // 3178, IVP_VHash_Store in src/IVP/ivp_utility/ivp_vhash.cpp) keyed
    // by each Candidate's own +0x8 field; single-slot mode instead treats
    // ObjectHashMap as AT MOST ONE cached candidate pointer stored directly
    // (no container object at all). The candidate entries' own concrete C++
    // type was not determined this pass -- every confirmed call site only
    // ever touches a candidate's +0x8 field, never anything deeper -- so they
    // stay void* rather than guessing a struct layout.
    void* find_object_hash_entry(unsigned Key) const;
    void register_object_hash_entry(void* Candidate);
    void unregister_object_hash_entry(void* Candidate);

    // FUN_06347d50 (Common.dll.txt:411767-411786, decomp-tracker item 2873,
    // 13 insns). D-target063555b0-followup pass (2026-09-10, traced while
    // porting IVP_ListManager::MergeFrom's Phase 2, ivp_mindist_limiter.cpp):
    // unregister_object_hash_entry(Candidate), then if Candidate is non-null,
    // clears its own embedded IVP_U_Vector<T>-shaped header at +0x0 (via
    // FUN_063449d0/item 2767, the same generic IVP_U_Vector<T>::Clear()
    // instantiation IVP_ListManager_CorePairEntry's own teardown already
    // uses) and frees it. Every traced caller's Candidate is a small
    // IVP_Core_ObjectHashEntry-shaped object (see ivp_mindist_limiter.cpp) --
    // kept void* here since find/register/unregister_object_hash_entry above
    // stay deliberately generic (item 3290's unrelated single-slot caller
    // does not reach this specific release path, so its own shape is not
    // assumed).
    // 2026-10-08 (claude-code:cmn-phy3): every caller (06355490, 063555b0,
    // 06355890) releases a friction system's per-core record, which is
    // IVP_Friction_Info_For_Core (ivp_mindist.h, operator_new(0xc)), and
    // FUN_06391d5a is operator delete -- typed accordingly, released with
    // `delete` (the record's IVP_U_Vector destructor is FUN_063449d0, the
    // same Clear retail calls first).
    void release_object_hash_entry(IVP_Friction_Info_For_Core* Candidate);

    // FUN_063474d0 (Common.dll.txt:410941-410980, decomp-tracker item 2861,
    // 35 insns). D-worldtransform pass (2026-09-10): initializes this Core's
    // whole world transform (MWorldFCore/PosWorldFCore/QWorldFCore/
    // QWorldFCoreNext) from a caller-supplied local coordinate system,
    // concatenated onto the Core's EXISTING MWorldFCore (i.e.
    // NewMWorldFCore = OldMWorldFCore * LocalCsys -- see
    // IVP_U_Matrix::mmult4 in ivp_math.h for the full trace of
    // FUN_063481d0/item 2890). Sole traced caller is FUN_06345b20/item 2817.
    // D-item2785-wire pass (2026-09-10): NOW WIRED into
    // IVP_InitCoreRestStateAndDamping (ivp_real_object.cpp) -- see
    // ivp_core.cpp for the full field-by-field body trace and item 2817's own
    // updated notes for the LocalCsys-construction evidence trail that
    // unblocked this.
    void init_world_transform_from_local_csys(const IVP_U_Matrix* LocalCsys);

    // FUN_06347410 (Common.dll.txt:408400 call site inside FUN_06345b20/item
    // 2817; the function's own body is elsewhere in the listing). D-
    // rotinertia-selector pass (2026-09-10): this is the SAME retail function
    // already ported, file-local, as PhySys_RecomputeInverseInertia in
    // common_physys.cpp (that copy operates on raw byte offsets into an
    // opaque `void* Core` for the separate PhySys reimplementation's own
    // callers -- PhySys::SetMass/SetMomentOfInertia -- and is `static` there,
    // so it cannot be called from this retail-accurate IVP_* subsystem
    // directly). This method reproduces the identical logic against this
    // class's own real, named fields instead: InvRotInertia = 1/RotInertia
    // (componentwise), InvMass = 1/Mass, an "are the 3 principal reciprocals
    // nearly equal" isotropic check that sets/clears Flags bits 0x40/0x80,
    // and DAngularLimitFactor = 0.5f/UpperLimitRadius. Retail divides
    // unconditionally (a zero inertia/mass legitimately yields +/-INF), so
    // this must NOT special-case zero -- see PhySys_RecomputeInverseInertia's
    // own comment for the same caveat. Called from
    // IVP_InitCoreRestStateAndDamping (ivp_real_object.cpp) right after
    // RotInertia/Mass are computed, matching retail's own instruction order
    // (Common.dll.txt:408400, immediately after the Mass store at 408377 and
    // the RotSpeedDamp/RotSpeed/Speed inits at 408378-408399).
    void RecomputeInverseInertia();

    // ---- cmn-physys3 block (2026-10-08): the core helpers PhySys calls ----
    // FUN_06347c10 (thiscall, RET 4): Mass = NewMass, RotInertia scaled by NewMass / old Mass,
    // then calc_calc (RecomputeInverseInertia). Reached through IVP_Real_Object 06344d30.
    void set_mass(float NewMass);
    // FUN_06346bb0 (thiscall, RET 4): RotInertia = *Inertia, then calc_calc.
    void set_rotation_inertia(const Vector* Inertia);
    // FUN_06346a00 (thiscall, RET 8): SpeedChange += Direction * (InvMass * Magnitude).
    // PhySys::LinearImpulse.
    void push_core_linear(const Vector* Direction, float Magnitude);
    // FUN_06346c80 (thiscall, RET 8): the world axis taken into core space through
    // MWorldFCore (06366010), scaled by InvRotInertia, RotSpeedChange += it * Magnitude.
    // PhySys::AngularImpulse.
    void push_core_angular_ws(const Vector* AxisWs, float Magnitude);
    // FUN_06346d80 (thiscall, RET 8): the same with a core-space axis.
    // PhySys::AngularImpulseOS.
    void push_core_angular_cs(const Vector* AxisCs, float Magnitude);
    // ---- end cmn-physys3 block ----

    // ------------------------------------------------------------------
    // D-cluster-06346800 pass (2026-09-10): 12-item cluster spanning
    // Common.dll.txt:409576-411849 (decomp-tracker items 2836/2838/2840/
    // 2842/2845/2849/2859/2863/2867/2868/2874/2875), sitting between two
    // DO-NOT-TOUCH ranges. All confirmed IVP_Core methods (`this` used
    // directly against already-static_assert'd fields throughout) -- see
    // each method's own header comment in ivp_core.cpp for the full
    // evidence trail.
    // ------------------------------------------------------------------

    // FUN_06346800 (Common.dll.txt:409576-409621, item 2836, 40 insns).
    // Per-core kinetic energy: 0.5*(Mass*|LinearVelocity|^2 +
    // dot(RotInertia, AngularVelocityOS componentwise-squared)).
    // AngularVelocityOS must be in OBJECT space (RotInertia is only
    // diagonal in the core's own body frame). Confirmed via its sole
    // caller (Common.dll.txt:06369143): a running total-system-energy scan
    // passing this core's PREDICTED post-pending-impulse velocities
    // (Speed+SpeedChange, RotSpeed+RotSpeedChange).
    double calc_kinetic_energy(const Vector* LinearVelocity, const Vector* AngularVelocityOS) const;

    // FUN_06346890 (Common.dll.txt:409659-409726, item 2838, 57 insns).
    // Writes this core's pose at an explicit absolute time
    // (CurrentTimeBase/CurrentTimeOffset, matching Environment::current_time's
    // own 2-dword shape) into a caller-owned *OutMatrix, WITHOUT touching
    // this core's own QWorldFCoreNext/MWorldFCore (unlike
    // recalc_interpolated_frame(), which advances them in place). Falls
    // back to a raw copy of the current MWorldFCore when the core is
    // static (signed MovementState low byte >= 8) or the elapsed-time delta
    // compares equal to 0 or unordered.
    void calc_interpolated_transform(float CurrentTimeBase, float CurrentTimeOffset,
                                      IVP_U_Matrix* OutMatrix) const;

    // FUN_06346a50 (Common.dll.txt:409843-409971, item 2842, 113 insns).
    // Per-axis exponential (or linear-approximated, for small deltas)
    // velocity damping -- see ivp_core.cpp for the full exp()/linear
    // branch-selection formula. Scales RotSpeed componentwise by RotDampVec
    // and Speed uniformly by SpeedDampValue.
    void apply_velocity_damping(float DeltaTime, const Vector* RotDampVec, float SpeedDampValue);

    // FUN_06346980 (Common.dll.txt:409750-409793, item 2840, 34 insns).
    // Dispatcher for apply_velocity_damping() above: when
    // MovementState low byte >= 2, fudges RotSpeedDamp/SpeedDamp by +0.1f
    // (DAT_063a5464) before damping; otherwise uses them raw.
    void damp_velocities(float DeltaTime);

    // FUN_06346c00 (Common.dll.txt:410029-410077, item 2845, 36 insns).
    // The real "apply an impulse at a point" entry point: calls
    // calc_impulse_deltas() and ACCUMULATES the result into
    // RotSpeedChange/SpeedChange (rather than returning it). See
    // commit_speed_changes() below for the matching flush step.
    void apply_impulse_at_point(const Vector* LeverArmOS, const Vector* PointImpulseOS,
                                 const Vector* PointImpulseWS);

    // FUN_06346dd0 (Common.dll.txt:410242-410278, item 2849, 32 insns).
    // Flushes RotSpeedChange/SpeedChange into RotSpeed/Speed, then zeroes
    // both accumulators.
    void commit_speed_changes();

    // FUN_06346960 (.text:06346960-0634697d): zeroes RotSpeedChange and
    // SpeedChange without applying them.
    void clear_speed_change();

    // FUN_063473b0 (Common.dll.txt:410820-410850, item 2859, 26 insns).
    // Restores RotSpeed and QWorldFCoreNext from the IVP_Old_Sync_Rot_Z
    // record FastSimUnitData (+0x1B4) points at, then clears
    // FastSimUnitData to null WITHOUT freeing it (frozen-core arena memory,
    // written by recalc_interpolated_frame()).
    void restore_fast_sim_snapshot();

    // FUN_06347630 (Common.dll.txt:411079-411100, item 2863, 16 insns).
    // Byte-for-byte the already-generic IVP_U_Vector<T>::Clear()
    // (ivp_u_vector.h) run against this core's own Objects field.
    void clear_objects_vector();

    // FUN_063478f0 (Common.dll.txt:411363-411559, item 2867, 184 insns).
    // Per-PSI motion classifier: compares position/orientation deltas since
    // the last short-term and long-term checkpoints against 4 thresholds,
    // updates PosLastMotion/QLastMotion/TimeOfLastMotion and/or
    // PosLastLongMotion/QLastLongMotion/TimeOfLastLongMotion as needed, and
    // returns a new MovementState low-byte value (1/2/3). Sole caller
    // folds the return value straight into MovementState's low byte -- see
    // ivp_core.cpp for the complete formula/evidence trail.
    int classify_motion_state(float CurrentTimeBase, float CurrentTimeOffset);

    // FUN_06347be0 (Common.dll.txt:411572-411595, item 2868, 18 insns).
    // Caches Environment->field_114 into Unconfirmed_0x1B8 (a REAL
    // IVP-internals write, correcting that field's own prior "never
    // written from 0x0634xxxx" note), then calls the still-deferred
    // FUN_06344f50 (item 2787, already `simplified`) on every attached
    // object.
    void notify_objects_and_cache_env_field();

    // FUN_06347d80 (Common.dll.txt:411801-411817, item 2874, 11 insns).
    // Standing "wake this core if needed" dispatcher: no-op if
    // (Flags & 0xC) != 0; otherwise revive_sleeping_core() when
    // MovementState low byte == 8 (static/inactive), else
    // ensure_in_simulation(). Both callees are real, already-ported
    // methods above.
    void wake_or_ensure_simulation();

    // FUN_06347db0 (Common.dll.txt:411829-411849, item 2875, 15 insns).
    // Calls IVP_Real_Object::EnsureInSimulation() on every attached object.
    void ensure_objects_in_simulation();
};

static_assert(sizeof(IVP_Core) == 0x1BC, "IVP_Core size must be 0x1BC (444 bytes)");
static_assert(offsetof(IVP_Core, Flags) == 0x00, "IVP_Core+0x00");
static_assert(offsetof(IVP_Core, RotInertia) == 0x14, "IVP_Core+0x14");
static_assert(offsetof(IVP_Core, Mass) == 0x20, "IVP_Core+0x20");
static_assert(offsetof(IVP_Core, RotSpeedDamp) == 0x24, "IVP_Core+0x24");
static_assert(offsetof(IVP_Core, InvRotInertia) == 0x34, "IVP_Core+0x34");
static_assert(offsetof(IVP_Core, InvMass) == 0x40, "IVP_Core+0x40");
static_assert(offsetof(IVP_Core, SpeedDamp) == 0x44, "IVP_Core+0x44");
static_assert(offsetof(IVP_Core, Objects) == 0x50, "IVP_Core+0x50");
static_assert(offsetof(IVP_Core, ObjectHashMap) == 0x5C, "IVP_Core+0x5C");
static_assert(offsetof(IVP_Core, MovementState) == 0x60, "IVP_Core+0x60");
static_assert(offsetof(IVP_Core, RotSpeed) == 0x94, "IVP_Core+0x94");
static_assert(offsetof(IVP_Core, Speed) == 0xA4, "IVP_Core+0xA4");
static_assert(offsetof(IVP_Core, PosWorldFCore) == 0xB4, "IVP_Core+0xB4");
static_assert(offsetof(IVP_Core, DeltaWorldFCore) == 0xC4, "IVP_Core+0xC4");
static_assert(offsetof(IVP_Core, QWorldFCore) == 0xD4, "IVP_Core+0xD4");
static_assert(offsetof(IVP_Core, QWorldFCoreNext) == 0xE4, "IVP_Core+0xE4");
static_assert(offsetof(IVP_Core, MWorldFCore) == 0xF4, "IVP_Core+0xF4");
static_assert(offsetof(IVP_Core, Margin) == 0x144, "IVP_Core+0x144");
static_assert(offsetof(IVP_Core, BoundingRadius) == 0x14C, "IVP_Core+0x14C");
static_assert(offsetof(IVP_Core, Controllers) == 0x150, "IVP_Core+0x150");
static_assert(offsetof(IVP_Core, OwningSimUnit) == 0x15C, "IVP_Core+0x15C");
static_assert(offsetof(IVP_Core, Unconfirmed_0x1B8) == 0x1B8, "IVP_Core+0x1B8");

// FUN_063557c0: IVP_Core::get_master_core() equivalent, defined in
// ivp_core.cpp (decomp-tracker Common.dll item 3227). Declared here (rather
// than only file-local in ivp_core.cpp) so other IVP_Core-adjacent
// translation units -- e.g. ivp_simulation_unit.cpp's merge/split logic,
// decomp-tracker items 2911/2913 -- can call it directly.
IVP_Core* __fastcall FUN_063557c0(IVP_Core* Core);

// FUN_0634d510 (cdecl): release an object's narrow-phase cache slot -- if +0x40 is set,
// clear the slot's owner (+0x08) and +0x40.
void IVP_Real_Object_Free_Cache_Object(IVP_Real_Object* Object);

// FUN_06351700 / FUN_06351a50 / FUN_06351b90 (and the rest of .text:06351310-0635212f)
// are IVP_Calc_Next_PSI_Solver methods: ivp_calc_next_psi_solver.h.
