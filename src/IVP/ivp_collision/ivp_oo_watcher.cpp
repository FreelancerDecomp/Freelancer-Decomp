// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP_OO_Watcher / IVP_Synapse_OO -- Common.dll .text:06369a00-06369e68
// (cmn-ivp2b, 2026-10-08). See ivp_oo_watcher.h for the class layout and the
// retail vtables.

#include "ivp_oo_watcher.h"

#include "ivp_mindist_create.h"                 // IVP_Create_Exact_Mindists (FUN_0634e0b0)
#include "../ivp_physics/ivp_real_object.h"     // IVP_Real_Object, IVP_Hull_Manager
#include "../ivp_physics/ivp_environment.h"     // IVP_Environment, IVP_Range_Manager
#include "../ivp_physics/ivp_core.h"            // IVP_Time

#include <cstddef>
#include <cstdlib>

static_assert(offsetof(IVP_Synapse_OO, HullPoolSlot) == 0x04, "IVP_Synapse_OO+0x04");
static_assert(offsetof(IVP_Synapse_OO, object) == 0x08, "IVP_Synapse_OO+0x08");
static_assert(offsetof(IVP_Synapse_OO, watcher) == 0x0c, "IVP_Synapse_OO+0x0c");
static_assert(offsetof(IVP_OO_Watcher, synapses) == 0x18, "IVP_OO_Watcher+0x18");
static_assert(offsetof(IVP_OO_Watcher, mindists) == 0x38, "IVP_OO_Watcher+0x38");

namespace
{
    // DAT_063a58cc, bits 0x60ad78ec: the library's P_FLOAT_MAX (1e20f). Added
    // to the first key so a freshly initialised synapse never fires before
    // the constructor's own hull_limit_exceeded_event re-keys it.
    constexpr float GSynapseOOInitialKeyBias = 1.0e20f;

    // The hull key every insert below builds, kept on the x87 stack until the
    // FSTP dword into the argument slot:
    //   FLD TimeHi; FSUB [obj+0x44]; FADD TimeLo; FSUB [obj+0x48];
    //   FMUL [obj+0x4c]; FADD [obj+0x54]; FADD Bias; FSTP float.
    // Intermediates are evaluated in double; the only rounding is the final
    // store.
    float HullKeyAtTime(const IVP_Hull_Manager& Hull, float TimeHi, float TimeLo, float Bias)
    {
        const double Elapsed = (((double)(TimeHi) - Hull.LastEditTimeHi) + TimeLo) - Hull.LastEditTimeLo;
        return (float)(Elapsed * Hull.OuterGradient + Hull.OuterValueAtLastEdit + Bias);
    }
}

// ============================================================================
// IVP_Synapse_OO
// ============================================================================

// FUN_06369b70 (.text:06369b70-06369bca, RET 8). Stack args (watcher, object):
// the ctor pushes the object first and the watcher last (:06369c74/:06369c75).
void IVP_Synapse_OO::init_synapse_oo(IVP_OO_Watcher* Watcher, IVP_Real_Object* Object)
{
    object = Object;                                                       // :06369b7a
    // :06369b7d-:06369b8c -- the time pair of the object's environment.
    const IVP_Environment* Environment = Object->environment;
    const float TimeHi = Environment->current_time.time_base;              // env+0xf8
    const float TimeLo = Environment->current_time.time_offset;            // env+0xfc
    // :06369b90-:06369bb2 -- the key plus DAT_063a58cc, then FUN_06362f90 on
    // object+0x64 (hull_manager+0x20, the hull pool) with this as payload.
    const float Key = HullKeyAtTime(Object->hull_manager, TimeHi, TimeLo, GSynapseOOInitialKeyBias);
    HullPoolSlot = Object->hull_manager.PoolInsert(this, Key);             // :06369bbb
    watcher = Watcher;                                                     // :06369bbe
}

// FUN_06369b30 (.text:06369b30-06369b49): restamp this class's vptr (the
// compiler does that), then FUN_06363280 on object+0x64 with the stored slot.
IVP_Synapse_OO::~IVP_Synapse_OO()
{
    object->hull_manager.PoolRemove(static_cast<uint16_t>(HullPoolSlot));
}

// LAB_06369a00 (.text:06369a00-06369a0a, RET 8).
void IVP_Synapse_OO::hull_limit_exceeded_event(IVP_Hull_Manager* /*HullManager*/, float /*HullIntrusionValue*/)
{
    watcher->hull_limit_exceeded_event();
}

// LAB_06369b20 (.text:06369b20-06369b29, RET 4) -> FUN_06369bd0
// (`TEST ECX,ECX; JZ; PUSH 1; CALL [EAX+0x10]`).
void IVP_Synapse_OO::hull_manager_is_going_to_be_deleted_event(IVP_Hull_Manager* /*HullManager*/)
{
    if (watcher != nullptr)
        delete watcher;
}

// ============================================================================
// IVP_OO_Watcher
// ============================================================================

// FUN_06369be0 (.text:06369be0-06369ca6, RET 0xc). Order of the retail body:
// IVP_Collision's inlined ctor (+0x08 = delegator, +0x10 = +0x0c = -1; +0x04
// is not written), the delegator base vptr at +0x14, the synapse array (vptrs
// only), the vector (memsize 8, n_elems 0, elems = FUN_06343f70(0x20) =
// malloc), the derived vptrs, init_synapse_oo on both synapses, and finally
// hull_limit_exceeded_event.
IVP_OO_Watcher::IVP_OO_Watcher(IVP_Collision_Delegator* Delegator, IVP_Real_Object* Object0, IVP_Real_Object* Object1)
    : IVP_Collision(Delegator)                                                     // :06369bf9-:06369c0f
    , IVP_Collision_Delegator()                                                    // :06369c1b
    , mindists(static_cast<IVP_Collision**>(std::malloc(0x20)), 8)                 // :06369c3f-:06369c58
{
    synapses[0].init_synapse_oo(this, Object0); // :06369c70-:06369c77
    synapses[1].init_synapse_oo(this, Object1); // :06369c7c-:06369c85
    hull_limit_exceeded_event();                // :06369c8a
}

// FUN_06369ce0 (.text:06369ce0-06369da6). Body: delete every mindist from the
// top down (`CALL [EDX+0x10]` with 1, null entries skipped; each deletion calls
// back into collision_is_going_to_be_deleted_event below and shrinks the
// vector), then report this record to its own delegator (`[this+8]->slot0
// (this)`). The rest of the retail function is the member/base teardown the
// compiler emits here in the same order: the vector's inline-buffer-aware
// clear (:06369d3b-:06369d71), the `eh vector destructor iterator` over both
// synapses (FUN_06391dfc with FUN_06369b30, :06369d7c), the delegator vptr at
// +0x14 and IVP_Collision's vptr.
IVP_OO_Watcher::~IVP_OO_Watcher()
{
    for (int32_t I = static_cast<int32_t>(mindists.n_elems) - 1; I >= 0; --I) // :06369d0e-:06369d31
    {
        IVP_Collision* Mindist = mindists.elems[I];                               // :06369d1f-:06369d22 (elems re-read)
        if (Mindist != nullptr)
            delete Mindist;                                                       // :06369d29-:06369d2d
    }
    delegator->collision_is_going_to_be_deleted_event(this);                       // :06369d33-:06369d39
}

// FUN_06369a10 (.text:06369a10-06369b1a, plain RET).
void IVP_OO_Watcher::hull_limit_exceeded_event()
{
    IVP_Real_Object* Object0 = synapses[0].object;          // :06369a17
    IVP_Environment* Environment = Object0->environment;    // :06369a1a
    IVP_Real_Object* Object1 = synapses[1].object;          // :06369a21

    ++Environment->range_intra_exceeded;                    // :06369a1d/:06369a2b/:06369a32

    // :06369a35-:06369a3a -- range manager slot 0 with (obj0, obj1, &R0, &R1).
    float Range0;
    float Range1;
    Environment->range_manager->get_coll_range_intra_objects(Object0, Object1, &Range0, &Range1);

    // :06369a3c-:06369a6a -- FUN_0634e0b0(obj0, obj1, (float)(R0 + R1),
    // &mindists, 0, 0, 0, 0, delegator = this+0x14), cdecl (ADD ESP,0x24).
    const float ScanRadius = (float)((double)(Range0) + Range1); // FLD / FADD / FSTP float
    IVP_Create_Exact_Mindists(Object0, Object1, ScanRadius, &mindists,
                              nullptr, nullptr, nullptr, nullptr,
                              static_cast<IVP_Collision_Delegator*>(this));

    // :06369a6f-:06369a75 -- the time pair is read once, from obj0's
    // environment, and used for both sides.
    const float TimeHi = Environment->current_time.time_base;   // env+0xf8
    const float TimeLo = Environment->current_time.time_offset; // env+0xfc

    // :06369a8c-:06369ab9 -- side 0: FUN_06363280(synapse0 slot) then
    // FUN_06362f90(synapse0, key + R0) on obj0+0x64; the slot goes to +0x1c.
    Object0->hull_manager.PoolRemove(static_cast<uint16_t>(synapses[0].HullPoolSlot));
    synapses[0].HullPoolSlot = Object0->hull_manager.PoolInsert(
        &synapses[0], HullKeyAtTime(Object0->hull_manager, TimeHi, TimeLo, Range0));

    // :06369abe-:06369b0c -- side 1 on obj1+0x64 with R1; the slot goes to +0x2c.
    Object1->hull_manager.PoolRemove(static_cast<uint16_t>(synapses[1].HullPoolSlot));
    synapses[1].HullPoolSlot = Object1->hull_manager.PoolInsert(
        &synapses[1], HullKeyAtTime(Object1->hull_manager, TimeHi, TimeLo, Range1));
}

// LAB_06369e20 (.text:06369e20-06369e2f, RET 4).
void IVP_OO_Watcher::get_objects(IVP_Real_Object* Out[2])
{
    Out[0] = synapses[0].object; // +0x20
    Out[1] = synapses[1].object; // +0x30
}

// LAB_06369e40 (.text:06369e40-06369e51, RET 4).
void IVP_OO_Watcher::get_ledges(const IVP_Compact_Ledge* Out[2])
{
    Out[0] = nullptr;
    Out[1] = nullptr;
}

// LAB_06369db0 (.text:06369db0-06369e1b, RET 4), `this` = the +0x14
// sub-object. The swap-with-last removal of the collision from `mindists`
// through the collision's own two back-indices -- the same shape as
// IVP_OV_Element::remove_oo_collision (FUN_0635c250).
void IVP_OO_Watcher::collision_is_going_to_be_deleted_event(IVP_Collision* Collision)
{
    // :06369db6-:06369dcf -- IndexInElement0 is trusted only when in range
    // (`JL` / `JGE`, signed) and the slot really holds this collision.
    int32_t Index = Collision->IndexInElement0;
    if (Index < 0 || Index >= static_cast<int32_t>(mindists.n_elems) || mindists.elems[Index] != Collision)
        Index = Collision->IndexInElement1;

    --mindists.n_elems;                                        // :06369dd2
    const int32_t Last = static_cast<int32_t>(mindists.n_elems);
    if (Last > Index)                                          // :06369de1 `CMP EAX,EDX; JLE`
    {
        IVP_Collision* Moved = mindists.elems[Last];           // :06369de8
        mindists.elems[Index] = Moved;                         // :06369deb
        if (Moved->IndexInElement0 == Last)                    // :06369df4
            Moved->IndexInElement0 = Index;
        else
            Moved->IndexInElement1 = Index;                    // :06369dfe
    }
    if (Collision->IndexInElement0 == Index)                   // :06369e01
        Collision->IndexInElement0 = -1;
    else
        Collision->IndexInElement1 = -1;                       // :06369e12
}
