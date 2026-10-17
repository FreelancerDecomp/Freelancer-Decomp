// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_misc.h"

#include <cstdlib>
#include <cstring>

// ============================================================================
// Heap
// ============================================================================

Heap::Heap(unsigned int ElementSize, int MaxFreeCount)
    : ElementSize(ElementSize), FreeList(nullptr), MaxFreeCount(MaxFreeCount), CurrentFreeCount(0)
{
    // Common.dll.c 80681-80690 (0x4d450).
}

void* Heap::Malloc()
{
    // Common.dll.c RVA 0xae580. Pop the free list on a hit; otherwise a fresh malloc().
    if (FreeList != nullptr)
    {
        FFreeBlock* Block = FreeList;
        FreeList = Block->Next;
        --CurrentFreeCount;
        return Block;
    }
    return malloc(ElementSize);
}

void Heap::Free(void* Ptr)
{
    // Common.dll.c RVA 0xae5a0. If the pool is full (MaxFreeCount >= 0 and already at/over
    // cap), just free() straight to the CRT. Otherwise stomp the whole block with the 0xCD
    // debug fill (dwords, then the tail bytes) and push it onto the free list.
    if (MaxFreeCount <= CurrentFreeCount && MaxFreeCount >= 0)
    {
        free(Ptr);
        return;
    }

    memset(Ptr, 0xCD, ElementSize);

    ++CurrentFreeCount;
    FFreeBlock* const Block = (FFreeBlock*)Ptr;
    Block->Next = FreeList;
    FreeList = Block;
}

void Heap::Shrink()
{
    // Common.dll.c RVA 0xae600. Frees every pooled block back to the CRT and empties the pool.
    FFreeBlock* Node = FreeList;
    while (Node != nullptr)
    {
        FFreeBlock* const Next = Node->Next;
        free(Node);
        Node = Next;
    }
    CurrentFreeCount = 0;
    FreeList = nullptr;
}
