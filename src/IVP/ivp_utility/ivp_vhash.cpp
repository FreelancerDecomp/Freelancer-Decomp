// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP_VHash / IVP_VHash_Store bodies and the CRC-32 table they share with
// IVP_Hash and the active-value hash. Moved verbatim from
// src/Common/common_stubs.cpp (via common_stubs_a3.cpp) on 2026-10-14
// (claude-code:cmn-a3); the raw extern "C" FUN_ entry points became the
// members declared in ivp_vhash.h. No arithmetic, branch or allocation
// changed in the move.

#include "ivp_vhash.h"

#include "ivp_crc32.h"

#include <cstdlib>

// ============================================================================
// The CRC-32 table (retail DAT_063edcdc), filled by a dynamic initializer.
// This TU takes common_stubs.cpp's place at the top of Common.vcxproj, so the
// initializer keeps its former relative order (no load-time reader exists).
// ============================================================================

namespace
{
    unsigned InitCrc32Table_063edcdc(unsigned (&table)[256])
    {
        for (unsigned i = 0; i < 256; ++i)
        {
            unsigned c = i;
            for (int k = 0; k < 8; ++k)
            {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        return 0;
    }
}

unsigned g_Crc32Table_063edcdc[256];

namespace
{
    unsigned g_Crc32Table_063edcdc_init = InitCrc32Table_063edcdc(g_Crc32Table_063edcdc);
}

// ============================================================================
// IVP_VHash (the "Robin Hood value-set"), Common.dll.txt:430291-430720.
//
// Distinct from IVP_VHash_Store below: this one stores no separate key/value
// pair, only a single caller-supplied element pointer per slot, tagged by a
// caller-supplied 32-bit hash. Identified and traced by lane D102
// (2026-09-09, decomp-tracker items 3170-3176) from Common.dll.txt and the
// Ghidra pseudocode (Common.dll.c:208870-209040).
//
// Slot array (+0x0c), 8 bytes per slot:
//   +0x0 HashIndex  caller's hash OR'd with 0x80000000 ("occupied" marker);
//                   only ever compared against another already-tagged hash,
//                   so the OR is applied again at each compare site exactly
//                   like retail
//   +0x4 Elem       caller's element; null means "slot empty"
// The count dword (+0x08) keeps the element count in its low 24 bits and a
// "not owned / already released" flag in its top byte.
//
// The two "carried displacement" primitives (Rehash, AddElem) and the two
// query primitives (RemoveElem, FindElem) are transliterated line-for-line
// against Common.dll.c's variable-tracked pseudocode (not "cleaned up"):
// Robin Hood carried-displacement swap order, and the backward-shift
// deletion's wraparound-boundary refDist precomputation, are both easy to
// get subtly wrong from a paraphrase.
//
// RemoveElem reproduces one retail quirk: if its probe loop reaches an empty
// slot without a comparator hit (the caller asked to remove an element that
// is not present), retail writes through a literal null pointer
// (Common.dll.txt:430561, `MOV dword ptr [DAT_00000000],0x0`) -- an
// intentional crash/assert-via-fault for a contract violation, confirmed
// against the raw listing. Reproduced the same way. FindElem, by contrast,
// has a real empty-slot bailout in retail and returns null.
// ============================================================================

namespace
{
    struct FVHashElem
    {
        unsigned hashTag; // +0x0
        void* value;      // +0x4
    };

    // Common.dll.txt:430291-430720's shared "increment/decrement the low 24
    // bits of a count-and-flags dword while preserving its top flag byte"
    // idiom (`(new+-1 ^ old) & 0xffffff ^ new`, generalized here to any
    // desired new low-24-bit value rather than literally +1/-1, matching
    // Rehash's own use of it to restore the pre-rehash count). Folded into one
    // helper since all four call sites are verbatim-identical bit tricks.
    unsigned ReplaceLow24PreserveTopByte(unsigned dwordWithFlags, unsigned newLow24)
    {
        return ((dwordWithFlags ^ newLow24) & 0xffffffu) ^ dwordWithFlags;
    }
}

// Common.dll.c 0x6352320 / Common.dll.txt:430367-430440 (Common.dll.c
// 208870-208905). Grows the table to exactly `param_1` buckets and rehashes
// every occupied old entry back in through AddElem, then frees the old bucket
// array unless the "not owned" flag is set (post-grow, that flag is always
// cleared -- a freshly calloc'd table is always owned). The rehash loop
// temporarily bumps the count once per re-inserted entry (via AddElem's own
// increment); the final ReplaceLow24PreserveTopByte call restores the
// count's low 24 bits back to their pre-rehash value (the actual element
// count is unchanged by growing) while keeping whatever flag byte resulted
// from the rehash loop.
void IVP_VHash::Rehash(int param_1) // formerly extern "C" FUN_06352320
{
    int oldMask = SizeMm;
    FVHashElem* oldBuckets = static_cast<FVHashElem*>(Elems);
    SizeMm = param_1 - 1;
    Elems = calloc(static_cast<size_t>(param_1), 0x8); // FUN_06343f80
    unsigned preRehashCount = NElemsAndFlags;

    if (oldMask >= 0)
    {
        int n = oldMask + 1;
        FVHashElem* entry = oldBuckets;
        do
        {
            if (entry->value != nullptr)
            {
                AddElem(entry->value, entry->hashTag);
            }
            ++entry;
            --n;
        } while (n != 0);
    }

    NElemsAndFlags = ReplaceLow24PreserveTopByte(NElemsAndFlags, preRehashCount & 0xffffffu);
    if (((NElemsAndFlags & 0xff000000u) == 0) && (oldBuckets != nullptr))
    {
        free(oldBuckets); // FUN_06343fb0
    }
    NElemsAndFlags &= 0xffffffu;
}

// Common.dll.c 0x63523d0 / Common.dll.txt:430453-430531 (Common.dll.c
// 208909-208951). Insert-with-auto-grow: doubles the table first (to
// `2*mask+2` buckets) if the load factor would reach 50%, then performs the
// raw Robin Hood carried-displacement insert -- probing forward from
// `hash & mask`, and at each occupied slot swapping the incoming (value,
// hash) in for whichever resident has probed a shorter distance from its
// own ideal bucket (`curIdx < residentIdeal`), carrying the displaced
// resident forward to keep probing. `param_1`=value, `param_2`=hash (already
// tagged with 0x80000000 by the caller, matching the multiplicative hash
// `(x*0x3e9>>0x10)+x*0x4b | 0x80000000` seen at every call site in
// Common.dll.c).
void IVP_VHash::AddElem(void* param_1, unsigned param_2) // formerly extern "C" FUN_063523d0
{
    int mask = SizeMm;
    int doubledCount = static_cast<int>((NElemsAndFlags & 0xffffffu)) * 2;
    if (doubledCount != mask && mask <= doubledCount)
    {
        Rehash(mask * 2 + 2);
    }

    void* value = param_1;
    unsigned hash = param_2;

    unsigned mask2 = static_cast<unsigned>(SizeMm);
    unsigned idx = mask2 & hash;
    FVHashElem* buckets = static_cast<FVHashElem*>(Elems);
    FVHashElem* slot = &buckets[idx];
    NElemsAndFlags = ReplaceLow24PreserveTopByte(NElemsAndFlags, (NElemsAndFlags + 1) & 0xffffffu);

    void* residentValue = slot->value;
    unsigned curIdx = idx;
    while (residentValue != nullptr)
    {
        unsigned residentHash = slot->hashTag;
        unsigned residentIdeal = residentHash & mask2;
        if (static_cast<int>(curIdx) < static_cast<int>(residentIdeal))
        {
            slot->value = value;
            slot->hashTag = hash;
            curIdx = residentIdeal;
            value = residentValue;
            hash = residentHash;
        }
        mask2 = static_cast<unsigned>(SizeMm);
        idx = (idx + 1) & mask2;
        buckets = static_cast<FVHashElem*>(Elems);
        slot = &buckets[idx];
        residentValue = slot->value;
    }

    slot->value = value;
    slot->hashTag = hash;
}

// Common.dll.c 0x6352480 / Common.dll.txt:430539-430650 (Common.dll.c
// 208957-209014). Find-and-remove: probes forward from `hash & mask`,
// calling the installed comparator (vtable slot 0, Compare(storedValue,
// queryArg), thiscall with `this` = the container itself) on every hash-tag
// match, then performs a full backward-shift Robin Hood deletion once found
// -- the `refDist` precomputation from the wraparound-boundary bucket
// (index == mask) lets the shift-eligibility test stay correct across the
// array's wraparound, exactly mirroring IVP_VHash_Store's erase below.
// `param_1`=queryArg, `param_2`=hash. See the cluster note above for the
// intentional null-deref if the element being removed is not present (a
// retail contract-violation assert, not reproduced as a bounds check).
void* IVP_VHash::RemoveElem(void* param_1, unsigned param_2) // formerly extern "C" FUN_06352480
{
    unsigned idx = param_2 & static_cast<unsigned>(SizeMm);
    FVHashElem* slot;
    for (;;)
    {
        FVHashElem* buckets = static_cast<FVHashElem*>(Elems);
        slot = &buckets[idx];
        if (slot->value == nullptr)
        {
            // Retail: MOV dword ptr [DAT_00000000],0x0 -- intentional
            // null-pointer-write assert/crash on a contract violation
            // (removing a value that was never inserted). See cluster note.
            *reinterpret_cast<volatile int*>(0) = 0;
        }
        if (((slot->hashTag | 0x80000000u) == param_2))
        {
            if (Compare(slot->value, param_1) == 1)
            {
                break;
            }
        }
        idx = (idx + 1) & static_cast<unsigned>(SizeMm);
    }

    void* removedValue = slot->value;
    FVHashElem* buckets = static_cast<FVHashElem*>(Elems);
    unsigned mask = static_cast<unsigned>(SizeMm);
    NElemsAndFlags = ReplaceLow24PreserveTopByte(NElemsAndFlags, (NElemsAndFlags - 1) & 0xffffffu);

    unsigned refDist = mask;
    if (buckets[mask].value != nullptr)
    {
        refDist = buckets[mask].hashTag & mask;
    }

    unsigned vacant = idx;
    unsigned scan = (idx + 1) & mask;
    while (buckets[scan].value != nullptr)
    {
        unsigned scanIdeal = mask & buckets[scan].hashTag;
        bool doShift = false;
        if (static_cast<int>(vacant) < static_cast<int>(scan))
        {
            if (((static_cast<int>(refDist) <= static_cast<int>(scanIdeal)) &&
                 (scanIdeal != refDist || static_cast<int>(scan) < static_cast<int>(scanIdeal))) ||
                (static_cast<int>(scanIdeal) <= static_cast<int>(vacant)))
            {
                doShift = true;
            }
        }
        else
        {
            if (vacant != mask)
            {
                break;
            }
            if (static_cast<int>(refDist) <= static_cast<int>(scanIdeal))
            {
                doShift = true;
            }
        }

        if (doShift)
        {
            buckets[vacant] = buckets[scan];
            vacant = scan;
        }

        mask = static_cast<unsigned>(SizeMm);
        buckets = static_cast<FVHashElem*>(Elems);
        scan = (scan + 1) & mask;
    }

    buckets[vacant].value = nullptr;
    buckets[vacant].hashTag = 0;
    return removedValue;
}

// Common.dll.c 0x6352580 / Common.dll.txt:430662-430720 (Common.dll.c
// 209018-209040, decomp-tracker item 2976's former blocker).
// Find-only: same probe+comparator loop as RemoveElem above but, unlike
// that function, has a real empty-slot bailout (returns null) instead of
// asserting -- this is the plain Get()/Contains() entry point, which retail
// calls speculatively (e.g. to check membership before deciding whether to
// insert or remove), so hitting an empty slot here is an expected outcome,
// not a contract violation.
void* IVP_VHash::FindElem(void* param_1, unsigned param_2) // formerly extern "C" FUN_06352580
{
    unsigned idx = param_2 & static_cast<unsigned>(SizeMm);
    FVHashElem* buckets = static_cast<FVHashElem*>(Elems);
    FVHashElem* slot = &buckets[idx];
    void* value = slot->value;

    for (;;)
    {
        if (value == nullptr)
        {
            return nullptr;
        }
        if ((slot->hashTag | 0x80000000u) == param_2)
        {
            if (Compare(value, param_1) != 0)
            {
                break;
            }
        }
        idx = (idx + 1) & static_cast<unsigned>(SizeMm);
        buckets = static_cast<FVHashElem*>(Elems);
        slot = &buckets[idx];
        value = slot->value;
    }

    return slot->value;
}

// FUN_063522f0 (the non-deleting ~IVP_VHash) is IVP_VHash::~IVP_VHash in ivp_vhash.h; the
// derived FUN_0636cf90 (vtable 063a58f8 stamp, JMP 063522f0) is
// FLedgeSoupPointHash::~FLedgeSoupPointHash in ivp_surface_builder_ledge_soup.cpp. The raw
// extern "C" copies that used to stand here had no caller and were deleted 2026-10-14.

// Common.dll.txt:448769-448793 (decomp-tracker item 3359). Standalone,
// separately-CALLed copy of the byte-at-a-time CRC-32-over-4-raw-bytes hash
// that IVP_VHash_Store inlines at four call sites (HashKey_063edcdc(key) |
// 0x80000000) -- identical CRC table (DAT_063edcdc), identical 0xffffffff
// init, identical final "occupied" tag. Confirmed byte-for-byte against the
// raw listing (PUSH ESI / OR EAX,0xffffffff / MOV ESI,4 / 4x table-lookup
// loop over the 4 raw bytes of the input dword / OR EAX,0x80000000 / POP ESI
// / RET 4). Caller: IVP_Cluster_Manager's table lookups
// (ivp_cluster_manager.cpp), which feed the result to IVP_VHash::FindElem.
unsigned __fastcall IVP_VHash_HashPointer(unsigned param_1) // formerly extern "C" FUN_0635d4d0
{
    return HashKey_063edcdc(param_1) | 0x80000000u;
}

// ============================================================================
// IVP_VHash_Store, 0x63525f0-0x6352a10: a pointer/handle -> pointer map
// (Robin Hood open addressing, keys hashed through the CRC-32 table above).
//
// Distinct from the multiplicative-hash (`(x*0x3e9>>0x10)+x*0x4b`) IVP_VHash
// value-set above (vtable'd, 8-byte {hash, value} slots, dozens of unrelated
// call sites).
//
// Header layout (20 bytes): see ivp_vhash.h. A null `Value` means "slot
// empty" throughout (find/grow use it as the occupied test); insert instead
// tests `Key != null` for the same thing -- both hold given this container's
// actual keys/values (pointers, refcounts) are never legitimately zero, and
// each raw function matches Ghidra's own choice of which field it happened
// to still have in a register.
// ============================================================================

namespace
{
    struct FVHashStoreElem
    {
        unsigned hash; // +0x0
        void* key;     // +0x4
        void* value;   // +0x8
    };
}

// Common.dll.c 0x63525f0: constructor. Allocates `capacity` buckets up front
// (no small-buffer optimization at construction time).
IVP_VHash_Store::IVP_VHash_Store(int param_1) // formerly extern "C" FUN_063525f0
{
    SizeMm = param_1 - 1;
    Size = param_1;
    NElems = 0;
    Field_0x10 = nullptr;
    // FUN_06343f80 is calloc(count, size) -- it mallocs then zero-fills via
    // rep stosd/stosb (confirmed against Common.dll.txt ~404900-404919), not
    // a plain malloc. The zero-fill is load-bearing here: this container's
    // "slot empty" test throughout is `value == 0` (see the header comment
    // above), so an unzeroed bucket array would make find()/grow() read
    // garbage as live entries.
    ElemsStore = calloc(static_cast<size_t>(param_1), 0xc);
}

// Common.dll.c 0x6352620 (aka thunk_FUN_06352620, same body folded by
// /OPT:ICF): destructor -- frees the bucket array unless it is the
// (always-null-here) Field_0x10 pointer.
IVP_VHash_Store::~IVP_VHash_Store() // formerly extern "C" FUN_06352620
{
    if (ElemsStore != Field_0x10 && ElemsStore != nullptr)
    {
        free(ElemsStore); // FUN_06343fb0
        ElemsStore = nullptr;
    }
}

// Common.dll.c 0x6352650: doubles the table and rehashes every occupied
// entry through the raw insert, then frees the old bucket array. Called
// from the raw insert when load factor would reach 50%.
void IVP_VHash_Store::Rehash(int param_1) // formerly extern "C" FUN_06352650
{
    FVHashStoreElem* oldBuckets = static_cast<FVHashStoreElem*>(ElemsStore);
    int oldCapacity = Size;
    Size = param_1;
    SizeMm = param_1 - 1;
    ElemsStore = calloc(static_cast<size_t>(param_1), 0xc); // FUN_06343f80 is calloc, not malloc -- see note in the ctor
    int savedCount = NElems;
    if (oldCapacity > 0)
    {
        for (int i = 0; i < oldCapacity; ++i)
        {
            if (oldBuckets[i].value != nullptr)
            {
                AddElem(oldBuckets[i].key, oldBuckets[i].value, oldBuckets[i].hash);
            }
        }
    }
    NElems = savedCount;
    if (oldBuckets != Field_0x10 && oldBuckets != nullptr)
    {
        free(oldBuckets); // FUN_06343fb0
    }
}

// Common.dll.c 0x63526d0: CRC32-hash a raw 4-byte key and unconditionally
// insert (key, value), tagging the hash as "generated" (top bit set).
void IVP_VHash_Store::AddElem(void* param_1, void* param_2) // formerly extern "C" FUN_063526d0
{
    unsigned hash = HashKey_063edcdc((unsigned)param_1) | 0x80000000;
    AddElem(param_1, param_2, hash);
}

// Common.dll.c 0x6352720: raw Robin Hood insert. `param_1`/`param_3` are the
// caller-supplied key and its pre-hashed probe value (top bit already set);
// `param_2` is the payload. Transliterated line-for-line against
// Common.dll.c:209133-209179 (rather than "cleaned up") since Robin Hood
// swap/carry order is easy to get subtly wrong.
void IVP_VHash_Store::AddElem(void* param_1, void* param_2, unsigned param_3) // formerly extern "C" FUN_06352720
{
    if (Size <= NElems * 2)
    {
        Rehash(Size * 2);
    }
    FVHashStoreElem* base = static_cast<FVHashStoreElem*>(ElemsStore);
    unsigned uVar7 = static_cast<unsigned>(SizeMm);
    unsigned uVar5 = uVar7 & param_3;
    FVHashStoreElem* puVar1 = &base[uVar5];
    NElems = NElems + 1;
    void* uVar2 = puVar1->key;
    unsigned uVar6 = uVar5;
    void* uVar4 = param_2;
    while (uVar2 != nullptr)
    {
        unsigned uVar3 = puVar1->hash;
        uVar7 = uVar3 & uVar7;
        param_2 = uVar4;
        if (static_cast<int>(uVar6) < static_cast<int>(uVar7))
        {
            param_2 = puVar1->value;
            puVar1->key = param_1;
            puVar1->value = uVar4;
            puVar1->hash = param_3;
            uVar6 = uVar7;
            param_1 = uVar2;
            param_3 = uVar3;
        }
        uVar7 = static_cast<unsigned>(SizeMm);
        uVar5 = (uVar5 + 1) & uVar7;
        base = static_cast<FVHashStoreElem*>(ElemsStore);
        puVar1 = &base[uVar5];
        uVar4 = param_2;
        uVar2 = puVar1->key;
    }
    puVar1->key = param_1;
    puVar1->value = uVar4;
    puVar1->hash = param_3;
}

// Common.dll.c 0x63527e0: CRC32-hash a raw 4-byte key and erase it.
// Ghidra types this void (matching its only caller's usage), but the
// underlying raw erase does return the removed value via EAX at the retail
// ABI level, so this is typed to return it too rather than discard it.
void* IVP_VHash_Store::RemoveElem(void* param_1) // formerly extern "C" FUN_063527e0
{
    unsigned hash = HashKey_063edcdc((unsigned)param_1) | 0x80000000;
    return RemoveElem(param_1, hash);
}

// Common.dll.c 0x6352830: raw Robin Hood erase (backward-shift deletion).
// Returns the removed entry's value. Transliterated line-for-line against
// Common.dll.c:209206-209264, including the `refDist` precomputation from
// the wraparound-boundary bucket (index == mask) that the retail code uses
// to get probe-distance comparisons right across the array wraparound.
void* IVP_VHash_Store::RemoveElem(void* param_1, unsigned param_2) // formerly extern "C" FUN_06352830
{
    FVHashStoreElem* buckets = static_cast<FVHashStoreElem*>(ElemsStore);
    unsigned mask = static_cast<unsigned>(SizeMm);
    unsigned probe = param_2;
    unsigned foundIdx;
    for (;;)
    {
        probe = mask & probe;
        if (((buckets[probe].hash | 0x80000000) == param_2) && (buckets[probe].key == param_1))
        {
            foundIdx = probe;
            break;
        }
        probe = probe + 1;
    }
    void* removedValue = buckets[foundIdx].value;
    NElems = NElems - 1;

    unsigned uVar7 = foundIdx;
    unsigned uVar5 = mask;
    unsigned refDist = uVar5;
    if (buckets[uVar5].key != nullptr)
    {
        refDist = buckets[uVar5].hash & uVar5;
    }
    unsigned uVar8 = (uVar7 + 1) & uVar5;
    FVHashStoreElem* puVar2 = &buckets[uVar8];
    while (puVar2->key != nullptr)
    {
        unsigned uVar9 = puVar2->hash & uVar5;
        bool doShift = false;
        if (static_cast<int>(uVar7) < static_cast<int>(uVar8))
        {
            if (((static_cast<int>(refDist) <= static_cast<int>(uVar9)) &&
                 (uVar9 != refDist || static_cast<int>(uVar8) < static_cast<int>(uVar9))) ||
                (static_cast<int>(uVar9) <= static_cast<int>(uVar7)))
            {
                doShift = true;
            }
        }
        else
        {
            if (uVar7 != uVar5)
            {
                break;
            }
            if (static_cast<int>(refDist) <= static_cast<int>(uVar9))
            {
                doShift = true;
            }
        }
        if (doShift)
        {
            buckets[uVar7] = *puVar2;
            uVar7 = uVar8;
        }
        mask = static_cast<unsigned>(SizeMm);
        uVar5 = mask;
        buckets = static_cast<FVHashStoreElem*>(ElemsStore);
        uVar8 = (uVar8 + 1) & uVar5;
        puVar2 = &buckets[uVar8];
    }
    buckets[uVar7].key = nullptr;
    buckets[uVar7].hash = 0;
    return removedValue;
}

// Common.dll.c 0x6352930: CRC32-hash a raw 4-byte key and look it up.
// Ghidra types this void, but its callers capture the return value, so the
// underlying raw find's result genuinely propagates through this tail call at
// the retail ABI level -- typed accordingly.
void* IVP_VHash_Store::FindElem(void* param_1) // formerly extern "C" FUN_06352930
{
    unsigned hash = HashKey_063edcdc((unsigned)param_1) | 0x80000000;
    return FindElem(param_1, hash);
}

// Common.dll.c 0x6352980: raw find. The probe count is bounded by the
// capacity exactly as in retail (`CMP EDX,EDI; JL`) -- not a port guard.
void* IVP_VHash_Store::FindElem(void* param_1, unsigned param_2) // formerly extern "C" FUN_06352980
{
    int probes = 0;
    if (Size > 0)
    {
        unsigned idx = param_2;
        do
        {
            idx = static_cast<unsigned>(SizeMm) & idx;
            FVHashStoreElem* buckets = static_cast<FVHashStoreElem*>(ElemsStore);
            FVHashStoreElem* slot = &buckets[idx];
            probes = probes + 1;
            if (slot->value == nullptr)
            {
                return nullptr;
            }
            if (((slot->hash | 0x80000000) == param_2) && (slot->key == param_1))
            {
                return slot->value;
            }
            idx = idx + 1;
        } while (probes < Size);
    }
    return nullptr;
}

// Common.dll.c 0x63529e0: update the value for an existing key; a no-op
// (not an insert) if the key is not present. Used by
// IVP_Controller_Phantom's contact dispatch to bump/decrement a per-pair
// reference count once the pair is already registered.
void IVP_VHash_Store::ChangeElem(void* param_1, void* param_2) // formerly extern "C" FUN_063529e0
{
    unsigned crc = HashKey_063edcdc((unsigned)param_1);
    if (Size > 0)
    {
        unsigned hash = crc | 0x80000000;
        unsigned idx = hash;
        int probes = 0;
        for (;;)
        {
            idx = static_cast<unsigned>(SizeMm) & idx;
            FVHashStoreElem* buckets = static_cast<FVHashStoreElem*>(ElemsStore);
            FVHashStoreElem* slot = &buckets[idx];
            probes = probes + 1;
            if (slot->value == nullptr)
            {
                return; // key not present -- update-only, never inserts
            }
            if (((slot->hash | 0x80000000) == hash) && (slot->key == param_1))
            {
                slot->value = param_2;
                return;
            }
            idx = idx + 1;
            if (Size <= probes)
            {
                return;
            }
        }
    }
}
