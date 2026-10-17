// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "ivp_core.h"
#include "ivp_real_object.h"
#include "ivp_controller.h" // IVP_ListManager is an IVP_Controller_Dependent; IVP_Event_Sim
#include "../ivp_compact_builder/ivp_compact_ledge.h"
#include "../ivp_collision/ivp_collision.h" // IVP_Collision / IVP_Collision_Delegator (mindist bases)
#include "ivp_listener.h" // IVP_Listener_Hull (IVP_Synapse_Real's base)
#include <cstdint>
#include <cstring>

class IVP_Environment;
struct IVP_Friction_Info_For_Core; // declared in full next to IVP_ListManager below

// Synapse status (IVP_Synapse_Real::status, +0x1a): which feature of the
// object the synapse currently tracks. Polygon synapses start at 0 and ball
// synapses at 3 (IVP_Ledge_Mindist::init_mindist, 0634def0 via 0634e090);
// the exact-simplex search (0635bdd0 family) moves polygon synapses between
// 0/1/2 as it narrows the closest features.
enum EIVP_Synapse_Type : int16_t
{
    IVP_SYNAPSE_POINT = 0,
    IVP_SYNAPSE_EDGE  = 1,
    IVP_SYNAPSE_POLY  = 2,
    IVP_SYNAPSE_BALL  = 3
};

class IVP_Ledge_Mindist;

// ============================================================================
// IVP_Synapse_Real (library name), cmn-phy5 2026-10-08. One side of a mindist:
// IVP_Ledge_Mindist holds two of them by value at +0x18 / +0x34 (stride 0x1c),
// built by the eh vector constructor iterator FUN_06391ef0 in the mindist base
// ctor 0634db70 with ctor LAB_0634dd00 / dtor LAB_0634dc50, torn down by the
// eh vector destructor iterator in the mindist dtor 0634dd60 (:0634de24-:0634de36).
// It is an IVP_Listener_Hull: 06350050 files the synapse itself as the payload
// of its object's hull-manager pool, and the hull manager dispatches through
// its vtable 063a55cc:
//   slot 0 06325390 get_type (returns 0)
//   slot 1 0634f9e0 hull_limit_exceeded_event   -> mindist 0634fa40
//   slot 2 0634fa00 hull_manager_is_going_to_be_deleted_event -> delete mindist
//   slot 3 0634f9c0 hull_manager_is_reset       -> mindist 0634fa20
//   slot 4 0634dc60 scalar deleting dtor
// Layout evidence: +0x04 pool slot (06350050 :063500b4 / 06350020); +0x08/+0x0c
// list links on the object's synapse lists (0634fec0 :0634ff06-:0634ff2b,
// 063454b0 :063454d1); +0x10 object (0634e070 :0634e07e; 0628b7b1 / 0628bd14 /
// 0628c0ef in common_physys); +0x14 tagged edge and +0x1a status (0634e090);
// +0x18 int16 mindist - synapse (0634e070 `SUB EAX,ECX; MOV [ECX+0x18],AX`,
// read back by 0634f9e0 / 0634fa00 / 0634f9c0 `MOVSX; ADD`).
// ============================================================================
class IVP_Synapse_Real : public IVP_Listener_Hull
{
public:
    IVP_Synapse_Real* next;         // +0x08: next synapse on l_obj's synapse_list_head / invalid_synapse_list_head
    IVP_Synapse_Real* prev;         // +0x0c
    IVP_Real_Object*  l_obj;        // +0x10
    IVP_Compact_Edge* edge;         // +0x14: tagged, masked `& 0xfffffff0` before use
    int16_t           mindist_offset; // +0x18: (char*)mindist - (char*)this
    int16_t           status;       // +0x1a: EIVP_Synapse_Type

    // LAB_0634dd00: only the vptr store; the fields are filled by
    // init_synapse_real / set_edge (0634def0's callees).
    IVP_Synapse_Real() {}
    // LAB_0634dc50 / LAB_0634dc60 (slot 4).
    virtual ~IVP_Synapse_Real() {}

    IVP_Ledge_Mindist* get_mindist() const
    {
        return reinterpret_cast<IVP_Ledge_Mindist*>(
            reinterpret_cast<char*>(const_cast<IVP_Synapse_Real*>(this)) + mindist_offset);
    }

    // FUN_0634e070 (.text:0634e070-0634e081, RET 8): mindist_offset = mindist - this; l_obj = object.
    void init_synapse_real(IVP_Ledge_Mindist* Mindist, IVP_Real_Object* Object)
    {
        mindist_offset = static_cast<int16_t>(reinterpret_cast<char*>(Mindist) - reinterpret_cast<char*>(this));
        l_obj = Object;
    }

    // FUN_0634e090 (RET 8): edge = Edge; status = Status.
    void set_edge(IVP_Compact_Edge* Edge, int16_t Status)
    {
        edge = Edge;
        status = Status;
    }

    // FUN_0634ded0 (.text:0634ded0-0634dee4): the ledge the (masked) edge's
    // triangle belongs to -- tri = edge & ~0xf; tri - ((*(uint32*)tri & 0xfff) + 1) * 16.
    // Defined in ivp_mindist_limiter.cpp.
    const IVP_Compact_Ledge* get_ledge() const;

    // ---- IVP_Listener_Hull (vtable 063a55cc), defined in ivp_mindist_limiter.cpp ----
    // slot 0, 06325390 (shared `XOR EAX,EAX; RET`).
    int32_t get_type() override { return 0; }
    // slot 1, LAB_0634f9e0 (RET 8): get_mindist()->hull_limit_exceeded_event(HullIntrusionValue).
    void hull_limit_exceeded_event(IVP_Hull_Manager* HullManager, float HullIntrusionValue) override;
    // slot 2, LAB_0634fa00 (RET 4): if the mindist address is non-null, its slot 4 with 1 (delete).
    void hull_manager_is_going_to_be_deleted_event(IVP_Hull_Manager* HullManager) override;
    // slot 3, LAB_0634f9c0 (RET 8): get_mindist()->hull_manager_is_reset(DeltaTime, CenterDeltaTime).
    void hull_manager_is_reset(float DeltaTime, float CenterDeltaTime) override;
};
static_assert(offsetof(IVP_Synapse_Real, HullPoolSlot) == 0x04, "IVP_Synapse_Real+0x04 -- 06350050 :063500b4");
static_assert(offsetof(IVP_Synapse_Real, next) == 0x08, "IVP_Synapse_Real+0x08 -- 0634fec0 :0634ff06");
static_assert(offsetof(IVP_Synapse_Real, prev) == 0x0C, "IVP_Synapse_Real+0x0c");
static_assert(offsetof(IVP_Synapse_Real, l_obj) == 0x10, "IVP_Synapse_Real+0x10 -- 0634e070 :0634e07e");
static_assert(offsetof(IVP_Synapse_Real, edge) == 0x14, "IVP_Synapse_Real+0x14 -- 0634e090");
static_assert(offsetof(IVP_Synapse_Real, mindist_offset) == 0x18, "IVP_Synapse_Real+0x18 -- 0634e070 :0634e07a");
static_assert(offsetof(IVP_Synapse_Real, status) == 0x1A, "IVP_Synapse_Real+0x1a -- 0634e090");
static_assert(sizeof(IVP_Synapse_Real) == 0x1C, "IVP_Synapse_Real stride -- mindist holds two at +0x18/+0x34");

// D110 pass (2026-09-09): resolved the D104/D107 class-identity question for
// the IVP mindist candidate-winner-selection / impact-dispatch cluster
// (Common.dll.txt 0x06358d60-0x0635a1c0, decomp-tracker items 3282-3300).
// D107's blocker notes flagged an apparent conflict: FUN_06358d60 (item
// 3282, this=ESI) reads ESI+0x30/+0x34 and ESI+0x98/+0xa8 as if they were
// "pointer pairs", which looked incompatible with this class's Cores[2]
// (pointers, correct) and CoordOS[2] (float Vector data, apparently NOT
// pointers) declared at those same offsets.
//
// Full re-trace of FUN_06358d60's own disasm (both branches,
// Common.dll.txt:441275-441310) resolves this as NOT a conflict:
//   - ESI+0x98 / ESI+0xa8 are only ever used via LEA (address-of), never
//     dereferenced -- e.g. `LEA EDX,[ESI+0x98]` / `LEA EDX,[ESI+0xa8]`,
//     with the resulting addresses handed onward as `Vector*` (they become
//     IVP_Impact_Solver::ContactCoordsOS[0]/[1], exactly matching the
//     existing IVP_Impact_Solver_Long_Term::DoImpact body in ivp_impact_solver.cpp:985-986,
//     `Solver->ContactCoordsOS[0] = &CoordOS[0];`). Taking the address of a
//     `Vector CoordOS[2]` array element is completely normal and was never
//     actually in conflict with CoordOS being float data -- D104/D107 read
//     an address-of as if it were a stored pointer VALUE.
//   - ESI+0x30 / ESI+0x34 genuinely ARE dereferenced as pointers
//     (`MOV EAX,[ESI+0x30]; MOV EAX,[EAX+0x98]`). CORRECTED in a follow-up
//     pass (2026-09-09, see the dedicated correction paragraph further below,
//     right before the field declarations): they hold `IVP_Real_Object*`
//     (renamed Objects[2]), not `IVP_Core*` -- the `+0x98` further
//     dereference is IVP_Real_Object::core, not an IVP_Core field. The
//     original D110 pass matched this offset against the pre-existing (and,
//     it turns out, mislabeled) `Cores[2]` declaration without independently
//     verifying the pointed-to class; it stayed correct that ESI+0x30/+0x34
//     ARE valid pointers, just wrong about which class they point to.
//   - Status (+0x52, `MOVSX EDX,word ptr [ESI+0x52]`), CollSpeed (+0x60,
//     `FLD float ptr [ESI+0x60]`) and ContactNormalWS (+0x00-0x08) all match
//     the current declaration exactly, with no interpretation ambiguity.
// Cross-checked against FUN_063591a0 (item 3289, the cluster's "find the
// winning candidate" hub): its post-loop code resolves `this` for
// FUN_06358d60 as `*(winner_grandchild + 0x40)` (Common.dll.txt:06359265-
// 063592ce) and independently confirms the SAME object's +0x52 field as an
// incrementing counter (`INC word ptr [owner+0x52]`) right before the call
// -- i.e. this cluster's "owner"/"this" IS the exact same retail class as
// the already-declared IVP_Impact_Solver_Long_Term, not a different/coincidentally-similar
// one. The class-identity question is RESOLVED: same class, no conflict.
//
// While resolving this, two real (separate) issues were found and fixed
// this pass:
//   1. CoordOS[2]'s stride was wrong. `LEA ESI+0x98` / `LEA ESI+0xa8` are
//      exactly 0x10 (16 bytes) apart, not the 0x0C (12 bytes) a tightly
//      packed `Vector CoordOS[2]` implies (Vector itself is a plain 3-float,
//      12-byte, unpadded class -- see common_ini.h -- so a raw C++ array
//      would place element 1 at +0xA4). This 16-byte-per-Vector-slot stride
//      matches the SAME convention IVP_Core uses throughout (RotSpeedChange/
//      SpeedChange/RotSpeed/Speed/PosWorldFCore/DeltaWorldFCore in
//      ivp_core.h, each a 12-byte Vector padded out to a 16-byte slot).
//      Fixed below via IVP_Mindist_PadVec.
//   2. This class's own retail-offset comments (ImpactCounter +0x50, Status
//      +0x52, CollSpeed +0x60, CoordOS +0x98) were NEVER actually enforced
//      by padding members -- with no gap-filler declared between Cores[2]
//      (ending +0x38) and ImpactCounter, the compiler packed ImpactCounter
//      at the real offset +0x38, not +0x50, and every field after it was
//      correspondingly wrong too. Harmless so far (the one existing caller,
//      DoImpact in ivp_impact_solver.cpp, only ever uses named-member access,
//      which stays self-consistent regardless of the real offset), but a
//      landmine for any future byte-exact use (WinDbg smoke-testing,
//      porting FUN_06358d60/FUN_063591a0 as real methods). Fixed below by
//      adding real padding members for every documented gap, matching the
//      convention IVP_Core and IVP_Ledge_Mindist already use. This makes
//      the true retail size at least 0xB8 (CoordOS[1] ends 0xB4, rounds to
//      0xB8), NOT the ~0xA4 previously assumed (see the corrected note near
//      IVP_Ledge_Mindist below) -- more bytes past +0xB8 may still exist,
//      unconfirmed.
//
// Also newly identified this pass, in what was previously a fully
// unaccounted +0x54-0x98 gap (all from FUN_063591a0/item 3289's own trace,
// Common.dll.txt:06359217-063592c4, where the "owner" = *(winner+0x40) is
// this same IVP_Impact_Solver_Long_Term):
//   - StateProcessed (+0x54): gates an expensive per-candidate recompute
//     (FUN_063595c0/item 3294) -- `CMP byte ptr [owner+0x54],0x1; JZ skip`.
//     Exact write-side semantics not traced this pass.
//   - LastImpactTimeStep (+0x58): read and passed directly as FUN_06358d60's
//     TimeStep argument (`MOV EDX,[owner+0x58]` -> pushed as DoImpact's
//     float TimeStep param). Producer not identified this pass.
//   - WinnerPriority (+0x5C): the float FUN_063591a0 compares against a
//     running minimum to pick the winning candidate (`FLD float ptr
//     [owner+0x5c]; FCOMP ...`). Likely written by FUN_063595c0/item 3294.
//   - ChildCandidate[2] (+0x70/+0x74): two pointer slots. FUN_063591a0's
//     own post-loop code dereferences these directly as pointers to an
//     object with its own +0x1b4 field (`MOV EDI,[owner+EBP*1]` for
//     EBP=0x74,0x70; `TEST EDI,EDI`; `MOV EAX,[EDI+0x1b4]`) -- unresolved
//     child-candidate pointer type, consistent with the "child mindist"
//     concept IVP_Complex_Mindist already uses elsewhere in this file
//     (different class, same idea). FUN_06358d60 (item 3282) ALSO reads
//     these same two fields for a second, asymmetric purpose while building
//     a local IVP_Impact_Solver -- with a `[ESI+0x30/0x34]->+0x98` fallback
//     when a slot is zero. ChildCandidate[2] stays typed as a bare `void*`
//     (the class it points to is still unidentified).
//
// RESOLVED (2026-09-09, follow-up pass): the `Cores[N]->+0x98` fallback D110
// flagged as contradicting ivp_core.h's RotSpeed.y at +0x98 was traced to its
// actual bytes and is NOT a conflict with IVP_Core at all -- it was this
// class's OWN +0x30/+0x34 field that was mistyped. Proof, from a function
// D110 did not examine, FUN_06358c50 (Common.dll.txt:441108-441145, a
// this=<unidentified candidate helper>, IVP_Impact_Solver_Long_Term* param_1=ESI populate
// routine called from 6 sites across this cluster incl. FUN_06358f40/item
// 3284):
//   `MOV EBX,[EDI+0x10]` ... `MOV [ESI+0x30],EBX` (ESI = the IVP_Impact_Solver_Long_Term
//   being populated, EDI = FUN_06358c50's own `this`) -- i.e. this class's
//   own +0x30 field is a DIRECT COPY of EDI+0x10, no dereference at write
//   time. The very next instructions use that same EBX value as a pointer
//   and dereference it at +0x18 (`MOV EAX,[EBX+0x18]`, then `MOV
//   EBX,[EAX+0x9c]` and two vtable calls through it) -- +0x18 on an
//   IVP_Core would be RotInertia.y (a float, nonsensical to dereference as a
//   pointer), but +0x18 on an IVP_Real_Object is the already-confirmed
//   `environment` field (IVP_Object base, ivp_real_object.h:25) -- a real
//   pointer, and IVP_Environment+0x9c being a further manager pointer with
//   its own vtable is completely ordinary. So the value FUN_06358c50 writes
//   into this class's +0x30 (and, symmetrically, +0x34, from EDI+0x24 -> EBP)
//   is an `IVP_Real_Object*`, NOT an `IVP_Core*`.
// That makes the FUN_06358d60 fallback read exactly `Objects[N]->core`:
// IVP_Real_Object::core lives at +0x98 (ivp_real_object.h:47), independently
// confirmed by a completely unrelated function, FUN_06343700 (item 2744,
// Common.dll.txt:404281,404323-404325: `MOV EDI,[EBX+0x98]` reading one
// IVP_Real_Object's `core`, then `MOV [ESI+0x98],EDI` / `+0x9c` / `+0xa0`
// writing another IVP_Real_Object's core/original_core/field_a0 -- the exact
// "reassign an object's physics core" operation of IVP_Merge_Object_Into_Core
// (06343700)). A THIRD independent witness: the sibling
// mindist_limiter lane's own trace of FUN_06356bc0 (ivp_mindist_limiter.cpp,
// around its item 3276 notes) found the exact same `[[EBX+0x30]+0x98]`
// pattern used as a base for reads at +0x14/+0x18/+0x1c/+0x20 (RotInertia+
// Mass-shaped) and +0x94/+0x98/+0x9c/+0xa4/+0xa8/+0xac -- i.e. RotSpeed
// (+0x94) and Speed (+0xA4) read BACK-TO-BACK off the double-dereferenced
// pointer, exactly matching ivp_core.h's IVP_Core layout once you accept
// that pointer is `Objects[N]->core`, not `Objects[N]` itself.
//
// CONCLUSION: ivp_core.h needs NO changes -- RotSpeed.y at IVP_Core+0x98 was
// correct all along (also independently reconfirmed by FUN_06346fb0's
// zeroing sequence, Common.dll.txt:410439-410451, which zeroes +0x94/+0x98/
// +0x9c as one Vector alongside Speed/SpeedChange/RotSpeedChange at their
// own already-declared offsets). The bug was entirely in THIS class: the
// field at +0x30/+0x34 was mistyped as `IVP_Core* Cores[2]` when it is
// really `IVP_Real_Object* Objects[2]`. Fixed below. The OLD `Objects[2]`
// declared at +0x28 had no independent disasm citation of its own (pure
// sequential-packing guess by an earlier pass) and is demoted to unconfirmed
// padding rather than deleted outright -- what (if anything) really lives at
// +0x28/+0x2C is still open for a future lane.
//
// D-item3282-followup pass (2026-09-09): item 3282's two remaining assigned
// blockers were resolved this pass:
//   1. FUN_06273bc0 (Common.dll.txt:0x06273bc0-0x06273bed, called from
//      FUN_06358d60 at :441271) is the generic MSVC-generated "vector
//      constructor iterator" thunk -- thiscall-loop over `param_3` elements
//      of `param_2` bytes each starting at `param_1`, calling ctor function
//      pointer `param_4` on each -- NOT IVP-specific (21 call sites across
//      unrelated clusters, e.g. FlashLightSetInfo). At this call site it
//      constructs 2 elements of 0x10 bytes at local IVP_Impact_Solver+0x84,
//      suggestively matching a padded-to-16-byte Vector-slot pair
//      (SpeedDelta0/RotDelta1) but IVP_Impact_Solver's layout fix for that
//      is left to the IVP_Impact_Solver lane (touches every field past
//      +0x80). See the trace comment above IVP_Impact_Solver_Long_Term::DoImpact in
//      ivp_impact_solver.cpp for the full writeup.
//   2. The asymmetric Cores[0]/[1] + ContactCoordsOS[0]/[1] population
//      branch (Common.dll.txt:441275-441310) is now ported disasm-exact
//      into IVP_Impact_Solver_Long_Term::DoImpact, replacing the previous clean-room
//      "always Objects[N]->core" approximation -- see the same trace
//      comment in ivp_impact_solver.cpp for the full per-branch derivation.
//      New convergent evidence on ChildCandidate[2]'s pointee: both branches
//      copy a ChildCandidate[N] raw pointer directly into a Cores[N]
//      (IVP_Core*) slot with zero validation, matching FUN_063591a0/item
//      3289's independent finding that ChildCandidate is dereferenced at
//      +0x10 with the same idiom IVP_Core::AxisLock (+0x10) uses (that
//      field was named PhysicalObject and typed IVP_Real_Object* when this
//      note was written; retyped IVP_Core_AxisLock* on 2026-09-17).
//      Suggestive that ChildCandidate[N] is IVP_Core-compatible when
//      non-null, but its own allocation/construction site is still
//      untraced, so it stays typed as a bare `void*` here rather than being
//      retyped -- do not upgrade this to IVP_Core* without independently
//      confirming a real construction/assignment site.
// Still open for a future lane (genuinely separate, unexamined functions,
// not part of this pass's 2 assigned blockers): FUN_06358c00 (called twice
// from FUN_06358d60, this=local Solver -- populates Solver+0xFC/+0x108/
// +0x10C-ish scratch slots this pass found pre-seeded with CollSpeed / a
// branch-dependent normal pointer / &ContactPointWS, exact effect unknown)
// and FUN_06359380 (conditional second call gated on an outside-this-cluster
// byte flag). No store into Solver+0x10..+0x1B exists in FUN_06358d60's
// body; that slot is IVP_Impact_Solver::Unread_0x10 (no reader in retail).
//
// See decomp-tracker Common.dll items 3282/3287/3289/3290 (updated this
// pass) for the full evidence trail.

// A Vector padded out to retail's actual 16-byte per-element slot size (see
// the class-level comment above) while still converting implicitly to
// `Vector*`/`const Vector*` for existing call sites that take &CoordOS[N].
struct IVP_Mindist_PadVec : public Vector
{
    float _Pad; // retail's unused 4th float lane; not independently confirmed to hold anything meaningful
};
static_assert(sizeof(IVP_Mindist_PadVec) == 0x10, "IVP_Mindist_PadVec must be a padded-to-16-byte Vector slot");

// Collision pair / minimum distance tracker
class alignas(16) IVP_Impact_Solver_Long_Term
{
public:
    Vector             ContactNormalWS;    // +0x00: Normalized contact normal (World Space)
    float              CollDist;           // +0x0C: Minimum distance between objects
    // +0x10: the library's `speed` (relative speed of the contact). The impact
    // solver writes it through IVP_Impact_Solver::FrictionVectorWS (+0x10C),
    // which do_impact points here (ivp_impact.cpp); do_impact saves and
    // restores it around the impact. Was called ContactPointWS, with no writer.
    Vector             Speed;              // +0x10
    float              ContactRadius;      // +0x1C: Margin / radius
    // +0x20..0x2B: RETYPED 2026-09-17 (claude:common-ivp2, decomp-tracker item
    // 3297). This span was declared `IVP_Synapse_Real* Synapses[2]` -- two
    // POINTERS -- with no disasm citation of its own, and nothing anywhere in
    // this port ever read it. Three independent retail sites prove it is a
    // world-space Vector instead:
    //   1. FUN_063599f0 (item 3297) ends by storing the three floats of its own
    //      first point argument here: `FSTP [ESI+0x20]` / `FSTP [ESI+0x24]` /
    //      `MOV [ESI+0x28],EDX` (Common.dll.txt:06359ac0-06359ac9). An x87 FSTP
    //      to +0x20 and +0x24 is a float-typing proof, and +0x28 -- which the
    //      old declaration called padding -- receives the third component.
    //   2. FUN_0635a1e0 (item 3301) at Common.dll.txt:0635a429-0635a432 calls
    //      FUN_0628be60 (`Vec3 AddScaled(dst=ECX, base, dir, scale)`, item 772)
    //      with dst=base=&this[0x20] and dir=&this[0x00] (ContactNormalWS), i.e.
    //      `SolverContactPointWS += ContactNormalWS * t` -- only meaningful on a
    //      3-float vector.
    //   3. The same function feeds it into BOTH per-object transform blocks:
    //      `FLD [ESI+0x20]; FSUB [core+0x124]` at :0635a4c7 (object 1, result to
    //      CoordOS[0]/+0x98) and the identical `FLD [ESI+0x20]` at :0635a69a
    //      (object 2, result to CoordOS[1]/+0xA8). So this is the SINGLE shared
    //      world-space contact position that both object-space coordinates are
    //      derived from.
    // (+0x10 is the relative speed, see Speed above; this is the contact point.)
    Vector             SolverContactPointWS; // +0x20..0x2B: see the evidence block above.
    uint8_t            _pad2c[0x04];       // +0x2C..0x2F: unconfirmed (was part of the old `_pad28[0x08]`).
    IVP_Real_Object*   Objects[2];         // +0x30: Real objects involved -- RETYPED this pass (follow-up,
                                            // 2026-09-09; was `IVP_Core* Cores[2]`, a mislabel -- see the
                                            // dedicated correction paragraph in the class-level comment above).
    // +0x38..0x47: NEW (item 3283/3280/3278 pass, 2026-09-10) -- carved out of
    // the old `_pad38[0x10]` span by fully tracing FUN_06358c50 (decomp-tracker
    // item 3280, Common.dll.txt:441108-441145) and its own callee FUN_06358bb0
    // (item 3278, Common.dll.txt:441023-441063), both called with this=Candidate
    // (an `IVP_Contact_Point*`, see that struct below) and param_1=this
    // exact `IVP_Impact_Solver_Long_Term* Owner`. FUN_06358c50's own body:
    //   Owner->Objects[0] = Candidate->Object1;   (already declared above)
    //   Owner->Objects[1] = Candidate->Object2;
    //   Owner->Feature0   = Candidate->Feature1;  (NEW, this pass)
    //   Owner->Feature1   = Candidate->Feature2;  (NEW, this pass)
    //   FUN_06358bb0(Candidate, &Owner->Materials[0]);
    //   Owner->ContactElasticity = material_manager->get_elasticity(Owner);
    //   Candidate->FrictionCoefficient = material_manager->get_friction_factor(Owner);
    // 2026-10-08 (claude-code:cmn-phy3): the object at IVP_Environment+0x9c is
    // the IVP_Material_Manager (ivp_material.h, vtable 063a551c), so +0x40/+0x44
    // are the two sides' IVP_Material* and both manager dispatches are ported --
    // see IVP_PopulatePairRecordIntoOwner in ivp_mindist_limiter.cpp.
    void*              Feature0;           // +0x38: tagged IVP_Compact_Edge* copy of Candidate->Feature1; mask
                                            // `&0xfffffff0` before use, same convention as IVP_Ledge_Mindist::FeatureA/B.
    void*              Feature1;           // +0x3c: mirror of Feature0, Candidate->Feature2.
    IVP_Material*      Materials[2];       // +0x40, +0x44: per-side FUN_06358bb0 output (the object's material,
                                            // or the manager's get_material_by_index for a tagged feature).
    float              _field48;           // +0x48: NEW (item 3276 pass, 2026-09-09) -- confirmed FLOAT (FSTP), written by FUN_06358680 (Common.dll.txt:06358a68) as (per-side-A minus per-side-B) of a quaternion/rotation dot-product pair computed over Objects[0]->core/Objects[1]->core. RESOLVED (follow-up pass, 2026-09-09): the source operands trace through `Objects[N]->+0x98`, which is IVP_Real_Object::core (ivp_real_object.h:47), NOT a field on IVP_Core -- the apparent conflict with ivp_core.h's RotSpeed.y this comment originally flagged is gone; see the class-level comment above for the full evidence trail. Semantic meaning of the dot-product itself is still not resolved.
    float              _field4c;           // +0x4C: NEW (this pass, 2026-09-09, item 3276) -- companion float to _field48, same call site (Common.dll.txt:06358a73). Ends exactly at +0x50, so no further padding is needed before ImpactCounter. See _field48.
    // +0x50..+0x67: 2026-10-08 (claude-code:cmn-phy2). The friction solver
    // (ivp_friction_solver.cpp, .text:06368300-063691e4) uses this same object
    // as its per-contact record (it is retail's IVP_Impact_Solver_Long_Term: the
    // contact point's +0x40), and writes several of these slots with a different
    // type than the impact path does -- retail overlays the two uses, so the
    // port declares unions:
    //   +0x50 word: FUN_06368970 writes the contact's index in the friction
    //     matrix (`MOV word ptr [EDI+0x50],AX`) or 0xffff, FUN_06368a30 reads it
    //     with MOVSX. Nothing else in the port touched the old `ImpactCounter`
    //     name, so it is renamed.
    //   +0x55: bits 0..1 of the dword at +0x54 (`AND ECX,0x300; CMP ECX,0x100`,
    //     .text:06368da8/06356598).
    //   +0x58/+0x5c: two IVP_Friction_Info_For_Core* written by FUN_06368970
    //     from IVP_Core::find_object_hash_entry (06347c40).
    //   +0x60: dword copy of IVP_Contact_Point::HasNegativePullSince
    //     (`MOV ECX,[ESI+0x64]; MOV [EDI+0x60],ECX`, .text:063689d6-063689dc),
    //     tested as an int by FUN_06368a30 (.text:06368b0b).
    //   +0x64: float copy of IVP_Contact_Point::GapDistance (.text:063689d0-
    //     063689d3); FUN_06368a30 subtracts it from IVP_Mindist_Settings+0x10c.
    int16_t            IndexInFs;          // +0x50: see above (was ImpactCounter, unreferenced)
    int16_t            Status;             // +0x52: Status / impact iteration count -- D110 (2026-09-09): confirmed INC'd by FUN_063591a0 (Common.dll.txt:063592bd) and read as DoImpactSystem's ImpactCounter arg by FUN_06358d60 (:06358ebc)
    uint8_t            StateProcessed;     // +0x54: NEW (D110, 2026-09-09) -- 0/1 "already processed this pass" gate, see FUN_063591a0, Common.dll.txt:06359217
    // +0x55 (formerly Flags55): bits 0..1 = (dword +0x54 >> 8) & 3. Apart
    // from whole-dword initialisation, every writer found stores the value 1 (`AND ~0x200; OR 0x100`: degenerate contact
    // frame, 063598d9 / 06359cfa / 0635a0e2 / 0635a109); every reader tests (bits & 3) == 1 and then
    // removes the contact from its friction system (06368da8, 06356598,
    // 06355c47, 06358f17). Bit 1 is never set in Common.dll.
    uint8_t            FrictionIsBroken;   // +0x55
    uint8_t            _pad56[0x02];       // +0x56..0x57: unconfirmed
    union
    {
        struct
        {
            float      LastImpactTimeStep; // +0x58: NEW (D110, 2026-09-09) -- consumed as DoImpact's TimeStep argument by FUN_063591a0, Common.dll.txt:063592c4
            float      WinnerPriority;     // +0x5C: NEW (D110, 2026-09-09) -- candidate-selection comparison key, FUN_063591a0, Common.dll.txt:06359230
        };
        IVP_Friction_Info_For_Core* FrictionInfos[2]; // +0x58/+0x5C: friction use, see above
    };
    union
    {
        // +0x60: the pair's combined elasticity -- FUN_06358c50 stores
        // material_manager->get_elasticity(this) here (`CALL [EAX+0x8]; FSTP
        // float ptr [ESI+0x60]`, .text:06358c89-06358c8c), and DoImpact feeds
        // it to FUN_06358c00. Was named CollSpeed, with no writer behind it.
        float          ContactElasticity;
        int32_t        HasNegativePullSince; // +0x60: friction use, see above
    };
    float              GapDistance;        // +0x64: friction use, see above
    // +0x68/+0x6C: RESOLVED 2026-09-17 (lane claude:common-ivp5, decomp-tracker
    // item 3301). FUN_0635a1e0 builds both, one per object side, and they are
    // the contact's scalar effective-mass pair:
    //   InvEffectiveMass (+0x68) = DAT_063a53d0(1.0f) / EffectiveMassDenom,
    //     a single `FLD 1.0f; FDIV [ESI+0x6c]; FSTP [ESI+0x68]`
    //     (Common.dll.txt:0635a85b-0635a867).
    //   EffectiveMassDenom (+0x6C) accumulates, per side,
    //     `core->InvRotInertia . (r x n)^2 + core->InvMass` where (r x n) is
    //     that side's CoordOSCrossNormal below -- Common.dll.txt:0635a60f-
    //     0635a649 writes the first side's term, :0635a7f5-0635a830 adds the
    //     second's (`FADD [EBX+0x40]` is IVP_Core::InvMass at +0x40 and the
    //     three `FMUL [EBX+0x34/0x38/0x3c]` are IVP_Core::InvRotInertia, both
    //     already static_assert'd in ivp_core.h). A kinematic side contributes
    //     nothing: its whole branch zeroes the field instead
    //     (Common.dll.txt:0635a65e).
    float              EffectiveMassDenomRecip; // +0x68
    float              EffectiveMassDenom;      // +0x6C
    // +0x70/+0x74: RETYPED void* -> IVP_Core* 2026-09-17 (lane
    // claude:common-ivp5, decomp-tracker item 3301). The old comment said "its
    // own construction site is untraced; stays void* until independently
    // confirmed" -- FUN_0635a1e0 IS that construction site, and it settles the
    // type outright: `MOV EBX,[ECX+0x98]` loads Objects[side]->core (the
    // already-declared IVP_Real_Object::core) and the very next use of EBX is
    // `MOV [ESI+0x70],EBX` / `MOV [ESI+0x74],EBX` (Common.dll.txt:0635a4b6 +
    // :0635a685, and :0635a68b + :0635a861). Both slots are zeroed instead when
    // that side's core fails the `TEST byte ptr [core],0xc` kinematic-body test
    // (`XOR EBX,EBX` at :0635a650 / :0635a859), which is exactly the null that
    // IVP_Impact_Solver_Long_Term::DoImpact's two branches already test for. The legacy NAME is
    // kept only to avoid churning its dozen existing references; it is a per-
    // side IVP_Core pointer, not a "child candidate".
    IVP_Core*          ChildCandidate[2];  // +0x70, +0x74
    // +0x78..0x97: RESOLVED 2026-09-17 (claude:common-ivp2, decomp-tracker item
    // 3297). This was one opaque `_pad78[0x20]` blob; it is really the contact's
    // two-vector FRICTION BASIS, built to be orthonormal with ContactNormalWS.
    //   TangentAxis0 (+0x78): FUN_063599f0 writes the three components through
    //     `LEA ECX,[ESI+0x78]` + `FSTP [ECX]` / `FSTP [ECX+4]` / `FSTP [ECX+8]`
    //     (Common.dll.txt:06359a7c, :06359aa3-06359ab0) and then normalizes the
    //     result in place via FUN_06347ef0 (item 2881). The value stored is
    //     cross(ContactNormalWS, R) with R = (1,0,0) when N.x*N.x < 0.9f
    //     (DAT_063a53fc = 0x3f666666) and R = (0,0,1) otherwise -- the standard
    //     "pick whichever cardinal axis is least parallel to N" construction.
    //     FUN_0635a1e0 re-normalizes the same field at :0635a46f-0635a474.
    //   TangentAxis1 (+0x88): FUN_0635a1e0, Common.dll.txt:0635a479-0635a4ad,
    //     computes cross(ContactNormalWS, TangentAxis0) component-by-component
    //     (`FLD [EBX+4]; FMUL [ESI]` ... with EBX = this+0x78, ESI = this) and
    //     stores it to +0x88 / +0x8c / +0x90. Together with TangentAxis0 and the
    //     normal this is a right-handed orthonormal frame.
    // The 4-byte gaps at +0x84 and +0x94 are the usual 16-byte-slot padding this
    // class already uses for CoordOS[2] (IVP_Mindist_PadVec) -- no instruction
    // touches either one.
    Vector             TangentAxis0;       // +0x78..0x83
    uint8_t            _pad84[0x04];       // +0x84..0x87
    Vector             TangentAxis1;       // +0x88..0x93
    uint8_t            _pad94[0x04];       // +0x94..0x97
    IVP_Mindist_PadVec CoordOS[2];         // +0x98 / +0xA8: Contact coords in object local spaces (D110: corrected to the real 0x10-byte-per-element stride, see class-level comment above)
    // +0xB8..+0xD7: NEW (D-item3301 pass, 2026-09-10) -- carved out of what used
    // to be unconfirmed trailing space. FUN_0635a1e0 (decomp-tracker item 3301,
    // Common.dll.txt:0635a1f9, `PUSH 0xd8; CALL FUN_06347380`) allocates a
    // *retail* IVP_Impact_Solver_Long_Term as a full 0xD8-byte block (not merely the ">= 0xB8"
    // this class previously asserted) and writes real floats out to +0xb8/+0xbc/
    // +0xc0 (Common.dll.txt:0635a5f0-0635a679, a rotated-normal-ish Vector) and,
    // on a second/mirrored code path, +0xc8/+0xcc/+0xd0 (Common.dll.txt:0635a7c3-
    // 0635a7d7, the SAME shape for the other object side). Only the SIZE/
    // primitive-type/write-site is settled this pass (2 confirmed Vector-shaped
    // float triples); semantic names are not assigned per this task's
    // placeholder-field policy. FUN_0635a1e0's own remaining ~480 instructions
    // (the matrix/rotation blocks that produce these values, a 2-level switch,
    // and calls into IVP_Core::calc_point_speed_ws) are NOT traced this pass --
    // see decomp-tracker item 3301's own notes for the full itemized breakdown.
    // +0xB8..+0xD7: RESOLVED 2026-09-17 (lane claude:common-ivp5, decomp-tracker
    // item 3301). Was `_pad_b8[0x20]`, "2 confirmed Vector-shaped float triples,
    // rest still unconfirmed". The two triples are one per object side and each
    // is the LEVER-ARM CROSS NORMAL for that side, both in that side's own
    // object space:
    //     CoordOSCrossNormal[side] = cross(CoordOS[side], vimult(core, ContactNormalWS))
    // FUN_0635a1e0 emits them component-by-component right after building
    // CoordOS[side]: Common.dll.txt:0635a54a-0635a604 (side 0, into +0xb8/+0xbc/
    // +0xc0) and :0635a71d-0635a7d7 (side 1, into +0xc8/+0xcc/+0xd0), with the
    // rotation done by the same MWorldFCore row triple (+0xf4/+0x104/+0x114)
    // transposed -- i.e. IVP_U_Matrix3::vimult -- that the CoordOS blocks
    // immediately above use. A kinematic side zeroes its triple instead
    // (Common.dll.txt:0635a673 / :0635a847). EffectiveMassDenom above is
    // literally `InvRotInertia . CoordOSCrossNormal^2 + InvMass` summed over
    // both, which is the standard rigid-body contact denominator and is the
    // independent cross-check on this reading.
    // The stride is the same 16-byte padded slot CoordOS[2] already uses (+0xb8
    // and +0xc8 are 0x10 apart), and two of them end exactly at +0xd8 = the
    // confirmed retail allocation size, with no trailing bytes left over.
    IVP_Mindist_PadVec CoordOSCrossNormal[2]; // +0xB8 / +0xC8
};
static_assert(sizeof(IVP_Impact_Solver_Long_Term) >= 0xD8, "IVP_Impact_Solver_Long_Term must be at least as large as its confirmed retail fields (D-item3301 pass, 2026-09-10: FUN_0635a1e0's own operator_new(0xd8) call site confirms the real retail alloc size)");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, SolverContactPointWS) == 0x20, "IVP_Impact_Solver_Long_Term+0x20");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, Feature0) == 0x38, "IVP_Impact_Solver_Long_Term+0x38");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, Feature1) == 0x3C, "IVP_Impact_Solver_Long_Term+0x3C");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, Materials) == 0x40, "IVP_Impact_Solver_Long_Term+0x40");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, LastImpactTimeStep) == 0x58, "IVP_Impact_Solver_Long_Term+0x58");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, TangentAxis0) == 0x78, "IVP_Impact_Solver_Long_Term+0x78");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, TangentAxis1) == 0x88, "IVP_Impact_Solver_Long_Term+0x88");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, CoordOS) == 0x98, "IVP_Impact_Solver_Long_Term+0x98");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, EffectiveMassDenomRecip) == 0x68, "IVP_Impact_Solver_Long_Term+0x68");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, EffectiveMassDenom) == 0x6C, "IVP_Impact_Solver_Long_Term+0x6C");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, ChildCandidate) == 0x70, "IVP_Impact_Solver_Long_Term+0x70");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, CoordOSCrossNormal) == 0xB8, "IVP_Impact_Solver_Long_Term+0xB8");
// Upper bound: retail allocates exactly 0xD8 bytes for this class
// (Common.dll.txt:0635a1f9, `PUSH 0xd8` into FUN_06347380). A field declared
// PAST the end of the retail object is a real, shipped-before bug class, so the
// size is pinned from both sides now that the trailing span is named.
static_assert(sizeof(IVP_Impact_Solver_Long_Term) <= 0xE0, "IVP_Impact_Solver_Long_Term must fit retail's 0xD8-byte allocation (rounded up to this class's own 16-byte alignment)");

// Complete IVP_Impact_Solver context structure (size ~0x110 = 272 bytes)
class alignas(16) IVP_Impact_Solver
{
public:
    // +0x00 (cmn-ivp3f 2026-10-09, renamed from EffectiveInvMass): a SPEED, not a
    // mass. Written once by DoImpactSystem (063578eb-06357902) as
    // float((G_IVP_MindistSettings.min_vertical_speed_at_collision + RescueSpeedAddon) * 1.2f); read by
    // PushContactPoint (06357564: Needed = RescueSpeed - RelativeSpeed) and by
    // FinalizeImpact (063581e8: RescueSpeed * -0.8333333f).
    float             RescueSpeed;            // +0x00
    // +0x04/+0x08: D89 pass (2026-09-09) fully re-derived these (formerly
    // Pad04/Pad08) by tracing FUN_06358560 (decomp-tracker item 3274) and
    // FUN_06346790 (item 2835, now IVP_Core::calc_effective_mass_along_direction,
    // see ivp_core.cpp) instruction-by-instruction. FUN_06358560 loops over
    // both cores (Common.dll.txt:440524-440553, `LEA ESI,[EDI+0xf8]` down to
    // `ESI-0x4`), rotates the caller-supplied world-space normal into each
    // core's object space via IVP_U_Matrix3::vimult (FUN_06366010, confirmed
    // item, == TransformWSToOS below), then calls
    // Cores[N]->calc_effective_mass_along_direction(ContactCoordsOS[N],
    // &NormalOS, &NormalWS) and stores the result here: EffectiveMass1
    // (+0x08) on the Cores[1]/ContactCoordsOS[1] iteration, EffectiveMass0
    // (+0x04) on the Cores[0]/ContactCoordsOS[0] iteration
    // (Common.dll.txt:063585b5, `FSTP [ESI-0xf0]` resolves to +0x08 then
    // +0x04 as ESI walks down by 4 each iteration). After the loop, a
    // kinematic-body override (Common.dll.txt:063585f3-0635861e) rescales
    // whichever field belongs to a `Flags&0xC != 0` core using the OTHER
    // (already-computed, possibly-already-overridden) core's value times
    // DAT_063a5670 (confirmed == 100000.0f) -- a stiff-immovable-body
    // approximation. FUN_06358630 (item 3275) then reads both as
    // `2 * EffectiveMass0 * EffectiveMass1 / (EffectiveMass0 +
    // EffectiveMass1)` -- the classic two-body reduced-mass formula, doubled
    // -- confirming these are per-core EFFECTIVE MASSES (impulse/velocity),
    // not inverse masses, despite the class's other InvMass-named fields.
    // (An earlier clean-room CalcEffectiveMass method stored its two scalars
    // into Speed0.y/Speed0.z instead of here, clobbering a real accumulator;
    // it was deleted 2026-09-17 when DoImpactSystem was rewritten around the
    // real PopulateEffectiveMasses/CalcImpulseNeeded2 pair.)
    float             EffectiveMass0;         // +0x04: per-core effective mass, Cores[0]/ContactCoordsOS[0]
    float             EffectiveMass1;         // +0x08: per-core effective mass, Cores[1]/ContactCoordsOS[1]
    // +0x0C (cmn-ivp3f 2026-10-09, was `float EnergyFactor`): an int flag. Its only
    // writer is DoImpactSystem -- `MOV [ESI+0xc],EAX` with the raw second stack
    // argument (063578f1) and `MOV [ESI+0xc],EBP` (EBP == 0) past ten impacts
    // (06357abe) -- and its only reader is FinalizeImpact, `MOV EAX,[ESI+0xc];
    // TEST EAX,EAX; JZ` (063580a9): zero forces both cores to be committed
    // immediately, non-zero allows the deferred commit of the heavier core.
    int32_t           bAllowDeferredCommit;   // +0x0C
    // +0x10..+0x1B (cmn-ivp3g 2026-10-09, was `Vector ContactNormalWS`, a name
    // nothing supported): NO READER in retail. The only access anywhere is
    // DoImpactSystem's `MOV [ESI+0x18],EBP` (EBP == 0, 06357a0f). The solver
    // object is the stack local local_110 of FUN_06358d60; its address goes
    // only to FUN_06358c00 (ECX unused), FUN_06359380 (spilled to the
    // write-only local_38) and FUN_063578a0, whose callees never touch
    // +0x10/+0x14/+0x18 (grep of 06356f30-06358680 and 06358c00-063599xx).
    int32_t           Unread_0x10[3];         // +0x10..+0x1B: dead slot, [2] zeroed by DoImpactSystem
    const IVP_U_Matrix* MWorldFCore[2];       // +0x1C, +0x20: Core orientation matrices
    Vector            RotSpeed0;              // +0x24: Core 0 angular velocity
    uint8_t           _pad30[0x04];           // +0x30..0x33: restores the real 0x10-byte Vector-slot stride (FUN_06357340/FUN_06358070, decomp-tracker items 3267/3270, both address RotSpeed{index}/Speed{index} via `this+index*0x10`) -- this header previously packed RotSpeed0/RotSpeed1/Speed0/Speed1 back-to-back with no gap, which does not match retail
    Vector            RotSpeed1;              // +0x34: Core 1 angular velocity
    uint8_t           _pad40[0x04];           // +0x40..0x43: see _pad30 above
    Vector            Speed0;                 // +0x44: Core 0 linear velocity
    uint8_t           _pad50[0x04];           // +0x50..0x53: see _pad30 above
    Vector            Speed1;                 // +0x54: Core 1 linear velocity
    uint8_t           _pad60[0x04];           // +0x60..0x63: see _pad30 above -- closes the gap before AngularDelta0Scratch (+0x64)
    // +0x60..+0x73 (0x14 bytes): D89 pass (2026-09-09) traced FUN_06358350
    // (decomp-tracker item 3273, the true retail StepImpulse-equivalent,
    // ported below as StepImpulse2) writing a transient calc_impulse_deltas
    // angular-delta OUTPUT to `this+0x64` (a 12-byte Vector) for its Cores[0]
    // branch. D89 assumed this was pure scratch (immediately consumed into
    // RotSpeed0, "never read again"), but that was only true WITHIN
    // StepImpulse2's own body: D93 pass (2026-09-09), while tracing
    // FUN_06357340 (UndoLastStepImpulse, item 3267), found it reads this
    // SAME field back afterward to roll back the most recent StepImpulse2
    // application (Common.dll.txt:0635734c-0635736c) -- so it IS a real
    // persistent field after all, just one whose only documented purpose is
    // "the last angular delta StepImpulse2 applied to Cores[0]". Declared
    // below; StepImpulse2 was corrected this pass to write through it
    // instead of using a local temporary (see ivp_impact_solver.cpp). The
    // remaining +0x70..+0x73 bytes before RotDelta0 are still unaccounted
    // for.
    Vector            AngularDelta0Scratch;   // +0x64: last StepImpulse2 angular delta applied to Cores[0], see UndoLastStepImpulse/item 3267
    uint8_t           _pad70[0x04];           // +0x70..+0x73: still unconfirmed
    Vector            RotDelta0;              // +0x74: WARNING (D89, 2026-09-09; reconfirmed D93): FUN_06358350/
                                                // StepImpulse2's Cores[1] branch writes its calc_impulse_deltas
                                                // ANGULAR-delta output here (not Cores[0]'s, despite the "0" suffix).
                                                // D93 pass (2026-09-09): FUN_06357340 (UndoLastStepImpulse, item 3267)
                                                // reads this field back to undo the most recent StepImpulse2 call on
                                                // Cores[1] (Common.dll.txt:063573a6-063573c6) -- so, like
                                                // AngularDelta0Scratch above, this IS a real persistent "last applied
                                                // delta" field, not pure within-function scratch. StepImpulse2 was
                                                // corrected this pass to write through it directly. CLOSED 2026-09-17
                                                // (D-ivp-doimpact pass): item 3269's own body has now been traced
                                                // end-to-end, and the ONLY other readers/writers of these four delta
                                                // fields in the whole impact cluster are StepImpulse2 and
                                                // UndoLastStepImpulse. There is no further call site where the
                                                // "Core 0" suffix could be rehabilitated -- the mismatched naming is
                                                // simply wrong and kept only to avoid churning every citation that
                                                // references it.
    uint8_t           _pad80[0x04];           // +0x80..0x83: same 0x10-byte Vector-slot stride as _pad30 (see class comment above -- FUN_06273bc0's vector-constructor-iterator confirms SpeedDelta0/RotDelta1 as a padded-to-16-byte pair starting at +0x84)
    Vector            SpeedDelta0;            // +0x84: Linear impulse delta Core 0 (StepImpulse2's Cores[0] linear output; also read back by UndoLastStepImpulse/item 3267)
    uint8_t           _pad90[0x04];           // +0x90..0x93: see _pad80 above
    Vector            RotDelta1;              // +0x94: WARNING (D89, 2026-09-09; reconfirmed D93): FUN_06358350/
                                                // StepImpulse2's Cores[1] branch writes its calc_impulse_deltas
                                                // LINEAR-delta output here (NOT angular, despite the "Rot" name). D93
                                                // pass (2026-09-09): FUN_06357340 (UndoLastStepImpulse, item 3267)
                                                // reads it back too (Common.dll.txt:063573c9-063573ec) -- same
                                                // "real persistent last-applied-delta field" correction as RotDelta0
                                                // above; StepImpulse2 now writes through it directly.
    uint8_t           _padA0[0x04];           // +0xA0..0xA3: see _pad30/_pad80 above
    Vector            SpeedDelta1;            // +0xA4: Linear impulse delta Core 1
    uint8_t           _padB0[0x04];           // +0xB0..0xB3: see _pad30/_pad80 above
    Vector            PushDirection;          // +0xB4: Normalized impulse push direction
    uint8_t           _padC0[0x04];           // +0xC0..0xC3: see _pad30/_pad80 above
    Vector            FrictionImpulse;        // +0xC4: Accumulated friction impulse
    // +0xD0..+0xEB (0x1C bytes): D89 pass (2026-09-09) traced FUN_06356fb0
    // (item 3264) and FUN_06357120 (item 3265) partway, finding four new
    // fields in this gap. D93 pass (2026-09-09) completed FUN_06357120's
    // trace (the previously-unresolved asin/cone-blend algebra, resuming
    // exactly at D89's documented Common.dll.txt:063571c0 stop point) and
    // ported both functions fully -- see ivp_impact_solver.cpp for the
    // complete writeups. All four fields below are now declared with real
    // (if still provisional) names; their WRITER is still not identified
    // (not FUN_06356fb0/06357120 themselves -- both only read them -- and
    // D93's full trace of FUN_06357340/06358070, items 3267/3270, found
    // neither of THOSE write them either, so this remains open for a future
    // lane).
    uint8_t           _padD0[0x04];           // +0xD0..+0xD3: still unconfirmed
    int32_t           UseFrictionConeBlend;   // +0xD4: nonzero -> FUN_06356fb0 (UpdateFrictionPushDirection) calls ClampPushDirectionToFrictionCone instead of its plain Gram-Schmidt blend
    float             ConeBlendScale;         // +0xD8: FUN_06357120's elliptical-cone secondary-axis scale (see ClampPushDirectionToFrictionCone)
    // Dotted against the Gram-Schmidt tangent inside FUN_06357120
    // (Common.dll.txt:06357173-06357193) before the first asin() call --
    // name is tentative, real identity not derived beyond "some per-contact
    // reference friction axis in object space".
    Vector            FrictionReferenceAxis;  // +0xDC: see ClampPushDirectionToFrictionCone
    uint8_t           _padE8[0x04];           // +0xE8..+0xEB: still unconfirmed
    IVP_Core*         Cores[2];               // +0xEC, +0xF0: Participating cores
    Vector*           ContactCoordsOS[2];     // +0xF4, +0xF8: Local contact coordinates
    float             Restitution;            // +0xFC: Coefficient of restitution (0.0 to 1.0)
    // +0x100/+0x104 (2 floats, confirmed by D86+D89, declared by D93): read
    // by FUN_06356fb0/UpdateFrictionPushDirection, FUN_06357120/
    // ClampPushDirectionToFrictionCone, AND FUN_06358560/PopulateEffectiveMasses's
    // sibling FUN_06358630 as friction-cone blend coefficients -- see
    // UpdateFrictionPushDirection's trace comment in ivp_impact_solver.cpp
    // for the exact formula (`Tangent*TangentCoeff - NormalVectorWS*NormalCoeff`).
    float             NormalCoeff;            // +0x100
    float             TangentCoeff;           // +0x104
    Vector*           NormalVectorWS;         // +0x108: Pointer to World Space normal
    Vector*           FrictionVectorWS;       // +0x10C: Pointer to World Space friction axis

    void PushContactPoint(const Vector* PushVector, int32_t bNormalDirection);

    // Retail FUN_063578a0 (Common.dll.txt:439371-440021, decomp-tracker item
    // 3269, 593 insns) -- the impact system's entry point, called once per
    // impact from IVP_Impact_Solver_Long_Term::DoImpact (FUN_06358d60, Common.dll.txt:06358ed0).
    // REWRITTEN end-to-end 2026-09-17 (D-ivp-doimpact pass, lane
    // claude:common-ivp) as an instruction-by-instruction port; the
    // CalcEffectiveMass / CalcImpulseNeeded / StepImpulse / CommitImpact
    // clean-room cluster that used to be declared alongside it has been
    // DELETED (each had a disasm-traced replacement below). See
    // ivp_impact_solver.cpp for the full control-flow writeup and the
    // per-line address citations. `bAllowDeferred` is retail's second stack
    // parameter, copied raw into bAllowDeferredCommit (+0x0C); the only call
    // site passes 1. `RescueSpeedAddon` is the fourth.
    void DoImpactSystem(IVP_Core* CoresOut[2], int32_t bAllowDeferred, int32_t ImpactCounter, float RescueSpeedAddon);

    // Retail FUN_06356f30 (decomp-tracker item 3263, ported by D86 2026-09-09).
    // Recomputes the current relative world-space velocity at the contact
    // point from this solver's own working-state Speed0/RotSpeed0/Speed1/
    // RotSpeed1 accumulators and stores it into SpeedDelta1. Called 3x by
    // retail's real DoImpactSystem (FUN_063578a0, item 3269), and WIRED IN as
    // of 2026-09-17 (D-ivp-doimpact pass): once before the restitution loop,
    // once per loop iteration, and once after the unit-impulse probe.
    void UpdateRelativeSpeed();

    // Retail FUN_06358560 (decomp-tracker item 3274, ported by D89
    // 2026-09-09). Populates EffectiveMass0/EffectiveMass1 (+0x04/+0x08,
    // see field comments above) from the given world-space normal, one
    // IVP_Core::calc_effective_mass_along_direction call per core, then
    // applies the kinematic-body override. See ivp_impact_solver.cpp for
    // the full trace writeup.
    void PopulateEffectiveMasses(const Vector* NormalWS);

    // Retail FUN_06358630 (decomp-tracker item 3275, identified by D86,
    // ported by D89 2026-09-09). The TRUE retail CalcImpulseNeeded
    // equivalent: calls PopulateEffectiveMasses(NormalVectorWS) then returns
    // the two-body reduced-mass impulse `-(NormalVectorWS|SpeedDelta1) * 2 *
    // EffectiveMass0 * EffectiveMass1 / (EffectiveMass0+EffectiveMass1)`.
    // WIRED IN as of 2026-09-17 (D-ivp-doimpact pass): DoImpactSystem calls it
    // exactly once, and scales the result by 0.1f to get its per-iteration
    // impulse step (Common.dll.txt:06357a43-06357a54).
    // Returns double: retail leaves the result unrounded in ST0 (no FSTP dword
    // before the RET at 0635867c); the caller rounds after its *0.1f.
    double CalcImpulseNeeded2();

    // Retail FUN_06358350 (Common.dll.txt:440305-440484, decomp-tracker item
    // 3273, ported by D89 2026-09-09, corrected by D93 2026-09-09). The TRUE
    // retail StepImpulse equivalent -- applies an impulse of ImpulseMagnitude
    // along PushDirection to each dynamic core via calc_impulse_deltas (full
    // lever-arm + MWorldFCore rotation), unlike the simplified StepImpulse
    // below (object-space cross product only, no rotation -- the same class
    // of bug D82 fixed in PushContactPoint). D93 CORRECTION: now writes its
    // calc_impulse_deltas outputs through the real AngularDelta0Scratch(+0x64)/
    // SpeedDelta0(+0x84)/RotDelta0(+0x74)/RotDelta1(+0x94) fields instead of
    // local temporaries -- FUN_06357340/UndoLastStepImpulse (item 3267) reads
    // these back afterward to roll back the most recent StepImpulse2 call, so
    // they are NOT pure within-function scratch as D89 assumed. See
    // ivp_impact_solver.cpp for the full trace writeup.
    void StepImpulse2(float ImpulseMagnitude);

    // Retail FUN_06357340 (Common.dll.txt:438929-438989, decomp-tracker item
    // 3267, 53 insns). Fully traced by D93 (2026-09-09). __thiscall(this=
    // IVP_Impact_Solver*), no stack params. Single call site in retail's real
    // DoImpactSystem (item 3269, Common.dll.txt:06357c5f). For each dynamic
    // core, subtracts back out the delta StepImpulse2 most recently applied:
    //   RotSpeed0 -= AngularDelta0Scratch; Speed0 -= SpeedDelta0;
    //   RotSpeed1 -= RotDelta0;            Speed1 -= RotDelta1;
    // i.e. "undo the last StepImpulse2 step" -- presumably part of a
    // try-a-step/evaluate/rollback-if-needed pattern in DoImpactSystem's real
    // restitution loop (not yet re-derived, see item 3269's blocker_reason).
    // This function is what proved AngularDelta0Scratch/RotDelta0/RotDelta1/
    // SpeedDelta0 are real persistent fields, not pure scratch -- see those
    // fields' comments above and StepImpulse2's correction note.
    void UndoLastStepImpulse();

    // Retail FUN_06356fb0 (Common.dll.txt:438610-438737, decomp-tracker item
    // 3264, 114 insns). Fully traced by D89 (2026-09-09, all 4 branches),
    // completed/ported by D93 (2026-09-09) once item 3265's dependency
    // resolved. __thiscall(this=IVP_Impact_Solver*), no stack params. Called
    // 2x from retail's real DoImpactSystem (item 3269, Common.dll.txt:
    // 06357ac8 and 06357b89) to recompute this solver's friction-tangent
    // PushDirection for the current iteration -- NOT an impulse-applying
    // function itself (touches no Speed/RotSpeed accumulator). See
    // ivp_impact_solver.cpp for the full trace writeup.
    void UpdateFrictionPushDirection();

    // Retail FUN_06357120 (Common.dll.txt:438739-438860, decomp-tracker item
    // 3265, 109 insns). Two-thirds traced by D89 (2026-09-09); the remaining
    // ~90-instruction asin/elliptical-cone-blend middle section (D89's
    // documented resume point, Common.dll.txt:063571c0) completed by D93
    // (2026-09-09). __thiscall(this=IVP_Impact_Solver*), 1 stack float param
    // (Dot = NormalVectorWS . normalize(SpeedDelta1), passed by
    // UpdateFrictionPushDirection's branch 4a when UseFrictionConeBlend != 0).
    // See ivp_impact_solver.cpp for the complete trace writeup (Gram-Schmidt
    // tangent, elliptical friction-cone radius test via two asin() calls and
    // the same 4th-order Taylor cosine CalcFrictionDampingFactors uses, early
    // exit when already inside the cone, else rewrite PushDirection to the
    // exact cone-boundary direction).
    void ClampPushDirectionToFrictionCone(float Dot);

    // Retail FUN_063582f0 (Common.dll.txt:440268-440293, decomp-tracker item
    // 3272, 22 insns). Fully traced by D93 (2026-09-09). Zeroes
    // Cores[0]/Cores[1]->RotSpeedChange and ->SpeedChange. Called at the
    // start of every one of FinalizeImpact's three exit paths.
    void ClearPendingCoreChanges();

    // Retail FUN_063572b0 (Common.dll.txt:438870-438917, decomp-tracker item
    // 3266, 42 insns). Traced by D93 (2026-09-09). __thiscall(this=
    // IVP_Impact_Solver*), 1 stack int32_t param (CoreIndex, 0 or 1).
    // Overwrites Cores[CoreIndex]->Speed/RotSpeed directly with this
    // solver's working Speed{CoreIndex}/RotSpeed{CoreIndex} -- the
    // "immediate" commit path FinalizeImpact uses for the non-deferred core.
    // GAP CLOSED 2026-09-17 (D-ivp-anomaly pass): retail's tail asks
    // Environment->anomaly_manager (+0x20, NOT controller_manager as this note
    // used to claim) whether a core that has exceeded
    // anomaly_limits->MaxCollisionsPerPsi may be frozen, and splices the
    // answer into MovementState bits 8..15. Both sub-managers are now real
    // modeled classes (ivp_anomaly_manager.h), so all 42 instructions are
    // ported -- see ivp_impact_solver.cpp for the register-by-register
    // citation.
    void CommitCoreVelocityImmediate(int32_t CoreIndex);

    // Retail FUN_06358270 (Common.dll.txt:440224-440255, decomp-tracker item
    // 3271, 27 insns). Fully traced by D93 (2026-09-09). __thiscall(this=
    // IVP_Impact_Solver*), 1 stack int32_t param (CoreIndex). Records
    // Cores[CoreIndex]->SpeedChange = Speed{CoreIndex} - Cores[CoreIndex]->Speed
    // (and the RotSpeedChange/RotSpeed equivalent) WITHOUT touching
    // Cores[CoreIndex]->Speed/RotSpeed themselves -- the "deferred" commit
    // path FinalizeImpact uses for the winning (larger effective-mass) core.
    // This is what disproved the old clean-room CommitImpact (deleted
    // 2026-09-17), which accumulated `SpeedChange += SpeedDelta0` -- retail
    // OVERWRITES SpeedChange/RotSpeedChange here.
    void CommitCoreVelocityDeferred(int32_t CoreIndex);

    // Retail FUN_06358070 (Common.dll.txt:440038-440208, decomp-tracker item
    // 3270, 147 insns). Fully traced by D93 (2026-09-09) together with its 4
    // callees (items 2830, 3266, 3271, 3272, all documented in
    // ivp_impact_solver.cpp). __thiscall(this=IVP_Impact_Solver*), 1 stack
    // param (IVP_Core* CoresOut[2]). Single call site in retail's real
    // DoImpactSystem (item 3269, Common.dll.txt:06357ef1) -- the
    // finalization step deciding, per impact, whether to commit both cores'
    // resolved velocities immediately or defer the winning (larger
    // effective-mass) core's update for the main simulation step to
    // integrate later. GAP CLOSED 2026-09-17 (D-ivp-anomaly pass): the one
    // remaining instruction was a plain `INC dword [Environment+0x5c]`, not
    // the sub-manager chain this note used to describe; all 147 instructions
    // are now disasm-exact.
    void FinalizeImpact(IVP_Core* CoresOut[2]);
};
static_assert(sizeof(IVP_Impact_Solver) == 0x110, "IVP_Impact_Solver size must be 0x110 (272 bytes), per the class's own top-of-file comment");
static_assert(offsetof(IVP_Impact_Solver, EffectiveMass0) == 0x04, "IVP_Impact_Solver+0x04");
static_assert(offsetof(IVP_Impact_Solver, EffectiveMass1) == 0x08, "IVP_Impact_Solver+0x08");
static_assert(offsetof(IVP_Impact_Solver, bAllowDeferredCommit) == 0x0C, "IVP_Impact_Solver+0x0C");
static_assert(offsetof(IVP_Impact_Solver, Unread_0x10) == 0x10, "IVP_Impact_Solver+0x10");
static_assert(offsetof(IVP_Impact_Solver, MWorldFCore) == 0x1C, "IVP_Impact_Solver+0x1C");
static_assert(offsetof(IVP_Impact_Solver, RotSpeed0) == 0x24, "IVP_Impact_Solver+0x24");
static_assert(offsetof(IVP_Impact_Solver, RotSpeed1) == 0x34, "IVP_Impact_Solver+0x34");
static_assert(offsetof(IVP_Impact_Solver, Speed0) == 0x44, "IVP_Impact_Solver+0x44");
static_assert(offsetof(IVP_Impact_Solver, Speed1) == 0x54, "IVP_Impact_Solver+0x54");
static_assert(offsetof(IVP_Impact_Solver, AngularDelta0Scratch) == 0x64, "IVP_Impact_Solver+0x64");
static_assert(offsetof(IVP_Impact_Solver, RotDelta0) == 0x74, "IVP_Impact_Solver+0x74");
static_assert(offsetof(IVP_Impact_Solver, SpeedDelta0) == 0x84, "IVP_Impact_Solver+0x84");
static_assert(offsetof(IVP_Impact_Solver, RotDelta1) == 0x94, "IVP_Impact_Solver+0x94");
static_assert(offsetof(IVP_Impact_Solver, SpeedDelta1) == 0xA4, "IVP_Impact_Solver+0xA4");
static_assert(offsetof(IVP_Impact_Solver, PushDirection) == 0xB4, "IVP_Impact_Solver+0xB4");
static_assert(offsetof(IVP_Impact_Solver, FrictionImpulse) == 0xC4, "IVP_Impact_Solver+0xC4");
static_assert(offsetof(IVP_Impact_Solver, UseFrictionConeBlend) == 0xD4, "IVP_Impact_Solver+0xD4");
static_assert(offsetof(IVP_Impact_Solver, ConeBlendScale) == 0xD8, "IVP_Impact_Solver+0xD8");
static_assert(offsetof(IVP_Impact_Solver, FrictionReferenceAxis) == 0xDC, "IVP_Impact_Solver+0xDC");
static_assert(offsetof(IVP_Impact_Solver, Cores) == 0xEC, "IVP_Impact_Solver+0xEC");
static_assert(offsetof(IVP_Impact_Solver, ContactCoordsOS) == 0xF4, "IVP_Impact_Solver+0xF4");
static_assert(offsetof(IVP_Impact_Solver, Restitution) == 0xFC, "IVP_Impact_Solver+0xFC");
static_assert(offsetof(IVP_Impact_Solver, NormalCoeff) == 0x100, "IVP_Impact_Solver+0x100");
static_assert(offsetof(IVP_Impact_Solver, TangentCoeff) == 0x104, "IVP_Impact_Solver+0x104");
static_assert(offsetof(IVP_Impact_Solver, NormalVectorWS) == 0x108, "IVP_Impact_Solver+0x108");
static_assert(offsetof(IVP_Impact_Solver, FrictionVectorWS) == 0x10C, "IVP_Impact_Solver+0x10C");

// ---------------------------------------------------------------------------
// Complex-ledge mindist spawn limiter
// ---------------------------------------------------------------------------
// Retail: Common.dll.c / .txt 0x06362620-0x063632b0 plus the big allocator at
// 0x0634e0b0 and the tiny base ctor/dtor pair at 0x0634dd10/0x0634dd60
// (decomp-tracker Common.dll agent "claude-common-mindist-limiter", 2026-09-06).
//
// This is a SEPARATE pair of classes from IVP_Impact_Solver_Long_Term above -- different
// sizes (0x80 / 0x94 bytes vs IVP_Impact_Solver_Long_Term's at-least-0xB8, corrected by D110
// 2026-09-09 -- see the class-level comment above IVP_Impact_Solver_Long_Term), different field layout,
// and reconstructed from a different part of the retail binary (the
// PhySys::CalcClosestPointOnSynapse cluster around 0x0628xxxx that produced
// IVP_Impact_Solver_Long_Term above never touches these addresses). Do not merge them.
//
// What retail is doing: when two (potentially complex / multi-ledge) real
// objects are tested for collision, a single vertex/edge/face synapse pair
// is not always enough -- a non-convex object built from many convex
// IVP_Compact_Ledge sub-hulls can have several simultaneously-close ledge
// pairs. Retail handles this with two sibling classes sharing one field
// layout for the "one candidate pair" fields (offsets < 0x80, inherited):
//
//   IVP_Ledge_Mindist   (ctor FUN_0634dd10, dtor FUN_0634dd60, 0x80 bytes)
//     A single candidate ledge/feature pair. Holds two embedded synapse-like
//     records (offsets +0x18 / +0x34, 0x1c bytes each -- same size as
//     IVP_Synapse_Real above, and retail's FUN_0634e090 writes fields at
//     relative +0x14 / +0x1a within them that line up with that struct's
//     EdgeOrVertex / FeatureType, though the rest of each 0x1c-byte span
//     was not independently re-derived here), the two owning IVP_Real_Object
//     pointers (+0x28 / +0x44), the two IVP_Compact_Edge* candidate features
//     (+0x2c / +0x48), the combined contact radius (+0x50) and a packed
//     state bitfield (+0x14).
//
//   IVP_Complex_Mindist : IVP_Ledge_Mindist  (ctor FUN_06362ab0, size 0x94)
//     Adds, starting exactly at +0x80 where the base class ends (the
//     "surface_manager also lives at +0x80" note on IVP_Real_Object is a
//     numeric coincidence between two unrelated classes, not a shared
//     field): a second vtable-bearing sub-object (retail PTR_LAB_063a5868,
//     decomp-tracker vtable id 159 -- GetCount()/AdjustCount()/RemoveChildRecord(),
//     modeled here as overrides of the IVP_Mindist_Count_Chain interface
//     both classes share rather than a second C++ base/vtable) and a small
//     growable array of child mindist pointers (+0x8a count / +0x8c pointer,
//     small-buffer-optimized into the object itself while there is at most
//     one child) used to track recursively-spawned child IVP_Ledge_Mindist /
//     IVP_Complex_Mindist instances. Every spawn is gated against
//     PhySys::MAX_SPAWNED_MINDIST_COUNT (retail DAT_0658ab3c, confirmed
//     elsewhere to be a runtime cache of that exact ini constant) via
//     GetCount()/AdjustCount(), which delegate up a chain of nested managers
//     (FUN_06362ec0's `*(this-0x78)` hop, i.e. IVP_Ledge_Mindist::CountChainParent)
//     so one shared cap applies across the whole recursive tree, not per-node.
//
// FUN_0634e0b0 (the spawn path's allocator, the library's
// create_exact_mindists) is IVP_Create_Exact_Mindists in
// src/IVP/ivp_collision/ivp_mindist_create.h; IVP_Complex_Mindist::
// SpawnOrDeferChild (06362c70) calls it with the WinnerFlag-selected ledges.
//
// Deep IVP internals this cluster reaches into but which remain unported
// elsewhere in the tree (IVP_Real_Object::environment chain, the compound
// core / anomaly-limiter helpers FUN_0634fec0/ffa0/06350020/0350350, the
// synapse-list linkage FUN_06391ef0/06391dfc, hesse-vector recompute
// FUN_06350250, IVP_Compact_Edge hashing FUN_0634e820/e7f0/e860) are called
// through small stub hooks tagged FULLPORT-DEFERRED[IVP], matching the
// convention already established in common_physys.h.

// The former IVP_Mindist_Count_Chain interface (GetCount/AdjustCount) is gone
// (2026-10-08, cmn-phy5): the "count chain root" is the library's
// IVP_Collision_Delegator (ivp_collision.h) -- slot 2
// change_spawned_mindist_count, slot 3 get_spawned_mindist_count -- reached
// through IVP_Collision::delegator (+0x08).

// D-item3262 pass (2026-09-09): RESOLVED the "grandchild candidate" class
// identity that decomp-tracker items 3287/3289/3293 (and the D104/D107
// survey) had been blocked on for several passes.
// Those items independently derived a still-unidentified class whose
// instances are reached through a "pending queue"/"child" indirection from
// the outer candidate-winner-selection hub (FUN_063591a0/item 3289) and which
// owns a +0x40 back-pointer to IVP_Impact_Solver_Long_Term plus (through that back-pointer)
// reads of IVP_Impact_Solver_Long_Term's own +0x52/+0x54/+0x58/+0x5c fields -- see item
// 3289's blocker_reason for the original "grandchild" framing.
//
// Full trace of FUN_06356bc0 (Common.dll.txt:438218-438543, decomp-tracker
// item 3262, thiscall this=ECX/EDI, 4 stack params) settles this: the
// GRANDCHILD IS THIS CLASS (IVP_Ledge_Mindist), not a separate/undiscovered
// one. Evidence, all from FUN_06356bc0's own disasm plus its 3 call sites:
//   - `this` (EDI) is read at +0x28/+0x44 via a tagged 2-way select (bit 0x100
//     of StateFlags at +0x14 chooses the "primary" side; SHR 8/AND 3 then a
//     x7 multiply-by-LEA/SUB idiom computes a 0x1c=28-byte-stride index into
//     the +0x28 base -- i.e. index 0 -> +0x28, index 1 -> +0x44) and each
//     selected pointer is dereferenced at +0x9c
//     (`MOV EBP,[EBX+0x9c]` / `TEST byte ptr [EBP],0xc`) -- exactly
//     `IVP_Real_Object::original_core` (ivp_real_object.h:48) then
//     `IVP_Core::Flags` (ivp_core.h:27, offsetof==0, confirmed by
//     static_assert) low-byte-tested with the same `Flags&0xC` kinematic-body
//     mask used throughout this cluster. This independently confirms +0x28/
//     +0x44 hold `IVP_Real_Object*` -- i.e. ObjectA/ObjectB below, at their
//     already-declared offsets, no new class needed.
//   - `this+0x40` is read and passed as FUN_06358c50's param_1
//     (Common.dll.txt:06356c83-06356c89, :06356ca2-06356ca8), which the
//     IVP_Impact_Solver_Long_Term class-level comment above independently establishes is an
//     `IVP_Impact_Solver_Long_Term*` being populated by FUN_06358c50. See the new `Owner`
//     field below for the full citation, including a SECOND independent
//     confirmation via item 3293's own trace of FUN_06359560.
//   - `this` is never dereferenced above +0x44 anywhere in FUN_06356bc0's
//     body (every other reference either passes EDI opaquely as an argument
//     to sibling helpers -- FUN_0635a1e0, FUN_0634cfa0, FUN_0634b9a0,
//     FUN_0634b900, FUN_063556b0, FUN_063562d0 -- or is unrelated stack/
//     register scratch), consistent with `this` fitting inside the base
//     class's retail 0x80-byte size.
//     CORRECTED (D-item3004 pass, 2026-09-10): this claim conflates "EDI" with
//     "this" -- see the correction block below (right before "Base: a single
//     candidate ledge/feature pair") for the found EDI-reassignment at
//     Common.dll.txt:06356c4f. The "sibling helper" calls listed here (all
//     past that reassignment) pass a DIFFERENT object (a found-or-created
//     "pair record"), not `this`/Ctx opaquely.
//   - All 3 call sites (Common.dll.txt:0634fd40, 06356499, 063586ec) pass a
//     pointer whose own +0x28 (ObjectA) and/or +0x14 (StateFlags)/+0x54
//     fields are read by the CALLER immediately before/after the call,
//     matching this class's own already-declared fields at those exact
//     offsets. The 063586ec site in particular (inside FUN_06358680, itself
//     called from FUN_06358cb0 aka IVP_Ledge_Mindist::do_impact in
//     ivp_mindist_limiter.cpp) traces back through an explicit
//     `MOV EDX,[this_of_06358cb0]; ...; PUSH EDX` chain to FUN_06358cb0's OWN
//     `this` -- which that function's own entry instruction
//     (`MOV EAX,[ECX+0x28]`) already treats as this exact class. The inline
//     comment in ivp_mindist_limiter.cpp/item 3276's notes calling this
//     caller's `this` "Mindist" was informal shorthand for "the Ledge/Complex
//     Mindist instance", not literally the separate IVP_Impact_Solver_Long_Term class above
//     -- clarified there this pass.
//   - FUN_06356bc0 does NOT construct a new object: it returns `this`
//     unchanged (`MOV EAX,EDI` immediately before the epilogue,
//     Common.dll.txt:06356f17) after mutating it in place. This is why items
//     3276/3293 independently described its return value as an opaque "Ctx"
//     -- Ctx IS the same IVP_Ledge_Mindist instance the function was called
//     on, not a new "grandchild" object.
//     CORRECTED (D-item3004 pass, 2026-09-10): FALSE. `MOV EAX,EDI` at
//     06356f17 returns whatever EDI currently holds, but EDI was reassigned
//     at Common.dll.txt:06356c4f (`MOV EDI,EAX`, right after `CALL
//     FUN_0635a950`) BEFORE every path that can reach this epilogue -- so the
//     "unchanged" claim is true only relative to 06356c4f, not relative to
//     function entry. FUN_06356bc0's real return value is FUN_0635a950's own
//     return value: an existing-or-newly-`operator_new(0x74)`-allocated "pair
//     record" object keyed by (Ctx->ObjectA, Ctx->ObjectB), NOT Ctx itself.
//     Full trace in the correction block below (right before "Base: a single
//     candidate ledge/feature pair"). The class-identity conclusion for
//     ObjectA/ObjectB/StateFlags/etc. above (established via the EARLY,
//     pre-06356c4f part of the function) is unaffected; only claims about
//     what EDI/"this" means AFTER 06356c4f are corrected.
// Item 3262 itself stays `partial` rather than `ready_unverified`: the class
// identity is now solid, but ~110 of 284 instructions still dispatch through
// 10+ unexamined external helpers (FUN_0635a950, FUN_0634cfa0, FUN_0634b0e0,
// FUN_06347c40/item 3290, FUN_0634b9a0, FUN_0634b900, FUN_063555b0,
// FUN_06347c80, FUN_0634b9d0, FUN_0634b600, FUN_063556b0, FUN_063562d0,
// FUN_06348c60, FUN_06348d80, FUN_06391d5a) plus 3 allocation sites building
// what looks like a small list/tree node -- porting the full body without
// first resolving those would mean guessing their semantics, so it is left
// unported; see item 3262's own notes for the complete per-branch trace.
//
// D-item3004 pass (2026-09-10): RESOLVED the D136 blocker-list entry that
// flagged an apparent conflict between FUN_0634b9a0 (item 3004) writing to
// its "node" argument's own +0x0/+0x4/+0x70 and THIS class's own +0x00
// vtable / +0x70 LastDispatchTick declaration. D136's premise was that the
// EDI value FUN_06356bc0 pushes as that node argument (Common.dll.txt:
// 06356d2b/06356d33 and :06356e7c/06356e84) is "PROVABLY FUN_06356bc0's own
// unchanged `this`" because EDI is what the function returns unmodified at
// its epilogue (`MOV EAX,EDI`, Common.dll.txt:06356f17). That premise is
// FALSE: re-reading FUN_06356bc0's full body found a SECOND `MOV EDI,`
// write, at Common.dll.txt:06356c4f (`MOV EDI,EAX`), executed unconditionally
// right after `CALL FUN_0635a950` (Common.dll.txt:06356c4a) -- i.e. EDI is
// reassigned partway through the function, and this reassignment happens on
// EVERY control-flow path that can reach either FUN_0634b9a0 call site (both
// call sites, and the "returns EDI unchanged" epilogue itself, are only
// reachable via LAB_06356c93 onward / LAB_06356f13's 6 XREFs, all of which
// post-date Common.dll.txt:06356c4f -- confirmed by grepping every `MOV
// EDI,` in the function's Common.dll.txt:438218-438543 span: only two exist,
// at 06356bdc (`MOV EDI,ECX`, the real `this`-capture at entry) and 06356c4f).
// So "the value FUN_06356bc0 returns unmodified" and "this function's own
// entry `this`" are NOT the same value after 06356c4f runs -- item 3262's
// "Ctx==this, unchanged" proof only holds for the EARLY part of the function
// body (before 06356c4f), not for anything downstream of it.
//
// What EDI actually holds after 06356c4f: the return value of FUN_0635a950
// (Common.dll.txt:443905-443951), which was called with param_1=Ctx (the
// ORIGINAL EDI, pushed at :06356c41 before the reassignment) and param_2=
// &local_24 (an out-tag). Full trace of FUN_0635a950 shows it is a
// find-or-create lookup keyed by Ctx, NOT an identity function:
//   - It calls FUN_0635a900(Ctx) (:0635a96b), which walks a linked list
//     rooted at `Ctx->ObjectA->+0x28` (ObjectA read via the already-declared
//     +0x28 field) looking for an existing node whose fields match
//     `Ctx->ObjectB` (+0x44), confirming a candidate via FUN_06356810. If
//     found, FUN_0635a950 returns that EXISTING node directly (EAX left
//     untouched from the FUN_0635a900 call, :0635a973-0635a990) with
//     *param_2=0.
//   - If NOT found, FUN_0635a950 allocates a NEW, distinct 0x74-byte object
//     (`PUSH 0x74; CALL operator_new`, :0635a991-0635a998), constructs it via
//     FUN_063539d0(new_obj, Ctx) (:0635a9ac-0635a9ae, this=new_obj,
//     param=Ctx), and returns THAT new object with *param_2=1.
// Either way the returned object -- call it a "pair record", keyed by the
// (ObjectA, ObjectB) pair, distinct from item 2992's "ListManager" class too
// -- is a DIFFERENT, independently-allocated object from Ctx itself. EDI from
// 06356c4f onward (and therefore every "node" argument passed to
// FUN_0634b9a0/FUN_0634b900/FUN_0634b9d0/FUN_063556b0, AND the function's own
// final return value at 06356f17) refers to THIS pair-record object, not to
// the IVP_Ledge_Mindist `this` FUN_06356bc0 was called on.
//
// CONCLUSION (resolves D136, per this task's option (a)): FUN_0634b9a0 does
// NOT write into IVP_Ledge_Mindist's own +0x0/+0x4/+0x70. No conflict with
// this class's current vtable-at-+0x00 / LastDispatchTick-at-+0x70
// declaration exists, and NO layout change is needed here. FUN_0634b9a0's
// node argument, and the "pair record" class it belongs to, remain
// unidentified (still `partial` on item 3004) -- but they are now confirmed
// to be a SEPARATE class from this one.
//
// IMPORTANT FOLLOW-ON, NOT RESOLVED THIS PASS: this same EDI-reassignment
// bug undermines the `Owner` field's own evidence trail further below (+0x40,
// "IVP_Impact_Solver_Long_Term* Owner"). That field was derived from "this+0x40" reads at
// Common.dll.txt:06356c83/06356ca2 (inside FUN_06358c50's param_1 setup) --
// but BOTH of those sites are also past 06356c4f, so they are reads of the
// pair-record object's own +0x40, not Ctx(IVP_Ledge_Mindist)+0x40. The
// `this` dereferenced above +0x44 claim in the class-level comment above
// (further up this file, "this is never dereferenced above +0x44... every
// other reference either passes EDI opaquely as an argument to sibling
// helpers") is subject to the exact same correction: those "opaque EDI
// arguments" are the pair-record object post-06356c4f, not Ctx. The `Owner`
// field declaration below is left UNCHANGED this pass (its own dedicated
// re-verification, using ONLY pre-06356c4f EDI reads or an independent
// source, is real scope -- not guessed at here per this task's "never guess
// a layout" instruction) but should be treated as an OPEN QUESTION by any
// future lane, not settled fact.
//
// D-owner-resolve pass (2026-09-10): RESOLVES the `Owner` OPEN QUESTION above.
// Exhaustive cross-reference sweep (every call/consumer site of the "pair
// record" object FUN_0635a950/FUN_0635a900 find-or-create, plus a full
// instruction-by-instruction re-trace of its constructor FUN_063539d0,
// Common.dll.txt:432911-433038): `Owner` (IVP_Impact_Solver_Long_Term*) does NOT belong on
// IVP_Ledge_Mindist. It lives at +0x40 of the separate 0x74-byte "pair
// record" object -- see the new IVP_Contact_Point struct declared right
// below this comment. Evidence, independent of the D-item3004/3262 reads that
// originally (and correctly, per this resolution) got flagged as suspect:
//   1. FUN_06356bc0 itself (Common.dll.txt:06356c83/06356ca2/06356cb0 -- THREE
//      separate sites, re-traced fresh this pass): after EDI is reassigned at
//      :06356c4f to FUN_0635a950's return (the pair record), all three sites
//      read `EDI->0x40` and feed it as FUN_06358c50's param_1 -- and
//      FUN_06358c50's param_1 is independently, solidly established elsewhere
//      in this file (D110 pass, see FUN_06358c50's own writeup above the
//      IVP_Impact_Solver_Long_Term class) to be an `IVP_Impact_Solver_Long_Term*` it populates (Objects[2] at
//      +0x30/+0x34, etc). This alone means pair-record+0x40 is an IVP_Impact_Solver_Long_Term*.
//   2. INDEPENDENT, STRONGER confirmation via FUN_063595c0 and FUN_06359560
//      (Common.dll.txt:0635956c-063595b8 and 442165-442230, decomp-tracker
//      item 3294-adjacent cluster): FUN_063595c0's own `this` (EBX) is the
//      pair record (confirmed by FUN_06359560 being called with the SAME
//      `this=EBX` a few lines later and reading `[ECX+0x58]`, matching
//      FUN_063595c0's own `FLD float ptr [EBX+0x58]` -- i.e. both read the
//      SAME pair-record-relative +0x58 field, see IVP_Contact_Point's
//      own GapDistance below). FUN_063595c0 computes `ESI = EBX->0x40` (i.e.
//      pair-record->0x40) and THEN performs, through ESI, three separate,
//      independently-confirmed IVP_Impact_Solver_Long_Term field writes:
//        - `ESI->0x54` (StateProcessed, `AND 0xffffff01; OR 1`) --
//          Common.dll.txt:063595ca-063595d5.
//        - `ESI->0x5c = 0x60ad78ec` (a huge sentinel float -- a
//          disqualifying reset of WinnerPriority, IVP_Impact_Solver_Long_Term+0x5C, which
//          FUN_063591a0 compares against a running MINIMUM to pick a winning
//          candidate; setting it to a huge value knocks this candidate out of
//          contention) -- Common.dll.txt:063595e8.
//        - `ESI->0x58` (LastImpactTimeStep, IVP_Impact_Solver_Long_Term+0x58) --
//          `FSTP float ptr [ESI+0x58]`, Common.dll.txt:06359605, receiving
//          FUN_06359560's own return value.
//      THREE independent field-offset matches (+0x54, +0x58, +0x5c), all
//      through the SAME `this->0x40` pointer, all landing exactly on
//      IVP_Impact_Solver_Long_Term's own already-established (D110-confirmed) field layout.
//      This is decisive: pair-record+0x40 is `IVP_Impact_Solver_Long_Term* Owner`, full stop.
//   3. As a side effect, this SAME re-trace independently reconfirms
//      pair-record+0x10/+0x24 == IVP_Real_Object* (FUN_063595c0's own
//      `MOV ECX,[EBX+0x10]; MOV EBP,[ECX+0x18]` dereferences pair-record->0x10
//      as an IVP_Real_Object* and reads its `environment` field at +0x18,
//      the already-established offset) -- matching item 3003's own
//      independent FUN_0634b900 trace of the exact same two offsets.
// CONCLUSION: the `Owner` field is REMOVED from IVP_Ledge_Mindist below (it
// never belonged there -- both of its original citations were post-
// 06356c4f/post-063586f9 reads of the pair-record, not of this class) and
// replaced with a still-unconfirmed padding span at the same offset. See
// IVP_Contact_Point::Owner (+0x40) for the real, now-fully-cited
// declaration; its consumer is do_impact (06358680, ivp_impact.cpp).
//
// ---------------------------------------------------------------------------
// "Pair record": per-(ObjectA,ObjectB) bookkeeping node
// ---------------------------------------------------------------------------
// Retail: allocated via `operator_new(0x74)` + constructed via FUN_063539d0
// (Common.dll.txt:432911-433038), found-or-created by FUN_0635a950
// (Common.dll.txt:443905-443951) which first tries FUN_0635a900
// (Common.dll.txt:443852-443947, a lookup keyed by (ObjectA,ObjectB) identity
// walking the intrusive list rooted at ObjectA->+0x28) before allocating a
// fresh one. This is the object FUN_06356bc0/IVP_Ledge_Mindist's own
// "grandchild"/"Ctx" helper functions (FUN_0634b9a0/item 3004,
// FUN_0634b900/item 3003, FUN_0635a1e0, FUN_06358c50, FUN_0634cfa0,
// FUN_063556b0, FUN_063562d0, FUN_063595c0, FUN_06359560) actually operate on
// -- NOT IVP_Ledge_Mindist itself (see the correction blocks above).
//
// Two embedded intrusive-list nodes (one per side) let a single pair record
// be found by walking EITHER object's own per-object list (Object->+0x28):
// this is exactly the "keyed by ObjectA/ObjectB identity" lookup
// FUN_0635a900 performs, and each node's own self-relative Delta lets that
// walk recover the pair record's base address from just the node pointer
// (retail's usual compact-link idiom, same convention IVP_Ledge_Mindist's own
// Synapse0LinkDelta/Synapse1LinkDelta use).
//
// Per this task's placeholder-field policy: every offset below has a
// disasm-confirmed primitive type/size (either a direct write in the ctor, or
// an independent consumer read); fields whose retail SEMANTIC purpose was not
// independently nailed down keep a Ghidra-style placeholder name (`_fieldNN`)
// rather than a guessed one, with the confirmed construction-time behavior
// (zeroed / flag-twiddled / copied-from-X) documented instead of invented.
// Forward declaration: IVP_ListManager is declared in full further below
// (right after this struct), but IVP_Contact_Point::ListManagerOwner
// (see below) needs the pointer type now. Kept as a fully separate class
// rather than nested, since it is allocated/constructed independently (see
// ivp_mindist_limiter.cpp's IVP_ListManager section).
class IVP_ListManager;

#pragma pack(push, 4)
struct IVP_Contact_Point
{
    // RETYPED (D-listmanager pass, 2026-09-10, from `_pad00[0x08]`). D-item3004
    // (2026-09-10) proved every "node" argument FUN_0634b9a0/FUN_0634b8d0/
    // FUN_0634b900 (decomp-tracker items 3004/3002/3003) operate on IS this
    // exact pair-record object (not IVP_Ledge_Mindist -- see the correction
    // block above the IVP_Contact_Point struct-level comment). Re-tracing
    // FUN_0634b9a0 (Common.dll.txt:0634b9a0-0634b9c1) confirms it writes
    // `node->0x0`/`node->0x4` directly as a plain intrusive-list Next/Prev pair
    // threaded through IVP_ListManager::CtxListHead (+0x20) -- a COMPLETELY
    // SEPARATE list from Next1/Prev1 (+0x08/+0x0C) below, which thread the
    // per-Object1 `mindist_list`. See IVP_ListManager::InsertCtx/RemoveCtx in
    // ivp_mindist_limiter.cpp for the real port of FUN_0634b9a0/FUN_0634b8d0.
    IVP_Contact_Point* ListManagerListNext; // +0x00
    IVP_Contact_Point* ListManagerListPrev; // +0x04

    // Embedded intrusive-list node #1, threaded into Object1->+0x28's list.
    // RETYPED (D-ctx-resolve pass, 2026-09-10, from `IVP_Contact_Point*`)
    // -- these do NOT point at another record's base. Retail's raw list
    // entries are NODE pointers (the embedded Next1/Next2 sub-struct's own
    // address, i.e. record+0x08 or record+0x1C for whichever side a given
    // record was linked under), recovered back to a record base only via the
    // node's own self-relative Delta field (Delta1/Delta2 below) -- see
    // IVP_FindExistingPairRecord/IVP_Contact_Point::IVP_Contact_Point in
    // ivp_mindist_limiter.cpp, the real port of FUN_0635a900/FUN_063539d0,
    // for the walking/insertion code that actually uses this convention.
    void* Next1; // +0x08
    void* Prev1; // +0x0C
    IVP_Real_Object* Object1;       // +0x10: CONFIRMED (ctor FUN_063539d0:06353a05 copies one of Mindist->ObjectA/
                                    // ObjectB here, selected by the same StateFlags-bit-0x100 tag FUN_06356bc0's own
                                    // entry code uses; independently re-read by FUN_0634b900/item 3003 at this exact
                                    // offset, and by FUN_063595c0 which dereferences Object1->+0x18/environment).
    int16_t Delta1;                 // +0x14: self-relative link, base(this+0x8) to record base (always -0x8)
    uint16_t _hash1;                 // +0x16: XOR-combined with ShapeType(side1)'s low byte at construction; exact
                                    // purpose (type tag? hash bucket key?) not independently resolved -- placeholder.
    void* Feature1;                  // +0x18: tagged IVP_Compact_Edge* copy (Mindist->FeatureA/FeatureB, side-1-matched)

    // Embedded intrusive-list node #2 -- exact mirror of node #1, threaded into Object2->+0x28's list instead
    // (Object2 = the OTHER of Mindist->ObjectA/ObjectB, selected via the complementary StateFlags tag).
    // RETYPED (D-ctx-resolve pass, 2026-09-10) -- see Next1/Prev1's comment above.
    void* Next2; // +0x1C
    void* Prev2; // +0x20
    IVP_Real_Object* Object2;       // +0x24: CONFIRMED, same evidence class as Object1 (also independently re-read by
                                    // FUN_0634b900/item 3003 at this exact offset).
    int16_t Delta2;                  // +0x28: self-relative link (always -0x1c)
    uint16_t _hash2;                  // +0x2A: mirror of _hash1
    void* Feature2;                   // +0x2C: mirror of Feature1, other side

    // RETYPED (d229:cluster-06355200 pass, 2026-09-10, from `_pad30[0x04]`).
    // FUN_06355cc0 (decomp-tracker item 3234, Common.dll.txt:436391-436432)
    // reads `CorePairEntry->CoreA->Environment->+0x120` (an already-declared
    // IVP_Environment field, `field_120`), multiplies it into this exact
    // offset (`FMUL float ptr [ESI+0x24]` where ESI is really the
    // CorePairEntry, i.e. `_field24 *= Environment->field_120`), then FUN_063562d0
    // (item 3243, Common.dll.txt:06356240-06356329... actually 06356240 is a
    // different function; see FUN_063562d0 at 437055-437156) independently
    // writes THIS SAME +0x30 slot as
    // `1.0f / ((EffMass0*EffMass1)/(EffMass0+EffMass1))` -- the classic
    // reduced-effective-mass reciprocal, using IVP_Core::
    // calc_conservative_effective_mass (FUN_063463f0, already ported,
    // ivp_core.cpp) on Object1->core/Object2->core. Both writers agree this
    // is a float "combined mass factor" for the pair. (The former duplicate
    // declaration of this record in ivp_contact_point.h called this slot
    // NormalImpulse and used "MassFactor" for +0x54, which is
    // NowFrictionPressure below; merged 2026-10-08.)
    float MassFactor; // +0x30
    // +0x34: a BYTE, not a flags dword (2026-10-08, claude-code:cmn-phy3). Every
    // access is byte-wide or touches the low byte only: the ctor clears it
    // (`AND 0xffffff00`), FUN_063562d0 sets it to exactly 1 (`AND 0xffffff01;
    // OR 1`), and the friction kernels test `CMP byte ptr [ESI+0x34],1`
    // (.text:063566fb) -- 1 selects the one-direction reaction FUN_063548e0,
    // anything else the 2x2 one FUN_06354120. Name taken from the former
    // duplicate declaration of this record in ivp_contact_point.h (SolverMode).
    uint8_t SolverMode;          // +0x34
    uint8_t _pad35[0x03];        // +0x35..0x37: never read or written as such
    // +0x38/+0x3C: RETYPED int32_t -> float 2026-09-17 (lane claude:common-ivp5,
    // decomp-tracker item 3301). They are still zeroed at construction, but they
    // are no longer consumer-less: FUN_0635a1e0's tail integrates the contact's
    // TANGENTIAL SLIP into them once per pass, one component per friction axis,
    //     _field38 -= dot(RelativePointVelocityWS, Owner->TangentAxis0) * dt
    //     _field3c -= dot(RelativePointVelocityWS, Owner->TangentAxis1) * dt
    // (Common.dll.txt:0635a895-0635a8b9 and :0635a8bc-0635a8e3; each is
    // `FMUL ST1` against the elapsed time on the x87 stack followed by
    // `FSUBR [EDI+0x38]` / `FSUBR [EDI+0x3c]` -- FSUBR against a memory operand
    // is `mem - ST0`, i.e. a subtract-into-the-field, and an FSUBR/FSTP pair on
    // a slot is float-typing proof on its own). `dt` is the same elapsed time
    // the _envTick68/_envTick6c update below produces.
    // 2026-10-08 (claude-code:cmn-phy3): merged with TangentImpulseX/Y of the
    // former duplicate declaration in ivp_contact_point.h -- the same two floats, which the
    // friction kernels clamp into the friction cone (.text:063566e9-063566f8)
    // and project onto the contact's tangent axes. Ipion's name for the pair
    // is span_friction_s[2]; the layout matches its IVP_Contact_Point.
    float SpanFrictionS[2];      // +0x38 / +0x3C
    IVP_Impact_Solver_Long_Term* Owner;           // +0x40: RESOLVED this pass (see the class-level comment block above for the full
                                 // 3-independent-field-match evidence trail: StateProcessed+0x54, LastImpactTimeStep
                                 // +0x58, WinnerPriority+0x5c, all read/written through this exact pointer by
                                 // FUN_063595c0/FUN_06359560). This is the SAME field two prior lanes flagged as an
                                 // "IVP_Ledge_Mindist+0x40 OPEN QUESTION" -- it never lived there, it lives here.
    // +0x44: NEW (item 3283/3280 pass, 2026-09-10) -- RETYPED from
    // `_pad44[0x04]` (unconfirmed). FUN_06358c50 (decomp-tracker item 3280,
    // Common.dll.txt:06358c8f-06358c97) dispatches through
    // `Object1->environment->l_material_manager`'s vtable slot [+4]
    // (IVP_Material_Manager::get_friction_factor, `PUSH Owner; CALL [EDX+4]`)
    // and stores the FPU return value here with `FSTP float ptr [EDI+0x44]`
    // (EDI = the pair record being populated). Ported 2026-10-08 in
    // IVP_PopulatePairRecordIntoOwner (ivp_mindist_limiter.cpp). NOTE: unlike
    // several neighboring fields, FUN_063539d0/item 3204's own ctor trace does
    // NOT list +0x44 among the dwords it zeroes (+0x38/+0x3c/+0x48/+0x50/
    // +0x54/+0x64/+0x70) -- this field is left as raw `operator_new(0x74)`
    // garbage at construction in retail, matched here by NOT zero-initializing
    // it either. Semantic meaning not resolved.
    // 2026-10-08 (claude-code:cmn-phy3): the friction kernels read it as the
    // contact's friction coefficient -- the cone radius is MassFactor (+0x30) *
    // NowFrictionPressure (+0x54) * this * dt^2 (.text:06356671-06356680), and
    // FUN_06354120's force limit is this * pressure * dt (06354133-0635413c).
    // Name from the former duplicate declaration in ivp_contact_point.h.
    float FrictionCoefficient;   // +0x44
    // +0x48: zeroed at construction. FLOAT (2026-10-08): the friction-cone clamp
    // accumulates `(len - cone) * pressure * coefficient` into it
    // (`FADD [ESI+0x48]; FSTP float ptr [ESI+0x48]`, .text:063566e3-063566e6).
    float FrictionCorrection;    // +0x48
    float _field4c;              // +0x4C: CONDITIONALLY computed at construction, only when side-2's ShapeType==2
                                 // (Poly, Mindist->ShapeTypeA/B convention): `DAT_063a53d0 / FUN_06347e60()` (FDIVR)
                                 // over the masked feature pointer via FUN_0635b940. Left as uninitialized garbage
                                 // when that condition is false -- reproduced literally in the port, not "fixed".
    // +0x50: zeroed at construction. FLOAT (2026-10-08): FUN_06354120 keeps its
    // running tangential work here and returns the per-pass increment
    // (`FSUB [EDI+0x50]` / `FSTP [EDI+0x50]`, .text:0635487a-063548ae).
    float TangentWork;           // +0x50
    // +0x54: zeroed at construction (distinct from IVP_Impact_Solver_Long_Term+0x54). RETYPED int32_t ->
    // float 2026-10-08 (claude-code:cmn-phy2): the friction solver stores the clamped
    // solved impulse times IVP_Event_Sim::i_delta_time here (`FMUL [EAX+4]; FSTP float
    // ptr [ESI+0x54]`, .text:06368946-0636894d) -- the contact's current friction
    // pressure (Ipion: now_friction_pressure). The former duplicate declaration in
    // ivp_contact_point.h called this slot `MassFactor`.
    float NowFrictionPressure;   // +0x54
    float GapDistance;           // +0x58 (formerly _field58): the surface gap. CONFIRMED -- initialized from global DAT_0658ab1c at construction;
                                 // consumed by FUN_06359560 (compared against DAT_0658aa14, subtracted, scaled by a
                                 // 3rd object's own +0xb0 field, combined with FUN_063594d0()'s return) as a
                                 // per-record time/tick-like float. DAT_0658ab1c is also read elsewhere in this
                                 // cluster (FUN_06356568, FUN_06362da6, FUN_06368ad9/dc9) as a plain float, consistent
                                 // with a shared "current simulation time/tick" global; exact semantics of the
                                 // FUN_06359560 formula not resolved this pass.
                                 // UPDATE 2026-09-17 (claude:common-ivp2, decomp-tracker item 3297): this field is
                                 // RE-WRITTEN every narrow-phase pass and its runtime meaning is a SURFACE GAP
                                 // DISTANCE, not a tick. FUN_063599f0 stores `|PointB - PointA|` here directly
                                 // (`FSTP [ESI+0x58]`, Common.dll.txt:06359a24, ESI still == this at that point --
                                 // ESI is only re-pointed at the Owner four instructions later). FUN_0635a1e0 then
                                 // subtracts both objects' own +0x94 surface radii from it and clamps the result at
                                 // zero (`FSUBR [EDI+0x58]; FST [EDI+0x58]; FCOMP DAT_063a53d4(0.0f)`, then
                                 // `MOV [EDI+0x58],0` on the negative branch -- Common.dll.txt:0635a444-0635a45b).
                                 // So after a pass it holds max(0, centre distance - radiusA - radiusB). The
                                 // construction-time DAT_0658ab1c seed above is still accurate; it is just a seed.
    int16_t _const5c;             // +0x5C: CONFIRMED constant -- always == 0x14 (20) at construction, no other writer
                                 // traced. Purpose (capacity? magic tag?) unresolved.
    uint8_t _pad5e[0x02];         // +0x5E..0x5F: unconfirmed
    uint32_t _flags60;            // +0x60: flags dword -- ctor forces bit0 = 1, preserves the rest of whatever
                                 // operator_new returned (same "partial init" pattern as _flags34); semantics
                                 // unresolved, placeholder.
    // +0x64: zeroed at construction. 2026-10-08 (claude-code:cmn-phy2): the friction
    // solver's push counter -- FUN_063687b0 (.text:063687f5-06368838) sets it to -1 /
    // decrements it while the solved impulse is positive, zeroes it on a zero impulse,
    // and counts it up (wrapping past 9 to 0) while the impulse is negative;
    // FUN_06368e60 sorts the friction-system list by it and FUN_06368540 counts the
    // leading negative entries. Name from the Ipion SDK field with that role.
    int32_t HasNegativePullSince;  // +0x64
    // +0x68/+0x6C: RETYPED int32_t -> float 2026-09-17 (lane claude:common-ivp5,
    // decomp-tracker item 3301). They hold a private copy of the environment's
    // split-precision current_time -- retail's ctor copies the two dwords
    // verbatim, which is why they were first declared as dwords, but every
    // CONSUMER reads them with x87: FUN_0635a1e0 computes
    //     dt = (env->current_time.hi - _envTick68) + (env->current_time.lo - _envTick6c)
    // with `FSUB [EDI+0x68]` / `FSUB [EDI+0x6c]` (Common.dll.txt:0635a884 and
    // :0635a88f) and then re-stamps both from the environment before using dt to
    // integrate _field38/_field3c above. So this pair is a "when did this pair
    // record last run a contact-frame pass" timestamp, not an opaque tick count,
    // and it is written EVERY pass, not only at construction.
    float _envTick68;             // +0x68: seeded from Mindist->ObjectA->environment->current_time.time_base at
                                 // construction (always ObjectA specifically, not the tagged-selected side -- ctor
                                 // re-reads Ctx->+0x28 fresh for this, Common.dll.txt:06353aac).
    float _envTick6c;             // +0x6C: seeded from Mindist->ObjectA->environment->current_time.time_offset.
    // RETYPED (D-listmanager pass, 2026-09-10, from `_field70`). FUN_0634b9a0
    // (item 3004) writes `node->0x70 = this` (Common.dll.txt:0634b9a4, the
    // FIRST instruction in that function's body) -- a plain owner back-pointer
    // to whichever IVP_ListManager this record is currently threaded onto via
    // ListManagerListNext/Prev above. Zeroed at construction (record not yet
    // attached to any ListManager); set for real by
    // IVP_ListManager::InsertCtx below.
    IVP_ListManager* ListManagerOwner; // +0x70

    // ---- friction-impulse methods (ivp_contact_point.cpp) ------------------
    // Until 2026-10-08 these lived on a second, duplicate declaration of this
    // record in ivp_contact_point.h; that duplicate was merged into this struct
    // by claude-code:cmn-phy3 (its fields mapped 1:1 onto the offsets above).

    // FUN_06354d70: project ImpulseChange onto Owner's two tangent axes and
    // add the results into SpanFrictionS.
    void AccumulateFrameTangent(const Vector& ImpulseChange);

    // FUN_06353f10 (RET 0xc): normal-impulse solve for one PSI. EventSim is
    // read only through its i_delta_time (+4); Depth and Gain are floats.
    void SolveNormalImpulse(const IVP_Event_Sim* EventSim, float Depth, float Gain);

    // FUN_06353e80 (RET 4): world-space friction direction from SpanFrictionS,
    // normalized; returns its length, or ClampTangentDirection's value when
    // SolverMode == 1.
    float RecomputeTangentDirection(Vector* OutDir);

    // FUN_06353b50 (RET 4): still a stub, see ivp_contact_point.cpp.
    float ClampTangentDirection(Vector* InOutDir);

    // FUN_063539d0 (thiscall, RET 4): the constructor, called by
    // IVP_FindOrCreatePairRecord (FUN_0635a950) on a fresh operator_new(0x74)
    // block. Defined in ivp_contact_point.cpp. (A user-declared constructor
    // does not affect standard layout, so the offsetof() asserts below hold.)
    explicit IVP_Contact_Point(IVP_Ledge_Mindist* Mindist);

    // FUN_0635a160 (thiscall, RET 8): subtract a split time delta from the
    // +0x68 / +0x6c timestamp and renormalize the offset into [0, 1].
    // Defined in ivp_contact_point.cpp; caller IVP_ListManager::reset_time.
    void ShiftTimeStamp(float DeltaBase, float DeltaOffset);
};
#pragma pack(pop)
static_assert(sizeof(IVP_Contact_Point) == 0x74, "retail operator_new(0x74), FUN_0635a950/FUN_063539d0");
static_assert(offsetof(IVP_Contact_Point, Next1) == 0x08, "IVP_Contact_Point+0x08");
static_assert(offsetof(IVP_Contact_Point, Prev1) == 0x0C, "IVP_Contact_Point+0x0C");
static_assert(offsetof(IVP_Contact_Point, MassFactor) == 0x30, "IVP_Contact_Point+0x30");
static_assert(offsetof(IVP_Contact_Point, Object1) == 0x10, "IVP_Contact_Point+0x10");
static_assert(offsetof(IVP_Contact_Point, Delta1) == 0x14, "IVP_Contact_Point+0x14");
static_assert(offsetof(IVP_Contact_Point, Feature1) == 0x18, "IVP_Contact_Point+0x18");
static_assert(offsetof(IVP_Contact_Point, Next2) == 0x1C, "IVP_Contact_Point+0x1C");
static_assert(offsetof(IVP_Contact_Point, Prev2) == 0x20, "IVP_Contact_Point+0x20");
static_assert(offsetof(IVP_Contact_Point, Object2) == 0x24, "IVP_Contact_Point+0x24");
static_assert(offsetof(IVP_Contact_Point, Delta2) == 0x28, "IVP_Contact_Point+0x28");
static_assert(offsetof(IVP_Contact_Point, Feature2) == 0x2C, "IVP_Contact_Point+0x2C");
static_assert(offsetof(IVP_Contact_Point, Owner) == 0x40, "IVP_Contact_Point+0x40 -- the resolved Owner field");
static_assert(offsetof(IVP_Contact_Point, ListManagerListNext) == 0x00, "IVP_Contact_Point+0x00");
static_assert(offsetof(IVP_Contact_Point, SolverMode) == 0x34, "IVP_Contact_Point+0x34");
static_assert(offsetof(IVP_Contact_Point, SpanFrictionS) == 0x38, "IVP_Contact_Point+0x38");
static_assert(offsetof(IVP_Contact_Point, FrictionCoefficient) == 0x44, "IVP_Contact_Point+0x44");
static_assert(offsetof(IVP_Contact_Point, FrictionCorrection) == 0x48, "IVP_Contact_Point+0x48");
static_assert(offsetof(IVP_Contact_Point, TangentWork) == 0x50, "IVP_Contact_Point+0x50");
static_assert(offsetof(IVP_Contact_Point, NowFrictionPressure) == 0x54, "IVP_Contact_Point+0x54");
static_assert(offsetof(IVP_Contact_Point, _field4c) == 0x4C, "IVP_Contact_Point+0x4C");
static_assert(offsetof(IVP_Contact_Point, GapDistance) == 0x58, "IVP_Contact_Point+0x58");
static_assert(offsetof(IVP_Contact_Point, _const5c) == 0x5C, "IVP_Contact_Point+0x5C");
static_assert(offsetof(IVP_Contact_Point, _flags60) == 0x60, "IVP_Contact_Point+0x60");
static_assert(offsetof(IVP_Contact_Point, HasNegativePullSince) == 0x64, "IVP_Contact_Point+0x64");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, IndexInFs) == 0x50, "IVP_Impact_Solver_Long_Term+0x50");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, FrictionIsBroken) == 0x55, "IVP_Impact_Solver_Long_Term+0x55");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, FrictionInfos) == 0x58, "IVP_Impact_Solver_Long_Term+0x58");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, HasNegativePullSince) == 0x60, "IVP_Impact_Solver_Long_Term+0x60");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, GapDistance) == 0x64, "IVP_Impact_Solver_Long_Term+0x64");
static_assert(offsetof(IVP_Contact_Point, _envTick68) == 0x68, "IVP_Contact_Point+0x68");
static_assert(offsetof(IVP_Contact_Point, _envTick6c) == 0x6C, "IVP_Contact_Point+0x6C");
static_assert(offsetof(IVP_Contact_Point, ListManagerOwner) == 0x70, "IVP_Contact_Point+0x70");

// ---------------------------------------------------------------------------
// "ListManager": per-(SimUnit-or-Core-group) IVP_Contact_Point registry
// ---------------------------------------------------------------------------
// D-listmanager pass (2026-09-10). This class has been circled by MANY prior
// lanes (D104 through D158) across decomp-tracker items 2992 (ctor,
// FUN_0634b600), 3002 (remove, FUN_0634b8d0), 3004 (insert, FUN_0634b9a0),
// 3003 (find-or-attach a per-(Core,Core) entry, FUN_0634b900), 3005 (register
// a Core, FUN_0634b9d0) and 3222 (merge two ListManagers, FUN_063555b0) --
// see those items' own decomp-tracker notes for the full accumulated
// evidence trail this struct/class reproduces. Retail allocates it via
// `operator_new(0x4c)` (confirmed at the one known construction call site,
// Common.dll.txt:06356e27, inside FUN_06356bc0/item 3262's own still-unported
// body) and constructs it via FUN_0634b600/item 2992.
//
// Two DIFFERENT "pair record" concepts collide in this cluster's own prior
// notes -- disambiguated here:
//   - IVP_Contact_Point (declared above, 0x74 bytes, keyed by
//     (Object1,Object2) IVP_Real_Object* identity): FUN_06356bc0's own
//     "Ctx"/find-or-create-pair-record chain (items 3302/3303/3204, already
//     ported, IVP_FindOrCreatePairRecord in ivp_mindist_limiter.cpp). This IS
//     the "node"/"Ctx" argument threaded onto a ListManager's own
//     CtxListHead intrusive list below (via its own ListManagerListNext/
//     Prev/ListManagerOwner fields, retyped above this pass).
//   - IVP_ListManager_CorePairEntry (declared just below, 0x30 bytes, keyed
//     by (CoreA,CoreB) IVP_Core* identity): a SEPARATE per-ListManager
//     bookkeeping node, found-or-created by FUN_0634b900/item 3003 (ctor
//     FUN_06356240/item 3241, lookup FUN_06355770/item 3226) each time an
//     IVP_Contact_Point gets attached to a ListManager. Each entry owns
//     its own small IVP_U_Vector<IVP_Contact_Point> of every
//     IVP_Contact_Point attached under that specific Core pair.
//
// Field-by-field evidence (all from FUN_0634b600/item 2992's own disasm,
// Common.dll.txt:418727-418765, re-verified against raw bytes this pass):
//   +0x00/+0x08/+0x10: CORRECTED 2026-10-08 (claude-code:cmn-phy3). These are
//     three VTABLE POINTERS, not "a Common.dll-wide shared slot" -- the old
//     reading came from Ghidra labelling each table by its first entry
//     (062ed730, IVP_Controller's default `RET 4`, ICF-labelled
//     MetaList<GoodDesc>::operator=). The ctor writes:
//       +0x08 <- .rdata:063a5500  (.text:0634b617)  static friction handle
//       +0x10 <- .rdata:063a54e4  (.text:0634b61e)  energy friction handle
//       +0x00 <- .rdata:063a54c8  (.text:0634b667)  the friction system itself
//     and the destructor FUN_0634b710 stamps IVP_Controller's own table
//     .rdata:0639b64c back into all three (.text:0634b7b2-0634b7bd). The
//     friction system is an IVP_Controller_Dependent (its slot 2 returns its
//     own core vector +0x2c, see below); each handle is an
//     IVP_Controller_Independent (slot 2 = LAB_06285a60, the Independent
//     branch's shared empty vector) holding a back pointer at its own +4.
//     RegisterCore registers all three on every dynamic core
//     (.text:0634ba1f-0634ba3f), UnregisterCore removes them.
//   +0x04: the ctor's own incoming stack parameter, the environment (see the
//     field comment below).
//   +0x0c/+0x14: the two handles' back pointers (`MOV [EAX+0xC],EAX` /
//     `MOV [EAX+0x14],EAX`), read by every handle method as `[this+4]`
//     (e.g. .text:063568a4, 06356513, 063565c3).
//   +0x20: intrusive CtxListHead, zeroed at construction -- see
//     IVP_Contact_Point::ListManagerListNext/Prev/ListManagerOwner above
//     and InsertCtx/RemoveCtx below for the real FUN_0634b9a0/FUN_0634b8d0
//     port.
//   +0x24/+0x2c/+0x34: three embedded IVP_U_Vector<T>-shaped headers, all
//     zeroed at construction, ALL matching the exact generic
//     {memsize:u16@0,n_elems:u16@2,elems:T**@4} layout items 3223/3224
//     (FUN_063556b0/FUN_063556e0, already `skipped` as instantiations of the
//     real IVP_U_Vector<T> template) grow/push through -- so this port
//     embeds the actual template directly rather than raw offset fields, for
//     an automatic byte-exact match. +0x24/+0x2c: item 3005's "all
//     registered cores" / "non-kinematic registered cores" pair.  +0x34:
//     item 3003's "per-(CoreA,CoreB) entry" vector (cross-confirmed
//     independently via FindCorePairEntryExact's (06356280) own +0x36/+0x38
//     reads, see item 3262's own D-item3262-outparams pass notes).
//   +0x3c/+0x3e: two int16 counters, zeroed at construction -- +0x3c is
//     RegisterCore's own per-call counter (item 3005, incremented
//     unconditionally regardless of the kinematic branch); +0x3e is
//     CtxListHead's own live count (items 3002/3004, INC/DEC on
//     insert/remove).
//   +0x44 (low byte only): read-modify-write on otherwise-uninitialized
//     memory (`MOV EDX,[EAX+0x44]; AND EDX,0xffffff00; MOV [EAX+0x44],EDX`) --
//     the 8-bit bit-field union_find_necessary (see the member). The upper
//     3 bytes stay as the allocation left them, as in retail.
//   +0x18..+0x1f, +0x40..+0x43, +0x48..+0x4b: NOT touched by the ctor at
//     all -- either genuine padding or lazily-initialized by some other,
//     still-unexamined ListManager method. Left untouched here too (matches
//     retail's own uninitialized-memory behavior; nothing in this port reads
//     them).
// IVP_Friction_Info_For_Core: a core's per-friction-system record, the object
// IVP_Core::find_object_hash_entry(Key = system) returns (06347c40 compares the
// candidate's +0x8 against the key). operator_new(0xc) at .text:063558fd and in
// FUN_06356bc0; +0x00 is an IVP_U_Vector of the contact points (FUN_06355310
// removes from it, FUN_06368a30 walks it at .text:06368b71-06368b8d), +0x08 the
// owning system. 2026-10-08 (claude-code:cmn-phy2): declared here for the friction
// solver. The two file-local copies (IVP_Core_ObjectHashEntry in
// ivp_mindist_limiter.cpp and ivp_listmanager_topology.cpp) were merged into
// this one the same day (claude-code:cmn-phy3). Released by
// IVP_Core::release_object_hash_entry with `delete`.
struct IVP_Friction_Info_For_Core
{
    IVP_U_Vector<IVP_Contact_Point> FrDists; // +0x00
    IVP_ListManager* FrictionSystem;              // +0x08
};
static_assert(sizeof(IVP_Friction_Info_For_Core) == 0xc, "IVP_Friction_Info_For_Core is 0xc bytes");

#pragma pack(push, 4)
struct IVP_ListManager_CorePairEntry
{
    // +0x00: embedded IVP_U_Vector<IVP_Contact_Point>, zeroed by
    // FUN_06356240/item 3241 (`word@0=0, word@2=0, dword@4=0`). Populated by
    // FUN_0634b900/item 3003 via FUN_063556b0/item 3223 (already `skipped`,
    // the same generic Add() this port's IVP_U_Vector<T>::Add already
    // reproduces).
    IVP_U_Vector<IVP_Contact_Point> AttachedCtx; // +0x00

    uint8_t _pad08[0x10]; // +0x08..0x17: not touched by FUN_06356240; unconfirmed

    // FUN_06356240/item 3241 (Common.dll.txt:436966-436987, 16 insns, fully
    // traced this pass): +0x18 = 1 (int32), +0x1c = -1000.0f (float,
    // 0xc47a0000). Purpose of both unresolved -- refcount/priority-sentinel
    // are only guesses at the *shape* (an initial "not yet contended" /
    // "worse than any real candidate" value), not independently confirmed;
    // kept as placeholder names/values rather than invented semantics.
    // +0x18 (2026-10-08, claude-code:cmn-phy3): the redundant-dist merge
    // countdown -- FUN_063551c0 decrements it once per pass and, at zero, runs
    // FUN_06355050 on this pair and resets it to 5. Name from the merged
    // IVP_Contact_Set declaration.
    int32_t MergeCacheCountdown = 1; // +0x18
    // +0x1c: the library's last_impact_time. FUN_06356240 builds the IVP_Time
    // {0xc47a0000 = -1000.0f, 0} on its stack and copies it here
    // (.text:06356247-:06356266); IVP_Mindist_Do_Impact (ivp_impact.cpp)
    // reads and restamps it with env->current_time.
    IVP_Time LastImpactTime = { -1000.0f, 0.0f }; // +0x1c / +0x20
    // +0x24: zeroed at construction. FLOAT, not an integer -- the per-tick budget in
    // IVP_ListManager_TickCorePairBudget reads and writes it with the x87 unit:
    //     .text:06355cd0  FMUL float ptr [ESI + 0x24]
    //     .text:06355cd7  FST  float ptr [ESI + 0x24]
    // An `FST float` is a four-byte float store, so the width is unchanged and the offsetof
    // assert below still holds. It was declared int32_t until 2026-09-17, which made the
    // scale-by-field_120 step truncate the budget to a whole number on every single tick
    // (MSVC flagged it as C4244 on both the store and the later `-= Consumed`).
    float _field24 = 0.0f;

    // +0x28/+0x2c: NOT written by FUN_06356240 itself -- written by its
    // caller, IVP_ListManager::FindOrAttachCorePairEntry (0634b900), right
    // after construction (`MOV [EAX+0x28],EBX` / `MOV [EAX+0x2c],EBP`,
    // .text:0634b96f / 0634b972).
    IVP_Core* CoreA; // +0x28
    IVP_Core* CoreB; // +0x2c

    // FUN_06356240 (.text:06356240-06356276): the initialisers above and
    // nothing else.
    IVP_ListManager_CorePairEntry() {}
};
#pragma pack(pop)
static_assert(sizeof(IVP_ListManager_CorePairEntry) == 0x30, "retail operator_new(0x30), FUN_0634b900/FUN_06356240");
static_assert(offsetof(IVP_ListManager_CorePairEntry, MergeCacheCountdown) == 0x18, "IVP_ListManager_CorePairEntry+0x18");
static_assert(offsetof(IVP_ListManager_CorePairEntry, LastImpactTime) == 0x1C, "IVP_ListManager_CorePairEntry+0x1C");
static_assert(offsetof(IVP_ListManager_CorePairEntry, _field24) == 0x24, "IVP_ListManager_CorePairEntry+0x24");
static_assert(offsetof(IVP_ListManager_CorePairEntry, CoreA) == 0x28, "IVP_ListManager_CorePairEntry+0x28");
static_assert(offsetof(IVP_ListManager_CorePairEntry, CoreB) == 0x2C, "IVP_ListManager_CorePairEntry+0x2C");

// ---------------------------------------------------------------------------
// The friction system's two embedded controller handles (2026-10-08,
// claude-code:cmn-phy3). Retail builds the IVP sources without RTTI, so the
// class names are the IVP SDK's names for these roles; each is identified by
// its vtable (stamped by the IVP_ListManager ctor) and its back pointer.
// Both destruct through IVP_Controller's destructor only (slot 6 is 0634b6b0,
// which calls FUN_0634b6a0 = stamp 0639b64c): no state of their own to tear
// down.
// ---------------------------------------------------------------------------
class IVP_ListManager;

// IVP_ListManager+0x08. vtable .rdata:063a5500:
//   +0x00 063568a0  core_is_going_to_be_deleted_event      (this class)
//   +0x04 0634b6d0  `FLD [063a53d0]=1.0f; RET`              (this class's copy)
//   +0x08 06285a60  IVP_Controller_Independent's shared vector
//   +0x0c 0628f050  IVP_Controller's `RET 0x8`
//   +0x10 063565c0  do_simulation_controller               (this class)
//   +0x14 06325390  `XOR EAX,EAX; RET` -> priority 0        (this class)
//   +0x18 0634b6b0  deleting destructor
class IVP_Friction_Sys_Static : public IVP_Controller_Independent
{
public:
    IVP_ListManager* FrictionSystem; // +0x04 (= IVP_ListManager+0x0c)

    explicit IVP_Friction_Sys_Static(IVP_ListManager* FrictionSystemIn)
        : FrictionSystem(FrictionSystemIn)
    {
    }
    virtual ~IVP_Friction_Sys_Static() {}

    // 063568a0 (RET 4)
    virtual void core_is_going_to_be_deleted_event(IVP_Core* Core) override;
    // 0634b6d0
    virtual float get_minimum_simulation_frequency() override { return 1.0f; } // formerly slot_01
    // 063565c0 (RET 8)
    virtual void do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList) override;
    // 06325390
    virtual IVP_CONTROLLER_PRIORITY get_controller_priority() override { return IVP_CP_STATIC_FRICTION; }

    // 06356560 (thiscall on this handle, RET 4): the one-dist case of the
    // static pass -- normal impulse on the system's only dist, then drop it
    // when it is out of range.
    void DoSimulationSingleFriction(IVP_Event_Sim* EventSim);
};
static_assert(sizeof(IVP_Friction_Sys_Static) == 0x8, "static friction handle: vptr + back pointer");

// IVP_ListManager+0x10. vtable .rdata:063a54e4:
//   +0x00 062ed730  IVP_Controller's `RET 4`
//   +0x04 0627cd20  IVP_Controller's 1.0f default
//   +0x08 06285a60  IVP_Controller_Independent's shared vector
//   +0x0c 0628f050  IVP_Controller's `RET 0x8`
//   +0x10 06356510  do_simulation_controller               (this class)
//   +0x14 0634b690  `MOV EAX,0x7d0; RET` -> priority 2000  (this class)
//   +0x18 0634b6b0  deleting destructor
class IVP_Friction_Sys_Energy : public IVP_Controller_Independent
{
public:
    IVP_ListManager* FrictionSystem; // +0x04 (= IVP_ListManager+0x14)

    explicit IVP_Friction_Sys_Energy(IVP_ListManager* FrictionSystemIn)
        : FrictionSystem(FrictionSystemIn)
    {
    }
    virtual ~IVP_Friction_Sys_Energy() {}

    // 06356510 (RET 8)
    virtual void do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList) override;
    // 0634b690
    virtual IVP_CONTROLLER_PRIORITY get_controller_priority() override { return IVP_CP_ENERGY_FRICTION; }
};
static_assert(sizeof(IVP_Friction_Sys_Energy) == 0x8, "energy friction handle: vptr + back pointer");

// IVP_ListManager itself (Ipion's IVP_Friction_System). vtable .rdata:063a54c8:
//   +0x00 062ed730  IVP_Controller's `RET 4` (core_is_going_to_be_deleted_event default)
//   +0x04 0634b6d0  `FLD 1.0f; RET`                         (this class's copy)
//   +0x08 06356500  `LEA EAX,[ECX+0x2c]; RET` -> &RegisteredDynamicCores
//                   (ICF-labelled EngineObject::get_position)
//   +0x0c 06355bb0  reset_time(IVP_Time)                     (this class)
//   +0x10 06356660  do_simulation_controller                 (this class)
//   +0x14 0634b6e0  `MOV EAX,0x258; RET` -> priority 600     (this class)
//   +0x18 0634b6f0  deleting destructor (FUN_0634b710 + operator delete)
#pragma pack(push, 4)
class IVP_ListManager : public IVP_Controller_Dependent
{
public:
    // +0x04: the ctor's incoming param. RETYPED IVP_Core* -> IVP_Environment* 2026-10-08
    // (claude-code:cmn-phy2): the friction solver reads `[[fs+4]+0xa0]` as the
    // environment's IVP_U_Memory arena (.text:06368f7f-06368f82) and
    // `[[fs+4]+0xc4]` as IVP_Environment::field_c4 (.text:06368362-06368367 via the
    // solver's copy at +0x1c, set from [fs+4] at .text:063553c2-063553c5); the
    // split path FUN_06355890 passes the old system's own [+4] (.text:063558c6),
    // and the "PrimaryCore->0xc" the old note traced is IVP_Core+0x0c, the core's
    // environment pointer.
    IVP_Environment* Environment;     // +0x04
    IVP_Friction_Sys_Static StaticFsHandle; // +0x08 vptr / +0x0c back pointer
    IVP_Friction_Sys_Energy EnergyFsHandle; // +0x10 vptr / +0x14 back pointer
    uint8_t _pad18[0x08];             // +0x18..0x1f: not touched by the ctor; unconfirmed.

    IVP_Contact_Point* CtxListHead; // +0x20: head of the intrusive Ctx list, threaded through EACH NODE's
                                       // own ListManagerListNext/Prev (IVP_Contact_Point, above) -- see
                                       // InsertCtx/RemoveCtx below (FUN_0634b9a0/FUN_0634b8d0, items 3004/3002).
    IVP_U_Vector<IVP_Core> RegisteredCores;        // +0x24: every Core RegisterCore() has ever seen (item 3005).
    IVP_U_Vector<IVP_Core> RegisteredDynamicCores; // +0x2c: subset of the above that were NOT kinematic at
                                       // registration time (item 3005's `TEST byte[Core],0xc` gate).
    IVP_U_Vector<IVP_ListManager_CorePairEntry> CorePairEntries; // +0x34: per-(CoreA,CoreB) bookkeeping nodes
                                       // (items 3003/3226/3241).
    uint16_t RegisterCoreCallCount;   // +0x3c: incremented once per RegisterCore() call, both branches (item 3005).
    uint16_t CtxListCount;            // +0x3e: live count of the CtxListHead intrusive list (items 3002/3004).
    // +0x40: int16, not touched by the ctor. The friction pass reads it with MOVSX as
    // the count of leading CtxListHead entries left out of the friction matrix
    // (.text:063553ce, 063687d2, 06368990) and FUN_06368d80 writes it (always 0,
    // .text:06368e08 / 06368e10). Ipion's complex_not_necessary_number.
    int16_t ComplexNotNecessaryNumber; // +0x40
    uint8_t _pad42[0x02];             // +0x42..0x43: unconfirmed.
    // +0x44: an 8-bit bit-field in a dword (Ipion's union_find_necessary). Every access is
    // a read-modify-write of the low byte only: the ctor clears it (`AND 0xffffff00`,
    // .text:0634b661), RemoveDistFromSystem (063554d0-063554db) and the core teardown
    // (063464e8-063464f4) set it to 1 (`AND 0xffffff01; OR 1`), and the friction pass
    // tests and clears it before the union-find split (IVP_ListManager_FindDisconnectedCore).
    // The upper 24 bits are never written, as in retail (formerly uint32_t _flags44).
    uint32_t union_find_necessary : 8; // +0x44
    uint32_t _Unused44 : 24;           // +0x45..0x47: never touched.
    uint8_t _pad48[0x04];             // +0x48..0x4b: not touched by the ctor; unconfirmed. Closes out retail's
                                       // confirmed operator_new(0x4c) allocation size.

    // FUN_0634b600 (item 2992, .text:0634b600-0634b680, RET 4).
    explicit IVP_ListManager(IVP_Environment* EnvironmentIn);

    // FUN_0634b710 (destructor body) / FUN_0634b6f0 (deleting destructor,
    // vtable slot 6). Retail tears down the three vectors in reverse order
    // (+0x34, +0x2c, +0x24) and stamps IVP_Controller's vtable into all three
    // sub-objects -- exactly the member destruction order of this class. It
    // does NOT touch the dist list or the core-pair entries.
    virtual ~IVP_ListManager() {}

    // ---- IVP_Controller overrides (vtable .rdata:063a54c8) ----------------
    // 0634b6d0
    virtual float get_minimum_simulation_frequency() override { return 1.0f; } // formerly slot_01
    // 06356500 (`LEA EAX,[ECX+0x2c]; RET`)
    virtual IVP_U_Vector<IVP_Core>* get_associated_controlled_cores() override { return &RegisteredDynamicCores; }
    // 06355bb0 (RET 8): rebases every dist's IVP_Time stamp by Delta.
    virtual void reset_time(IVP_Time Delta) override; // formerly slot_03
    // 06356660 (RET 8): the friction-impulse pass (ivp_contact_point.cpp).
    virtual void do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList) override;
    // 0634b6e0
    virtual IVP_CONTROLLER_PRIORITY get_controller_priority() override { return IVP_CP_DYNAMIC_FRICTION; }

    // FUN_0634b7d0 (.text:0634b7d0-0634b84f): re-runs the contact frame of
    // every dist (FUN_0635a1e0) and adds the pressure-weighted gap closure of
    // each core pair, when positive, into that pair's _field24.
    void RecalcAllContactPoints();

    // FUN_0634b9a0 (item 3004, Common.dll.txt:0634b9a0-0634b9c1). Insert-at-head
    // into the CtxListHead intrusive list; stamps Node->ListManagerOwner = this.
    void InsertCtx(IVP_Contact_Point* Node);

    // FUN_0634b8d0 (item 3002, Common.dll.txt:0634b8d0-0634b8f4). Remove-from-list
    // companion to InsertCtx.
    void RemoveCtx(IVP_Contact_Point* Node);

    // FUN_06355770 (item 3226, Common.dll.txt:06355770-063557ae). Unordered-pair
    // linear scan over CorePairEntries for an entry whose {CoreA,CoreB} matches
    // the given pair in either order.
    IVP_ListManager_CorePairEntry* FindCorePairEntry(IVP_Core* CoreA, IVP_Core* CoreB);

    // FUN_06356280 (.text:06356280-063562be, RET 8). Scans CorePairEntries from
    // the last element down for the entry whose {CoreA,CoreB} is exactly
    // {KeyA,KeyB} in either order; 0 when none. Called by do_impact (06358680).
    // formerly the free function IVP_FindPairedCandidate (ivp_mindist_candidates.h)
    IVP_ListManager_CorePairEntry* FindCorePairEntryExact(IVP_Core* KeyA, IVP_Core* KeyB);

    // FUN_0634b900 (item 3003, Common.dll.txt:0634b900-0634b998). Resolves
    // Ctx's own two sides (Ctx->Object1->core / Ctx->Object2->core) to their
    // IVP_ListManager_CorePairEntry (via FindCorePairEntry, creating one via
    // operator_new(0x30)+FUN_06356240/item 3241 on a miss), then attaches Ctx
    // into that entry's own AttachedCtx vector.
    void FindOrAttachCorePairEntry(IVP_Contact_Point* Ctx);

    // FUN_0634b9d0 (item 3005, .text:0634b9d0-0634ba4b, RET 4). Adds Core to
    // RegisteredCores; a dynamic core ((Flags & 0xc) == 0) also goes into
    // RegisteredDynamicCores and gets the three controllers registered on it
    // with IVP_Core::register_controller (FUN_06349180): the static handle
    // (+0x08), the system itself, the energy handle (+0x10), in that order
    // (.text:0634ba1f-0634ba3f). Always bumps RegisterCoreCallCount.
    void RegisterCore(IVP_Core* Core);

    // FUN_0634ba50 (.text:0634ba50-0634bb1a, RET 4). Mirror of RegisterCore:
    // a dynamic core leaves RegisteredDynamicCores and has the three
    // controllers removed with IVP_Core::detach_controller (FUN_06349110) in
    // the REVERSE order -- energy handle, system, static handle
    // (.text:0634baa5-0634babe); then Core leaves RegisteredCores and
    // RegisterCoreCallCount drops.
    void UnregisterCore(IVP_Core* Core);

    // FUN_063555b0 (item 3222, Common.dll.txt:435482-435597). Merges Other's
    // tracked state into `this`. Phase 1 (real): walks Other's CtxListHead,
    // retiring each node's CorePairEntry bookkeeping on Other
    // (IVP_RetirePairRecordBookkeeping / FUN_0634b850, item 3001 -- now real,
    // see ivp_mindist_limiter.cpp) before moving the node onto `this`
    // (InsertCtx + FindOrAttachCorePairEntry). Phase 2 (now real, see
    // IVP_MergeRegisteredCores in ivp_mindist_limiter.cpp): for every Core in
    // Other->RegisteredCores, re-keys or merges Core's own per-ListManager
    // hash entry (IVP_Core::find/register/unregister_object_hash_entry,
    // items 2870-2872) from Other to `this`, moving Core itself between the
    // two ListManagers' RegisteredCores/RegisteredDynamicCores lists via
    // UnregisterCore/RegisterCore when no merge target already existed. Tail
    // (.text:06355698-063556a2): `if (Other) Other->vtbl[6](1)`, i.e.
    // `delete Other` -- Other is gone when this returns.
    void MergeFrom(IVP_ListManager* Other);

    // ------------------------------------------------------------------
    // Friction pass (Ipion's IVP_Friction_System). 2026-10-08,
    // claude-code:cmn-phy2. Defined in ivp_friction_solver.cpp (except
    // RetirePairRecordBookkeeping, ivp_mindist_limiter.cpp); every method
    // below was traced against the listing named on it.
    // ------------------------------------------------------------------

    // FUN_0634b850 (.text:0634b850-0634b8be, RET 4). Detaches Dist from its
    // (CoreA,CoreB) entry; destroys the entry when it becomes empty.
    // Returns 1 if the entry was destroyed. Was a file-local helper of
    // ivp_mindist_limiter.cpp (IVP_RetirePairRecordBookkeeping).
    int RetirePairRecordBookkeeping(IVP_Contact_Point* Dist);

    // FUN_06355490 (.text:06355490-0635558b, RET 4): removes Dist from the
    // system and deletes it.
    void RemoveDistFromSystem(IVP_Contact_Point* Dist);

    // FUN_06368540 (.text:06368540-06368557): number of leading CtxListHead
    // entries whose HasNegativePullSince is negative.
    int GetNumSupposedActiveFrDists() const;

    // FUN_06368e20 (.text:06368e20-06368e53, RET 8): moves Second in front of
    // First (the two are neighbours, First before Second).
    void ExchangeFrDists(IVP_Contact_Point* First, IVP_Contact_Point* Second);

    // FUN_06368e60 (.text:06368e60-06368e9f): one insertion-sort pass of
    // CtxListHead by HasNegativePullSince.
    void ReorderMindistsForComplex();

    // FUN_06368d80 (.text:06368d80-06368e18): drops dists that are out of
    // friction range, moves far-but-shared ones to the list head, then
    // zeroes ComplexNotNecessaryNumber.
    void RecheckFrDistsBeforeSolve();

    // FUN_06368ea0 (.text:06368ea0-06368eee, RET 4): 1 unless Core sits in
    // more than one CorePairEntries pair whose two cores are both dynamic.
    int CoreIsInAtMostOneDynamicPair(IVP_Core* Core) const;

    // FUN_06368f00 (.text:06368f00-06368f58): the oversized-matrix fallback.
    void StaticFrOversizedMatrixPanic();

    // FUN_06368f60 (.text:06368f60-06369074, RET 4): the friction pass entry
    // (called by FUN_063565c0 when more than one dist is attached).
    void DoPushesDistanceKeepers(const IVP_Event_Sim* EventSim);

    // FUN_063690b0 (.text:063690b0-06369160): kinetic energy of
    // RegisteredCores with their pending speed changes applied.
    double KineticEnergyOfSystem() const;

    // FUN_06369170 (.text:06369170-0636918d): commit_speed_changes on every
    // RegisteredDynamicCores entry.
    void ConfirmComplexPushes();

    // FUN_06369190 (.text:06369190-063691ad): clear_speed_change on every
    // RegisteredDynamicCores entry.
    void UndoComplexPushes();

    // FUN_063691b0 (.text:063691b0-063691e4): sum of Mass * 9.81f * 0.1f over
    // the RegisteredCores whose Flags & 0xc is clear.
    double MaxEnergyGain() const;
};
#pragma pack(pop)
static_assert(sizeof(IVP_ListManager) == 0x4c, "retail operator_new(0x4c), FUN_0634b600/item 2992");
static_assert(offsetof(IVP_ListManager, Environment) == 0x04, "IVP_ListManager+0x04");
static_assert(offsetof(IVP_ListManager, ComplexNotNecessaryNumber) == 0x40, "IVP_ListManager+0x40");
static_assert(offsetof(IVP_ListManager, StaticFsHandle) == 0x08, "IVP_ListManager+0x08 static handle (vtable 063a5500)");
static_assert(offsetof(IVP_ListManager, EnergyFsHandle) == 0x10, "IVP_ListManager+0x10 energy handle (vtable 063a54e4)");
static_assert(offsetof(IVP_ListManager, CtxListHead) == 0x20, "IVP_ListManager+0x20");
static_assert(offsetof(IVP_ListManager, RegisteredCores) == 0x24, "IVP_ListManager+0x24");
static_assert(offsetof(IVP_ListManager, RegisteredDynamicCores) == 0x2C, "IVP_ListManager+0x2C");
static_assert(offsetof(IVP_ListManager, CorePairEntries) == 0x34, "IVP_ListManager+0x34");
static_assert(offsetof(IVP_ListManager, RegisterCoreCallCount) == 0x3C, "IVP_ListManager+0x3C");
static_assert(offsetof(IVP_ListManager, CtxListCount) == 0x3E, "IVP_ListManager+0x3E");
static_assert(offsetof(IVP_ListManager, _pad48) == 0x48, "IVP_ListManager+0x44 bit-field dword ends at +0x48");

// Forward declaration: IVP_Complex_Mindist is declared in full further below,
// but IVP_Ledge_Mindist::AttachAsPendingSynapse (D227 pass, 2026-09-10, see
// its own comment near the class's other vtable-slot methods) needs the
// pointer type now.
class IVP_Complex_Mindist;
class IVP_Mindist_Manager; // src/IVP/ivp_physics/ivp_ov_element.h

// IVP_Ledge_Mindist: a single candidate ledge/feature pair (retail 0x80 bytes).
// Fields carry their retail byte offsets.
//
// 2026-10-08 (claude-code:cmn-phy5): this is the library's IVP_Mindist (vtable
// 063a55e0; IVP_Complex_Mindist below is IVP_Mindist_Recursive, 063a5878). It
// derives from IVP_Mindist_Base (+0x14..+0x6f, cmn-soup 2026-10-08), which
// derives from the real IVP_Collision (ivp_collision.h), which owns:
//   +0x04 EventQueueHandle -- the handle this mindist holds in the
//         environment's time-ordered event queue, 0xFFFF = not queued
//         (FUN_0634f320: CMP/MOV [ESI+0x4] at :0634f35e/:0634f3a7/:0634f98b);
//   +0x08 delegator        -- was `CountChainParent`; FUN_0634db70 stores the
//         ctor's delegator argument there (:0634db8d), FUN_06362ec0/ea0 reach it
//         as [this-0x78] from the +0x80 delegator sub-object;
//   +0x0c/+0x10 the two IVP_OV_Element back-indices (was `_pad0c`).
// Retail vtable slots 5..8 (06352b10 / 06325390 / 0634e9d0 / 06358cb0) are
// declared below, in retail order, as mindist_rescue_push / is_recursive /
// exact_mindist_went_invalid / do_impact (cmn-phy8 2026-10-08): IVP_Collision
// declares slots 0..4, so the C++ slot indices now equal retail's.
// ============================================================================
// IVP_Mindist_Base (library name, spelled in Common.dll's exported PhySys
// signatures), vtable 063a55a4 (cmn-soup 2026-10-08). The layer between
// IVP_Collision and IVP_Ledge_Mindist (= the library's IVP_Mindist):
//   slot 0 0634dc00 (IVP_Collision's CORE trap) / 1 0634de70 get_objects /
//   2 0634de90 get_ledges / 3 0634dc10 (IVP_Collision) / 4 0634dc80 deleting
//   dtor over 0634dca0.
// Ctor FUN_0634db70 (RET 4, arg = delegator), see ivp_mindist_limiter.cpp.
// Dtor FUN_0634dca0: vptr 063a55a4, eh vector destructor iterator over the
// two synapses (LAB_0634dc50), vptr 063a55b8 -- the member and base
// destructors. IVP_Ledge_Mindist's dtor 0634dd60 inlines the same tail
// (:0634de1e-:0634de40).
// ============================================================================
#pragma pack(push, 4)
class IVP_Mindist_Base : public IVP_Collision
{
public:
    uint32_t StateFlags;                 // +0x14: packed bitfield (kind bits 0xc000/0xc00, winner-recalc bits 0x100000/0x1000)
    // +0x18 / +0x34: the two synapses, by value (cmn-phy5 2026-10-08; were
    // flat fields ObjectA/FeatureA/ShapeTypeA/... with an int16 "link delta"
    // where each synapse's vptr lives). Built by the eh vector constructor
    // iterator in 0634db70 (:0634dba3-:0634dbbd, size 0x1c, count 2). Index i
    // is the physical slot: synapse[0] at +0x18 (object +0x28, edge +0x2c,
    // status +0x32), synapse[1] at +0x34 (object +0x44, edge +0x48, status
    // +0x4e). StateFlags bits 8-9 pick the "sorted" order (get_sorted_synapse).
    // The +0x40 retraction history (it is synapse[1].prev, not a pair-record
    // Owner) is in IVP_Contact_Point's comment above.
    IVP_Synapse_Real synapse[2];           // +0x18 / +0x34

    // FUN_06352e20 (.text:06352e20-06352e3c, thiscall, RET 4): StateFlags bits
    // 8-9 select which physical synapse is sorted index 0;
    // `this + 0x18 + (((flags >> 8) & 3) ^ index) * 0x1c`. Other callers inline
    // the same arithmetic, the other side with `flags ^ 0x100` (0634f320
    // :0634f328-:0634f358, 0634fa40 :0634fa6d-:0634faa0).
    IVP_Synapse_Real* get_sorted_synapse(int32_t Index)
    {
        return &synapse[((StateFlags >> 8) & 3u) ^ static_cast<uint32_t>(Index)];
    }

    float CombinedRadius;                 // +0x50: sum of both objects' contact radii (FUN_0634def0)
    // +0x54/+0x58/+0x60-0x68: written by the minimize solver's leaves
    // (ivp_mindist_minimize.cpp); the simplest writer is minimize_BB
    // (FUN_063536d0, .text:0635376b-06353823), which differences both
    // objects' cache objects (IVP_Real_Object::ResolveNarrowPhaseCacheSlot)
    // at +0x4c (WorldTransform.vv) into SeparatingNormal and their +0x5c
    // (::Secondary) for SecondaryNormalDot. Retail field names are not in
    // the binary; the names describe the math.
    float SeparationGap;                   // +0x54: (1/|Delta|)*|Delta|^2 - CombinedRadius, i.e. |Delta| - CombinedRadius
                                            // on the normal path (|Delta| > a small epsilon, DAT_063a53d8); on the
                                            // near-zero-length degenerate path retail skips the reciprocal and this
                                            // becomes |Delta|^2 - CombinedRadius instead (063536d0 :063537b4-:063537de).
    float SecondaryNormalDot;              // +0x58: dot(SlotA.Secondary - SlotB.Secondary, SeparatingNormal)
                                            // (063536d0 :063537fe-:06353823).
    // +0x5c: RESOLVED 2026-09-17 (lane claude:common-ivp5, decomp-tracker items
    // 3117/3128). Was `_pad5c[0x04]`. FUN_06350050 ends by storing an x87 value
    // here (`FSTP float ptr [EDI+0x5c]`, Common.dll.txt:06350129) -- a float
    // regardless of anything else. The value is the sum, over BOTH sides, of
    // that side's object's hull spread evaluated at the current environment
    // time: for each of ObjectA/ObjectB,
    //     spread = (HullMgr.OuterGradient - HullMgr.InnerGradient) * dt
    //            + (HullMgr.OuterValueAtLastEdit - HullMgr.InnerValueAtLastEdit)
    // with dt measured from that hull manager's own LastEditTime (see
    // IVP_Hull_Manager in ivp_real_object.h). In other words: how far the two
    // objects may still travel before this pair's cached mindist stops being
    // valid. Name is this port's own.
    float CombinedHullSpread;              // +0x5c..0x5f
    Vector SeparatingNormal;               // +0x60: normalized (SlotA.Position - SlotB.Position), or the raw
                                            // difference on the degenerate near-zero-length path (see SeparationGap).
    uint8_t _pad6c[0x04];                  // +0x6c..0x6f: still unconfirmed

    // FUN_0634db70 (.text:0634db70-0634dbf9, RET 4). Defined in
    // ivp_mindist_limiter.cpp.
    explicit IVP_Mindist_Base(IVP_Collision_Delegator* Delegator);

    // FUN_0634dca0 (+ deleting FUN_0634dc80, slot 4): nothing of its own
    // besides the synapse member destructors.
    ~IVP_Mindist_Base() override {}

    // Slot 1, LAB_0634de70 (.text:0634de70-0634de7f): Out[0] = +0x28, Out[1] = +0x44.
    void get_objects(IVP_Real_Object* Out[2]) override { Out[0] = synapse[0].l_obj; Out[1] = synapse[1].l_obj; }
    // Slot 2, LAB_0634de90: see GetLedges.
    void get_ledges(const IVP_Compact_Ledge* Out[2]) override { GetLedges(Out); }

    // LAB_0634de90 (.text:0634de90-0634dec1, RET 4): each synapse's ledge from its
    // tagged edge. Defined in ivp_mindist_limiter.cpp.
    void GetLedges(const IVP_Compact_Ledge* Out[2]) const;
};
#pragma pack(pop)
static_assert(offsetof(IVP_Mindist_Base, StateFlags) == 0x14, "IVP_Mindist_Base+0x14 (0634db70 :0634dbdb)");
static_assert(offsetof(IVP_Mindist_Base, synapse) == 0x18, "IVP_Mindist_Base::synapse[0] at +0x18 (0634db70 :0634dbb1)");
static_assert(offsetof(IVP_Mindist_Base, CombinedRadius) == 0x50, "IVP_Mindist_Base+0x50");
static_assert(offsetof(IVP_Mindist_Base, CombinedHullSpread) == 0x5c, "IVP_Mindist_Base+0x5c (0634db70 :0634dbde)");
static_assert(sizeof(IVP_Mindist_Base) == 0x70, "IVP_Ledge_Mindist's own fields start at +0x70 (0634dd10 :0634dd1f)");

#pragma pack(push, 4)
class IVP_Ledge_Mindist : public IVP_Mindist_Base
{
public:
    uint32_t LastDispatchTick;             // +0x70: RETYPED (D-item3192 follow-up pass, 2026-09-10) from
                                            // `IVP_Ledge_Mindist* NextInOwnerList`. The old declaration's only
                                            // citation was the ctor/dtor pair (FUN_0634dd10/dd60), and re-tracing
                                            // both settles this as WRONG: FUN_0634dd10 (the base ctor,
                                            // Common.dll.txt:423244-423245, `XOR EAX,EAX; MOV [ESI+0x70],EAX`) only
                                            // zero-initializes this field -- a plain zero-init proves nothing about
                                            // pointer-ness one way or the other -- and FUN_0634dd60 (the dtor,
                                            // Common.dll.txt:423282-423373, its ENTIRE body re-read this pass) never
                                            // touches +0x70 at all; the field is left completely alone by the
                                            // destructor, directly contradicting the old comment's "touched by
                                            // FUN_0634dd10/dd60" claim.
                                            // Meanwhile TWO independent sibling functions both use +0x70 as a raw
                                            // 32-bit per-tick cache-validity stamp, the identical idiom in each:
                                            //   FUN_06352cf0 (item 3192, Common.dll.txt:431646-431655): `EAX =
                                            //   ObjectA->environment->+0x110` (this class's own ObjectA at +0x28,
                                            //   ->environment at +0x18, an unnamed per-tick/per-frame counter IVP_
                                            //   Environment does not declare yet -- open, see item 3192's notes);
                                            //   `CMP [this+0x70],EAX; JNZ recompute` -- if equal, return 4 (already
                                            //   up to date this tick) WITHOUT recomputing; else `MOV [this+0x70],EAX`
                                            //   (stamp the new tick) and fall through to the real dispatch work.
                                            //   FUN_06352b90 (item 3191, this function's single-dispatch twin,
                                            //   Common.dll.txt:431528-431542): byte-for-byte the SAME
                                            //   ObjectA->environment->+0x110 read, the SAME `CMP [this+0x70],EAX;
                                            //   JNZ`/early-return-4 gate, the SAME `MOV [this+0x70],EAX` update.
                                            // Two independent call sites using the identical idiom is decisive:
                                            // +0x70 is a per-tick "already processed this simulation tick, skip the
                                            // expensive recompute" cache-stamp, not a linked-list pointer. Renamed
                                            // accordingly; whatever (if anything) retail uses for a real owner-list
                                            // link is unidentified and is NOT this field -- open for a future lane.
    // +0x74/+0x78: RESOLVED 2026-09-17 (lane claude:w26-simunit). Was the first
    // 8 bytes of `_pad74[0x0c]`. This mindist's node on
    // IVP_Mindist_Manager::ActiveMindistHead (+0x08 of that class). The
    // direction is settled by which link rewrites the head, not by symmetry:
    // Common.dll.txt:0634fee4-0634ff06 unlinks with
    //     EAX=[ESI+0x74]; if (EAX) [EAX+0x78]=[ESI+0x78]
    //     EAX=[ESI+0x78]; if (EAX) [EAX+0x74]=[ESI+0x74]
    //                     else     [EDI+0x08]=[ESI+0x74]     (EDI = the manager)
    // so only a node with a NULL +0x78 is the head -- +0x78 is prev, +0x74 next.
    IVP_Ledge_Mindist* ManagerNext;        // +0x74
    IVP_Ledge_Mindist* ManagerPrev;        // +0x78
    int32_t Field_0x7c;                    // +0x7c: zeroed by FUN_0634dd10 (:0634dd22); TODO: no reader traced yet.

    // FUN_0634dd10 (.text:0634dd10-0634dd38, RET 8): IVP_Mindist_Base's ctor
    // FUN_0634db70 first. Defined in ivp_mindist_limiter.cpp.
    IVP_Ledge_Mindist(IVP_Environment* Environment, IVP_Collision_Delegator* Delegator);

    // FUN_0634dd60 (.text:0634dd60-0634de53). Defined in ivp_mindist_limiter.cpp.
    ~IVP_Ledge_Mindist() override;

    // ---- IVP_Collision slots (retail vtable 063a55e0) ----
    // Slot 0, FUN_0634fe60: the argument is never read (RET 4), the body is
    // UpdatePerTick() below.
    void simulate_time_event(IVP_Environment* /*Environment*/) override { UpdatePerTick(); }
    // Slots 1 / 2 (get_objects / get_ledges) are IVP_Mindist_Base's, unchanged.

    // FUN_0634fa40 (.text:0634fa40-0634fd0a, RET 4), reached through synapse
    // slot 1 (0634f9e0). Defined in ivp_mindist_limiter.cpp.
    void hull_limit_exceeded_event(float HullIntrusionValue);
    // FUN_0634fa20 (.text:0634fa20-0634fa2e, RET 8), reached through synapse
    // slot 3 (0634f9c0): `FLD a; FSUB b; FADD [+0x5c]; FSTP [+0x5c]` -- the
    // difference stays on the x87 stack, so it is held as double here.
    void hull_manager_is_reset(float DeltaTime, float CenterDeltaTime)
    {
        CombinedHullSpread = (float)(((double)DeltaTime - CenterDeltaTime) + CombinedHullSpread);
    }
    // FUN_0634def0 (.text:0634def0-0634e067, RET 0x10). Files the two objects
    // and their start edges into the synapses, sums the objects' extra radii
    // into CombinedRadius and hands the mindist to the environment's mindist
    // manager. Defined in ivp_mindist_limiter.cpp.
    void init_mindist(IVP_Real_Object* Object0, IVP_Real_Object* Object1,
                      IVP_Compact_Edge* Edge0, IVP_Compact_Edge* Edge1);

    // FUN_06352cf0 (.text:06352cf0-06352e1b, thiscall, no arguments): stamps
    // +0x70 with the environment's step count (return 4 if already done this
    // step), runs IVP_Mindist_Minimize_Solver (P_Finish_Counter 0x14) through
    // mms_function_table, retries after proove_polypoly at most twice, and
    // ends in mindist_rescue_push unless the 0x1000 / 0x100000 kinds are set.
    // Returns the IVP_MRC_TYPE of the last handler. Defined in
    // ivp_mindist_limiter.cpp.
    int32_t DispatchNarrowPhaseSolve();

    // FUN_06352b90 (.text:06352b90-06352ce2, thiscall, no arguments): the
    // library's IVP_Mindist::recalc_mindist. Same per-tick stamp (+0x70) and
    // mms_function_table dispatch as DispatchNarrowPhaseSolve with
    // P_Finish_Counter 0 and without the retry: result 3 runs proove_polypoly
    // once and stops. Callers: 0634eb0a (insert_lazy_mindist), 06362d96,
    // 06345478. Defined in ivp_mindist_limiter.cpp.
    int32_t recalc_mindist();

    // Retail vtable slot 5 (063a55f4 = LAB_06352b10, .text:06352b10-06352b2a):
    // the library's mindist_rescue_push --
    // obj0->environment->anomaly_manager->inter_penetration(this, obj0, obj1)
    // (`CALL [vtbl+0x8]` on environment +0x20). IVP_Complex_Mindist's slot 5
    // (063a588c) is the shared `RET` 062a8920. Called through `CALL
    // [vtbl+0x14]` by 06352b90 (:06352cc7) and 06352cf0 (:06352dff). Defined
    // in ivp_mindist_limiter.cpp.
    virtual void mindist_rescue_push();

    // Retail vtable slot 6 (063a55f8 = 06325390 `XOR EAX,EAX; RET` here,
    // 063a5890 = 063624b0 `MOV EAX,1; RET` in IVP_Complex_Mindist): nonzero
    // for the recursive mindist. Called through `CALL [vtbl+0x18]` by
    // 06350460 (:063504ef) and 0634eb00 (:0634eb9c). Library name is_recursive
    // (cmn-phy5 2026-10-08).
    virtual int32_t is_recursive() { return 0; }

    // LAB_0634e9d0 (decomp-tracker item 3108), retail vtable slot 7 (063a55fc
    // == 063a55e0+0x1c; IVP_Complex_Mindist's slot 7 is LAB_06362920).
    // (.text:0634e9d0-0634e9ea, RET 4): the stack argument is the MINDIST
    // MANAGER, used as `this` of both calls:
    //   FUN_0634fec0(mm, this) = Manager->DetachMindist(this);
    //   FUN_0634e960(mm, this) = Manager->insert_invalid_mindist(this).
    // Was `AttachAsPendingSynapse(IVP_Complex_Mindist* Target)` forwarding to an
    // empty stub (cmn-phy5, 2026-10-08). Called by
    // IVP_Mindist_Manager::insert_exact_mindist (:0634eae5-:0634eaea).
    virtual void exact_mindist_went_invalid(IVP_Mindist_Manager* Manager);

    // Retail vtable slot 8 (063a5600 = FUN_06358cb0, .text:06358cb0-06358d54,
    // thiscall, plain RET): the library's IVP_Mindist::do_impact. Dispatched by
    // UpdatePerTick (`CALL [EAX+0x20]`, :0634fe99); IVP_Complex_Mindist's slot
    // 8 (063a5898) is LAB_06362650, which calls this body directly
    // (:06362797). Was the free function IVP_ResolveAmbiguousShapeWinner,
    // unreachable from UpdatePerTick's empty slot-8 stub (cmn-phy8
    // 2026-10-08). Defined in ivp_mindist_limiter.cpp.
    virtual void do_impact();

    // FUN_0634fe60 (decomp-tracker item 3124, slot 0 -- XREF 063a55e0/063a5878
    // directly, confirming this as the vtable's FIRST slot in both tables).
    // Full writeup above the definition in ivp_mindist_limiter.cpp: runs
    // DispatchNarrowPhaseSolve(), then (unless the "kind" bits 0xc000 are
    // already set) either defers via FUN_0634f320 (IVP_RescheduleCollisionEvent)
    // when StateFlags' low nibble is set, or compares SeparationGap against a
    // per-shape-pair threshold (DAT_0658aa18[idx] + DAT_0658ab34) to decide
    // between the virtual do_impact (slot 8) and the same FUN_0634f320
    // deferral with a different event mode.
    void UpdatePerTick();
};
#pragma pack(pop)
static_assert(offsetof(IVP_Ledge_Mindist, LastDispatchTick) == 0x70, "IVP_Ledge_Mindist+0x70 (0634dd10 :0634dd1f)");
static_assert(sizeof(IVP_Ledge_Mindist) == 0x80,
              "IVP_Ledge_Mindist must be retail's 0x80 bytes: IVP_Complex_Mindist's delegator vptr sits at +0x80");

// Derived: the library's IVP_Mindist_Recursive (retail 0x94 bytes, vtable
// 063a5878). 2026-10-08 (cmn-phy5): it is ALSO an IVP_Collision_Delegator --
// the second vptr at +0x80 (vtable 063a5868: 06362c00 / 06362ee0 / 06362ea0 /
// 06362ec0) -- and the delegator of every child mindist it spawns. Base order
// matters: IVP_Ledge_Mindist is exactly 0x80 bytes, so the delegator vptr
// lands at +0x80 and WinnerFlag at +0x84 as in retail.
#pragma pack(push, 4)
class IVP_Complex_Mindist : public IVP_Ledge_Mindist, public IVP_Collision_Delegator
{
public:
    int32_t WinnerFlag;                   // +0x84: 0/1, which of FeatureA/FeatureB "wins" (FUN_06362840/LAB_06362650/LAB_06362920); -1 from the ctor
    // +0x88: the spawned child collisions. Retail runs the generic IVP_U_Vector
    // code on it (memsize +0x88, n_elems +0x8a, elems +0x8c); its "inline
    // buffer" test compares elems against this+0x90 (:06362a77/:06362b93),
    // which is simply the next field.
    IVP_U_Vector<IVP_Collision> Children;
    // +0x90: spawned_mindist_count. FUN_06362ea0 adds to it (`[ECX+0x10]` with
    // ECX = this+0x80), FUN_06362ec0 returns it. Was misread as an inline
    // child slot ("InlineChild").
    int32_t SpawnedMindistCount;

    // FUN_06362ab0 (.text:06362ab0-06362b2a, RET 8).
    IVP_Complex_Mindist(IVP_Environment* Environment, IVP_Collision_Delegator* Delegator);
    ~IVP_Complex_Mindist() override;                                  // FUN_06362b50 (+ FUN_06362b30 wrapper)

    // ---- IVP_Collision_Delegator slots (sub-object vtable 063a5868) ----
    // Slot 0, LAB_06362c00: O(1) removal of a child from Children.
    void collision_is_going_to_be_deleted_event(IVP_Collision* Collision) override;
    // Slot 2, LAB_06362ea0.
    void change_spawned_mindist_count(int32_t Change) override;
    // Slot 3, FUN_06362ec0.
    int32_t get_spawned_mindist_count() override;

    // Vtable-slot methods (PTR_FUN_063a5878, decomp-tracker vtable id 160).
    // Slots 0-3 (FUN_0634fe60/LAB_0634de70/de90/LAB_0634dc10) are outside
    // this task's 21-item scope; slot 4 is the scalar deleting destructor.

    // "Recalculate winning feature" family -- three sibling overrides with
    // the exact same `this` layout, selected by which concrete synapse
    // pair the mindist was spawned for:
    // FUN_06362840 (RET 4): tagged-pointer (IVP_Compact_Ledge) variant. The
    // stack argument (the mindist manager, EBX of insert_lazy_mindist at
    // :0634eba3) is never read.
    void RecalcWinnerLedgePair(IVP_Mindist_Manager* Manager);
    // LAB_06362650: retail vtable slot 8 of 063a5878 (063a5898), this class's
    // do_impact override -- the 2-level switch on the two synapse status
    // words; falls back to IVP_Ledge_Mindist::do_impact (06358cb0, called
    // directly at :06362797). Was RecalcWinnerByShapeType, never reached
    // (cmn-phy8 2026-10-08). Per-case trace above its definition in
    // ivp_mindist_limiter.cpp.
    void do_impact() override;
    // Slot 5, 063a588c = 062a8920 (`RET`, ICF-shared): the recursive mindist
    // does not push.
    void mindist_rescue_push() override {}

    // Slot 6, 063624b0 (`MOV EAX,1; RET`).
    int32_t is_recursive() override { return 1; }

    // LAB_06362920: retail vtable slot 7 of 063a5878, i.e. this class's
    // exact_mindist_went_invalid override (stack argument = the mindist
    // manager, RET 4). Defined in ivp_mindist_limiter.cpp.
    void exact_mindist_went_invalid(IVP_Mindist_Manager* Manager) override;

    void SpawnOrDeferChild(float Radius); // FUN_06362c70 (RET 4): the capped spawn step; Radius = R0 + R1 of the range manager
    void DeactivateToSimple();         // FUN_06362d60: drop back out of complex tracking
    void RecalcAndMaybeDeactivate();   // FUN_06362d90: recompute + conditionally call DeactivateToSimple

    void ClearChildren();              // FUN_06362a30: teardown helper (also used by the real dtor)
    // (SidePoolA / SidePoolB removed 2026-10-08, cmn-phy5: the port allocated
    // two private priority pools here; retail has none -- its synapses at
    // +0x18 / +0x34 are entries of their OBJECTS' hull-manager pools,
    // object +0x64, see IVP_Synapse_Real.)
};
#pragma pack(pop)

// (IVP_SpawnLedgeMindist, the FUN_0634e0b0 lookalike, removed 2026-10-08 by
// cmn-ivp2c: SpawnOrDeferChild calls the real IVP_Create_Exact_Mindists,
// src/IVP/ivp_collision/ivp_mindist_create.h.)

// Retail FUN_06360c80 (the object-level mindist factory) is
// IVP_Collision_Delegator_Root_Mindist::delegate_collisions_for_object, slot 5
// of vtable 063a5760 (ivp_collision.h / ivp_ov_element.cpp). The free-function
// lookalike IVP_SpawnObjectMindist that stood here was removed 2026-10-08.
