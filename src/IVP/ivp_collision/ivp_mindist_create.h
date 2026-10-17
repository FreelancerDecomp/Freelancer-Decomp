// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstdint>

#include "../ivp_utility/ivp_u_vector.h" // IVP_U_Vector

class IVP_Real_Object;
class IVP_Compact_Ledge;
class IVP_Collision;
class IVP_Collision_Delegator;

// ============================================================================
// FUN_0634e0b0 (.text:0634e0b0-0634e7cc, cdecl, 9 arguments) -- the library's
// IVP_Mindist_Manager::create_exact_mindists (cmn-ivp2b, 2026-10-08). Kept a
// free function: the library member is static (no `this`), and the port's
// IVP_Mindist_Manager is declared in another lane's header.
//
// Re-synchronises `Mindists` (an IVP_U_Vector of IVP_Collision records whose
// two back-indices +0x0c/+0x10 are kept in step, the "fvector" convention)
// with the set of ledge pairs that are now within `ScanRadius` of each other:
// existing records whose ledge pair is still wanted are kept (compacted to the
// front), the rest are deleted, and a new IVP_Ledge_Mindist (0x80) or
// IVP_Complex_Mindist (0x94) is created for every new pair, with `Delegator`
// as its delegator.
//
// Ledge sources per side: the side's single ledge if given (SingleLedge0/1),
// otherwise the object's surface manager slot 4 (get_all_ledges_within_radius)
// around the OTHER object's predicted core position, in the object's own space,
// with RootLedge0/1 as the root and the other side's single ledge as the
// reference ledge.
//
// Callers: the OO watcher (06369a62: obj0, obj1, radius, &watcher->mindists,
// 0, 0, 0, 0, watcher+0x14) and IVP_Complex_Mindist (06362d38).
// ============================================================================
void IVP_Create_Exact_Mindists(IVP_Real_Object* Object0, IVP_Real_Object* Object1, float ScanRadius,
                               IVP_U_Vector<IVP_Collision>* Mindists,
                               const IVP_Compact_Ledge* SingleLedge0, const IVP_Compact_Ledge* SingleLedge1,
                               const IVP_Compact_Ledge* RootLedge0, const IVP_Compact_Ledge* RootLedge1,
                               IVP_Collision_Delegator* Delegator);

// The {table, mask, heap flag} open-addressing table of int16 record indices
// both FUN_0634e0b0 (local +0x2c) and IVP_Mindist_Manager::RecheckOvElement
// (FUN_0634ebd0) keep on the stack. Its destructor is FUN_06350560 (run by the
// unwind funclets Unwind@06397f96 / Unwind@06397feb; the normal paths inline
// the same test): free the table only when it was heap allocated and is
// non-null, then null it. Name is this port's own.
struct FCollisionIndexTable
{
    int16_t* Table = nullptr; // +0x00
    int32_t Mask = 0;         // +0x04
    int32_t bHeap = 0;        // +0x08

    ~FCollisionIndexTable();
};

// FUN_063505a0 (.text:063505a0-063505de, thiscall on the vector, RET 8): swap
// two slots of a collision fvector and repair each moved record's back-index
// (+0x0c if it equals the old slot, else +0x10). Callers: 0634e57f (above)
// and 0634ef99 (IVP_Mindist_Manager::RecheckOvElement).
void IVP_SwapCollisionSlots(IVP_U_Vector<IVP_Collision>* Vector, int32_t IndexA, int32_t IndexB);
