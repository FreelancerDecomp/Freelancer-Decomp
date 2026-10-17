// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "effect_type_list.h"
#include "effect_base.h"

#include <cstdint>

FEffectTypeBucket G_EffectTypeBuckets[kEffectTypeBucketCount] = {};

namespace
{
    void** NodePrev(void* Node)
    {
        return reinterpret_cast<void**>(Node);
    }

    void** NodeNext(void* Node)
    {
        return reinterpret_cast<void**>(static_cast<unsigned char*>(Node) + 4);
    }

    void** EffectLinkSelf(void* Effect)
    {
        // Effect+0x68.
        return reinterpret_cast<void**>(static_cast<unsigned char*>(Effect) + 0x68);
    }

    unsigned* EffectTypeId(void* Effect)
    {
        return reinterpret_cast<unsigned*>(static_cast<unsigned char*>(Effect) + 0x5c);
    }

    unsigned* EffectRefCount(void* Effect)
    {
        return reinterpret_cast<unsigned*>(static_cast<unsigned char*>(Effect) + 0x58);
    }

    unsigned char* EffectFlags(void* Effect)
    {
        return static_cast<unsigned char*>(Effect) + 8;
    }

    void* EffectLinkNode(void* Effect)
    {
        return static_cast<unsigned char*>(Effect) + 0x60;
    }
}

void EffectTypeBucketPushFront(FEffectTypeBucket* Bucket, void* Node)
{
    // FUN_004fc920(this=Bucket, Node): only the node is tested (004fc924).
    if (Node == nullptr)
        return;

    *NodeNext(Node) = Bucket->Head;
    *NodePrev(Node) = nullptr;
    if (Bucket->Head != nullptr)
    {
        *NodePrev(Bucket->Head) = Node;
        Bucket->Head = Node;
        ++Bucket->Count;
        return;
    }
    Bucket->Tail = Node;
    Bucket->Head = Node;
    ++Bucket->Count;
}

void EffectTypeListLink(void* Effect)
{
    // FUN_004f7af0 (thiscall on the effect; no null test, no range test on the type id).
    if (*EffectLinkSelf(Effect) != nullptr)
        return;
    if ((*EffectFlags(Effect) & 2u) != 0)
        return;

    unsigned const TypeId = *EffectTypeId(Effect);

    void* const Node = EffectLinkNode(Effect);
    *EffectLinkSelf(Effect) = Effect;
    EffectTypeBucketPushFront(&G_EffectTypeBuckets[TypeId], Node);
    *EffectRefCount(Effect) += 1;
}

void EffectTypeListUnlinkAndRelease(void* Effect)
{
    // The sequence retail writes out at 0x004f7910 (bucket walk), 0x004f716a (FUN_004f7140) and
    // 0x004f73ea (FUN_004f73c0): only a linked effect is touched; +0x68 is cleared FIRST, the
    // +0x60 node is unlinked from bucket[+0x5c], the bucket count drops, then the reference the
    // link held is released -- signed DEC, `JG` keeps it, otherwise the object's own slot +0x20
    // runs with 1 (delete).
    if (*EffectLinkSelf(Effect) == nullptr)
        return;

    FEffectTypeBucket* const Bucket = &G_EffectTypeBuckets[*EffectTypeId(Effect)];
    void* const Node = EffectLinkNode(Effect);
    *EffectLinkSelf(Effect) = nullptr;

    void* const Prev = *NodePrev(Node);
    void* const Next = *NodeNext(Node);
    if (Prev == nullptr)
    {
        Bucket->Head = Next;
        if (Next != nullptr)
            *NodePrev(Next) = nullptr;
        else
            Bucket->Tail = nullptr;
    }
    else if (Next == nullptr)
    {
        Bucket->Tail = Prev;
        *NodeNext(Prev) = nullptr;
    }
    else
    {
        *NodeNext(Prev) = Next;
        *NodePrev(Next) = Prev;
    }
    *NodePrev(Node) = nullptr;
    *NodeNext(Node) = nullptr;
    --Bucket->Count;

    int const Remaining = static_cast<int>(*EffectRefCount(Effect)) - 1;
    *EffectRefCount(Effect) = static_cast<unsigned>(Remaining);
    if (Remaining <= 0)
    {
        typedef void(__thiscall * FDeleteSlot)(void*, int);
        delete (FEffectBase*)Effect;
    }
}

void EffectTypeListKillAll()
{
    // FUN_004f73c0 (and the first half of FUN_004f7140): for every bucket, as long as it has a
    // head, take the head's effect (node +8, i.e. Effect+0x68), run its slot +4 (kill -- which
    // links it if needed), then unlink it and drop the link's reference. The head is re-read
    // each time, so the loop ends when the bucket is empty.
    for (int Index = 0; Index < kEffectTypeBucketCount; ++Index)
    {
        FEffectTypeBucket* const Bucket = &G_EffectTypeBuckets[Index];
        while (Bucket->Head != nullptr)
        {
            void* const Effect = *reinterpret_cast<void**>(static_cast<unsigned char*>(Bucket->Head) + 8);
            typedef void(__thiscall * FKillSlot)(void*);
            ((FEffectBase*)Effect)->Kill();
            EffectTypeListUnlinkAndRelease(Effect);
        }
    }
}
