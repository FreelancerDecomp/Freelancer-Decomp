// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP_Hash bodies (06356a40 / 06356a70 / 06356ac0 / 06356b40) and the string
// compare FUN_063569e0 next to them. Moved verbatim from
// src/Common/common_stubs.cpp (via common_stubs_a3.cpp) on 2026-10-14
// (claude-code:cmn-a3); the raw extern "C" FUN_ entry points became the
// IVP_Hash members declared in ivp_hash.h. No logic changed in the move.

#include "ivp_hash.h"

#include "ivp_crc32.h"

#include <cstdlib>
#include <cstring>

// ============================================================================
// d230:cluster-063568a0 pass (2026-09-10): items 3255/3257/3258/3259/3260/
// 3261 (Common.dll.txt:437863-438212). A SEPARATE hash-table implementation
// from IVP_VHash_Store (ivp_vhash.cpp) -- this one is a classic
// fixed-key-length chained (linked-list-bucket) hash table, not
// open-addressed, and its entries own malloc'd copies of their own key bytes
// rather than storing a bare 32-bit key. It reuses the SAME CRC32 table
// global (g_Crc32Table_063edcdc, confirmed byte-identical: both read
// &DAT_063edcdc) but hashes over a caller-fixed number of KEY BYTES (a
// string/blob, not a single 4-byte int) via its own byte-at-a-time loop
// (Common.dll.txt:06356ad7-06356af4 / 06356b56-06356b73), so it gets its own
// small helper below rather than reusing HashKey_063edcdc.
//
// Layout (16 bytes), fields confirmed against FUN_06356a40's own ctor writes
// and cross-checked by every read site in FUN_06356ac0/FUN_06356a70/
// FUN_06356b40:
//   +0x00 KeyLength    number of bytes hashed/compared per key (same for
//                       every key in a given table instance -- a
//                       FIXED-length-key table, not null-terminated strings).
//   +0x04 BucketCount  divisor for `hash % BucketCount`; also the element
//                       count passed to the ctor's calloc call.
//   +0x08 DefaultValue returned by Find on a miss.
//   +0x0c Buckets      calloc'd BucketCount-slot array of chain-head
//                       pointers (IVP_Hash_Elem*).
// Node layout (KeyLength+0xc bytes, malloc'd per insert -- retail
// over-allocates by 4 bytes past the 8-byte header + KeyLength payload it
// actually uses; reproduced 1:1, not "fixed" as a bug):
//   +0x00 Next   chain link.
//   +0x04 Value  caller payload.
//   +0x08 Key[]  KeyLength raw bytes, memcpy'd in by Add.
// ============================================================================

struct IVP_Hash_Elem // formerly FixedKeyHashNode_06356a40
{
    IVP_Hash_Elem* Next; // +0x00
    unsigned Value;      // +0x04
    // +0x08: KeyLength raw key bytes follow inline (see the
    // malloc(KeyLength+0xc) call in IVP_Hash::Add below).
    unsigned char* KeyBytes()
    {
        return (unsigned char*)(this + 1);
    }
};
static_assert(sizeof(IVP_Hash_Elem) == 0x8, "IVP_Hash_Elem key bytes start at +0x08");

namespace
{
    // .text:06356ad7-06356af4 (inlined again at 06356b56-06356b73 for Add): CRC-32
    // over Length key bytes, starting from 0xffffffff, no final inversion. A
    // Length <= 0 hashes nothing (`LEA ECX,[EBX-1]; TEST ECX,ECX; JL`).
    unsigned HashKeyBytes(const unsigned char* Key, int32_t Length)
    {
        unsigned Crc = 0xffffffffu;
        for (int32_t I = 0; I < Length; ++I)
        {
            Crc = (Crc >> 8) ^ g_Crc32Table_063edcdc[(Key[I] ^ Crc) & 0xff];
        }
        return Crc;
    }
}

// FUN_06356a40 (item 3258, Common.dll.txt:437995-438019, 16 insns, tiny).
// Constructor. `param_1`=BucketCount, `param_2`=KeyLength, `param_3`=
// DefaultValue (stack-arg order confirmed against the raw asm's own
// `MOV [ESI+4],EAX(param_1)` / `MOV [ESI],ECX(param_2)` /
// `MOV [ESI+8],EDX(param_3)` writes). Buckets = calloc(4, BucketCount) --
// FUN_06343f80 is calloc(count,size) (established convention, see
// IVP_VHash_Store's own ctor); swapping which literal argument is "count" vs
// "size" doesn't change the allocated byte total (4*BucketCount either way),
// so this is a faithful port regardless of that ambiguity.
IVP_Hash::IVP_Hash(int32_t Size, int32_t KeySize, unsigned NotFoundValue) // formerly extern "C" FUN_06356a40
{
    BucketCount = Size;                                 // 06356a52
    KeyLength = KeySize;                                // 06356a55
    DefaultValue = NotFoundValue;                       // 06356a57
    Buckets = (IVP_Hash_Elem**)calloc(4, (size_t)Size); // 06356a5a FUN_06343f80
}

// FUN_06356a70 (item 3259, Common.dll.txt:438026-438073, 36 insns). Frees
// every chain node in every bucket, then frees the Buckets array itself and
// nulls the field (matches retail's own final `MOV [param_1+0xc],0`). Does
// NOT free `this` and does NOT touch KeyLength/BucketCount/DefaultValue --
// operator delete (or the stack frame) releases the object separately.
IVP_Hash::~IVP_Hash() // formerly extern "C" FUN_06356a70
{
    for (int32_t I = 0; I < BucketCount; ++I) // 06356a74-06356aa3 (signed, count re-read)
    {
        IVP_Hash_Elem* Node = Buckets[I];
        while (Node != nullptr)
        {
            IVP_Hash_Elem* Next = Node->Next;
            free(Node); // FUN_06343fb0
            Node = Next;
        }
    }
    if (Buckets != nullptr)
    {
        free(Buckets); // FUN_06343fb0
        Buckets = nullptr;
    }
}

// FUN_06356ac0 (item 3260, Common.dll.txt:438077-438137, 51 insns). Find:
// hash the first KeyLength bytes of `param_1`, walk the matching bucket's
// chain comparing KeyLength bytes exactly (retail's `CMPSB.REPE`), return
// the matching node's Value or DefaultValue on a miss/empty chain.
unsigned IVP_Hash::Find(const void* Key) // formerly extern "C" FUN_06356ac0
{
    const unsigned char* KeyBytes = (const unsigned char*)Key;
    const unsigned Hash = HashKeyBytes(KeyBytes, KeyLength);
    IVP_Hash_Elem* Node = Buckets[Hash % (unsigned)BucketCount]; // 06356af6-06356afe (unsigned DIV)
    while (Node != nullptr)
    {
        if (memcmp(Node->KeyBytes(), KeyBytes, (size_t)KeyLength) == 0) // 06356b05-06356b12 REPE CMPSB
        {
            return Node->Value;
        }
        Node = Node->Next;
    }
    return DefaultValue;
}

// FUN_06356b40 (item 3261, Common.dll.txt:438153-438211, 50 insns). Add:
// unconditional head-insert into the target bucket's chain (no duplicate-key
// check, matching retail exactly -- a repeat key just shadows the earlier
// node, which becomes unreachable and leaks, same as retail). Allocates
// KeyLength+0xc bytes (see the node-layout comment above for the 4
// unexplained-but-harmless extra bytes) and memcpy's the key in.
void IVP_Hash::Add(const void* Key, unsigned Value) // formerly extern "C" FUN_06356b40
{
    const unsigned char* KeyBytes = (const unsigned char*)Key;
    const unsigned Hash = HashKeyBytes(KeyBytes, KeyLength);
    const unsigned BucketIndex = Hash % (unsigned)BucketCount;          // 06356b75-06356b7e

    IVP_Hash_Elem* Node = (IVP_Hash_Elem*)malloc((size_t)KeyLength + 0xc); // 06356b80 FUN_06343f70
    memcpy(Node->KeyBytes(), KeyBytes, (size_t)KeyLength);                // 06356b85-06356b9a

    Node->Next = Buckets[BucketIndex]; // 06356ba5
    Buckets[BucketIndex] = Node;       // 06356bb0
    Node->Value = Value;               // 06356bb4
}

// FUN_063569e0 (item 3257, .text:063569e0-06356a32, cdecl). Three-way compare
// of two null-checked strings: 0 when equal, -1 / 1 at the first byte that
// differs (bytes compared unsigned: `CMP DL,BL` then `SBB EAX,EAX; SBB EAX,-1`).
// A null first string returns 1 when the second is non-null (0 when both are
// null); a non-null first string against a null second returns -1. The loop is
// the inlined CRT strcmp, two bytes per pass. Caller: LAB_06361580,
// IVP_U_Active_Value_Hash::Compare (ivp_active_value.cpp).
int IVP_CompareStringsNullsLast(const char* First, const char* Second) // formerly extern "C" FUN_063569e0
{
    if (First == nullptr)                      // 063569e4-063569f3
    {
        return Second != nullptr ? 1 : 0;
    }
    if (Second == nullptr)                     // 063569f4-06356a00
    {
        return -1;
    }
    const unsigned char* A = (const unsigned char*)First;
    const unsigned char* B = (const unsigned char*)Second;
    for (;;)                                   // 06356a02-06356a24
    {
        if (A[0] != B[0])
        {
            return A[0] < B[0] ? -1 : 1;       // 06356a2b-06356a2e
        }
        if (A[0] == 0)
        {
            return 0;
        }
        if (A[1] != B[1])
        {
            return A[1] < B[1] ? -1 : 1;
        }
        if (A[1] == 0)
        {
            return 0;
        }
        A += 2;
        B += 2;
    }
}
