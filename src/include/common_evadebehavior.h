// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior
#include "common_goods.h"     // BaseWatcher
#include "common_idea.h"      // DodgeIdea, BreakAwayIdea, DodgeMissileIdea, FaceIdea, AfterburnerIdea

#include <cstddef>

struct IObjRW;  // common_iface.h -- get_target()'s result

// ============================================================================
// EvadeBehavior -- behavior type 5 ("Evade"), factory body .text:062d0e30 (`PUSH 0x2c0;
// CALL operator_new; ... CALL FUN_062c7790`). RTTI: COL .rdata:063a8e04 at vftable-4 names
// ".?AVEvadeBehavior@@"; its ClassHierarchyDescriptor (063a8df4, numBaseClasses 2) is
// [EvadeBehavior, Behavior], single inheritance at offset 0.
//
// Re-ported from the listing 2026-10-08 (claude-code:cmn-ai4). Until then the five
// sub-objects were opaque byte arrays and almost every call into them, into the
// SystemManager and into the personality was a documented no-op.
//
// Constructor FUN_062c7790 builds, in this order (EH states 0..4):
//   +0x030  DodgeIdea         FUN_062d77d0
//   +0x110  BreakAwayIdea     FUN_062d8970
//   +0x1ac  DodgeMissileIdea  FUN_062d94b0
//   +0x1cc  FaceIdea          FUN_062d6750
//   +0x28c  AfterburnerIdea   FUN_062da500
// then the bare BaseWatcher at +0x2a0 (EH state 6), the vptr 0639f874 and its own scalars.
//
// Vtable .rdata:0639f874, 27 slots. Overrides of Behavior's table (vtd.py, raw .rdata):
//    0  FUN_062c78e0  scalar-deleting dtor around FUN_062c7900
//    1  LAB_062c79d0  SetContext
//    3  LAB_062c8260  Slot3
//    4  FUN_062c85e0  Slot4
//    5  LAB_062c7ab0  Slot5
//    7  LAB_062c7d60  Slot7
//    8  FUN_062c7d00  Slot8
//   10  LAB_062c8630  Slot10
//   21  LAB_062c7870  Slot21
//   24  LAB_062c78c0  get_target
// Every other slot is Behavior's own entry.
// ============================================================================
class EvadeBehavior : public Behavior
{
public:
    // FUN_062c7790 (.text:062c7790-062c786d): FUN_062d2020(TypeId), the five ideas, the
    // +0x2a0 watcher zeroed, vptr, +0x2b4/+0x2b5/+0x2b6/+0x2b7 = 0, Mode2c = 0, +0x2b8 = 0,
    // +0x2ac = 3.0f, +0x2bc = 0x3f34fdf4 (0.707f).
    explicit EvadeBehavior(int InTypeId);
    // FUN_062c7900: set_pointer(0) on +0x2a0, +0x1e0 (FaceIdea's watcher), +0x1c0
    // (DodgeMissileIdea's), +0x138 (BreakAwayIdea's picker), +0x58 (DodgeIdea's picker) --
    // the member destructors, in reverse construction order -- then FUN_062d2080.
    ~EvadeBehavior() override;

    EvadeBehavior(EvadeBehavior const&) = delete;
    EvadeBehavior& operator=(EvadeBehavior const&) = delete;

    // Slot 1, LAB_062c79d0 (RET 0xc): FUN_062d2090; on success each idea's slot 0 with
    // (Owner, Sys, Manager) in member order, DodgeIdea seeded ConeAngle = pi/2,
    // DodgeDistance = 50.0f, IntervalTime = 6.0f right after its own SetContext, then +0x2a8 = 10.0f and
    // +0x2b0 = 0. Returns the binder's result.
    bool SetContext(void* Owner, void* Manager, void* Sys) override;

    // Slot 3, LAB_062c8260: re-arms the evade from the personality's EvadeDodge /
    // EvadeBreak blocks (see the .cpp).
    void Slot3() override;

    // Slot 4, FUN_062c85e0: Flag2b5 = 0, Commit on Dodge, BreakAway, DodgeMissile and
    // Afterburner (not Face), then `JMP FUN_062d20f0` (Behavior::Slot4).
    void Slot4() override;

    // Slot 5, LAB_062c7ab0 (RET 4, argument unread). Result stored at +0x0c.
    int Slot5(float Unused) override;

    // Slot 7, LAB_062c7d60 (391 instructions). Result stored at +0x10.
    int Slot7() override;

    // Slot 8, FUN_062c7d00 (RET 0).
    void Slot8() override;

    // Slot 10, LAB_062c8630 (RET 4): watch the op's target (FUN_062e74a0 on Sys+0x34) or,
    // with no op, the closest hostile (FindClosestHostileTarget); Flags28 |= 2; true.
    bool Slot10(void* DirectiveOp) override;

    // Slot 21, LAB_062c7870 (RET 4): drop the watched object when its id is Id.
    void Slot21(void* Arg) override;

    // Slot 24, LAB_062c78c0: the watched object (Watched - 8) or null.
    IObjRW const* get_target() override;

    // FUN_062c8690 (RET 0, non-virtual, one caller .text:062c8417): a rand()-weighted pick
    // 0..3 over the personality's EvadeDodge Field_04..Field_10.
    int PickDodgeStyle();

    // ---- Behavior's own +0x04..+0x29 block, still declared per class ----
    int TypeId;                  // +0x04
    unsigned CachedHandle08;     // +0x08
    int ModeSelector0xc;         // +0x0c, Slot5's result.
    int UpdateResult10;          // +0x10, Slot7's result.
    void* OwnerContext;          // +0x14, the owner IObjRW* (binder).
    unsigned char Opaque18[0x1c - 0x18];
    float DeltaTime;             // +0x1c
    void* BehaviorMgrPtr;        // +0x20, the IBehaviorManager (binder).
    SystemManager* NavHelper;    // +0x24, the SystemManager (binder).
    unsigned char Flags28;       // +0x28
    unsigned char Flag29;        // +0x29
    unsigned char Pad2a[0x2c - 0x2a];

    // ---- EvadeBehavior's own fields ----
    int Mode2c;                  // +0x2c. Slot7's mode 1..4; ctor and Slot3 zero it.
    DodgeIdea Dodge;             // +0x30
    BreakAwayIdea BreakAway;     // +0x110
    DodgeMissileIdea DodgeMissile; // +0x1ac
    FaceIdea Face;               // +0x1cc
    AfterburnerIdea Afterburner; // +0x28c
    BaseWatcher Watcher2a0;      // +0x2a0, the object being evaded.
    float DodgeTime2a8;          // +0x2a8. SetContext 10.0f; Slot3: EvadeDodge.Field_30.
    float BreakTime2ac;          // +0x2ac. Ctor 3.0f; Slot3: EvadeBreak.Field_00.
    float Elapsed2b0;            // +0x2b0. Slot7 accumulates DeltaTime; SetContext/Slot3 zero
                                 // it; Slot5/Slot8 set it to DodgeTime2a8.
    unsigned char Flag2b4;       // +0x2b4. Slot5/Slot8: Timers[0].Active latch.
    unsigned char Flag2b5;       // +0x2b5. Slot8: Timers[1].Active latch; Slot4 clears it.
    unsigned char Flag2b6;       // +0x2b6. Ctor 0; no other writer in the listing.
    unsigned char Flag2b7;       // +0x2b7. Ctor 0; no other writer in the listing.
    float ConeCos2b8;            // +0x2b8. Ctor 0; Slot3 -cos(pi/3) when the dodge style is 2.
    float ConeCos2bc;            // +0x2bc. Ctor 0x3f34fdf4.
};

static_assert(offsetof(EvadeBehavior, TypeId) == 0x04, "EvadeBehavior::TypeId offset");
static_assert(offsetof(EvadeBehavior, ModeSelector0xc) == 0x0c, "EvadeBehavior::ModeSelector0xc offset");
static_assert(offsetof(EvadeBehavior, UpdateResult10) == 0x10, "EvadeBehavior::UpdateResult10 offset");
static_assert(offsetof(EvadeBehavior, OwnerContext) == 0x14, "EvadeBehavior::OwnerContext offset");
static_assert(offsetof(EvadeBehavior, DeltaTime) == 0x1c, "EvadeBehavior::DeltaTime offset");
static_assert(offsetof(EvadeBehavior, BehaviorMgrPtr) == 0x20, "EvadeBehavior::BehaviorMgrPtr offset");
static_assert(offsetof(EvadeBehavior, NavHelper) == 0x24, "EvadeBehavior::NavHelper offset");
static_assert(offsetof(EvadeBehavior, Flags28) == 0x28, "EvadeBehavior::Flags28 offset");
static_assert(offsetof(EvadeBehavior, Flag29) == 0x29, "EvadeBehavior::Flag29 offset");
static_assert(offsetof(EvadeBehavior, Mode2c) == 0x2c, "EvadeBehavior::Mode2c offset");
static_assert(offsetof(EvadeBehavior, Dodge) == 0x30, "EvadeBehavior::Dodge offset");
static_assert(offsetof(EvadeBehavior, BreakAway) == 0x110, "EvadeBehavior::BreakAway offset");
static_assert(offsetof(EvadeBehavior, DodgeMissile) == 0x1ac, "EvadeBehavior::DodgeMissile offset");
static_assert(offsetof(EvadeBehavior, Face) == 0x1cc, "EvadeBehavior::Face offset");
static_assert(offsetof(EvadeBehavior, Afterburner) == 0x28c, "EvadeBehavior::Afterburner offset");
static_assert(offsetof(EvadeBehavior, Watcher2a0) == 0x2a0, "EvadeBehavior::Watcher2a0 offset");
static_assert(offsetof(EvadeBehavior, DodgeTime2a8) == 0x2a8, "EvadeBehavior::DodgeTime2a8 offset");
static_assert(offsetof(EvadeBehavior, BreakTime2ac) == 0x2ac, "EvadeBehavior::BreakTime2ac offset");
static_assert(offsetof(EvadeBehavior, Elapsed2b0) == 0x2b0, "EvadeBehavior::Elapsed2b0 offset");
static_assert(offsetof(EvadeBehavior, Flag2b4) == 0x2b4, "EvadeBehavior::Flag2b4 offset");
static_assert(offsetof(EvadeBehavior, Flag2b7) == 0x2b7, "EvadeBehavior::Flag2b7 offset");
static_assert(offsetof(EvadeBehavior, ConeCos2b8) == 0x2b8, "EvadeBehavior::ConeCos2b8 offset");
static_assert(offsetof(EvadeBehavior, ConeCos2bc) == 0x2bc, "EvadeBehavior::ConeCos2bc offset");
static_assert(sizeof(EvadeBehavior) == 0x2c0, "EvadeBehavior size must match retail 0x2c0");
