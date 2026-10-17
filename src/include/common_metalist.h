// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// ============================================================================
// MetaNode<T> / MetaList<T> / OwnerList<T> -- retail's intrusive doubly linked
// list templates (Common.dll exports the GoodDesc instantiations; Common.dll,
// Server.dll and freelancer.exe each export OwnerList<pub::SpaceObj::CargoDesc>).
//
// Layout (all instantiations): vptr +0x00, Head +0x04, Tail +0x08, bUseHeap
// (a byte) +0x0C, FreeList +0x10; sizeof 0x14. A node is {Next +0x00, Prev
// +0x04, Data +0x08}, allocated with operator new(0xc). With bUseHeap set, freed
// nodes are pushed on FreeList and reused by alloc_node; otherwise they are
// deleted.
//
// Member bodies below are traced from the MetaList<GoodDesc> instantiation in
// Common.dll (.text:0627c5c0-0627ca17, plus the ICF-folded getters at
// .text:062a66a0 / 062d1ad0 and the empty assignment at .text:062ed730).
// ============================================================================

template <typename T>
class MetaNode
{
public:
    MetaNode* Next; // +0x00
    MetaNode* Prev; // +0x04
    T* Data;        // +0x08
};

template <typename T>
class MetaList
{
public:
    // .text:0627c670: Head, Tail, FreeList = 0; bUseHeap = the argument. The
    // default argument is what makes MSVC emit the exported default constructor
    // closure ??_F (.text:0627bde0, the same stores with 0).
    MetaList(bool bInUseHeap = false)
        : Head(nullptr), Tail(nullptr), bUseHeap(bInUseHeap), FreeList(nullptr)
    {
    }

    // .text:0627c930: the free() walk, then the free_heap() walk, inlined.
    virtual ~MetaList()
    {
        free();
        free_heap();
    }

    // .text:0627c6e0
    void prepend(T* Item)
    {
        MetaNode<T>* Node = alloc_node();
        Node->Next = Head;
        Node->Prev = nullptr;
        Node->Data = Item;
        if (Head != nullptr)
        {
            Head->Prev = Node;
            Head = Node;
            return;
        }
        Tail = Node;
        Head = Node;
    }

    // .text:0627c720: splice Other's chain after Tail, then empty Other (its
    // FreeList is left alone).
    void append(MetaList& Other)
    {
        MetaNode<T>* OtherHead = Other.Head;
        if (OtherHead == nullptr)
            return;
        if (Tail != nullptr)
        {
            Tail->Next = OtherHead;
            Other.Head->Prev = Tail;
        }
        else
        {
            Head = OtherHead;
        }
        Tail = Other.Tail;
        Other.Tail = nullptr;
        Other.Head = nullptr;
    }

    // .text:0627c760
    void append(T* Item)
    {
        MetaNode<T>* Node = alloc_node();
        Node->Prev = Tail;
        Node->Next = nullptr;
        Node->Data = Item;
        if (Tail != nullptr)
        {
            Tail->Next = Node;
            Tail = Node;
            return;
        }
        Head = Node;
        Tail = Node;
    }

    // .text:0627c7a0: After == 0 prepends, After == Tail appends (both inlined
    // there), anything else links the new node between After and After->Next.
    void insert_after(MetaNode<T>* After, T* Item)
    {
        if (After == nullptr)
        {
            prepend(Item);
            return;
        }
        if (Tail == After)
        {
            append(Item);
            return;
        }
        MetaNode<T>* Node = alloc_node();
        Node->Data = Item;
        Node->Prev = After;
        Node->Next = After->Next;
        After->Next = Node;
        Node->Next->Prev = Node;
    }

    // .text:0627c840: unlink, free_node (inlined), and leave Node pointing at the
    // removed node's old Prev. A null Node is a no-op.
    void remove(MetaNode<T>*& Node)
    {
        if (Node == nullptr)
            return;
        if (Head == Node)
        {
            Head = Node->Next;
            if (Head == nullptr)
                Tail = nullptr;
            else
                Head->Prev = nullptr;
        }
        else if (Tail == Node)
        {
            Tail = Tail->Prev;
            if (Tail == nullptr)
                Head = nullptr;
            else
                Tail->Next = nullptr;
        }
        else
        {
            Node->Prev->Next = Node->Next;
            Node->Next->Prev = Node->Prev;
        }
        MetaNode<T>* OldPrev = Node->Prev;
        free_node(Node);
        Node = OldPrev;
    }

    // .text:0627c8d0: a null Node starts at Head (forward) or Tail (backward);
    // otherwise it steps to Next or Prev. True while Node is non-null.
    bool traverse(MetaNode<T>*& Node, bool bForward) const
    {
        if (Node == nullptr)
            Node = bForward ? Head : Tail;
        else
            Node = bForward ? Node->Next : Node->Prev;
        return Node != nullptr;
    }

    // .text:0627c990: every node goes to FreeList (bUseHeap) or is deleted; the
    // data pointers are not touched. Then Tail = Head = 0.
    void free()
    {
        MetaNode<T>* Current = Head;
        while (Current != nullptr)
        {
            MetaNode<T>* Node = Current;
            Current = Current->Next;
            if (bUseHeap)
            {
                Node->Next = FreeList;
                FreeList = Node;
            }
            else
            {
                delete Node;
            }
        }
        Tail = nullptr;
        Head = nullptr;
    }

    // .text:0627c9d0: delete every node on FreeList, then FreeList = 0.
    void free_heap()
    {
        MetaNode<T>* Current = FreeList;
        while (Current != nullptr)
        {
            MetaNode<T>* Node = Current;
            Current = Current->Next;
            delete Node;
        }
        FreeList = nullptr;
    }

    // .text:0627ca00
    unsigned int count() const
    {
        unsigned int Result = 0;
        for (MetaNode<T>* Node = Head; Node != nullptr; Node = Node->Next)
            ++Result;
        return Result;
    }

    // .text:062d1ad0 (`MOV EAX,[ECX+4]; RET`, ICF-shared).
    MetaNode<T>* get_head() const { return Head; }

    // .text:062a66a0 (`MOV EAX,[ECX+8]; RET`, ICF-shared).
    MetaNode<T>* get_tail() const { return Tail; }

protected:
    // .text:0627c5c0: only the vptr is stored; nothing is copied or initialised.
    MetaList(MetaList const& Other)
    {
        (void)Other;
    }

    // .text:062ed730: empty (`RET 4`, ICF-shared); returns void.
    void operator=(MetaList const& Other)
    {
        (void)Other;
    }

    // .text:0627c690: pop FreeList when bUseHeap and it is not empty, otherwise
    // operator new(0xc) (the node is not initialised).
    MetaNode<T>* alloc_node()
    {
        if (bUseHeap && FreeList != nullptr)
        {
            MetaNode<T>* Node = FreeList;
            FreeList = Node->Next;
            return Node;
        }
        return new MetaNode<T>;
    }

    // .text:0627c6b0: with bUseHeap a non-null node is pushed on FreeList,
    // otherwise it is deleted.
    void free_node(MetaNode<T>* Node)
    {
        if (bUseHeap)
        {
            if (Node != nullptr)
            {
                Node->Next = FreeList;
                FreeList = Node;
            }
            return;
        }
        delete Node;
    }

    MetaNode<T>* Head;     // +0x04
    MetaNode<T>* Tail;     // +0x08
    bool bUseHeap;         // +0x0C byte
    MetaNode<T>* FreeList; // +0x10
};

// How OwnerList<T>::free releases one element. Retail's free() is `delete Data`
// on an element type with a trivial destructor, so only an operator delete call
// survives: the class one where T declares it (OwnerList<GoodDesc>, .text:0627be93
// -> ??3GoodDesc, JMP free), the global one otherwise. The generic form calls the
// global operator delete directly because OwnerList<pub::SpaceObj::CargoDesc> is
// instantiated with CargoDesc incomplete; an element type with its own operator
// delete specialises this next to its definition.
template <typename T>
inline void OwnerListDeleteData(T* Data)
{
    ::operator delete(Data);
}

// ============================================================================
// OwnerList<T> -- a MetaList<T> that owns its elements.
//
// Server's RTTI for ??_7OwnerList<CargoDesc> (.rdata:06d64f24, COL
// .rdata:06d6be3c) lists ".?AV?$MetaList@UCargoDesc@SpaceObj@pub@@@@" as the
// base, and ~OwnerList re-stamps MetaList's vftable half way through.
//
// Bodies are identical in Common.dll (GoodDesc: .text:0627be00 ctor, 0627be20
// copy ctor, 0627be50 dtor, 062ed500 operator=, 0627ca20 free; CargoDesc:
// 062ed4d0, 062ed4f0, 062eed20, 062ed500, 062eecb0), Server.dll (CargoDesc:
// 06ce3040, 06ce3060, 06ce5810, 06ce10d0, 06ce57a0) and freelancer.exe
// (CargoDesc: 004c9770, 004c9790, 004ced60, 0041c1a0, 004cecf0).
// ============================================================================
template <typename T>
class OwnerList : public MetaList<T>
{
public:
    // MetaList<T>(false) inlined, then the OwnerList vptr.
    OwnerList()
        : MetaList<T>(false)
    {
    }

    // Only the vptr: MetaList<T>'s protected copy constructor is empty.
    OwnerList(OwnerList const& Other)
        : MetaList<T>(Other)
    {
    }

    // free() inlined, then MetaList<T>'s destructor (the base runs it).
    virtual ~OwnerList()
    {
        free();
    }

    // `MOV EAX,this; RET 0x4`: MetaList<T>::operator= is empty.
    OwnerList& operator=(OwnerList const& Other)
    {
        MetaList<T>::operator=(Other);
        return *this;
    }

    // Phase 1: release each node's Data (OwnerListDeleteData) and null the slot. Phase 2: every node to FreeList (bUseHeap) or deleted.
    // Then Tail = Head = 0; FreeList is kept.
    void free()
    {
        MetaNode<T>* Current = nullptr;
        for (;;)
        {
            if (Current == nullptr)
                Current = this->Head;
            else
                Current = Current->Next;
            if (Current == nullptr)
                break;
            OwnerListDeleteData<T>(Current->Data);
            Current->Data = nullptr;
        }

        Current = this->Head;
        while (Current != nullptr)
        {
            MetaNode<T>* Node = Current;
            Current = Current->Next;
            if (this->bUseHeap)
            {
                Node->Next = this->FreeList;
                this->FreeList = Node;
            }
            else
            {
                delete Node;
            }
        }

        this->Tail = nullptr;
        this->Head = nullptr;
    }
};
