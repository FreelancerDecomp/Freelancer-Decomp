// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll BaseWatcher (one link of a Watchable's watcher chain). Split out of
// common_goods.cpp. (The FUN_062ec590 / 5b0 / 5d0 / 610 / 650 accessors that passed through
// this file are FGunConvergenceStateBlock members in common_gunconvergence.cpp.)
//
// Retail's three "clear the BaseWatcher embedded at +0x14 / +0x28 / +0x8" helpers
// (FUN_062c3040 / FUN_062c5c20 / FUN_062d27b0) are called only from EH unwind funclets
// (Unwind@06394a48 .. Unwind@0639536e): they are the compiler's member destructor on the
// exception path. In the port the BaseWatcher members' own destructor (set(nullptr)) runs
// there, so those helpers have no body of their own here.

#include "common_goods.h"
#include "common_iface.h" // IObjRW, ObjRwFromWatchable (BaseWatcher::get_watched_id)

// ---------------------------------------------------------------------------
// BaseWatcher
// ---------------------------------------------------------------------------

BaseWatcher::BaseWatcher()
    : Watched(nullptr)
    , Next(nullptr)
{
}

BaseWatcher::BaseWatcher(IObjRW* Target)
    : Watched(nullptr)
    , Next(nullptr)
{
    // Common.dll.c FUN_062c2920 (0x62920). Retail: zero Watched/Next, then if
    // Target != null, set_pointer(Target+8) -- Target+8 is IObjRW's Watchable
    // sub-object, same "+8" convention used everywhere else in this cluster
    // (ScanList::add/remove, ScannerAddEntry/ScannerRemoveEntry). Retail also has
    // a redundant "(Watchable*)(param_1+8) != 0" check that can never be false
    // once param_1 != 0 (pointer arithmetic on a non-null pointer can't yield
    // null here), so it's omitted.
    if (Target != nullptr)
    {
        Watchable* WatchedObj = Target;   // IObjRW -> its Watchable base (+8)
        set_pointer(WatchedObj);
    }
}

BaseWatcher::BaseWatcher(Watchable* const* Src)
    : Watched(nullptr)
    , Next(nullptr)
{
    // Common.dll.c FUN_062ed070 (0x62ed070). Verified against raw disasm
    // (Common.dll.txt:253086-253125): dereferences Src once (no +8 adjustment,
    // unlike the IObjRW* overload above) and additionally treats the literal
    // pointer value 0x8 as a sentinel alongside null.
    Watchable* V = *Src;
    if (V != nullptr && V != (Watchable*)8)
        set_pointer(V);
}

BaseWatcher::~BaseWatcher()
{
    set(nullptr);
}

BaseWatcher& BaseWatcher::operator=(BaseWatcher const& Other)
{
    // .text:06280ee0: a raw copy of both dwords (Watched, Next). It does not unlink from the
    // old chain or link into the new one.
    Watched = Other.Watched;
    Next = Other.Next;
    return *this;
}

void BaseWatcher::set(Watchable const* Target)
{
    if (Target != Watched)
        set_pointer(Target);
}

void BaseWatcher::set_pointer(Watchable const* Target)
{
    // Retail 0xe1610: unlink `this` from whatever chain it is currently in --
    // that chain is rooted at Watched->Head, NOT at this->Next (a prior version
    // of this port walked from `Next`, which never finds `this` and, worse,
    // never fixes up Watched->Head when `this` is the head of its chain --
    // leaving a dangling head pointer. Retail instead walks a "pointer to the
    // slot that currently holds `this`" starting at &Watched->Head, chasing
    // ->Next, exactly like the pointer-to-pointer idiom below).
    if (Watched != nullptr)
    {
        BaseWatcher** Slot = &const_cast<Watchable*>(Watched)->Head;
        while (*Slot != this)
            Slot = &(*Slot)->Next;
        *Slot = Next;
        Next = nullptr;
    }

    if (Target != nullptr)
    {
        // Link into target's watcher chain
        Next = Target->Head;
        const_cast<Watchable*>(Target)->Head = this;
    }
    Watched = Target;
}

unsigned int BaseWatcher::get_watched_id() const
{
    // FUN_062c2ab0, Common.dll.txt .text:062c2ab0-062c2ac4 (thiscall, no args):
    //   062c2ab0-062c2ab4  EAX = Watched; 0 -> return 0
    //   062c2ab6-062c2abb  ECX = Watched - 8 (the Watchable -> IObjRW downcast); 0 -> return 0
    //   062c2abd-062c2abf  tail JMP [vt + 0x20] with ECX = the IObjRW: IObjInspect slot 8,
    //                      get_id(), a plain thiscall virtual (common_iface.h).
    // The port used to call slot +0x20 through a DACOM_API (__stdcall) pointer with `this`
    // pushed on the stack: get_id then ran with a garbage ECX (wild Object read on docking,
    // CMN-BUG11) and the stack came back 4 bytes off. The typed call below is retail's.
    IObjRW* const Owner = ObjRwFromWatchable(const_cast<Watchable*>(Watched));
    if (Owner == nullptr)
        return 0;
    return Owner->get_id();
}
