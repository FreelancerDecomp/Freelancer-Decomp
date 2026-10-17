// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- IAction (the action interface, with its own operator new/delete) and ActionDB
// (the per-object action list).
// CMN-SPLIT stage 1 (2026-10-14): split out of common_iface.cpp unchanged.

#include "common_cequip.h" // IAction, ActionDB

#include <cstdlib>
#include <cstring>
#include <new>

// ============================================================================
// IAction
// ============================================================================

IAction::IAction()
{
    // Ghidra @ 0x27720
    Next = nullptr;
}

IAction::IAction(IAction const& Other)
{
    // Ghidra @ 0x27730
    Next = Other.Next;
}

IAction::~IAction()
{
    // Ghidra @ 0x3FF20: trivial dtor
}

IAction& IAction::operator=(IAction const& Other)
{
    // .text:06287760..0628777c: copies Other+4 into this+4 and returns this; no self test.
    Next = Other.Next;
    return *this;
}

void* IAction::operator new(size_t Size)
{
    // ICF-folded @ 0x9D560 with FmtStr::Val::operator new
    return malloc(Size);
}

void IAction::operator delete(void* Ptr)
{
    // ICF-folded @ 0x9D570 with FmtStr::Val::operator delete
    free(Ptr);
}

char const* IAction::get_id() const
{
    // Retail 0x3ff10: MOV EAX,.rdata:DAT_063997b0 (empty string ""); RET
    // Base class always returns empty string. Derived classes override.
    return "";
}

// ============================================================================
// ActionDB
// ============================================================================

ActionDB::ActionDB()
    : Head(nullptr)
    , Tail(nullptr)
{
    // Ghidra @ 0x277E0
}

ActionDB::~ActionDB()
{
    // Ghidra @ 0x400C0
    free();
}

ActionDB& ActionDB::operator=(ActionDB const& Other)
{
    // Ghidra @ 0x06280ee0 (retail RVA "0x20EE0" comment format; base 0x06260000, NOT
    // 0x06280000+0xee0 as a previous pass assumed -- that miscalculation pointed at an
    // unrelated generic std::vector<T>::insert instantiation, see tracker item 947/
    // FUN_062a0ee0's notes). Confirmed via Common.dll.txt:64244-64265: retail's
    // ??4ActionDB@@QAEAAU0@ABU0@@Z is ICF-folded together with several other classes'
    // operator= (DamageObjInfo, BaseWatcher, CArchGrpTraverser, CFLIDMaker,
    // CollisionGroupDesc, CountermeasureUseStruct, option_spec -- all plain 2-dword
    // structs) into one shared trivial-struct-assignment body: `this->Field0 =
    // Other->Field0; this->Field4 = Other->Field4; return this;`. For ActionDB that is
    // exactly `Head = Other.Head; Tail = Other.Tail;` -- a raw shallow pointer copy with
    // NO ownership transfer, NO deep copy of the list, and (unlike a previous pass's
    // guess) no `free()` of the destination's existing actions either. Reproduced
    // faithfully: this aliases both ActionDBs onto the same intrusive list, exactly as
    // retail does (retail truly has this aliasing/potential-double-free hazard; not
    // "improved" here since no port caller currently exercises this operator).
    Head = Other.Head;
    Tail = Other.Tail;
    return *this;
}

void ActionDB::add(IAction* Action)
{
    // .text:0629ff30..0629ff4d: links Action at the tail. No null test, and Action's own
    // link (+4) is left as it is -- IAction's ctor and remove() are what zero it.
    if (Tail != nullptr)
    {
        Tail->Next = Action;
        Tail = Action;
    }
    else
    {
        Head = Action;
        Tail = Action;
    }
}

void ActionDB::update(float Dt)
{
    // Ghidra @ 0x3FF50. Retail dispatches through the raw vtable slot1 (IAction's
    // purecall update() slot); now that IAction::update() is a real virtual in the
    // port, use normal C++ dispatch instead of poking the vtable by hand. On false retail
    // inlines remove()'s body (0629ff73..0629ffb2: scan from Head, unlink, Tail fix-up,
    // next = 0, then vt+8 deleting dtor); Current is never null here, so calling remove()
    // runs exactly those steps. Next is read before update(Dt), as at 0629ff66.
    IAction* Current = Head;
    while (Current != nullptr)
    {
        IAction* Next = Current->Next;
        bool bKeep = Current->update(Dt);
        if (!bKeep)
        {
            remove(Current);
        }
        Current = Next;
    }
}

bool ActionDB::remove(IAction* Action)
{
    // Ghidra @ 0x3FFD0. Fixed a genuine behavior mismatch: retail's found-case and
    // not-found-case both fall through to one shared tail block (LAB_062a000e) that
    // unconditionally deletes the ARGUMENT `Action` (via its vtable deleting-destructor
    // slot, i.e. `delete Action`) whenever it is non-null -- whether or not Action was
    // actually linked into this list. The previously ported version only deleted Action
    // when the scan found it, silently leaking it (and leaving the caller with a
    // dangling-but-undeleted pointer relative to retail) on the not-found path. Now
    // reproduces "always delete a non-null Action" faithfully. There is no null test on
    // entry: the result is SETZ(Curr == Action) at 062a000e, so remove(nullptr) scans to
    // the end (Curr = 0) and returns true without deleting anything.
    IAction* Prev = nullptr;
    IAction* Curr = Head;
    while (Curr != nullptr)
    {
        if (Curr == Action)
        {
            IAction* const Next = Curr->Next;
            if (Prev != nullptr)
            {
                Prev->Next = Next;
            }
            else
            {
                Head = Next;
            }
            if (Curr == Tail)
            {
                Tail = Prev;
            }
            Action->Next = nullptr;
            break;
        }
        Prev = Curr;
        Curr = Curr->Next;
    }
    bool const bResult = (Curr == Action);
    if (Action != nullptr)
    {
        delete Action;
    }
    return bResult;
}

IAction* ActionDB::find(char const* Name) const
{
    // .text:062a0030..062a009c: vt[0] get_id on each node, inline strcmp(Name, Id); no
    // null test on either string.
    for (IAction* Curr = Head; Curr != nullptr; Curr = Curr->Next)
    {
        char const* ActionId = Curr->get_id();
        if (strcmp(Name, ActionId) == 0)
        {
            return Curr;
        }
    }
    return nullptr;
}

int ActionDB::get_action_count() const
{
    // Ghidra @ 0x400A0
    int Count = 0;
    for (IAction* Curr = Head; Curr != nullptr; Curr = Curr->Next)
    {
        ++Count;
    }
    return Count;
}

void ActionDB::free()
{
    // Ghidra @ 0x400C0
    IAction* Curr = Head;
    while (Curr != nullptr)
    {
        IAction* Next = Curr->Next;
        delete Curr;
        Curr = Next;
    }
    Head = nullptr;
    Tail = nullptr;
}
