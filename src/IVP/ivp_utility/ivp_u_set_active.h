// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstdint>

#include "ivp_u_vector.h"
#include "ivp_vhash.h"

// ============================================================================
// IVP_U_Set_Active<T> -- the IVP SDK's pointer set with change listeners
// (claude-code:cmn-ivp3d, 2026-10-08).
//
// Retail builds three instantiations, all inside IVP_Controller_Phantom
// (FUN_06344710): T = IVP_Mindist_Base (embedded at phantom +0x10),
// IVP_Real_Object (heap, phantom +0x28) and IVP_Core (heap, phantom +0x34).
// Each instantiation has TWO vtables, 2 slots each {compare, scalar deleting
// dtor}, stored in this order by every constructor (:0634474e-:06344767,
// :0634479e-:063447b7, :06344827-:06344840):
//   IVP_VHash ctor FUN_063522a0 (vtable 063a5648, abstract compare)
//   -> vptr := intermediate vtable  (IVP_U_Set_Active_Base<T>)
//   -> listeners vector ctor FUN_06344cf0(0)
//   -> vptr := final vtable         (IVP_U_Set_Active<T>)
// so there is a member-less class layer between IVP_VHash and the set. The
// destructors (FUN_06344a00 / 06344b50 / 06344c30) store the vtables in the
// reverse order. Tables (.rdata, compare 06344ab0 in every one):
//   T                 intermediate (slot 1)     final (slot 1)
//   IVP_Mindist_Base  063a5408 (06344b10)      063a5410 (06344af0 -> 06344a00)
//   IVP_Core          063a5420 (06344cd0)      063a5418 (06344c10 -> 06344c30)
//   IVP_Real_Object   063a5430 (06344bf0)      063a5428 (06344b30 -> 06344b50)
// The intermediate destructors (06344aa0 / 06344ae0 / 06344ad0) only store
// their vtable and jump to IVP_VHash's FUN_063522f0.
// No RTTI: the dword before each vtable is data (063a5404 = 0.01f). The class
// name IVP_U_Set_Active and the listener interface IVP_Listener_Set_Active<T>
// are the SDK's (the latter's RTTI type descriptor exists for T =
// IVP_Real_Object at .data:063eb240); the intermediate layer's name is this
// port's own.
//
// Layout (0x18 bytes, operator new(0x18) at :06344783 / :0634480c):
//   +0x00 vptr, +0x04..+0x0f IVP_VHash fields,
//   +0x10 listeners (IVP_U_Vector: memsize +0x10, n_elems +0x12, elems +0x14;
//         the destructor's inline-buffer test compares elems with +0x18).
// ============================================================================

template <class T>
class IVP_U_Set_Active;

// The SDK's IVP_Listener_Set_Active<T>: three pure slots, no destructor slot.
// Every retail call site pushes the set first:
//   slot 0 element_added(set, elem)          06344449 `PUSH EBX; PUSH EBP; CALL [EDX]`
//   slot 1 element_removed(set, elem)        063445c9 `CALL [EDX+0x4]`
//   slot 2 pset_is_going_to_be_deleted(set)  06344a3f `PUSH ESI; CALL [EDX+0x8]`
template <class T>
class IVP_Listener_Set_Active
{
public:
    virtual void element_added(IVP_U_Set_Active<T>* Set, T* Elem) = 0;
    virtual void element_removed(IVP_U_Set_Active<T>* Set, T* Elem) = 0;
    virtual void pset_is_going_to_be_deleted(IVP_U_Set_Active<T>* Set) = 0;
};

// The pointer hash every IVP_U_Set_Active call site computes inline before
// FUN_063523d0 / FUN_06352480 (06344407-06344424):
//   ((int32)(Key * 1001) >> 16) + Key * 75, top bit set.
// The shift is SAR (arithmetic) on the 32-bit product.
inline unsigned IVP_U_Set_Active_Hash_Index(const void* Elem)
{
    const uint32_t Key = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Elem));
    const uint32_t Spread = static_cast<uint32_t>(static_cast<int32_t>(Key * 1001u) >> 16);
    return (Spread + Key * 75u) | 0x80000000u;
}

// The member-less intermediate layer (port-local name, see the file head).
template <class T>
class IVP_U_Set_Active_Base : public IVP_VHash
{
public:
    explicit IVP_U_Set_Active_Base(int Size)
        : IVP_VHash(Size)
    {
    }

    // slot 0, LAB_06344ab0 (RET 8): `XOR EAX,EAX; CMP ECX,EDX; SETZ AL` --
    // pointer equality.
    int Compare(void* Elem0, void* Elem1) override
    {
        return (Elem0 == Elem1) ? 1 : 0;
    }

    // slot 1: 06344b10 / 06344cd0 / 06344bf0 (scalar deleting) over
    // 06344aa0 / 06344ae0 / 06344ad0 -- nothing beyond ~IVP_VHash.
};

template <class T>
class IVP_U_Set_Active : public IVP_U_Set_Active_Base<T>
{
public:
    IVP_U_Vector<IVP_Listener_Set_Active<T>> listeners; // +0x10

    // Inlined in FUN_06344710 at each construction (see the file head):
    // IVP_VHash(Size), then the listener vector with size 0 (FUN_06344cf0).
    explicit IVP_U_Set_Active(int Size)
        : IVP_U_Set_Active_Base<T>(Size)
        , listeners(0)
    {
    }

    // FUN_06344a00 / 06344b50 / 06344c30: every listener, last first, gets
    // slot 2 with this set (:06344a25-:06344a43); then the listener vector is
    // released (member destructor, the inline-buffer test at :06344a4a) and
    // the base layers run (:06344a77-:06344a7d).
    ~IVP_U_Set_Active() override
    {
        for (int32_t I = static_cast<int32_t>(listeners.n_elems) - 1; I >= 0; --I)
        {
            listeners.elems[I]->pset_is_going_to_be_deleted(this);
        }
    }

    // Inlined by every adder (06344400 :06344407-:0634444c): insert, then
    // slot 0 on every listener, last first (the count is read once, the
    // element array on every pass).
    void add_element(T* Elem)
    {
        this->AddElem(Elem, IVP_U_Set_Active_Hash_Index(Elem));
        for (int32_t I = static_cast<int32_t>(listeners.n_elems) - 1; I >= 0; --I)
        {
            listeners.elems[I]->element_added(this, Elem);
        }
    }

    // Inlined by every remover (06344580 :06344587-:063445cd): remove
    // (FUN_06352480, which writes to address 0 when Elem is absent), then
    // slot 1 on every listener, last first.
    void remove_element(T* Elem)
    {
        this->RemoveElem(Elem, IVP_U_Set_Active_Hash_Index(Elem));
        for (int32_t I = static_cast<int32_t>(listeners.n_elems) - 1; I >= 0; --I)
        {
            listeners.elems[I]->element_removed(this, Elem);
        }
    }
};
