// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior
#include "common_ini.h"       // Vector

#include <cstddef>

// ============================================================================
// DrasticEvadeBehavior -- AI "drastic evade" directive/behavior (Behavior family)
// ============================================================================
//
// RTTI: COL .rdata:063a8e4c (offset 0), TypeDescriptor .data:063ebeb4
// ".?AVDrasticEvadeBehavior@@", ClassHierarchyDescriptor 063a8e3c with two bases
// (DrasticEvadeBehavior, Behavior) -- a single-inheritance child of Behavior.
//
// Constructor FUN_062c8730 (Common.dll.txt 195469-195488): FUN_062d2020(TypeId),
// +0x40 = 0, vptr 0639f8ec, +0x30 = 4.0f, +0x34/+0x38/+0x3c = 0.
//
// Vtable .rdata:0639f8ec, 27 slots. Overrides of Behavior's table (raw .rdata dwords,
// 2026-10-08):
//    0  FUN_062c7090  scalar-deleting dtor, ICF-shared by 7 behavior vtables
//    1  LAB_062cf3e0  `JMP FUN_062d2090` -- Behavior::BindContext
//    3  FUN_062c8790  Slot3
//    4  FUN_062c87b0  Slot4
//    5  LAB_062c8760  Slot5
//    7  LAB_062c8800  Slot7
//   10  LAB_062c87c0  Slot10
// Every other slot is Behavior's own entry.
class DrasticEvadeBehavior : public Behavior
{
public:
    // Retail's ctor argument is the behavior type id (18, "DrasticEvade"), pushed by the factory out of
    // the descriptor's +0x04 -- see Behavior::GetTypeId() in common_behavior.h.
    explicit DrasticEvadeBehavior(int InTypeId);
    ~DrasticEvadeBehavior() override;

    // Slot 1, LAB_062cf3e0 (Common.dll.txt 205851-205852): `JMP FUN_062d2090`.
    bool SetContext(void* Owner, void* Manager, void* Sys) override;

    // Slot 3, FUN_062c8790 (Common.dll.txt 195521-195530): FUN_062d20d0, then +0x2c = 0.
    void Slot3() override;

    // Slot 4, FUN_062c87b0 (Common.dll.txt 195547-195556): FUN_062d20f0, then
    // Flags28 |= 0x2.
    void Slot4() override;

    // Slot 5, LAB_062c8760 (Common.dll.txt 195491-195507), `RET 4`, argument unread.
    // With Flags28 bit 0x2: 3 when Flag29 is set, else 0. Without it: 3 when Flag29 is
    // set, else 2. Stored at +0x0c and returned.
    int Slot5(float Unused) override;

    // Slot 7, LAB_062c8800 (Common.dll.txt 195590-195666). Switch on Kind40:
    //   0 -> timer += delta; owner's IObjAffect::set_nudge_vec({Offset.x, Offset.y, 0})
    //        while timer < duration (result 1), else set_nudge_vec({0,0,0}) (result 2);
    //   1 -> timer += delta; Behavior_UpdateCruiseGate(Sys+0x18, -1.0f or 0.0f, 0),
    //        result 1 / 2 the same way;
    //   other -> result 3.
    int Slot7() override;

    // Slot 10, LAB_062c87c0 (Common.dll.txt 195559-195584), `RET 4`: takes a
    // pub::AI::DirectiveDrasticEvadeOp-shaped op: Kind40 = Op+0xc; when it is 0,
    // Offset = Op+0x10..+0x18; Flags28 |= 0x2; Duration30 = Op+0x1c; returns true.
    bool Slot10(void* DirectiveOp) override;

public:
    // +0x04..+0x29 is Behavior's own block (see common_behavior.h), still declared here
    // until the family-wide hoist.
    int TypeId;  // +0x04. The type id retail's shared base ctor FUN_062d2020 parks here.
    unsigned CachedHandle08;   // +0x08. IObjInspect::get_id() of the owner (the binder).
    int ModeSelector0xc;       // +0x0c. Slot5's result.
    int UpdateResult10;        // +0x10. Slot7's result.
    void* OwnerContextPtr14;   // +0x14. The owner IObjRW* (the binder).
    unsigned char Opaque18[0x1c - 0x18];
    float DeltaTime1c;         // +0x1c. Per-tick delta the manager stores before Slot7.
    void* BehaviorMgrPtr20;    // +0x20. The IBehaviorManager (the binder); only the shared
                               // FUN_062d20f0 reads it in this class.
    SystemManager* NavHelperPtr24; // +0x24. The SystemManager (the binder).
    unsigned char Flags28;     // +0x28.
    unsigned char Flag29;      // +0x29.
    unsigned char Pad2a[0x2c - 0x2a];

    float StateTimer2c;        // +0x2c. Slot7 accumulates the delta; Slot3 zeroes it. Not
                               // written by the ctor (retail leaves it to Slot3).
    float DurationThreshold30; // +0x30. Ctor 4.0f; Slot10 copies Op+0x1c.
    Vector TargetOffset;       // +0x34. Ctor zero; Slot10 copies Op+0x10..+0x18 when the kind is 0.
    int TargetKind40;          // +0x40. Ctor 0; Slot10 copies Op+0xc; Slot7 switches on it.
};

static_assert(offsetof(DrasticEvadeBehavior, CachedHandle08) == 0x08, "DrasticEvadeBehavior::CachedHandle08 offset");
static_assert(offsetof(DrasticEvadeBehavior, ModeSelector0xc) == 0x0c, "DrasticEvadeBehavior::ModeSelector0xc offset");
static_assert(offsetof(DrasticEvadeBehavior, UpdateResult10) == 0x10, "DrasticEvadeBehavior::UpdateResult10 offset");
static_assert(offsetof(DrasticEvadeBehavior, OwnerContextPtr14) == 0x14, "DrasticEvadeBehavior::OwnerContextPtr14 offset");
static_assert(offsetof(DrasticEvadeBehavior, DeltaTime1c) == 0x1c, "DrasticEvadeBehavior::DeltaTime1c offset");
static_assert(offsetof(DrasticEvadeBehavior, BehaviorMgrPtr20) == 0x20, "DrasticEvadeBehavior::BehaviorMgrPtr20 offset");
static_assert(offsetof(DrasticEvadeBehavior, NavHelperPtr24) == 0x24, "DrasticEvadeBehavior::NavHelperPtr24 offset");
static_assert(offsetof(DrasticEvadeBehavior, Flags28) == 0x28, "DrasticEvadeBehavior::Flags28 offset");
static_assert(offsetof(DrasticEvadeBehavior, Flag29) == 0x29, "DrasticEvadeBehavior::Flag29 offset");
static_assert(offsetof(DrasticEvadeBehavior, StateTimer2c) == 0x2c, "DrasticEvadeBehavior::StateTimer2c offset");
static_assert(offsetof(DrasticEvadeBehavior, DurationThreshold30) == 0x30, "DrasticEvadeBehavior::DurationThreshold30 offset");
static_assert(offsetof(DrasticEvadeBehavior, TargetOffset) == 0x34, "DrasticEvadeBehavior::TargetOffset offset");
static_assert(offsetof(DrasticEvadeBehavior, TargetKind40) == 0x40, "DrasticEvadeBehavior::TargetKind40 offset");
// Retail object size: the factory pushes 0x44 before operator new.
static_assert(sizeof(DrasticEvadeBehavior) == 0x44, "DrasticEvadeBehavior size must match retail 0x44");
