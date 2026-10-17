// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU40: abstract base of the [ObjList] directive objects built by the factory
// FUN_06f24870 (switch on the directive type, 0x19 cases) and kept in the TU50 ship's
// directive list (FContentShipBase::Directives, +0x158).
//
// Retail vtable 06fb8314 (the base, written inline by every derived ctor before its own):
//   +0x00 06eacd40 AddRef          (ICF-shared with FContentPlayerStateBase)
//   +0x04 06eacd50 Release         (at 0 -> vtbl[+8](1))
//   +0x08 06f06eb0 scalar deleting dtor (dtor body FUN_06ea8cc0: vptr reset only)
//   +0x0c purecall Execute(ship)   e.g. SetFlag directive 06f25f60 (SubmitState on the ship)
//   +0x10 06ebf6f0 Parse(INI_Reader*) default: RET 4
//   +0x14 purecall GetType         e.g. SetFlag directive 06ea1450 returns 0; 0x13 = goto
//                                  (checked by FUN_06f579c0)
//   +0x18 06f25f30 Save(FILE*)     if Name.Length: fprintf(File, "%s\n", Name.Data)
// Fields (factory case 0, 06f248bf-06f248cd): +4 RefCount = 1, +8 Name.Length = 0,
// +0xc Name.Data[0] = 0. The first derived member sits at +0x10c (SetFlag directive's
// SetFlagParams), so Name is TString<0x100>.
//
// Owner of the derived classes: CNT-MS-TRIG (TU40). CNT-AI3 published only this base
// because the TU50 ship base calls its slots.

#include "ContentRefObject.h"   // FContentRefObject (the ref-counted base 06fb43f8)
#include "common_tstring.h"

#include <cstdio>
#include <list>

class INI_Reader;
class FContentShipBase;

// Slots 0-2 and +0x04 are the ref-counted base FContentRefObject (vtable 06fb43f8).
class FObjListDirective : public FContentRefObject
{
public:
    TString<0x100> Name;           // +0x08

    FObjListDirective()
    {
        Name.Length = 0;
        Name.Data[0] = '\0';
    }

    // Slot 2 (06f06eb0 -> FUN_06ea8cc0).
    ~FObjListDirective() override = default;

    // Slot 3 (+0x0c): run the directive on the ship; false = finished at once (the ship
    // then advances to the next one, FUN_06f57b00 06f57bc6).
    virtual bool Execute(FContentShipBase* Ship) = 0;

    // Slot 4 (+0x10), base 06ebf6f0: nothing.
    virtual void Parse(INI_Reader* Reader)
    {
        (void)Reader;
    }

    // Slot 5 (+0x14).
    virtual int GetType() = 0;

    // Slot 6 (+0x18), base 06f25f30 (format DAT_06fb91d0 "%s\n").
    virtual void Save(FILE* File)
    {
        if (Name.Length != 0)
            fprintf(File, "%s\n", Name.Data);
    }
};

// A list of [ObjList] directives holding one reference on each element. Retail keeps these as
// bare std::list<Directive*> and inlines the reference handling at every use: the empty list
// FUN_06f1d4c0, the assignment FUN_06f20230 (slot 1 Release + erase on the old elements, list
// assign, slot 0 AddRef on every new element; a copy is the empty list + this), the teardown
// FUN_06ed2a00 (slot 1 Release on every element, then erase) + FUN_06f15350 (free the head);
// FUN_06ecfa90 is that teardown as an unwind action (FContentShipBase +0x158: FuncInfo
// 06fc3b30 state 3, 06fc3b8c state 6).
// Used by the mission ship slot (+0x58), the [ObjList] map (+0x100), the guides' directive
// lists and the ship base's queue (FContentShipBase::Directives, +0x158). Moved here from
// MissionObject.h so that BehaviorCmds.h can hold one (MissionObject.h includes it through
// NpcShip.h).
class FMissionDirectiveList
{
public:
    FMissionDirectiveList() = default;
    // FUN_06f1d4c0 + FUN_06f20230 on the empty list: the elements copied, each AddRef'ed.
    FMissionDirectiveList(FMissionDirectiveList const& Other);
    // FUN_06f20230 (also inlined as the +0x58 part of FUN_06ed34e0): Release every old element,
    // erase (FUN_06fa8370 / FUN_06ecfec0), assign (FUN_06f93490), AddRef every new element.
    FMissionDirectiveList& operator=(FMissionDirectiveList const& Other);
    // FUN_06ed2a00 + FUN_06f15350.
    ~FMissionDirectiveList();

    // FUN_06ed2a00: Release every element, then erase them.
    void Clear();

    std::list<FObjListDirective*> Items;
};

// The empty list + FUN_06f20230: copy the nodes, AddRef (slot 0) on each copied directive.
inline FMissionDirectiveList::FMissionDirectiveList(FMissionDirectiveList const& Other)
    : Items(Other.Items)
{
    std::list<FObjListDirective*>::iterator It = Items.begin();
    for (; It != Items.end(); ++It)
        (*It)->AddRef();
}

// FUN_06f20230 (06f20230-06f202eb, RET 4) and the +0x58 part of FUN_06ed34e0
// (06ed35c2-06ed364e): Release the old elements, erase them, assign Other's nodes (skipped when
// Other is this list), AddRef each.
inline FMissionDirectiveList& FMissionDirectiveList::operator=(FMissionDirectiveList const& Other)
{
    Clear();
    Items = Other.Items;
    std::list<FObjListDirective*>::iterator It = Items.begin();
    for (; It != Items.end(); ++It)
        (*It)->AddRef();
    return *this;
}

inline FMissionDirectiveList::~FMissionDirectiveList()
{
    Clear();
}

// FUN_06ed2a00: Release (slot 1) on every element, then erase.
inline void FMissionDirectiveList::Clear()
{
    std::list<FObjListDirective*>::iterator It = Items.begin();
    for (; It != Items.end(); ++It)
        (*It)->Release();
    Items.clear();
}
