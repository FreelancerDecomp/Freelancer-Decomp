// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_phantom.h"

#include "ivp_real_object.h"
#include "ivp_environment.h"
#include "ivp_mindist.h"   // IVP_Ledge_Mindist / IVP_Synapse_Real
#include "ivp_ov_element.h" // IVP_Mindist_Manager::RecheckOvElement

// ============================================================================
// IVP_Controller_Phantom bodies (claude-code:cmn-ivp3d, 2026-10-08). Class
// layout and the listener / set contracts are in ivp_phantom.h and
// ivp_u_set_active.h.
// ============================================================================

namespace
{
    // The mindist set holds IVP_Mindist_Base, IVP_Ledge_Mindist's base class
    // (ivp_mindist.h); the upcast keeps the address.
    IVP_Mindist_Base* IVP_Phantom_AsMindistBase(IVP_Ledge_Mindist* Mindist)
    {
        return Mindist;
    }

    // The intruder of a mindist as both bodies select it inline
    // (06344455-0634445c, 063444dd-063444e4, mirrored in 06344580):
    // synapse[0]'s object, or synapse[1]'s when synapse[0]'s is the phantom's
    // own object.
    IVP_Real_Object* IVP_Phantom_OtherObject(IVP_Ledge_Mindist* Mindist, IVP_Real_Object* PhantomObject)
    {
        IVP_Real_Object* Other = Mindist->synapse[0].l_obj; // +0x28
        if (Other == PhantomObject)
        {
            Other = Mindist->synapse[1].l_obj; // +0x44
        }
        return Other;
    }
}

// FUN_06344710 (.text:06344710-063449cd, thiscall, RET 8).
IVP_Controller_Phantom::IVP_Controller_Phantom(IVP_Real_Object* Object, const IVP_Template_Phantom* Template)
    : listeners()           // :06344734-:0634473c memsize / n_elems / elems = 0
    , mindist_set(0x10)     // :0634473f-:06344767 IVP_VHash(0x10), listener vector(0)
{
    object = Object; // :06344775

    // :06344778-:06344802. The object set is stored before the store is
    // allocated (:063447c8 / :063447ee).
    if (Template->manage_intruding_objects != 0)
    {
        set_of_objects = new IVP_U_Set_Active<IVP_Real_Object>(0x10); // operator new(0x18)
        object_store = new IVP_VHash_Store(0x10);                     // operator new(0x14), FUN_063525f0
    }
    else
    {
        set_of_objects = nullptr; // :063447ff
        object_store = nullptr;   // :06344802
    }

    // :06344805-:0634488b, the same for cores (+0x34 before +0x30).
    if (Template->manage_intruding_cores != 0)
    {
        set_of_cores = new IVP_U_Set_Active<IVP_Core>(0x10);
        core_store = new IVP_VHash_Store(0x10);
    }
    else
    {
        set_of_cores = nullptr; // :06344888
        core_store = nullptr;   // :0634488b
    }

    // :0634488e-:063448b5: an owner whose movement byte (+0x78) is exactly
    // 0x10 is turned into 9 (`AND 0xffffff09; OR 9`) and re-checked by the
    // mindist manager (env +0x10), unless the template forbids it.
    if (Template->dont_check_for_unmoveables == 0 && static_cast<uint8_t>(object->flags) == 0x10)
    {
        object->flags = (object->flags & 0xffffff09u) | 0x9u;
        object->environment->mindist_manager->RecheckOvElement(object); // FUN_0634ebd0
    }

    exit_policy_extra_radius = Template->exit_policy_extra_radius; // :063448ba-:063448c0
    client_data = Template->client_data;                           // :063448c3-:063448c6

    // :063448c9-:06344906: every invalid mindist of the owner (+0x24) enters.
    // Bits: 0x1000 set / 0x2000 cleared before the call, 0x400 set / 0x800
    // cleared after it (re-read). The next link is read after the call.
    for (IVP_Synapse_Real* Synapse = object->invalid_synapse_list_head; Synapse != nullptr; Synapse = Synapse->next)
    {
        IVP_Ledge_Mindist* const Mindist = Synapse->get_mindist();
        Mindist->StateFlags = (Mindist->StateFlags & 0xffffdfffu) | 0x1000u;
        mindist_entered_volume(Mindist);
        Mindist->StateFlags = (Mindist->StateFlags & 0xfffff7ffu) | 0x400u;
    }

    // :06344908-:06344964: every exact mindist of the owner (+0x20). The
    // flags get 0x1000 / ~0x2000 first; then `FLD [M+0x54]; FCOMP [063a53d4]
    // (0.0f); FNSTSW; TEST AH,0x1; JZ else`: C0 clear (gap >= 0, ordered)
    // jumps to the else arm, C0 set (gap < 0, or unordered) falls through
    // into entered. The else arm stores the pre-computed value with 0xc00
    // cleared (`AND ECX,0xfffff3ff`), it does not re-read the flags.
    for (IVP_Synapse_Real* Synapse = object->synapse_list_head; Synapse != nullptr; Synapse = Synapse->next)
    {
        IVP_Ledge_Mindist* const Mindist = Synapse->get_mindist();
        const uint32_t Flags = (Mindist->StateFlags & 0xffffdfffu) | 0x1000u;
        Mindist->StateFlags = Flags;
        if (!(Mindist->SeparationGap >= 0.0f))
        {
            mindist_entered_volume(Mindist);
            Mindist->StateFlags = (Mindist->StateFlags & 0xfffff7ffu) | 0x400u;
        }
        else
        {
            Mindist->StateFlags = Flags & 0xfffff3ffu;
        }
    }

    // :06344966-:063449b7: walk the owner's hull manager pool (+0x64) from
    // PoolLiveIndex along Entry::Next. A null payload ends the walk; a
    // listener whose get_type() (slot 0) is 0 is a synapse, and its mindist
    // gets 0x1000 / ~0x2000 unless 0x3000 is already set (`TEST AH,0x30`).
    IVP_Priority_Pool& Pool = object->hull_manager.Pool();
    uint32_t Index = Pool.PoolLiveIndex;
    while (Index != 0xffffu)
    {
        IVP_Priority_Pool::Entry* const Entry = &Pool.PoolEntries[Index];
        IVP_Listener_Hull* const Listener = static_cast<IVP_Listener_Hull*>(Entry->Payload);
        Index = Entry->Next;
        if (Listener == nullptr)
        {
            break;
        }
        if (Listener->get_type() != 0)
        {
            continue;
        }
        IVP_Ledge_Mindist* const Mindist = static_cast<IVP_Synapse_Real*>(Listener)->get_mindist();
        if ((Mindist->StateFlags & 0x3000u) != 0)
        {
            continue;
        }
        Mindist->StateFlags = (Mindist->StateFlags & 0xffffdfffu) | 0x1000u;
    }

    // +0x38 / +0x3c are not written (retail leaves the operator new bytes).
}

// FUN_063442c0 (.text:063442c0-063443fd).
IVP_Controller_Phantom::~IVP_Controller_Phantom()
{
    // :063442e1-:063442ff: every listener, last first, slot 2.
    for (int32_t I = static_cast<int32_t>(listeners.n_elems) - 1; I >= 0; --I)
    {
        listeners.elems[I]->phantom_is_going_to_be_deleted_event(this);
    }

    // :06344301-:06344305: the owner forgets its phantom.
    object->first_child = nullptr;

    // :06344308-:0634435e, in this order: +0x30, +0x2c (FUN_06352620 then
    // operator delete), +0x28, +0x34 (virtual slot 1 with 1). Each pointer is
    // nulled after its test.
    delete core_store;
    core_store = nullptr;
    delete object_store;
    object_store = nullptr;
    delete set_of_objects;
    set_of_objects = nullptr;
    delete set_of_cores;
    set_of_cores = nullptr;

    // :0634435b-:063443ec: the members, mindist_set (inline FUN_06344a00) then
    // the listener vector, run as member destructors.
}

// FUN_06344400 (.text:06344400-0634457c, RET 4).
void IVP_Controller_Phantom::mindist_entered_volume(IVP_Ledge_Mindist* Mindist)
{
    IVP_Mindist_Base* const MindistBase = IVP_Phantom_AsMindistBase(Mindist);

    // :06344401-:0634444c.
    mindist_set.add_element(MindistBase);

    // :0634444e-:063444d0. The store counts how many mindists of this
    // phantom touch the object; only the first one adds it to the set.
    if (set_of_objects != nullptr)
    {
        IVP_Real_Object* const Other = IVP_Phantom_OtherObject(Mindist, object);
        const int32_t Count = static_cast<int32_t>(reinterpret_cast<intptr_t>(object_store->FindElem(Other))); // FUN_06352930
        if (Count != 0)
        {
            object_store->ChangeElem(Other, reinterpret_cast<void*>(static_cast<intptr_t>(Count + 1))); // FUN_063529e0
        }
        else
        {
            object_store->AddElem(Other, reinterpret_cast<void*>(static_cast<intptr_t>(1))); // FUN_063526d0
            set_of_objects->add_element(Other);
        }
    }

    // :063444d2-:0634455e: the same for the object's core (+0x98).
    if (set_of_cores != nullptr)
    {
        IVP_Core* const Core = IVP_Phantom_OtherObject(Mindist, object)->core;
        const int32_t Count = static_cast<int32_t>(reinterpret_cast<intptr_t>(core_store->FindElem(Core)));
        if (Count != 0)
        {
            core_store->ChangeElem(Core, reinterpret_cast<void*>(static_cast<intptr_t>(Count + 1)));
        }
        else
        {
            core_store->AddElem(Core, reinterpret_cast<void*>(static_cast<intptr_t>(1)));
            set_of_cores->add_element(Core);
        }
    }

    // :06344560-:06344576: every phantom listener, last first, slot 0.
    for (int32_t I = static_cast<int32_t>(listeners.n_elems) - 1; I >= 0; --I)
    {
        listeners.elems[I]->mindist_entered_volume(this, MindistBase);
    }
}

// FUN_06344580 (.text:06344580-063446fe, RET 4): the mirror of 06344400.
void IVP_Controller_Phantom::mindist_left_volume(IVP_Ledge_Mindist* Mindist)
{
    IVP_Mindist_Base* const MindistBase = IVP_Phantom_AsMindistBase(Mindist);

    // :06344581-:063445cd.
    mindist_set.remove_element(MindistBase);

    // :063445cf-:06344651. `CMP EAX,1; JLE` (signed): a count above 1 is
    // decremented, anything else removes the object.
    if (set_of_objects != nullptr)
    {
        IVP_Real_Object* const Other = IVP_Phantom_OtherObject(Mindist, object);
        const int32_t Count = static_cast<int32_t>(reinterpret_cast<intptr_t>(object_store->FindElem(Other)));
        if (Count > 1)
        {
            object_store->ChangeElem(Other, reinterpret_cast<void*>(static_cast<intptr_t>(Count - 1)));
        }
        else
        {
            object_store->RemoveElem(Other); // FUN_063527e0
            set_of_objects->remove_element(Other);
        }
    }

    // :06344653-:063446df.
    if (set_of_cores != nullptr)
    {
        IVP_Core* const Core = IVP_Phantom_OtherObject(Mindist, object)->core;
        const int32_t Count = static_cast<int32_t>(reinterpret_cast<intptr_t>(core_store->FindElem(Core)));
        if (Count > 1)
        {
            core_store->ChangeElem(Core, reinterpret_cast<void*>(static_cast<intptr_t>(Count - 1)));
        }
        else
        {
            core_store->RemoveElem(Core);
            set_of_cores->remove_element(Core);
        }
    }

    // :063446e1-:063446f8: every phantom listener, last first, slot 1.
    for (int32_t I = static_cast<int32_t>(listeners.n_elems) - 1; I >= 0; --I)
    {
        listeners.elems[I]->mindist_left_volume(this, MindistBase);
    }
}

// FUN_06344230 (.text:06344230-0634425a, RET 4): IVP_U_Vector::add on +0x08
// (grow through FUN_063441a0 when n_elems >= memsize, unsigned `JC`).
void IVP_Controller_Phantom::add_listener_phantom(IVP_Listener_Phantom* Listener)
{
    listeners.Add(Listener);
}

// FUN_06344260 (.text:06344260-063442b0, RET 4): IVP_U_Vector::remove_element
// inlined on +0x08 -- a backward search, then the count drops by one and the
// tail moves down over the found slot.
void IVP_Controller_Phantom::remove_listener_phantom(IVP_Listener_Phantom* Listener)
{
    // :06344261-:06344286: Index ends at -1 when Listener is absent (and is
    // -1 straight away when the vector is empty, `DEC EAX; JS`).
    int32_t Index = static_cast<int32_t>(listeners.n_elems) - 1;
    while (Index >= 0)
    {
        if (listeners.elems[Index] == Listener)
        {
            break;
        }
        --Index;
    }

    // VERY STRANGE, REPRODUCED ON PURPOSE (:06344288-:063442ac): retail has no
    // not-found test. The count is decremented regardless (an empty vector
    // wraps to 0xffff) and the shift loop runs from Index = -1, so an absent
    // listener writes elems[-1] = elems[0] and shifts the whole vector down
    // by one; on an empty vector the loop walks 0x10000 slots of foreign
    // memory. The loop bound re-reads the (unsigned 16-bit) count each pass.
    listeners.n_elems = static_cast<unsigned short>(listeners.n_elems - 1);
    for (int32_t I = Index; I < static_cast<int32_t>(listeners.n_elems); ++I)
    {
        listeners.elems[I] = listeners.elems[I + 1];
    }
}
