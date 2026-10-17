// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// IVP_U_Min_Hash -- Ipion's "hash + tournament tree" priority set: elements are keyed by
// pointer (hash buckets) and ordered by a float value, ties broken by insertion order.
// IVP_Ray_Solver_Min_Hash (vtable .rdata:0639c2c4) collects ray hits in one, and PhySys
// walks them nearest first.
//
// Retail (Common.dll):
//   0634d210 IVP_U_Min_Hash(unsigned size)      0634d3e0 add(void* elem, float value)
//   0634d260 ~IVP_U_Min_Hash()                  0634d470 remove(void* elem)
//   0634d2d0 add_sort(Elem*, int pos)           0634d4f0 remove_min()
//   0634d320 remove_sort(Elem*, int pos)
//   DAT_063edcc8: the process-wide insertion counter (initial 1) stamped into every element.
//
// Layout (0x14 bytes, every member read at these offsets by the functions above):
//   +0x00 size     -- bucket count, a power of two (the constructor's log2 loop has no effect)
//   +0x04 stadium  -- p_calloc(4, 2*size): the tournament tree; [1] is the overall minimum,
//                     [size + pos] is the minimum of bucket pos
//   +0x08 leaves   -- stadium + size (an alias of the leaf row)
//   +0x0c buckets  -- p_calloc(4, size): singly linked chains of IVP_U_Min_Hash_Elem
//   +0x10 counter  -- number of elements

#include <cstdint>

#pragma pack(push, 4)

// operator new(0x10) in add (0634d40a), released through FUN_06391d5a.
struct IVP_U_Min_Hash_Elem
{
    IVP_U_Min_Hash_Elem* next; // +0x00 bucket chain
    float                value; // +0x04 sort key
    int32_t              seq;   // +0x08 insertion counter (DAT_063edcc8), ties: lower first
    void*                elem;  // +0x0c the hashed pointer
};
static_assert(sizeof(IVP_U_Min_Hash_Elem) == 0x10, "IVP_U_Min_Hash_Elem size");

class IVP_U_Min_Hash
{
public:
    uint32_t              size;    // +0x00
    IVP_U_Min_Hash_Elem** stadium; // +0x04
    IVP_U_Min_Hash_Elem** leaves;  // +0x08
    IVP_U_Min_Hash_Elem** buckets; // +0x0c
    int32_t               counter; // +0x10

    // 0634d210 (thiscall, RET 4).
    explicit IVP_U_Min_Hash(uint32_t Size);
    // 0634d260 (thiscall).
    ~IVP_U_Min_Hash();

    IVP_U_Min_Hash(const IVP_U_Min_Hash&) = delete;
    IVP_U_Min_Hash& operator=(const IVP_U_Min_Hash&) = delete;

    // 0634d3e0 (thiscall, RET 8).
    void add(void* Elem, float Value);
    // 0634d470 (thiscall, RET 4).
    void remove(void* Elem);
    // 0634d4f0 (thiscall): removes stadium[1]'s element when there is one.
    void remove_min();

    // Inlined by every caller: `MOV EAX,[hash+4]; MOV EAX,[EAX+4]` then the element's +0xc.
    void* find_min_elem() const
    {
        IVP_U_Min_Hash_Elem* const Min = stadium[1];
        return Min != nullptr ? Min->elem : nullptr;
    }

private:
    // 0634d2d0 (thiscall, RET 8).
    void add_sort(IVP_U_Min_Hash_Elem* Elem, int32_t Pos);
    // 0634d320 (thiscall, RET 8).
    void remove_sort(IVP_U_Min_Hash_Elem* Removed, int32_t Pos);
};
static_assert(sizeof(IVP_U_Min_Hash) == 0x14, "IVP_U_Min_Hash size");

#pragma pack(pop)
