// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_ov_element.h"

#include "ivp_core.h"
#include "ivp_environment.h"
#include "ivp_real_object.h"

#include "ivp_mindist.h" // IVP_Ledge_Mindist / IVP_Complex_Mindist (delegate_collisions_for_object)
#include "ivp_listener.h" // IVP_Collision_Filter (environment->collision_filter)
#include "ivp_cluster_manager.h" // IVP_Universe_Manager (environment->universe_manager)
#include "../ivp_compact_builder/ivp_compact_ledge.h"
#include "../ivp_collision/ivp_oo_watcher.h" // IVP_OO_Watcher (delegate_collisions_for_object)
#include "../ivp_utility/ivp_core_trap.h" // IVP_CoreTrap (LAB_0634dc00)

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

// ============================================================================
// Lane claude:w18-ivp, 2026-09-17; OV-tree half completed by lane
// claude:w19-ovtree the same day.
//
// Address ranges verified instruction by instruction for this file:
//   FUN_0635c020   Common.dll.txt:0635c020-0635c06a   (IVP_OV_Element ctor,     item 3330)
//   FUN_0635c070   Common.dll.txt:0635c070-0635c08b   (deleting dtor thunk,     item 3331)
//   FUN_0635c090   Common.dll.txt:0635c090-0635c127   (real dtor,               item 3332)
//   FUN_0635c130   Common.dll.txt:0635c130-0635c1d1   (hull re-key,             item 3333)
//   FUN_0634ebd0   Common.dll.txt:0634ebd0-0634f13a   (RecheckOvElement,        item 3111)
//   FUN_0634f140   Common.dll.txt:0634f140-0634f172   (partner hash,            item 3112)
//   FUN_0634f180   Common.dll.txt:0634f180-0634f204   (RebuildOvElement,        item 3113)
//   FUN_0635d2e0   Common.dll.txt:0635d2e0-0635d358   (remove_ov_element,       item 3353)
//   FUN_063505a0   Common.dll.txt:063505a0-063505de   (collision slot swap)
// ---- added by claude:w19-ovtree, every one read from first byte to RET ----
//   FUN_0635c2c0   Common.dll.txt:0635c2c0-0635c300   (IVP_OV_Node ctor,        item 3337)
//   FUN_0635c310   Common.dll.txt:0635c310-0635c40e   (IVP_OV_Node dtor,        item 3338)
//   FUN_0635c590   Common.dll.txt:0635c590-0635c615   (cell_contains,           item 3343)
//   FUN_0635c620   Common.dll.txt:0635c620-0635c8c1   (compute_query_cell,      item 3344)
//   FUN_0635c8d0   Common.dll.txt:0635c8d0-0635c972   (cell_overlaps,           item 3345)
//   FUN_0635c980   Common.dll.txt:0635c980-0635c9cd   (find_deepest_container,  item 3346)
//   FUN_0635c9d0   Common.dll.txt:0635c9d0-0635cbc9   (link_node_under,         item 3347)
//   FUN_0635cbd0   Common.dll.txt:0635cbd0-0635cdc9   (grow_root,               item 3348)
//   FUN_0635cdd0   Common.dll.txt:0635cdd0-0635ce80   (collect_subtree,         item 3349)
//   FUN_0635ce90   Common.dll.txt:0635ce90-0635d021   (collect_touching,        item 3350)
//   FUN_0635d030   Common.dll.txt:0635d030-0635d25e   (insert_ov_element,       item 3351)
//   FUN_0635d270   Common.dll.txt:0635d270-0635d2d3   (collapse_empty_node,     item 3352)
//   FUN_0635d360   Common.dll.txt:0635d360-0635d3a8   (node_cell_bounds,        item 3354)
//   FUN_06369200   Common.dll.txt:06369200-0636922f   (IVP_OV_HashNodeKey,      item 3594)
// ============================================================================

namespace
{
    // The multiplicative spread every hash site in this cluster shares:
    // `EDX = h >> 8 (arithmetic); EAX = EDX << 10; EAX -= EDX; EAX += h`,
    // i.e. (h >> 8) * 0x3ff + h. Appears inline at Common.dll.txt:0634edee-
    // :0634edfa and :0634eee9-:0634eef3, and as the tail of FUN_0634f140.
    // Returned UNMASKED at every one of those three sites.
    inline uint32_t SpreadHash(uint32_t Value)
    {
        const int32_t Shifted = static_cast<int32_t>(Value) >> 8;
        return static_cast<uint32_t>(Shifted * 0x3ff) + Value;
    }

    // FUN_063505a0 (Common.dll.txt:063505a0-063505de). Swaps two slots of an
    // IVP_OV_Element::collision_fvector and repairs each moved record's own
    // "which slot am I in" back-index. Each IVP_Collision carries two such
    // indices (one per participating element); retail picks whichever of the
    // two currently equals the old slot, which is how it works without
    // knowing which side of the pair this vector belongs to.
    void SwapCollisionSlots(IVP_U_Vector<IVP_Collision>* Vector, int32_t IndexA, int32_t IndexB)
    {
        IVP_Collision* A = Vector->elems[IndexA]; // :063505a9
        IVP_Collision* B = Vector->elems[IndexB]; // :063505b1
        Vector->elems[IndexA] = B;                // :063505b4
        Vector->elems[IndexB] = A;                // :063505ba

        if (A->IndexInElement0 == IndexA)  // :063505bd
            A->IndexInElement0 = IndexB;   // :063505c2
        else
            A->IndexInElement1 = IndexB;   // :063505c7

        if (B->IndexInElement0 == IndexB)  // :063505ca
            B->IndexInElement0 = IndexA;   // :063505d0
        else
            B->IndexInElement1 = IndexA;   // :063505d9
    }
}

// The node hash (IVP_OV_Tree_Hash, an IVP_VHash) is driven through the
// IVP_VHash members AddElem / RemoveElem / FindElem (FUN_063523d0 /
// FUN_06352480 / FUN_06352580, ivp_utility/ivp_vhash.cpp). Each stores the
// node pointer AND hands it to the comparator; the hash tag is the
// caller-computed IVP_OV_HashNodeKey.

namespace
{
    // FUN_0635c570 (Common.dll.txt:0635c570-0635c583; formerly the extern "C"
    // FUN_0635c570 in the former src/Common/common_stubs.cpp): `(param_1 >> 0x17 & 0xff)
    // - 0x7f` -- the unbiased IEEE-754 exponent of a float handed over as its
    // raw bit pattern (bits 23-30 minus the 127 bias), i.e. floor(log2(|x|))
    // for a normal. One stack-passed uint, no this-pointer; retail pops it
    // (RET 4), the port keeps __cdecl. Sole caller: compute_query_cell
    // (FUN_0635c620) below.
    int __cdecl IvpFloatExponent(unsigned param_1)
    {
        return (int)((param_1 >> 0x17) & 0xff) - 0x7f;
    }

    // ------------------------------------------------------------------------
    // FUN_06369200's lookup table (retail .data:DAT_063edcdc, the standard
    // reflected CRC-32 table with polynomial 0xEDB88320). The IVP hash
    // containers share a byte-identical copy of the same retail global,
    // g_Crc32Table_063edcdc (ivp_utility/ivp_crc32.h, defined in
    // ivp_vhash.cpp); this TU still builds its own. Merging the two is a
    // separate behaviour-neutral cleanup.
    // ------------------------------------------------------------------------
    struct OvCrc32Table
    {
        uint32_t Entry[256];

        OvCrc32Table()
        {
            for (uint32_t I = 0; I < 256; ++I)
            {
                uint32_t C = I;
                for (int32_t K = 0; K < 8; ++K)
                    C = ((C & 1u) != 0) ? (0xEDB88320u ^ (C >> 1)) : (C >> 1);
                Entry[I] = C;
            }
        }
    };

    const OvCrc32Table g_OvCrc32Table;

    // ------------------------------------------------------------------------
    // Retail indexes IVP_OV_Tree_Manager's power-of-two table straight off the
    // manager base with no bound check -- `LEA ECX,[ESI + EAX*4]` at
    // :0635c653 and `FLD [ECX + EDX*4 + 0xa0]` at :0635cca1 / :0635d367. Both
    // index expressions can, at the extreme ends of their input range
    // (a radius that underflows past 2^-41), step one slot outside the 81-entry
    // table and read QueryCell.CellX or the dword in front of the object. These
    // two helpers reproduce that addressing exactly, through the object base,
    // rather than indexing the array out of bounds.
    //   ScaleAt  : the raw table slot, as FUN_0635c620 addresses it.
    //   CellSizeAt: CellSizeTable[Level + 40] == 2^Level == one cell's world
    //               size at that level, as FUN_0635cbd0/FUN_0635d360 address it.
    // ------------------------------------------------------------------------
    inline float ScaleAt(const IVP_OV_Tree_Manager* Self, int32_t Index)
    {
        return reinterpret_cast<const float*>(Self)[Index];
    }

    inline float CellSizeAt(const IVP_OV_Tree_Manager* Self, int32_t Level)
    {
        return reinterpret_cast<const float*>(Self)[Level + 40];
    }

    // `CALL floor` / `CALL ceil` followed by `CALL ftol`. ftol truncates toward
    // zero, which is exact on an already-integral double, so the composition is
    // just the C library call plus a cast.
    inline int32_t FloorToInt(double Value) { return static_cast<int32_t>(std::floor(Value)); }
    inline int32_t CeilToInt(double Value)  { return static_cast<int32_t>(std::ceil(Value)); }

    // ------------------------------------------------------------------------
    // The sphere test both collectors share, byte-identical at
    // :0635cdf0-:0635ce2b (FUN_0635cdd0) and :0635ceb1-:0635ceec (FUN_0635ce90).
    //
    // The x87 stream builds dx/dy/dz and the SUM of the two radii and keeps all
    // four on the FPU stack (double); the distance is summed (dz*dz + dy*dy) +
    // dx*dx. `FCOMPP` leaves flags for ST0 (rsum*rsum) against ST1 (dist^2), and
    // `TEST AH,0x1` isolates C0, which FCOMPP sets iff ST0 < ST1 or unordered.
    // The `JNZ` therefore SKIPS the collect, so the collect happens on
    // rsum^2 >= dist^2 -- an exact tangent counts as touching, NaN does not.
    // ------------------------------------------------------------------------
    inline bool SpheresTouch(const IVP_OV_Element* A, const IVP_OV_Element* B)
    {
        const double Dx = (double)A->CenterX - B->CenterX;
        const double Dy = (double)A->CenterY - B->CenterY;
        const double Dz = (double)A->CenterZ - B->CenterZ;
        const double RSum = (double)A->radius + B->radius;
        return RSum * RSum >= (Dz * Dz + Dy * Dy) + Dx * Dx;
    }

    // ------------------------------------------------------------------------
    // FUN_0635c9d0's per-axis child pick, written out once instead of three
    // times (:0635ca26-:0635ca6b for X, :0635ca6b-:0635cab0 for Y,
    // :0635cab0-:0635caef for Z -- three byte-identical blocks).
    //
    // A node is two cells wide, so splitting one produces THREE overlapping
    // children at 2C, 2C+1 and 2C+2. `Delta` is the parent's level minus the
    // target's; the child's own level is one finer, hence the (Delta - 1) shift
    // in the second test. Retail phrases the first test as
    // `Target >= (C+1) << Delta`, which is the same boundary as
    // `Target >= (2C+2) << (Delta-1)`.
    // ------------------------------------------------------------------------
    inline int32_t PickChildCoord(int32_t ParentCoord, int32_t TargetCoord, int32_t Delta)
    {
        if (TargetCoord >= ((ParentCoord + 1) << Delta))
            return (ParentCoord + 1) * 2;                              // :0635ca44-:0635ca4b
        if (TargetCoord >= ((ParentCoord * 2 + 1) << (Delta - 1)))
            return ParentCoord * 2 + 1;                                // :0635ca61-:0635ca65
        return ParentCoord * 2;                                        // :0635ca69
    }
}

// ----------------------------------------------------------------------------
// FUN_0634f140 (decomp-tracker item 3112).
// ----------------------------------------------------------------------------
uint32_t IVP_HashCollisionPartner(IVP_Collision* Collision, IVP_Real_Object* Object)
{
    IVP_Real_Object* Pair[2];
    Collision->get_objects(Pair); // :0634f144-:0634f14e (vtable slot 1, out-array on the stack)

    // :0634f151-:0634f15f. One of the two IS `Object`, so the XOR cancels it
    // and what is left is the PARTNER object's pointer -- which is exactly the
    // key the lookup side of RecheckOvElement hashes directly.
    const uint32_t Key = reinterpret_cast<uint32_t>(Pair[1])
                       ^ reinterpret_cast<uint32_t>(Pair[0])
                       ^ reinterpret_cast<uint32_t>(Object);
    return SpreadHash(Key); // :0634f161-:0634f16d
}

// ============================================================================
// IVP_OV_Element
// ============================================================================

// FUN_0635c020 (decomp-tracker item 3330).
IVP_OV_Element::IVP_OV_Element(IVP_Real_Object* Owner)
{
    // :0635c024 / :0635c047 -- the two vptr stores (IVP_Listener_Hull's
    // 063a5694, then 063a5680) are the compiler's.

    // :0635c02e-:0635c03d. NOTE the element's collision vector does NOT use an
    // inline buffer: retail malloc's 0x40 bytes (16 slots) up front, which is
    // why the destructor's `elems != this+0x30` inline-buffer guard always
    // takes the free path in practice.
    collision_fvector.memsize = 0x10;
    collision_fvector.n_elems = 0;
    collision_fvector.elems = static_cast<IVP_Collision**>(std::malloc(0x40));

    node = nullptr;         // :0635c044
    CenterX = 0.0f;         // :0635c04d
    CenterY = 0.0f;         // :0635c050
    CenterZ = 0.0f;         // :0635c053
    hull_manager = nullptr; // :0635c059
    real_object = Owner;    // :0635c05c
    radius = -1.0f;         // :0635c060 (the literal bit pattern 0xbf800000)

    // HullPoolSlot (+0x04) and Pad1C (+0x1c) are not written here, as in retail;
    // HullPoolSlot is only read after InsertIntoHullManager has set it.
}

// FUN_0635c090 (decomp-tracker item 3332). FUN_0635c070/item 3331 is just the
// compiler's scalar deleting destructor around this one (`call this; if (flag
// & 1) operator delete(this)`), which C++ `delete` expresses directly.
IVP_OV_Element::~IVP_OV_Element()
{
    // :0635c0b4-:0635c0cd -- drop our slot in the owner's hull manager pool.
    if (hull_manager != nullptr)
    {
        hull_manager->PoolRemove(static_cast<uint16_t>(HullPoolSlot));
        hull_manager = nullptr;
    }

    IVP_Environment* Env = real_object->environment; // :0635c0d0-:0635c0d3

    // :0635c0d6-:0635c0d7 -- FUN_0634caf0(this=Env, param_1=real_object): every
    // collision delegator's object_is_removed_from_collision_detection.
    IVP_Environment_Notify_Vector12c_Slot4(Env, real_object);

    // :0635c0dc-:0635c0e8 -- unlink from the spatial subdivision.
    IVP_OV_Tree_Manager* Tree = Env->ov_tree_manager;
    Tree->remove_ov_element(this);

    // :0635c0ed-:0635c117 -- the collision vector's own inlined destructor,
    // byte-for-byte what IVP_U_Vector<T>::Clear() does.
    collision_fvector.Clear();
}

// LAB_0635c1e0 (.text:0635c1e0-0635c1ef, RET 8): EAX = +0x24 (the object),
// PUSH it, ECX = object->environment(+0x18)->mindist_manager(+0x10), CALL
// FUN_0634ebd0. Neither argument is read.
void IVP_OV_Element::hull_limit_exceeded_event(IVP_Hull_Manager* /*HullManager*/, float /*HullIntrusionValue*/)
{
    real_object->environment->mindist_manager->RecheckOvElement(real_object);
}

// LAB_0634dc10 (.text:0634dc10-0634dc1b, RET 4): `TEST ECX,ECX; JZ` then
// `this->slot4(1)` -- the deleting destructor.
void IVP_OV_Element::hull_manager_is_going_to_be_deleted_event(IVP_Hull_Manager* /*HullManager*/)
{
    delete this;
}

// FUN_0635c130 (decomp-tracker item 3333).
void IVP_OV_Element::InsertIntoHullManager(IVP_Hull_Manager* NewHullMgr, float KeyBias)
{
    // :0635c136-:0635c142. The environment's split-precision "now".
    IVP_Environment* Env = real_object->environment;
    const float TimeHi = Env->current_time.time_base;   // env+0xf8
    const float TimeLo = Env->current_time.time_offset; // env+0xfc

    IVP_Hull_Manager* Mgr = hull_manager; // :0635c149
    if (Mgr != nullptr)
    {
        // :0635c158-:0635c16a. Retail IGNORES the NewHullMgr argument on this
        // path (it is always the same object's own manager anyway) and simply
        // re-keys the existing slot: drop it, then insert a fresh one.
        Mgr->PoolRemove(static_cast<uint16_t>(HullPoolSlot));
    }
    else
    {
        Mgr = NewHullMgr;    // :0635c19e
        hull_manager = Mgr;  // :0635c1a6
    }

    // :0635c16f-:0635c185 (and the identical :0635c1a2-:0635c1bc on the other
    // branch): the hull's OUTER value linearly extrapolated to "now", plus the
    // caller's bias. Association follows the x87 stream exactly --
    // FLD TimeHi / FSUB Hi / FADD TimeLo / FSUB Lo / FMUL grad / FADD base /
    // FADD bias -- on the FPU stack, rounded to float once by the FSTP into
    // the Insert argument (:0635c189 / :0635c1c0).
    const double Elapsed = (((double)(TimeHi) - Mgr->LastEditTimeHi) + TimeLo) - Mgr->LastEditTimeLo;
    const float Key = (float)((Elapsed * Mgr->OuterGradient + Mgr->OuterValueAtLastEdit) + KeyBias);

    // :0635c18d/:0635c194 (and :0635c1c4/:0635c1ca) -- remember the returned
    // slot so the destructor and the next re-key can find it again.
    HullPoolSlot = Mgr->PoolInsert(this, Key);
}

// FUN_0635c200 (.text:0635c200-0635c248, RET 4), cmn-phy5 2026-10-08.
void IVP_OV_Element::add_oo_collision(IVP_Collision* Collision)
{
    // :0635c200-:0635c224 -- `CMP n_elems,memsize; JC` skips the realloc
    // (FUN_063441a0), then elems[n_elems] = Collision.
    const int32_t Index = static_cast<int32_t>(collision_fvector.n_elems);
    collision_fvector.Add(Collision);

    // :0635c227-:0635c243 -- the first free back-index gets the slot.
    if (Collision->IndexInElement0 == -1)
        Collision->IndexInElement0 = Index; // :0635c235
    else
        Collision->IndexInElement1 = Index; // :0635c240
}

// FUN_0635c250 (.text:0635c250-0635c2bb, RET 4), cmn-phy5 2026-10-08.
void IVP_OV_Element::remove_oo_collision(IVP_Collision* Collision)
{
    // :0635c256-:0635c26f -- IndexInElement0 is trusted only when it is in
    // range (`JL` / `JGE`, signed) and the slot really holds this collision.
    int32_t Index = Collision->IndexInElement0;
    if (Index < 0 || Index >= static_cast<int32_t>(collision_fvector.n_elems) ||
        collision_fvector.elems[Index] != Collision)
        Index = Collision->IndexInElement1;

    --collision_fvector.n_elems;                                       // :0635c272
    const int32_t Last = static_cast<int32_t>(collision_fvector.n_elems);
    if (Last > Index)                                                  // :0635c281 `CMP EAX,EDX; JLE`
    {
        IVP_Collision* Moved = collision_fvector.elems[Last];          // :0635c288
        collision_fvector.elems[Index] = Moved;                        // :0635c28b
        if (Moved->IndexInElement0 == Last)                            // :0635c294
            Moved->IndexInElement0 = Index;
        else
            Moved->IndexInElement1 = Index;                            // :0635c29e
    }
    if (Collision->IndexInElement0 == Index)                           // :0635c2a1
        Collision->IndexInElement0 = -1;
    else
        Collision->IndexInElement1 = -1;                               // :0635c2b2
}

// ============================================================================
// IVP_Collision (vtable 063a55b8) and IVP_Collision_Delegator_Root_Mindist
// (vtable 063a5760) -- see src/IVP/ivp_collision/ivp_collision.h.
// ============================================================================

// LAB_0634dc00 (.text:0634dc00-0634dc0a): `MOV dword ptr [0],0; RET 4`.
void IVP_Collision::simulate_time_event(IVP_Environment* /*Environment*/)
{
    IVP_CoreTrap(); // 0634dc00
}

// LAB_0634dc10 (.text:0634dc10-0634dc1b, RET 4): `TEST ECX,ECX; JZ; PUSH 1;
// CALL [EAX+0x10]` -- delete this; the argument is not read.
void IVP_Collision::delegator_is_going_to_be_deleted_event(IVP_Collision_Delegator* /*Delegator*/)
{
    delete this;
}

// LAB_06360c40 (.text:06360c40-06360c7c, RET 4).
void IVP_Collision_Delegator_Root_Mindist::collision_is_going_to_be_deleted_event(IVP_Collision* Collision)
{
    IVP_Real_Object* Objects[2];
    Collision->get_objects(Objects);                         // :06360c4b-:06360c52 (slot 1)
    IVP_OV_Element* Element0 = Objects[0]->ov_element;       // :06360c5d
    IVP_OV_Element* Element1 = Objects[1]->ov_element;       // :06360c63
    Element0->remove_oo_collision(Collision);                // :06360c6a FUN_0635c250
    Element1->remove_oo_collision(Collision);                // :06360c72
}

// LAB_06360c00 (.text:06360c00-06360c2a, RET 4): every collision filed on
// the object's OV element, top down, gets slot 3 with this delegator.
// n_elems is read once (`MOV SI,word ptr [EDI+0x2a]`); elems is re-read each
// pass (:06360c18).
void IVP_Collision_Delegator_Root_Mindist::object_is_removed_from_collision_detection(IVP_Real_Object* Object)
{
    IVP_OV_Element* Element = Object->ov_element;                       // :06360c07
    for (int32_t I = static_cast<int32_t>(Element->collision_fvector.n_elems) - 1; I >= 0; --I)
    {
        IVP_Collision* Collision = Element->collision_fvector.elems[I];  // :06360c18-:06360c1b
        Collision->delegator_is_going_to_be_deleted_event(this);         // :06360c20-:06360c21 (slot 3)
    }
}

// FUN_06360c80 (.text:06360c80-06360dba, RET 8).
IVP_Collision* IVP_Collision_Delegator_Root_Mindist::delegate_collisions_for_object(IVP_Real_Object* BaseObject,
                                                                                 IVP_Real_Object* CollidingElement)
{
    // :06360c9f-:06360cb8 -- surface manager slot 0 on each object: the single
    // convex ledge, or null.
    IVP_Compact_Ledge* Ledge0 = static_cast<IVP_Compact_Ledge*>(BaseObject->surface_manager->get_single_convex());
    IVP_Compact_Ledge* Ledge1 = static_cast<IVP_Compact_Ledge*>(CollidingElement->surface_manager->get_single_convex());

    IVP_Collision* Collision = nullptr;
    if (Ledge0 != nullptr && Ledge1 != nullptr) // :06360cba / :06360cc6
    {
        // :06360cce-:06360cdd -- `TEST byte [ledge+8],3` on both: if either
        // ledge has those bits set, a recursive mindist (0x94), else a plain
        // one (0x80). Both get (BaseObject->environment, this) (:06360cd1).
        IVP_Environment* Environment = BaseObject->environment;
        IVP_Ledge_Mindist* Mindist;
        if ((Ledge0->SizeAndFlags & 3) == 0 &&
            (Ledge1->SizeAndFlags & 3) == 0)
            Mindist = new IVP_Ledge_Mindist(Environment, this);     // :06360cdf-:06360d00 FUN_0634dd10
        else
            Mindist = new IVP_Complex_Mindist(Environment, this);   // :06360d07-:06360d28 FUN_06362ab0

        // :06360d31-:06360d4f -- FUN_0634def0(Base, Colliding, ledge0+0x14,
        // ledge1+0x14): the first triangle's first edge of each ledge.
        Mindist->init_mindist(BaseObject, CollidingElement, Ledge0->GetFirstEdge(), Ledge1->GetFirstEdge());
        Collision = Mindist;
    }
    else
    {
        // :06360d5a-:06360d7e -- operator_new(0x40) + FUN_06369be0(this,
        // BaseObject, CollidingElement) (pushes EBP, EBX, ESI: ESI = this,
        // EBX = BaseObject, EBP = CollidingElement): the library's
        // object-pair watcher for objects without a single convex ledge.
        Collision = new IVP_OO_Watcher(this, BaseObject, CollidingElement);
    }

    // :06360d8c-:06360da1 -- FUN_0635c200 on both objects' OV elements.
    BaseObject->ov_element->add_oo_collision(Collision);
    CollidingElement->ov_element->add_oo_collision(Collision);
    return Collision;
}

// LAB_06360c30 (.text:06360c30-06360c3b, RET 4): `if (this) this->slot1(1)`
// -- delete this. The argument is not read.
void IVP_Collision_Delegator_Root_Mindist::environment_is_going_to_be_deleted_event(IVP_Environment* /*Environment*/)
{
    delete this;
}

// ============================================================================
// IVP_OV_Tree_Manager
// ============================================================================

// FUN_0635d2e0 (decomp-tracker item 3353, Common.dll.txt:0635d2e0-0635d358).
// This is the function whose "unknowable side effects" item 3111's inherited
// blocker_reason was built on. It writes exactly ONE field of the element.
void IVP_OV_Tree_Manager::remove_ov_element(IVP_OV_Element* Element)
{
    IVP_OV_Node* Node = Element->node; // :0635d2e8
    if (Node == nullptr)               // :0635d2eb
        return;

    Element->node = nullptr; // :0635d2f0 -- the ONLY write this function makes to the element.

    // :0635d2f7-:0635d33b. Retail inlines an ordered remove-by-value over the
    // node's element vector: find the slot holding this element scanning
    // BACKWARD, then shift everything after it down by one. Note it decrements
    // n_elems before the shift and re-reads it inside the shift loop, so a
    // not-found element (index stays -1) still shrinks the vector -- faithfully
    // reproduced here rather than "fixed".
    const uint16_t OldCount = Node->Elements.n_elems;
    int32_t Index = static_cast<int32_t>(OldCount) - 1; // :0635d2fb-:0635d303
    while (Index >= 0)                                  // :0635d313/:0635d315
    {
        if (Node->Elements.elems[Index] == Element) // :0635d30b
            break;
        --Index; // :0635d30f
    }

    Node->Elements.n_elems = static_cast<uint16_t>(OldCount - 1); // :0635d317-:0635d31a
    for (; Index < static_cast<int32_t>(Node->Elements.n_elems); ++Index) // :0635d324/:0635d339
        Node->Elements.elems[Index] = Node->Elements.elems[Index + 1];    // :0635d328-:0635d32f

    // :0635d33d-:0635d354. Collapse the now-possibly-empty node, then keep
    // collapsing its parents for as long as they fall empty too.
    IVP_OV_Node* Collapsed = collapse_empty_node(Node);
    while (Collapsed != nullptr)
        Collapsed = collapse_empty_node(Collapsed);
}

// ----------------------------------------------------------------------------
// FUN_0635c620 (decomp-tracker item 3344, Common.dll.txt:0635c620-0635c8c1,
// 207 insns -- read end to end).
//
// Pick the coarsest subdivision level at which the element's bounding box
// spans at most two cells per axis, and write the resulting cell key into
// QueryCell. Then, if the caller offered a LARGER radius than the one that was
// just fitted, try that larger radius one level coarser as well: if it also
// fits, retail prefers it, because filing an element with a generous radius is
// what keeps it from having to be re-filed on every small move.
//
// Returns whichever radius the key ends up describing -- MinRadius on the
// normal path, MaxRadius only when the wider retry succeeded. That return
// value becomes IVP_OV_Element::radius and, in RecheckOvElement, the hull key.
// ----------------------------------------------------------------------------
float IVP_OV_Tree_Manager::compute_query_cell(IVP_OV_Element* Element, float MinRadius, float MaxRadius)
{
    // :0635c629-:0635c63e. The starting level is the IEEE exponent of TWICE
    // the radius (`FLD r; FADD ST0,ST0`), plus one. `FUN_0635c570` takes the
    // float's raw bits (IvpFloatExponent), so the doubled value has to be
    // punned, not converted.
    const float Doubled = MinRadius + MinRadius;
    uint32_t DoubledBits = 0;
    std::memcpy(&DoubledBits, &Doubled, sizeof(DoubledBits));

    int32_t LevelPlus1 = IvpFloatExponent(DoubledBits) + 1; // :0635c637-:0635c63e, FUN_0635c570
    if (LevelPlus1 < -0x28)                             // :0635c63f-:0635c644
        LevelPlus1 = -0x28;

    // :0635c64c-:0635c656. The table slot holding 2^-(LevelPlus1-1), i.e. the
    // RECIPROCAL of this level's cell size -- world coordinates get multiplied
    // by it to become grid coordinates. Coarsening by one level means stepping
    // one slot DOWN in the table (:0635c737-:0635c73f walks the pointer back
    // by 4 and bumps the level), which halves the reciprocal.
    int32_t TableIndex = 0x29 - LevelPlus1;

    int32_t XMin = 0;
    int32_t YMin = 0;
    int32_t ZMin = 0;

    for (;;) // LAB_0635c65a
    {
        const double Inv = (double)(ScaleAt(this, TableIndex)); // :0635c65e

        // Per axis: floor((c - r) * inv) and ceil((c + r) * inv). The level is
        // acceptable only while the two are at most two cells apart. Retail
        // spells the max side as `r + c` and the min side as `c - r`; the
        // reciprocal is an exact power of two, so both products are exact.
        XMin = FloorToInt(((double)(Element->CenterX) - MinRadius) * Inv); // :0635c667-:0635c685
        if (CeilToInt(((double)(Element->CenterX) + MinRadius) * Inv) > XMin + 2) // :0635c67f-:0635c6a7
        {
            ++LevelPlus1;  // :0635c73b
            --TableIndex;  // :0635c73c-:0635c73f
            continue;
        }

        YMin = FloorToInt(((double)(Element->CenterY) - MinRadius) * Inv); // :0635c6ad-:0635c6ce
        if (CeilToInt(((double)(Element->CenterY) + MinRadius) * Inv) > YMin + 2) // :0635c6c8-:0635c6f0
        {
            ++LevelPlus1;
            --TableIndex;
            continue;
        }

        ZMin = FloorToInt(((double)(Element->CenterZ) - MinRadius) * Inv); // :0635c6f2-:0635c713
        if (CeilToInt(((double)(Element->CenterZ) + MinRadius) * Inv) > ZMin + 2) // :0635c70d-:0635c735
        {
            ++LevelPlus1;
            --TableIndex;
            continue;
        }

        break; // LAB_0635c748
    }

    // :0635c748-:0635c776. Commit the key. Level and LevelPlus1 are written as
    // the pair (LevelPlus1 - 1, LevelPlus1), which is where the class-wide
    // invariant "LevelPlus1 == Level + 1" comes from.
    QueryCell.CellX = XMin;                 // :0635c756
    QueryCell.CellY = YMin;                 // :0635c767
    QueryCell.CellZ = ZMin;                 // :0635c76d
    QueryCell.Level = LevelPlus1 - 1;       // :0635c75c-:0635c75f
    QueryCell.LevelPlus1 = LevelPlus1;      // :0635c776

    // :0635c74c-:0635c77c. `FLD MinRadius; FCOMP MaxRadius; FNSTSW AX;
    // TEST AH,0x1; JNZ retry`. AH bit 0 is C0, which FCOMP sets when ST0 is
    // LESS than the operand or the pair is unordered, so the retry runs when
    // MinRadius < MaxRadius or either is NaN; the fall-through returns
    // MinRadius (:0635c77e).
    if (MinRadius >= MaxRadius)
        return MinRadius;

    // LAB_0635c78a. Exactly ONE extra level is tried -- this is not a loop.
    const int32_t WideLevelPlus1 = LevelPlus1 + 1;                             // :0635c78a
    const double WideInv = (double)(ScaleAt(this, 0x29 - WideLevelPlus1)); // :0635c78b-:0635c795

    const int32_t WideXMin = FloorToInt(((double)(Element->CenterX) - MaxRadius) * WideInv); // :0635c79f-:0635c7bd
    if (CeilToInt(((double)(Element->CenterX) + MaxRadius) * WideInv) > WideXMin + 2)        // :0635c7b7-:0635c7df
        return MinRadius; // :0635c7e1

    const int32_t WideYMin = FloorToInt(((double)(Element->CenterY) - MaxRadius) * WideInv); // :0635c7ed-:0635c80e
    if (CeilToInt(((double)(Element->CenterY) + MaxRadius) * WideInv) > WideYMin + 2)        // :0635c808-:0635c830
        return MinRadius; // :0635c832

    const int32_t WideZMin = FloorToInt(((double)(Element->CenterZ) - MaxRadius) * WideInv); // :0635c83e-:0635c85f
    if (CeilToInt(((double)(Element->CenterZ) + MaxRadius) * WideInv) > WideZMin + 2)        // :0635c859-:0635c881
        return MinRadius; // :0635c883

    // LAB_0635c88f. The wider radius fits too, so the key describes it instead.
    QueryCell.CellX = WideXMin;             // :0635c89a
    QueryCell.CellY = WideYMin;             // :0635c8aa
    QueryCell.CellZ = WideZMin;             // :0635c8b0
    QueryCell.Level = WideLevelPlus1 - 1;   // :0635c8a0/:0635c8b6
    QueryCell.LevelPlus1 = WideLevelPlus1;  // :0635c8a3
    return MaxRadius;                       // :0635c893
}

// ----------------------------------------------------------------------------
// FUN_0635c590 (item 3343, Common.dll.txt:0635c590-0635c615, 71 insns).
//
// Scale `Coarse` down by `LevelDelta` levels and ask whether it fully contains
// `Fine`. Both nodes are two cells wide, so at the fine level `Coarse` occupies
// the inclusive index range [C << d, (C << d) + (2 << d) - 2] -- the `- 2` is
// there because `Fine` itself already takes up two of those cells.
//
// Retail is a bare __stdcall (RET 0xc); the caller's `MOV ECX,ESI` is a
// leftover this-pointer that the body clobbers at :0635c596 without reading,
// which is why this is a static member here.
// ----------------------------------------------------------------------------
bool IVP_OV_Tree_Manager::cell_contains(const IVP_OV_Node* Coarse, const IVP_OV_Node* Fine, int32_t LevelDelta)
{
    const int32_t Span = (2 << LevelDelta) - 2;       // :0635c5a8-:0635c5b7
    const int32_t Ax = Coarse->CellX << LevelDelta;   // :0635c5af
    const int32_t Ay = Coarse->CellY << LevelDelta;   // :0635c5b1
    const int32_t Az = Coarse->CellZ << LevelDelta;   // :0635c5b3

    if (Fine->CellX < Ax) return false;               // :0635c5ba-:0635c5bc
    if (Fine->CellY < Ay) return false;               // :0635c5ca-:0635c5cc
    if (Fine->CellZ < Az) return false;               // :0635c5da-:0635c5dc
    if (Fine->CellX > Ax + Span) return false;        // :0635c5e7-:0635c5eb
    if (Fine->CellY > Ay + Span) return false;        // :0635c5f6-:0635c5fb
    return Fine->CellZ <= Az + Span;                  // :0635c606-:0635c60c (SETLE)
}

// ----------------------------------------------------------------------------
// FUN_0635c8d0 (item 3345, Common.dll.txt:0635c8d0-0635c972, 81 insns).
//
// The same scaling as cell_contains, but a half-open AABB overlap:
// [F, F + 2) against [C << d, (C + 2) << d). Also a bare __stdcall.
// ----------------------------------------------------------------------------
bool IVP_OV_Tree_Manager::cell_overlaps(const IVP_OV_Node* Coarse, const IVP_OV_Node* Fine, int32_t LevelDelta)
{
    if (Fine->CellX + 2 <= (Coarse->CellX << LevelDelta)) return false;   // :0635c8ea-:0635c8f5
    if (Fine->CellY + 2 <= (Coarse->CellY << LevelDelta)) return false;   // :0635c908-:0635c90f
    if (Fine->CellZ + 2 <= (Coarse->CellZ << LevelDelta)) return false;   // :0635c922-:0635c929
    if (Fine->CellX >= ((Coarse->CellX + 2) << LevelDelta)) return false; // :0635c93c-:0635c943
    if (Fine->CellY >= ((Coarse->CellY + 2) << LevelDelta)) return false; // :0635c94e-:0635c955
    return Fine->CellZ < ((Coarse->CellZ + 2) << LevelDelta);             // :0635c960-:0635c96b (SETL)
}

// ----------------------------------------------------------------------------
// FUN_0635cbd0 (item 3348, Common.dll.txt:0635cbd0-0635cdc9, 172 insns).
//
// Replace CurrentRoot with a fresh cell one level COARSER that still contains
// the old root, and adopt the old root as its only child. Called in a loop by
// insert_ov_element until the root has grown far enough to swallow a new cell.
// ----------------------------------------------------------------------------
void IVP_OV_Tree_Manager::grow_root(const IVP_OV_Node* Target)
{
    IVP_OV_Node* OldRoot = CurrentRoot; // :0635cbed / :0635cc0f

    if (OldRoot->LevelPlus1 > 0x28) // :0635cbf3-:0635cbfe
        std::printf("*** ERROR *** Excessive sizelevel (%d) for element\n", OldRoot->LevelPlus1); // :0635cc00-:0635cc06

    IVP_OV_Node* NewRoot = new IVP_OV_Node(); // :0635cc15-:0635cc34 (operator new(0x28) + FUN_0635c2c0)

    // :0635cc36-:0635cc6f. MSVC's signed `x % 2`, which is -1 for a negative
    // odd coordinate, 0 for an even one and +1 for a positive odd one
    // (`AND 0x80000001; JNS +5; DEC; OR 0xfffffffe; INC`). Read BEFORE the
    // halving below, off the OLD root.
    const int32_t ParityX = OldRoot->CellX % 2;
    const int32_t ParityY = OldRoot->CellY % 2;
    const int32_t ParityZ = OldRoot->CellZ % 2;

    // :0635cc6f-:0635cc89. `CDQ; SUB EAX,EDX; SAR EAX,1` is division by two
    // rounding TOWARD ZERO -- i.e. plain C `/ 2`, not an arithmetic shift.
    NewRoot->CellX = OldRoot->CellX / 2;
    NewRoot->CellY = OldRoot->CellY / 2;
    NewRoot->CellZ = OldRoot->CellZ / 2;
    NewRoot->Level = OldRoot->Level + 1;           // :0635cc8c-:0635cc90
    NewRoot->LevelPlus1 = OldRoot->LevelPlus1 + 1; // :0635cc93-:0635cc9a

    const double NewCell = (double)(CellSizeAt(this, NewRoot->Level));   // :0635cc9d-:0635ccaf
    const double TargetCell = (double)(CellSizeAt(this, Target->Level)); // :0635cca8-:0635ccba

    // :0635ccbe-:0635cd56, three byte-identical per-axis blocks.
    //   * A negative ODD coordinate needs one more step left, because the
    //     round-toward-zero halving above moved it the wrong way.
    //   * An EVEN coordinate has a free choice: both C/2 and C/2 - 1 produce a
    //     parent that covers the old root, so retail breaks the tie toward
    //     `Target` -- if the parent's origin would sit to the RIGHT of the
    //     target's, step it one cell left. `FCOMPP` + `TEST AH,0x41` (C3|C0,
    //     i.e. equal-or-less) with `JNZ skip` means the step happens only on a
    //     STRICT greater-than.
    //   * A positive ODD coordinate needs no adjustment at all.
    if (ParityX == -1)
        NewRoot->CellX -= 1;                                                          // :0635ccc0
    else if (ParityX == 0 && NewRoot->CellX * NewCell > Target->CellX * TargetCell)    // :0635ccc4-:0635cce7
        NewRoot->CellX -= 1;                                                          // :0635cce9

    if (ParityY == -1)
        NewRoot->CellY -= 1;                                                          // :0635ccf1
    else if (ParityY == 0 && NewRoot->CellY * NewCell > Target->CellY * TargetCell)    // :0635ccf6-:0635cd1b
        NewRoot->CellY -= 1;                                                          // :0635cd1d

    if (ParityZ == -1)
        NewRoot->CellZ -= 1;                                                          // :0635cd26
    else if (ParityZ == 0 && NewRoot->CellZ * NewCell > Target->CellZ * TargetCell)    // :0635cd2b-:0635cd50
        NewRoot->CellZ -= 1;                                                          // :0635cd52

    // :0635cd56-:0635cd68. A brand-new node has no children, so this warning
    // can never fire here; retail's check is simply generic. Note it triggers
    // at >= 27, whereas link_node_under's copy triggers at > 27.
    if (NewRoot->Children.n_elems >= 0x1b)
        std::printf("*** ERROR *** : Mehr als 27 Kinder *********************************\n");

    NewRoot->Children.Add(OldRoot); // :0635cd6b-:0635cd90
    OldRoot->Parent = NewRoot;      // :0635cd94

    // :0635cd97-:0635cda8. Register the new cell under its own key hash.
    NodeHash->AddElem(NewRoot, IVP_OV_HashNodeKey(NewRoot)); // FUN_063523d0

    CurrentRoot = NewRoot; // :0635cdb6
}

// ----------------------------------------------------------------------------
// FUN_0635c980 (item 3346, Common.dll.txt:0635c980-0635c9cd, 37 insns).
//
// Walk down from `Start` for as long as some child still contains `Target`,
// and return the deepest node reached. Note the level delta is measured for
// the CHILDREN of the current node (one level finer), hence the extra -1.
// ----------------------------------------------------------------------------
IVP_OV_Node* IVP_OV_Tree_Manager::find_deepest_container(IVP_OV_Node* Start, const IVP_OV_Node* Target)
{
    IVP_OV_Node* Current = Start; // :0635c983

    for (;;) // LAB_0635c98d
    {
        const int32_t ChildDelta = Current->Level - Target->Level - 1; // :0635c991-:0635c997

        int32_t Index = 0;      // :0635c998
        bool Descended = false;
        while (Index < static_cast<int32_t>(Current->Children.n_elems)) // :0635c99a-:0635c9a2
        {
            IVP_OV_Node* Child = Current->Children.elems[Index]; // :0635c9a4-:0635c9ab
            if (cell_contains(Child, Target, ChildDelta))        // :0635c9b5-:0635c9bd (`CMP EAX,1; JZ`)
            {
                Current = Child; // :0635c9c2
                Descended = true;
                break;
            }
            ++Index; // :0635c9bf
        }

        if (!Descended)
            return Current; // LAB_0635c9c6
    }
}

// ----------------------------------------------------------------------------
// FUN_0635c9d0 (item 3347, Common.dll.txt:0635c9d0-0635cbc9, 171 insns).
//
// Splice `Node` in underneath `Ancestor`, materialising every intermediate
// cell on the way down. Each new intermediate is registered in the key hash
// exactly like a root grown by grow_root.
// ----------------------------------------------------------------------------
void IVP_OV_Tree_Manager::link_node_under(IVP_OV_Node* Ancestor, IVP_OV_Node* Node)
{
    IVP_OV_Node* Parent = Ancestor;                                // :0635c9e7
    int32_t Delta = Parent->LevelPlus1 - Node->LevelPlus1;         // :0635c9f1-:0635c9f8

    while (Delta != 1) // :0635c9fb-:0635c9fe / :0635cb67-:0635cb6c
    {
        // LAB_0635ca05.
        IVP_OV_Node* Child = new IVP_OV_Node();                    // :0635ca05-:0635ca24
        Child->CellX = PickChildCoord(Parent->CellX, Node->CellX, Delta); // :0635ca26-:0635ca6b
        Child->CellY = PickChildCoord(Parent->CellY, Node->CellY, Delta); // :0635ca6b-:0635cab0
        Child->CellZ = PickChildCoord(Parent->CellZ, Node->CellZ, Delta); // :0635cab0-:0635caef
        Child->Level = Parent->Level - 1;                          // :0635caef-:0635caf3
        Child->LevelPlus1 = Parent->LevelPlus1 - 1;                // :0635caf6-:0635cafd
        Child->Parent = Parent;                                    // :0635cb00

        Parent->Children.Add(Child);                               // :0635cb03-:0635cb24
        NodeHash->AddElem(Child, IVP_OV_HashNodeKey(Child)); // :0635cb28-:0635cb39, FUN_063523d0

        if (Parent->Children.n_elems > 0x1b)                       // :0635cb3e-:0635cb46
            std::printf("*** ERROR *** Excessive amount of children: %d\n",
                        static_cast<int32_t>(Parent->Children.n_elems));

        Parent = Child;                                            // :0635cb6a
        Delta = Child->LevelPlus1 - Node->LevelPlus1;              // :0635cb5d-:0635cb64
    }

    // LAB_0635cb73. `Node` is now exactly one level below `Parent`.
    Node->Parent = Parent;         // :0635cb76
    Parent->Children.Add(Node);    // :0635cb79-:0635cb95

    if (Parent->Children.n_elems > 0x1b) // :0635cb99-:0635cba1
        std::printf("*** ERROR *** Excessive amount of children: %d\n",
                    static_cast<int32_t>(Parent->Children.n_elems));
}

// ----------------------------------------------------------------------------
// FUN_0635cdd0 (item 3349, Common.dll.txt:0635cdd0-0635ce80, 73 insns).
//
// Append every element in `Node`'s entire subtree whose sphere touches
// `Element`'s to CollectVector. Used once the caller already knows the whole
// subtree is in range, so there is no further pruning.
// ----------------------------------------------------------------------------
void IVP_OV_Tree_Manager::collect_subtree(const IVP_OV_Element* Element, IVP_OV_Node* Node)
{
    for (int32_t I = static_cast<int32_t>(Node->Elements.n_elems) - 1; I >= 0; --I) // :0635cdd9-:0635ce5c
    {
        IVP_OV_Element* Other = Node->Elements.elems[I]; // :0635cdea-:0635cded
        if (SpheresTouch(Other, Element))                // :0635cdf0-:0635ce2b
            CollectVector->Add(Other);                   // :0635ce2d-:0635ce53
    }

    for (int32_t I = static_cast<int32_t>(Node->Children.n_elems) - 1; I >= 0; --I) // :0635ce5e-:0635ce79
        collect_subtree(Element, Node->Children.elems[I]);                          // :0635ce67-:0635ce73
}

// ----------------------------------------------------------------------------
// FUN_0635ce90 (item 3350, Common.dll.txt:0635ce90-0635d021, 154 insns).
//
// The same collect as collect_subtree for this node's own elements, but the
// descent is pruned: only children that can actually reach `Target` are walked,
// and the child that IS `Target` hands off to collect_subtree because
// everything below it is in range by construction.
//
// Which of the two sides of the overlap test is the "coarse" one flips with
// the level relation, which is why there are two near-identical halves.
// ----------------------------------------------------------------------------
void IVP_OV_Tree_Manager::collect_touching(const IVP_OV_Element* Element, IVP_OV_Node* Node, const IVP_OV_Node* Target)
{
    for (int32_t I = static_cast<int32_t>(Node->Elements.n_elems) - 1; I >= 0; --I) // :0635ce96-:0635cf21
    {
        IVP_OV_Element* Other = Node->Elements.elems[I]; // :0635cea7-:0635ceaa
        if (SpheresTouch(Other, Element))                // :0635ceb1-:0635ceec
            CollectVector->Add(Other);                   // :0635ceee-:0635cf14
    }

    // :0635cf29-:0635cf36. `Node->LevelPlus1 - 1` is exactly the level index of
    // Node's own children, so this asks whether the children are still COARSER
    // than the target.
    if (Node->LevelPlus1 - 1 > Target->LevelPlus1)
    {
        const int32_t Delta = Node->Level - Target->Level - 1; // :0635cf38-:0635cf3e
        for (int32_t I = 0; I < static_cast<int32_t>(Node->Children.n_elems); ++I) // :0635cf3f-:0635cfa7
        {
            IVP_OV_Node* Child = Node->Children.elems[I]; // :0635cf60-:0635cf67
            if (Child == Target)                          // :0635cf6a
                collect_subtree(Element, Child);          // :0635cf6e-:0635cf72
            else if (cell_overlaps(Child, Target, Delta)) // :0635cf79-:0635cf86
                collect_touching(Element, Child, Target); // :0635cf88-:0635cf91
        }
    }
    else
    {
        // LAB_0635cfb0. Now the TARGET is the coarse side of the test.
        const int32_t Delta = Target->Level - Node->Level + 1; // :0635cfb0-:0635cfb6
        for (int32_t I = 0; I < static_cast<int32_t>(Node->Children.n_elems); ++I) // :0635cfb7-:0635d01b
        {
            IVP_OV_Node* Child = Node->Children.elems[I]; // :0635cfd4-:0635cfdb
            if (Child == Target)                          // :0635cfde
                collect_subtree(Element, Child);          // :0635cfe2-:0635cfe6
            else if (cell_overlaps(Target, Child, Delta)) // :0635cfed-:0635cffa
                collect_touching(Element, Child, Target); // :0635cffc-:0635d005
        }
    }
}

// ----------------------------------------------------------------------------
// FUN_0635d030 (decomp-tracker item 3351, Common.dll.txt:0635d030-0635d25e,
// 179 insns -- read end to end).
//
// File `Element` into the subdivision at a level chosen from its own radius,
// growing or subdividing the tree as needed, and (when `TouchingOut` is given)
// collect every element whose sphere touches it. Returns the radius the
// element was actually filed at, which is what retail leaves on the x87 stack.
// ----------------------------------------------------------------------------
float IVP_OV_Tree_Manager::insert_ov_element(IVP_OV_Element* Element,
                                             float MinRadius,
                                             float MaxRadius,
                                             IVP_U_Vector<IVP_OV_Element>* TouchingOut)
{
    if (Element == nullptr)  // :0635d04b-:0635d051
        return 0.0f;         // :0635d053 (.rdata:DAT_063a53d4, raw bytes 00 00 00 00)

    // :0635d05e-:0635d074. Choose the cell and remember the radius it describes.
    const float FiledRadius = compute_query_cell(Element, MinRadius, MaxRadius);
    Element->radius = FiledRadius;

    // :0635d077-:0635d094. Does that cell already exist? Both the hash tag and
    // the comparator argument are the embedded QueryCell: the tag is the CRC of
    // its first 20 bytes and the comparator (LAB_06369240) compares the first
    // four dwords of the stored node against those of the key.
    IVP_OV_Node* Node = (IVP_OV_Node*)NodeHash->FindElem(&QueryCell, IVP_OV_HashNodeKey(&QueryCell)); // FUN_06352580

    if (Node != nullptr)
    {
        // :0635d09c-:0635d0c7. It does -- nothing structural to do.
        Node->Elements.Add(Element); // :0635d09c-:0635d0c0
        Element->node = Node;        // :0635d0c4
    }
    else
    {
        // :0635d0cc-:0635d144. Build the cell from the key and file into it.
        Node = new IVP_OV_Node();                 // :0635d0cc-:0635d0eb
        Node->CellX = QueryCell.CellX;            // :0635d0ed-:0635d0ef
        Node->CellY = QueryCell.CellY;            // :0635d0f1-:0635d0f7
        Node->CellZ = QueryCell.CellZ;            // :0635d0fa-:0635d100
        Node->Level = QueryCell.Level;            // :0635d103-:0635d109
        Node->LevelPlus1 = QueryCell.LevelPlus1;  // :0635d10c-:0635d115
        Node->Elements.Add(Element);              // :0635d118-:0635d140
        Element->node = Node;                     // :0635d144

        if (CurrentRoot == nullptr) // :0635d147-:0635d14f
        {
            // :0635d151-:0635d16e. The very first cell IS the tree. Note this
            // path jumps straight to the epilogue, so it deliberately does NOT
            // publish `TouchingOut` into CollectVector and does NOT gather --
            // there is nothing to gather against yet.
            CurrentRoot = Node;
            NodeHash->AddElem(Node, IVP_OV_HashNodeKey(Node)); // FUN_063523d0
            return FiledRadius; // -> LAB_0635d248
        }

        // :0635d173-:0635d19b. Coarsen the root until it swallows the new cell.
        // A root FINER than the new cell (`root->Level < Node->Level`) cannot
        // even be asked the containment question, so it short-circuits straight
        // to another growth step -- retail's own `JL LAB_0635d193`.
        while (CurrentRoot->Level < Node->Level ||
               !cell_contains(CurrentRoot, Node, CurrentRoot->Level - Node->Level))
        {
            grow_root(Node); // :0635d193-:0635d19b
        }

        if (CurrentRoot->Level == Node->Level) // :0635d19d-:0635d1a9
        {
            // :0635d1ab-:0635d1f5. The root grew into exactly this cell (it
            // contains the new one AND shares its level, so the two are the
            // same cell). Re-file into the root and destroy the temporary.
            CurrentRoot->Elements.Add(Element); // :0635d1ab-:0635d1ca
            Element->node = CurrentRoot;        // :0635d1d0-:0635d1d6
            delete Node;                        // :0635d1db-:0635d1e8 (FUN_0635c310 + FUN_06391d5a)
            Node = CurrentRoot;                 // :0635d1eb-:0635d1f1
        }
        else
        {
            // :0635d1f7-:0635d221. Register the cell, then splice it under the
            // deepest already-existing node that still contains it.
            NodeHash->AddElem(Node, IVP_OV_HashNodeKey(Node)); // :0635d1f7-:0635d209, FUN_063523d0
            link_node_under(find_deepest_container(CurrentRoot, Node), Node);                   // :0635d20e-:0635d221
        }
    }

    // :0635d22a-:0635d243. The out-vector is handed to the recursion through
    // the manager rather than as an argument; retail writes it even when it is
    // null, so this port does too.
    CollectVector = TouchingOut;
    if (TouchingOut != nullptr)
        collect_touching(Element, CurrentRoot, Node);

    return FiledRadius; // :0635d248 (the value FST'd back at :0635d070)
}

// ----------------------------------------------------------------------------
// FUN_0635d270 (item 3352, Common.dll.txt:0635d270-0635d2d3, 37 insns).
//
// Delete `Node` if it has become completely empty and return its former parent
// so the caller can keep collapsing upward; null when nothing was collapsed.
// ----------------------------------------------------------------------------
IVP_OV_Node* IVP_OV_Tree_Manager::collapse_empty_node(IVP_OV_Node* Node)
{
    if (Node->Elements.n_elems != 0) // :0635d275-:0635d27a
        return nullptr;
    if (Node->Children.n_elems != 0) // :0635d282-:0635d287
        return nullptr;

    if (Node->Parent == nullptr)     // :0635d28f-:0635d294
        CurrentRoot = nullptr;       // :0635d296

    // :0635d2a1-:0635d2b3. Unregister the cell. FUN_06352480 is the value-set's
    // find-AND-remove entry point, which faults deliberately if the value is
    // not present -- every node in the tree was registered on the way in.
    NodeHash->RemoveElem(Node, IVP_OV_HashNodeKey(Node)); // FUN_06352480

    IVP_OV_Node* Parent = Node->Parent; // :0635d2ba (read AFTER the unregister, as retail does)
    delete Node;                        // :0635d2bf-:0635d2cc
    return Parent;                      // :0635d2cf
}

// ----------------------------------------------------------------------------
// FUN_0635d360 (item 3354, Common.dll.txt:0635d360-0635d3a8, 17 insns).
//
// Decode a cell back into world space. A node is two cells wide, so its edge
// length is 2^(Level+1) == CellSizeTable[LevelPlus1 + 40], and the same scalar
// serves all three axes (retail emits one float, not three).
//
// Its two retail callers (:0634a84b and :063507fd) are the ray solver
// (ivp_ray_solver.cpp) and the sphere query (ivp_sphere_query.cpp).
// ----------------------------------------------------------------------------
void IVP_OV_Tree_Manager::node_cell_bounds(const IVP_OV_Node* Node, float* OriginOut3, float* ExtentOut) const
{
    OriginOut3[0] = CellSizeAt(this, Node->Level) * (float)(Node->CellX); // :0635d364-:0635d375
    OriginOut3[1] = CellSizeAt(this, Node->Level) * (float)(Node->CellY); // :0635d377-:0635d384
    OriginOut3[2] = CellSizeAt(this, Node->Level) * (float)(Node->CellZ); // :0635d387-:0635d395
    *ExtentOut = CellSizeAt(this, Node->LevelPlus1);                                 // :0635d398-:0635d3a6
}

// ============================================================================
// IVP_OV_Node
// ============================================================================

// FUN_0635c2c0 (decomp-tracker item 3337, Common.dll.txt:0635c2c0-0635c300).
// Zeroes Parent and both vector headers -- and nothing else. CellX/CellY/CellZ,
// Level and LevelPlus1 are deliberately left as raw allocation scratch, exactly
// as in retail: every construction site fills them in immediately afterwards.
IVP_OV_Node::IVP_OV_Node()
    : Parent(nullptr) // :0635c2ef
{
    // Children (:0635c2d9-:0635c2e1) and Elements (:0635c2e4-:0635c2ec) are
    // zeroed by IVP_U_Vector's own default constructor, which writes the same
    // three fields with the same values.
}

// ----------------------------------------------------------------------------
// IVP_OV_Tree_Hash / IVP_OV_Tree_Manager ctor and dtor (cmn-phy7 2026-10-08;
// the ctor was the raw FUN_0635c410 "AnomalyManager0635c410" in
// common_stubs.cpp, the dtor a FULLPORT-DEFERRED gap in ~IVP_Environment).
// ----------------------------------------------------------------------------

// LAB_06369240 (.text:06369240-0636926?, RET 8): stored node vs probe node,
// +0x0c first, then +0x00, +0x04, +0x08; 0 on the first mismatch, else 1.
int IVP_OV_Tree_Hash::Compare(void* Elem0, void* Elem1)
{
    const IVP_OV_Node* const Stored = static_cast<const IVP_OV_Node*>(Elem0);
    const IVP_OV_Node* const Probe = static_cast<const IVP_OV_Node*>(Elem1);
    if (Stored->Level != Probe->Level)
    {
        return 0;
    }
    if (Stored->CellX != Probe->CellX)
    {
        return 0;
    }
    if (Stored->CellY != Probe->CellY)
    {
        return 0;
    }
    return Stored->CellZ == Probe->CellZ ? 1 : 0;
}

// FUN_0635c410.
IVP_OV_Tree_Manager::IVP_OV_Tree_Manager()
{
    // :0635c42c-:0635c436 -- QueryCell's ctor (0635c2c0) ran as a member.
    Unmodeled_0x174 = nullptr; // :0635c445
    CurrentRoot = nullptr;     // :0635c44f
    NodeHash = new IVP_OV_Tree_Hash(0x100); // :0635c43b-:0635c47c, stored at :0635c498

    // :0635c486-:0635c4c0 -- two x87 accumulators seeded with 1.0f
    // (DAT_063a53d0): one doubled (FADD ST0,ST0) and stored upward from +0xa0,
    // one multiplied by 0.5f (DAT_063a5460) and stored downward from +0xa0,
    // 0x29 rounds. Every value is an exact power of two, so the float stores
    // are exact.
    float Upward = 1.0f;
    float Downward = 1.0f;
    for (int32_t Round = 0; Round < 0x29; ++Round)
    {
        CellSizeTable[40 + Round] = Upward;   // FSTP [EAX-4], EAX from +0xa0 upward
        CellSizeTable[40 - Round] = Downward; // FST [ECX+4], ECX from +0xa0 downward
        Upward = Upward + Upward;
        Downward = Downward * 0.5f;
    }
}

// FUN_0635c500.
IVP_OV_Tree_Manager::~IVP_OV_Tree_Manager()
{
    if (NodeHash != nullptr)
    {
        delete NodeHash; // :0635c52f-:0635c533, slot 1 (1) = 0635c4e0
    }
    NodeHash = nullptr; // :0635c53c
    // :0635c536-:0635c54e -- ~QueryCell (0635c310) runs as the member's dtor.
}

// FUN_0635c310 (item 3338, Common.dll.txt:0635c310-0635c40e, 89 insns).
IVP_OV_Node::~IVP_OV_Node()
{
    // :0635c32f-:0635c386. Unlink from the parent's child vector: scan BACKWARD
    // for ourselves, then decrement the count and shift the tail down. Retail
    // decrements before the shift and re-reads the count inside the loop, and
    // it runs the shift even when the scan found nothing (the index is then -1)
    // -- the same shape as remove_ov_element's own inlined copy, reproduced
    // rather than "fixed".
    if (Parent != nullptr) // :0635c32f-:0635c33e
    {
        const int32_t OldCount = static_cast<int32_t>(Parent->Children.n_elems); // :0635c342
        int32_t Index = OldCount - 1;                                            // :0635c34d
        while (Index >= 0)                                                       // :0635c34e/:0635c35e-:0635c360
        {
            if (Parent->Children.elems[Index] == this) // :0635c356
                break;
            --Index; // :0635c35a
        }

        Parent->Children.n_elems = static_cast<uint16_t>(OldCount - 1); // :0635c362-:0635c365
        for (; Index < static_cast<int32_t>(Parent->Children.n_elems); ++Index) // :0635c36f/:0635c384
            Parent->Children.elems[Index] = Parent->Children.elems[Index + 1];  // :0635c373-:0635c37a
    }

    // :0635c388-:0635c3ab. Destroy every child. Taking slot 0 over and over
    // terminates because each child's own destructor runs the unlink above and
    // shrinks THIS node's vector. A null slot only skips the delete and re-tests
    // the count (:0635c395 -> LAB_0635c3a7), exactly as retail.
    while (Children.n_elems != 0) // :0635c388-:0635c38c / :0635c3a7-:0635c3ab
    {
        IVP_OV_Node* Child = Children.elems[0]; // :0635c38e-:0635c391
        if (Child != nullptr)                   // :0635c393-:0635c395
            delete Child; // :0635c399 (FUN_0635c310) + :0635c39f (FUN_06391d5a)
    }

    // :0635c3ad-:0635c3fd. Both vector headers torn down the standard
    // IVP_U_Vector way, elements first (note the inline-buffer guards compare
    // against this+0x28 and this+0x20 respectively, i.e. each vector's own
    // `this + 1`).
    Elements.Clear(); // :0635c3ad-:0635c3cf
    Children.Clear(); // :0635c3d3-:0635c3fd
}

// ----------------------------------------------------------------------------
// FUN_06369200 (item 3594, Common.dll.txt:06369200-0636922f, 17 insns).
// ----------------------------------------------------------------------------
uint32_t IVP_OV_HashNodeKey(const IVP_OV_Node* Node)
{
    const uint8_t* Bytes = reinterpret_cast<const uint8_t*>(Node);

    uint32_t Crc = 0xffffffffu;        // :06369205 (`OR EAX,0xffffffff`)
    for (int32_t I = 0; I < 0x14; ++I) // :06369208 (`MOV ESI,0x14`) -- 20 bytes == the five key dwords
        Crc = (Crc >> 8) ^ g_OvCrc32Table.Entry[(Bytes[I] ^ Crc) & 0xff]; // :0636920f-:06369224

    // :06369229. Force the top bit so a live tag can never be mistaken for the
    // value-set's own "slot empty" zero.
    return Crc | 0x80000000u;
}

// ============================================================================
// IVP_Mindist_Manager
// ============================================================================

// ----------------------------------------------------------------------------
// FUN_0634ebd0 (decomp-tracker item 3111, Common.dll.txt:0634ebd0-0634f13a,
// 411 insns). The whole range was read and ported this pass.
//
// The object has moved (or has just acquired collision geometry): re-file its
// OV element in the spatial subdivision, then reconcile the set of pairwise
// collision records hanging off the element against the set of objects that
// are actually within range now. The reconciliation is a compaction: every
// surviving record is swapped down into the first `KeptCount` slots of
// collision_fvector, everything past that is deleted, and every genuinely new
// partner is handed to the environment's collision delegators to build a
// fresh record for.
//
// The gap that used to be recorded here -- "insert_ov_element is a stub, so
// TouchingElements always comes back empty and this function tears down every
// existing collision record" -- is CLOSED as of 2026-09-17 (lane
// claude:w19-ovtree): the whole OV tree above is ported, so the collect really
// does come back populated now.
//
// Note the self-pair is expected in that result: the element being inserted is
// already filed in its own cell by the time collect_touching runs, and it
// trivially passes its own sphere test (dx=dy=dz=0). Retail relies on the
// `Object->original_core == OtherObj->original_core` gate below to drop it --
// that is not a missing guard.
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// FUN_06350390 / FUN_063503c0 -- IVP_Mindist_Manager ctor / dtor (cmn-phy7
// 2026-10-08; the ctor was the raw FUN_06350390 in common_stubs.cpp, the dtor
// a FULLPORT-DEFERRED gap in ~IVP_Environment).
// ----------------------------------------------------------------------------
IVP_Mindist_Manager::IVP_Mindist_Manager(IVP_Environment* Environment)
{
    // :06350395-:0635039d -- the IVP_U_Vector member ctor ran (memsize, n_elems,
    // elems = 0); :063503a0-:063503a7 REP STOSD clears all six dwords again.
    InListenerCallback = 0;
    environment = nullptr;
    ActiveMindistHead = nullptr;
    ActiveMindists.memsize = 0;
    ActiveMindists.n_elems = 0;
    ActiveMindists.elems = nullptr;
    InvalidMindistHead = nullptr;

    environment = Environment; // :063503a9-:063503ad
}

IVP_Mindist_Manager::~IVP_Mindist_Manager()
{
    // :063503df-:063503fe -- the +0x08 list. The CMP ECX,0 inside the loop is
    // always true there (the loop only continues on a non-null next).
    IVP_Ledge_Mindist* Mindist = ActiveMindistHead;
    while (Mindist != nullptr)
    {
        IVP_Ledge_Mindist* const Next = Mindist->ManagerNext; // :063503ee
        delete Mindist;                                       // :063503f3-:063503f7, slot 4 (1)
        Mindist = Next;
    }

    // :06350400-:06350419 -- the +0x14 list, same shape.
    Mindist = InvalidMindistHead;
    while (Mindist != nullptr)
    {
        IVP_Ledge_Mindist* const Next = Mindist->ManagerNext; // :06350409
        delete Mindist;                                       // :0635040e-:06350412
        Mindist = Next;
    }

    // :0635041b-:06350445 -- ~IVP_U_Vector of +0x0c (the member's destructor).
}

void IVP_Mindist_Manager::RecheckOvElement(IVP_Real_Object* Object)
{
    // :0634ebed-:0634ec0f -- a 128-slot stack vector with its buffer inline
    // right behind the header, which is exactly IVP_U_Vector_Fixed.
    IVP_U_Vector_Fixed<IVP_OV_Element, 128> TouchingElements;

    IVP_OV_Element* Element = Object->ov_element; // :0634ec1e
    if (Element == nullptr)                       // :0634ec31 -> LAB_0634f10b
        return;                                   // retail only runs the vector destructor here.

    IVP_OV_Tree_Manager* Tree = environment->ov_tree_manager; // :0634ec37
    IVP_Core* Core = Object->core; // :0634ec3d

    Tree->remove_ov_element(Element); // :0634ec44

    // :0634ec49-:0634ec50 -- `++Environment->+0x7c`, a plain counter bump.
    ++environment->counter_7c;

    // :0634ec53-:0634ec7a -- refresh the element's cached center from the
    // core's world matrix translation (IVP_Core::MWorldFCore.vv == core+0x124).
    // Retail moves the first component through the x87 stack and the other two
    // as raw dwords; all three are floats.
    Element->CenterX = Core->MWorldFCore.vv.x;
    Element->CenterY = Core->MWorldFCore.vv.y;
    Element->CenterZ = Core->MWorldFCore.vv.z;

    IVP_Hull_Manager* HullMgr = &Object->hull_manager; // :0634ec74 (`LEA EAX,[EBX+0x44]`)

    if (InListenerCallback != 0) // :0634ec81 -> LAB_0634f0e1
    {
        // Re-entrant path: we are inside the +0x2c listener's own callback, so
        // do the bare minimum -- re-file at the core's own radius, collecting
        // nothing, and re-key the hull entry with a pure epsilon bias.
        // NOTE FOR A LATER LANE: core+0x04 is declared `float UpperLimitRadius`
        // in ivp_core.h, but every use of it in THIS function is a RADIUS --
        // it is added to the environment's collision range to produce the OV
        // insert bound (:0634ec97) and subtracted back out of the insert's
        // returned radius to produce the hull key (:0634eceb), and it is the
        // sole bound passed on the re-entrant path here. That is the classic
        // IVP "upper_limit_radius" role, not an angular speed limit. Flagged,
        // not renamed: this pass did not read core+0x04's own writers.
        const float Radius = Core->UpperLimitRadius; // core+0x04
        Tree->insert_ov_element(Element, Radius, Radius, nullptr); // :0634f0f3 (:0634f0f8 FSTP ST0 discards the result)
        Element->InsertIntoHullManager(HullMgr, 1.0e-10f);         // :0634f106 (literal 0x2edbe6ff == 1e-10f)
        return;
    }

    // :0634ec8b-:0634ec97. Environment+0x1c's vtable slot 1 takes the object
    // and returns a float collision range; retail adds the core's own radius
    // to it straight away.
    // IVP_Range_Manager::get_coll_range_in_world (slot 1); its x87 result is
    // FADDed with the radius and only then FSTP'd as a float (:0634eca2).
    const double CollRange = environment->range_manager->get_coll_range_in_world(Object);
    const float Bound = (float)(CollRange + Core->UpperLimitRadius); // core+0x04 -- see the radius note above.

    // :0634ec9a-:0634ecca. Environment+0x2c's vtable slot 0 is a "this object
    // is about to be re-filed at this center/radius" notification, dispatched
    // only for objects that are actually movable ((flags & 7) != 0 -- retail
    // sign-extends the low byte first, which does not change the test) and
    // guarded against re-entry by InListenerCallback.
    IVP_Universe_Manager* const UniverseManager = environment->universe_manager; // env+0x2c
    if (UniverseManager != nullptr && (Object->flags & 0x7u) != 0)
    {
        // (:0634eca8-:0634ecb1 loads the byte at +0x78 and sign-extends it via
        // SHL 0x18 / SAR 0x18 before `TEST AL,0x7`; the sign extension cannot
        // affect bits 0-2, so this is just `flags & 7`.)
        InListenerCallback = 1; // :0634ecbe
        UniverseManager->ensure_objects_in_environment(Object, &Element->CenterX, Bound); // :0634ecc8, slot 0
        InListenerCallback = 0; // :0634ecca
    }

    // :0634ecd1-:0634ecfe. Re-file, then re-key the hull entry with the
    // difference between the new extrapolated radius and the core's own.
    const float NewRadius = Tree->insert_ov_element(Element, Bound, Bound, &TouchingElements);
    Element->InsertIntoHullManager(HullMgr, NewRadius - Core->UpperLimitRadius);

    // ---- build an open-addressed index of the EXISTING collision records ----
    // :0634ed03-:0634ed1e. Environment+0x18's vtable slot 0 is the pairwise
    // "may these two objects be collision-tested against each other" predicate
    // used further down.
    //
    // SETTLED 2026-09-17 (lane claude:w19-ovtree): env+0x18 is IVP's COLLISION
    // FILTER, not a statistic manager -- its constructor FUN_0634bcb0 builds an
    // 8-byte {vtable, owned-by-env flag} object whose slot 0, FUN_0634bc60,
    // compares the two objects' 8-byte `coll_group_ident` strings at
    // IVP_Real_Object+0x84 and refuses the pair when they match. That is stock
    // IVP_Collision_Filter_Coll_Group_Ident. The field was called
    // `statistic_manager` until 2026-09-17; see the full proof chain on the
    // field itself in ivp_environment.h.
    IVP_Collision_Filter* PairFilter = environment->collision_filter;

    // :0634ed22-:0634ed32. Table size: about 4 entries per record we expect,
    // rounded DOWN to a power of two and capped at 1024.
    const int32_t NumTouching = static_cast<int32_t>(TouchingElements.n_elems);
    const int32_t Need = (static_cast<int32_t>(Element->collision_fvector.n_elems) + NumTouching) * 4 + 4;
    int32_t TableSize = 0x400;
    if (Need < TableSize)
    {
        do
        {
            TableSize >>= 1;
        } while (TableSize > Need);
    }

    // :0634ed3a-:0634ed8e. The 1024-entry inline table covers every capped
    // case; only a pathologically large record count forces a malloc.
    uint16_t InlineTable[1024]; // the 0x800-byte stack block at ESP+0x458
    uint16_t* Table = nullptr;
    int32_t Mask = 0;
    bool TableOnHeap = false;

    const int32_t NumExisting = static_cast<int32_t>(Element->collision_fvector.n_elems);
    if (TableSize > NumExisting) // :0634ed40/:0634ed42
    {
        Mask = TableSize - 1;  // :0634ed78
        TableOnHeap = false;   // :0634ed82
        Table = InlineTable;   // :0634ed7b
    }
    else
    {
        const int32_t Want = NumExisting * 2; // :0634ed44
        TableOnHeap = true;                   // :0634ed48
        if (TableSize <= Want)                // :0634ed46/:0634ed50
        {
            do
            {
                TableSize <<= 1;
            } while (TableSize <= Want); // :0634ed52-:0634ed56
        }
        Mask = TableSize - 1;                                                       // :0634ed5b
        Table = static_cast<uint16_t*>(std::malloc(static_cast<size_t>(TableSize) * 2u)); // :0634ed5e-:0634ed63
    }

    // :0634ed99-:0634edb9 -- fill the table with 0xffff, a dword at a time.
    for (int32_t I = (Mask >> 1); I >= 0; --I)
        reinterpret_cast<uint32_t*>(Table)[I] = 0xffffffffu;

    int32_t KeptCount = 0; // :0634ed9d (the compaction cursor)

    // :0634edc2-:0634ee18 -- index every existing record by its PARTNER object.
    for (int32_t I = static_cast<int32_t>(Element->collision_fvector.n_elems) - 1; I >= 0; --I)
    {
        IVP_Collision* Coll = Element->collision_fvector.elems[I]; // :0634edcb-:0634edce
        IVP_Real_Object* Pair[2];
        Coll->get_objects(Pair); // :0634edd1-:0634edd8

        const uint32_t Key = reinterpret_cast<uint32_t>(Pair[1])
                           ^ reinterpret_cast<uint32_t>(Pair[0])
                           ^ reinterpret_cast<uint32_t>(Object); // :0634eddb-:0634edec
        uint32_t Slot = SpreadHash(Key) & static_cast<uint32_t>(Mask); // :0634edee-:0634ee00

        while (Table[Slot] != 0xffff) // :0634ee02-:0634ee11 (linear probing)
            Slot = (Slot + 1) & static_cast<uint32_t>(Mask);

        Table[Slot] = static_cast<uint16_t>(I); // :0634ee13
    }

    // :0634ee21-:0634ee4a -- a second 128-slot inline stack vector, for the
    // partners we have no record for yet.
    IVP_U_Vector_Fixed<IVP_Real_Object, 128> NewPartners;

    for (int32_t K = static_cast<int32_t>(TouchingElements.n_elems) - 1; K >= 0; --K) // :0634ee50/:0634efe7
    {
        IVP_OV_Element* Other = TouchingElements.elems[K]; // :0634ee50-:0634ee57
        IVP_Real_Object* OtherObj = Other->real_object;    // :0634ee61

        // :0634ee64-:0634ee6e -- at least one of the two has to be movable.
        if ((Object->flags & 0x7u) == 0 && (OtherObj->flags & 0x7u) == 0)
            continue;

        // :0634ee74-:0634ee80 -- objects sharing an original core never
        // collide with each other.
        if (Object->original_core == OtherObj->original_core)
            continue;

        // :0634ee86-:0634eecb -- two asymmetric core-flag gates. 0x300 and 0xc
        // are two disjoint pairs of state bits; a 0x300 object refuses both a
        // 0x300 and a 0xc partner, and a 0xc object refuses a 0x300 partner.
        // The second `TEST CH,3` at :0634eea7 re-tests a value that cannot
        // have changed -- a compiler artifact, kept structurally but with no
        // behavioural effect.
        IVP_Core* CoreA = Object->core;
        const uint32_t FlagsA = CoreA->Flags;
        if ((FlagsA & 0x300u) != 0)
        {
            if ((OtherObj->core->Flags & 0x300u) != 0)
                continue;
            if ((CoreA->Flags & 0x300u) != 0 && (OtherObj->core->Flags & 0x0cu) != 0)
                continue;
        }
        if ((FlagsA & 0x0cu) != 0)
        {
            if ((OtherObj->core->Flags & 0x300u) != 0)
                continue;
        }

        // :0634eed1-:0634eedd -- and finally the environment's own pairwise
        // filter.
        // Slot 0 (`CALL [EDX]`), IVP_Collision_Filter::check_objects_for_collision_detection.
        if (PairFilter->check_objects_for_collision_detection(Object, OtherObj) == 0)
            continue;

        // :0634eee3-:0634ef39 -- look the partner up in the table. The lookup
        // key is the partner pointer itself, because the build-side XOR above
        // cancelled `Object` out of every entry.
        uint32_t Slot = SpreadHash(reinterpret_cast<uint32_t>(OtherObj)) & static_cast<uint32_t>(Mask);
        int32_t Found = static_cast<int16_t>(Table[Slot]); // :0634eef7 (MOVSX -- 0xffff means empty)

        while (Found != -1)
        {
            IVP_Collision* Candidate = Element->collision_fvector.elems[Found]; // :0634ef04-:0634ef0e
            IVP_Real_Object* Pair[2];
            Candidate->get_objects(Pair); // :0634ef1c
            if (Pair[0] == OtherObj || Pair[1] == OtherObj) // :0634ef1f/:0634ef25
                break;
            Slot = (Slot + 1) & static_cast<uint32_t>(Mask);   // :0634ef2f
            Found = static_cast<int16_t>(Table[Slot]);         // :0634ef32
        }

        if (Found == -1)
        {
            // LAB_0634efaf -- no record yet, remember the partner.
            NewPartners.Add(OtherObj); // :0634efaf-:0634efd5
            continue;
        }

        // LAB_0634ef3d -- a record already exists; compact it down to the
        // KeptCount cursor, keeping the table's slot -> index mapping intact.
        if (Found > KeptCount) // :0634ef41/:0634ef43
        {
            IVP_Collision* CollAtCursor = Element->collision_fvector.elems[KeptCount]; // :0634ef45-:0634ef4f
            uint32_t CursorSlot = IVP_HashCollisionPartner(CollAtCursor, Object)
                                & static_cast<uint32_t>(Mask); // :0634ef52-:0634ef68

            // Retail sign-extends the table entry (MOVSX) and compares it
            // against the FULL 32-bit cursor, so this probe is written the
            // same way rather than truncating the cursor to 16 bits.
            while (static_cast<int32_t>(static_cast<int16_t>(Table[CursorSlot])) != KeptCount) // :0634ef6a-:0634ef82
                CursorSlot = (CursorSlot + 1) & static_cast<uint32_t>(Mask);

            Table[CursorSlot] = static_cast<uint16_t>(Found);      // :0634ef88
            Table[Slot] = static_cast<uint16_t>(KeptCount);        // :0634ef91
            SwapCollisionSlots(&Element->collision_fvector, KeptCount, Found); // :0634ef99
        }
        ++KeptCount; // :0634efa6
    }

    // :0634eff1-:0634f018 -- everything past the cursor is no longer in range.
    for (int32_t I = static_cast<int32_t>(Element->collision_fvector.n_elems) - 1; I >= KeptCount; --I)
    {
        IVP_Collision* Coll = Element->collision_fvector.elems[I]; // :0634f000-:0634f003
        if (Coll != nullptr)                                        // :0634f006
            delete Coll;                                            // :0634f00c-:0634f00e (vtable slot 4, arg 1 = deleting dtor)
    }

    // :0634f01a-:0634f05f -- offer every new partner to the environment's
    // collision delegators (Environment+0x12c) until one of them claims the
    // pair by returning non-zero.
    for (int32_t B = static_cast<int32_t>(NewPartners.n_elems) - 1; B >= 0; --B)
    {
        IVP_Real_Object* NewObj = NewPartners.elems[B]; // :0634f02a-:0634f02e
        for (int32_t D = static_cast<int32_t>(environment->vector_12c.n_elems) - 1; D >= 0; --D)
        {
            IVP_Collision_Delegator_Root* Delegator =
                static_cast<IVP_Collision_Delegator_Root*>(environment->vector_12c.elems[D]); // :0634f03d-:0634f046
            IVP_Collision* Claimed =
                Delegator->delegate_collisions_for_object(Object, NewObj); // :0634f054 (`CALL [EDX+0x14]`, slot 5)
            if (Claimed != nullptr)             // :0634f057/:0634f059
                break;
        }
    }

    // :0634f061-:0634f0df -- teardown, in retail's own order.
    NewPartners.Clear();
    if (TableOnHeap && Table != nullptr)
        std::free(Table);
    TouchingElements.Clear();
}

// ----------------------------------------------------------------------------
// FUN_0634f180 (decomp-tracker item 3113, Common.dll.txt:0634f180-0634f204).
// Throw away whatever OV element the object had and build a fresh one, then
// immediately re-file it. Its own caller is FUN_06344d50 (item 2783,
// IVP_Real_Object::SetCollisionGeomEnabled's Enable path), whose note has been
// asking "what does re-enabling actually build" since D227 -- this is it.
// ----------------------------------------------------------------------------
void IVP_Mindist_Manager::RebuildOvElement(IVP_Real_Object* Object)
{
    // :0634f19d-:0634f1ab -- retail dispatches vtable slot 4 with arg 1, the
    // scalar deleting destructor; `delete` is the same thing here (see
    // IVP_OV_Element::vtable's comment).
    if (Object->ov_element != nullptr)
        delete Object->ov_element;

    Object->ov_element = nullptr; // :0634f1b0

    void* Raw = ::operator new(0x30);                    // :0634f1ae-:0634f1ba
    Object->ov_element = (Raw != nullptr)                 // :0634f1c6/:0634f1d0
                       ? new (Raw) IVP_OV_Element(Object) // :0634f1d5
                       : nullptr;                         // LAB_0634f1dc

    RecheckOvElement(Object); // :0634f1ef
}
