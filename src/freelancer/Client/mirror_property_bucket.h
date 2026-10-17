// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// A LocationController base runtime's per-type property cache (freelancer.exe Client\Gf):
// a MetaList<T> of heap-owned T plus a completion byte. The base runtime holds five of them in
// a row, each 0x18 bytes -- +0x00 MissionVendorProperties (MetaList vftable 0x005cb024), +0x18
// NewsBroadcastProperties (0x005cb02c), then CharacterProperties (0x005cb034), ScriptBehavior
// (0x005cb03c) and AmbientScriptProperties (0x005cb044). The MirrorDatabase listener
// (LAB_004378f0 ..) upserts into them through the LocationController (FUN_0043a960 /
// FUN_0043a9f0), and a flag setter (FUN_0043aaf0 for +0x14) marks the list complete.
//
// The MetaList half is Common's template (common_metalist.h): FUN_0043c070 / FUN_0043cbd0 are
// its (scalar deleting) destructor, FUN_0043ce50 free(), FUN_0043ce90 traverse(), FUN_0043cef0
// free_heap(), FUN_0043cf80 remove(), FUN_0043d010 free_node() -- the MissionVendorProperties
// instantiation, ICF-shared across the five types where the bodies match.
//
// Ported: the MissionVendorProperties bucket (the base runtime's +0x00, read by NN_MissionVendor)
// and the NewsBroadcastProperties bucket (+0x18, FUN_0043c310 / FUN_0043c330 / FUN_0043c3f0,
// read by NN_News). The other three types use the same template once the runtime carries them.

#include "common_metalist.h"
#include "dacom.h" // FDUMP

template <class T>
struct TMirrorPropertyBucket
{
    // FUN_0043c0d0 (thiscall, returns this): the MetaList vptr, Head / Tail / FreeList = 0,
    // bUseHeap = 0, the +0x14 byte = 0.
    TMirrorPropertyBucket()
        : List(false)
        , bComplete(0)
    {
    }

    // FUN_0043c0f0 (the base runtime's inlined teardown at 0x00439cda..0x00439d64 has the same
    // body): Clear, then MetaList's destructor (free + free_heap).
    ~TMirrorPropertyBucket()
    {
        Clear();
    }

    // 0x0043c115..0x0043c173: every element destroyed and freed and its slot nulled (a traverse
    // loop), then every node to the free list or deleted (MetaList::free), then +0x14 = 0.
    void Clear()
    {
        MetaNode<T>* Node = nullptr;
        while (List.traverse(Node, true))
        {
            delete Node->Data;
            Node->Data = nullptr;
        }
        List.free();
        bComplete = 0;
    }

    // FUN_0043c1b0 (thiscall, RET 4): the element whose key (dword +0x00, get_key) equals
    // Value's is destroyed, freed and its node removed (FUN_0043cf80); then a new T is
    // copy-assigned from Value (operator new(sizeof(T)), T(), operator=) and appended at the tail.
    void Upsert(T const& Value)
    {
        unsigned long const Key = Value.get_key();
        MetaNode<T>* Found = nullptr;
        MetaNode<T>* Node = nullptr;
        while (List.traverse(Node, true))
        {
            if (Node->Data->get_key() == Key)
            {
                Found = Node;
                break;
            }
        }
        if (Found != nullptr)
        {
            delete Found->Data;
            List.remove(Found);
        }
        T* const Copy = new T();
        *Copy = Value;
        List.append(Copy);
    }

    // The erase the LocationController inlines (MirrorDatabase.h line 0xf6; FUN_0043a9f0
    // 0x0043aa53..0x0043aae6 for +0x00, FUN_0043a750 0x0043a7b3..0x0043a848 for +0x18): the
    // first node whose element's key (dword +0x00) equals Key -- its element destroyed and freed
    // when non-null, the node removed (FUN_0043cf80). No such node: a WARNING FDUMP.
    void Erase(unsigned long Key)
    {
        MetaNode<T>* Found = nullptr;
        MetaNode<T>* Node = nullptr;
        while (List.traverse(Node, true))
        {
            if (Node->Data->get_key() == Key)
            {
                Found = Node;
                break;
            }
        }
        if (Found == nullptr)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
                  "E:\\FL\\Scratch\\Source\\Client\\Gf\\MirrorDatabase.h", 0xf6,
                  "Cannot find record to remove\n");
            return;
        }
        if (Found->Data != nullptr)
            delete Found->Data;
        List.remove(Found);
    }

    MetaList<T> List;          // +0x00 (vptr, Head +0x04, Tail +0x08, bUseHeap +0x0c, FreeList +0x10)
    unsigned char bComplete;   // +0x14 set by the LocationController (FUN_0043aaf0 for +0x00's)
    unsigned char Pad15[3];
};
